# HeapTrace - Makefile (GNU make; on Windows use mingw32-make from MinGW-w64)
#   make          build both programs into bin/
#   make cpp      build only the C++ implementation
#   make c        build only the C implementation
#   make test     build + run the full verification (run_tests.sh / run_tests.bat)
#   make clean    remove build output
# If you do not have make, use build.bat (Windows) or build.sh (Linux/macOS) instead.

CXX ?= g++
CC  ?= gcc
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra
CFLAGS   ?= -std=c99 -O2 -Wall -Wextra

CPP_SRC := cpp/src/AllocationStrategy.cpp cpp/src/CommandProcessor.cpp cpp/src/FragmentationReporter.cpp \
           cpp/src/HeapSimulator.cpp cpp/src/MemoryBlock.cpp cpp/src/SessionLog.cpp \
           cpp/src/StrategyComparator.cpp cpp/src/TraceGenerator.cpp cpp/src/Util.cpp \
           cpp/src/VirtualMachine.cpp cpp/src/main.cpp
C_SRC   := c/src/compare.c c/src/heap.c c/src/processor.c c/src/report.c c/src/tracegen.c c/src/util.c \
           c/src/vm.c c/src/main.c

ifeq ($(OS),Windows_NT)
  EXE := .exe
  TESTCMD := run_tests.bat
else
  EXE :=
  TESTCMD := ./run_tests.sh
endif

CPP_BIN := bin/heaptrace_cpp$(EXE)
C_BIN   := bin/heaptrace_c$(EXE)

.PHONY: all cpp c test clean
all: cpp c
cpp: $(CPP_BIN)
c: $(C_BIN)

$(CPP_BIN): $(CPP_SRC) $(wildcard cpp/include/*.h)
	$(CXX) $(CXXFLAGS) -Icpp/include $(CPP_SRC) -o $@

$(C_BIN): $(C_SRC) $(wildcard c/include/*.h)
	$(CC) $(CFLAGS) -Ic/include $(C_SRC) -o $@

test:
	$(TESTCMD)

clean:
ifeq ($(OS),Windows_NT)
	-cmd /c "if exist bin\*.exe del /q bin\*.exe"
	-cmd /c "if exist bin\obj rmdir /s /q bin\obj"
	-cmd /c "if exist bin\tmp rmdir /s /q bin\tmp"
else
	rm -rf bin/heaptrace_cpp bin/heaptrace_c bin/test_cpp bin/test_c bin/obj bin/tmp
endif
