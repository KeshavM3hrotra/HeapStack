@echo off
rem ===========================================================================
rem  HeapTrace - full verification on Windows (needs g++ / gcc from MinGW-w64)
rem    1. builds both programs
rem    2. runs the C++ unit tests (with allocation tracking)
rem    3. runs the C unit tests   (with allocation tracking)
rem    4. cross-check: C and C++ programs must print IDENTICAL output for every
rem       trace in traces\ and for generated random traces
rem  Exit code 0 = everything passed.
rem ===========================================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set FAILED=0
set TOTAL=0
set MISMATCH=0
set TMPD=bin\tmp

call "%~dp0build.bat"
if errorlevel 1 exit /b 1
if not exist bin\tmp mkdir bin\tmp
if not exist bin\obj mkdir bin\obj
del /q bin\obj\*.o >nul 2>nul

set CPP_LIB=cpp/src/AllocationStrategy.cpp cpp/src/CommandProcessor.cpp cpp/src/FragmentationReporter.cpp cpp/src/HeapSimulator.cpp cpp/src/MemoryBlock.cpp cpp/src/SessionLog.cpp cpp/src/StrategyComparator.cpp cpp/src/TraceGenerator.cpp cpp/src/Util.cpp cpp/src/VirtualMachine.cpp

echo.
echo === C++ unit tests ===
g++ -std=c++11 -O2 -Wall -Wextra -Icpp/include %CPP_LIB% cpp/tests/test_heaptrace.cpp -static -o bin/test_cpp.exe
if errorlevel 1 exit /b 1
pushd bin\tmp
..\test_cpp.exe
if errorlevel 1 set FAILED=1
popd

echo.
echo === C unit tests ===
for %%f in (compare heap processor report tracegen util vm) do (
    gcc -std=c99 -O2 -Wall -Wextra -DHT_ALLOC_TRACKING -include c/tests/alloc_track.h -Ic/include -c c/src/%%f.c -o bin/obj/%%f.o
    if errorlevel 1 exit /b 1
)
gcc -std=c99 -O2 -Wall -Wextra -DHT_ALLOC_TRACKING -include c/tests/alloc_track.h -Ic/include -c c/tests/test_heaptrace.c -o bin/obj/test_heaptrace.o
if errorlevel 1 exit /b 1
gcc -std=c99 -O2 -c c/tests/alloc_track.c -o bin/obj/alloc_track.o
if errorlevel 1 exit /b 1
gcc bin/obj/compare.o bin/obj/heap.o bin/obj/processor.o bin/obj/report.o bin/obj/tracegen.o bin/obj/util.o bin/obj/vm.o bin/obj/test_heaptrace.o bin/obj/alloc_track.o -lm -o bin/test_c.exe
if errorlevel 1 exit /b 1
pushd bin\tmp
..\test_c.exe
if errorlevel 1 set FAILED=1
popd

echo.
echo === Cross-check: C output must equal C++ output ===
for %%t in (traces\*.txt) do (
    for %%s in (first best worst) do call :same run %%t --strategy %%s --no-log
    call :same run %%t --brief --no-log
    call :same compare %%t --no-log
)
for %%s in (1 2 3 4 5 99 12345) do (
    for %%h in (1 7 100 2000 100000) do (
        call :same gen --heap %%h --ops 300 --seed %%s
        bin\heaptrace_cpp.exe gen --heap %%h --ops 300 --seed %%s > %TMPD%\gen.txt
        call :same compare %TMPD%\gen.txt --no-log
        call :same run %TMPD%\gen.txt --strategy best --no-log
    )
)
echo   !TOTAL! comparisons, !MISMATCH! mismatches
if not "!MISMATCH!"=="0" set FAILED=1

echo.
if "!FAILED!"=="0" (
    echo ALL VERIFICATION STEPS PASSED
    exit /b 0
)
echo VERIFICATION FAILED
exit /b 1

:same
rem Runs both programs with the same arguments and requires identical output.
set /a TOTAL+=1
bin\heaptrace_cpp.exe %* > %TMPD%\out_cpp.txt 2>&1
bin\heaptrace_c.exe %* > %TMPD%\out_c.txt 2>&1
fc /b %TMPD%\out_cpp.txt %TMPD%\out_c.txt >nul
if errorlevel 1 (
    set /a MISMATCH+=1
    echo   MISMATCH: %*
)
exit /b 0
