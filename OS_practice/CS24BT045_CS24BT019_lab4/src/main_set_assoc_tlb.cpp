// Memory management simulator for Lab 4 (4-way Set-Associative FIFO TLB variant of main_fifo.cpp).
// Everything is identical to src/main_fifo.cpp except TranslationLookasideBuffer is
// 4-way set-associative (32 sets x 4 ways = 128 entries) instead of fully-associative.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue> // [FIFO CHANGE] Added for std::queue<uint32_t>
#include <string>
#include <vector>

// System parameters that define the virtual address space and the simulator cost model.
constexpr uint32_t PAGE_OFFSET_BITS     = 12;
constexpr uint32_t PAGE_SIZE_BYTES      = 1u << PAGE_OFFSET_BITS;
constexpr uint32_t VPN_BITS             = 32 - PAGE_OFFSET_BITS;
constexpr uint32_t TOTAL_VIRTUAL_PAGES  = 1u << VPN_BITS;
constexpr uint32_t INVALID_FRAME        = 0xFFFFFFFFu;

// [SET-ASSOC TLB CHANGE] 128 total entries organized as 32 sets x 4 ways.
// The 20-bit VPN is split into:
//   - lower 5 bits: set index  (vpn & (TLB_SETS - 1)) -> selects 1 of 32 sets
//   - upper 15 bits: tag       (vpn >> TLB_SET_BITS)  -> matched against the 4 ways in that set
constexpr uint32_t TLB_SET_BITS         = 5;
constexpr uint32_t TLB_SETS             = 1u << TLB_SET_BITS; // 32 sets
constexpr uint32_t TLB_WAYS             = 4;                  // 4-way associative (32 * 4 = 128 entries)

constexpr uint64_t TLB_ACCESS_CYCLES    = 1;
constexpr uint64_t MEMORY_ACCESS_CYCLES = 200;
constexpr uint64_t DISK_ACCESS_CYCLES   = 20000000ULL;

constexpr uint64_t FNV_PRIME            = 1099511628211ULL;
constexpr uint64_t FNV_OFFSET_BASIS     = 1469598103934665603ULL;

// Command-line options provided to the simulator.
struct SimulatorConfig {
    std::string trace_file_path;
    std::string page_table_type   = "single"; // "single" or "two"
    bool        enable_tlb        = false;    // Enables 128-entry 4-way set-associative TLB (Part III)
    uint64_t    frame_capacity    = 0;        // Max physical frames (0 = unlimited for Parts I-III)
    double      frame_fraction    = -1.0;     // Fraction of 2^20 virtual pages (e.g. 0.5, 0.25)
    uint64_t    max_requests      = 0;        // Optional limit for debugging large traces
    std::string hist_output_file;             // Path to export hot-page CDF CSV
    bool        csv_mode          = false;    // Concise single-line output for batch script
};

// Aggregated execution metrics collected during a run.
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

// [FIFO CHANGE] Replaced LRU doubly-linked list fields with a simple FIFO queue
// storing physical frame numbers in the order they were loaded.
struct PhysicalMemoryManager {
    uint32_t capacity;
    uint32_t used_frames_count;
    std::vector<uint32_t> frame_to_vpn;
    std::queue<uint32_t> fifo_queue;
};

// [SET-ASSOC TLB CHANGE] Replaced the single 128-entry fully-associative array + global linked list
// (cached_vpn[128], prev_slot[128], next_slot[128], head_newest, tail_oldest, free_slots, vpn_to_slot)
// with 32 independent sets, each holding 4 ways. Each way stores valid, tag, pfn, and an
// insertion sequence number so FIFO eviction per set stays exact even after invalidate_tlb().
struct TLBSet {
    bool     valid[TLB_WAYS];
    uint32_t tag[TLB_WAYS];        // Upper (20 - TLB_SET_BITS) = 15 bits of VPN
    uint32_t pfn[TLB_WAYS];        // Cached physical frame number
    uint64_t insert_seq[TLB_WAYS]; // Monotonic insertion timestamp for per-set FIFO victim choice
};

