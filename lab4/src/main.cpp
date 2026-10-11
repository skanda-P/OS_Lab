// ============================================================================
// CS Memory Virtualization Simulator (Laboratory 4)
//
// Covers all five parts of the assignment in a single, self-contained file:
//   Part I   : Single-Level Page Table & Free Frame Management (Bump Allocator)
//   Part II  : Two-Level Page Table & Verification of Frame Hash Consistency
//   Part III : 128-Entry Fully-Associative TLB with Exact LRU Policy
//   Part IV  : Bounded Physical Memory with LRU Page Replacement & Disk Penalty
//   Part V   : High-Performance Simulation Loop & Runtime Measurement
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// SECTION 1: ARCHITECTURAL CONSTANTS AND SYSTEM CONFIGURATION
// ============================================================================

// Memory Architecture: 32-bit Virtual Address, 4 KB Page Size
constexpr uint32_t PAGE_OFFSET_BITS     = 12;                             // 12 bits for 4 KB offset (2^12 = 4096)
constexpr uint32_t PAGE_SIZE_BYTES      = 1u << PAGE_OFFSET_BITS;         // 4096 bytes per page
constexpr uint32_t VPN_BITS             = 32 - PAGE_OFFSET_BITS;          // 20 bits for Virtual Page Number (VPN)
constexpr uint32_t TOTAL_VIRTUAL_PAGES  = 1u << VPN_BITS;                 // 2^20 = 1,048,576 virtual pages
constexpr uint32_t INVALID_FRAME        = 0xFFFFFFFFu;                    // Sentinel value indicating unmapped/empty

// Cycle Cost Model (as specified in assignment instructions)
constexpr uint64_t TLB_ACCESS_CYCLES    = 1;                              // 1 cycle for TLB lookup
constexpr uint64_t MEMORY_ACCESS_CYCLES = 200;                            // 200 cycles per main memory access
constexpr uint64_t DISK_ACCESS_CYCLES   = 20000000ULL;                    // 20,000,000 cycles for page replacement

// 64-bit FNV-1a Hash Constants for Metric E: H = (H ^ frame_number) * PRIME
constexpr uint64_t FNV_PRIME            = 1099511628211ULL;
constexpr uint64_t FNV_OFFSET_BASIS     = 1469598103934665603ULL;

// Simulator Command-Line Options
struct SimulatorConfig {
    std::string trace_file_path;
    std::string page_table_type   = "single"; // "single" or "two"
    bool        enable_tlb        = false;    // Enables 128-entry TLB (Part III)
    uint64_t    frame_capacity    = 0;        // Max physical frames (0 = unlimited for Parts I-III)
    double      frame_fraction    = -1.0;     // Fraction of 2^20 virtual pages (e.g. 0.5, 0.25)
    uint64_t    max_requests      = 0;        // Optional limit for debugging large traces
    std::string hist_output_file;             // Path to export hot-page CDF CSV
    bool        csv_mode          = false;    // Concise single-line output for batch script
};

// Simulation Execution Metrics & Final Results
struct SimulationResults {
    uint64_t total_requests       = 0;
    uint64_t instruction_requests = 0;
    uint64_t data_requests        = 0;
    uint64_t read_requests        = 0;
    uint64_t write_requests       = 0;
    uint64_t distinct_pages       = 0;

    // Part I & II Metrics
    uint64_t frames_used          = 0;        // Metric A: Distinct physical frames used
    uint64_t total_cycles         = 0;        // Metric C: Total execution cycles
    uint64_t pt_allocated_entries = 0;        // Metric D: Total allocated page table entries
    uint64_t pt_accessed_entries  = 0;        // Metric D: Total accessed page table entries
    uint64_t frame_hash           = FNV_OFFSET_BASIS; // Metric E: Frame hash

    // Part III Metrics (TLB)
    uint64_t tlb_hits             = 0;
    uint64_t tlb_misses           = 0;

    // Part IV Metrics (Bounded Physical Memory & Replacement)
    uint64_t page_faults          = 0;        // Total faults (first touches + reloads)
    uint64_t replacement_faults   = 0;        // Faults requiring disk replacement
    uint64_t frame_capacity       = 0;

    // Part V Metric (Simulator Speed)
    double   simulation_time_sec  = 0.0;

    // Per-VPN access counts for hot-page analysis (Metric B)
    std::vector<uint64_t> page_access_counts;
};

