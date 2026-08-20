#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <signal.h>

using namespace std;

int main(int argc, char **argv)
{
	if(argc != 5)
	{
		cout <<"usage: ./partitioner.out <path-to-file> <pattern> <search-start-position> <search-end-position>\nprovided arguments:\n";
		for(int i = 0; i < argc; i++)
			cout << argv[i] << "\n";
		return -1;
	}
	
	char *file_to_search_in = argv[1];
	char *pattern_to_search_for = argv[2];
	int search_start_position = atoi(argv[3]);
	int search_end_position = atoi(argv[4]);

	pid_t pid = getpid();

	ifstream file(file_to_search_in);
	if(!file.is_open())
	{
		cout << "[-1] failed to open file: " << file_to_search_in << "\n";
		return -1;
	}

	int length_to_search = search_end_position - search_start_position + 1;

	if(length_to_search > 0)
	{
		file.seek(search_start_position);
		string file_chunk;
		file_chunk.resize(length_to_search);
		file.read(&file_chunk[0], length_to_search);
		file_chunk.resize(file.gcount());

		string pattern(pattern_to_search_for);
		size_t found_position = file_chunk.find(pattern);
		if(found_position != string::npos)
		{
			// Pattern found
			cout << "[" << pid << "] found at " << (search_start_position + found_position) << "\n";
			return 1;	
		}
	}

cout << "[" << pid << "] didn't find\n";
	return 0;
}