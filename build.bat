@echo off
rem ===========================================================================
rem  HeapTrace - Windows build script (needs g++ and gcc from MinGW-w64 / MSYS2)
rem  Usage:  build.bat
rem  Output: bin\heaptrace_cpp.exe   (C++ implementation, the main project)
rem          bin\heaptrace_c.exe     (C implementation, same behaviour)
rem ===========================================================================
setlocal
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo ERROR: g++ was not found on PATH.
    echo        Install MSYS2 / MinGW-w64 ^(see README.md, section "Prerequisites"^) and open a new terminal.
    exit /b 1
)
where gcc >nul 2>nul
if errorlevel 1 (
    echo ERROR: gcc was not found on PATH ^(it ships together with g++ in MinGW-w64^).
    exit /b 1
)

if not exist bin mkdir bin

echo [1/2] Building the C++ implementation  -^> bin\heaptrace_cpp.exe
g++ -std=c++11 -O2 -Wall -Wextra -Icpp/include ^
    cpp/src/AllocationStrategy.cpp cpp/src/CommandProcessor.cpp cpp/src/FragmentationReporter.cpp ^
    cpp/src/HeapSimulator.cpp cpp/src/MemoryBlock.cpp cpp/src/SessionLog.cpp cpp/src/StrategyComparator.cpp ^
    cpp/src/TraceGenerator.cpp cpp/src/Util.cpp cpp/src/VirtualMachine.cpp cpp/src/main.cpp ^
    -o bin/heaptrace_cpp.exe
if errorlevel 1 (
    echo ERROR: the C++ build failed.
    exit /b 1
)

echo [2/2] Building the C implementation    -^> bin\heaptrace_c.exe
gcc -std=c99 -O2 -Wall -Wextra -Ic/include ^
    c/src/compare.c c/src/heap.c c/src/processor.c c/src/report.c c/src/tracegen.c c/src/util.c c/src/vm.c ^
    c/src/main.c -o bin/heaptrace_c.exe
if errorlevel 1 (
    echo ERROR: the C build failed.
    exit /b 1
)

echo.
echo Build finished. Try:
echo     bin\heaptrace_cpp.exe compare traces\worked_example.txt
echo     bin\heaptrace_c.exe   compare traces\worked_example.txt
exit /b 0
