#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/wait.h>


using namespace std;

int main(int argc, char **argv)
{
	if(argc != 6)
	{
		cout <<"usage: ./partitioner.out <path-to-file> <pattern> <search-start-position> <search-end-position> <max-chunk-size>\nprovided arguments:\n";
		for(int i = 0; i < argc; i++)
			cout << argv[i] << "\n";
		return -1;
	}
	
	pid_t my_pid = getpid();

	char *file_to_search_in = argv[1];
	char *pattern_to_search_for = argv[2];
	int search_start_position = atoi(argv[3]);
	int search_end_position = atoi(argv[4]);
	int max_chunk_size = atoi(argv[5]);
	
	cout << "[" << my_pid << "] start position = " << search_start_position << " ; end position = " << search_end_position << "\n";

	// Calculate the size of our assigned chunk
	int length = search_end_position - search_start_position + 1;
	int status;


	pid_t my_children[2];

	if(length <= max_chunk_size){
		// Base Case : Range is within max_chunk_size, so we search it directly.
		pid_t searcher_pid = fork();
		if(searcher_pid == 0){
			// Child: Replace the current process image with the searcher executable
			execl("./part2_searcher.out","./part2_searcher.out",argv[1],argv[2],argv[3],argv[4],NULL);
		}
		cout << "[" << my_pid << "] forked searcher child " << searcher_pid << "\n";

		// Parent: Wait for the searcher to finish
		waitpid(searcher_pid,&status,0);
		int searcher_exit_code = 0;

		// Extract the exit code (1 = found, 0 = not found)

		if(WIFEXITED(status)){
			searcher_exit_code =  WEXITSTATUS(status);
			cout << "[" << my_pid << "] searcher child returned" << endl;
			return searcher_exit_code; // Bubble the result back up
		}

		return 0;
	}else{
		// Recursive Case: Range is too large, split it in half
		int mid = search_start_position + (search_end_position -search_start_position) / 2;
		int right = mid + 1;

		// Convert integers to C-strings for execl arguments
		string right_start  = to_string(right);
		string mid_str = to_string(mid);

		// Spawn left partitioner
		my_children[0] = fork();

		if(my_children[0] == 0){
			execl("./part2_partitioner.out","./part2_partitioner.out",argv[1],argv[2],argv[3],mid_str.c_str(),argv[5],NULL);
		}
		cout << "[" << my_pid << "] forked left child " << my_children[0] << "\n";
		
		// Spawn right partitioner
		my_children[1] = fork();

		if(my_children[1] == 0){
			execl("./part2_partitioner.out","./part2_partitioner.out",argv[1],argv[2],right_start.c_str(),argv[4],argv[5],NULL);
		}
		cout << "[" << my_pid << "] forked right child " << my_children[1] << "\n";

		// Wait for both child partitioners to finish and grab their exit statuses
		int left_status;
		waitpid(my_children[0],&left_status,0);
		int left_exit_code = 0;
		if(WIFEXITED(left_status)){
			left_exit_code = WEXITSTATUS(left_status);
		}
		cout << "[" << my_pid << "] left child returned\n";

		int right_status;
		waitpid(my_children[1],&right_status,0);
		int right_exit_code = 0;
		if(WIFEXITED(right_status)){
			right_exit_code = WEXITSTATUS(right_status);
		}
		cout << "[" << my_pid << "] right child returned\n";

		// If either the left or right subtree found the pattern, return 1
		if(left_exit_code == 1 || right_exit_code == 1){
			return 1;
		}
		return 0;

	}

	return 0;
}
