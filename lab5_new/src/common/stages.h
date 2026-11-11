// stages.h
// The stage bodies, written ONCE and used by every mechanism (threads, pipes,
// shared memory, sockets). Each stage runs S1/S2/S3 the FULL number of times
// required by the assignment - nothing is skipped.
//
//   Variant 1 : N runs; run k of S1 feeds run k of S2 feeds run k of S3, all
//               streamed pixel by pixel.  The output of the N-th S3 is written.
//   Variant 2 : S1 runs N times on the same image, the result of every 100th
//               run is streamed to S2; S2 runs 100 times per received result and
//               streams the result of its 100th run; S3 runs 100 times per
//               received result; the last S3 result is written.
//
// Results of runs that are "not passed on" are still stored into a buffer and
// published with keep(), so the optimiser cannot delete that work.
#ifndef STAGES_H
#define STAGES_H

#include "channels.h"

// ============================ Variant 1 =====================================
template <class Tx>
void v1_S1(const image_t* img, int iters, Tx& tx) {
    const int H = img->height, W = img->width;
    for (int it = 0; it < iters; ++it) {
        int p = 0;
        for (int i = 0; i < H; ++i)
            for (int j = 0; j < W; ++j) {
                uint8_t r, g, b;
                compute_S1_pixel(img, i, j, r, g, b);
                tx.send(make_msg(p++, r, g, b));
            }
    }
    tx.finish();
}

template <class Rx, class Tx>
void v1_S2(const image_t* img, int iters, Rx& rx, Tx& tx) {
    const int H = img->height, W = img->width;
    for (int it = 0; it < iters; ++it) {
        int p = 0;
        for (int i = 0; i < H; ++i)
            for (int j = 0; j < W; ++j, ++p) {
                PixelMsg in = rx.get(p);
                uint8_t r, g, b;
                compute_S2_pixel(img, i, j, in.r, in.g, in.b, r, g, b);
                tx.send(make_msg(p, r, g, b));
            }
    }
    rx.finish();
    tx.finish();
}

template <class Rx>
void v1_S3(const image_t* img, image_t* out, int iters, Rx& rx) {
    const int H = img->height, W = img->width;
    for (int it = 0; it < iters; ++it) {
        int p = 0;
        for (int i = 0; i < H; ++i)
            for (int j = 0; j < W; ++j, ++p) {
                PixelMsg in = rx.get(p);
                uint8_t r, g, b;
                compute_S3_pixel(img, i, j, in.r, in.g, in.b, r, g, b);
                store_pixel(out, i, j, r, g, b);      // every run; last run = final file
            }
        keep(out);
    }
    rx.finish();
}

// S1 + S2 on the same computer (Part III.1, computer A)
template <class Tx>
void v1_S12(const image_t* img, int iters, Tx& tx) {
    const int H = img->height, W = img->width;
    for (int it = 0; it < iters; ++it) {
        int p = 0;
        for (int i = 0; i < H; ++i)
            for (int j = 0; j < W; ++j, ++p) {
                uint8_t a, b, c;
                compute_S1_pixel(img, i, j, a, b, c);
                uint8_t r, g, bl;
                compute_S2_pixel(img, i, j, a, b, c, r, g, bl);
                tx.send(make_msg(p, r, g, bl));
            }
    }
    tx.finish();
}

// S2 + S3 on the same computer (Part III.2, computer B)
template <class Rx>
void v1_S23(const image_t* img, image_t* out, int iters, Rx& rx) {
    const int H = img->height, W = img->width;
    for (int it = 0; it < iters; ++it) {
        int p = 0;
        for (int i = 0; i < H; ++i)
            for (int j = 0; j < W; ++j, ++p) {
                PixelMsg in = rx.get(p);
                uint8_t a, b, c;
                compute_S2_pixel(img, i, j, in.r, in.g, in.b, a, b, c);
                uint8_t r, g, bl;
                compute_S3_pixel(img, i, j, a, b, c, r, g, bl);
                store_pixel(out, i, j, r, g, bl);
            }
        keep(out);
    }
    rx.finish();
}

// ============================ Variant 2 =====================================
// Helpers working on whole-image buffers of PixelMsg
inline void s1_pass(const image_t* img, std::vector<PixelMsg>& buf) {
    int p = 0;
    for (int i = 0; i < img->height; ++i)
        for (int j = 0; j < img->width; ++j, ++p) {
            uint8_t r, g, b;
            compute_S1_pixel(img, i, j, r, g, b);
            buf[p] = make_msg(p, r, g, b);
        }
    keep(buf.data());
}

struct NoTx { void send(const PixelMsg&) {} };   // placeholder: "do not stream"

// one S2 pass over `in`, storing into `outb`; optionally streams each pixel
template <class Tx>
void s2_pass(const image_t* img, const std::vector<PixelMsg>& in,
             std::vector<PixelMsg>& outb, Tx* tx) {
    int p = 0;
    for (int i = 0; i < img->height; ++i)
        for (int j = 0; j < img->width; ++j, ++p) {
            uint8_t r, g, b;
            compute_S2_pixel(img, i, j, in[p].r, in[p].g, in[p].b, r, g, b);
            outb[p] = make_msg(p, r, g, b);
            if (tx) tx->send(outb[p]);
        }
    keep(outb.data());
}

