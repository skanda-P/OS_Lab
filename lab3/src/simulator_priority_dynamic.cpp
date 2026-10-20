#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cctype>
#include <iomanip>

using namespace std;

// Process structure (identical to lab structure)
struct Process {
    int pid;
    int arrival_time;
    vector<int> bursts;       // Even indices are CPU bursts, odd indices are I/O bursts
    int curr_b_idx;
    int remaining_b_time;
    int completion_time;
    int queue_level;          // Base User Priority: 0 (highest) to 2 (lowest)
    double dynamic_priority;  // Dynamic priority that decays with CPU usage and recovers with aging/IO
};

// ============================================================================
// INDEPENDENT PRIORITY ADJUSTMENT FUNCTIONS
// ============================================================================

// 1. Decay function: Running process consumes CPU -> priority decays
void apply_priority_decay(Process* current_running, double decay_rate) {
    if (current_running != nullptr) {
        current_running->dynamic_priority += decay_rate;
    }
}

// 2. Boost on I/O return: Rewards interactive tasks by resetting to base priority
void boost_process_to_base(Process* p) {
    if (p != nullptr) {
        p->dynamic_priority = (double)p->queue_level;
    }
}

// 3. Aging boost: Waiting processes in ready pool recover priority each tick
void apply_waiting_aging(vector<Process*> &pool, double aging_rate) {
    for (Process* p : pool) {
        if (p->dynamic_priority > (double)p->queue_level) {
            p->dynamic_priority = max((double)p->queue_level, p->dynamic_priority - aging_rate);
        }
    }
}

