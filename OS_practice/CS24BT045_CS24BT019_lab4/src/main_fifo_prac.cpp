#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue> // [FIFO CHANGE] Added for std::queue<uint32_t>
#include <string>
#include <vector>

constexpr uint32_t PAGE_OFFSET_BITS     = 12;
constexpr uint32_t PAGE_SIZE_BYTES      = 1u << PAGE_OFFSET_BITS;
constexpr uint32_t VPN_BITS             = 32 - PAGE_OFFSET_BITS;
constexpr uint32_t TOTAL_VIRTUAL_PAGES  = 1u << VPN_BITS;
constexpr uint32_t INVALID_FRAME        = 0xFFFFFFFFu;

constexpr uint64_t TLB_ACCESS_CYCLES    = 1;
constexpr uint64_t MEMORY_ACCESS_CYCLES = 200;
constexpr uint64_t DISK_ACCESS_CYCLES   = 20000000ULL;

constexpr uint64_t FNV_PRIME            = 1099511628211ULL;
constexpr uint64_t FNV_OFFSET_BASIS     = 1469598103934665603ULL;

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

// Plain data structures used by the page tables, physical memory, and TLB.
struct SingleLevelPageTable {
    std::vector<uint32_t> entries;
};

struct TwoLevelPageTable {
    std::vector<uint32_t*> l1_directory;
    uint64_t l2_tables_created;
};

struct PhysicalMemoryManager {
    uint32_t capacity;
    uint32_t used_frame_count;
    std::vector<uint32_t> frame_to_vpn;
    std::queue<uint32_t> fifo_queue;
}

struct TranslationLookasideBuffer {
    uint32_t cached_vpn[128];
    uint32_t cached_pfn[128];
    uint8_t prev_slot[128];
    uint8_t next_slot[128];
    uint8_t head_mru;
    uint8_t tail_lru;
    uint8_t free_slots[128];
    int free_count;
    std::vector<uint8_t> vpn_to_slot;
};

void initialize_single_page_table(SingleLevelPageTable &table)
{
    table.entries.assign(TOTAL_VIRTUAL_PAGES, INVALID_FRAME);
}

uint32_t lookup_single_page(SingleLevelPageTable& table, uint32_t vpn) {
    return table.entries[vpn];
}

void map_single_page(SingleLevelPageTable& table, uint32_t vpn, uint32_t frame) {
    table.entries[vpn] = frame;
}

void unmap_single_page(SingleLevelPageTable& table, uint32_t vpn) {
    table.entries[vpn] = INVALID_FRAME;
}

void initialize_two_level_page_table(TwoLevelPageTable& table) {
    table.l1_directory.assign(1024, nullptr);
    table.l2_tables_created = 0;
}

void destroy_two_level_page_table(TwoLevelPageTable& table) {
    for (uint32_t* l2_table : table.l1_directory) {
        delete[] l2_table;
    }
}

uint32_t lookup_two_level_page(TwoLevelPageTable& table, uint32_t vpn) {
    uint32_t* l2_table = table.l1_directory[vpn >> 10];
    if (!l2_table) return INVALID_FRAME;
    return l2_table[vpn & 0x3FF];
}

void map_two_level_page(TwoLevelPageTable& table, uint32_t vpn, uint32_t frame) {
    uint32_t*& l2_table = table.l1_directory[vpn >> 10];
    if (!l2_table) {
        l2_table = new uint32_t[1024];
        std::fill(l2_table, l2_table + 1024, INVALID_FRAME);
        ++table.l2_tables_created;
    }
    l2_table[vpn & 0x3FF] = frame;
}

void unmap_two_level_page(TwoLevelPageTable& table, uint32_t vpn) {
    uint32_t* l2_table = table.l1_directory[vpn >> 10];
    if (l2_table) l2_table[vpn & 0x3FF] = INVALID_FRAME;
}

void initialize_memory_manager(PhysicalMemoryManager& memory, uint32_t capacity)
{
    memory.capacity = capacity;
    memory.used_frames_count = 0 ;
    memroy.frame_to_vpn.assign(capacity, INVALID_FRAME);
    while(!memory.fifo_queue.empty()) memory.fifo_queue.pop();
}

uint32_t allocate_frame(PhysicalMemoryManager& memory, uint32_t vpn, uint32_t& evicted_vpn) {
    uint32_t frame;
    if (memory.used_frames_count < memory.capacity)
    {
        frame = memory.used_frames_count++;
        evicted_vpn = INVALID_FRAME++;
    }
    else
    {
        frame = memory.fifo_queue.front();
        memory.fifo_queue.pop();
        evicted_vpn = memory.frame_to_vpn[frame];
    }
    memory.frame_to_vpn[frame] = vpn;
    memory.fifo_queue.push(frame);
    return frame;
}

void initialize_tlb()