inline void s3_pass(const image_t* img, image_t* out, const std::vector<PixelMsg>& in) {
    int p = 0;
    for (int i = 0; i < img->height; ++i)
        for (int j = 0; j < img->width; ++j, ++p) {
            uint8_t r, g, b;
            compute_S3_pixel(img, i, j, in[p].r, in[p].g, in[p].b, r, g, b);
            store_pixel(out, i, j, r, g, b);
        }
    keep(out);
}

template <class Rx>
void recv_image(const image_t* img, Rx& rx, std::vector<PixelMsg>& buf) {
    const int total = img->width * img->height;
    for (int p = 0; p < total; ++p) buf[p] = rx.get(p);
}

template <class Tx>
void v2_S1(const image_t* img, int total_runs, Tx& tx) {
    const int total = img->width * img->height;
    std::vector<PixelMsg> buf(total);
    for (int run = 1; run <= total_runs; ++run) {
        const bool pass_on = (run % V2_INTERVAL == 0);
        int p = 0;
        for (int i = 0; i < img->height; ++i)
            for (int j = 0; j < img->width; ++j, ++p) {
                uint8_t r, g, b;
                compute_S1_pixel(img, i, j, r, g, b);
                buf[p] = make_msg(p, r, g, b);
                if (pass_on) tx.send(buf[p]);
            }
        keep(buf.data());
    }
    tx.finish();
}

template <class Rx, class Tx>
void v2_S2(const image_t* img, int total_runs, Rx& rx, Tx& tx) {
    const int total = img->width * img->height;
    const int batches = total_runs / V2_INTERVAL;
    std::vector<PixelMsg> in(total), outb(total);
    for (int b = 0; b < batches; ++b) {
        recv_image(img, rx, in);
        for (int run = 1; run <= V2_S2_RUNS; ++run)
            s2_pass(img, in, outb, run == V2_S2_RUNS ? &tx : (Tx*)nullptr);
    }
    rx.finish();
    tx.finish();
}

template <class Rx>
void v2_S3(const image_t* img, image_t* out, int total_runs, Rx& rx) {
    const int total = img->width * img->height;
    const int batches = total_runs / V2_INTERVAL;
    std::vector<PixelMsg> in(total);
    for (int b = 0; b < batches; ++b) {
        recv_image(img, rx, in);
        for (int run = 1; run <= V2_S3_RUNS; ++run) s3_pass(img, out, in);
    }
    rx.finish();
}

// S1 + S2 on the same computer (Part III.1, computer A)
template <class Tx>
void v2_S12(const image_t* img, int total_runs, Tx& tx) {
    const int total = img->width * img->height;
    std::vector<PixelMsg> s1(total), s2(total);
    for (int run = 1; run <= total_runs; ++run) {
        s1_pass(img, s1);
        if (run % V2_INTERVAL == 0)
            for (int r2 = 1; r2 <= V2_S2_RUNS; ++r2)
                s2_pass(img, s1, s2, r2 == V2_S2_RUNS ? &tx : (Tx*)nullptr);
    }
    tx.finish();
}

// S2 + S3 on the same computer (Part III.2, computer B)
template <class Rx>
void v2_S23(const image_t* img, image_t* out, int total_runs, Rx& rx) {
    const int total = img->width * img->height;
    const int batches = total_runs / V2_INTERVAL;
    std::vector<PixelMsg> s1(total), s2(total);
    for (int b = 0; b < batches; ++b) {
        recv_image(img, rx, s1);
        for (int r2 = 1; r2 <= V2_S2_RUNS; ++r2) s2_pass(img, s1, s2, (NoTx*)nullptr);
        for (int r3 = 1; r3 <= V2_S3_RUNS; ++r3) s3_pass(img, out, s2);
    }
    rx.finish();
}

// ============================ dispatch by variant ============================
template <int V, class Tx> void stage1(const image_t* img, int n, Tx& tx) {
    if constexpr (V == 1) v1_S1(img, n, tx); else v2_S1(img, n, tx);
}
template <int V, class Rx, class Tx> void stage2(const image_t* img, int n, Rx& rx, Tx& tx) {
    if constexpr (V == 1) v1_S2(img, n, rx, tx); else v2_S2(img, n, rx, tx);
}
template <int V, class Rx> void stage3(const image_t* img, image_t* out, int n, Rx& rx) {
    if constexpr (V == 1) v1_S3(img, out, n, rx); else v2_S3(img, out, n, rx);
}
template <int V, class Tx> void stage12(const image_t* img, int n, Tx& tx) {
    if constexpr (V == 1) v1_S12(img, n, tx); else v2_S12(img, n, tx);
}
template <int V, class Rx> void stage23(const image_t* img, image_t* out, int n, Rx& rx) {
    if constexpr (V == 1) v1_S23(img, out, n, rx); else v2_S23(img, out, n, rx);
}

#endif
