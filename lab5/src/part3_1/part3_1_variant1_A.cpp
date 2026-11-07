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
    int port = (argc >= 2) ? std::atoi(argv[1]) : 12345;
    char* input_file = (argc >= 3) ? argv[2] : (char*)"images/1.ppm";
    int iterations = (argc >= 4) ? std::atoi(argv[3]) : 1000;

    auto t_start_read = std::chrono::high_resolution_clock::now();
    struct image_t* input_image = read_ppm_file(input_file);
    auto t_end_read = std::chrono::high_resolution_clock::now();

    int width = input_image->width;
    int height = input_image->height;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "[Part 3.1 Variant 1 - Computer A (S1 + S2 Server)] Listening on port " << port << "...\n";
    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
        perror("accept");
        return 1;
    }
    std::cout << "Connected to Computer B!\n";

    auto t_start_compute = std::chrono::high_resolution_clock::now();

    // Send metadata header: width, height, iterations
    int header[3] = { width, height, iterations };
    write_all(client_fd, header, sizeof(header));

    // Send original image pixels so Computer B can compute S3
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            write_all(client_fd, input_image->image_pixels[i][j], 3);
        }
    }

    // Stream S1 and S2 results to Computer B
    PixelMsg send_buf[CHUNK_SIZE];
    int buf_count = 0;

    for (int iter = 0; iter < iterations; ++iter) {
        int seq = 0;
        for (int i = 0; i < height; ++i) {
            for (int j = 0; j < width; ++j) {
                // S1
                uint8_t s1_r = 0, s1_g = 0, s1_b = 0;
                compute_S1_pixel(input_image, i, j, s1_r, s1_g, s1_b);

                // S2
                PixelMsg msg;
                msg.seq = seq++;
                compute_S2_pixel(input_image, i, j, s1_r, s1_g, s1_b, msg.r, msg.g, msg.b);

                send_buf[buf_count++] = msg;
                if (buf_count == CHUNK_SIZE) {
                    write_all(client_fd, send_buf, sizeof(send_buf));
                    buf_count = 0;
                }
            }
        }
        if (buf_count > 0) {
            write_all(client_fd, send_buf, buf_count * sizeof(PixelMsg));
            buf_count = 0;
        }
    }

    auto t_end_compute = std::chrono::high_resolution_clock::now();
    close(client_fd);
    close(server_fd);

    double read_ms = std::chrono::duration<double, std::milli>(t_end_read - t_start_read).count();
    double compute_ms = std::chrono::duration<double, std::milli>(t_end_compute - t_start_compute).count();

    std::cout << "[Computer A Done] Read Time: " << read_ms << " ms | S1+S2 Compute & Send Time: " << compute_ms << " ms\n";
    return 0;
}
