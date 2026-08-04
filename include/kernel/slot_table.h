#ifndef KERNEL_SLOT_TABLE_H
#define KERNEL_SLOT_TABLE_H

/* K3 of docs/kernel-cxx-port.md.
   ---------------------------------------------------------------------
   src/capability.c's per-process capability table turned out, on
   inspection, NOT to be the same shape as src/ramfs.c's file table (K1's
   kernel::bounded_table<T,N>): capability.c addresses slots by a decoded
   (pid, slot, generation) handle, not by name, and needs the generation
   counter bounded_table has no concept of (it exists specifically to make
   a closed-then-reopened slot's OLD handle stop resolving, even though
   the slot itself is now in use again for something else -- ramfs has no
   equivalent: two different ramfs_open calls for the same name always
   resolve to the same object_id for as long as that name exists, there is
   no "stale handle" case to guard against).

   Rather than force capability.c onto bounded_table (which would mean
   either faking up per-slot names nobody needs, or stripping bounded_table
   of the name concept until it stops being bounded_table), this is a
   second, genuinely different generic: a fixed-capacity slot allocator
   with a generation counter per slot, addressed by index instead of name.
   The real duplication this removes is the close-logic that was
   hand-copied between capability_close and capabilities_close_all in the
   original C (bump generation, clear the slot, hand back what was stored
   so the caller can run its service-specific cleanup) -- collapsing that
   into release()/take() below is the concrete templates+RAII-shaped win
   for this subsystem, not a forced reuse of K1's type.

   Same freestanding constraints as every other kernel C++ header here: no
   exceptions, no RTTI, no libstdc++. */

#include <stddef.h>
#include <stdint.h>

namespace kernel {

template <typename T, size_t Capacity>
class slot_table {
public:
    slot_table() : entries_{} {}

    // Claims the first free slot. On success, *slot and *generation
    // identify it (generation is never 0, so a caller can pack
    // (pid, slot, generation) into a handle the same way capability.c's
    // make_handle already does and treat 0 as "never a valid handle"), and
    // *payload points at the freshly default-constructed payload for the
    // caller to fill in -- same "allocate then the caller populates
    // fields" split allocate_slot already has in the original C.
    bool allocate(uint32_t *slot, uint16_t *generation, T **payload) {
        for (uint32_t i = 0; i < Capacity; ++i) {
            entry &candidate = entries_[i];
            if (candidate.open) continue;
            candidate.open = true;
            candidate.payload = T();
            *slot = i;
            *generation = candidate.generation;
            *payload = &candidate.payload;
            return true;
        }
        return false;
    }

    // Handle-checked lookup: both the slot AND its generation must match,
    // so a handle from a since-closed-and-reopened slot is correctly
    // rejected even though the slot index itself is in use again.
    T *resolve(uint32_t slot, uint16_t generation) {
        if (slot >= Capacity) return nullptr;
        entry &candidate = entries_[slot];
        if (!candidate.open || candidate.generation != generation) return nullptr;
        return &candidate.payload;
    }

    // Index-only lookup, no generation check -- for operations that
    // already know the slot is theirs by construction (capabilities_close_all
    // iterating every slot a process owns) rather than by an untrusted
    // caller-supplied handle. Deliberately separate from resolve() so a
    // real handle-validation call site can never accidentally use the
    // unchecked path by mistake.
    T *peek(uint32_t slot) {
        if (slot >= Capacity) return nullptr;
        entry &candidate = entries_[slot];
        return candidate.open ? &candidate.payload : nullptr;
    }

    bool is_open(uint32_t slot) const {
        return slot < Capacity && entries_[slot].open;
    }

    uint16_t generation_of(uint32_t slot) const {
        return slot < Capacity ? entries_[slot].generation : 0u;
    }

    // Frees the slot and bumps its generation (skipping 0, which is
    // reserved as "never valid") so any handle still holding the old
    // generation stops resolving. Hands back a copy of the payload the
    // slot held so the caller can run service-specific cleanup (e.g.
    // capability.c's surface_release for a closed CAPABILITY_SERVICE_SURFACE
    // handle) after the slot itself is already gone -- exactly what both
    // capability_close and capabilities_close_all needed to do, now in one
    // place instead of two.
    bool release(uint32_t slot, T *released_payload_out) {
        if (slot >= Capacity) return false;
        entry &candidate = entries_[slot];
        if (!candidate.open) return false;
        if (released_payload_out != nullptr) *released_payload_out = candidate.payload;
        candidate.open = false;
        candidate.payload = T();
        ++candidate.generation;
        if (candidate.generation == 0u) candidate.generation = 1u;
        return true;
    }

private:
    struct entry {
        bool open = false;
        uint16_t generation = 1u;
        T payload{};
    };

    entry entries_[Capacity];
};

}  // namespace kernel

#endif