// ============================================================================
// SECTION 2: PAGE TABLE IMPLEMENTATIONS (PARTS I & II)
// ============================================================================

// ----------------------------------------------------------------------------
// SingleLevelPageTable (Part I)
// - Simple flat array of 2^20 entries indexed directly by VPN (size = 4 MiB).
// - Each translation requires exactly 1 memory access (200 cycles).
// ----------------------------------------------------------------------------
class SingleLevelPageTable {
public:
    static constexpr int PAGE_WALK_LEVELS = 1;

private:
    std::vector<uint32_t> entries;

public:
    SingleLevelPageTable() : entries(TOTAL_VIRTUAL_PAGES, INVALID_FRAME) {}

    // Lookup virtual page. Returns physical frame number (PFN) or INVALID_FRAME.
    inline uint32_t lookup(uint32_t vpn) const {
        return entries[vpn];
    }

    // Map virtual page to physical frame.
    inline void map(uint32_t vpn, uint32_t frame_number) {
        entries[vpn] = frame_number;
    }

    // Unmap virtual page when it is evicted to disk.
    inline void unmap(uint32_t vpn) {
        entries[vpn] = INVALID_FRAME;
    }

    // Metric D: Total allocated page table entries (always 2^20 in a flat table).
    uint64_t allocated_entries() const {
        return TOTAL_VIRTUAL_PAGES;
    }

    // Metric D: Extra directory entries accessed (0 for single-level).
    uint64_t extra_accessed_entries() const {
        return 0;
    }
};

// ----------------------------------------------------------------------------
// TwoLevelPageTable (Part II)
// - 32-bit address split:
//     * L1 Page Directory Index : bits 31..22 (10 bits -> 1024 entries)
//     * L2 Page Table Index     : bits 21..12 (10 bits -> 1024 entries per table)
//     * Page Offset             : bits 11..0  (12 bits -> 4096 bytes)
// - L2 tables are allocated lazily only when a page within that 4 MB range is used.
// - Each translation requires 2 memory accesses (L1 lookup + L2 lookup = 400 cycles).
// ----------------------------------------------------------------------------
class TwoLevelPageTable {
public:
    static constexpr int PAGE_WALK_LEVELS = 2;

private:
    static constexpr uint32_t L1_SIZE = 1024;
    static constexpr uint32_t L2_SIZE = 1024;

    std::vector<uint32_t*> l1_directory;
    uint64_t l2_tables_created = 0;

public:
    TwoLevelPageTable() : l1_directory(L1_SIZE, nullptr) {}

    // Clean up all dynamically allocated L2 tables on destruction
    ~TwoLevelPageTable() {
        for (uint32_t* l2_table : l1_directory) {
            delete[] l2_table;
        }
    }

    // Prevent accidental copying
    TwoLevelPageTable(const TwoLevelPageTable&) = delete;
    TwoLevelPageTable& operator=(const TwoLevelPageTable&) = delete;

    // Lookup VPN: checks L1 pointer first, then reads L2 table entry.
    inline uint32_t lookup(uint32_t vpn) const {
        const uint32_t l1_index = vpn >> 10;
        const uint32_t* l2_table = l1_directory[l1_index];
        if (!l2_table) return INVALID_FRAME;
        return l2_table[vpn & 0x3FF];
    }

    // Map VPN to physical frame. Dynamically creates the L2 table if not present.
    inline void map(uint32_t vpn, uint32_t frame_number) {
        const uint32_t l1_index = vpn >> 10;
        uint32_t*& l2_table = l1_directory[l1_index];
        if (!l2_table) {
            l2_table = new uint32_t[L2_SIZE];
            std::fill(l2_table, l2_table + L2_SIZE, INVALID_FRAME);
            ++l2_tables_created;
        }
        l2_table[vpn & 0x3FF] = frame_number;
    }

    // Unmap virtual page when evicted.
    inline void unmap(uint32_t vpn) {
        const uint32_t l1_index = vpn >> 10;
        uint32_t* l2_table = l1_directory[l1_index];
        if (l2_table) {
            l2_table[vpn & 0x3FF] = INVALID_FRAME;
        }
    }

    // Metric D: Allocated entries = 1024 L1 entries + (1024 * number of active L2 tables)
    uint64_t allocated_entries() const {
        return L1_SIZE + (l2_tables_created * L2_SIZE);
    }

    // Metric D: Each active L2 table was reached through exactly 1 active L1 entry
    uint64_t extra_accessed_entries() const {
        return l2_tables_created;
    }
};

