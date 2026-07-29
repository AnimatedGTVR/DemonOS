#include <kernel/acpi.h>
#include <kernel/serial.h>

#include <stddef.h>
#include <stdint.h>

/* Physical addresses above this are outside the 1 GiB identity map
   mako_build_identity_4m sets up before acpi_init() ever runs (see its
   comment in mako/kernel_probe.mko) -- anything a firmware table points
   past here is skipped rather than dereferenced. */
#define ACPI_SAFE_LIMIT 0x40000000ull

static bool g_acpi_present;
static bool g_battery_present;

static uint8_t phys_u8(uint64_t address) {
    return *(const volatile uint8_t *)(uintptr_t)address;
}

static uint32_t phys_u32(uint64_t address) {
    uint32_t value = 0u;
    for (uint32_t i = 0u; i < 4u; ++i)
        value |= (uint32_t)phys_u8(address + i) << (8u * i);
    return value;
}

static uint64_t phys_u64(uint64_t address) {
    uint64_t value = 0u;
    for (uint32_t i = 0u; i < 8u; ++i)
        value |= (uint64_t)phys_u8(address + i) << (8u * i);
    return value;
}

static bool sig_matches(uint64_t address, const char *signature) {
    for (uint32_t i = 0u; i < 4u; ++i)
        if (phys_u8(address + i) != (uint8_t)signature[i]) return false;
    return true;
}

static bool checksum_ok(uint64_t address, uint32_t length) {
    uint8_t sum = 0u;
    for (uint32_t i = 0u; i < length; ++i) sum = (uint8_t)(sum + phys_u8(address + i));
    return sum == 0u;
}

/* The ACPI-spec "compressed EISA ID" encoding of the ASCII PNP ID
   "PNP0C0A" (the Control Method Battery device), computed once here rather
   than hardcoded as a magic number so the derivation is auditable: each of
   the 3 letters packs into 5 bits as (letter - '@'), the 4 hex digits pack
   into the low 16 bits 4 bits at a time. This is the exact 32-bit value
   EISAID("PNP0C0A") produces in real ASL source, and therefore the exact
   little-endian DWord an ASL compiler emits into the DSDT/SSDT AML byte
   stream when a real battery device declares that _HID. */
static uint32_t battery_eisa_id(void) {
    const uint32_t c0 = (uint32_t)('P' - '@');
    const uint32_t c1 = (uint32_t)('N' - '@');
    const uint32_t c2 = (uint32_t)('P' - '@');
    const uint32_t h3 = 0x0u; /* '0' */
    const uint32_t h4 = 0xCu; /* 'C' */
    const uint32_t h5 = 0x0u; /* '0' */
    const uint32_t h6 = 0xAu; /* 'A' */
    return (c0 << 26) | (c1 << 21) | (c2 << 16) |
           (h3 << 12) | (h4 << 8) | (h5 << 4) | h6;
}

/* Scans one ACPI table's raw bytes for either encoding a real ASL compiler
   could have used for _HID(PNP0C0A): the packed EISA ID as a DWordPrefix
   (0x0C) followed by its 4 little-endian bytes (the overwhelmingly common
   case -- EISAID() is what every real DSDT uses), or, for completeness,
   the literal ASCII string "PNP0C0A" (permitted by spec, rarely used). No
   AML opcodes are interpreted; this is a byte-pattern search, not
   execution, which is exactly why it can only prove a battery device is
   declared, never read its live state. */
static bool table_declares_battery(uint64_t base, uint32_t length) {
    if (length < 8u) return false;
    const uint32_t eisa_id = battery_eisa_id();
    const uint8_t eisa_bytes[4] = {
        (uint8_t)(eisa_id), (uint8_t)(eisa_id >> 8u),
        (uint8_t)(eisa_id >> 16u), (uint8_t)(eisa_id >> 24u)};
    const char *ascii = "PNP0C0A";
    for (uint32_t offset = 0u; offset + 8u <= length; ++offset) {
        const uint64_t here = base + offset;
        if (phys_u8(here) == 0x0Cu &&
            phys_u8(here + 1u) == eisa_bytes[0] &&
            phys_u8(here + 2u) == eisa_bytes[1] &&
            phys_u8(here + 3u) == eisa_bytes[2] &&
            phys_u8(here + 4u) == eisa_bytes[3])
            return true;
        bool ascii_match = true;
        for (uint32_t i = 0u; i < 7u; ++i)
            if (phys_u8(here + i) != (uint8_t)ascii[i]) { ascii_match = false; break; }
        if (ascii_match) return true;
    }
    return false;
}

static bool scan_table_for_battery(uint64_t table_address) {
    if (table_address == 0u || table_address >= ACPI_SAFE_LIMIT) return false;
    const uint32_t length = phys_u32(table_address + 4u);
    if (length < 36u || table_address + length > ACPI_SAFE_LIMIT) return false;
    return table_declares_battery(table_address, length);
}

/* FADT (signature "FACP") layout: 32-bit Dsdt pointer at offset 40;
   revision >=2 FADTs (length >= 148) additionally carry a 64-bit X_Dsdt at
   offset 140 that should be preferred when nonzero. */
