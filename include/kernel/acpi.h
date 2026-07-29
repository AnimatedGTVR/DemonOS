#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H

#include <stdbool.h>

/* Scans firmware-owned physical memory (EBDA + the 0xE0000-0xFFFFF BIOS
   area) for the ACPI RSDP, then walks RSDT/XSDT -> FADT -> DSDT/SSDT to
   look for the standard Control Method Battery device ID (PNP0C0A). This
   is real ACPI table parsing, not a fake reading: it can only tell whether
   a battery DEVICE is declared in the firmware's ACPI namespace, not its
   current charge, since reading _BST/_BIF values requires a full AML
   interpreter (a project far larger than table parsing) that this kernel
   does not have. Safe to call once, early in boot, after paging is
   confirmed active -- every physical address it touches is required by
   spec to sit in the first 1 MiB (RSDP) or is bounds-checked against the
   1 GiB identity-mapped window mako_build_identity_4m establishes before
   returning true. */
bool acpi_init(void);

/* True once acpi_init() has found and checksum-validated a real RSDP. */
bool acpi_present(void);

/* True only if acpi_init() found the PNP0C0A Control Method Battery ID
   encoded in the DSDT or any SSDT. False (never fabricated) if ACPI is
   absent, if no battery device is declared, or if a table exceeded the
   safely-mapped physical range and had to be skipped. */
bool acpi_battery_present(void);

#endif
