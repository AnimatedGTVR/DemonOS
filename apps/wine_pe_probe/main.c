#include <demon/portkit.h>
#include <demon/c_app.h>
#include "../../ports/wine/platform/pe.h"
#include <stddef.h>
#include <stdint.h>

/* W0 of docs/wine-port.md. This builds its own minimal, synthetic PE32+
   fixtures at runtime -- not a real Windows/Wine binary -- the same legal
   hygiene as FreeDOOM standing in for DOOM elsewhere in this codebase: no
   copyrighted Microsoft/Wine content is bundled or read here. Field values
   are picked to be structurally valid, not to resemble any real binary. */

#define WINE_PE_PROBE_ARENA_SIZE (256u * 1024u)
#define FIXTURE_SIZE 1024u

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

/* Fills `buffer` (must be FIXTURE_SIZE bytes, pre-zeroed by the caller)
   with one syntactically valid minimal PE32+ image: DOS header, "PE\0\0"
   signature, an x86-64 COFF header, a minimal (112-byte, zero data
   directories) PE32+ optional header, and one ".text" section whose raw
   data genuinely fits inside the fixture. See ports/wine/platform/pe.c for
   the field layout this mirrors. */
static void build_valid_pe(uint8_t *buffer) {
    buffer[0] = 'M'; buffer[1] = 'Z';
    write_u32(buffer, 0x3Cu, 64u);              /* e_lfanew: PE header at 64 */

    buffer[64] = 'P'; buffer[65] = 'E'; buffer[66] = 0u; buffer[67] = 0u;
    write_u16(buffer, 68u, 0x8664u);             /* Machine: x86-64 */
    write_u16(buffer, 70u, 1u);                  /* NumberOfSections */
    write_u16(buffer, 84u, 112u);                /* SizeOfOptionalHeader */
    write_u16(buffer, 86u, 0x0022u);              /* Characteristics */

    const size_t opt = 88u;                       /* optional header start */
    write_u16(buffer, opt + 0u, 0x020Bu);          /* Magic: PE32+ */
    write_u32(buffer, opt + 16u, 0x1000u);         /* AddressOfEntryPoint (RVA) */
    write_u64(buffer, opt + 24u, 0x140000000ULL);  /* ImageBase */
    write_u32(buffer, opt + 32u, 0x1000u);         /* SectionAlignment */
    write_u32(buffer, opt + 36u, 0x200u);          /* FileAlignment */
    write_u32(buffer, opt + 56u, 0x2000u);         /* SizeOfImage */
    write_u32(buffer, opt + 60u, 0x200u);          /* SizeOfHeaders */
    write_u16(buffer, opt + 68u, 3u);              /* Subsystem: console */
    write_u32(buffer, opt + 108u, 0u);             /* NumberOfRvaAndSizes */

    const size_t section = opt + 112u;             /* section table start */
    buffer[section] = '.'; buffer[section + 1u] = 't'; buffer[section + 2u] = 'e';
    buffer[section + 3u] = 'x'; buffer[section + 4u] = 't';
    write_u32(buffer, section + 8u, 0x1000u);      /* VirtualSize */
    write_u32(buffer, section + 12u, 0x1000u);     /* VirtualAddress */
    write_u32(buffer, section + 16u, 0x200u);      /* SizeOfRawData */
    write_u32(buffer, section + 20u, 0x200u);      /* PointerToRawData */
    write_u32(buffer, section + 36u, 0x60000020u); /* Characteristics: code|execute|read */
}

/* Avoids pulling in apps/doom/libc.h for one strlen call -- this port has
   no reason to depend on the Doom engine's compat layer. */
static size_t path_length(const char *path) {
    size_t length = 0u;
    while (path[length] != '\0') ++length;
    return length;
}

static bool create_fixture(const char *path, const uint8_t *bytes, size_t size) {
    const struct demon_port_runtime *port = demon_port_status();
    const uint64_t handle = demon_file_open(port->storage, path, path_length(path), 1u);
    if (handle == DEMON_PORT_INVALID_HANDLE) return false;
    const bool written = demon_handle_write(handle, bytes, size) == size;
    (void)demon_handle_close(handle);
    return written;
}

static uint8_t fixture_buffer[FIXTURE_SIZE];

static void zero_fixture(void) {
    for (size_t i = 0u; i < FIXTURE_SIZE; ++i) fixture_buffer[i] = 0u;
}

uint64_t wine_pe_probe_main(void) {
    if (!demon_port_init_dynamic(WINE_PE_PROBE_ARENA_SIZE)) return 10u;

    zero_fixture();
    build_valid_pe(fixture_buffer);
    if (!create_fixture("/tmp/valid.exe", fixture_buffer, FIXTURE_SIZE)) return 11u;

    zero_fixture();
    build_valid_pe(fixture_buffer);
    fixture_buffer[0] = 'X'; fixture_buffer[1] = 'X'; /* corrupt DOS magic */
    if (!create_fixture("/tmp/bad-dos-magic.exe", fixture_buffer, FIXTURE_SIZE)) return 12u;

    zero_fixture();
    build_valid_pe(fixture_buffer);
    fixture_buffer[64] = 'X'; fixture_buffer[65] = 'X'; /* corrupt "PE\0\0" */
    if (!create_fixture("/tmp/bad-pe-signature.exe", fixture_buffer, FIXTURE_SIZE)) return 13u;

    zero_fixture();
    build_valid_pe(fixture_buffer);
    /* Point .text's raw data past the end of a file truncated to just the
       headers -- the section table itself is still intact and readable,
       only the data range it describes is now out of bounds. */
    if (!create_fixture("/tmp/bad-section-table.exe", fixture_buffer, 240u)) return 14u;

    struct demon_port_file pe;
    struct wine_pe_info info;

    if (!demon_port_open(&pe, "/tmp/valid.exe")) return 15u;
    if (wine_pe_validate(&pe, &info) != WINE_PE_OK ||
        info.machine != 0x8664u || info.section_count != 1u ||
        info.optional_magic != 0x020Bu || info.entry_point_rva != 0x1000u ||
        info.image_base != 0x140000000ULL || info.size_of_image != 0x2000u)
        return 16u;
    demon_port_close(&pe);

    if (!demon_port_open(&pe, "/tmp/bad-dos-magic.exe")) return 17u;
    if (wine_pe_validate(&pe, &info) != WINE_PE_BAD_DOS_MAGIC) return 18u;
    demon_port_close(&pe);

    if (!demon_port_open(&pe, "/tmp/bad-pe-signature.exe")) return 19u;
    if (wine_pe_validate(&pe, &info) != WINE_PE_BAD_PE_SIGNATURE) return 20u;
    demon_port_close(&pe);

    if (!demon_port_open(&pe, "/tmp/bad-section-table.exe")) return 21u;
    if (wine_pe_validate(&pe, &info) != WINE_PE_BAD_SECTION_TABLE) return 22u;
    demon_port_close(&pe);

    demon_port_write("WINE_PE_PROBE_READY dos-header pe-signature coff-header "
                     "optional-header section-table\n");
    demon_port_shutdown();
    return 0u;
}
