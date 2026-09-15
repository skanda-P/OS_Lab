#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <queue>
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

int main(int argc, char **argv) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <algorithm> [quantum | boost_interval] <workload_file>\n";
        return 1;
    }

    string algo = argv[1];
    for (char &c : algo) c = toupper(c);

    // Default configuration values
    int quantum = 2;          // Default RR time quantum
    int boost_interval = 0;   // Default MLFQ boost interval (0 = no priority boost)
    string filepath;

    // Support both strict signature (<algo> <file>) and parameterized signature (<algo> <param> <file>)
    if (algo == "FIFO" || algo == "FCFS") {
        filepath = argv[2];
    } else if (algo == "RR") {
        if (argc >= 4) {
            quantum = stoi(argv[2]);
            filepath = argv[3];
        } else {
            filepath = argv[2];
        }
    } else if (algo == "MLFQ") {
        if (argc >= 4) {
            boost_interval = stoi(argv[2]);
            filepath = argv[3];
        } else {
            filepath = argv[2];
        }
    } else {
        cerr << "Error: Unknown scheduling algorithm '" << argv[1] << "'\n";
        return 1;
    }

    // Set time slice length per algorithm specification (MLFQ queues all use quantum = 2)
    int current_quantum = 0;
    if (algo == "RR") current_quantum = quantum;
    else if (algo == "MLFQ") current_quantum = 2;

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

    // Scheduling queues and execution tracking
    queue<Process*> ready_queue;         // Used by FIFO and RR
    queue<Process*> mlfq_queues[3];      // MLFQ: Q0 (highest) to Q2 (lowest)
    vector<Process*> io_waiters;         // Processes currently performing I/O
    Process* current_running = nullptr;

    int curr_start_time = 0;
    int current_slice = 0;
    vector<string> output;

    // Discrete-time simulation loop
    while (completed_processes != total_processes) {
        // 1. Admit new process arrivals at curr_time
        for (int i = 0; i < total_processes; i++) {
            if (processes[i].arrival_time == curr_time) {
                if (algo == "MLFQ") {
                    processes[i].queue_level = 0;
                    mlfq_queues[0].push(&processes[i]);
                } else {
                    ready_queue.push(&processes[i]);
                }
            }
        }

        // 2. Periodic priority boost for MLFQ: move all processes back to Q0
        if (algo == "MLFQ" && boost_interval > 0 && curr_time > 0 && curr_time % boost_interval == 0) {
            for (int lvl = 1; lvl < 3; lvl++) {
                while (!mlfq_queues[lvl].empty()) {
                    Process* p = mlfq_queues[lvl].front();
                    mlfq_queues[lvl].pop();
                    p->queue_level = 0;
                    mlfq_queues[0].push(p);
                }
            }
            for (Process* p : io_waiters) {
                p->queue_level = 0;
            }
            if (current_running != nullptr) {
                current_running->queue_level = 0;
                current_slice = 0;
            }
        }

        // 3. Dispatch CPU if idle
        if (current_running == nullptr) {
            if (algo == "MLFQ") {
                for (int lvl = 0; lvl < 3; lvl++) {
                    if (!mlfq_queues[lvl].empty()) {
                        current_running = mlfq_queues[lvl].front();
                        mlfq_queues[lvl].pop();
                        curr_start_time = curr_time;
                        current_slice = 0;
                        break;
                    }
                }
            } else if (!ready_queue.empty()) {
                current_running = ready_queue.front();
                ready_queue.pop();
                curr_start_time = curr_time;
                current_slice = 0;
            }
        }

        // 4. Parallel execution during interval [curr_time, curr_time + 1)
        if (current_running != nullptr) {
            current_running->remaining_b_time--;
            current_slice++;
        }

        // Advance all active I/O bursts
        for (Process* p : io_waiters) {
            p->remaining_b_time--;
        }

        // 5. State transitions at end of tick (curr_time + 1)
        // 5a. Check I/O completions: move ready processes to queues for next tick
        for (int i = (int)io_waiters.size() - 1; i >= 0; i--) {
            if (io_waiters[i]->remaining_b_time == 0) {
                Process* p = io_waiters[i];
                p->curr_b_idx++;
                p->remaining_b_time = p->bursts[p->curr_b_idx];
                if (algo == "MLFQ") {
                    mlfq_queues[p->queue_level].push(p);
                } else {
                    ready_queue.push(p);
                }
                io_waiters.erase(io_waiters.begin() + i);
            }
        }

        // 5b. Check CPU burst completion or time quantum expiration
        if (current_running != nullptr) {
            if (current_running->remaining_b_time == 0) {
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
                current_slice = 0;
            } else if (current_quantum > 0 && current_slice == current_quantum) {
                // Time slice expired; preempt process
                int burst_num = (current_running->curr_b_idx / 2) + 1;
                output.push_back("P" + to_string(current_running->pid) + "," + to_string(burst_num) +
                                 "\t" + to_string(curr_start_time + 1) +
                                 "\t" + to_string(curr_time + 1));

                if (algo == "MLFQ") {
                    if (current_running->queue_level < 2) {
                        current_running->queue_level++;
                    }
                    mlfq_queues[current_running->queue_level].push(current_running);
                } else {
                    ready_queue.push(current_running);
                }
                current_running = nullptr;
                current_slice = 0;
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
