#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <queue>

using namespace std;

struct Process{
    int pid;
    int arrival_time;
    vector<int> bursts;  // even indices are CPU bursts , odd indices are I/O bursts
    int curr_b_idx;
    int remaining_b_time;
    int completion_time;
};


int main(int argc,char **argv){
  

    if(argc <= 3){
        cout << "Usage: ./simulator <algorithm> [quantum-if-RR]  <workload_file>" << endl;
        return 1;
    }
  

    string algo = argv[1];
    int quantum = 0;
    string filepath;
    if(algo == "RR"){
      if(argc != 4){
        cout << "Error: RR requires a time quantum " << endl;
        return 1;
      }

      quantum = stoi(argv[2]); // Convert the string "20" to an integer 20
      filepath = argv[3];
    }else{
        if(argc != 3){
        cout << "Error: FIFO does not use a time quantum " << endl;
        return 1;
        }

    }

    
    

    ifstream file(filepath);

    if(!file.is_open()){
        cout << "Failed to open file." << endl;
        return 1;
    }
    

    vector<Process> processes;
    int pid_counter = 1;

    int arrival;

    // Read the first integer of a line (arrival time)

    while(file >> arrival){
        Process p;
        p.pid = pid_counter++;
        p.arrival_time = arrival;
        p.curr_b_idx = 0;

        int burst_val;

        while(file >> burst_val && burst_val != -1){
            p.bursts.push_back(burst_val);
        }


        p.remaining_b_time = p.bursts[0];
        processes.push_back(p);
    }

    cout << "Parsed " << processes.size() << " processes" << endl; 

    int curr_time = 0;
    int completed_processes = 0;
    int total_processes = processes.size();

    queue<Process*> ready_queue;
    vector<Process*> io_waiters;
    Process* current_running = nullptr;

    vector<string> output;
    int curr_start_time = 0;
    int current_slice = 0;


    while(completed_processes != total_processes){
        for(int i = 0; i < total_processes; i++){
            if(processes[i].arrival_time == curr_time){
                ready_queue.push(&processes[i]);
            } 
        }

        if(current_running  == nullptr && !ready_queue.empty()){
            current_running = ready_queue.front();
            ready_queue.pop();
            curr_start_time = curr_time;
        }

        if(current_running != nullptr){
          current_running->remaining_b_time--;
          current_slice++;

            if(current_running->remaining_b_time == 0){
              current_running->curr_b_idx++;
              
              int burst_num = (current_running->curr_b_idx / 2) + 1;

              string str = "P" + to_string(current_running->pid) + 
                           "," + to_string(burst_num) +
                           "\t" + to_string(curr_start_time) + 
                           "\t" + to_string(curr_time + 1);

              output.push_back(str);

              if(current_running->curr_b_idx >= (int)current_running->bursts.size()){
                  current_running->completion_time = curr_time + 1;
                  completed_processes++;
              }else{
                current_running->remaining_b_time = current_running->bursts[current_running->curr_b_idx];
                io_waiters.push_back(current_running);
              }
              current_running = nullptr;
              current_slice = 0;

            }else if(algo == "RR" && current_slice == quantum){

              int burst_num = (current_running->curr_b_idx / 2) + 1;

              string str = "P" + to_string(current_running->pid) + 
                           "," + to_string(burst_num) +
                           "\t" + to_string(curr_start_time) + 
                           "\t" + to_string(curr_time + 1);

              output.push_back(str);

              ready_queue.push(current_running);

              current_running = nullptr;
              current_slice = 0;
            }
        }
        

        for(int i = io_waiters.size() - 1 ; i >=0; i-- ){
            Process* io_process = io_waiters[i];

            io_process->remaining_b_time--;

            if(io_process->remaining_b_time == 0){
              io_process->curr_b_idx++;
              io_process->remaining_b_time = io_process->bursts[io_process->curr_b_idx];
              ready_queue.push(io_process);
              io_waiters.erase(io_waiters.begin() + i);
            }
        }


        curr_time++;
    }

    int total_turnaround = 0;
    int max_turnaround = 0;
    int total_run_time = 0;

    cout << endl << "CPU0" << endl;
    for(string s : output){
        cout << s << endl;
    }
    cout << endl;

    cout << " Final Results" << endl;
    for(Process p : processes){
      int turnaround = p.completion_time - p.arrival_time;
      cout << "P" << p.pid << " Turnaround Time: " << turnaround << endl;
      total_turnaround += turnaround;

      if(turnaround >= max_turnaround){
        max_turnaround = turnaround;
      }

      for(int i = 0; i < p.bursts.size(); i+=2){
        total_run_time += p.bursts[i];
      }
    }

    
    cout << "Average Turnaround Time : " << ((double)total_turnaround / total_processes) << endl;
    cout << "Maximum Turnaround Time : " << max_turnaround << endl;
    cout << "Simulator Run Time : " << total_run_time << endl;

    return 0;
}
