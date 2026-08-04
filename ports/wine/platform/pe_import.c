#include "pe_import.h"

#define IMPORT_DESCRIPTOR_SIZE 20u
#define ORDINAL_FLAG (1ULL << 63u)

static uint32_t read_le32_mem(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) |
        ((uint32_t)bytes[3] << 24u);
}

static uint64_t read_le64_mem(const uint8_t *bytes) {
    return (uint64_t)read_le32_mem(bytes) | ((uint64_t)read_le32_mem(bytes + 4u) << 32u);
}

static void write_le64_mem(uint8_t *bytes, uint64_t value) {
    for (size_t i = 0u; i < 8u; ++i) bytes[i] = (uint8_t)(value >> (i * 8u));
}

enum wine_pe_import_result wine_pe_resolve_imports(const struct wine_pe_image *image,
                                                   const struct wine_pe_export_table *exports) {
    if (image == NULL || exports == NULL) return WINE_PE_IMPORT_UNRESOLVED;
    if (image->info.import_directory_size == 0u) return WINE_PE_IMPORT_NONE;

    const uint8_t *base = (const uint8_t *)(uintptr_t)image->base;
    size_t descriptor_index = 0u;
    for (;;) {
        const uint8_t *descriptor = base + image->info.import_directory_rva +
            descriptor_index * IMPORT_DESCRIPTOR_SIZE;
        const uint32_t original_first_thunk = read_le32_mem(descriptor + 0u);
        const uint32_t name_rva = read_le32_mem(descriptor + 12u);
        const uint32_t first_thunk = read_le32_mem(descriptor + 16u);
        /* An all-zero descriptor terminates the array (checking Name is
           enough in practice; every real descriptor names its DLL). */
        if (name_rva == 0u) break;

        const uint32_t lookup_rva = original_first_thunk != 0u ? original_first_thunk : first_thunk;
        for (size_t thunk_index = 0u; ; ++thunk_index) {
            const uint8_t *lookup_thunk = base + lookup_rva + thunk_index * 8u;
            const uint64_t thunk_value = read_le64_mem(lookup_thunk);
            if (thunk_value == 0u) break; /* thunk array terminator */
            if ((thunk_value & ORDINAL_FLAG) != 0u) return WINE_PE_IMPORT_UNRESOLVED;

            const uint32_t import_by_name_rva = (uint32_t)thunk_value;
            const char *imported_name = (const char *)(base + import_by_name_rva + 2u /* Hint */);
            const uint64_t resolved = wine_pe_resolve_export(exports, imported_name);
            if (resolved == 0u) return WINE_PE_IMPORT_UNRESOLVED;

            uint8_t *address_thunk = (uint8_t *)base + first_thunk + thunk_index * 8u;
            write_le64_mem(address_thunk, resolved);
        }
        ++descriptor_index;
    }
    return WINE_PE_IMPORT_OK;
}
