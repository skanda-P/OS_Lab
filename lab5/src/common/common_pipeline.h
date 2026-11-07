#ifndef COMMON_PIPELINE_H
#define COMMON_PIPELINE_H

#include "libppm.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <unistd.h>

struct PixelMsg {
    int seq;       // Sequence ID: 0 .. total_pixels - 1 (proves in-order delivery)
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

inline struct image_t* allocate_image(int width, int height) {
    struct image_t* img = new struct image_t;
    img->width = width;
    img->height = height;
    img->image_pixels = new uint8_t**[height];
    for (int i = 0; i < height; i++) {
        img->image_pixels[i] = new uint8_t*[width];
        for (int j = 0; j < width; j++) {
            img->image_pixels[i][j] = new uint8_t[3];
        }
    }
    return img;
}

inline uint8_t clamp_to_byte(int val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (uint8_t)val;
}

// S1: Smoothen pixel (i, j)
inline void compute_S1_pixel(const struct image_t* img, int i, int j, uint8_t &r, uint8_t &g, uint8_t &b) {
    int height = img->height;
    int width = img->width;
    for (int k = 0; k < 3; ++k) {
        int sum = 0;
        int count = 0;
        for (int di = -1; di <= 1; ++di) {
            for (int dj = -1; dj <= 1; ++dj) {
                int ni = i + di;
                int nj = j + dj;
                if (ni >= 0 && ni < height && nj >= 0 && nj < width) {
                    sum += img->image_pixels[ni][nj][k];
                    count++;
                }
            }
        }
        uint8_t val = (uint8_t)(sum / count);
        if (k == 0) r = val;
        else if (k == 1) g = val;
        else b = val;
    }
}

// S2: Find details pixel (i, j)
inline void compute_S2_pixel(const struct image_t* img, int i, int j, uint8_t s1_r, uint8_t s1_g, uint8_t s1_b, uint8_t &out_r, uint8_t &out_g, uint8_t &out_b) {
    out_r = (uint8_t)(img->image_pixels[i][j][0] - s1_r);
    out_g = (uint8_t)(img->image_pixels[i][j][1] - s1_g);
    out_b = (uint8_t)(img->image_pixels[i][j][2] - s1_b);
}

// S3: Sharpen pixel (i, j)
inline void compute_S3_pixel(const struct image_t* img, int i, int j, uint8_t s2_r, uint8_t s2_g, uint8_t s2_b, uint8_t &out_r, uint8_t &out_g, uint8_t &out_b) {
    int orig_r = (int)img->image_pixels[i][j][0];
    int orig_g = (int)img->image_pixels[i][j][1];
    int orig_b = (int)img->image_pixels[i][j][2];

    out_r = clamp_to_byte(orig_r + (int8_t)s2_r);
    out_g = clamp_to_byte(orig_g + (int8_t)s2_g);
    out_b = clamp_to_byte(orig_b + (int8_t)s2_b);
}

// Safe socket/pipe read & write
inline ssize_t write_all(int fd, const void* buf, size_t count) {
    size_t total = 0;
    const char* ptr = (const char*)buf;
    while (total < count) {
        ssize_t n = write(fd, ptr + total, count - total);
        if (n <= 0) return (total == 0) ? n : total;
        total += n;
    }
    return total;
}

inline ssize_t read_all(int fd, void* buf, size_t count) {
    size_t total = 0;
    char* ptr = (char*)buf;
    while (total < count) {
        ssize_t n = read(fd, ptr + total, count - total);
        if (n <= 0) return (total == 0) ? n : total;
        total += n;
    }
    return total;
}

#endif

