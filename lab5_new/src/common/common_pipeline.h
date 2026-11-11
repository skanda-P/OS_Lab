// common_pipeline.h
// Shared definitions: pixel message, per-pixel stage kernels (S1/S2/S3),
// FNV-1a checksum helpers, error handling and robust read/write helpers.
#ifndef COMMON_PIPELINE_H
#define COMMON_PIPELINE_H

#include "libppm.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <signal.h>

// ---------------------------------------------------------------------------
// Message that travels between stages: one pixel plus its sequence number.
// Size is fixed at 8 bytes (explicit pad byte, always zero) so that it can be
// sent raw through pipes / shared memory / sockets without uninitialised bytes.
// ---------------------------------------------------------------------------
struct PixelMsg {
    int32_t seq;   // 0 .. total_pixels-1 inside one image; proves ORDER
    uint8_t r, g, b;
    uint8_t pad;
};
static_assert(sizeof(PixelMsg) == 8, "PixelMsg must be exactly 8 bytes");

inline PixelMsg make_msg(int seq, uint8_t r, uint8_t g, uint8_t b) {
    PixelMsg m;
    m.seq = seq; m.r = r; m.g = g; m.b = b; m.pad = 0;
    return m;
}

// ---------------------------------------------------------------------------
// Fatal error path. If we are a forked child, also kill the parent so that no
// process is ever left blocked forever on a pipe / semaphore.
// ---------------------------------------------------------------------------
inline pid_t g_kill_on_fatal = 0;

__attribute__((format(printf, 1, 2)))
[[noreturn]] inline void fatal(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "\n*** FAILURE: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    fflush(stderr);
    if (g_kill_on_fatal > 0) kill(g_kill_on_fatal, SIGKILL);
    _exit(2);
}

// ---------------------------------------------------------------------------
// FNV-1a 32-bit checksum
// ---------------------------------------------------------------------------
constexpr uint32_t FNV_INIT = 2166136261u;

inline uint32_t fnv_u8(uint32_t h, uint8_t b) { return (h ^ b) * 16777619u; }

inline uint32_t fnv_msg(uint32_t h, const PixelMsg& m) {
    uint32_t s = (uint32_t)m.seq;
    h = fnv_u8(h, (uint8_t)(s));
    h = fnv_u8(h, (uint8_t)(s >> 8));
    h = fnv_u8(h, (uint8_t)(s >> 16));
    h = fnv_u8(h, (uint8_t)(s >> 24));
    h = fnv_u8(h, m.r);
    h = fnv_u8(h, m.g);
    h = fnv_u8(h, m.b);
    return h;
}

inline uint32_t fnv_image(const struct image_t* img) {
    uint32_t h = FNV_INIT;
    for (int i = 0; i < img->height; ++i)
        for (int j = 0; j < img->width; ++j)
            for (int k = 0; k < 3; ++k)
                h = fnv_u8(h, img->image_pixels[i][j][k]);
    return h;
}

// ---------------------------------------------------------------------------
// Image helpers
// ---------------------------------------------------------------------------
inline struct image_t* allocate_image(int width, int height) {
    struct image_t* img = new struct image_t;
    img->width = width;
    img->height = height;
    img->image_pixels = new uint8_t**[height];
    for (int i = 0; i < height; i++) {
        img->image_pixels[i] = new uint8_t*[width];
        for (int j = 0; j < width; j++) {
            img->image_pixels[i][j] = new uint8_t[3]();
        }
    }
    return img;
}

inline uint8_t clamp_to_byte(int val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (uint8_t)val;
}

// ---------------------------------------------------------------------------
// Per-pixel stage kernels. They are IDENTICAL to the Part 1 maths.
// ---------------------------------------------------------------------------
// S1: smoothen pixel (i, j) - mean of the existing 3x3 neighbourhood
inline void compute_S1_pixel(const struct image_t* img, int i, int j,
                             uint8_t& r, uint8_t& g, uint8_t& b) {
    int height = img->height;
    int width = img->width;
    uint8_t v[3];
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
        v[k] = (uint8_t)(sum / count);
    }
    r = v[0]; g = v[1]; b = v[2];
}

