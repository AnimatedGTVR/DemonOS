#ifndef WINE_PE_LOAD_H
#define WINE_PE_LOAD_H

#include "pe.h"
#include <demon/portkit.h>

/* W2 of docs/wine-port.md: maps a validated PE32+ image's sections into
   freshly reserved+committed anonymous memory (syscalls 46/47, W1),
   zero-filling each section and copying its raw file data over the start
   of it. Every section is committed writable regardless of its
   IMAGE_SCN_MEM_WRITE bit -- populating a section during loading requires
   writing into it no matter what its final intended permission is, and
   there is no mprotect-equivalent syscall yet to downgrade a range to
   read-only afterward (see pe_load.c's comment on why: real, bounded
   kernel work, just not done in this pass -- discovered by a real page
   fault when an earlier version committed .text non-writable up front and
   then tried to zero-fill it). No execute-vs-data enforcement either (this
   kernel has no NXE yet -- see anonymous_commit_pages's comment in
   src/arch/x86_64/userspace.c). No import-table resolution, no relocation
   processing. A loaded image is not runnable Win32 code; it proves the
   mapping step works, nothing past it. */

enum wine_pe_load_result {
    WINE_PE_LOAD_OK = 0,
    WINE_PE_LOAD_VALIDATE_FAILED,
    WINE_PE_LOAD_TOO_LARGE,
    WINE_PE_LOAD_BAD_SECTION,
    WINE_PE_LOAD_RESERVE_FAILED,
    WINE_PE_LOAD_COMMIT_FAILED,
    WINE_PE_LOAD_READ_FAILED,
};

struct wine_pe_image {
    uint64_t base;          /* actual load base (the reservation's address) */
    uint64_t entry_point;   /* base + info.entry_point_rva */
    struct wine_pe_info info;
};

/* `file` must already be open. `base` is the address this image's RVA 0
   maps to; the CALLER reserves address space (demon_memory_reserve) and
   picks it -- this function only commits and populates, it does not
   reserve for itself. This split exists because loading more than one
   image into the same process (an EXE plus at least one DLL sharing one
   address space -- the single most basic real DLL-loading scenario) needs
   ONE reservation sized to fit everything, with each wine_pe_load call
   given a different, non-overlapping `base` inside it; W1's
   reserve/commit model has exactly one reservation per process, so a
   version of this function that reserved for itself could only ever load
   one image per process, ever (found by trying to write the two-image
   import-resolution test below and discovering it couldn't work at all).
   Sections are required to appear in non-decreasing VirtualAddress order
   with no section extending past the image's declared SizeOfImage -- true
   of every real PE image any real linker produces; a violation is
   rejected (WINE_PE_LOAD_BAD_SECTION) rather than silently reordered or
   clamped. */
enum wine_pe_load_result wine_pe_load(struct demon_port_file *file,
                                      uint64_t base, struct wine_pe_image *image);

#endif
