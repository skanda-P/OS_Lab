CXX ?= g++
CXXFLAGS ?= -O3 -Wall -Wextra -std=c++17 -pthread -Icommon

INPUT ?= input.ppm
ITERS ?= 1000
PORT  ?= 12345
IP    ?= 127.0.0.1

COMMON_SRCS = common/libppm.cpp

all: part1 part2_1_variant1 part2_1_variant2 part2_2_variant1 part2_2_variant2 part2_3

# ----------------- Part I -----------------
part1: part1.out
	./part1.out $(INPUT) output_part1.ppm

part1.out: part1/part1.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^


# ----------------- Part II.1 (Threads + Atomic) -----------------
part2_1_variant1: part2_1_variant1.out
	./part2_1_variant1.out $(INPUT) output_part2_1_variant1.ppm $(ITERS)
	@cp -f output_part2_1_variant1.ppm output_part2_2_variant1.ppm 2>/dev/null || true

part2_1_variant1.out: part2_1/part2_1_variant1.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

part2_1_variant2: part2_1_variant2.out
	./part2_1_variant2.out $(INPUT) output_part2_1_variant2.ppm $(ITERS)
	@cp -f output_part2_1_variant2.ppm output_part2_2_variant2.ppm 2>/dev/null || true

part2_1_variant2.out: part2_1/part2_1_variant2.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^


# ----------------- Part II.2 (Processes + Pipes) -----------------
part2_2_variant1: part2_2_variant1.out
	./part2_2_variant1.out $(INPUT) output_part2_2_variant1.ppm $(ITERS)

part2_2_variant1.out: part2_2/part2_2_variant1.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

part2_2_variant2: part2_2_variant2.out
	./part2_2_variant2.out $(INPUT) output_part2_2_variant2.ppm $(ITERS)

part2_2_variant2.out: part2_2/part2_2_variant2.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^


# ----------------- Part II.3 (Processes + SHM + Semaphores) -----------------
part2_3: part2_3.out
	./part2_3.out $(INPUT) output_part2_3.ppm

part2_3.out: part2_3/part2_3.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

part2_3_variant1: part2_3_variant1.out
	./part2_3_variant1.out $(INPUT) output_part2_3_variant1.ppm $(ITERS)

part2_3_variant1.out: part2_3/part2_3_variant1.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

part2_3_variant2: part2_3_variant2.out
	./part2_3_variant2.out $(INPUT) output_part2_3_variant2.ppm $(ITERS)

part2_3_variant2.out: part2_3/part2_3_variant2.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^


# ----------------- Part III.1 (Sockets: A=S1+S2, B=S3) -----------------
part3_1_variant1_A: part3_1_variant1_A.out
	./part3_1_variant1_A.out $(PORT) $(INPUT) $(ITERS)

part3_1_variant1_A.out: part3_1/part3_1_variant1_A.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

part3_1_variant1_B: part3_1_variant1_B.out
	./part3_1_variant1_B.out $(IP) $(PORT) output_part3_1_variant1.ppm

part3_1_variant1_B.out: part3_1/part3_1_variant1_B.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

part3_1_variant2_A: part3_1_variant2_A.out
	./part3_1_variant2_A.out $(PORT) $(INPUT) $(ITERS)

part3_1_variant2_A.out: part3_1/part3_1_variant2_A.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

part3_1_variant2_B: part3_1_variant2_B.out
	./part3_1_variant2_B.out $(IP) $(PORT) output_part3_1_variant2.ppm

part3_1_variant2_B.out: part3_1/part3_1_variant2_B.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^


# ----------------- Part III.2 (Sockets: A=S1, B=S2+S3) -----------------
part3_2_variant1_A: part3_2_variant1_A.out
	./part3_2_variant1_A.out $(PORT) $(INPUT) $(ITERS)

part3_2_variant1_A.out: part3_2/part3_2_variant1_A.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

part3_2_variant1_B: part3_2_variant1_B.out
	./part3_2_variant1_B.out $(IP) $(PORT) output_part3_2_variant1.ppm

part3_2_variant1_B.out: part3_2/part3_2_variant1_B.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

part3_2_variant2_A: part3_2_variant2_A.out
	./part3_2_variant2_A.out $(PORT) $(INPUT) $(ITERS)

part3_2_variant2_A.out: part3_2/part3_2_variant2_A.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

part3_2_variant2_B: part3_2_variant2_B.out
	./part3_2_variant2_B.out $(IP) $(PORT) output_part3_2_variant2.ppm

part3_2_variant2_B.out: part3_2/part3_2_variant2_B.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^


# ----------------- Convenience targets for local socket testing -----------------
test_part3_1_variant1: part3_1_variant1_A.out part3_1_variant1_B.out
	./part3_1_variant1_A.out $(PORT) $(INPUT) $(ITERS) & \
	SERVER_PID=$$!; \
	sleep 0.1; \
	./part3_1_variant1_B.out $(IP) $(PORT) output_part3_1_variant1.ppm; \
	wait $$SERVER_PID

test_part3_1_variant2: part3_1_variant2_A.out part3_1_variant2_B.out
	./part3_1_variant2_A.out $(PORT) $(INPUT) $(ITERS) & \
	SERVER_PID=$$!; \
	sleep 0.1; \
	./part3_1_variant2_B.out $(IP) $(PORT) output_part3_1_variant2.ppm; \
	wait $$SERVER_PID

test_part3_2_variant1: part3_2_variant1_A.out part3_2_variant1_B.out
	./part3_2_variant1_A.out $(PORT) $(INPUT) $(ITERS) & \
	SERVER_PID=$$!; \
	sleep 0.1; \
	./part3_2_variant1_B.out $(IP) $(PORT) output_part3_2_variant1.ppm; \
	wait $$SERVER_PID

test_part3_2_variant2: part3_2_variant2_A.out part3_2_variant2_B.out
	./part3_2_variant2_A.out $(PORT) $(INPUT) $(ITERS) & \
	SERVER_PID=$$!; \
	sleep 0.1; \
	./part3_2_variant2_B.out $(IP) $(PORT) output_part3_2_variant2.ppm; \
	wait $$SERVER_PID

clean:
	rm -f *.out output_*.ppm

.PHONY: all clean part1 \
        part2_1_variant1 part2_1_variant2 \
        part2_2_variant1 part2_2_variant2 \
        part2_3 part2_3_variant1 part2_3_variant2 \
        part3_1_variant1_A part3_1_variant1_B part3_1_variant2_A part3_1_variant2_B \
        part3_2_variant1_A part3_2_variant1_B part3_2_variant2_A part3_2_variant2_B \
        test_part3_1_variant1 test_part3_1_variant2 test_part3_2_variant1 test_part3_2_variant2

