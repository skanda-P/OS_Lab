#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cctype>

using namespace std;

// Process structure tracking description from file and runtime execution state
struct Process {
    int pid;
    int arrival_time;
    vector<int> bursts;  // Even indices are CPU bursts, odd indices are I/O bursts
    int curr_b_idx;
    int remaining_b_time;
    int completion_time;
    int queue_level;     // MLFQ queue priority: 0 (highest) to 2 (lowest)
};

// Helper function: finds index of the process with the shortest remaining CPU burst time.
// Tie-breaks by arrival time (earlier first), then PID (smaller first).
int get_shortest_job_index(const vector<Process*> &pool) {
    if (pool.empty()) return -1;
    int best_idx = 0;
    for (size_t i = 1; i < pool.size(); i++) {
        if (pool[i]->remaining_b_time < pool[best_idx]->remaining_b_time) {
            best_idx = (int)i;
        } else if (pool[i]->remaining_b_time == pool[best_idx]->remaining_b_time) {
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
        cerr << "Usage: " << argv[0] << " <SJF | STCF> <workload_file>\n";
        return 1;
    }

    string algo = argv[1];
    for (char &c : algo) c = toupper(c);

    if (algo != "SJF" && algo != "STCF" && algo != "SRTF") {
        cerr << "Error: Unknown algorithm '" << argv[1] << "'. Supported: SJF, STCF (or SRTF)\n";
        return 1;
    }
    bool is_preemptive = (algo == "STCF" || algo == "SRTF");

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
        p.queue_level = 0;

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
    vector<Process*> ready_pool;         // Ready processes available to schedule
    vector<Process*> io_waiters;         // Processes currently performing I/O
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

        // 2. Preemption Check for STCF (Preemptive SJF):
        // If a newly arrived or I/O-returned process has STRICTLY shorter remaining
        // CPU burst time than the currently running process, preempt the running process.
        if (is_preemptive && current_running != nullptr && !ready_pool.empty()) {
            int best_idx = get_shortest_job_index(ready_pool);
            if (ready_pool[best_idx]->remaining_b_time < current_running->remaining_b_time) {
                // Log schedule slice for the preempted process: [curr_start_time + 1, curr_time]
                int burst_num = (current_running->curr_b_idx / 2) + 1;
                output.push_back("P" + to_string(current_running->pid) + "," + to_string(burst_num) +
                                 "\t" + to_string(curr_start_time + 1) +
                                 "\t" + to_string(curr_time));

                // Return preempted process to ready pool
                ready_pool.push_back(current_running);
                current_running = nullptr;
            }
        }

        // 3. Dispatch CPU if idle
        if (current_running == nullptr && !ready_pool.empty()) {
            int best_idx = get_shortest_job_index(ready_pool);
            current_running = ready_pool[best_idx];
            ready_pool.erase(ready_pool.begin() + best_idx);
            curr_start_time = curr_time;
        }

        // 4. Parallel execution during interval [curr_time, curr_time + 1)
        if (current_running != nullptr) {
            current_running->remaining_b_time--;
        }

        // Advance all active I/O bursts
        for (Process* p : io_waiters) {
            p->remaining_b_time--;
        }

        // 5. State transitions at end of tick (curr_time + 1)
        // 5a. Check I/O completions: move ready processes to ready_pool for next tick
        for (int i = (int)io_waiters.size() - 1; i >= 0; i--) {
            if (io_waiters[i]->remaining_b_time == 0) {
                Process* p = io_waiters[i];
                p->curr_b_idx++; // Advance to next CPU burst
                p->remaining_b_time = p->bursts[p->curr_b_idx];
                ready_pool.push_back(p);
                io_waiters.erase(io_waiters.begin() + i);
            }
        }

        // 5b. Check CPU burst completion
        if (current_running != nullptr && current_running->remaining_b_time == 0) {
            // CPU burst finished; record schedule in 1-based inclusive format
            int burst_num = (current_running->curr_b_idx / 2) + 1;
            output.push_back("P" + to_string(current_running->pid) + "," + to_string(burst_num) +
                             "\t" + to_string(curr_start_time + 1) +
                             "\t" + to_string(curr_time + 1));

            current_running->curr_b_idx++;
            if (current_running->curr_b_idx >= (int)current_running->bursts.size()) {
                current_running->completion_time = curr_time + 1;
                completed_processes++;
            } else {
                // Next phase is I/O burst; begins on the next simulation tick
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

    // Compute and report performance statistics
    int total_turnaround = 0;
    int max_turnaround = 0;
    int total_run_time = 0;

    cout << "Final Results\n";
    for (const Process &p : processes) {
        int turnaround = p.completion_time - p.arrival_time;
        cout << "P" << p.pid << " Turnaround Time: " << turnaround << "\n";
        total_turnaround += turnaround;

        if (turnaround > max_turnaround) {
            max_turnaround = turnaround;
        }

        for (size_t i = 0; i < p.bursts.size(); i += 2) {
            total_run_time += p.bursts[i];
        }
    }

    cout << "Average Turnaround Time : " << ((double)total_turnaround / total_processes) << "\n";
    cout << "Maximum Turnaround Time : " << max_turnaround << "\n";
    cout << "Simulator Run Time : " << total_run_time << "\n";

    return 0;
}
