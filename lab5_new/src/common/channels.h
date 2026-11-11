// channels.h
// The three IPC mechanisms of Part II (+ sockets for Part III) behind one tiny
// interface, plus Sender/Receiver wrappers that do the verification:
//
//   * ORDER    : every received pixel must carry the next expected seq number
//   * INTEGRITY: sender and receiver both fold every (seq,r,g,b) into an
//                FNV-1a checksum; the sender ships its final checksum as a
//                trailer message, and the receiver compares it with its own.
//
// Channel interface:  put(msg)  flush()  get() -> msg
#ifndef CHANNELS_H
#define CHANNELS_H

#include "common_pipeline.h"
#include <atomic>
#include <semaphore.h>
#include <fcntl.h>
#include <sched.h>
#include <arpa/inet.h>

// ===========================================================================
// Verification wrappers
// ===========================================================================
constexpr uint8_t TRAILER_R = 0xC5, TRAILER_G = 0x5A, TRAILER_B = 0x3C;

#ifdef INJECT_FAULT
// Fault-injection build (make fault_test): corrupt one pixel in transit AFTER the
// sender's checksum was updated, to prove the checker really detects errors.
#ifndef INJECT_AT
#define INJECT_AT 12345
#endif
#endif

template <class Chan>
class Sender {
    Chan& c_;
    uint32_t h_ = FNV_INIT;
    uint64_t n_ = 0;
public:
    explicit Sender(Chan& c) : c_(c) {}
    void send(const PixelMsg& m) {
        h_ = fnv_msg(h_, m);
        ++n_;
#ifdef INJECT_FAULT
        if (n_ == INJECT_AT) {
            PixelMsg bad = m;
            bad.r ^= 0x01;
            c_.put(bad);
            return;
        }
#endif
        c_.put(m);
    }
    // end of stream: ship checksum, flush everything
    void finish() {
        c_.put(make_msg((int32_t)h_, TRAILER_R, TRAILER_G, TRAILER_B));
        c_.flush();
    }
};

template <class Chan>
class Receiver {
    Chan& c_;
    const char* link_;
    uint32_t h_ = FNV_INIT;
public:
    Receiver(Chan& c, const char* link) : c_(c), link_(link) {}
    // returns the next pixel; aborts unless it is exactly pixel #expected_seq
    PixelMsg get(int expected_seq) {
        PixelMsg m = c_.get();
        if (m.seq != expected_seq)
            fatal("[%s] pixel out of order: expected seq %d, got %d", link_, expected_seq, m.seq);
        h_ = fnv_msg(h_, m);
        return m;
    }
    void finish() {
        PixelMsg t = c_.get();
        if (t.r != TRAILER_R || t.g != TRAILER_G || t.b != TRAILER_B)
            fatal("[%s] stream longer than expected (trailer not found)", link_);
        if ((uint32_t)t.seq != h_)
            fatal("[%s] checksum mismatch: sender %08x, receiver %08x (pixel data corrupted)",
                  link_, (uint32_t)t.seq, h_);
    }
};

// ===========================================================================
// Part II.1 : threads of one process, lock-free SPSC ring, atomics only.
// Producer/consumer publish their indices in batches (BATCH items) so the
// shared cache lines are not bounced for every single pixel.
// ===========================================================================
inline void cpu_relax() {
#if defined(__x86_64__) || defined(_M_X64)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
}

// spin politely: pause first, yield the CPU if we have been spinning for long
// (keeps the program live even on machines with fewer than 3 free cores)
inline void backoff(int& spins) {
    cpu_relax();
    if (++spins > 2000) { sched_yield(); spins = 0; }
}

class AtomicChan {
    static constexpr size_t CAP = 1u << 14;   // power of two
    static constexpr size_t MASK = CAP - 1;
    static constexpr size_t BATCH = 256;

    PixelMsg* buf_;
    alignas(64) std::atomic<size_t> tail_{0};   // written by producer only
    alignas(64) std::atomic<size_t> head_{0};   // written by consumer only
    // producer-private
    alignas(64) size_t p_tail_ = 0, p_pub_ = 0, p_head_cache_ = 0;
    // consumer-private
    alignas(64) size_t c_head_ = 0, c_pub_ = 0, c_tail_cache_ = 0;

    void publish_tail() {
        if (p_pub_ != p_tail_) {
            tail_.store(p_tail_, std::memory_order_release);
            p_pub_ = p_tail_;
        }
    }
    void publish_head() {
        if (c_pub_ != c_head_) {
            head_.store(c_head_, std::memory_order_release);
            c_pub_ = c_head_;
        }
    }
public:
    AtomicChan() : buf_(new PixelMsg[CAP]) {}
    ~AtomicChan() { delete[] buf_; }
    AtomicChan(const AtomicChan&) = delete;
    AtomicChan& operator=(const AtomicChan&) = delete;

    void put(const PixelMsg& m) {                       // producer thread only
        int spins = 0;
        while (p_tail_ - p_head_cache_ >= CAP) {        // ring full
            publish_tail();                             // let the consumer see what we have
            p_head_cache_ = head_.load(std::memory_order_acquire);
            if (p_tail_ - p_head_cache_ >= CAP) backoff(spins);
        }
        buf_[p_tail_ & MASK] = m;
        ++p_tail_;
        if (p_tail_ - p_pub_ >= BATCH) publish_tail();
    }
    void flush() { publish_tail(); }

