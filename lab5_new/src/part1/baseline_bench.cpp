// Fair single-thread baseline for the Part II / III speed-ups.
// Reads the image ONCE, allocates ONCE, then performs exactly the same amount of
// S1/S2/S3 work as the parallel programs (N runs of each), with the same per-pixel
// kernels, but with no threads, no communication and no allocation inside the loop.
//   variant 1 : N x (S1 -> S2 -> S3) per pixel
//   variant 2 : N/100 batches of { 100 x S1, 100 x S2, 100 x S3 } on whole images
// usage: baseline_bench <variant 1|2> <input.ppm> <output.ppm> [iterations]
#include "common_pipeline.h"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: " << argv[0] << " <variant 1|2> <input.ppm> <output.ppm> [iterations]\n";
        return 1;
    }
    const int V = std::atoi(argv[1]);
    const int N = (argc >= 5) ? std::atoi(argv[4]) : 1000;
    if ((V != 1 && V != 2) || !iterations_valid(V, N)) return 1;

    auto t0 = Clock::now();
    image_t* img = read_ppm_file(argv[2]);
    auto t1 = Clock::now();
    const int W = img->width, H = img->height, total = W * H;
    image_t* out = allocate_image(W, H);

    auto c0 = Clock::now();
    if (V == 1) {
        for (int it = 0; it < N; ++it) {
            for (int i = 0; i < H; ++i)
                for (int j = 0; j < W; ++j) {
                    uint8_t a, b, c, d, e, f, r, g, bl;
                    compute_S1_pixel(img, i, j, a, b, c);
                    compute_S2_pixel(img, i, j, a, b, c, d, e, f);
                    compute_S3_pixel(img, i, j, d, e, f, r, g, bl);
                    store_pixel(out, i, j, r, g, bl);
                }
            keep(out);
        }
    } else {
        std::vector<PixelMsg> s1(total), s2(total);
        for (int batch = 0; batch < N / V2_INTERVAL; ++batch) {
            for (int run = 0; run < V2_INTERVAL; ++run) {          // S1 x100
                int p = 0;
                for (int i = 0; i < H; ++i)
                    for (int j = 0; j < W; ++j, ++p) {
                        uint8_t r, g, b;
                        compute_S1_pixel(img, i, j, r, g, b);
                        s1[p] = make_msg(p, r, g, b);
                    }
                keep(s1.data());
            }
            for (int run = 0; run < V2_S2_RUNS; ++run) {           // S2 x100
                int p = 0;
                for (int i = 0; i < H; ++i)
                    for (int j = 0; j < W; ++j, ++p) {
                        uint8_t r, g, b;
                        compute_S2_pixel(img, i, j, s1[p].r, s1[p].g, s1[p].b, r, g, b);
                        s2[p] = make_msg(p, r, g, b);
                    }
                keep(s2.data());
            }
            for (int run = 0; run < V2_S3_RUNS; ++run) {           // S3 x100
                int p = 0;
                for (int i = 0; i < H; ++i)
                    for (int j = 0; j < W; ++j, ++p) {
                        uint8_t r, g, b;
                        compute_S3_pixel(img, i, j, s2[p].r, s2[p].g, s2[p].b, r, g, b);
                        store_pixel(out, i, j, r, g, b);
                    }
                keep(out);
            }
        }
    }
    auto c1 = Clock::now();

    auto w0 = Clock::now();
    write_ppm_file(argv[3], out);
    auto w1 = Clock::now();

    char title[64];
    snprintf(title, sizeof title, "Baseline (single thread) Variant %d", V);
    print_report(title, "n/a (no communication)", ms_between(t0, t1), ms_between(c0, c1),
                 ms_between(w0, w1), fnv_image(out));
    return 0;
}
