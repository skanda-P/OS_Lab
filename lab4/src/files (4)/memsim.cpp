// memsim.cpp -- Memory virtualization simulator (Lab 4)
//
// One binary covers all five parts of the assignment:
//   --pt single|two     single-level or two-level page table   (Parts I, II)
//   --tlb               add the 128-entry fully associative TLB (Part III)
//   --frames N          limit physical memory to N frames       (Part IV)
//   --frames-frac X     same, as a fraction of the 2^20 virtual pages
// Part V is the "sim_seconds" line printed at the end of every run.
//
// See README.md for the full explanation of every design decision.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

typedef uint8_t  u8;
typedef uint32_t u32;
typedef uint64_t u64;

// ----------------------------------------------------------------------------
// Constants taken from the assignment
// ----------------------------------------------------------------------------
static constexpr u32 PAGE_BITS  = 12;                 // 4 KB pages
static constexpr u32 VPN_BITS   = 32 - PAGE_BITS;     // 20
static constexpr u32 NUM_PAGES  = 1u << VPN_BITS;     // 2^20 virtual pages
static constexpr u32 INVALID    = 0xFFFFFFFFu;        // "no mapping"
static constexpr u64 TLB_CYCLES  = 1;
static constexpr u64 MEM_CYCLES  = 200;
static constexpr u64 DISK_CYCLES = 20000000ULL;       // 20 million
static constexpr u64 FNV_PRIME   = 1099511628211ULL;  // hash multiplier
static constexpr u64 FNV_BASIS   = 1469598103934665603ULL;

// ----------------------------------------------------------------------------
// Command line configuration
// ----------------------------------------------------------------------------
struct Config {
    std::string trace;
    std::string pt = "single";
    bool   tlb = false;
    u64    frames = 0;          // 0 => unlimited (Parts I-III)
    double frac = -1.0;         // if >= 0, frames = frac * 2^20
    bool   plru = false;        // optional: pseudo-LRU TLB (3-bit counters)
    u32    plruPeriod = 128;    // accesses between counter decays
    u64    maxAccesses = 0;     // 0 => whole trace
    std::string hist;           // write hot-page CDF to this csv
    bool   csv = false;         // print a single csv row instead of report
};

// ----------------------------------------------------------------------------
// Page tables.  All expose the same interface so the simulation loop is a
// template and has zero virtual-call overhead.
// ----------------------------------------------------------------------------

// Single level: one flat array indexed by VPN. 2^20 entries * 4 B = 4 MiB.
struct SinglePT {
    static constexpr int LEVELS = 1;   // memory accesses for a page walk
    std::vector<u32> pte;
    SinglePT() : pte(NUM_PAGES, INVALID) {}
    inline u32  lookup(u32 vpn) const { return pte[vpn]; }
    inline void map(u32 vpn, u32 pfn) { pte[vpn] = pfn; }
    inline void unmap(u32 vpn)        { pte[vpn] = INVALID; }
    u64 allocatedEntries() const      { return NUM_PAGES; }
    u64 extraAccessedEntries() const  { return 0; }   // no upper level
};

// Two level: 10-bit L1 index, 10-bit L2 index. L2 tables are allocated lazily.
struct TwoLevelPT {
    static constexpr int LEVELS = 2;
    static constexpr u32 L2_ENTRIES = 1024;
    std::vector<u32*> l1;
    u64 l2Count = 0;
    TwoLevelPT() : l1(L2_ENTRIES, nullptr) {}
    ~TwoLevelPT() { for (u32* t : l1) delete[] t; }
    inline u32 lookup(u32 vpn) const {
        const u32* t = l1[vpn >> 10];
        return t ? t[vpn & 1023] : INVALID;
    }
    inline void map(u32 vpn, u32 pfn) {
        u32*& t = l1[vpn >> 10];
        if (!t) {
            t = new u32[L2_ENTRIES];
            std::fill(t, t + L2_ENTRIES, INVALID);
            ++l2Count;
        }
        t[vpn & 1023] = pfn;
    }
    inline void unmap(u32 vpn) { l1[vpn >> 10][vpn & 1023] = INVALID; }
    // L1 (1024 entries) + every allocated L2 table.
    u64 allocatedEntries() const { return L2_ENTRIES + l2Count * L2_ENTRIES; }
    // Every allocated L2 table was reached through exactly one L1 entry, so
    // l2Count L1 entries have been accessed in addition to the leaf entries.
    u64 extraAccessedEntries() const { return l2Count; }
};