    PixelMsg get() {                                    // consumer thread only
        int spins = 0;
        while (c_head_ == c_tail_cache_) {              // ring empty
            publish_head();                             // free space for the producer
            c_tail_cache_ = tail_.load(std::memory_order_acquire);
            if (c_head_ == c_tail_cache_) backoff(spins);
        }
        PixelMsg m = buf_[c_head_ & MASK];
        ++c_head_;
        if (c_head_ - c_pub_ >= BATCH) publish_head();
        return m;
    }
};

// ===========================================================================
// Part II.2 and Part III : byte-stream channels over a file descriptor
// (pipe or TCP socket). `net` converts the seq field to network byte order.
// ===========================================================================
class FdWriter {
    static constexpr int CHUNK = 512;      // 512 * 8 B = 4096 B = PIPE_BUF
    int fd_;
    bool net_;
    PixelMsg buf_[CHUNK];
    int n_ = 0;
public:
    explicit FdWriter(int fd, bool net = false) : fd_(fd), net_(net) {}
    void put(const PixelMsg& m) {
        buf_[n_] = m;
        if (net_) buf_[n_].seq = (int32_t)htonl((uint32_t)m.seq);
        if (++n_ == CHUNK) flush();
    }
    void flush() {
        if (n_ > 0) {
            write_fully(fd_, buf_, (size_t)n_ * sizeof(PixelMsg));
            n_ = 0;
        }
    }
};

class FdReader {
    int fd_;
    bool net_;
    uint8_t raw_[4096];
    size_t have_ = 0, off_ = 0;
public:
    explicit FdReader(int fd, bool net = false) : fd_(fd), net_(net) {}
    PixelMsg get() {
        // read() may return any number of bytes: keep reading until one whole
        // message is buffered (never truncate / drop a partial message)
        while (have_ - off_ < sizeof(PixelMsg)) {
            if (off_ > 0) {
                memmove(raw_, raw_ + off_, have_ - off_);
                have_ -= off_;
                off_ = 0;
            }
            ssize_t k = read(fd_, raw_ + have_, sizeof(raw_) - have_);
            if (k < 0) {
                if (errno == EINTR) continue;
                fatal("read() failed: %s", strerror(errno));
            }
            if (k == 0) fatal("unexpected EOF: peer closed the stream before it was complete");
            have_ += (size_t)k;
        }
        PixelMsg m;
        memcpy(&m, raw_ + off_, sizeof(m));
        off_ += sizeof(m);
        if (net_) m.seq = (int32_t)ntohl((uint32_t)m.seq);
        return m;
    }
};

inline std::pair<int, int> make_pipe() {
    int p[2];
    if (pipe(p) < 0) fatal("pipe: %s", strerror(errno));
#ifdef F_SETPIPE_SZ
    fcntl(p[0], F_SETPIPE_SZ, 1 << 20);   // best effort: bigger kernel buffer
#endif
    return {p[0], p[1]};
}

// ===========================================================================
// Part II.3 : shared memory ring of chunks, synchronised with POSIX
// (process-shared) counting semaphores.  `empty` counts free slots, `full`
// counts filled slots; single producer / single consumer per queue, so no
// extra mutex is needed.
// ===========================================================================
constexpr int SHM_CHUNK = 256;
constexpr int SHM_SLOTS = 64;

struct ShmChunk {
    int count;
    PixelMsg px[SHM_CHUNK];
};

struct ShmQueue {
    ShmChunk slots[SHM_SLOTS];
    int in, out;           // in: touched only by producer, out: only by consumer
    sem_t empty, full;

    void init() {
        in = out = 0;
        sem_init(&empty, /*pshared=*/1, SHM_SLOTS);
        sem_init(&full, /*pshared=*/1, 0);
    }
    void destroy() {
        sem_destroy(&empty);
        sem_destroy(&full);
    }
};

inline void sem_wait_retry(sem_t* s) {
    while (sem_wait(s) < 0) {
        if (errno != EINTR) fatal("sem_wait: %s", strerror(errno));
    }
}

class ShmWriter {
    ShmQueue* q_;
    ShmChunk cur_;
public:
    explicit ShmWriter(ShmQueue* q) : q_(q) { cur_.count = 0; }
    void put(const PixelMsg& m) {
        cur_.px[cur_.count++] = m;
        if (cur_.count == SHM_CHUNK) flush();
    }
    void flush() {
        if (cur_.count == 0) return;
        sem_wait_retry(&q_->empty);                       // wait for a free slot
        ShmChunk& s = q_->slots[q_->in];
        s.count = cur_.count;
        memcpy(s.px, cur_.px, (size_t)cur_.count * sizeof(PixelMsg));
        q_->in = (q_->in + 1) % SHM_SLOTS;
        sem_post(&q_->full);                              // announce a filled slot
        cur_.count = 0;
    }
};

class ShmReader {
    ShmQueue* q_;
    ShmChunk cur_;
    int pos_ = 0;
public:
    explicit ShmReader(ShmQueue* q) : q_(q) { cur_.count = 0; }
    PixelMsg get() {
        if (pos_ >= cur_.count) {
            sem_wait_retry(&q_->full);                    // wait for a filled slot
            ShmChunk& s = q_->slots[q_->out];
            cur_.count = s.count;
            memcpy(cur_.px, s.px, (size_t)s.count * sizeof(PixelMsg));
            q_->out = (q_->out + 1) % SHM_SLOTS;
            sem_post(&q_->empty);                         // slot is free again
            pos_ = 0;
        }
        return cur_.px[pos_++];
    }
};

#endif
