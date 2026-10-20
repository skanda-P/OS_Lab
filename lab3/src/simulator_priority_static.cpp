#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <queue>
#include <cctype>

using namespace std;

// Process structure (identical to lab structure)
struct Process {
    int pid;
    int arrival_time; 
    vector<int> bursts;  // Even indices are CPU bursts, odd indices are I/O bursts
    int curr_b_idx;
    int remaining_b_time;
    int completion_time;
    int queue_level;     // Static User Priority: 0 (highest) to 2 (lowest)
};

// Custom Comparator for std::priority_queue
// In C++, operator(a, b) returns true if 'a' has LOWER priority than 'b' (so 'b' goes to top)
struct ComparePriority {
    bool operator()(const Process* a, const Process* b) const {
        if (a->queue_level != b->queue_level) {
            return a->queue_level > b->queue_level; // Lower number = higher priority
        }
        if (a->arrival_time != b->arrival_time) {
            return a->arrival_time > b->arrival_time; // Earlier arrival = higher priority
        }
        return a->pid > b->pid; // Lower PID = tie breaker
    }
};

int main(int argc, char **argv) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <PRIORITY_PREEMPTIVE | PRIORITY_NONPREEMPTIVE> <workload_file>\n";
        return 1;
    }

    string algo = argv[1];
    for (char &c : algo) c = toupper(c);

    bool is_preemptive = true;
    if (algo == "PRIORITY_NONPREEMPTIVE" || algo == "NONPREEMPTIVE") {
        is_preemptive = false;
    } else if (algo != "PRIORITY_PREEMPTIVE" && algo != "PREEMPTIVE" && algo != "PRIORITY") {
        cerr << "Error: Unknown algorithm '" << argv[1] << "'. Supported: PRIORITY_PREEMPTIVE, PRIORITY_NONPREEMPTIVE\n";
        return 1;
    }

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
        // User-defined static priority: (PID % 3 gives priority 0, 1, or 2)
        p.queue_level = (p.pid % 3);

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
    // PRIORITY QUEUE: Automatically maintains highest priority at top()
    // -------------------------------------------------------------
    priority_queue<Process*, vector<Process*>, ComparePriority> ready_pq;
    vector<Process*> io_waiters;
    Process* current_running = nullptr;

    int curr_start_time = 0;
    vector<string> output;

    // Discrete-time simulation loop
    while (completed_processes != total_processes) {
        // 1. Admit new process arrivals at curr_time
        for (int i = 0; i < total_processes; i++) {
            if (processes[i].arrival_time == curr_time) {
                ready_pq.push(&processes[i]); // O(log N) push
            }
        }

        // 2. Preemption Check (Preemptive Static Priority):
        // If highest priority process in ready_pq has strictly better priority than running process
        if (is_preemptive && current_running != nullptr && !ready_pq.empty()) {
            Process* top_candidate = ready_pq.top(); // O(1) peek
            if (top_candidate->queue_level < current_running->queue_level) {
                int burst_num = (current_running->curr_b_idx / 2) + 1;
                output.push_back("P" + to_string(current_running->pid) + "," + to_string(burst_num) +
                                 "\t" + to_string(curr_start_time + 1) +
                                 "\t" + to_string(curr_time));

                ready_pq.push(current_running); // Push preempted process back into PQ
                current_running = nullptr;
            }
        }

        // 3. Dispatch CPU if idle
        if (current_running == nullptr && !ready_pq.empty()) {
            current_running = ready_pq.top(); // O(1) read
            ready_pq.pop();                   // O(log N) remove
            curr_start_time = curr_time;
        }

        // 4. Parallel execution during interval [curr_time, curr_time + 1)
        if (current_running != nullptr) {
            current_running->remaining_b_time--;
        }

        for (Process* p : io_waiters) {
            p->remaining_b_time--;
        }

        // 5. State transitions at end of tick (curr_time + 1)
        // 5a. Check I/O completions: push ready processes into ready_pq
        for (int i = (int)io_waiters.size() - 1; i >= 0; i--) {
            if (io_waiters[i]->remaining_b_time == 0) {
                Process* p = io_waiters[i];
                p->curr_b_idx++;
                p->remaining_b_time = p->bursts[p->curr_b_idx];
                ready_pq.push(p); // Push back into PQ
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
        cout << "P" << p.pid << " (Priority: " << p.queue_level << ") Turnaround Time: " << turnaround << "\n";
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
