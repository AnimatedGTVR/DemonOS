#ifndef WINE_PE_RELOC_H
#define WINE_PE_RELOC_H

#include "pe_load.h"

/* W4 of docs/wine-port.md: applies IMAGE_REL_BASED_DIR64 base relocations
   to an already-loaded image, adjusting every absolute 64-bit pointer a
   linker baked in assuming the image would land at its preferred
   ImageBase, now that it has actually landed at image->base instead (the
   two are essentially never equal here: ImageBase is whatever the file
   declares, W1's reservation is wherever the kernel's anonymous region
   happens to be). Only DIR64 (the PE32+ 64-bit absolute relocation type)
   and ABSOLUTE (a documented no-op padding entry every block may end with)
   are handled; any other relocation type is rejected rather than silently
   skipped or misapplied. */

enum wine_pe_reloc_result {
    WINE_PE_RELOC_OK = 0,
    WINE_PE_RELOC_NONE,       /* no relocation directory -- nothing to do, not an error */
    WINE_PE_RELOC_UNSUPPORTED /* a relocation type other than DIR64/ABSOLUTE was present */
};

enum wine_pe_reloc_result wine_pe_apply_relocations(const struct wine_pe_image *image);

#endif
