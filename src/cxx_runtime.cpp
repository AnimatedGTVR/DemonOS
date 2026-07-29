// Wave 0 of the EDE port -- see include/demon/cxx_runtime.h for why this
// exists and what it deliberately does not do (no exceptions, no RTTI;
// every EDE-facing app is built with -fno-exceptions -fno-rtti, matching
// the freestanding CFLAGS every native C app already uses).
//
// The allocator below replaces an earlier bump-only version (operator
// delete was a no-op) with a real first-fit, address-ordered implicit
// free list with boundary-tag coalescing -- the same basic design as a
// textbook K&R malloc. Needed before any longer-running C++ component
// (a real port of ede-panel's applets, not just a short-lived dialog like
// ede_calc) is realistic: a bump allocator that never reclaims eventually
// exhausts its arena even in a process that just opens and closes a few
// dialogs in a loop.
#include <demon/cxx_runtime.h>

#include <stddef.h>
#include <stdint.h>

namespace {

// Every live or free block is prefixed by this header. `size` is the
// USABLE payload size (not including the header), always 16-byte aligned
// so the payload right after the header meets operator new's alignment
// guarantee. Blocks are singly-linked by address order (`next`), which is
// also how we find each block's physical neighbor for coalescing --
// there's no separate free list, just a walk over every block in the
// arena, since CXX_RUNTIME_ARENA_BYTES is small enough (tens of KiB) that
// an O(n) walk per alloc/free is cheap relative to what these short-lived
// UI apps actually do with it.
struct block_header {
    size_t size;
    bool used;
    block_header *next;
};

alignas(16) uint8_t arena[CXX_RUNTIME_ARENA_BYTES];
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

// Extends the arena by carving a brand-new block out of untouched space
// past every block allocated so far -- the same "bump" step the old
// allocator did unconditionally, now only a fallback once the free list
// has nothing big enough to satisfy a request.
block_header *extend(size_t payload_size) {
    const size_t needed = kHeaderSize + payload_size;
    if (needed > static_cast<size_t>(arena + CXX_RUNTIME_ARENA_BYTES - arena_high_water))
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

    // First-fit scan over the existing block list; splits an
    // over-large free block when the remainder is big enough to be a
    // useful block of its own, so freeing a large allocation and
    // reallocating something small doesn't waste the rest of it.
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

    // Coalesce with the next block if it's physically adjacent and free
    // (it always is adjacent in this design -- blocks are laid out back
    // to back with no gaps -- so this only needs the free-flag check).
    if (block->next != nullptr && !block->next->used) {
        block->size += kHeaderSize + block->next->size;
        block->next = block->next->next;
    }

    // Coalescing with the PREVIOUS block needs a scan (no back-pointer),
    // acceptable for the same reason the allocate-side scan is: this
    // arena is small and these are short-lived UI-object frees, not a
    // hot allocator loop.
    for (block_header *prev = first_block; prev != nullptr; prev = prev->next) {
        if (prev->next == block && !prev->used) {
            prev->size += kHeaderSize + block->size;
            prev->next = block->next;
            break;
        }
    }
}

}  // namespace

extern "C" size_t cxx_runtime_arena_remaining(void) {
    size_t free_bytes = static_cast<size_t>(arena + CXX_RUNTIME_ARENA_BYTES - arena_high_water);
    for (block_header *block = first_block; block != nullptr; block = block->next)
        if (!block->used) free_bytes += block->size;
    return free_bytes;
}

// Replaces libstdc++'s operator new/delete (never linked in -- -nostdlib).
// A null return here is a real allocation failure: with -fno-exceptions,
// operator new can't throw std::bad_alloc, so the caller sees a null
// pointer exactly like C's malloc(). EDE code that assumes new never
// returns null (idiomatic pre-C++11 style, common in this vintage of
// codebase) will need auditing per component during the actual port --
// tracked as part of each wave, not solved generically here.
void *operator new(size_t bytes) { return bump_allocate(bytes); }
void *operator new[](size_t bytes) { return bump_allocate(bytes); }
// Real reclaim now (see the allocator above), not a no-op: a block freed
// here is coalesced with its physical neighbors and becomes available to
// a later `new` of any size that fits, not just the exact size freed.
void operator delete(void *ptr) noexcept { bump_free(ptr); }
void operator delete[](void *ptr) noexcept { bump_free(ptr); }
void operator delete(void *ptr, size_t) noexcept { bump_free(ptr); }
void operator delete[](void *ptr, size_t) noexcept { bump_free(ptr); }

// A pure-virtual call with no override reachable is a real bug in the
// calling code, not something to paper over -- exit loudly via the same
// process-exit syscall path entry.S already uses, instead of falling
// through to whatever garbage sits at a null vtable slot.
extern "C" void __cxa_pure_virtual() {
    __asm__ volatile(
        "mov $2, %%eax\n"
        "mov $111, %%edi\n"
        "int $0x80\n"
        :
        :
        : "eax", "edi", "memory");
    for (;;) __asm__ volatile("hlt");
}

// Global objects with non-trivial destructors register themselves here via
// the C++ ABI. Nothing in MAKO-ABI ever calls process-exit-time cleanup
// (userspace_run_init tears the whole address space down instead, same as
// every other native app) -- so this is intentionally an inert stub, not a
// missing feature.
extern "C" int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
extern "C" {
void *__dso_handle = nullptr;
}
