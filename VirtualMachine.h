// VirtualMachine.h - the VM Manager layer (new in Phase-II).
//
// Creating a VM is an allocation, terminating it is a release. VMManager keeps a
// record of every request (running, terminated or rejected) and talks to the
// HeapSimulator on the VMs' behalf.
#ifndef HEAPTRACE_VIRTUALMACHINE_H
#define HEAPTRACE_VIRTUALMACHINE_H

#include "HeapSimulator.h"

#include <string>
#include <vector>

namespace heaptrace {

enum class VMState { Running, Terminated, Rejected };

class VirtualMachine {
public:
    VirtualMachine(int id, const std::string& name, MemSize memory)
        : id_(id), name_(name), memory_(memory), state_(VMState::Rejected), start_(-1) {}

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    MemSize memory() const { return memory_; }
    VMState state() const { return state_; }
    MemSize start() const { return start_; }  // valid while Running

    void markRunning(MemSize start) { state_ = VMState::Running; start_ = start; }
    void markTerminated() { state_ = VMState::Terminated; }
    void markRejected() { state_ = VMState::Rejected; start_ = -1; }

private:
    int id_;
    std::string name_;
    MemSize memory_;
    VMState state_;
    MemSize start_;
};

struct CreateResult {
    bool duplicate;        // a VM with this name is already running (nothing was done)
    int id;                // id of the new request, -1 if duplicate
    AllocationResult alloc;
    CreateResult() : duplicate(false), id(-1) {}
};

enum class TerminateStatus {
    Terminated,   // the VM was running and its block was released
    WasRejected,  // the VM's creation had been rejected earlier: nothing to release
    NotFound      // no such VM, or it was already terminated
};

struct TerminateResult {
    TerminateStatus status;
    int id;
    ReleaseResult release;
    TerminateResult() : status(TerminateStatus::NotFound), id(-1) {}
};

class VMManager {
public:
    // The heap must outlive the manager.
    explicit VMManager(HeapSimulator& heap) : heap_(heap) {}

    CreateResult createVM(const std::string& name, MemSize memory);
    TerminateResult terminateVM(const std::string& name);

    const VirtualMachine* findById(int id) const;
    int runningCount() const;
    std::size_t requestCount() const { return vms_.size(); }

private:
    // Most recent request with this name, or null.
    VirtualMachine* latestByName(const std::string& name);

    HeapSimulator& heap_;
    std::vector<VirtualMachine> vms_;  // index == VM id
};

}  // namespace heaptrace

#endif
