# Memory Virtualization Simulator (Lab 4)

A single C++17 program, `memsim`, that covers all five parts of the assignment:
single-level and two-level page tables, a 128-entry TLB, bounded physical
memory with LRU replacement, and a speed-optimized simulation loop.

```
memsim.cpp    simulator (all logic)
Makefile      `make` builds ./memsim
run_all.sh    runs every experiment for Parts I-IV on every trace in a folder -> results.csv
plot_hot.py   plots the hot-page curve (Part I-B) from the --hist output
```

## 1. Build and run

```bash
make
./memsim --trace trace.txt --pt single                  # Part I
./memsim --trace trace.txt --pt two                     # Part II
./memsim --trace trace.txt --pt two --tlb               # Part III
./memsim --trace trace.txt --pt single --tlb --frames 4096        # Part IV (absolute)
./memsim --trace trace.txt --pt single --tlb --frames-frac 0.25   # Part IV (fraction of 2^20 pages)
./run_all.sh traces/ results.csv                        # everything, as CSV
python3 plot_hot.py hist/*.single.csv                   # hot-page plot
```

| Flag | Meaning |
|---|---|
| `--trace F` | trace file (`<hex address>,<I/D>,<R/W>` per line) |
| `--pt single\|two` | page table type |
| `--tlb` | enable the TLB |
| `--frames N` / `--frames-frac X` | physical memory size (default: unlimited, i.e. all 2^20 frames) |
| `--tlb-policy lru\|plru` | exact LRU (default) or optional pseudo-LRU TLB |
| `--plru-period K` | accesses between counter decays for pseudo-LRU (default 128) |
| `--max N` | stop after N requests (handy while developing on large files) |
| `--hist out.csv` | write the hot-page cumulative curve |
| `--csv` | print one CSV row instead of the readable report |

**Assumptions about the trace:** addresses are hexadecimal (with or without `0x`)
and only the lower 32 bits are used. If your traces use decimal, change the digit
loop in `parseLine`. The I/D and R/W fields are only counted, never used for
behaviour.

## 2. Address handling

4 KB pages -> 12 offset bits, 20 VPN bits, 2^20 virtual pages.

```
vpn    = addr >> 12
offset = addr & 4095        (not needed by the simulator, only the VPN matters)
```

## 3. Page tables (Parts I and II)

Both classes implement `lookup / map / unmap / allocatedEntries`, and the
simulation loop is a C++ template over the type, so there is no virtual-call cost.

**Single level** is a flat `uint32_t[2^20]` indexed by VPN (4 MiB). `INVALID`
(0xFFFFFFFF) marks "not mapped".

**Two level** splits the VPN into a 10-bit L1 index (`vpn >> 10`) and a 10-bit L2
index (`vpn & 1023`). The L1 array has 1024 pointers. An L2 table (1024 entries)
is allocated the first time any page in its 4 MB region is mapped. L2 tables are
never freed, even when all their pages are evicted.

### Frame management scheme (document this in your report)
A **bump allocator**: frames are handed out as 0, 1, 2, ... in order of first
touch. Because a page fault in Parts I-III always finds a free frame, nothing
else is needed. When physical memory is bounded (Part IV) and full, the LRU
frame is **reused directly** for the incoming page, so no free list is required.
Allocation ("which frame?") and replacement ("who gets evicted?") are separate
steps inside `PhysMem::allocate`.

Both page-table types use the identical allocator, so they produce the **same
frame sequence and the same hash**. This is the expected answer to the Part II
question, and the program confirms it on a test trace.

### Metrics
* **A. Frames used**: `PhysMem::used` (bump counter, capped by capacity).
* **B. Hot pages**: per-VPN access counts, sorted descending. The report prints
  the share of accesses captured by the hottest 1/5/10/20/50% of touched pages.
  `--hist` writes a 1000-point cumulative curve; `plot_hot.py` draws it against
  the "uniform access" diagonal. A curve far above the diagonal supports the
  hypothesis.
* **C. Cycles**: see the cost model below.
* **D. Page-table utilization** = accessed entries / allocated entries.
  Single level: distinct pages touched / 2^20. Two level: (distinct pages touched
  + number of L1 entries touched) / (1024 + 1024 x number of L2 tables). Expect
  two-level to be much higher because only needed L2 tables exist.
* **E. Hash**: `H = (H XOR frame_number) * 1099511628211` (64-bit FNV-style),
  updated on every access, starting from the FNV offset basis. It depends on the
  entire frame sequence and its order.

## 4. Cycle-cost model (state this explicitly in your report)

