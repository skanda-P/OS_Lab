#include <iostream>
#include <string>
#include <cstdlib>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;

// Globals so the signal handler (which cannot take extra arguments)
// can reach whichever children THIS process happens to have.
// Exactly one of {g_search_pid} or {g_left_pid, g_right_pid} is
// populated, depending on which branch main() takes.
static pid_t g_search_pid = -1;
static pid_t g_left_pid   = -1;
static pid_t g_right_pid  = -1;

// Runs when an ancestor has decided this whole subtree is no longer
// needed. Announce ourselves, forward the kill to our own children
// (if any), and disappear immediately.
void handle_sigterm(int /*signum*/)
{
	pid_t my_pid = getpid();
	cout << "[" << my_pid << "] received SIGTERM\n";

	// Kill AND reap each child before we die ourselves, so we don't
	// orphan grandchildren and leave zombies behind us.
	if (g_search_pid > 0) { kill(g_search_pid, SIGTERM); waitpid(g_search_pid, NULL, 0); }
	if (g_left_pid   > 0) { kill(g_left_pid,   SIGTERM); waitpid(g_left_pid,   NULL, 0); }
	if (g_right_pid  > 0) { kill(g_right_pid,  SIGTERM); waitpid(g_right_pid,  NULL, 0); }

	_exit(0);
}

int main(int argc, char **argv)
{
	if(argc != 6)
	{
		cout <<"usage: ./partitioner.out <path-to-file> <pattern> <search-start-position> <search-end-position> <max-chunk-size>\nprovided arguments:\n";
		for(int i = 0; i < argc; i++)
			cout << argv[i] << "\n";
		return -1;
	}

	signal(SIGTERM, handle_sigterm);
	cout << unitbuf; // flush after every print so nothing is lost if we're _exit()'d mid-flight

	pid_t my_pid = getpid();

	char *file_to_search_in = argv[1];
	char *pattern_to_search_for = argv[2];
	int search_start_position = atoi(argv[3]);
	int search_end_position = atoi(argv[4]);
	int max_chunk_size = atoi(argv[5]);

	cout << "[" << my_pid << "] start position = " << search_start_position << " ; end position = " << search_end_position << "\n";

	int length = search_end_position - search_start_position + 1;

	if(length <= max_chunk_size)
	{
		// Base case: hand this chunk to a single searcher.
		g_search_pid = fork();

		if(g_search_pid == 0)
		{
			execl("./part3_searcher.out", "./part3_searcher.out",
			      argv[1], argv[2], argv[3], argv[4], NULL);
			// execl only returns on failure
			cout << "[" << my_pid << "] exec failed for searcher\n";
			_exit(127);
		}

		cout << "[" << my_pid << "] forked searcher child " << g_search_pid << "\n";

		int status;
		waitpid(g_search_pid, &status, 0);
		int code = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
		cout << "[" << my_pid << "] searcher child returned\n";

		return (code == 1) ? 1 : 0;
	}
	else
	{
		// Recursive case: split the range and hand each half to a child partitioner.
		int mid   = search_start_position + (search_end_position - search_start_position) / 2;
		int right = mid + 1;

		string mid_str   = to_string(mid);
		string right_str = to_string(right);

		g_left_pid = fork();
		if(g_left_pid == 0)
		{
			execl("./part3_partitioner.out", "./part3_partitioner.out",
			      argv[1], argv[2], argv[3], mid_str.c_str(), argv[5], NULL);
			cout << "[" << my_pid << "] exec failed for left child\n";
			_exit(127);
		}
		cout << "[" << my_pid << "] forked left child " << g_left_pid << "\n";

		g_right_pid = fork();
		if(g_right_pid == 0)
		{
			execl("./part3_partitioner.out", "./part3_partitioner.out",
			      argv[1], argv[2], right_str.c_str(), argv[4], argv[5], NULL);
			cout << "[" << my_pid << "] exec failed for right child\n";
			_exit(127);
		}
		cout << "[" << my_pid << "] forked right child " << g_right_pid << "\n";

		// Unlike Part II, we do NOT always wait left-then-right: we wait for
		// WHICHEVER child finishes first, so that if it already found the
		// pattern we can kill the other subtree immediately instead of
		// letting it keep searching pointlessly.
		int status1;
		pid_t first_done = wait(&status1);
		int code1 = WIFEXITED(status1) ? WEXITSTATUS(status1) : 0;
		bool first_is_left = (first_done == g_left_pid);

		cout << "[" << my_pid << "] " << (first_is_left ? "left" : "right") << " child returned\n";

		pid_t other_pid = first_is_left ? g_right_pid : g_left_pid;

		if(code1 == 1)
		{
			// Already found: the other subtree is now useless. Kill it and
			// reap it quietly (no "returned" line for it -- it didn't
			// return, it was terminated) then bubble the success up.
			kill(other_pid, SIGTERM);
			waitpid(other_pid, NULL, 0);
			return 1;
		}

		// First child came back empty-handed: wait normally for the second.
		int status2;
		waitpid(other_pid, &status2, 0);
		int code2 = WIFEXITED(status2) ? WEXITSTATUS(status2) : 0;

		cout << "[" << my_pid << "] " << (first_is_left ? "right" : "left") << " child returned\n";

		return (code2 == 1) ? 1 : 0;
	}
}
