// K0 of docs/kernel-cxx-port.md -- see include/kernel/cxx_runtime.h.
//
// Deliberately the same design as src/cxx_runtime.cpp's userspace allocator
// (first-fit, address-ordered implicit free list, boundary-tag coalescing):
// proven there already, and small enough to re-review here rather than
// shared, since the two runtimes serve different privilege levels and must
// never be linked into the same translation unit.
#include <kernel/cxx_runtime.h>
#include <kernel/serial.h>

#include <stddef.h>
#include <stdint.h>

namespace {

// Kept small and static on purpose, matching every other fixed kernel
// budget in this codebase (RAMFS_STORAGE_MAX, USERSPACE_HEAP_PAGES, ...):
// K0's job is proving the mechanism works, not sizing it for a real
// subsystem. Raise this only when a specific K2/K3 migration needs more,
// with the same scrutiny given to every other kernel memory bound.
constexpr size_t kArenaBytes = 16u * 1024u;

struct block_header {
    size_t size;
    bool used;
    block_header *next;
};

alignas(16) uint8_t arena[kArenaBytes];
block_header *first_block = nullptr;
uint8_t *arena_high_water = arena;

constexpr size_t kAlign = 16u;
constexpr size_t kHeaderSize = (sizeof(block_header) + kAlign - 1u) & ~(kAlign - 1u);

size_t align_up(size_t bytes) { return (bytes + kAlign - 1u) & ~(kAlign - 1u); }

uint8_t *payload_of(block_header *block) {
    return reinterpret_cast<uint8_t *>(block) + kHeaderSize;
}

block_header *header_of(void *payload) {
    return reinterpret_cast<block_header *>(
        reinterpret_cast<uint8_t *>(payload) - kHeaderSize);
}

block_header *extend(size_t payload_size) {
    const size_t needed = kHeaderSize + payload_size;
    if (needed > static_cast<size_t>(arena + kArenaBytes - arena_high_water))
        return nullptr;
    block_header *block = reinterpret_cast<block_header *>(arena_high_water);
    block->size = payload_size;
    block->used = true;
    block->next = nullptr;
    arena_high_water += needed;

    if (first_block == nullptr) {
        first_block = block;
    } else {
        block_header *tail = first_block;
        while (tail->next != nullptr) tail = tail->next;
        tail->next = block;
    }
    return block;
}

void *bump_allocate(size_t bytes) {
    const size_t payload_size = align_up(bytes);

    for (block_header *block = first_block; block != nullptr; block = block->next) {
        if (block->used || block->size < payload_size) continue;
        const size_t leftover = block->size - payload_size;
        if (leftover >= kHeaderSize + kAlign) {
            block_header *remainder = reinterpret_cast<block_header *>(
                payload_of(block) + payload_size);
            remainder->size = leftover - kHeaderSize;
            remainder->used = false;
            remainder->next = block->next;
            block->next = remainder;
            block->size = payload_size;
        }
        block->used = true;
        return payload_of(block);
    }
    block_header *block = extend(payload_size);
    return block == nullptr ? nullptr : payload_of(block);
}

void bump_free(void *payload) {
    if (payload == nullptr) return;
    block_header *block = header_of(payload);
    block->used = false;

    if (block->next != nullptr && !block->next->used) {
        block->size += kHeaderSize + block->next->size;
        block->next = block->next->next;
    }

    for (block_header *prev = first_block; prev != nullptr; prev = prev->next) {
        if (prev->next == block && !prev->used) {
            prev->size += kHeaderSize + block->size;
            prev->next = block->next;
            break;
        }
    }
}

// Self-test fixture: one pure-virtual base and one concrete override, so
// the vtable call and the virtual-destructor call both exercise a real
// indirect call through a base-class pointer rather than a direct one the
// compiler could devirtualize away.
class probe_base {
public:
    virtual ~probe_base() = default;
    virtual uint32_t value() const = 0;
};

class probe_derived : public probe_base {
public:
    explicit probe_derived(uint32_t seed) : seed_(seed) {}
    ~probe_derived() override { destroyed_marker = seed_; }
    uint32_t value() const override { return seed_ * 2u; }

    static uint32_t destroyed_marker;

private:
    uint32_t seed_;
};

uint32_t probe_derived::destroyed_marker = 0u;

}  // namespace

extern "C" size_t kernel_cxx_arena_remaining(void) {
    size_t free_bytes = static_cast<size_t>(arena + kArenaBytes - arena_high_water);
    for (block_header *block = first_block; block != nullptr; block = block->next)
        if (!block->used) free_bytes += block->size;
    return free_bytes;
}

extern "C" bool kernel_cxx_self_test(void) {
    probe_derived::destroyed_marker = 0u;
    probe_base *object = new probe_derived(21u);
    if (object == nullptr) {
        serial_write("DEBUG kernel_cxx alloc-failed\n");
        return false;
    }
    const void *first_address = static_cast<const void *>(object);

    // Indirect call through the base pointer: proves the vtable is wired
    // up correctly, not just that placement/construction succeeded.
    const uint32_t observed = object->value();
    if (observed != 42u) {
        serial_write("DEBUG kernel_cxx value-mismatch got=");
        serial_write_u64(observed);
        serial_write("\n");
        return false;
    }

    delete object;  // Virtual destructor call through a base pointer.
    if (probe_derived::destroyed_marker != 21u) {
        serial_write("DEBUG kernel_cxx destructor-not-called marker=");
        serial_write_u64(probe_derived::destroyed_marker);
        serial_write("\n");
        return false;
    }

    // A single isolated block's header overhead is only reclaimed by
    // coalescing with an adjacent freed neighbor (see bump_free above) --
    // there is none here, so total-remaining-bytes is not the right
    // invariant to check. What operator delete actually promises is reuse:
    // the next same-size allocation must come back at the freed block's
    // address instead of bumping the arena further.
    probe_derived::destroyed_marker = 0u;
    probe_base *second = new probe_derived(7u);
    if (second == nullptr) {
        serial_write("DEBUG kernel_cxx realloc-failed\n");
        return false;
    }
    const bool reused = static_cast<const void *>(second) == first_address;
    delete second;
    if (!reused) {
        serial_write("DEBUG kernel_cxx freed-block-not-reused\n");
        return false;
    }

    return true;
}

void *operator new(size_t bytes) { return bump_allocate(bytes); }
void *operator new[](size_t bytes) { return bump_allocate(bytes); }
void operator delete(void *ptr) noexcept { bump_free(ptr); }
void operator delete[](void *ptr) noexcept { bump_free(ptr); }
void operator delete(void *ptr, size_t) noexcept { bump_free(ptr); }
void operator delete[](void *ptr, size_t) noexcept { bump_free(ptr); }

// A pure-virtual call with no override reachable is a real bug in the
// calling kernel code. There is no process to exit here (ring 0, no
// scheduler context to unwind) -- halt loudly instead of falling through
// to whatever garbage sits at a null vtable slot.
extern "C" void __cxa_pure_virtual() {
    for (;;) __asm__ volatile("cli; hlt");
}

// Nothing in the kernel ever runs process-exit-time cleanup (there is no
// "exit" for ring 0) -- an inert stub, not a missing feature, matching the
// userspace runtime's rationale in src/cxx_runtime.cpp.
extern "C" int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
extern "C" {
void *__dso_handle = nullptr;
}
