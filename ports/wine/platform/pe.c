#include "pe.h"

#define DOS_HEADER_SIZE 64u
#define DOS_E_LFANEW_OFFSET 0x3Cu
#define PE_SIGNATURE_SIZE 4u
#define COFF_HEADER_SIZE 20u
#define SECTION_HEADER_SIZE 40u
#define OPTIONAL_MAGIC_PE32_PLUS 0x020Bu
#define MACHINE_AMD64 0x8664u

static uint16_t read_le16(const uint8_t bytes[2]) {
    return (uint16_t)((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u));
}

static uint32_t read_le32(const uint8_t bytes[4]) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) |
        ((uint32_t)bytes[3] << 24u);
}

static uint64_t read_le64(const uint8_t bytes[8]) {
    return (uint64_t)read_le32(bytes) |
        ((uint64_t)read_le32(bytes + 4u) << 32u);
}

static bool read_exact(struct demon_port_file *file, size_t offset,
                       uint8_t *bytes, size_t length) {
    return demon_port_seek(file, offset) &&
        demon_port_read(file, bytes, length) == length;
}

enum wine_pe_result wine_pe_validate(struct demon_port_file *file,
                                     struct wine_pe_info *info) {
    if (file == NULL || info == NULL) return WINE_PE_IO;
    if (file->size < DOS_HEADER_SIZE) return WINE_PE_TRUNCATED;

    uint8_t dos_header[DOS_HEADER_SIZE];
    if (!read_exact(file, 0u, dos_header, sizeof(dos_header))) return WINE_PE_IO;
    if (dos_header[0] != 'M' || dos_header[1] != 'Z') return WINE_PE_BAD_DOS_MAGIC;

    const uint32_t pe_offset = read_le32(&dos_header[DOS_E_LFANEW_OFFSET]);
    if ((size_t)pe_offset > file->size ||
        (size_t)(PE_SIGNATURE_SIZE + COFF_HEADER_SIZE) >
            file->size - (size_t)pe_offset)
        return WINE_PE_BAD_PE_OFFSET;

    uint8_t signature[PE_SIGNATURE_SIZE];
    if (!read_exact(file, pe_offset, signature, sizeof(signature))) return WINE_PE_IO;
    if (signature[0] != 'P' || signature[1] != 'E' ||
        signature[2] != 0u || signature[3] != 0u)
        return WINE_PE_BAD_PE_SIGNATURE;

    const size_t coff_offset = (size_t)pe_offset + PE_SIGNATURE_SIZE;
    uint8_t coff[COFF_HEADER_SIZE];
    if (!read_exact(file, coff_offset, coff, sizeof(coff))) return WINE_PE_IO;
    const uint16_t machine = read_le16(&coff[0]);
    const uint16_t section_count = read_le16(&coff[2]);
    const uint16_t optional_header_size = read_le16(&coff[16]);
    const uint16_t characteristics = read_le16(&coff[18]);
    if (machine != MACHINE_AMD64) return WINE_PE_UNSUPPORTED_MACHINE;

    const size_t optional_offset = coff_offset + COFF_HEADER_SIZE;
    /* Standard fields this parser reads end at byte 112 of the optional
       header (see docs/wine-port.md's W0 write-up for the field layout);
       a conforming PE32+ image always declares at least that much. */
    if ((size_t)optional_header_size < 112u ||
        (size_t)optional_header_size > file->size - optional_offset)
        return WINE_PE_TRUNCATED;

    uint8_t optional[112];
    if (!read_exact(file, optional_offset, optional, sizeof(optional))) return WINE_PE_IO;
    const uint16_t optional_magic = read_le16(&optional[0]);
    if (optional_magic != OPTIONAL_MAGIC_PE32_PLUS) return WINE_PE_BAD_OPTIONAL_MAGIC;
    const uint32_t entry_point_rva = read_le32(&optional[16]);
    const uint64_t image_base = read_le64(&optional[24]);
    const uint32_t size_of_image = read_le32(&optional[56]);
    const uint32_t number_of_rva_and_sizes = read_le32(&optional[108]);

    /* Export (index 0), import (index 1), and base relocation (index 5)
       data directories immediately follow the 112 standard fields this
       parser already read -- present only if the declared optional header
       size actually extends that far (see the struct comment in pe.h).
       Reads the full standard 16-directory array (128 bytes) at once
       rather than only as many bytes as the directories currently in use
       need, so adding another directory index later doesn't need another
       size threshold. */
    uint32_t export_directory_rva = 0u, export_directory_size = 0u;
    uint32_t import_directory_rva = 0u, import_directory_size = 0u;
    uint32_t reloc_directory_rva = 0u, reloc_directory_size = 0u;
    if ((size_t)optional_header_size >= 240u) {
        uint8_t directories[128];
        if (!read_exact(file, optional_offset + 112u, directories, sizeof(directories)))
            return WINE_PE_IO;
        export_directory_rva = read_le32(&directories[0]);
        export_directory_size = read_le32(&directories[4]);
        import_directory_rva = read_le32(&directories[8]);
        import_directory_size = read_le32(&directories[12]);
        reloc_directory_rva = read_le32(&directories[40]);
        reloc_directory_size = read_le32(&directories[44]);
    }

    const size_t section_table_offset = optional_offset + optional_header_size;
    if ((size_t)section_count >
        (file->size - section_table_offset) / SECTION_HEADER_SIZE)
        return WINE_PE_BAD_SECTION_TABLE;

    uint8_t section[SECTION_HEADER_SIZE];
    for (uint16_t index = 0u; index < section_count; ++index) {
        const size_t offset = section_table_offset +
            (size_t)index * SECTION_HEADER_SIZE;
        if (!read_exact(file, offset, section, sizeof(section))) return WINE_PE_IO;
        const uint32_t raw_size = read_le32(&section[16]);
        const uint32_t raw_pointer = read_le32(&section[20]);
        /* A zero PointerToRawData is a real, valid case (e.g. .bss-like
           sections with no file-backed data) -- only a nonzero pointer's
           range needs to fit inside the file. */
        if (raw_pointer != 0u &&
            ((size_t)raw_pointer > file->size ||
             (size_t)raw_size > file->size - (size_t)raw_pointer))
            return WINE_PE_BAD_SECTION_TABLE;
    }

    info->machine = machine;
    info->section_count = section_count;
    info->optional_header_size = optional_header_size;
    info->characteristics = characteristics;
    info->optional_magic = optional_magic;
    info->entry_point_rva = entry_point_rva;
    info->image_base = image_base;
    info->size_of_image = size_of_image;
    info->number_of_rva_and_sizes = number_of_rva_and_sizes;
    info->section_table_offset = (uint32_t)section_table_offset;
    info->export_directory_rva = export_directory_rva;
    info->export_directory_size = export_directory_size;
    info->import_directory_rva = import_directory_rva;
    info->import_directory_size = import_directory_size;
    info->reloc_directory_rva = reloc_directory_rva;
    info->reloc_directory_size = reloc_directory_size;
    return WINE_PE_OK;
}
