#ifndef DOOM_H
#define DOOM_H

/* Fixed user-writable memory layout for the Doom platform-shim proof. The
   ELF image is a single read/execute PT_LOAD (user/linker.ld), so there is
   no writable .data/.bss -- every mutable cell lives at a fixed address in
   the per-process heap window (USER_HEAP 0x330000..0x335000, see
   include/kernel/userspace.h). Keep every symbol below inside that window
   and non-overlapping; bump any block only by editing the defines here. */

#include <stddef.h>
#include <stdint.h>
#include <demon/input.h>

#define DOOM_EVENT       ((struct input_event *)(uintptr_t)0x330000u)
#define DOOM_STATE       ((struct doom_shim_state *)(uintptr_t)0x330040u)
#define DOOM_RAND        (*(uint32_t *)(uintptr_t)0x330068u)
#define DOOM_STRTOK_NEXT (*(char **)(uintptr_t)0x330070u)
#define DOOM_OUTPUT      ((char *)(uintptr_t)0x330080u)
#define DOOM_OUTPUT_CAP  1024u
#define DOOM_STAGING     ((uint32_t *)(uintptr_t)0x330480u)
#define DOOM_STAGING_ROWS 8u
#define DOOM_ARENA_BASE  ((uint8_t *)(uintptr_t)0x332C80u)
#define DOOM_ARENA_SIZE  0x2380u

#define DOOM_WIDTH   320u
#define DOOM_HEIGHT  200u
#define DOOM_PIXELS  (DOOM_WIDTH * DOOM_HEIGHT)

struct doom_shim_state {
    uint64_t surface;
    uint64_t input;
    uint64_t storage;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
};

#endif
