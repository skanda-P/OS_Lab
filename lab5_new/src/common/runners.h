// runners.h : the main() bodies for Part II.1/II.2/II.3 and Part III A/B.
// Each part's .cpp file is a one-liner that picks the variant (1 or 2).
#ifndef RUNNERS_H
#define RUNNERS_H

#include "stages.h"
#include <thread>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

constexpr const char* VERIFIED_2LINKS =
    "PASSED - seq order + FNV-1a pixel checksum verified on S1->S2 and S2->S3";

// ---------------------------------------------------------------------------
inline bool parse_part2_args(int argc, char** argv, int variant, int default_iters,
                             char*& in, char*& out, int& iters) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_image> <output_image> [iterations]\n";
        return false;
    }
    in = argv[1];
    out = argv[2];
    iters = (argc >= 4) ? std::atoi(argv[3]) : default_iters;
    return iterations_valid(variant, iters);
}

// ===========================================================================
// Part II.1 : three threads + atomics
// ===========================================================================
template <int V>
int run_part2_1(int argc, char** argv, int default_iters = 1000) {
    char *in_f, *out_f; int N;
    if (!parse_part2_args(argc, argv, V, default_iters, in_f, out_f, N)) return 1;

    auto t0 = Clock::now();
    image_t* img = read_ppm_file(in_f);
    auto t1 = Clock::now();
    image_t* out = allocate_image(img->width, img->height);

    AtomicChan q1, q2;   // S1->S2, S2->S3

    Clock::time_point c1, w0, w1;
    auto c0 = Clock::now();
    std::thread th1([&] { Sender<AtomicChan> tx(q1); stage1<V>(img, N, tx); });
    std::thread th2([&] {
        Receiver<AtomicChan> rx(q1, "S1->S2");
        Sender<AtomicChan> tx(q2);
        stage2<V>(img, N, rx, tx);
    });
    std::thread th3([&] {
        Receiver<AtomicChan> rx(q2, "S2->S3");
        stage3<V>(img, out, N, rx);
        c1 = Clock::now();
        w0 = Clock::now();
        write_ppm_file(out_f, out);
        w1 = Clock::now();
    });
    th1.join(); th2.join(); th3.join();

    char title[96];
    snprintf(title, sizeof title, "Part 2.1 Variant %d - Threads + Atomic Operations", V);
    print_report(title, VERIFIED_2LINKS, ms_between(t0, t1), ms_between(c0, c1),
                 ms_between(w0, w1), fnv_image(out));
    return 0;
}