The assignment does not spell out every cost, so this is the model used:

| Event | Cycles |
|---|---|
| TLB lookup (only if TLB enabled; every access) | 1 |
| Page walk, single level (no TLB, or TLB miss) | 200 (one PTE read) |
| Page walk, two level (no TLB, or TLB miss) | 400 (L1 read + L2 read) |
| The data/instruction access itself | 200 |
| Eviction of a resident page (replacement) | 20,000,000 |

So with no TLB, each request costs 400 (single) or 600 (two-level) cycles. With
a TLB hit it costs 201. A first-time page fault while free frames remain has no
disk cost ("first set of pages comes for free"). Only a fault that forces an
eviction pays the 20M cycles. Check these choices with your TA; they are easy to
change in the loop in `run()`.

## 5. TLB (Part III)

128 entries, fully associative, exact LRU, empty at start, 1 cycle per access.

* Hit: return the PFN, move the entry to the MRU position, skip the page walk.
* Miss: do the page walk (and page-fault handling if needed), then insert. If the
  TLB is full the LRU entry is evicted first.
* Invalidation: when physical memory evicts a page, its TLB entry is removed, so
  the TLB can never hold a stale mapping.

**Simulator speed trick:** the TLB uses a doubly linked list over the 128 slots
for LRU order, plus a table `slot[vpn]` giving the entry holding that VPN. A
"fully associative search" is therefore O(1) instead of 128 comparisons. This
changes only how fast the simulator runs, not the simulated behaviour or cost.

### Optional pseudo-LRU (from your slide)
`--tlb-policy plru` uses the lecture's scheme: a 3-bit counter per entry,
incremented (saturating at 7) on access, all counters decremented every
`--plru-period` accesses, new entries set to 7, victim = smallest counter. This
approximates LRU and is provided for comparison only. **The assignment requires
exact LRU, so use the default for your reported results**, and mention pseudo-LRU
as an extra experiment (on the test trace it gave 86.47% vs 86.82% hit rate).

## 6. Bounded physical memory and LRU (Part IV)

`PhysMem` keeps, per frame, the resident VPN plus a doubly linked list ordered by
recency (head = MRU, tail = LRU), so touch and evict are both O(1).

On each request:
1. TLB hit -> frame known; mark that frame most recently used.
2. TLB miss -> page walk. Mapped: mark MRU. Not mapped: **page fault**.
3. Page fault: `allocate()` takes the next unused frame, or if full the LRU
   frame. For an eviction the old VPN is unmapped in the page table, invalidated
   in the TLB, and 20M cycles are charged. The new mapping is installed.

LRU recency is updated on **every** request, including TLB hits. This models
true LRU over all accesses (hardware could not do this cheaply, but the simulator
can, and it matches the assignment's "LRU replacement policy").

Reported: **page fault rate** = page faults / total requests (this includes the
free first-touch faults), and **replacement faults** separately (those that cost
disk time). Total cycles include the 20M per replacement.

Note that 0.5 x 2^20 frames is 524,288, which may exceed what a benchmark ever
touches (then nothing is evicted). `run_all.sh` therefore also runs absolute small
sizes (4096, 1024, 256, 64). Choose sizes that straddle each benchmark's number of
distinct pages so the fault-rate curve actually bends. Plot fault rate and total
cycles against memory size, and explain the knee using the working set.

## 7. Speed design (Part V)

* Chunked `fread` of 4 MiB plus a hand-written hex parser (no iostreams, no
  `getline`, no string allocation per line).
* Page table, TLB lookup and LRU are all O(1) with flat arrays and no hashing.
* Page-table type and TLB on/off are template parameters, so the compiled loop
  contains only the code needed.
* No output or allocation inside the per-request loop. Counters live in locals.
* Compile with `-O3 -march=native`.
* `sim_seconds` measures only the loop (file reading + simulation), not
  start-up allocation or report printing. Use that for Part V.

## 8. Suggested report structure

1. Page and frame allocation scheme, cycle model, hash function (sections 3-4).
2. Part I and II table: frames used, cycles, PT utilization, hash for each
   benchmark. State that hashes match (same allocator -> same frame sequence).
3. Hot-page plot per benchmark (`plot_hot.py`) and your conclusion.
4. Part III: TLB hit rate and cycles for both page tables, with the speedup over
   no TLB. Two-level costs more per walk, so the TLB narrows the gap.
5. Part IV: fault rate and cycles versus memory size; discuss working set.
6. Part V: simulator runtimes and the optimizations in section 7.
