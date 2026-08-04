#include <demon/portkit.h>
#include <demon/c_app.h>
#include "../../ports/wine/platform/pe_export.h"
#include "../../ports/wine/platform/pe_import.h"
#include <stddef.h>
#include <stdint.h>

/* W3 of docs/wine-port.md. Builds two minimal, synthetic PE32+ fixtures at
   runtime -- a fake "exporter" with one named export, and a fake
   "importer" that imports it by name -- same legal hygiene as every other
   fixture in this port: no real Windows/Wine content, entirely
   self-authored bytes proving the resolution mechanism in isolation. */

#define ARENA_SIZE 256u
#define FIXTURE_SIZE 4096u

static void write_u16(uint8_t *buffer, size_t offset, uint16_t value) {
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1u] = (uint8_t)(value >> 8u);
}

static void write_u32(uint8_t *buffer, size_t offset, uint32_t value) {
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1u] = (uint8_t)(value >> 8u);
    buffer[offset + 2u] = (uint8_t)(value >> 16u);
    buffer[offset + 3u] = (uint8_t)(value >> 24u);
}

static void write_u64(uint8_t *buffer, size_t offset, uint64_t value) {
    write_u32(buffer, offset, (uint32_t)value);
    write_u32(buffer, offset + 4u, (uint32_t)(value >> 32u));
}

static void write_cstr(uint8_t *buffer, size_t offset, const char *text) {
    size_t i = 0u;
    while (text[i] != '\0') { buffer[offset + i] = (uint8_t)text[i]; ++i; }
    buffer[offset + i] = 0u;
}

static void write_section(uint8_t *buffer, size_t offset, const char *name,
                          uint32_t virtual_size, uint32_t virtual_address,
                          uint32_t raw_size, uint32_t raw_pointer) {
    for (size_t i = 0u; i < 8u && name[i] != '\0'; ++i) buffer[offset + i] = (uint8_t)name[i];
    write_u32(buffer, offset + 8u, virtual_size);
    write_u32(buffer, offset + 12u, virtual_address);
    write_u32(buffer, offset + 16u, raw_size);
    write_u32(buffer, offset + 20u, raw_pointer);
    write_u32(buffer, offset + 36u, 0xC0000040u); /* read|write, doesn't matter for this test */
}

/* Common PE32+ headers with a full 240-byte (16-directory) optional header
   -- W0/W2's fixtures used the minimal 112-byte form with no directories;
   this test needs DataDirectory[0]/[1] (export/import), so it uses the
   real, complete size every actual linker emits. */
static void write_common_header(uint8_t *buffer, uint32_t size_of_image,
                                uint32_t directory_index, uint32_t directory_rva,
                                uint32_t directory_size) {
    buffer[0] = 'M'; buffer[1] = 'Z';
    write_u32(buffer, 0x3Cu, 64u);
    buffer[64] = 'P'; buffer[65] = 'E'; buffer[66] = 0u; buffer[67] = 0u;
    write_u16(buffer, 68u, 0x8664u);
    write_u16(buffer, 70u, 1u); /* one section */
    write_u16(buffer, 84u, 240u); /* SizeOfOptionalHeader: full, with directories */
    write_u16(buffer, 86u, 0x0022u);

    const size_t opt = 88u;
    write_u16(buffer, opt + 0u, 0x020Bu);
    write_u32(buffer, opt + 16u, 0x1000u); /* entry point RVA: unused by this test */
    write_u64(buffer, opt + 24u, 0x140000000ULL);
    write_u32(buffer, opt + 32u, 0x1000u);
    write_u32(buffer, opt + 36u, 0x200u);
    write_u32(buffer, opt + 56u, size_of_image);
    write_u32(buffer, opt + 60u, 0x200u);
    write_u16(buffer, opt + 68u, 3u);
    write_u32(buffer, opt + 108u, 16u); /* NumberOfRvaAndSizes */
    /* DataDirectory[directory_index] = {rva, size}; every other directory
       stays zeroed, which wine_pe_validate/wine_pe_read_exports/
       wine_pe_resolve_imports all already treat as "absent". */
    write_u32(buffer, opt + 112u + directory_index * 8u, directory_rva);
    write_u32(buffer, opt + 112u + directory_index * 8u + 4u, directory_size);
}