// ----------------------------------------------------------------------------
// Physical memory: frame metadata + exact LRU list (doubly linked, O(1)).
// Frame allocation scheme: "bump" allocator.  Frame numbers are handed out
// 0,1,2,... in order of first touch.  When memory is full, the LRU frame is
// reused directly for the incoming page, so no separate free list is needed.
// ----------------------------------------------------------------------------
struct PhysMem {
    static constexpr u32 NIL = 0xFFFFFFFFu;
    u32 cap, used = 0, head = NIL, tail = NIL;     // head = MRU, tail = LRU
    std::vector<u32> vpnOf, prv, nxt;
    explicit PhysMem(u32 c) : cap(c), vpnOf(c), prv(c), nxt(c) {}

    inline void unlink(u32 f) {
        if (prv[f] != NIL) nxt[prv[f]] = nxt[f]; else head = nxt[f];
        if (nxt[f] != NIL) prv[nxt[f]] = prv[f]; else tail = prv[f];
    }
    inline void pushFront(u32 f) {
        prv[f] = NIL; nxt[f] = head;
        if (head != NIL) prv[head] = f; else tail = f;
        head = f;
    }
    inline void touch(u32 f) {                 // mark frame most recently used
        if (f == head) return;
        unlink(f); pushFront(f);
    }
    // Returns the frame for `vpn`. If a resident page had to be evicted its
    // VPN is returned in `evicted` (else INVALID).
    inline u32 allocate(u32 vpn, u32& evicted) {
        u32 f;
        if (used < cap) { f = used++; evicted = INVALID; }
        else            { f = tail; evicted = vpnOf[f]; unlink(f); }
        vpnOf[f] = vpn;
        pushFront(f);
        return f;
    }
};

// ----------------------------------------------------------------------------
// TLB: 128 entries, fully associative.
//   * exact LRU (default): doubly linked list over the 128 slots, O(1).
//   * pseudo-LRU (optional, --tlb-policy plru): 3-bit counter per entry as in
//     the lecture slide.  Increment on access (saturate at 7), decay all
//     counters every `period` accesses, new entry gets 7, victim = smallest.
// A direct-indexed table slot[vpn] makes the "fully associative" search O(1)
// in the simulator; it is purely a simulator speed trick and does not change
// the simulated behaviour or the 1-cycle TLB access cost.
// ----------------------------------------------------------------------------
struct TLB {
    static constexpr int N = 128;
    static constexpr u8 NIL = 255;
    u32 vpn[N], pfn[N];
    u8  prv[N], nxt[N], ctr[N];
    u8  head = NIL, tail = NIL;
    u8  freeSlots[N]; int freeTop;
    std::vector<u8> slot;
    bool plru; u32 period, tick = 0;

    TLB(bool p, u32 per) : slot(NUM_PAGES, NIL), plru(p), period(per) {
        for (int i = 0; i < N; ++i) { freeSlots[i] = (u8)(N - 1 - i); ctr[i] = 0; }
        freeTop = N;
    }
    inline void unlink(u8 s) {
        if (prv[s] != NIL) nxt[prv[s]] = nxt[s]; else head = nxt[s];
        if (nxt[s] != NIL) prv[nxt[s]] = prv[s]; else tail = prv[s];
    }
    inline void pushFront(u8 s) {
        prv[s] = NIL; nxt[s] = head;
        if (head != NIL) prv[head] = s; else tail = s;
        head = s;
    }
    // Returns PFN on hit, INVALID on miss. Updates replacement state on hit.
    inline u32 lookup(u32 v) {
        if (plru && ++tick >= period) {
            tick = 0;
            for (int i = 0; i < N; ++i) if (ctr[i]) --ctr[i];
        }
        u8 s = slot[v];
        if (s == NIL) return INVALID;
        if (plru) { if (ctr[s] < 7) ++ctr[s]; }
        else if (s != head) { unlink(s); pushFront(s); }
        return pfn[s];
    }
    inline void insert(u32 v, u32 p) {
        u8 s;
        if (freeTop > 0) s = freeSlots[--freeTop];
        else {
            if (plru) { s = 0; for (int i = 1; i < N; ++i) if (ctr[i] < ctr[s]) s = (u8)i; }
            else      { s = tail; unlink(s); }
            slot[vpn[s]] = NIL;
        }
        vpn[s] = v; pfn[s] = p; slot[v] = s;
        if (plru) ctr[s] = 7; else pushFront(s);
    }
    // Called when a page is evicted from physical memory.
    inline void invalidate(u32 v) {
        u8 s = slot[v];
        if (s == NIL) return;
        slot[v] = NIL;
        if (!plru) unlink(s);
        freeSlots[freeTop++] = s;
    }
};