// ============================================================================
// SECTION 3: PHYSICAL MEMORY & LRU FRAME REPLACEMENT (PARTS I & IV)
// ============================================================================

// ----------------------------------------------------------------------------
// PhysicalMemoryManager
//
// Free Frame Management Scheme:
// 1. Initial State: All physical frames are free.
// 2. Sequential "Bump" Allocator: Free frames are handed out in increasing
//    order (0, 1, 2, ...) as distinct virtual pages are touched for the first time.
//    Because frame numbers depend solely on access sequence, single-level and
//    two-level page tables produce the EXACT SAME frame sequence and hash!
// 3. LRU Replacement List (Part IV):
//    - Tracks frame recency with an O(1) doubly-linked list:
//        * head_mru: Most Recently Used frame
//        * tail_lru: Least Recently Used frame
//    - When physical memory is full, the tail frame (LRU) is evicted and
//      reused directly for the incoming page.
// ----------------------------------------------------------------------------
class PhysicalMemoryManager {
private:
    static constexpr uint32_t NULL_NODE = 0xFFFFFFFFu;

    uint32_t capacity;
    uint32_t used_frames_count;
    uint32_t head_mru;
    uint32_t tail_lru;

    std::vector<uint32_t> frame_to_vpn;  // Tracks which virtual page owns each frame
    std::vector<uint32_t> list_prev;     // Doubly-linked list previous pointers
    std::vector<uint32_t> list_next;     // Doubly-linked list next pointers

    // Unlink a frame from anywhere in the doubly-linked list
    inline void unlink_frame(uint32_t f) {
        if (list_prev[f] != NULL_NODE) {
            list_next[list_prev[f]] = list_next[f];
        } else {
            head_mru = list_next[f];
        }

        if (list_next[f] != NULL_NODE) {
            list_prev[list_next[f]] = list_prev[f];
        } else {
            tail_lru = list_prev[f];
        }
    }

    // Push a frame to the front of the list (marking it Most Recently Used)
    inline void push_mru_front(uint32_t f) {
        list_prev[f] = NULL_NODE;
        list_next[f] = head_mru;
        if (head_mru != NULL_NODE) {
            list_prev[head_mru] = f;
        } else {
            tail_lru = f;
        }
        head_mru = f;
    }

public:
    explicit PhysicalMemoryManager(uint32_t cap)
        : capacity(cap),
          used_frames_count(0),
          head_mru(NULL_NODE),
          tail_lru(NULL_NODE),
          frame_to_vpn(cap, INVALID_FRAME),
          list_prev(cap, NULL_NODE),
          list_next(cap, NULL_NODE) {}

    // Update recency: move frame to MRU position (called on every access)
    inline void touch_frame(uint32_t frame_number) {
        if (frame_number == head_mru) return;
        unlink_frame(frame_number);
        push_mru_front(frame_number);
    }

    // Allocate a frame for incoming VPN:
    // - If free space remains: hands out used_frames_count++, evicted_vpn = INVALID_FRAME
    // - If memory is full: evicts tail_lru, returns evicted_vpn for unmapping & disk penalty
    inline uint32_t allocate_frame(uint32_t vpn, uint32_t& evicted_vpn) {
        uint32_t allocated_frame;

        if (used_frames_count < capacity) {
            allocated_frame = used_frames_count++;
            evicted_vpn = INVALID_FRAME; // No page evicted (first touches are free)
        } else {
            allocated_frame = tail_lru;  // Pick least recently used frame
            evicted_vpn = frame_to_vpn[allocated_frame];
            unlink_frame(allocated_frame);
        }

        frame_to_vpn[allocated_frame] = vpn;
        push_mru_front(allocated_frame);
        return allocated_frame;
    }

    uint32_t get_used_frames() const { return used_frames_count; }
};

// ============================================================================
// SECTION 4: TRANSLATION LOOKASIDE BUFFER (TLB) (PART III)
// ============================================================================

// ----------------------------------------------------------------------------
// TranslationLookasideBuffer
// - 128 entries, fully associative, exact LRU policy, empty at start.
// - 1 cycle lookup cost.
// - In hardware, 128 entries are compared in parallel. In software simulation,
//   scanning 128 entries sequentially per access is slow. A 1 MB direct-index
//   table `vpn_to_slot[vpn]` provides O(1) lookup while the 128-entry doubly-
//   linked list preserves exact LRU replacement behavior.
// ----------------------------------------------------------------------------
class TranslationLookasideBuffer {
public:
    static constexpr int CAPACITY = 128;

private:
    static constexpr uint8_t EMPTY_SLOT = 255;