// Helper: Scan vector to find process with highest dynamic priority
int get_highest_priority_index(const vector<Process*> &pool) {
    if (pool.empty()) return -1;
    int best_idx = 0;
    for (size_t i = 1; i < pool.size(); i++) {
        if (pool[i]->dynamic_priority < pool[best_idx]->dynamic_priority) {
            best_idx = (int)i;
        } else if (pool[i]->dynamic_priority == pool[best_idx]->dynamic_priority) {
            if (pool[i]->arrival_time < pool[best_idx]->arrival_time) {
                best_idx = (int)i;
            } else if (pool[i]->arrival_time == pool[best_idx]->arrival_time &&
                       pool[i]->pid < pool[best_idx]->pid) {
                best_idx = (int)i;
            }
        }
    }
    return best_idx;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <decay_rate> <workload_file>\n";
        return 1;
    }

    double decay_rate = stod(argv[1]);
    string filepath = argv[2];

    ifstream file(filepath);
    if (!file.is_open()) {
        cerr << "Error: Could not open workload file: " << filepath << "\n";
        return 1;
    }

    // Parse processes from workload description file
    vector<Process> processes;
    int pid_counter = 1;
    string line;

    while (getline(file, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        int arrival;
        if (!(ss >> arrival)) continue;

        Process p;
        p.pid = pid_counter++;
        p.arrival_time = arrival;
        p.curr_b_idx = 0;
        p.completion_time = 0;
        p.queue_level = (p.pid % 3); // Base priority: 0, 1, or 2
        p.dynamic_priority = (double)p.queue_level;

        int burst_val;
        while (ss >> burst_val && burst_val != -1) {
            p.bursts.push_back(burst_val);
        }

        if (!p.bursts.empty()) {
            p.remaining_b_time = p.bursts[0];
            processes.push_back(p);
        }
    }

    int curr_time = 0;
    int completed_processes = 0;
    int total_processes = (int)processes.size();

    // -------------------------------------------------------------
    // VECTOR: Allows in-place updates, aging, decay, and inspection
    // -------------------------------------------------------------
    vector<Process*> ready_pool;
    vector<Process*> io_waiters;
    Process* current_running = nullptr;

    int curr_start_time = 0;
    vector<string> output;

    // Discrete-time simulation loop
    while (completed_processes != total_processes) {
        // 1. Admit new process arrivals at curr_time
        for (int i = 0; i < total_processes; i++) {
            if (processes[i].arrival_time == curr_time) {
                ready_pool.push_back(&processes[i]);
            }
        }

        // 2. Preemption Check (Dynamic Priority):
        // If someone in ready_pool now has better priority than current_running
        if (current_running != nullptr && !ready_pool.empty()) {
            int best_idx = get_highest_priority_index(ready_pool);
            if (ready_pool[best_idx]->dynamic_priority < current_running->dynamic_priority) {
                int burst_num = (current_running->curr_b_idx / 2) + 1;
                output.push_back("P" + to_string(current_running->pid) + "," + to_string(burst_num) +
                                 "\t" + to_string(curr_start_time + 1) +
                                 "\t" + to_string(curr_time));

                ready_pool.push_back(current_running);
                current_running = nullptr;
            }
        }

        // 3. Dispatch CPU if idle
        if (current_running == nullptr && !ready_pool.empty()) {
            int best_idx = get_highest_priority_index(ready_pool);
            current_running = ready_pool[best_idx];
            ready_pool.erase(ready_pool.begin() + best_idx);
            curr_start_time = curr_time;
        }

        // 4. Parallel execution during interval [curr_time, curr_time + 1)
        if (current_running != nullptr) {
            current_running->remaining_b_time--;

            // Apply priority decay via independent function
            apply_priority_decay(current_running, decay_rate);
        }

        // Advance all active I/O bursts
        for (Process* p : io_waiters) {
            p->remaining_b_time--;
        }

        // 5. State transitions at end of tick (curr_time + 1)
        // 5a. Check I/O completions: boost back to base priority and push to pool
        for (int i = (int)io_waiters.size() - 1; i >= 0; i--) {
            if (io_waiters[i]->remaining_b_time == 0) {
                Process* p = io_waiters[i];
                p->curr_b_idx++;
                p->remaining_b_time = p->bursts[p->curr_b_idx];

                // Boost back upon I/O return via independent function
                boost_process_to_base(p);

                ready_pool.push_back(p);
                io_waiters.erase(io_waiters.begin() + i);
            }
        }

        // 5b. Check CPU burst completion
        if (current_running != nullptr && current_running->remaining_b_time == 0) {
            int burst_num = (current_running->curr_b_idx / 2) + 1;
            output.push_back("P" + to_string(current_running->pid) + "," + to_string(burst_num) +
                             "\t" + to_string(curr_start_time + 1) +
                             "\t" + to_string(curr_time + 1));

            current_running->curr_b_idx++;
            if (current_running->curr_b_idx >= (int)current_running->bursts.size()) {
                current_running->completion_time = curr_time + 1;
                completed_processes++;
            } else {
                current_running->remaining_b_time = current_running->bursts[current_running->curr_b_idx];
                io_waiters.push_back(current_running);
            }
            current_running = nullptr;
        }

        curr_time++;
    }

    // Print schedule matching schedule_format.txt
    cout << "CPU0\n";
    for (const string &s : output) {
        cout << s << "\n";
    }
    cout << "\n";

    // Performance statistics
    int total_turnaround = 0;
    int max_turnaround = 0;
    int total_run_time = 0;

    cout << "Final Results\n";
    for (const Process &p : processes) {
        int turnaround = p.completion_time - p.arrival_time;
        cout << "P" << p.pid << " (Base Priority: " << p.queue_level << ") Turnaround Time: " << turnaround << "\n";
        total_turnaround += turnaround;

        if (turnaround > max_turnaround) {
            max_turnaround = turnaround;
        }

        for (size_t i = 0; i < p.bursts.size(); i += 2) {
            total_run_time += p.bursts[i];
        }
    }

    cout << fixed << setprecision(2);
    cout << "Average Turnaround Time : " << ((double)total_turnaround / total_processes) << "\n";
    cout << "Maximum Turnaround Time : " << max_turnaround << "\n";
    cout << "Simulator Run Time : " << total_run_time << "\n";

    return 0;
}
