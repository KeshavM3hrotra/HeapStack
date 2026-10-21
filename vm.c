#include "heaptrace.h"

#include <stdlib.h>
#include <string.h>

VMManager *vm_manager_create(Heap *heap) {
    VMManager *m = (VMManager *)calloc(1, sizeof(VMManager));
    if (m != NULL) m->heap = heap;
    return m;
}

void vm_manager_destroy(VMManager *m) {
    if (m == NULL) return;
    free(m->vms);
    free(m);
}

/* Most recent request with this name, or NULL. */
static VirtualMachine *latest_by_name(VMManager *m, const char *name) {
    size_t i;
    for (i = m->count; i > 0; --i) {
        if (strcmp(m->vms[i - 1].name, name) == 0) return &m->vms[i - 1];
    }
    return NULL;
}

CreateResult vm_create(VMManager *m, const char *name, MemSize memory) {
    CreateResult r;
    VirtualMachine *existing, *vm;
    r.duplicate = 0;
    r.failed = 0;
    r.id = -1;
    r.alloc.success = 0;
    r.alloc.start = -1;
    r.alloc.reason = HT_REJECT_INVALID;

    if (strlen(name) > HT_MAX_NAME) {
        r.failed = 1;
        return r;
    }
    existing = latest_by_name(m, name);
    if (existing != NULL && existing->state == VM_RUNNING) {
        r.duplicate = 1;
        return r;
    }
    if (m->count == m->cap) {
        size_t ncap = m->cap == 0 ? 16 : m->cap * 2;
        VirtualMachine *grown = (VirtualMachine *)realloc(m->vms, ncap * sizeof(VirtualMachine));
        if (grown == NULL) {
            r.failed = 1;
            return r;
        }
        m->vms = grown;
        m->cap = ncap;
    }

    vm = &m->vms[m->count];
    vm->id = (int)m->count;
    strcpy(vm->name, name);
    vm->memory = memory;
    vm->state = VM_REJECTED;
    vm->start = -1;
    ++m->count;

    r.id = vm->id;
    r.alloc = heap_allocate(m->heap, vm->id, memory);
    if (r.alloc.success) {
        vm->state = VM_RUNNING;
        vm->start = r.alloc.start;
    }
    return r;
}

TerminateResult vm_terminate(VMManager *m, const char *name) {
    TerminateResult r;
    VirtualMachine *vm;
    r.status = TERM_NOT_FOUND;
    r.id = -1;
    r.release.found = 0;
    r.release.start = -1;
    r.release.size = 0;
    r.release.merges = 0;

    vm = latest_by_name(m, name);
    if (vm == NULL) return r;

    r.id = vm->id;
    if (vm->state == VM_REJECTED) {
        r.status = TERM_WAS_REJECTED;
    } else if (vm->state == VM_RUNNING) {
        r.release = heap_release(m->heap, vm->id);
        if (r.release.found) {
            vm->state = VM_TERMINATED;
            r.status = TERM_TERMINATED;
        }
    }
    return r; /* already-terminated VMs stay NOT_FOUND */
}

const VirtualMachine *vm_find_by_id(const VMManager *m, int id) {
    if (id < 0 || (size_t)id >= m->count) return NULL;
    return &m->vms[id];
}

int vm_running_count(const VMManager *m) {
    size_t i;
    int n = 0;
    for (i = 0; i < m->count; ++i) {
        if (m->vms[i].state == VM_RUNNING) ++n;
    }
    return n;
}