    uint32_t cached_vpn[CAPACITY];
    uint32_t cached_pfn[CAPACITY];
    uint8_t  prev_slot[CAPACITY];
    uint8_t  next_slot[CAPACITY];

    uint8_t head_mru = EMPTY_SLOT;
    uint8_t tail_lru = EMPTY_SLOT;

    uint8_t free_slots[CAPACITY];
    int     free_count;

    // Fast O(1) lookup map from VPN to TLB slot index (0..127 or EMPTY_SLOT)
    std::vector<uint8_t> vpn_to_slot;

    inline void unlink_slot(uint8_t slot) {
        if (prev_slot[slot] != EMPTY_SLOT) {
            next_slot[prev_slot[slot]] = next_slot[slot];
        } else {
            head_mru = next_slot[slot];
        }

        if (next_slot[slot] != EMPTY_SLOT) {
            prev_slot[next_slot[slot]] = prev_slot[slot];
        } else {
            tail_lru = prev_slot[slot];
        }
    }

    inline void push_mru_slot(uint8_t slot) {
        prev_slot[slot] = EMPTY_SLOT;
        next_slot[slot] = head_mru;
        if (head_mru != EMPTY_SLOT) {
            prev_slot[head_mru] = slot;
        } else {
            tail_lru = slot;
        }
        head_mru = slot;
    }

public:
    TranslationLookasideBuffer()
        : free_count(CAPACITY), vpn_to_slot(TOTAL_VIRTUAL_PAGES, EMPTY_SLOT) {
        for (int i = 0; i < CAPACITY; ++i) {
            free_slots[i] = static_cast<uint8_t>(CAPACITY - 1 - i);
            cached_vpn[i] = 0;
            cached_pfn[i] = INVALID_FRAME;
            prev_slot[i]  = EMPTY_SLOT;
            next_slot[i]  = EMPTY_SLOT;
        }
    }

    // Lookup VPN: returns PFN on hit (and moves entry to MRU); returns INVALID_FRAME on miss
    inline uint32_t lookup(uint32_t vpn) {
        uint8_t slot = vpn_to_slot[vpn];
        if (slot == EMPTY_SLOT) return INVALID_FRAME;

        // On hit: move entry to MRU
        if (slot != head_mru) {
            unlink_slot(slot);
            push_mru_slot(slot);
        }
        return cached_pfn[slot];
    }

    // Insert translation after a page table walk
    inline void insert(uint32_t vpn, uint32_t pfn) {
        uint8_t slot;
        if (free_count > 0) {
            slot = free_slots[--free_count];
        } else {
            // Evict LRU entry from tail
            slot = tail_lru;
            unlink_slot(slot);
            vpn_to_slot[cached_vpn[slot]] = EMPTY_SLOT; // Invalidate old mapping
        }

        cached_vpn[slot] = vpn;
        cached_pfn[slot] = pfn;
        vpn_to_slot[vpn] = slot;
        push_mru_slot(slot);
    }

    // Invalidate entry when page is evicted from physical memory in Part IV
    inline void invalidate(uint32_t vpn) {
        uint8_t slot = vpn_to_slot[vpn];
        if (slot == EMPTY_SLOT) return;

        vpn_to_slot[vpn] = EMPTY_SLOT;
        unlink_slot(slot);
        free_slots[free_count++] = slot;
    }
};

// ============================================================================
// SECTION 5: FAST TRACE PARSER AND BUFFERED I/O
// ============================================================================

