#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <cassert>
#include <chrono>
#include "common_pipeline.h"

const int CHUNK_SIZE = 512;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_image> <output_image> [iterations]\n";
        return 1;
    }

    char* input_file = argv[1];
    char* output_file = argv[2];
    int iterations = (argc >= 4) ? std::atoi(argv[3]) : 1000;

    auto t_start_read = std::chrono::high_resolution_clock::now();
    struct image_t* input_image = read_ppm_file(input_file);
    auto t_end_read = std::chrono::high_resolution_clock::now();

    int width = input_image->width;
    int height = input_image->height;
    int total_pixels = width * height;

    int pipe1[2]; // S1 -> S2
    int pipe2[2]; // S2 -> S3
    if (pipe(pipe1) < 0 || pipe(pipe2) < 0) {
        perror("pipe");
        return 1;
    }

    auto t_start_compute = std::chrono::high_resolution_clock::now();

    // Fork Process 1: S1
    pid_t pid1 = fork();
    if (pid1 == 0) {
        close(pipe1[0]); // close read end of pipe1
        close(pipe2[0]); // close both ends of pipe2
        close(pipe2[1]);

        PixelMsg write_buf[CHUNK_SIZE];
        int buf_count = 0;

        for (int iter = 0; iter < iterations; ++iter) {
            int seq = 0;
            for (int i = 0; i < height; ++i) {
                for (int j = 0; j < width; ++j) {
                    PixelMsg msg;
                    msg.seq = seq++;
                    compute_S1_pixel(input_image, i, j, msg.r, msg.g, msg.b);
                    write_buf[buf_count++] = msg;

                    if (buf_count == CHUNK_SIZE) {
                        write_all(pipe1[1], write_buf, sizeof(write_buf));
                        buf_count = 0;
                    }
                }
            }
            if (buf_count > 0) {
                write_all(pipe1[1], write_buf, buf_count * sizeof(PixelMsg));
                buf_count = 0;
            }
        }

        close(pipe1[1]);
        _exit(0);
    }

    // Fork Process 2: S2
    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(pipe1[1]); // close write end of pipe1
        close(pipe2[0]); // close read end of pipe2

        PixelMsg in_buf[CHUNK_SIZE];
        PixelMsg out_buf[CHUNK_SIZE];
        int in_count = 0;
        int in_pos = 0;
        int out_count = 0;

        for (int iter = 0; iter < iterations; ++iter) {
            int expected_seq = 0;
            for (int p = 0; p < total_pixels; ++p) {
                if (in_pos >= in_count) {
                    ssize_t n = read(pipe1[0], in_buf, sizeof(in_buf));
                    assert(n > 0 && "S2 unexpected EOF on pipe1");
                    in_count = n / sizeof(PixelMsg);
                    in_pos = 0;
                }
                PixelMsg in = in_buf[in_pos++];
                assert(in.seq == expected_seq && "S2 received pixel out of order!");
                expected_seq++;

                int i = in.seq / width;
                int j = in.seq % width;
                PixelMsg out;
                out.seq = in.seq;
                compute_S2_pixel(input_image, i, j, in.r, in.g, in.b, out.r, out.g, out.b);
                out_buf[out_count++] = out;

                if (out_count == CHUNK_SIZE) {
                    write_all(pipe2[1], out_buf, sizeof(out_buf));
                    out_count = 0;
                }
            }
            if (out_count > 0) {
                write_all(pipe2[1], out_buf, out_count * sizeof(PixelMsg));
                out_count = 0;
            }
        }

        close(pipe1[0]);
        close(pipe2[1]);
        _exit(0);
    }

    // Process 3: S3 (Parent Process)
    close(pipe1[0]);
    close(pipe1[1]);
    close(pipe2[1]); // close write end of pipe2

    struct image_t* sharpened_image = allocate_image(width, height);
    PixelMsg in_buf[CHUNK_SIZE];
    int in_count = 0;
    int in_pos = 0;

    for (int iter = 0; iter < iterations; ++iter) {
        int expected_seq = 0;
        for (int p = 0; p < total_pixels; ++p) {
            if (in_pos >= in_count) {
                ssize_t n = read(pipe2[0], in_buf, sizeof(in_buf));
                assert(n > 0 && "S3 unexpected EOF on pipe2");
                in_count = n / sizeof(PixelMsg);
                in_pos = 0;
            }
            PixelMsg in = in_buf[in_pos++];
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
    }

    close(pipe2[0]);
    waitpid(pid1, nullptr, 0);
    waitpid(pid2, nullptr, 0);
    auto t_end_compute = std::chrono::high_resolution_clock::now();

    auto t_start_write = std::chrono::high_resolution_clock::now();
    write_ppm_file(output_file, sharpened_image);
    auto t_end_write = std::chrono::high_resolution_clock::now();

    double read_ms = std::chrono::duration<double, std::milli>(t_end_read - t_start_read).count();
    double compute_ms = std::chrono::duration<double, std::milli>(t_end_compute - t_start_compute).count();
    double write_ms = std::chrono::duration<double, std::milli>(t_end_write - t_start_write).count();
    double total_ms = read_ms + compute_ms + write_ms;

    std::cout << "[Part 2.2 Variant 1 - Processes + Pipes]\n";
    std::cout << "Order verification: SUCCESS (All " << total_pixels << " pixels received in exact sent order for each of " << iterations << " iterations)\n";
    std::cout << "Read Time: " << read_ms << " ms | Compute Time: " << compute_ms << " ms | Write Time: " << write_ms << " ms | Total Time: " << total_ms << " ms\n";

    return 0;
}

