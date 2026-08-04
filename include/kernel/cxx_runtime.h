#ifndef KERNEL_CXX_RUNTIME_H
#define KERNEL_CXX_RUNTIME_H

/* K0 of docs/kernel-cxx-port.md: the minimum a freestanding, ring-0 C++
   translation unit needs to link into build/kernel.elf at all -- a real
   allocator (operator new/delete) backed by a static arena, plus the two
   ABI hooks the compiler emits references to as soon as any class has a
   pure-virtual function or (if one is ever added) a non-trivial global
   destructor: __cxa_pure_virtual and __cxa_atexit. Same constraints as the
   existing userspace runtime (include/demon/cxx_runtime.h): no exceptions,
   no RTTI, no libstdc++, nothing that assumes a real OS underneath it. */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes still available in the kernel C++ arena. */
size_t kernel_cxx_arena_remaining(void);

/* Boot self-test (K0): allocates via `new`, calls a virtual method through
   a base-class pointer, destroys it via a virtual destructor, and checks
   the arena reclaims the freed bytes. Mirrors cxx_hello's userspace proof,
   run once at boot before any real kernel subsystem depends on this. */
bool kernel_cxx_self_test(void);

#ifdef __cplusplus
}
#endif

#endif