// Parses one line: "<address>,<I/D>,<R/W>"
// Handles both decimal addresses (used by official benchmarks) and hexadecimal (with/without 0x)
static inline bool parse_trace_line(const char* start, const char* end,
                                    uint32_t& address, char& type, char& rw) {
    // Skip leading whitespace
    while (start < end && (*start == ' ' || *start == '\t')) ++start;
    if (start >= end || *start == '<') return false; // Skips header like <address>...

    // Determine if address is hexadecimal or decimal
    bool is_hex = false;
    if (end - start >= 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        is_hex = true;
        start += 2;
    } else {
        // If there are hex characters (a-f, A-F) before the comma, treat as hex
        const char* check = start;
        while (check < end && *check != ',' && *check != ' ' && *check != '\t' && *check != '\r') {
            char c = *check++;
            if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                is_hex = true;
                break;
            }
        }
    }

    uint64_t parsed_value = 0;
    bool has_digits = false;
    for (; start < end; ++start) {
        char c = *start;
        int digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (is_hex && c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (is_hex && c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            break;
        }

        if (is_hex) {
            parsed_value = (parsed_value << 4) | static_cast<uint64_t>(digit);
        } else {
            parsed_value = (parsed_value * 10) + static_cast<uint64_t>(digit);
        }
        has_digits = true;
    }
    if (!has_digits) return false;

    // Use lower 32 bits as required by assignment specification
    address = static_cast<uint32_t>(parsed_value);
    type = '?';
    rw = '?';

    // Parse I/D and R/W fields
    if (start < end && *start == ',') {
        ++start;
        while (start < end && *start == ' ') ++start;
        if (start < end) type = *start;

        while (start < end && *start != ',') ++start;
        if (start < end && *start == ',') {
            ++start;
            while (start < end && *start == ' ') ++start;
            if (start < end) rw = *start;
        }
    }
    return true;
}

// Reads file in large 4 MiB chunks to maximize simulator throughput (Part V)
template <typename LineProcessor>
static void read_trace_chunked(FILE* file, LineProcessor&& process_line) {
    constexpr size_t BUFFER_CHUNK_SIZE = 4 * 1024 * 1024; // 4 MiB buffer
    std::vector<char> buffer(BUFFER_CHUNK_SIZE * 2);
    char* base = buffer.data();
    size_t leftover_bytes = 0;

    while (true) {
        size_t bytes_read = fread(base + leftover_bytes, 1, BUFFER_CHUNK_SIZE, file);
        size_t total_bytes = leftover_bytes + bytes_read;
        size_t line_start = 0;

        while (true) {
            char* newline_ptr = static_cast<char*>(memchr(base + line_start, '\n', total_bytes - line_start));
            if (!newline_ptr) break;

            if (!process_line(base + line_start, newline_ptr)) return;
            line_start = static_cast<size_t>(newline_ptr - base) + 1;
        }

        if (bytes_read == 0) {
            if (line_start < total_bytes) {
                process_line(base + line_start, base + total_bytes);
            }
            return;
        }

        leftover_bytes = total_bytes - line_start;
        memmove(base, base + line_start, leftover_bytes);
    }
}

// ============================================================================
// SECTION 6: CORE SIMULATION ENGINE (TEMPLATED FOR MAXIMUM SPEED)
// ============================================================================

