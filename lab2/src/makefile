# --- Part I ---
build-part1: part1.out

part1.out: part1_searcher.cpp
	g++ -g part1_searcher.cpp -o part1.out

run-part1: part1.out
	-./part1.out file.txt NGTNIJGK 0 67108863

clean-part1:
	rm -f part1.out


# --- Part II ---
build-part2: part2_searcher.out part2_partitioner.out

part2_searcher.out: part2_searcher.cpp
	g++ -g part2_searcher.cpp -o part2_searcher.out

part2_partitioner.out: part2_partitioner.cpp
	g++ -g part2_partitioner.cpp -o part2_partitioner.out

run-part2: build-part2
	-./part2_partitioner.out file.txt NGTNIJGK 0 67108863 8388608

clean-part2:
	rm -f part2_searcher.out part2_partitioner.out


# --- Part III ---
build-part3: part3_searcher.out part3_partitioner.out

part3_searcher.out: part3_searcher.cpp
	g++ -g part3_searcher.cpp -o part3_searcher.out

part3_partitioner.out: part3_partitioner.cpp
	g++ -g part3_partitioner.cpp -o part3_partitioner.out

run-part3: build-part3
	-./part3_partitioner.out file.txt NGTNIJGK 0 67108863 8388608

clean-part3:
	rm -f part3_searcher.out part3_partitioner.out


# --- Global Clean ---
clean:
	rm -f *.out