struct TranslationLookasideBuffer {
    TLBSet   sets[TLB_SETS];
    uint64_t seq_counter;
};

void initialize_single_page_table(SingleLevelPageTable& table) {
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

// [FIFO CHANGE] Initialize frame_to_vpn and empty fifo_queue instead of LRU list arrays.
void initialize_memory_manager(PhysicalMemoryManager& memory, uint32_t capacity) {
    memory.capacity = capacity;
    memory.used_frames_count = 0;
    memory.frame_to_vpn.assign(capacity, INVALID_FRAME);
    while (!memory.fifo_queue.empty()) memory.fifo_queue.pop();
}

// [FIFO CHANGE] When memory is full, evict fifo_queue.front() (oldest loaded frame) instead
// of tail_lru, then push the newly loaded frame to the back of fifo_queue.
uint32_t allocate_frame(PhysicalMemoryManager& memory, uint32_t vpn,
                        uint32_t& evicted_vpn) {
    uint32_t frame;
    if (memory.used_frames_count < memory.capacity) {
        frame = memory.used_frames_count++;
        evicted_vpn = INVALID_FRAME;
    } else {
        frame = memory.fifo_queue.front();
        memory.fifo_queue.pop();
        evicted_vpn = memory.frame_to_vpn[frame];
    }
    memory.frame_to_vpn[frame] = vpn;
    memory.fifo_queue.push(frame);
    return frame;
}

// [SET-ASSOC TLB CHANGE] Clear all 32 sets x 4 ways and reset sequence counter.
void initialize_tlb(TranslationLookasideBuffer& tlb) {
    tlb.seq_counter = 0;
    for (uint32_t s = 0; s < TLB_SETS; ++s) {
        for (uint32_t w = 0; w < TLB_WAYS; ++w) {
            tlb.sets[s].valid[w] = false;
            tlb.sets[s].tag[w] = 0;
            tlb.sets[s].pfn[w] = INVALID_FRAME;
            tlb.sets[s].insert_seq[w] = 0;
        }
    }
}

// [SET-ASSOC TLB CHANGE] Replaced unlink_tlb_slot() and push_tlb_slot_newest().
// Set-associative lookup indexes directly into set = (vpn & 0x1F) and checks its 4 ways.
// FIFO policy: a hit returns pfn without modifying insert_seq.
uint32_t lookup_tlb(TranslationLookasideBuffer& tlb, uint32_t vpn) {
    const uint32_t set_idx = vpn & (TLB_SETS - 1);
    const uint32_t tag     = vpn >> TLB_SET_BITS;
    const TLBSet& set      = tlb.sets[set_idx];

    for (uint32_t w = 0; w < TLB_WAYS; ++w) {
        if (set.valid[w] && set.tag[w] == tag) {
            return set.pfn[w];
        }
    }
    return INVALID_FRAME;
}

// [SET-ASSOC TLB CHANGE] Insert into set = (vpn & 0x1F):
// 1. Prefer any empty way (!valid[w]) in that set.
// 2. If all 4 ways in that set are occupied, evict the way with the smallest insert_seq (FIFO).
void insert_tlb(TranslationLookasideBuffer& tlb, uint32_t vpn, uint32_t pfn) {
    const uint32_t set_idx = vpn & (TLB_SETS - 1);
    const uint32_t tag     = vpn >> TLB_SET_BITS;
    TLBSet& set            = tlb.sets[set_idx];

    uint32_t victim_way = 0;
    bool found_empty = false;
    for (uint32_t w = 0; w < TLB_WAYS; ++w) {
        if (!set.valid[w]) {
            victim_way = w;
            found_empty = true;
            break;
        }
    }

    if (!found_empty) {
        uint64_t oldest_seq = set.insert_seq[0];
        victim_way = 0;
        for (uint32_t w = 1; w < TLB_WAYS; ++w) {
            if (set.insert_seq[w] < oldest_seq) {
                oldest_seq = set.insert_seq[w];
                victim_way = w;
            }
        }
    }

    set.valid[victim_way]      = true;
    set.tag[victim_way]        = tag;
    set.pfn[victim_way]        = pfn;
    set.insert_seq[victim_way] = ++tlb.seq_counter;
}

// [SET-ASSOC TLB CHANGE] Invalidate an evicted VPN by searching only its mapped set.
void invalidate_tlb(TranslationLookasideBuffer& tlb, uint32_t vpn) {
    const uint32_t set_idx = vpn & (TLB_SETS - 1);
    const uint32_t tag     = vpn >> TLB_SET_BITS;
    TLBSet& set            = tlb.sets[set_idx];

    for (uint32_t w = 0; w < TLB_WAYS; ++w) {
        if (set.valid[w] && set.tag[w] == tag) {
            set.valid[w] = false;
            return;
        }
    }
}

// Parses a single trace record in the form "address,I/R" or "address,D/W".
bool parse_trace_line(const char* start, const char* end,
                      uint32_t& address, char& type, char& rw) {
    while (start < end && (*start == ' ' || *start == '\t')) ++start;
    if (start >= end || *start == '<') return false;

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

// Executes the simulation using either the single-level or two-level page table.
bool execute_simulation(const SimulatorConfig& cfg, SimulationResults& res) {
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

    SingleLevelPageTable single_page_table;
    TwoLevelPageTable two_page_table;
    PhysicalMemoryManager memory_manager;
    TranslationLookasideBuffer tlb;
    initialize_single_page_table(single_page_table);
    initialize_two_level_page_table(two_page_table);
    initialize_memory_manager(memory_manager, static_cast<uint32_t>(capacity));
    initialize_tlb(tlb);

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

    char line[256];
    while (fgets(line, sizeof(line), trace_file)) {
        const char* start = line;
        const char* end = line + strlen(line);
        uint32_t address;
        char type, rw;
        if (!parse_trace_line(start, end, address, type, rw)) continue;
        if (max_reqs && total_requests >= max_reqs) break;

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

        // Address translation: either a TLB hit or a full page-table walk.
        if (cfg.enable_tlb) {
            cycles += TLB_ACCESS_CYCLES;
            pfn = lookup_tlb(tlb, vpn);
        }

        if (cfg.enable_tlb) {
            if (pfn != INVALID_FRAME) {
                ++tlb_hits;
            } else {
                ++tlb_misses;
                cycles += static_cast<uint64_t>(cfg.page_table_type == "single" ? 1 : 2) *
                          MEMORY_ACCESS_CYCLES;
                if (cfg.page_table_type == "single") {
                    pfn = lookup_single_page(single_page_table, vpn);
                } else {
                    pfn = lookup_two_level_page(two_page_table, vpn);
                }

                if (pfn == INVALID_FRAME) {
                    ++page_faults;
                    uint32_t evicted_vpn;
                    pfn = allocate_frame(memory_manager, vpn, evicted_vpn);

                    if (evicted_vpn != INVALID_FRAME) {
                        if (cfg.page_table_type == "single") {
                            unmap_single_page(single_page_table, evicted_vpn);
                        } else {
                            unmap_two_level_page(two_page_table, evicted_vpn);
                        }
                        invalidate_tlb(tlb, evicted_vpn);
                        cycles += DISK_ACCESS_CYCLES;
                        ++replacement_faults;
                    }
                    if (cfg.page_table_type == "single") {
                        map_single_page(single_page_table, vpn, pfn);
                    } else {
                        map_two_level_page(two_page_table, vpn, pfn);
                    }
                }
                insert_tlb(tlb, vpn, pfn);
            }
        } else {
            cycles += static_cast<uint64_t>(cfg.page_table_type == "single" ? 1 : 2) *
                      MEMORY_ACCESS_CYCLES;
            if (cfg.page_table_type == "single") {
                pfn = lookup_single_page(single_page_table, vpn);
            } else {
                pfn = lookup_two_level_page(two_page_table, vpn);
            }

            if (pfn == INVALID_FRAME) {
                ++page_faults;
                uint32_t evicted_vpn;
                pfn = allocate_frame(memory_manager, vpn, evicted_vpn);

                if (evicted_vpn != INVALID_FRAME) {
                    if (cfg.page_table_type == "single") {
                        unmap_single_page(single_page_table, evicted_vpn);
                    } else {
                        unmap_two_level_page(two_page_table, evicted_vpn);
                    }
                    cycles += DISK_ACCESS_CYCLES;
                    ++replacement_faults;
                }
                if (cfg.page_table_type == "single") {
                    map_single_page(single_page_table, vpn, pfn);
                } else {
                    map_two_level_page(two_page_table, vpn, pfn);
                }
            }
        }

        // Every memory request also incurs the normal memory access latency.
        cycles += MEMORY_ACCESS_CYCLES;

        // Update the frame-hash metric used to compare page-table implementations.
        hash = (hash ^ static_cast<uint64_t>(pfn)) * FNV_PRIME;
    }

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
    res.frames_used = memory_manager.used_frames_count;
    if (cfg.page_table_type == "single") {
        res.pt_allocated_entries = TOTAL_VIRTUAL_PAGES;
        res.pt_accessed_entries  = distinct_pages;
    } else {
        res.pt_allocated_entries = 1024 + (two_page_table.l2_tables_created * 1024);
        res.pt_accessed_entries  = distinct_pages + two_page_table.l2_tables_created;
    }

    destroy_two_level_page_table(two_page_table);
    return true;
}

// Summarizes how concentrated the memory accesses are across the touched pages.
void analyze_hot_pages(const SimulationResults& res, const std::string& hist_file, bool quiet) {
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

// Prints the command-line usage information for the simulator.
void print_help(const char* binary_name) {
    fprintf(stderr,
        "Usage: %s --trace <file> [options]\n"
        "Options:\n"
        "  --pt single|two           Page table organization (default: single)\n"
        "  --tlb                     Enable 128-entry 4-way set-associative FIFO TLB (Part III)\n" // [SET-ASSOC TLB CHANGE]
        "  --frames <N>              Limit physical memory to N frames (Part IV)\n"
        "  --frames-frac <X>         Limit physical memory to fraction X of 2^20 (Part IV)\n"
        "  --max <N>                 Stop after N requests (useful for quick testing)\n"
        "  --hist <out.csv>          Export hot-page cumulative access CDF for plot_hot.py\n"
        "  --csv                     Print concise single-line CSV output for scripts\n",
        binary_name);
}

// Returns the value following a command-line option.
const char* next_argument(int argc, char** argv, int& index) {
    if (index + 1 >= argc) {
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    }
    ++index;
    return argv[index];
}

int main(int argc, char** argv) {
    SimulatorConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string flag = argv[i];

        if      (flag == "--trace")       cfg.trace_file_path = next_argument(argc, argv, i);
        else if (flag == "--pt")          cfg.page_table_type = next_argument(argc, argv, i);
        else if (flag == "--tlb")         cfg.enable_tlb = true;
        else if (flag == "--frames")      cfg.frame_capacity = strtoull(next_argument(argc, argv, i), nullptr, 10);
        else if (flag == "--frames-frac") cfg.frame_fraction = atof(next_argument(argc, argv, i));
        else if (flag == "--max")         cfg.max_requests = strtoull(next_argument(argc, argv, i), nullptr, 10);
        else if (flag == "--hist")        cfg.hist_output_file = next_argument(argc, argv, i);
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

    status = execute_simulation(cfg, results);

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
    printf("TLB                   : %s\n", cfg.enable_tlb ? "Enabled (128 entries, 4-way set-assoc, FIFO)" : "Disabled"); // [SET-ASSOC TLB CHANGE]
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