template <typename PageTableClass, bool UseTLB>
static bool execute_simulation(const SimulatorConfig& cfg, SimulationResults& res) {
    FILE* trace_file = fopen(cfg.trace_file_path.c_str(), "rb");
    if (!trace_file) {
        perror("Error opening trace file");
        return false;
    }

    // Determine physical memory capacity
    uint64_t capacity = TOTAL_VIRTUAL_PAGES;
    if (cfg.frame_fraction >= 0.0) {
        capacity = std::max<uint64_t>(1, static_cast<uint64_t>(cfg.frame_fraction * TOTAL_VIRTUAL_PAGES));
    } else if (cfg.frame_capacity > 0) {
        capacity = std::min<uint64_t>(cfg.frame_capacity, TOTAL_VIRTUAL_PAGES);
    }
    res.frame_capacity = capacity;

    PageTableClass              page_table;
    PhysicalMemoryManager       memory_manager(static_cast<uint32_t>(capacity));
    TranslationLookasideBuffer  tlb;

    res.page_access_counts.assign(TOTAL_VIRTUAL_PAGES, 0);
    uint64_t* access_counts = res.page_access_counts.data();

    uint64_t cycles               = 0;
    uint64_t hash                 = FNV_OFFSET_BASIS;
    uint64_t total_requests       = 0;
    uint64_t distinct_pages       = 0;
    uint64_t tlb_hits             = 0;
    uint64_t tlb_misses           = 0;
    uint64_t page_faults          = 0;
    uint64_t replacement_faults   = 0;
    uint64_t instr_count = 0, data_count = 0;
    uint64_t read_count = 0, write_count = 0;
    const uint64_t max_reqs = cfg.max_requests;

    auto start_time = std::chrono::steady_clock::now();

    read_trace_chunked(trace_file, [&](const char* start, const char* end) -> bool {
        uint32_t address;
        char type, rw;
        if (!parse_trace_line(start, end, address, type, rw)) return true;
        if (max_reqs && total_requests >= max_reqs) return false;

        ++total_requests;
        instr_count += (type == 'I');
        data_count  += (type == 'D');
        read_count  += (rw == 'R');
        write_count += (rw == 'W');

        const uint32_t vpn = address >> PAGE_OFFSET_BITS;
        if (access_counts[vpn]++ == 0) {
            ++distinct_pages; // First touch of this virtual page
        }

        uint32_t pfn = INVALID_FRAME;

        // --------------------------------------------------------------------
        // Step 1: Address Translation (TLB or Page Walk)
        // --------------------------------------------------------------------
        if constexpr (UseTLB) {
            cycles += TLB_ACCESS_CYCLES; // 1 cycle TLB lookup
            pfn = tlb.lookup(vpn);
        }

        if constexpr (UseTLB) {
            if (pfn != INVALID_FRAME) {
                // TLB Hit: update physical frame recency in O(1)
                ++tlb_hits;
                memory_manager.touch_frame(pfn);
            } else {
                // TLB Miss: page walk required
                ++tlb_misses;
                cycles += static_cast<uint64_t>(PageTableClass::PAGE_WALK_LEVELS) * MEMORY_ACCESS_CYCLES;
                pfn = page_table.lookup(vpn);

                if (pfn == INVALID_FRAME) {
                    // Page Fault: allot frame
                    ++page_faults;
                    uint32_t evicted_vpn;
                    pfn = memory_manager.allocate_frame(vpn, evicted_vpn);

                    if (evicted_vpn != INVALID_FRAME) {
                        // Replacement occurred: evict old page from PT and TLB
                        page_table.unmap(evicted_vpn);
                        tlb.invalidate(evicted_vpn);
                        cycles += DISK_ACCESS_CYCLES; // 20M cycle disk penalty
                        ++replacement_faults;
                    }
                    page_table.map(vpn, pfn);
                } else {
                    memory_manager.touch_frame(pfn);
                }
                // Install newly translated mapping into TLB
                tlb.insert(vpn, pfn);
            }
        } else {
            // No TLB: always execute page table walk
            cycles += static_cast<uint64_t>(PageTableClass::PAGE_WALK_LEVELS) * MEMORY_ACCESS_CYCLES;
            pfn = page_table.lookup(vpn);

            if (pfn == INVALID_FRAME) {
                // Page Fault: allot frame
                ++page_faults;
                uint32_t evicted_vpn;
                pfn = memory_manager.allocate_frame(vpn, evicted_vpn);

                if (evicted_vpn != INVALID_FRAME) {
                    page_table.unmap(evicted_vpn);
                    cycles += DISK_ACCESS_CYCLES;
                    ++replacement_faults;
                }
                page_table.map(vpn, pfn);
            } else {
                memory_manager.touch_frame(pfn);
            }
        }

        // --------------------------------------------------------------------
        // Step 2: The actual data or instruction memory access takes 200 cycles
        // --------------------------------------------------------------------
        cycles += MEMORY_ACCESS_CYCLES;

        // --------------------------------------------------------------------
        // Step 3: Metric E Hash Update: H = f(H, frame_number)
        // --------------------------------------------------------------------
        hash = (hash ^ static_cast<uint64_t>(pfn)) * FNV_PRIME;
        return true;
    });

    auto end_time = std::chrono::steady_clock::now();
    fclose(trace_file);

    // Populate results
    res.simulation_time_sec  = std::chrono::duration<double>(end_time - start_time).count();
    res.total_requests       = total_requests;
    res.instruction_requests = instr_count;
    res.data_requests        = data_count;
    res.read_requests        = read_count;
    res.write_requests       = write_count;
    res.distinct_pages       = distinct_pages;
    res.total_cycles         = cycles;
    res.frame_hash           = hash;
    res.tlb_hits             = tlb_hits;
    res.tlb_misses           = tlb_misses;
    res.page_faults          = page_faults;
    res.replacement_faults   = replacement_faults;
    res.frames_used          = memory_manager.get_used_frames();
    res.pt_allocated_entries = page_table.allocated_entries();
    res.pt_accessed_entries  = distinct_pages + page_table.extra_accessed_entries();

    return true;
}

// ============================================================================
// SECTION 7: HOT-PAGE DISTRIBUTION ANALYSIS (METRIC B)
// ============================================================================