/* Layout inside the exporter's one section (VA base 0x1000):
     0x1000  IMAGE_EXPORT_DIRECTORY (40 bytes)
     0x1028  AddressOfFunctions[1]
     0x102C  AddressOfNames[1]
     0x1030  AddressOfNameOrdinals[1]
     0x1040  "target_func\0"
     0x1050  DLL name "exp.dll\0"
     0x1060  the "function" itself (one placeholder byte) */
static void build_exporter(uint8_t *buffer) {
    write_common_header(buffer, 0x2000u, 0u, 0x1000u, 40u);
    write_section(buffer, 88u + 240u, ".edata", 0x1000u, 0x1000u, 0x200u, 512u);

    const size_t section_file_offset = 512u;
    uint8_t *s = buffer + section_file_offset; /* s[i] corresponds to VA 0x1000+i */

    write_u32(s, 12u, 0x1050u); /* Name -> "exp.dll" */
    write_u32(s, 16u, 1u);      /* Base */
    write_u32(s, 20u, 1u);      /* NumberOfFunctions */
    write_u32(s, 24u, 1u);      /* NumberOfNames */
    write_u32(s, 28u, 0x1028u); /* AddressOfFunctions */
    write_u32(s, 32u, 0x102Cu); /* AddressOfNames */
    write_u32(s, 36u, 0x1030u); /* AddressOfNameOrdinals */

    write_u32(s, 0x28u, 0x1060u); /* functions[0] = RVA of the "function" */
    write_u32(s, 0x2Cu, 0x1040u); /* names[0] = RVA of "target_func" */
    write_u16(s, 0x30u, 0u);      /* ordinals[0] = index 0 into functions[] */
    write_cstr(s, 0x40u, "target_func");
    write_cstr(s, 0x50u, "exp.dll");
    s[0x60u] = 0xC3u; /* a real byte at the exported address, not executed here */
}

/* Layout inside the importer's one section (VA base 0x1000):
     0x1000  IMAGE_IMPORT_DESCRIPTOR (20 bytes, real entry)
     0x1014  IMAGE_IMPORT_DESCRIPTOR (20 bytes, zero terminator)
     0x1028  ILT: one thunk + one zero terminator (16 bytes)
     0x1038  IAT: one thunk + one zero terminator (16 bytes) -- patched by
             wine_pe_resolve_imports
     0x1048  IMAGE_IMPORT_BY_NAME: Hint(2) + "target_func\0"
     0x1060  DLL name "exp.dll\0" */
static void build_importer(uint8_t *buffer) {
    write_common_header(buffer, 0x2000u, 1u, 0x1000u, 40u);
    write_section(buffer, 88u + 240u, ".idata", 0x1000u, 0x1000u, 0x200u, 512u);

    const size_t section_file_offset = 512u;
    uint8_t *s = buffer + section_file_offset;

    write_u32(s, 0x00u, 0x1028u); /* descriptor.OriginalFirstThunk */
    write_u32(s, 0x0Cu, 0x1060u); /* descriptor.Name */
    write_u32(s, 0x10u, 0x1038u); /* descriptor.FirstThunk */
    /* descriptor terminator at 0x1014 (relative 0x14) is already all-zero. */

    write_u64(s, 0x28u, 0x1048u); /* ilt[0] = RVA of IMAGE_IMPORT_BY_NAME */
    /* ilt[1] terminator at 0x30 (relative) already zero. */
    write_u64(s, 0x38u, 0x1048u); /* iat[0], pre-resolution value (irrelevant) */
    /* iat[1] terminator at 0x40 (relative) already zero. */
    write_u16(s, 0x48u, 0u);          /* Hint */
    write_cstr(s, 0x4Au, "target_func");
    write_cstr(s, 0x60u, "exp.dll");
}

static size_t path_length(const char *path) {
    size_t length = 0u;
    while (path[length] != '\0') ++length;
    return length;
}

