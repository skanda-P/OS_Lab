#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include "common_pipeline.h"

const int CHUNK_SIZE = 512;

int main(int argc, char** argv) {
    int port = (argc >= 2) ? std::atoi(argv[1]) : 12345;
    char* input_file = (argc >= 3) ? argv[2] : (char*)"input.ppm";
    int total_s1_runs = (argc >= 4) ? std::atoi(argv[3]) : 1000;
    int s1_interval = 100;
    int batches = total_s1_runs / s1_interval;

    auto t_start_read = std::chrono::high_resolution_clock::now();
    struct image_t* input_image = read_ppm_file(input_file);
    auto t_end_read = std::chrono::high_resolution_clock::now();

    int width = input_image->width;
    int height = input_image->height;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
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
    listen(server_fd, 1);

    std::cout << "[Part 3.2 Variant 2 - Computer A (S1 Server)] Listening on port " << port << "...\n";
    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
        perror("accept");
        return 1;
    }
    std::cout << "Connected to Computer B!\n";

    auto t_start_compute = std::chrono::high_resolution_clock::now();

    // Send metadata header: width, height, batches
    int header[3] = { width, height, batches };
    write_all(client_fd, header, sizeof(header));

    // Send original image pixels
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            write_all(client_fd, input_image->image_pixels[i][j], 3);
        }
    }

    PixelMsg send_buf[CHUNK_SIZE];
    int buf_count = 0;

    for (int run = 1; run <= total_s1_runs; ++run) {
        bool send_to_socket = (run % s1_interval == 0);
        int seq = 0;
        for (int i = 0; i < height; ++i) {
            for (int j = 0; j < width; ++j) {
                uint8_t r = 0, g = 0, b = 0;
                compute_S1_pixel(input_image, i, j, r, g, b);
                if (send_to_socket) {
                    send_buf[buf_count++] = {seq++, r, g, b};
                    if (buf_count == CHUNK_SIZE) {
                        write_all(client_fd, send_buf, sizeof(send_buf));
                        buf_count = 0;
                    }
                }
            }
        }
        if (send_to_socket && buf_count > 0) {
            write_all(client_fd, send_buf, buf_count * sizeof(PixelMsg));
            buf_count = 0;
        }
    }

    auto t_end_compute = std::chrono::high_resolution_clock::now();
    close(client_fd);
    close(server_fd);

    double read_ms = std::chrono::duration<double, std::milli>(t_end_read - t_start_read).count();
    double compute_ms = std::chrono::duration<double, std::milli>(t_end_compute - t_start_compute).count();

    std::cout << "[Computer A Done] Read Time: " << read_ms << " ms | S1 Compute & Send Time: " << compute_ms << " ms\n";
    return 0;
}