static void analyze_hot_pages(const SimulationResults& res, const std::string& hist_file, bool quiet) {
    std::vector<uint64_t> active_counts;
    for (uint64_t cnt : res.page_access_counts) {
        if (cnt > 0) active_counts.push_back(cnt);
    }
    std::sort(active_counts.begin(), active_counts.end(), std::greater<uint64_t>());
    const size_t num_touched = active_counts.size();
    if (num_touched == 0) return;

    if (!quiet) {
        const double percentiles[] = {0.01, 0.05, 0.10, 0.20, 0.50};
        for (double pct : percentiles) {
            size_t count_limit = std::max<size_t>(1, static_cast<size_t>(pct * num_touched));
            uint64_t cumulative_accesses = 0;
            for (size_t i = 0; i < count_limit; ++i) cumulative_accesses += active_counts[i];
            printf("  hottest %4.0f%% of touched pages -> %6.2f%% of all accesses\n",
                   pct * 100.0,
                   (100.0 * static_cast<double>(cumulative_accesses)) / static_cast<double>(res.total_requests));
        }
    }

    if (!hist_file.empty()) {
        FILE* hf = fopen(hist_file.c_str(), "w");
        if (!hf) {
            perror("Error opening history file for writing");
            return;
        }
        fprintf(hf, "page_frac,access_frac\n");
        constexpr int NUM_POINTS = 1000;
        uint64_t running_sum = 0;
        size_t index = 0;
        for (int p = 1; p <= NUM_POINTS; ++p) {
            size_t target_index = static_cast<size_t>((static_cast<double>(p) * num_touched) / NUM_POINTS);
            if (target_index == 0) target_index = 1;
            for (; index < target_index; ++index) running_sum += active_counts[index];
            fprintf(hf, "%.6f,%.6f\n",
                    static_cast<double>(target_index) / num_touched,
                    static_cast<double>(running_sum) / res.total_requests);
        }
        fclose(hf);
    }
}

// ============================================================================
// SECTION 8: CLI PARSING AND REPORTING
// ============================================================================

static void print_help(const char* binary_name) {
    fprintf(stderr,
        "Usage: %s --trace <file> [options]\n"
        "Options:\n"
        "  --pt single|two           Page table organization (default: single)\n"
        "  --tlb                     Enable 128-entry fully-associative LRU TLB (Part III)\n"
        "  --frames <N>              Limit physical memory to N frames (Part IV)\n"
        "  --frames-frac <X>         Limit physical memory to fraction X of 2^20 (Part IV)\n"
        "  --max <N>                 Stop after N requests (useful for quick testing)\n"
        "  --hist <out.csv>          Export hot-page cumulative access CDF for plot_hot.py\n"
        "  --csv                     Print concise single-line CSV output for scripts\n",
        binary_name);
}

