# HeapTrace

**A First-Fit vs. Best-Fit Memory Allocator Simulator for Single-Server Cloud VM Placement**

HeapTrace is a C++ console simulator that makes heap-allocator decisions visible. It models one cloud server whose memory is managed as a heap. Each VM you create is an allocation, and each VM you terminate is a release. The simulator lets you compare placement strategies on the same request trace and see how fragmentation builds up.

> Project-Based Learning (PBL), Data Structures & OOP with C++
> Department of Computer Science & Engineering, Graphic Era (Deemed to be University), Dehradun
> Team ID: **DSCPP-III-2026-T347** · Academic Session 2026–27

---

## Why this project?

Every `malloc()` or `new` call depends on an allocator that silently decides where memory goes. Because those decisions are hidden, it is hard to see why an allocation can fail even when plenty of memory is free.

In VM placement, the same problem shows up as a **rejected VM**. The server has enough free memory in total, but no single free region is large enough. HeapTrace reproduces this situation and measures it.

## Features

| Feature | Status |
|---|---|
| Linked-list heap of `MemoryBlock` nodes (no STL container) | ✅ Completed (Phase-I) |
| Block splitting on allocation | ✅ Completed (Phase-I) |
| Coalescing of adjacent free blocks on release | ✅ Completed (Phase-I) |
| Pluggable `AllocationStrategy`: first-fit, best-fit | 🔄 In progress |
| Worst-fit strategy | ⏳ Stretch goal |
| VM create / terminate commands (`VirtualMachine`) | 🔄 In progress |
| `fstream` session log and heap snapshots | 🔄 In progress |
| `FragmentationReporter` and strategy comparison | ⏳ Pending |

## Architecture

```
 Input                 VM Manager           Alloc/Free Engine        Report & Log
 heap size + trace ──► (VirtualMachine) ──► (HeapSimulator)    ──►  (FragmentationReporter,
                                                  ▲                   fstream session log)
                                                  │
                                         AllocationStrategy
                                     first / best / worst-fit
```

| Class | Responsibility |
|---|---|
| `MemoryBlock` | One contiguous region: start address, size, free/used flag, owning VM |
| `HeapSimulator` | Owns the block list; performs allocate (with split) and free (with coalesce) |
| `AllocationStrategy` | Abstract base class that chooses which free block serves a request |
| `FirstFitStrategy` | Picks the first free block that is large enough |
| `BestFitStrategy` | Picks the smallest free block that is large enough |
| `VirtualMachine` | VM ID, name and memory size; create/terminate lifecycle |
| `FragmentationReporter` | Heap snapshots and statistics |

`HeapSimulator` calls the active strategy through a base-class pointer. Because of this, the placement policy can be switched at run time without changing the engine.

## Metrics

For each strategy, run on the same trace, HeapTrace reports these values:

- **Rejected VMs**: requests that could not be placed.
- **Free blocks**: the number of separate free regions.
- **Largest free block**: the biggest single region still available.
- **External fragmentation** = `1 − (largest free block / total free memory)`.

## Worked example (hand-traced)

The server has 1000 MB of memory. Create VMs A = 300, B = 100, C = 200 and D = 150 MB. Then terminate A and C, and create E = 180 MB.

```
First-fit:  [ E 180 | free 120 | B 100 | free 200 | D 150 | free 250 ]
Best-fit:   [ free 300 | B 100 | E 180 | free 20 | D 150 | free 250 ]
```

| Strategy | Total free | Largest free | Ext. fragmentation | Next VM F = 290 MB |
|---|---|---|---|---|
| First-fit | 570 MB | 250 MB | 56.1 % | ❌ Rejected |
| Best-fit | 570 MB | 300 MB | 47.4 % | ✅ Placed |

Both heaps have the same amount of free memory, but first-fit rejects F. The cause is fragmentation, not a lack of capacity.

A single trace cannot show that one strategy is better in general. For example, best-fit tends to leave tiny unusable gaps like the 20 MB one above. Comparisons over multiple traces will be added in Phase-III.

## Build & Run

You need a C++17 compiler (for example g++ 9 or later) and nothing else.

```bash
git clone https://github.com/KeshavM3hrotra/HeapStack.git
cd HeapStack
g++ -std=c++17 -Wall -Wextra -o heaptrace src/*.cpp
./heaptrace
```

> Update the source path above to match the final folder layout once all modules are pushed.

## Project Structure (planned)

```
HeapStack/
├── src/        # .cpp files (engine, strategies, VM manager, reporter)
├── include/    # .h headers
├── traces/     # sample request traces
├── logs/       # session logs and heap snapshots (generated)
└── README.md
```

## Testing

| Test | Expected result | Status |
|---|---|---|
| First-fit placement | The request goes to the first free block that fits | 🔄 Test cases drafted |
| Best-fit placement | The request goes to the smallest free block that fits | 🔄 Test cases drafted |
| Split and coalesce | Oversized blocks are split; free adjacent blocks merge after release | ✅ Pass (Phase-I) |
| Request that does not fit | The request is refused and logged; the heap is unchanged | ⏳ Pending |

## Roadmap

- [x] **Phase-I**: proposal, design, core free-list engine
- [ ] **Phase-II**: strategy layer (12 Oct), VM layer (14 Oct), persistence and edge-case testing (16 Oct)
- [ ] **Phase-III**: fragmentation report and comparison runs (17 Oct), final demo
- [ ] Final README, report and presentation (project ends **19 Oct 2026**)

## Team

| Member | Role | Student ID |
|---|---|---|
| Keshav Mehrotra | Team Lead, Architecture & Strategy Layer | 2510010328 |
| Dhiraj Sah | Split/Coalesce Logic, Persistence & Testing | 2510370545 |
| Keshav Dabiyan | Fragmentation Reporting, Documentation & Demo | 2510012677 |

**Mentor:** Mr. Manish Mehta

## References

1. R. Thareja, *Data Structures Using C*, Oxford University Press.
2. A. Silberschatz, P. B. Galvin, G. Gagne, *Operating System Concepts*, Wiley.
3. D. E. Knuth, *The Art of Computer Programming, Vol. 1*, Sec. 2.5 "Dynamic Storage Allocation", Addison-Wesley.
4. P. R. Wilson, M. S. Johnstone, M. Neely, D. Boles, "Dynamic Storage Allocation: A Survey and Critical Review", *IWMM*, 1995.
