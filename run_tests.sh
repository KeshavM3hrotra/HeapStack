#!/bin/sh
# ============================================================================
#  HeapTrace - full verification (Linux, macOS, WSL, Git Bash / MSYS2)
#    1. builds both programs
#    2. runs the C++ unit tests   (with allocation tracking)
#    3. runs the C unit tests     (with allocation tracking)
#    4. cross-checks: the C and C++ programs must print IDENTICAL output for
#       every trace in traces/ and for generated random traces
#  Exit code 0 = everything passed.
# ============================================================================
cd "$(dirname "$0")" || exit 1
ROOT="$(pwd)"
CXX="${CXX:-g++}"
CC="${CC:-gcc}"
failed=0

# MinGW needs static linking so the C++ allocation tracker sees every allocation.
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) STATIC="-static" ;;
    *) STATIC="" ;;
esac

./build.sh || exit 1
mkdir -p bin/tmp bin/obj
rm -f bin/obj/*.o

CPP_LIB="cpp/src/AllocationStrategy.cpp cpp/src/CommandProcessor.cpp cpp/src/FragmentationReporter.cpp
         cpp/src/HeapSimulator.cpp cpp/src/MemoryBlock.cpp cpp/src/SessionLog.cpp cpp/src/StrategyComparator.cpp
         cpp/src/TraceGenerator.cpp cpp/src/Util.cpp cpp/src/VirtualMachine.cpp"
C_LIB="c/src/compare.c c/src/heap.c c/src/processor.c c/src/report.c c/src/tracegen.c c/src/util.c c/src/vm.c"

echo
echo "=== C++ unit tests ==="
# shellcheck disable=SC2086
"$CXX" -std=c++11 -O2 -Wall -Wextra -Icpp/include $CPP_LIB cpp/tests/test_heaptrace.cpp $STATIC -o bin/test_cpp || exit 1
(cd bin/tmp && "$ROOT/bin/test_cpp") || failed=1

echo
echo "=== C unit tests ==="
for f in $C_LIB c/tests/test_heaptrace.c; do
    "$CC" -std=c99 -O2 -Wall -Wextra -DHT_ALLOC_TRACKING -include c/tests/alloc_track.h -Ic/include \
        -c "$f" -o "bin/obj/$(basename "$f" .c).o" || exit 1
done
"$CC" -std=c99 -O2 -c c/tests/alloc_track.c -o bin/obj/alloc_track.o || exit 1
"$CC" bin/obj/*.o -lm -o bin/test_c || exit 1
(cd bin/tmp && "$ROOT/bin/test_c") || failed=1

echo
echo "=== Cross-check: C output must equal C++ output ==="
CPP=bin/heaptrace_cpp
C=bin/heaptrace_c
total=0
mism=0
same() {  # same <args...>: both programs, same arguments, outputs must be identical
    total=$((total + 1))
    $CPP "$@" > bin/tmp/out_cpp.txt 2>&1
    $C "$@" > bin/tmp/out_c.txt 2>&1
    if ! cmp -s bin/tmp/out_cpp.txt bin/tmp/out_c.txt; then
        mism=$((mism + 1))
        echo "  MISMATCH: $*"
        diff bin/tmp/out_cpp.txt bin/tmp/out_c.txt | head -5
    fi
}
for t in traces/*.txt; do
    for s in first best worst; do same run "$t" --strategy "$s" --no-log; done
    same run "$t" --brief --no-log
    same compare "$t" --no-log
done
for seed in 1 2 3 4 5 99 12345; do
    for heap in 1 7 100 2000 100000; do
        same gen --heap "$heap" --ops 300 --seed "$seed"
        $CPP gen --heap "$heap" --ops 300 --seed "$seed" > "bin/tmp/gen.txt"
        same compare bin/tmp/gen.txt --no-log
        same run bin/tmp/gen.txt --strategy best --no-log
    done
done
echo "  $total comparisons, $mism mismatches"
[ "$mism" -eq 0 ] || failed=1

echo
if [ "$failed" -eq 0 ]; then
    echo "ALL VERIFICATION STEPS PASSED"
else
    echo "VERIFICATION FAILED"
fi
exit "$failed"
