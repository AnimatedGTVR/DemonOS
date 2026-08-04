#include "pe_export.h"

static uint32_t read_le32_mem(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) |
        ((uint32_t)bytes[3] << 24u);
}

static uint16_t read_le16_mem(const uint8_t *bytes) {
    return (uint16_t)((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u));
}

enum wine_pe_export_result wine_pe_read_exports(const struct wine_pe_image *image,
                                                struct wine_pe_export_table *table) {
    if (image == NULL || table == NULL) return WINE_PE_EXPORT_BAD;
    if (image->info.export_directory_size == 0u) return WINE_PE_EXPORT_NONE;
    /* This reads directly out of the image's own already-loaded memory
       (base + RVA), not the file -- valid because wine_pe_load already
       copied every section's real bytes into place. It is NOT valid to
       call this before a successful wine_pe_load. */
    const uint8_t *directory =
        (const uint8_t *)(uintptr_t)(image->base + image->info.export_directory_rva);
    table->image_base = image->base;
    table->number_of_names = read_le32_mem(directory + 24u);
    table->address_of_functions = read_le32_mem(directory + 28u);
    table->address_of_names = read_le32_mem(directory + 32u);
    table->address_of_name_ordinals = read_le32_mem(directory + 36u);
    return WINE_PE_EXPORT_OK;
}

uint64_t wine_pe_resolve_export(const struct wine_pe_export_table *table,
                                const char *name) {
    if (table == NULL || name == NULL) return 0u;
    const uint8_t *names =
        (const uint8_t *)(uintptr_t)(table->image_base + table->address_of_names);
    const uint8_t *ordinals =
        (const uint8_t *)(uintptr_t)(table->image_base + table->address_of_name_ordinals);
    const uint8_t *functions =
        (const uint8_t *)(uintptr_t)(table->image_base + table->address_of_functions);
    for (uint32_t index = 0u; index < table->number_of_names; ++index) {
        const uint32_t name_rva = read_le32_mem(names + (size_t)index * 4u);
        const char *candidate =
            (const char *)(uintptr_t)(table->image_base + name_rva);
        size_t position = 0u;
        while (candidate[position] != '\0' && candidate[position] == name[position])
            ++position;
        if (candidate[position] != '\0' || name[position] != '\0') continue;
        const uint16_t ordinal = read_le16_mem(ordinals + (size_t)index * 2u);
        const uint32_t function_rva = read_le32_mem(functions + (size_t)ordinal * 4u);
        return table->image_base + function_rva;
    }
    return 0u;
}