// ----------------------------------------------------------------------------
// Trace reading: chunked fread + manual parse (much faster than iostreams).
// Line format: <address>,<I/D>,<R/W>   address is hexadecimal (0x optional).
// ----------------------------------------------------------------------------
static inline bool parseLine(const char* p, const char* e, u32& addr,
                             char& type, char& rw) {
    while (p < e && (*p == ' ' || *p == '\t')) ++p;
    if (p >= e) return false;
    if (e - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    u64 v = 0; bool any = false;
    for (; p < e; ++p) {
        char c = *p; int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = (v << 4) | (u64)d; any = true;
    }
    if (!any) return false;
    addr = (u32)v;                       // keep the lower 32 bits only
    type = rw = '?';
    if (p < e && *p == ',') {
        ++p; while (p < e && *p == ' ') ++p;
        if (p < e) type = *p;
        while (p < e && *p != ',') ++p;
        if (p < e) { ++p; while (p < e && *p == ' ') ++p; if (p < e) rw = *p; }
    }
    return true;
}

// Calls fn(lineStart, lineEnd) for each line; fn returns false to stop.
template <class F>
static void forEachLine(FILE* f, F&& fn) {
    const size_t CH = 1u << 22;                 // 4 MiB chunks
    std::vector<char> buf(CH * 2);
    char* base = buf.data();
    size_t have = 0;
    for (;;) {
        size_t n = fread(base + have, 1, CH, f);
        size_t total = have + n, start = 0;
        for (;;) {
            char* nl = (char*)memchr(base + start, '\n', total - start);
            if (!nl) break;
            if (!fn(base + start, nl)) return;
            start = (size_t)(nl - base) + 1;
        }
        if (n == 0) { if (start < total) fn(base + start, base + total); return; }
        have = total - start;
        memmove(base, base + start, have);
    }
}

// ----------------------------------------------------------------------------
// Results
// ----------------------------------------------------------------------------
struct Result {
    u64 accesses = 0, instr = 0, data = 0, reads = 0, writes = 0;
    u64 cycles = 0, distinct = 0;
    u64 tlbHits = 0, tlbMisses = 0;
    u64 faults = 0, replFaults = 0;
    u64 framesUsed = 0, framesCap = 0;
    u64 ptAllocated = 0, ptAccessed = 0;
    u64 hash = FNV_BASIS;
    double simSeconds = 0;
    std::vector<u64> counts;     // per-VPN access counts (for hot-page plot)
};

// ----------------------------------------------------------------------------
// The simulation loop.  Specialised at compile time for page-table type and
// for TLB on/off, so the hot loop has no unnecessary branches.
// ----------------------------------------------------------------------------
template <class PT, bool TLBON>
static bool run(const Config& cfg, Result& r) {
    FILE* f = fopen(cfg.trace.c_str(), "rb");
    if (!f) { perror("open trace"); return false; }

    u64 cap = NUM_PAGES;
    if (cfg.frac >= 0) cap = std::max<u64>(1, (u64)(cfg.frac * NUM_PAGES));
    else if (cfg.frames > 0) cap = std::min<u64>(cfg.frames, NUM_PAGES);
    r.framesCap = cap;

    PT pt;
    PhysMem pm((u32)cap);
    TLB tlb(cfg.plru, cfg.plruPeriod);
    r.counts.assign(NUM_PAGES, 0);
    u64* counts = r.counts.data();

    u64 cycles = 0, H = FNV_BASIS;
    u64 accesses = 0, distinct = 0, tlbHits = 0, tlbMisses = 0;
    u64 faults = 0, replFaults = 0, instr = 0, data = 0, reads = 0, writes = 0;
    const u64 maxAcc = cfg.maxAccesses;

    auto t0 = std::chrono::steady_clock::now();

    forEachLine(f, [&](const char* s, const char* e) -> bool {
        u32 addr; char ty, rw;
        if (!parseLine(s, e, addr, ty, rw)) return true;   // skip junk line
        if (maxAcc && accesses >= maxAcc) return false;
        ++accesses;
        instr += (ty == 'I'); data += (ty == 'D');
        reads += (rw == 'R');  writes += (rw == 'W');

        const u32 vpn = addr >> PAGE_BITS;
        if (counts[vpn]++ == 0) ++distinct;                // first touch

        u32 pfn = INVALID;
        if (TLBON) { cycles += TLB_CYCLES; pfn = tlb.lookup(vpn); }

        if (TLBON && pfn != INVALID) {
            ++tlbHits;
            pm.touch(pfn);                                  // exact LRU
        } else {
            if (TLBON) ++tlbMisses;
            cycles += (u64)PT::LEVELS * MEM_CYCLES;         // page walk
            pfn = pt.lookup(vpn);
            if (pfn == INVALID) {                           // page fault
                ++faults;
                u32 evicted;
                pfn = pm.allocate(vpn, evicted);
                if (evicted != INVALID) {                   // replacement
                    pt.unmap(evicted);
                    if (TLBON) tlb.invalidate(evicted);
                    cycles += DISK_CYCLES;
                    ++replFaults;
                }
                pt.map(vpn, pfn);
            } else {
                pm.touch(pfn);
            }
            if (TLBON) tlb.insert(vpn, pfn);
        }
        cycles += MEM_CYCLES;                               // the data access
        H = (H ^ (u64)pfn) * FNV_PRIME;                     // H = f(H, frame)
        return true;
    });

    auto t1 = std::chrono::steady_clock::now();
    fclose(f);

    r.simSeconds = std::chrono::duration<double>(t1 - t0).count();
    r.accesses = accesses; r.instr = instr; r.data = data;
    r.reads = reads; r.writes = writes;
    r.cycles = cycles; r.distinct = distinct;
    r.tlbHits = tlbHits; r.tlbMisses = tlbMisses;
    r.faults = faults; r.replFaults = replFaults;
    r.framesUsed = pm.used;
    r.ptAllocated = pt.allocatedEntries();
    r.ptAccessed  = distinct + pt.extraAccessedEntries();
    r.hash = H;
    return true;
}

// ----------------------------------------------------------------------------
// Hot-page analysis (Part I-B): sort pages by access count, report how much of
// the traffic the hottest x% of touched pages receive; optionally dump a CDF.
// ----------------------------------------------------------------------------
static void hotPageAnalysis(const Result& r, const std::string& histFile,
                            bool quiet) {
    std::vector<u64> c;
    for (u64 v : r.counts) if (v) c.push_back(v);
    std::sort(c.begin(), c.end(), std::greater<u64>());
    const size_t n = c.size();
    if (!n) return;

    if (!quiet) {
        const double pcts[] = {0.01, 0.05, 0.10, 0.20, 0.50};
        for (double p : pcts) {
            size_t k = std::max<size_t>(1, (size_t)(p * n));
            u64 s = 0; for (size_t i = 0; i < k; ++i) s += c[i];
            printf("hottest %4.0f%% of touched pages -> %6.2f%% of accesses\n",
                   p * 100, 100.0 * (double)s / (double)r.accesses);
        }
    }
    if (!histFile.empty()) {
        FILE* h = fopen(histFile.c_str(), "w");
        if (!h) { perror("hist"); return; }
        fprintf(h, "page_frac,access_frac\n");
        const int POINTS = 1000;
        u64 cum = 0; size_t idx = 0;
        for (int k = 1; k <= POINTS; ++k) {
            size_t upto = (size_t)((double)k * n / POINTS);
            if (upto == 0) upto = 1;
            for (; idx < upto; ++idx) cum += c[idx];
            fprintf(h, "%.6f,%.6f\n", (double)upto / n, (double)cum / r.accesses);
        }
        fclose(h);
    }
}

// ----------------------------------------------------------------------------
static void usage(const char* a0) {
    fprintf(stderr,
      "usage: %s --trace FILE [--pt single|two] [--tlb] [--frames N | --frames-frac X]\n"
      "          [--tlb-policy lru|plru] [--plru-period K] [--max N]\n"
      "          [--hist out.csv] [--csv]\n", a0);
}

int main(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](void) -> const char* {
            if (i + 1 >= argc) { usage(argv[0]); exit(1); }
            return argv[++i];
        };
        if      (a == "--trace")       cfg.trace = need();
        else if (a == "--pt")          cfg.pt = need();
        else if (a == "--tlb")         cfg.tlb = true;
        else if (a == "--frames")      cfg.frames = strtoull(need(), nullptr, 10);
        else if (a == "--frames-frac") cfg.frac = atof(need());
        else if (a == "--tlb-policy")  cfg.plru = (std::string(need()) == "plru");
        else if (a == "--plru-period") cfg.plruPeriod = (u32)atoi(need());
        else if (a == "--max")         cfg.maxAccesses = strtoull(need(), nullptr, 10);
        else if (a == "--hist")        cfg.hist = need();
        else if (a == "--csv")         cfg.csv = true;
        else { usage(argv[0]); return 1; }
    }
    if (cfg.trace.empty() || (cfg.pt != "single" && cfg.pt != "two")) {
        usage(argv[0]); return 1;
    }

    Result r;
    bool ok;
    if (cfg.pt == "single")
        ok = cfg.tlb ? run<SinglePT, true>(cfg, r)  : run<SinglePT, false>(cfg, r);
    else
        ok = cfg.tlb ? run<TwoLevelPT, true>(cfg, r) : run<TwoLevelPT, false>(cfg, r);
    if (!ok) return 1;

    const double util = r.ptAllocated ? (double)r.ptAccessed / r.ptAllocated : 0;
    const u64 tlbTot = r.tlbHits + r.tlbMisses;
    const double hitRate = tlbTot ? (double)r.tlbHits / tlbTot : 0;
    const double faultRate = r.accesses ? (double)r.faults / r.accesses : 0;

    if (cfg.csv) {
        printf("%s,%s,%d,%llu,%llu,%llu,%llu,%llu,%.6f,%016llx,%llu,%llu,%.6f,%llu,%llu,%.8f,%.3f\n",
               cfg.trace.c_str(), cfg.pt.c_str(), (int)cfg.tlb,
               (unsigned long long)r.framesCap, (unsigned long long)r.accesses,
               (unsigned long long)r.distinct, (unsigned long long)r.framesUsed,
               (unsigned long long)r.cycles, util, (unsigned long long)r.hash,
               (unsigned long long)r.tlbHits, (unsigned long long)r.tlbMisses,
               hitRate, (unsigned long long)r.faults,
               (unsigned long long)r.replFaults, faultRate, r.simSeconds);
        hotPageAnalysis(r, cfg.hist, true);
        return 0;
    }

    printf("trace              : %s\n", cfg.trace.c_str());
    printf("page table         : %s-level   TLB: %s%s\n", cfg.pt == "single" ? "1" : "2",
           cfg.tlb ? "on" : "off", cfg.tlb && cfg.plru ? " (pseudo-LRU)" : "");
    printf("memory requests    : %llu  (I=%llu D=%llu, R=%llu W=%llu)\n",
           (unsigned long long)r.accesses, (unsigned long long)r.instr,
           (unsigned long long)r.data, (unsigned long long)r.reads,
           (unsigned long long)r.writes);
    printf("distinct pages     : %llu\n", (unsigned long long)r.distinct);
    printf("frame capacity     : %llu\n", (unsigned long long)r.framesCap);
    printf("[A] frames used    : %llu\n", (unsigned long long)r.framesUsed);
    printf("[C] total cycles   : %llu\n", (unsigned long long)r.cycles);
    printf("[D] PT utilization : %.6f  (%llu accessed / %llu allocated entries)\n", util,
           (unsigned long long)r.ptAccessed, (unsigned long long)r.ptAllocated);
    printf("[E] hash           : %016llx\n", (unsigned long long)r.hash);
    if (cfg.tlb)
        printf("TLB hits / misses  : %llu / %llu   hit rate = %.4f%%\n",
               (unsigned long long)r.tlbHits, (unsigned long long)r.tlbMisses, 100 * hitRate);
    printf("page faults        : %llu  (rate %.6f%%)  of which replacements: %llu\n",
           (unsigned long long)r.faults, 100 * faultRate, (unsigned long long)r.replFaults);
    printf("[B] hot-page analysis:\n");
    hotPageAnalysis(r, cfg.hist, false);
    printf("[Part V] sim_seconds: %.3f\n", r.simSeconds);
    return 0;
}
