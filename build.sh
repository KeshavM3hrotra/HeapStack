#!/bin/sh
# ============================================================================
#  HeapTrace - build script for Linux, macOS, WSL and Git Bash / MSYS2
#  Usage:  ./build.sh            (override compilers with CXX=... CC=...)
#  Output: bin/heaptrace_cpp   (C++ implementation, the main project)
#          bin/heaptrace_c     (C implementation, same behaviour)
# ============================================================================
set -e
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
CC="${CC:-gcc}"

for tool in "$CXX" "$CC"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: '$tool' was not found. Install a C/C++ compiler (see README.md, section Prerequisites)." >&2
        exit 1
    fi
done

mkdir -p bin

CPP_SRC="cpp/src/AllocationStrategy.cpp cpp/src/CommandProcessor.cpp cpp/src/FragmentationReporter.cpp
         cpp/src/HeapSimulator.cpp cpp/src/MemoryBlock.cpp cpp/src/SessionLog.cpp cpp/src/StrategyComparator.cpp
         cpp/src/TraceGenerator.cpp cpp/src/Util.cpp cpp/src/VirtualMachine.cpp cpp/src/main.cpp"
C_SRC="c/src/compare.c c/src/heap.c c/src/processor.c c/src/report.c c/src/tracegen.c c/src/util.c c/src/vm.c
       c/src/main.c"

echo "[1/2] Building the C++ implementation  -> bin/heaptrace_cpp"
# shellcheck disable=SC2086
"$CXX" -std=c++11 -O2 -Wall -Wextra -Icpp/include $CPP_SRC -o bin/heaptrace_cpp

echo "[2/2] Building the C implementation    -> bin/heaptrace_c"
# shellcheck disable=SC2086
"$CC" -std=c99 -O2 -Wall -Wextra -Ic/include $C_SRC -o bin/heaptrace_c

echo
echo "Build finished. Try:"
echo "    ./bin/heaptrace_cpp compare traces/worked_example.txt"
echo "    ./bin/heaptrace_c   compare traces/worked_example.txt"
