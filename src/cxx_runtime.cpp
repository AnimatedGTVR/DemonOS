// Wave 0 of the EDE port -- see include/demon/cxx_runtime.h for why this
// exists and what it deliberately does not do (no real free(), no
// exceptions, no RTTI; every EDE-facing app is built with -fno-exceptions
// -fno-rtti, matching the freestanding CFLAGS every native C app already
// uses).
#include <demon/cxx_runtime.h>

#include <stddef.h>
#include <stdint.h>

namespace {
alignas(16) uint8_t arena[CXX_RUNTIME_ARENA_BYTES];
size_t arena_used = 0u;

void *bump_allocate(size_t bytes) {
    // 16-byte alignment covers every scalar type this toolchain can hand
    // `new` without needing an alignment parameter -- the same guarantee
    // malloc() gives on x86_64.
    const size_t aligned = (bytes + 15u) & ~static_cast<size_t>(15u);
    if (aligned > CXX_RUNTIME_ARENA_BYTES - arena_used) return nullptr;
    void *result = &arena[arena_used];
    arena_used += aligned;
    return result;
}
}  // namespace

extern "C" size_t cxx_runtime_arena_remaining(void) {
    return CXX_RUNTIME_ARENA_BYTES - arena_used;
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
// No-op: the bump arena never reclaims individual blocks (see the header
// comment). Safe because every EDE app targeted so far is short-lived --
// it runs, the process exits, and MAKO-ABI tears down the whole address
// space. A long-running component that churns allocations would need a
// real allocator, not this one.
void operator delete(void *) noexcept {}
void operator delete[](void *) noexcept {}
void operator delete(void *, size_t) noexcept {}
void operator delete[](void *, size_t) noexcept {}

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
