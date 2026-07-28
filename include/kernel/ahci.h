#ifndef KERNEL_AHCI_H
#define KERNEL_AHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/pci.h>

/* One-time reset + port discovery + IDENTIFY DEVICE against the first
   implemented, present SATA port on the HBA at mmio_base (an identity-mapped
   physical address the caller has already mapped, same convention as
   e1000_probe's mmio_base). Populates the sector count reported by the
   drive; call ahci_start() afterward to hand the driver its command-list/FIS
   DMA arena before any read/write. */
bool ahci_probe(const struct pci_device *device, uintptr_t mmio_base);

/* dma_arena must be 4096-aligned and at least AHCI_DMA_ARENA_BYTES; the same
   allocate_contiguous() helper kernel.c already uses for the E1000 rings is
   the intended source. */
#define AHCI_DMA_ARENA_BYTES (2u * 4096u)
bool ahci_start(uintptr_t dma_arena, size_t arena_size);

bool ahci_ready(void);
uint64_t ahci_sector_count(void);
uintptr_t ahci_mmio_base(void);

/* Polled (no interrupts, no NCQ) 512-byte sector read/write through command
   slot 0. buffer must hold count * 512 bytes and, like the DMA arena, must
   be addressable as a physical address the HBA can DMA into directly --
   pass identity-mapped kernel memory, not arbitrary userspace pointers.
   count is capped at AHCI_MAX_SECTORS_PER_REQUEST (one 4096-byte PRDT
   segment) to keep the single-PRDT command table this driver builds valid;
   callers needing more issue multiple requests. */
#define AHCI_MAX_SECTORS_PER_REQUEST 8u
bool ahci_read_sectors(uint64_t lba, uint32_t count, void *buffer);
bool ahci_write_sectors(uint64_t lba, uint32_t count, const void *buffer);

#endif
