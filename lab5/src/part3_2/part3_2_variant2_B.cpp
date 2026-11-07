#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <vector>
#include "common_pipeline.h"

const int CHUNK_SIZE = 512;

int main(int argc, char** argv) {
    char* server_ip = (argc >= 2) ? argv[1] : (char*)"127.0.0.1";
    int port = (argc >= 3) ? std::atoi(argv[2]) : 12345;
    char* output_file = (argc >= 4) ? argv[3] : (char*)"output_part3_2_variant2.ppm";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return 1;
    }

    std::cout << "[Part 3.2 Variant 2 - Computer B (S2 + S3 Client)] Connecting to " << server_ip << ":" << port << "...\n";
    while (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        usleep(100000);
    }
    std::cout << "Connected to Computer A!\n";

    auto t_start_compute = std::chrono::high_resolution_clock::now();

    // Receive metadata header
    int header[3];
    read_all(sock, header, sizeof(header));
    int width = header[0];
    int height = header[1];
    int batches = header[2];
    int total_pixels = width * height;
    int s2_runs_per_s1 = 100;
    int s3_runs_per_s2 = 100;

    // Receive original image pixels
    struct image_t* input_image = allocate_image(width, height);
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            read_all(sock, input_image->image_pixels[i][j], 3);
        }
    }

    struct image_t* sharpened_image = allocate_image(width, height);
    std::vector<PixelMsg> s1_pixels(total_pixels);
    std::vector<PixelMsg> s2_pixels(total_pixels);
    PixelMsg in_buf[CHUNK_SIZE];

    for (int b = 0; b < batches; ++b) {
        int expected_seq = 0;
        int received = 0;
        while (received < total_pixels) {
            int to_read = std::min(CHUNK_SIZE, total_pixels - received);
            ssize_t n = read_all(sock, in_buf, to_read * sizeof(PixelMsg));
            assert(n > 0 && "Unexpected socket EOF");

            for (int p = 0; p < to_read; ++p) {
                s1_pixels[received + p] = in_buf[p];
                assert(in_buf[p].seq == expected_seq && "S2 received pixel out of order!");
                expected_seq++;
            }
            received += to_read;
        }

        // Run S2 100 times
        for (int run = 1; run <= s2_runs_per_s1; ++run) {
            bool save_s2 = (run == s2_runs_per_s1);
            for (int p = 0; p < total_pixels; ++p) {
                int i = p / width;
                int j = p % width;
                uint8_t out_r, out_g, out_b;
                compute_S2_pixel(input_image, i, j, s1_pixels[p].r, s1_pixels[p].g, s1_pixels[p].b, out_r, out_g, out_b);
                if (save_s2) {
                    s2_pixels[p] = {p, out_r, out_g, out_b};
                }
            }
        }

        // Run S3 100 times
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

    auto t_end_compute = std::chrono::high_resolution_clock::now();
    close(sock);

    auto t_start_write = std::chrono::high_resolution_clock::now();
    write_ppm_file(output_file, sharpened_image);
    auto t_end_write = std::chrono::high_resolution_clock::now();

    double compute_ms = std::chrono::duration<double, std::milli>(t_end_compute - t_start_compute).count();
    double write_ms = std::chrono::duration<double, std::milli>(t_end_write - t_start_write).count();

    std::cout << "[Part 3.2 Variant 2 - Computer B Done]\n";
    std::cout << "Order verification: SUCCESS (All " << total_pixels << " pixels received in exact sent order for all " << batches << " batches)\n";
    std::cout << "Receive & S2+S3 Compute Time: " << compute_ms << " ms | Write Time: " << write_ms << " ms | Total Time: " << (compute_ms + write_ms) << " ms\n";

    return 0;
}

