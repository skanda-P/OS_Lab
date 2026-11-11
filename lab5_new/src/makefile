CXX ?= g++
CXXFLAGS ?= -O3 -Wall -Wextra -std=c++17 -pthread -Icommon

IMG_DIR ?= images
IMAGE   ?= 1
INPUT   ?= $(IMG_DIR)/$(IMAGE).ppm
ITERS ?= 1000
PORT  ?= 12345
IP    ?= 127.0.0.1

COMMON_SRCS = common/libppm.cpp
HDRS = $(wildcard common/*.h)

all: baseline_bench.out part1 part2_1_variant1 part2_1_variant2 part2_2_variant1 part2_2_variant2 part2_3


# ----------------- Fair single-thread baseline used for speed-ups -----------------
baseline_bench.out: part1/baseline_bench.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

baseline_variant1: baseline_bench.out
	./baseline_bench.out 1 $(INPUT) output_baseline_variant1.ppm $(ITERS)

baseline_variant2: baseline_bench.out
	./baseline_bench.out 2 $(INPUT) output_baseline_variant2.ppm $(ITERS)

# ----------------- Fault-injection self test -----------------
# Builds Part II programs with one pixel deliberately corrupted in transit and
# checks that the verification detects it (every run below MUST fail).
FAULT_ITERS ?= 2
fault_test: part2_1/part2_1_variant1.cpp part2_2/part2_2_variant1.cpp part2_3/part2_3_variant1.cpp part3_1/part3_1_variant1_A.cpp part3_1/part3_1_variant1_B.cpp $(COMMON_SRCS) $(HDRS)
	@set -e; for t in part2_1/part2_1_variant1 part2_2/part2_2_variant1 part2_3/part2_3_variant1; do \
	  $(CXX) $(CXXFLAGS) -DINJECT_FAULT -o fault.out $$t.cpp $(COMMON_SRCS); \
	  if timeout 60 ./fault.out $(INPUT) output_fault.ppm $(FAULT_ITERS) >/dev/null 2>fault.err; then \
	     echo "FAULT_TEST $$t: NOT DETECTED  (verification is broken!)"; exit 1; \
	  else echo "FAULT_TEST $$t: corruption detected ->" `grep -m1 -o 'FAILURE.*' fault.err`; fi; \
	done
	@$(CXX) $(CXXFLAGS) -DINJECT_FAULT -o fault_A.out part3_1/part3_1_variant1_A.cpp $(COMMON_SRCS)
	@$(CXX) $(CXXFLAGS) -DINJECT_FAULT -o fault_B.out part3_1/part3_1_variant1_B.cpp $(COMMON_SRCS)
	@./fault_A.out 12399 $(INPUT) $(FAULT_ITERS) >/dev/null 2>&1 & A=$$!; sleep 0.3; \
	  if timeout 60 ./fault_B.out 127.0.0.1 12399 output_fault.ppm >/dev/null 2>fault.err; then \
	     echo "FAULT_TEST part3_1: NOT DETECTED (verification is broken!)"; kill $$A 2>/dev/null; exit 1; \
	  else echo "FAULT_TEST part3_1: corruption detected ->" `grep -m1 -o 'FAILURE.*' fault.err`; fi; wait $$A 2>/dev/null || true
	@rm -f fault*.out fault.err output_fault.ppm

# ----------------- Part I -----------------
part1: part1.out
	./part1.out $(INPUT) output_part1.ppm

part1.out: part1/part1.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)


# ----------------- Part II.1 (Threads + Atomic) -----------------
part2_1_variant1: part2_1_variant1.out
	./part2_1_variant1.out $(INPUT) output_part2_1_variant1.ppm $(ITERS)
	@cp -f output_part2_1_variant1.ppm output_part2_2_variant1.ppm 2>/dev/null || true

part2_1_variant1.out: part2_1/part2_1_variant1.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

part2_1_variant2: part2_1_variant2.out
	./part2_1_variant2.out $(INPUT) output_part2_1_variant2.ppm $(ITERS)
	@cp -f output_part2_1_variant2.ppm output_part2_2_variant2.ppm 2>/dev/null || true

part2_1_variant2.out: part2_1/part2_1_variant2.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)


# ----------------- Part II.2 (Processes + Pipes) -----------------
part2_2_variant1: part2_2_variant1.out
	./part2_2_variant1.out $(INPUT) output_part2_2_variant1.ppm $(ITERS)

part2_2_variant1.out: part2_2/part2_2_variant1.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

part2_2_variant2: part2_2_variant2.out
	./part2_2_variant2.out $(INPUT) output_part2_2_variant2.ppm $(ITERS)

part2_2_variant2.out: part2_2/part2_2_variant2.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)


# ----------------- Part II.3 (Processes + SHM + Semaphores) -----------------
part2_3: part2_3_variant1.out part2_3_variant2.out
	./part2_3_variant1.out $(INPUT) output_part2_3_variant1.ppm $(ITERS)
	./part2_3_variant2.out $(INPUT) output_part2_3_variant2.ppm $(ITERS)

part2_3_variant1: part2_3_variant1.out
	./part2_3_variant1.out $(INPUT) output_part2_3_variant1.ppm $(ITERS)

part2_3_variant1.out: part2_3/part2_3_variant1.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

part2_3_variant2: part2_3_variant2.out
	./part2_3_variant2.out $(INPUT) output_part2_3_variant2.ppm $(ITERS)

part2_3_variant2.out: part2_3/part2_3_variant2.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)


# ----------------- Part III.1 (Sockets: A=S1+S2, B=S3) -----------------
part3_1_variant1_A: part3_1_variant1_A.out
	./part3_1_variant1_A.out $(PORT) $(INPUT) $(ITERS)

part3_1_variant1_A.out: part3_1/part3_1_variant1_A.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

part3_1_variant1_B: part3_1_variant1_B.out
	./part3_1_variant1_B.out $(IP) $(PORT) output_part3_1_variant1.ppm

part3_1_variant1_B.out: part3_1/part3_1_variant1_B.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

part3_1_variant2_A: part3_1_variant2_A.out
	./part3_1_variant2_A.out $(PORT) $(INPUT) $(ITERS)

part3_1_variant2_A.out: part3_1/part3_1_variant2_A.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

part3_1_variant2_B: part3_1_variant2_B.out
	./part3_1_variant2_B.out $(IP) $(PORT) output_part3_1_variant2.ppm

part3_1_variant2_B.out: part3_1/part3_1_variant2_B.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)


# ----------------- Part III.2 (Sockets: A=S1, B=S2+S3) -----------------
part3_2_variant1_A: part3_2_variant1_A.out
	./part3_2_variant1_A.out $(PORT) $(INPUT) $(ITERS)

part3_2_variant1_A.out: part3_2/part3_2_variant1_A.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

part3_2_variant1_B: part3_2_variant1_B.out
	./part3_2_variant1_B.out $(IP) $(PORT) output_part3_2_variant1.ppm

part3_2_variant1_B.out: part3_2/part3_2_variant1_B.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

part3_2_variant2_A: part3_2_variant2_A.out
	./part3_2_variant2_A.out $(PORT) $(INPUT) $(ITERS)

part3_2_variant2_A.out: part3_2/part3_2_variant2_A.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

part3_2_variant2_B: part3_2_variant2_B.out
	./part3_2_variant2_B.out $(IP) $(PORT) output_part3_2_variant2.ppm

part3_2_variant2_B.out: part3_2/part3_2_variant2_B.cpp $(COMMON_SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)


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
	rm -f *.out output_*.ppm fault.err

.PHONY: all clean part1 baseline_variant1 baseline_variant2 fault_test \
        part2_1_variant1 part2_1_variant2 \
        part2_2_variant1 part2_2_variant2 \
        part2_3 part2_3_variant1 part2_3_variant2 \
        part3_1_variant1_A part3_1_variant1_B part3_1_variant2_A part3_1_variant2_B \
        part3_2_variant1_A part3_2_variant1_B part3_2_variant2_A part3_2_variant2_B \
        test_part3_1_variant1 test_part3_1_variant2 test_part3_2_variant1 test_part3_2_variant2

