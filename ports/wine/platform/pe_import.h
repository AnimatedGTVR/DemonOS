#ifndef WINE_PE_IMPORT_H
#define WINE_PE_IMPORT_H

#include "pe_export.h"

/* W3 of docs/wine-port.md: walks an already-loaded image's import
   directory and patches its Import Address Table (IAT) with resolved
   addresses. There is no real DLL search here (see pe_export.h's file
   comment) -- every import is resolved against exactly one already-parsed
   export table the caller supplies, proving the by-name resolution +
   IAT-patch mechanism, not real Windows API compatibility or multi-library
   loading. */

enum wine_pe_import_result {
    WINE_PE_IMPORT_OK = 0,
    WINE_PE_IMPORT_NONE,        /* no import directory */
    WINE_PE_IMPORT_UNRESOLVED,  /* a name wasn't found, or was an ordinal-only import */
};

/* Ordinal-only imports (thunk's top bit set) are rejected as
   WINE_PE_IMPORT_UNRESOLVED -- name-based resolution only, in this pass. */
enum wine_pe_import_result wine_pe_resolve_imports(const struct wine_pe_image *image,
                                                   const struct wine_pe_export_table *exports);

#endif
