# HeapTrace

**A First-Fit vs. Best-Fit Memory Allocator Simulator for Single-Server Cloud VM Placement**

Team HeapTrace (DSCPP-III-2026-T347) · Data Structures & OOP with C++ (PBL) · Graphic Era (Deemed to be University), Dehradun · Session 2026-27
Keshav Mehrotra (Team Lead) · Dhiraj Sah · Keshav Dabiyan · Mentor: Mr. Manish Mehta

When a program calls `malloc()` or `new`, an allocator silently decides *which* free region to hand out. HeapTrace makes
that decision visible. A cloud server's memory is the heap, **creating a VM is an allocation, terminating it is a release**,
and the same request trace can be replayed under **first-fit, best-fit and worst-fit** to see which VMs get rejected and why:
often a VM is refused although *enough memory is free in total* — only no single free region is large enough
(external fragmentation).

The project is delivered in **two languages**, with identical behaviour:

| | Language | Role |
|---|---|---|
| `cpp/` | **C++11** | The main project, as specified in the Phase-II report: classes, inheritance, run-time polymorphism (`AllocationStrategy`), `fstream` log, hand-written linked list |
| `c/` | **C99** | A second, independent implementation of the same design (struct + function-pointer "vtable" instead of classes). It prints byte-for-byte the same output, so the test scripts use it as a cross-check of the C++ version |

---

## 1. What is in this folder

```
HeapTrace/
├── README.md                this file
├── build.bat / build.sh     build both programs (Windows / Linux, macOS, Git Bash)
├── run_tests.bat / .sh      build + unit tests + C-vs-C++ cross-check (one command, exit code 0 = all good)
├── Makefile                 optional: make / make test / make clean
├── cpp/
│   ├── include/             MemoryBlock, AllocationStrategy, HeapSimulator, VirtualMachine (+VMManager),
│   │                        SessionLog, FragmentationReporter, CommandProcessor, StrategyComparator, TraceGenerator
│   ├── src/                 implementations + main.cpp (command-line front end)
│   └── tests/               test_heaptrace.cpp (447 checks)
├── c/
│   ├── include/heaptrace.h  the whole C API in one header
│   ├── src/                 heap.c vm.c report.c processor.c compare.c tracegen.c util.c main.c
│   └── tests/               test_heaptrace.c (560 checks) + alloc_track.* (leak detector)
├── traces/                  ready-made request traces (see section 5)
├── docs/DESIGN.md           UML class diagram, algorithms, design decisions, limitations, viva questions
└── bin/                     build output goes here
```

---

## 2. Prerequisites — you need a C and a C++ compiler (g++ / gcc)

Check first: open a terminal and run `g++ --version` and `gcc --version`. If both print a version, skip to section 3.

**Windows 10/11** (MinGW-w64 via MSYS2 — one-time setup):
1. Install MSYS2 from <https://www.msys2.org> (default folder `C:\msys64`).
2. Open **"MSYS2 UCRT64"** from the Start menu and run: `pacman -S --needed mingw-w64-ucrt-x86_64-gcc`
3. Add `C:\msys64\ucrt64\bin` to your Windows **PATH** (Start → "Edit the system environment variables" → Environment Variables → Path → New).
4. Open a **new** Command Prompt or PowerShell and check `g++ --version`.
(Any other MinGW-w64 distribution, e.g. w64devkit or WinLibs, works too. VS Code with the C/C++ extension needs the same g++.)

**Ubuntu / Debian / WSL:** `sudo apt update && sudo apt install build-essential`
**macOS:** `xcode-select --install` (provides clang as `g++` / `gcc`)
**Fedora:** `sudo dnf install gcc gcc-c++ make`

---

## 3. Build and verify (about 20 seconds)

