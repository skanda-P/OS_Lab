#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cctype>
#include <iomanip>

using namespace std;

// Process structure tracking description from file and runtime execution state
struct Process {
    int pid;
    int arrival_time;
    vector<int> bursts;       // Even indices are CPU bursts, odd indices are I/O bursts
    int curr_b_idx;
    int remaining_b_time;
    int completion_time;
    int queue_level;          // Base priority: lower number = higher priority (0 highest)
    double dynamic_priority;  // Priority adjusted dynamically over time (decay / aging)
};

// Find the process with the highest priority (lowest numerical priority value).
// Tie-breaking: earlier arrival time, then smaller PID.
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

// ============================================================================
// INDEPENDENT PRIORITY ADJUSTMENT FUNCTIONS
// (Isolated from main simulation loop logic)
// ============================================================================

// 1. Priority Decay: Penalizes the running process for consuming CPU time.
// (Since lower numerical value = higher priority, decay adds to the value).
void apply_priority_decay(Process* current_running, double decay_rate) {
    if (current_running != nullptr) {
        current_running->dynamic_priority += decay_rate;
    }
}

// 2. Priority Boost (Aging): Boosts waiting processes to prevent starvation.
// Subtracts aging_rate down to a minimum floor of the base priority.
void apply_waiting_priority_boost(vector<Process*> &ready_pool, double aging_rate) {
    for (Process* p : ready_pool) {
        if (p->dynamic_priority > (double)p->queue_level) {
            p->dynamic_priority = max((double)p->queue_level, p->dynamic_priority - aging_rate);
        }
    }
}

// 3. Reset Boost: Restores a process's dynamic priority completely back to its base priority
// (e.g. when finishing an I/O burst, or during a periodic epoch boost).
void boost_process_to_base(Process* p) {
    if (p != nullptr) {
        p->dynamic_priority = (double)p->queue_level;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <PRIORITY | PRIORITY_DECAY> [decay_rate] <workload_file>\n";
        return 1;
    }

    string algo = argv[1];
    for (char &c : algo) c = toupper(c);

    double decay_rate = 0.5; // Default decay rate per CPU tick
    string filepath;

    if (algo == "PRIORITY") {
        filepath = argv[2];
    } else if (algo == "PRIORITY_DECAY") {
        if (argc >= 4) {
            decay_rate = stod(argv[2]);
            filepath = argv[3];
        } else {
            filepath = argv[2];
        }
    } else {
        cerr << "Error: Unknown algorithm '" << argv[1] << "'. Supported: PRIORITY, PRIORITY_DECAY\n";
        return 1;
    }

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
        
        // Base priority assignment:
        // By default, earlier processes or user-assigned priority can be placed here.
        // E.g., PID % 3 creates varying priorities (0 = High, 1 = Medium, 2 = Low)
        p.queue_level = (p.pid % 3); 
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

    // Scheduling pool and execution tracking
    vector<Process*> ready_pool;
    vector<Process*> io_waiters;
    Process* current_running = nullptr;

    int curr_start_time = 0;
    int current_slice = 0;
    const int RR_QUANTUM = 2; // Time quantum for Round-Robin among equal-priority processes
    vector<string> output;

    // Discrete-time simulation loop
    while (completed_processes != total_processes) {
        // 1. Admit new process arrivals at curr_time
        for (int i = 0; i < total_processes; i++) {
            if (processes[i].arrival_time == curr_time) {
                ready_pool.push_back(&processes[i]);
            }
        }

        // 2. Preemption Check:
        // If a process in ready_pool has strictly higher priority (lower numerical value),
        // preempt the currently running process.
        if (current_running != nullptr && !ready_pool.empty()) {
            int best_idx = get_highest_priority_index(ready_pool);
            if (ready_pool[best_idx]->dynamic_priority < current_running->dynamic_priority) {
                int burst_num = (current_running->curr_b_idx / 2) + 1;
                output.push_back("P" + to_string(current_running->pid) + "," + to_string(burst_num) +
                                 "\t" + to_string(curr_start_time + 1) +
                                 "\t" + to_string(curr_time));

                ready_pool.push_back(current_running);
                current_running = nullptr;
                current_slice = 0;
            }
        }

        // 3. Dispatch CPU if idle
        if (current_running == nullptr && !ready_pool.empty()) {
            int best_idx = get_highest_priority_index(ready_pool);
            current_running = ready_pool[best_idx];
            ready_pool.erase(ready_pool.begin() + best_idx);
            curr_start_time = curr_time;
            current_slice = 0;
        }

        // 4. Parallel execution during interval [curr_time, curr_time + 1)
        if (current_running != nullptr) {
            current_running->remaining_b_time--;
            current_slice++;

            // PRIORITY DECAY (calls independent function)
            if (algo == "PRIORITY_DECAY") {
                apply_priority_decay(current_running, decay_rate);
            }
        }

        // Advance all active I/O bursts
        for (Process* p : io_waiters) {
            p->remaining_b_time--;
        }

        // 5. State transitions at end of tick (curr_time + 1)
        // 5a. Check I/O completions: move ready processes to pool for next tick
        for (int i = (int)io_waiters.size() - 1; i >= 0; i--) {
            if (io_waiters[i]->remaining_b_time == 0) {
                Process* p = io_waiters[i];
                p->curr_b_idx++;
                p->remaining_b_time = p->bursts[p->curr_b_idx];
                
                // PRIORITY BOOST (calls independent function upon returning from I/O)
                boost_process_to_base(p);
                ready_pool.push_back(p);
                io_waiters.erase(io_waiters.begin() + i);
            }
        }

        // 5b. Check CPU burst completion or Round-Robin quantum expiration
        if (current_running != nullptr) {
            if (current_running->remaining_b_time == 0) {
                // CPU burst finished
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
                current_slice = 0;
            } 
            // Combined with Round-Robin: if equal-priority processes are waiting, time-slice!
            else if (current_slice == RR_QUANTUM && !ready_pool.empty()) {
                int best_idx = get_highest_priority_index(ready_pool);
                // Only time-slice if waiting process has EQUAL priority
                if (ready_pool[best_idx]->dynamic_priority == current_running->dynamic_priority) {
                    int burst_num = (current_running->curr_b_idx / 2) + 1;
                    output.push_back("P" + to_string(current_running->pid) + "," + to_string(burst_num) +
                                     "\t" + to_string(curr_start_time + 1) +
                                     "\t" + to_string(curr_time + 1));

                    ready_pool.push_back(current_running);
                    current_running = nullptr;
                    current_slice = 0;
                }
            }
        }

        curr_time++;
    }

    // Print schedule matching schedule_format.txt
    cout << "CPU0\n";
    for (const string &s : output) {
        cout << s << "\n";
    }
    cout << "\n";

    // Compute and report performance statistics
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