// ---------------------------------------------------------------------------
// helper for the forked variants: wait for children and make sure they passed
inline void wait_children_ok(pid_t a, pid_t b) {
    int st;
    if (waitpid(a, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0)
        fatal("process S1 did not finish successfully (status %d)", st);
    if (waitpid(b, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0)
        fatal("process S2 did not finish successfully (status %d)", st);
}

inline void child_setup() {
    g_kill_on_fatal = getppid();                 // a failing child takes the parent down
    prctl(PR_SET_PDEATHSIG, SIGKILL);            // and a dying parent takes the child down
}

// ===========================================================================
// Part II.2 : three processes + pipes
// ===========================================================================
template <int V>
int run_part2_2(int argc, char** argv, int default_iters = 1000) {
    char *in_f, *out_f; int N;
    if (!parse_part2_args(argc, argv, V, default_iters, in_f, out_f, N)) return 1;

    auto t0 = Clock::now();
    image_t* img = read_ppm_file(in_f);
    auto t1 = Clock::now();
    image_t* out = allocate_image(img->width, img->height);

    auto p1 = make_pipe();   // S1 -> S2
    auto p2 = make_pipe();   // S2 -> S3

    fflush(stdout);
    auto c0 = Clock::now();
    pid_t pid1 = fork();
    if (pid1 < 0) fatal("fork: %s", strerror(errno));
    if (pid1 == 0) {                                   // ---- process S1
        child_setup();
        close(p1.first); close(p2.first); close(p2.second);
        FdWriter w(p1.second);
        Sender<FdWriter> tx(w);
        stage1<V>(img, N, tx);
        close(p1.second);
        _exit(0);
    }
    pid_t pid2 = fork();
    if (pid2 < 0) fatal("fork: %s", strerror(errno));
    if (pid2 == 0) {                                   // ---- process S2
        child_setup();
        close(p1.second); close(p2.first);
        FdReader r(p1.first);
        FdWriter w(p2.second);
        Receiver<FdReader> rx(r, "S1->S2");
        Sender<FdWriter> tx(w);
        stage2<V>(img, N, rx, tx);
        close(p1.first); close(p2.second);
        _exit(0);
    }
    // ---- process S3 (parent)
    close(p1.first); close(p1.second); close(p2.second);
    {
        FdReader r(p2.first);
        Receiver<FdReader> rx(r, "S2->S3");
        stage3<V>(img, out, N, rx);
    }
    close(p2.first);
    wait_children_ok(pid1, pid2);
    auto c1 = Clock::now();

    auto w0 = Clock::now();
    write_ppm_file(out_f, out);
    auto w1 = Clock::now();

    char title[96];
    snprintf(title, sizeof title, "Part 2.2 Variant %d - Processes + Pipes", V);
    print_report(title, VERIFIED_2LINKS, ms_between(t0, t1), ms_between(c0, c1),
                 ms_between(w0, w1), fnv_image(out));
    return 0;
}

// ===========================================================================
// Part II.3 : three processes + shared memory + semaphores
// ===========================================================================
struct ShmRegion {
    ShmQueue q1;   // S1 -> S2
    ShmQueue q2;   // S2 -> S3
};

template <int V>
int run_part2_3(int argc, char** argv, int default_iters = 1000) {
    char *in_f, *out_f; int N;
    if (!parse_part2_args(argc, argv, V, default_iters, in_f, out_f, N)) return 1;

    auto t0 = Clock::now();
    image_t* img = read_ppm_file(in_f);
    auto t1 = Clock::now();
    image_t* out = allocate_image(img->width, img->height);

    ShmRegion* shm = (ShmRegion*)mmap(nullptr, sizeof(ShmRegion), PROT_READ | PROT_WRITE,
                                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shm == MAP_FAILED) fatal("mmap: %s", strerror(errno));
    shm->q1.init();
    shm->q2.init();

    fflush(stdout);
    auto c0 = Clock::now();
    pid_t pid1 = fork();
    if (pid1 < 0) fatal("fork: %s", strerror(errno));
    if (pid1 == 0) {                                   // ---- process S1
        child_setup();
        ShmWriter w(&shm->q1);
        Sender<ShmWriter> tx(w);
        stage1<V>(img, N, tx);
        _exit(0);
    }
    pid_t pid2 = fork();
    if (pid2 < 0) fatal("fork: %s", strerror(errno));
    if (pid2 == 0) {                                   // ---- process S2
        child_setup();
        ShmReader r(&shm->q1);
        ShmWriter w(&shm->q2);
        Receiver<ShmReader> rx(r, "S1->S2");
        Sender<ShmWriter> tx(w);
        stage2<V>(img, N, rx, tx);
        _exit(0);
    }
    {                                                  // ---- process S3 (parent)
        ShmReader r(&shm->q2);
        Receiver<ShmReader> rx(r, "S2->S3");
        stage3<V>(img, out, N, rx);
    }
    wait_children_ok(pid1, pid2);
    auto c1 = Clock::now();

    shm->q1.destroy();
    shm->q2.destroy();
    munmap(shm, sizeof(ShmRegion));

    auto w0 = Clock::now();
    write_ppm_file(out_f, out);
    auto w1 = Clock::now();

    char title[112];
    snprintf(title, sizeof title, "Part 2.3 Variant %d - Processes + Shared Memory + Semaphores", V);
    print_report(title, VERIFIED_2LINKS, ms_between(t0, t1), ms_between(c0, c1),
                 ms_between(w0, w1), fnv_image(out));
    return 0;
}

// ===========================================================================
// Part III : two computers over TCP.
//   SPLIT 1 : A = S1+S2, B = S3          SPLIT 2 : A = S1, B = S2+S3
// A is the server (listens), B is the client.
// Wire format: header {W,H,iters,fnv(original image)} as 4 x uint32 network
// order, then the raw original image (B needs it for S2/S3), then the pixel
// stream (seq in network byte order) terminated by the checksum trailer.
// ===========================================================================
template <int V, int SPLIT>
int run_part3_A(int argc, char** argv, int default_iters = 1000) {
    int port = (argc >= 2) ? std::atoi(argv[1]) : 12345;
    char* in_f = (argc >= 3) ? argv[2] : (char*)"images/1.ppm";
    int N = (argc >= 4) ? std::atoi(argv[3]) : default_iters;
    if (!iterations_valid(V, N)) return 1;

    auto t0 = Clock::now();
    image_t* img = read_ppm_file(in_f);
    auto t1 = Clock::now();
    const int W = img->width, H = img->height;

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) fatal("socket: %s", strerror(errno));
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
    if (bind(srv, (sockaddr*)&addr, sizeof addr) < 0) fatal("bind: %s", strerror(errno));
    if (listen(srv, 1) < 0) fatal("listen: %s", strerror(errno));
    printf("[Part 3.%d Variant %d - Computer A] listening on port %d ...\n", SPLIT, V, port);
    fflush(stdout);
    int fd = accept(srv, nullptr, nullptr);
    if (fd < 0) fatal("accept: %s", strerror(errno));
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);
    printf("Connected to Computer B\n");
    fflush(stdout);

    auto c0 = Clock::now();
    uint32_t hdr[4] = {htonl(W), htonl(H), htonl(N), htonl(fnv_image(img))};
    write_fully(fd, hdr, sizeof hdr);
    std::vector<uint8_t> raw((size_t)W * H * 3);
    size_t k = 0;
    for (int i = 0; i < H; ++i)
        for (int j = 0; j < W; ++j)
            for (int c = 0; c < 3; ++c) raw[k++] = img->image_pixels[i][j][c];
    write_fully(fd, raw.data(), raw.size());

    {
        FdWriter w(fd, /*net=*/true);
        Sender<FdWriter> tx(w);
        if constexpr (SPLIT == 1) stage12<V>(img, N, tx);
        else                      stage1<V>(img, N, tx);
    }
    // wait until B sends 'K' and closes: guarantees that B consumed and verified everything
    char ack = 0;
    ssize_t rn;
    do { rn = read(fd, &ack, 1); } while (rn < 0 && errno == EINTR);
    if (rn != 1 || ack != 'K')
        fatal("Computer B aborted or failed verification");
    char dummy;
    while ((rn = read(fd, &dummy, 1)) > 0 || (rn < 0 && errno == EINTR)) {}
    auto c1 = Clock::now();
    close(fd);
    close(srv);

    char title[96];
    snprintf(title, sizeof title, "Part 3.%d Variant %d - Computer A (%s)", SPLIT, V,
             SPLIT == 1 ? "S1+S2" : "S1");
    printf("[%s] Read Time: %.3f ms | Send+Compute Time: %.3f ms\n", title,
           ms_between(t0, t1), ms_between(c0, c1));
    return 0;
}