### Windows (Command Prompt or PowerShell, inside the `HeapTrace` folder)
```
.\build.bat
.\run_tests.bat
```
(Type the `.\` — some Windows setups do not run scripts from the current folder otherwise.)

### Linux / macOS / WSL / Git Bash
```
chmod +x build.sh run_tests.sh
./build.sh
./run_tests.sh
```

`build` creates `bin/heaptrace_cpp(.exe)` and `bin/heaptrace_c(.exe)`.
`run_tests` must end with **`ALL VERIFICATION STEPS PASSED`**; it runs the C++ tests, the C tests (both with a memory-leak
detector) and 130 side-by-side comparisons of the C and C++ programs.

No `make`? Not needed. If you do have it: `make`, `make test`, `make clean`.

---

## 4. Run it

All commands work identically for the C program — just replace `heaptrace_cpp` with `heaptrace_c`.
Windows: `bin\heaptrace_cpp.exe …` · Linux/macOS: `./bin/heaptrace_cpp …`

| Command | What it does |
|---|---|
| `heaptrace_cpp` or `heaptrace_cpp repl` | interactive console (type `HELP`) |
| `heaptrace_cpp run <trace> [--strategy first\|best\|worst] [--brief]` | replay a trace under **one** strategy, printing every operation and the fragmentation statistics after it |
| `heaptrace_cpp compare <trace>` | replay the **same** trace under first-, best- and worst-fit and print a comparison table |
| `heaptrace_cpp gen --heap 2000 --ops 300 --seed 7` | print a reproducible pseudo-random trace (redirect to a file with `>`) |
| `heaptrace_cpp help` | usage |

Common options: `--heap <MB>` (default 1000; a `HEAP` line inside a trace overrides it), `--log <file>` (default
`heaptrace_session.log`), `--no-log`.

### The demo from the Phase-II presentation (slides 10–11)

```
bin\heaptrace_cpp.exe compare traces\worked_example.txt
```

```
=== Strategy comparison: traces\worked_example.txt ===
Heap size: 1000 MB

Strategy   Placed  Rejected  (by frag)  FreeBlks  LargestFree  TotalFree  FinalFrag  AvgFrag  PeakFrag
first-fit       5         1          1         3          250        570      56.1%    27.2%     60.0%
best-fit        6         0          0         3          250        280      10.7%    20.4%     60.0%
worst-fit       5         1          1         3          250        570      56.1%    27.2%     60.0%

Rejected VMs:
  first-fit  F(290 MB)
  best-fit   none
  worst-fit  F(290 MB)
…
Fewest rejected VMs: best-fit  (result for this trace only, not a general ranking)
```

To see exactly the numbers on slide 11 ("state before F": first-fit 56.1 %, best-fit 47.4 %), run the shorter trace:

```
bin\heaptrace_cpp.exe compare traces\worked_example_before_F.txt
bin\heaptrace_cpp.exe run traces\worked_example.txt --strategy first
```
The second command shows `CREATE F 290 MB` being **[REJECTED] … largest free 250 MB, total free 570 MB: external fragmentation**
under first-fit; run it with `--strategy best` and F is placed at `[0, 290)`.

### Interactive console

```
> bin\heaptrace_cpp.exe repl --heap 300
heaptrace> create A 100
[OK]       CREATE A 100 MB -> placed at [0, 100)
           -> used=100 MB, free=200 MB, free blocks=1, largest free=200 MB, external fragmentation=0.0%
heaptrace> create B 50
heaptrace> terminate A
[OK]       TERMINATE A -> released 100 MB at [0, 100)
heaptrace> strategy best
heaptrace> snapshot
heaptrace> compare          <- replays everything typed so far under all three strategies
heaptrace> quit
```

### Command language (same for trace files and the console)

| Command | Meaning |
|---|---|
| `HEAP <size_mb>` | (re)initialise the server memory; discards all VMs |
| `CREATE <name> <size_mb>` | create a VM = allocate a block (placed, or rejected and logged) |
| `TERMINATE <name>` | terminate a VM = free its block and merge it with free neighbours |
| `STRATEGY first\|best\|worst` | switch the placement policy at run time |
| `SNAPSHOT`, `STATS` | print the heap block by block / the fragmentation statistics |
| `COMPARE`, `HELP`, `QUIT` | compare strategies on the session so far / help / leave |

Rules: one command per line; `#` starts a comment; commands are case-insensitive, VM names are case-sensitive
(1–32 characters of `A-Z a-z 0-9 _ - .`, not starting with `-` or `.`); sizes are whole MB from 1 to 10¹².
A malformed line is reported as `[ERROR] line N: …`, skipped, and never changes the heap. Terminating a VM whose creation
was rejected is reported as `[SKIPPED]`. Windows line endings (CRLF) and a UTF-8 BOM are accepted.

### Memory map legend
`map |EEEEEEEEEEE.......BBBBBB…|` — 60 characters, each is a slice of the heap: the first letter of the owning VM's name,
or `.` for free memory.

### Session log
Every run writes `heaptrace_session.log` (in the folder you ran it from): a header, one line per operation with its outcome
and statistics, snapshots on request, and the final state. Use `--log other.log` to change the file or `--no-log` to disable it.

---

## 5. Included traces

| File | Purpose |
|---|---|
| `worked_example.txt` | the slide-10/11 scenario incl. the request F = 290 MB that first-fit rejects and best-fit places |
| `worked_example_before_F.txt` | same, stopping before F: reproduces the table on slide 11 (56.1 % vs 47.4 %) |
| `textbook_example.txt` | classic holes of 100/500/200/300/600 MB and requests 212/417/112/426 MB: best-fit places all, first- and worst-fit reject the last |
| `cloud_workload.txt` | a mixed web/db/cache/batch/ML workload on an 8 GB host |
| `edge_cases.txt` | every error path: duplicate names, oversize requests, exact fit, bad numbers, unknown commands, … |
| *generated* | `heaptrace_cpp gen --heap 2000 --ops 300 --seed 7 > my_trace.txt`, then `compare my_trace.txt` |

**A caution about reading results:** none of the three strategies wins on every trace. On `worked_example.txt` and
`textbook_example.txt` best-fit is better, but those are hand-built to show a difference. As a sanity check we replayed
60 generated traces (seeds 1–60) for each of four settings (heap 2000 / 10000 MB × 300 / 1000 operations) — a synthetic
workload (sizes uniform in 1..heap/10, 50 % creates), so treat it as an illustration, not a finding:

| Mean over 60 seeds | rejected VMs (first / best / worst) | average fragmentation (first / best / worst) |
|---|---|---|
| heap 2000, 300 ops | 10.4 / 9.6 / 11.6 | 28.9 % / 27.4 % / 45.0 % |
| heap 2000, 1000 ops | 80.7 / 77.5 / 86.8 | 41.0 % / 39.0 % / 55.6 % |
| heap 10000, 300 ops | 9.6 / 9.2 / 11.4 | 28.7 % / 27.0 % / 45.2 % |
| heap 10000, 1000 ops | 78.2 / 75.5 / 85.5 | 41.0 % / 38.9 % / 55.6 % |

Best-fit rejected fewer VMs than first-fit in 20–39 of the 60 seeds, but first-fit won in 6–18 of them, so the gap
(about 3–4 %) is small and trace-dependent; worst-fit was clearly worst. That is the same direction as the
literature (e.g. Wilson et al., *Dynamic Storage Allocation: A Survey and Critical Review*, 1995). For Phase-III, do
this properly: choose workloads that are justified for VM placement, report averages with spread, and say what the
workload assumptions are.

---

## 6. How it is verified

`run_tests.bat` / `run_tests.sh` run:

* **C++ unit tests (447 checks)** and **C unit tests (560 checks)**: split on allocate, exact fit, coalescing left / right /
  both / none, all three strategies on one fixed layout, tie-breaking, run-time strategy switch, rejection reasons
  (fragmentation vs. capacity), VM lifecycle, every malformed-input path, the slide-11 numbers, session-log content, and a
  **30 000-operation randomised stress test per strategy that re-validates the full list invariants after every single operation**
  (blocks tile `[0, total)` with no gap/overlap, no two adjacent free blocks, links consistent, memory accounting exact).
* **Leak detection:** every `new`/`delete` (C++) and `malloc`/`free` (C) is counted; the tests fail if anything is left over.
* **C ↔ C++ cross-check:** 130 runs (all traces × strategies, plus generated traces) must produce byte-identical output.

The tests themselves were checked by deliberately breaking the code (best-fit inverted, merge-with-previous removed,
merged node leaked, wrong split size, wrong fragmentation formula) — each bug was detected.

Built warning-free with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` (g++ 15.2 / gcc 15.2, MinGW-w64, Windows 11).
**Not covered:** the scripts were exercised on Windows (cmd.exe/PowerShell and Git Bash); I could not run them on a real
Linux or macOS machine here, and AddressSanitizer/Valgrind were unavailable, so the leak detector above is the memory check.

---

## 7. Troubleshooting

| Problem | Fix |
|---|---|
| `'g++' is not recognized` / `g++: command not found` | compiler not installed or not on PATH — section 2; open a **new** terminal afterwards |
| `build.bat` is "not recognized" | type `.\build.bat` |
| `error: cannot open trace file` | wrong path — run from the `HeapTrace` folder or give the full path to the trace |
| `warning: cannot open log file` | the folder is read-only; use `--log <writable path>` or `--no-log` (the simulation still runs) |
| Output looks garbled in the console | the program only prints plain ASCII; use a monospaced font so the memory map lines up |
| Linux/macOS: `Permission denied` on `./build.sh` | `chmod +x build.sh run_tests.sh` |

See `docs/DESIGN.md` for the architecture, algorithms, design decisions and known limitations.