static bool write_fixture(const char *path, const uint8_t *bytes, size_t size) {
    const struct demon_port_runtime *port = demon_port_status();
    const uint64_t handle = demon_file_open(port->storage, path, path_length(path), 1u);
    if (handle == DEMON_PORT_INVALID_HANDLE) return false;
    const bool written = demon_handle_write(handle, bytes, size) == size;
    (void)demon_handle_close(handle);
    return written;
}

static uint8_t fixture_buffer[FIXTURE_SIZE];
static uint8_t static_arena[ARENA_SIZE];

uint64_t wine_pe_import_check_main(void) {
    if (!demon_port_init(static_arena, ARENA_SIZE)) return 10u;

    for (size_t i = 0u; i < FIXTURE_SIZE; ++i) fixture_buffer[i] = 0u;
    build_exporter(fixture_buffer);
    if (!write_fixture("/tmp/exporter.dll", fixture_buffer, FIXTURE_SIZE)) return 11u;

    for (size_t i = 0u; i < FIXTURE_SIZE; ++i) fixture_buffer[i] = 0u;
    build_importer(fixture_buffer);
    if (!write_fixture("/tmp/importer.exe", fixture_buffer, FIXTURE_SIZE)) return 12u;

    /* One reservation sized for BOTH images, each given a distinct,
       non-overlapping base within it -- see pe_load.h's comment on why
       wine_pe_load no longer reserves for itself. This is the actual point
       of this test: an EXE and a DLL sharing one process address space is
       the single most basic real DLL-loading scenario, and W1's original
       "wine_pe_load reserves its own region" design could never support
       it (discovered while first writing this test, not guessed ahead of
       time). Each image declares SizeOfImage 0x2000; 0x4000 covers both
       back to back with no gap. */
    void *region = demon_memory_reserve(0x4000u);
    if (region == NULL) return 25u;
    const uint64_t exporter_base = (uint64_t)(uintptr_t)region;
    const uint64_t importer_base = exporter_base + 0x2000u;

    struct demon_port_file exporter_file;
    if (!demon_port_open(&exporter_file, "/tmp/exporter.dll")) return 13u;
    struct wine_pe_image exporter_image;
    if (wine_pe_load(&exporter_file, exporter_base, &exporter_image) != WINE_PE_LOAD_OK) return 14u;
    demon_port_close(&exporter_file);

    struct wine_pe_export_table exports;
    if (wine_pe_read_exports(&exporter_image, &exports) != WINE_PE_EXPORT_OK) return 15u;
    if (exports.number_of_names != 1u) return 16u;

    const uint64_t resolved_directly = wine_pe_resolve_export(&exports, "target_func");
    if (resolved_directly != exporter_image.base + 0x1060u) return 17u;
    if (wine_pe_resolve_export(&exports, "no_such_function") != 0u) return 18u;

    struct demon_port_file importer_file;
    if (!demon_port_open(&importer_file, "/tmp/importer.exe")) return 19u;
    struct wine_pe_image importer_image;
    if (wine_pe_load(&importer_file, importer_base, &importer_image) != WINE_PE_LOAD_OK) return 20u;
    demon_port_close(&importer_file);

    if (wine_pe_resolve_imports(&importer_image, &exports) != WINE_PE_IMPORT_OK) return 21u;

    /* The proof: the importer's IAT slot at RVA 0x1038 must now hold the
       exporter's resolved absolute address, patched by
       wine_pe_resolve_imports into memory this test never touched
       directly. */
    const uint64_t *iat_slot =
        (const uint64_t *)(uintptr_t)(importer_image.base + 0x1038u);
    if (*iat_slot != resolved_directly) return 22u;

    /* One reservation, so one unmap call -- unmap takes the reservation's
       own base (region/exporter_base), not either image's individually. */
    if (demon_memory_unmap(region) != 0u) return 23u;

    demon_port_write("WINE_PE_IMPORT_OK export-read resolve-by-name "
                     "unresolved-rejected iat-patch\n");
    return 0u;
}