template <int V, int SPLIT>
int run_part3_B(int argc, char** argv) {
    const char* ip = (argc >= 2) ? argv[1] : "127.0.0.1";
    int port = (argc >= 3) ? std::atoi(argv[2]) : 12345;
    char* out_f = (argc >= 4) ? argv[3] : (char*)"output.ppm";

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &sa.sin_addr) <= 0) fatal("bad IP address '%s'", ip);
    printf("[Part 3.%d Variant %d - Computer B] connecting to %s:%d ...\n", SPLIT, V, ip, port);
    fflush(stdout);
    constexpr int MAX_CONNECT_RETRIES = 600; // ~60 s at 100 ms/retry
    int sock = -1;
    for (int attempt = 0; attempt < MAX_CONNECT_RETRIES; ++attempt) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) fatal("socket: %s", strerror(errno));
        if (connect(sock, (sockaddr*)&sa, sizeof sa) == 0) break;
        int err = errno;
        close(sock);
        sock = -1;
        if (attempt + 1 == MAX_CONNECT_RETRIES)
            fatal("connect to %s:%d failed after %d attempts: %s",
                  ip, port, MAX_CONNECT_RETRIES, strerror(err));
        usleep(100000);   // retry until A is up
    }
    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);
    printf("Connected to Computer A\n");
    fflush(stdout);

    auto c0 = Clock::now();
    uint32_t hdr[4];
    read_fully(sock, hdr, sizeof hdr);
    const int W = (int)ntohl(hdr[0]), H = (int)ntohl(hdr[1]), N = (int)ntohl(hdr[2]);
    const uint32_t img_hash = ntohl(hdr[3]);
    if (W <= 0 || H <= 0 || !iterations_valid(V, N)) fatal("invalid header from computer A");

    std::vector<uint8_t> raw((size_t)W * H * 3);
    read_fully(sock, raw.data(), raw.size());
    image_t* img = allocate_image(W, H);
    size_t k = 0;
    for (int i = 0; i < H; ++i)
        for (int j = 0; j < W; ++j)
            for (int c = 0; c < 3; ++c) img->image_pixels[i][j][c] = raw[k++];
    if (fnv_image(img) != img_hash)
        fatal("original image corrupted in transit (checksum mismatch)");
    image_t* out = allocate_image(W, H);

    {
        FdReader r(sock, /*net=*/true);
        Receiver<FdReader> rx(r, "A->B");
        if constexpr (SPLIT == 1) stage3<V>(img, out, N, rx);
        else                      stage23<V>(img, out, N, rx);
    }
    auto c1 = Clock::now();
    const char ok = 'K';
    write_fully(sock, &ok, 1);
    close(sock);

    auto w0 = Clock::now();
    write_ppm_file(out_f, out);
    auto w1 = Clock::now();

    char title[96];
    snprintf(title, sizeof title, "Part 3.%d Variant %d - Computer B (%s)", SPLIT, V,
             SPLIT == 1 ? "S3" : "S2+S3");
    // Compute Time here = connect -> final pixel (includes waiting for A and the network)
    print_report(title, "PASSED - original image checksum, seq order and FNV-1a pixel checksum verified on A->B",
                 0.0, ms_between(c0, c1), ms_between(w0, w1), fnv_image(out));
    return 0;
}

#endif
