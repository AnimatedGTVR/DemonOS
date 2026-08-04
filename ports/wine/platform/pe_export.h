#ifndef WINE_PE_EXPORT_H
#define WINE_PE_EXPORT_H

#include "pe_load.h"

/* W3 of docs/wine-port.md: reads the export directory (IMAGE_EXPORT_DIRECTORY)
   of an already-loaded image and resolves symbols out of it by name. This
   is the by-name resolution MECHANISM only -- there is no real DLL to
   export anything DemonOS programs would actually want (no kernel32.dll,
   no ntdll.dll; those are each individually larger than this whole
   codebase). Proven in this pass against synthetic, self-authored PE
   images with a fabricated export table, the same legal hygiene as every
   other synthetic fixture in this port (W0/W2). */

struct wine_pe_export_table {
    uint64_t image_base;
    uint32_t number_of_names;
    uint32_t address_of_functions;      /* RVA */
    uint32_t address_of_names;          /* RVA */
    uint32_t address_of_name_ordinals;  /* RVA */
};

enum wine_pe_export_result {
    WINE_PE_EXPORT_OK = 0,
    WINE_PE_EXPORT_NONE,   /* no export directory (DataDirectory[0].Size == 0) */
    WINE_PE_EXPORT_BAD,    /* export directory present but malformed */
};

/* Operates on `image`'s already-committed, already-populated memory (not
   the file) -- call after a successful wine_pe_load. */
enum wine_pe_export_result wine_pe_read_exports(const struct wine_pe_image *image,
                                                struct wine_pe_export_table *table);

/* Linear search by name (NumberOfNames is expected to be small for any
   test fixture this port builds; a real DLL's export table would want a
   binary search over AddressOfNames, since PE requires it sorted, but that
   optimization isn't worth the complexity for a mechanism-proving pass).
   Returns the resolved absolute address, or 0 if `name` isn't exported. */
uint64_t wine_pe_resolve_export(const struct wine_pe_export_table *table,
                                const char *name);

#endif
