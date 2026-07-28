#ifndef DEMON_CXX_RUNTIME_H
#define DEMON_CXX_RUNTIME_H

/* Wave 0 of the EDE port: the minimum a freestanding C++ MAKO-ABI app needs
   to link at all. There is no MAKO-ABI heap syscall (no brk/mmap
   equivalent, see docs/c-apps.md's "Current C runtime boundary") -- so
   operator new/delete here are backed by a small static bump arena baked
   into each app's own .bss, not a real allocator. Fine for the small,
   short-lived UI objects an EDE app allocates; not a general-purpose heap
   (never reclaims individual blocks; operator delete is a no-op).

   include this header (or just link cxx_runtime.o) from any C++ MAKO-ABI
   app that uses `new`/`delete` or classes with virtual destructors. */

#include <stddef.h>

/* Total bump-arena size available to `new` in a single process. Small on
   purpose: MAKO-ABI apps live inside a tight fixed-size ELF/BSS budget
   (see the Makefile's per-app `size`/footprint checks), same reasoning as
   every other native app here. Raise this per-app via cxx_runtime_reserve
   if a later EDE component genuinely needs more. */
#define CXX_RUNTIME_ARENA_BYTES (32u * 1024u)

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes still available in the bump arena -- lets an app fail loudly
   (rather than silently corrupt memory) if it's about to outgrow its
   budget. */
size_t cxx_runtime_arena_remaining(void);

#ifdef __cplusplus
}
#endif

#endif
