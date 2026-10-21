#include "VirtualMachine.h"

namespace heaptrace {

VirtualMachine* VMManager::latestByName(const std::string& name) {
    for (std::size_t i = vms_.size(); i > 0; --i) {
        if (vms_[i - 1].name() == name) return &vms_[i - 1];
    }
    return 0;
}

CreateResult VMManager::createVM(const std::string& name, MemSize memory) {
    CreateResult result;
    const VirtualMachine* existing = latestByName(name);
    if (existing != 0 && existing->state() == VMState::Running) {
        result.duplicate = true;
        return result;
    }

    const int id = static_cast<int>(vms_.size());
    vms_.push_back(VirtualMachine(id, name, memory));
    result.id = id;
    result.alloc = heap_.allocate(id, memory);
    if (result.alloc.success) {
        vms_[static_cast<std::size_t>(id)].markRunning(result.alloc.start);
    } else {
        vms_[static_cast<std::size_t>(id)].markRejected();
    }
    return result;
}

TerminateResult VMManager::terminateVM(const std::string& name) {
    TerminateResult result;
    VirtualMachine* vm = latestByName(name);
    if (vm == 0) return result;  // NotFound

    result.id = vm->id();
    if (vm->state() == VMState::Rejected) {
        result.status = TerminateStatus::WasRejected;
    } else if (vm->state() == VMState::Running) {
        result.release = heap_.release(vm->id());
        if (result.release.found) {
            vm->markTerminated();
            result.status = TerminateStatus::Terminated;
        }
    }
    return result;  // already-terminated VMs stay NotFound
}

const VirtualMachine* VMManager::findById(int id) const {
    if (id < 0 || static_cast<std::size_t>(id) >= vms_.size()) return 0;
    return &vms_[static_cast<std::size_t>(id)];
}

int VMManager::runningCount() const {
    int n = 0;
    for (std::size_t i = 0; i < vms_.size(); ++i) {
        if (vms_[i].state() == VMState::Running) ++n;
    }
    return n;
}

}  // namespace heaptrace