// S2: details pixel (i, j) = original - smoothened
inline void compute_S2_pixel(const struct image_t* img, int i, int j,
                             uint8_t s1_r, uint8_t s1_g, uint8_t s1_b,
                             uint8_t& out_r, uint8_t& out_g, uint8_t& out_b) {
    out_r = (uint8_t)(img->image_pixels[i][j][0] - s1_r);
    out_g = (uint8_t)(img->image_pixels[i][j][1] - s1_g);
    out_b = (uint8_t)(img->image_pixels[i][j][2] - s1_b);
}

// S3: sharpen pixel (i, j) = clamp(original + details)
inline void compute_S3_pixel(const struct image_t* img, int i, int j,
                             uint8_t s2_r, uint8_t s2_g, uint8_t s2_b,
                             uint8_t& out_r, uint8_t& out_g, uint8_t& out_b) {
    int orig_r = (int)img->image_pixels[i][j][0];
    int orig_g = (int)img->image_pixels[i][j][1];
    int orig_b = (int)img->image_pixels[i][j][2];

    out_r = clamp_to_byte(orig_r + (int8_t)s2_r);
    out_g = clamp_to_byte(orig_g + (int8_t)s2_g);
    out_b = clamp_to_byte(orig_b + (int8_t)s2_b);
}

inline void store_pixel(struct image_t* out, int i, int j,
                        uint8_t r, uint8_t g, uint8_t b) {
    out->image_pixels[i][j][0] = r;
    out->image_pixels[i][j][1] = g;
    out->image_pixels[i][j][2] = b;
}

// Compiler barrier: tells the optimiser that memory at p is observed, so the
// work that produced it can never be treated as dead code (benchmark hygiene).
inline void keep(const void* p) { asm volatile("" : : "r"(p) : "memory"); }

// ---------------------------------------------------------------------------
// Robust blocking read / write (handle partial transfers and EINTR, abort on
// error or premature EOF instead of silently continuing with stale data).
// ---------------------------------------------------------------------------
inline void write_fully(int fd, const void* buf, size_t count) {
    const char* p = (const char*)buf;
    size_t done = 0;
    while (done < count) {
        ssize_t n = write(fd, p + done, count - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            fatal("write() failed: %s", strerror(errno));
        }
        done += (size_t)n;
    }
}

inline void read_fully(int fd, void* buf, size_t count) {
    char* p = (char*)buf;
    size_t done = 0;
    while (done < count) {
        ssize_t n = read(fd, p + done, count - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            fatal("read() failed: %s", strerror(errno));
        }
        if (n == 0) fatal("unexpected EOF after %zu of %zu bytes", done, count);
        done += (size_t)n;
    }
}

// ---------------------------------------------------------------------------
// Timing + uniform report line (parsed by run_experiments.sh)
// ---------------------------------------------------------------------------
using Clock = std::chrono::steady_clock;

inline double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

inline void print_report(const char* title, const char* verification,
                         double read_ms, double compute_ms, double write_ms,
                         uint32_t out_hash) {
    printf("[%s]\n", title);
    printf("Verification: %s\n", verification);
    printf("Output hash: %08x\n", out_hash);
    printf("Read Time: %.3f ms | Compute Time: %.3f ms | Write Time: %.3f ms | Total Time: %.3f ms\n",
           read_ms, compute_ms, write_ms, read_ms + compute_ms + write_ms);
    fflush(stdout);
}

// Variant 2 constants (assignment: 1000 runs, every 100th result passed on,
// 100 runs of S2 per S1 result, 100 runs of S3 per S2 result)
constexpr int V2_INTERVAL = 100;
constexpr int V2_S2_RUNS = 100;
constexpr int V2_S3_RUNS = 100;

inline bool iterations_valid(int variant, int iters) {
    if (iters <= 0) {
        fprintf(stderr, "iterations must be > 0\n");
        return false;
    }
    if (variant == 2 && iters % V2_INTERVAL != 0) {
        fprintf(stderr, "Variant 2 needs iterations to be a positive multiple of %d (got %d)\n",
                V2_INTERVAL, iters);
        return false;
    }
    return true;
}

#endif