static uint64_t fadt_dsdt_address(uint64_t fadt_address, uint32_t fadt_length) {
    if (fadt_length >= 148u) {
        const uint64_t extended = phys_u64(fadt_address + 140u);
        if (extended != 0u) return extended;
    }
    if (fadt_length >= 44u) return (uint64_t)phys_u32(fadt_address + 40u);
    return 0u;
}

static bool walk_root_table(uint64_t root_address, bool entries_are_64bit) {
    if (root_address == 0u || root_address >= ACPI_SAFE_LIMIT) return false;
    const uint32_t length = phys_u32(root_address + 4u);
    if (length < 36u || root_address + length > ACPI_SAFE_LIMIT) return false;
    if (!checksum_ok(root_address, length)) return false;

    const uint32_t entry_size = entries_are_64bit ? 8u : 4u;
    const uint32_t entry_count = (length - 36u) / entry_size;
    const uint32_t max_entries = entry_count > 64u ? 64u : entry_count;
    bool found_battery = false;

    for (uint32_t i = 0u; i < max_entries; ++i) {
        const uint64_t entry_address = root_address + 36u + (uint64_t)i * entry_size;
        const uint64_t table_address = entries_are_64bit ?
            phys_u64(entry_address) : (uint64_t)phys_u32(entry_address);
        if (table_address == 0u || table_address >= ACPI_SAFE_LIMIT) continue;
        const uint32_t table_length = phys_u32(table_address + 4u);
        if (table_length < 36u || table_address + table_length > ACPI_SAFE_LIMIT) continue;

        if (sig_matches(table_address, "FACP")) {
            const uint64_t dsdt_address = fadt_dsdt_address(table_address, table_length);
            if (scan_table_for_battery(dsdt_address)) found_battery = true;
        } else if (sig_matches(table_address, "SSDT")) {
            if (table_declares_battery(table_address, table_length)) found_battery = true;
        }
    }
    return found_battery;
}

static bool rsdp_checksum_ok(uint64_t address, bool extended) {
    if (!checksum_ok(address, 20u)) return false;
    if (extended) {
        const uint32_t length = phys_u32(address + 20u);
        if (length < 33u || length > 4096u) return false;
        if (!checksum_ok(address, length)) return false;
    }
    return true;
}

static uint64_t find_rsdp_in_range(uint64_t start, uint64_t end) {
    for (uint64_t address = start; address + 20u <= end; address += 16u) {
        if (phys_u8(address) == 'R' && phys_u8(address + 1u) == 'S' &&
            phys_u8(address + 2u) == 'D' && phys_u8(address + 3u) == ' ' &&
            phys_u8(address + 4u) == 'P' && phys_u8(address + 5u) == 'T' &&
            phys_u8(address + 6u) == 'R' && phys_u8(address + 7u) == ' ')
            return address;
    }
    return 0u;
}

static uint64_t find_rsdp(void) {
    /* The EBDA segment (real-mode paragraph) lives at physical 0x40E;
       ACPI requires the RSDP to sit in its first 1 KiB when an EBDA is
       reported, searched before the fallback 0xE0000-0xFFFFF BIOS area. */
    const uint16_t ebda_segment = (uint16_t)phys_u8(0x40Eu) |
        ((uint16_t)phys_u8(0x40Fu) << 8u);
    if (ebda_segment != 0u) {
        const uint64_t ebda_base = (uint64_t)ebda_segment << 4u;
        if (ebda_base + 1024u < ACPI_SAFE_LIMIT) {
            const uint64_t found = find_rsdp_in_range(ebda_base, ebda_base + 1024u);
            if (found != 0u) return found;
        }
    }
    return find_rsdp_in_range(0xE0000ull, 0x100000ull);
}

bool acpi_init(void) {
    g_acpi_present = false;
    g_battery_present = false;

    const uint64_t rsdp_address = find_rsdp();
    if (rsdp_address == 0u) {
        serial_write("ACPI_RSDP_NOT_FOUND\n");
        return false;
    }
    const uint8_t revision = phys_u8(rsdp_address + 15u);
    if (!rsdp_checksum_ok(rsdp_address, revision >= 2u)) {
        serial_write("ACPI_RSDP_CHECKSUM_INVALID\n");
        return false;
    }
    g_acpi_present = true;
    serial_write("ACPI_RSDP_FOUND revision=");
    serial_write_u64(revision);
    serial_write("\n");

    bool found_battery = false;
    if (revision >= 2u) {
        const uint64_t xsdt_address = phys_u64(rsdp_address + 24u);
        if (xsdt_address != 0u && sig_matches(xsdt_address, "XSDT"))
            found_battery = walk_root_table(xsdt_address, true);
    }
    if (!found_battery) {
        const uint64_t rsdt_address = (uint64_t)phys_u32(rsdp_address + 16u);
        if (rsdt_address != 0u && sig_matches(rsdt_address, "RSDT"))
            found_battery = walk_root_table(rsdt_address, false) || found_battery;
    }

    g_battery_present = found_battery;
    serial_write("ACPI_BATTERY_PRESENT=");
    serial_write_u64(found_battery ? 1u : 0u);
    serial_write("\n");
    return true;
}

bool acpi_present(void) { return g_acpi_present; }
bool acpi_battery_present(void) { return g_battery_present; }
