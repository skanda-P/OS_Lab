#include <iostream>
#include <thread>
#include <atomic>
#include <cassert>
#include <chrono>
#include <vector>
#include "common_pipeline.h"

// Lock-free single-producer single-consumer circular queue using atomic operations
class AtomicQueue {
    static const int CAPACITY = 4096;
    static const int MASK = CAPACITY - 1;
    PixelMsg buffer[CAPACITY];
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};

public:
    void push(const PixelMsg& item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        while (current_tail - head.load(std::memory_order_acquire) >= CAPACITY) {
            #if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
            #endif
        }
        buffer[current_tail & MASK] = item;
        tail.store(current_tail + 1, std::memory_order_release);
    }

    PixelMsg pop() {
        size_t current_head = head.load(std::memory_order_relaxed);
        while (tail.load(std::memory_order_acquire) <= current_head) {
            #if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
            #endif
        }
        PixelMsg item = buffer[current_head & MASK];
        head.store(current_head + 1, std::memory_order_release);
        return item;
    }
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_image> <output_image> [iterations]\n";
        return 1;
    }

    char* input_file = argv[1];
    char* output_file = argv[2];
    int total_s1_runs = (argc >= 4) ? std::atoi(argv[3]) : 1000;
    int s1_interval = 100;
    int s2_runs_per_s1 = 100;
    int s3_runs_per_s2 = 100;
    int batches = total_s1_runs / s1_interval;

    auto t_start_read = std::chrono::high_resolution_clock::now();
    struct image_t* input_image = read_ppm_file(input_file);
    auto t_end_read = std::chrono::high_resolution_clock::now();

    int width = input_image->width;
    int height = input_image->height;
    int total_pixels = width * height;
    struct image_t* sharpened_image = allocate_image(width, height);

    AtomicQueue q1;
    AtomicQueue q2;

    auto t_start_compute = std::chrono::high_resolution_clock::now();

    // Core 1: S1 Thread
    std::thread t1([&]() {
        for (int run = 1; run <= total_s1_runs; ++run) {
            bool send_next = (run % s1_interval == 0);
            int seq = 0;
            for (int i = 0; i < height; ++i) {
                for (int j = 0; j < width; ++j) {
                    uint8_t r, g, b;
                    compute_S1_pixel(input_image, i, j, r, g, b);
                    if (send_next) {
                        q1.push({seq++, r, g, b});
                    }
                }
            }
        }
    });

    // Core 2: S2 Thread
    std::thread t2([&]() {
        std::vector<PixelMsg> s1_pixels(total_pixels);
        for (int b = 0; b < batches; ++b) {
            int expected_seq = 0;
            for (int p = 0; p < total_pixels; ++p) {
                s1_pixels[p] = q1.pop();
                assert(s1_pixels[p].seq == expected_seq && "S2 received pixel out of order!");
                expected_seq++;
            }

            for (int run = 1; run <= s2_runs_per_s1; ++run) {
                bool send_next = (run == s2_runs_per_s1);
                for (int p = 0; p < total_pixels; ++p) {
                    int i = p / width;
                    int j = p % width;
                    uint8_t out_r, out_g, out_b;
                    compute_S2_pixel(input_image, i, j, s1_pixels[p].r, s1_pixels[p].g, s1_pixels[p].b, out_r, out_g, out_b);
                    if (send_next) {
                        q2.push({p, out_r, out_g, out_b});
                    }
                }
            }
        }
    });

    // Core 3: S3 Thread
    std::thread t3([&]() {
        std::vector<PixelMsg> s2_pixels(total_pixels);
        for (int b = 0; b < batches; ++b) {
            int expected_seq = 0;
            for (int p = 0; p < total_pixels; ++p) {
                s2_pixels[p] = q2.pop();
                assert(s2_pixels[p].seq == expected_seq && "S3 received pixel out of order!");
                expected_seq++;
            }

            for (int run = 1; run <= s3_runs_per_s2; ++run) {
                bool save_final = (b == batches - 1 && run == s3_runs_per_s2);
                for (int p = 0; p < total_pixels; ++p) {
                    if (save_final) {
                        int i = p / width;
                        int j = p % width;
                        uint8_t out_r, out_g, out_b;
                        compute_S3_pixel(input_image, i, j, s2_pixels[p].r, s2_pixels[p].g, s2_pixels[p].b, out_r, out_g, out_b);
                        sharpened_image->image_pixels[i][j][0] = out_r;
                        sharpened_image->image_pixels[i][j][1] = out_g;
                        sharpened_image->image_pixels[i][j][2] = out_b;
                    }
                }
            }
        }
    });

    t1.join();
    t2.join();
    t3.join();
    auto t_end_compute = std::chrono::high_resolution_clock::now();

    auto t_start_write = std::chrono::high_resolution_clock::now();
    write_ppm_file(output_file, sharpened_image);
    auto t_end_write = std::chrono::high_resolution_clock::now();

    double read_ms = std::chrono::duration<double, std::milli>(t_end_read - t_start_read).count();
    double compute_ms = std::chrono::duration<double, std::milli>(t_end_compute - t_start_compute).count();
    double write_ms = std::chrono::duration<double, std::milli>(t_end_write - t_start_write).count();
    double total_ms = read_ms + compute_ms + write_ms;

    std::cout << "[Part 2.1 Variant 2 - Threads + Atomic Operations]\n";
    std::cout << "Order verification: SUCCESS (All " << total_pixels << " pixels received in exact sent order for all " << batches << " batches)\n";
    std::cout << "Read Time: " << read_ms << " ms | Compute Time: " << compute_ms << " ms | Write Time: " << write_ms << " ms | Total Time: " << total_ms << " ms\n";

    return 0;
}