int main(int argc, char** argv) {
    SimulatorConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string flag = argv[i];
        auto read_value = [&](void) -> const char* {
            if (i + 1 >= argc) {
                print_help(argv[0]);
                exit(EXIT_FAILURE);
            }
            return argv[++i];
        };

        if      (flag == "--trace")       cfg.trace_file_path = read_value();
        else if (flag == "--pt")          cfg.page_table_type = read_value();
        else if (flag == "--tlb")         cfg.enable_tlb = true;
        else if (flag == "--frames")      cfg.frame_capacity = strtoull(read_value(), nullptr, 10);
        else if (flag == "--frames-frac") cfg.frame_fraction = atof(read_value());
        else if (flag == "--max")         cfg.max_requests = strtoull(read_value(), nullptr, 10);
        else if (flag == "--hist")        cfg.hist_output_file = read_value();
        else if (flag == "--csv")         cfg.csv_mode = true;
        else {
            print_help(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (cfg.trace_file_path.empty() || (cfg.page_table_type != "single" && cfg.page_table_type != "two")) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    SimulationResults results;
    bool status = false;

    if (cfg.page_table_type == "single") {
        status = cfg.enable_tlb ? execute_simulation<SingleLevelPageTable, true>(cfg, results)
                                : execute_simulation<SingleLevelPageTable, false>(cfg, results);
    } else {
        status = cfg.enable_tlb ? execute_simulation<TwoLevelPageTable, true>(cfg, results)
                                : execute_simulation<TwoLevelPageTable, false>(cfg, results);
    }

    if (!status) return EXIT_FAILURE;

    // Derived statistics
    const double pt_utilization = results.pt_allocated_entries ?
        static_cast<double>(results.pt_accessed_entries) / results.pt_allocated_entries : 0.0;
    const uint64_t tlb_total = results.tlb_hits + results.tlb_misses;
    const double tlb_hit_rate = tlb_total ?
        static_cast<double>(results.tlb_hits) / tlb_total : 0.0;
    const double page_fault_rate = results.total_requests ?
        static_cast<double>(results.page_faults) / results.total_requests : 0.0;

    // CSV Output Mode
    if (cfg.csv_mode) {
        printf("%s,%s,%d,%llu,%llu,%llu,%llu,%llu,%.6f,%016llx,%llu,%llu,%.6f,%llu,%llu,%.8f,%.3f\n",
               cfg.trace_file_path.c_str(), cfg.page_table_type.c_str(), static_cast<int>(cfg.enable_tlb),
               static_cast<unsigned long long>(results.frame_capacity),
               static_cast<unsigned long long>(results.total_requests),
               static_cast<unsigned long long>(results.distinct_pages),
               static_cast<unsigned long long>(results.frames_used),
               static_cast<unsigned long long>(results.total_cycles),
               pt_utilization,
               static_cast<unsigned long long>(results.frame_hash),
               static_cast<unsigned long long>(results.tlb_hits),
               static_cast<unsigned long long>(results.tlb_misses),
               tlb_hit_rate,
               static_cast<unsigned long long>(results.page_faults),
               static_cast<unsigned long long>(results.replacement_faults),
               page_fault_rate,
               results.simulation_time_sec);
        analyze_hot_pages(results, cfg.hist_output_file, true);
        return 0;
    }

    // Formatted Readable Report Mode
    printf("======================================================================\n");
    printf(" MEMORY MANAGEMENT SIMULATOR (LAB 4)\n");
    printf("======================================================================\n");
    printf("Trace File            : %s\n", cfg.trace_file_path.c_str());
    printf("Page Table Model      : %s-level\n", cfg.page_table_type == "single" ? "1" : "2");
    printf("TLB                   : %s\n", cfg.enable_tlb ? "Enabled (128 entries, LRU)" : "Disabled");
    printf("Memory Requests       : %llu (Instr: %llu, Data: %llu, Reads: %llu, Writes: %llu)\n",
           static_cast<unsigned long long>(results.total_requests),
           static_cast<unsigned long long>(results.instruction_requests),
           static_cast<unsigned long long>(results.data_requests),
           static_cast<unsigned long long>(results.read_requests),
           static_cast<unsigned long long>(results.write_requests));
    printf("Distinct Pages Touched: %llu\n", static_cast<unsigned long long>(results.distinct_pages));
    printf("Physical Frame Limit  : %llu\n", static_cast<unsigned long long>(results.frame_capacity));
    printf("----------------------------------------------------------------------\n");
    printf("[A] Number of Frames Used  : %llu\n", static_cast<unsigned long long>(results.frames_used));
    printf("[C] Total Execution Cycles : %llu\n", static_cast<unsigned long long>(results.total_cycles));
    printf("[D] Page Table Utilization : %.6f (%llu accessed / %llu allocated)\n",
           pt_utilization,
           static_cast<unsigned long long>(results.pt_accessed_entries),
           static_cast<unsigned long long>(results.pt_allocated_entries));
    printf("[E] Frame Hash (H)         : %016llx\n", static_cast<unsigned long long>(results.frame_hash));
    if (cfg.enable_tlb) {
        printf("[Part III] TLB Hit Rate    : %.4f%% (%llu hits, %llu misses)\n",
               tlb_hit_rate * 100.0,
               static_cast<unsigned long long>(results.tlb_hits),
               static_cast<unsigned long long>(results.tlb_misses));
    }
    printf("[Part IV]  Page Faults     : %llu (Rate: %.6f%%) | Replacements: %llu\n",
           static_cast<unsigned long long>(results.page_faults),
           page_fault_rate * 100.0,
           static_cast<unsigned long long>(results.replacement_faults));
    printf("[Part V]   Simulation Time : %.4f seconds\n", results.simulation_time_sec);
    printf("----------------------------------------------------------------------\n");
    printf("[B] Hot-Page Analysis:\n");
    analyze_hot_pages(results, cfg.hist_output_file, false);
    printf("======================================================================\n");

    return 0;
}
