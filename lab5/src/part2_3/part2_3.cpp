#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <cassert>
#include <chrono>
#include "common_pipeline.h"

const int CHUNK_SIZE = 256;
const int NUM_SLOTS = 16;

struct Chunk {
    int count;
    PixelMsg pixels[CHUNK_SIZE];
};

struct SharedQueue {
    Chunk slots[NUM_SLOTS];
    int in;
    int out;
    sem_t mutex;
    sem_t empty;
    sem_t full;

    void init() {
        in = 0;
        out = 0;
        sem_init(&mutex, 1, 1);
        sem_init(&empty, 1, NUM_SLOTS);
        sem_init(&full, 1, 0);
    }

    void destroy() {
        sem_destroy(&mutex);
        sem_destroy(&empty);
        sem_destroy(&full);
    }

    void push(const Chunk& c) {
        sem_wait(&empty);
        sem_wait(&mutex);
        slots[in] = c;
        in = (in + 1) % NUM_SLOTS;
        sem_post(&mutex);
        sem_post(&full);
    }

    Chunk pop() {
        sem_wait(&full);
        sem_wait(&mutex);
        Chunk c = slots[out];
        out = (out + 1) % NUM_SLOTS;
        sem_post(&mutex);
        sem_post(&empty);
        return c;
    }
};

struct SharedMemory {
    SharedQueue q1;
    SharedQueue q2;
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_image> <output_image> [iterations]\n";
        return 1;
    }

    char* input_file = argv[1];
    char* output_file = argv[2];
    int iterations = (argc >= 4) ? std::atoi(argv[3]) : 1;

    auto t_start_read = std::chrono::high_resolution_clock::now();
    struct image_t* input_image = read_ppm_file(input_file);
    auto t_end_read = std::chrono::high_resolution_clock::now();

    int width = input_image->width;
    int height = input_image->height;
    int total_pixels = width * height;

    SharedMemory* shm = (SharedMemory*)mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    shm->q1.init();
    shm->q2.init();

    auto t_start_compute = std::chrono::high_resolution_clock::now();

    // Process 1: S1
    pid_t pid1 = fork();
    if (pid1 == 0) {
        for (int iter = 0; iter < iterations; ++iter) {
            Chunk c;
            c.count = 0;
            int seq = 0;
            for (int i = 0; i < height; ++i) {
                for (int j = 0; j < width; ++j) {
                    PixelMsg msg;
                    msg.seq = seq++;
                    compute_S1_pixel(input_image, i, j, msg.r, msg.g, msg.b);
                    c.pixels[c.count++] = msg;

                    if (c.count == CHUNK_SIZE) {
                        shm->q1.push(c);
                        c.count = 0;
                    }
                }
            }
            if (c.count > 0) {
                shm->q1.push(c);
            }
        }
        _exit(0);
    }

    // Process 2: S2
    pid_t pid2 = fork();
    if (pid2 == 0) {
        for (int iter = 0; iter < iterations; ++iter) {
            int received = 0;
            int expected_seq = 0;
            Chunk out_c;
            out_c.count = 0;

            while (received < total_pixels) {
                Chunk in_c = shm->q1.pop();
                for (int idx = 0; idx < in_c.count; ++idx) {
                    PixelMsg in = in_c.pixels[idx];
                    assert(in.seq == expected_seq && "S2 received pixel out of order!");
                    expected_seq++;

                    int i = in.seq / width;
                    int j = in.seq % width;
                    PixelMsg out;
                    out.seq = in.seq;
                    compute_S2_pixel(input_image, i, j, in.r, in.g, in.b, out.r, out.g, out.b);
                    out_c.pixels[out_c.count++] = out;

                    if (out_c.count == CHUNK_SIZE) {
                        shm->q2.push(out_c);
                        out_c.count = 0;
                    }
                }
                received += in_c.count;
            }
            if (out_c.count > 0) {
                shm->q2.push(out_c);
            }
        }
        _exit(0);
    }

    // Process 3: S3 (Parent)
    struct image_t* sharpened_image = allocate_image(width, height);

    for (int iter = 0; iter < iterations; ++iter) {
        int received = 0;
        int expected_seq = 0;

        while (received < total_pixels) {
            Chunk in_c = shm->q2.pop();
            for (int idx = 0; idx < in_c.count; ++idx) {
                PixelMsg in = in_c.pixels[idx];
                assert(in.seq == expected_seq && "S3 received pixel out of order!");
                expected_seq++;

                if (iter == iterations - 1) {
                    int i = in.seq / width;
                    int j = in.seq % width;
                    uint8_t out_r, out_g, out_b;
                    compute_S3_pixel(input_image, i, j, in.r, in.g, in.b, out_r, out_g, out_b);
                    sharpened_image->image_pixels[i][j][0] = out_r;
                    sharpened_image->image_pixels[i][j][1] = out_g;
                    sharpened_image->image_pixels[i][j][2] = out_b;
                }
            }
            received += in_c.count;
        }
    }

    waitpid(pid1, nullptr, 0);
    waitpid(pid2, nullptr, 0);
    auto t_end_compute = std::chrono::high_resolution_clock::now();

    shm->q1.destroy();
    shm->q2.destroy();
    munmap(shm, sizeof(SharedMemory));

    auto t_start_write = std::chrono::high_resolution_clock::now();
    write_ppm_file(output_file, sharpened_image);
    auto t_end_write = std::chrono::high_resolution_clock::now();

    double read_ms = std::chrono::duration<double, std::milli>(t_end_read - t_start_read).count();
    double compute_ms = std::chrono::duration<double, std::milli>(t_end_compute - t_start_compute).count();
    double write_ms = std::chrono::duration<double, std::milli>(t_end_write - t_start_write).count();
    double total_ms = read_ms + compute_ms + write_ms;

    std::cout << "[Part 2.3 - Processes + Shared Memory + Semaphores]\n";
    std::cout << "Order verification: SUCCESS (All " << total_pixels << " pixels received in exact sent order for each of " << iterations << " iterations)\n";
    std::cout << "Read Time: " << read_ms << " ms | Compute Time: " << compute_ms << " ms | Write Time: " << write_ms << " ms | Total Time: " << total_ms << " ms\n";

    return 0;
}

