#ifndef KERNEL_E1000_H
#define KERNEL_E1000_H

#include <kernel/pci.h>

#include <stdbool.h>
#include <stdint.h>

bool e1000_probe(const struct pci_device *device, uintptr_t mmio_base);
bool e1000_start(uintptr_t dma_arena, size_t arena_size);
bool e1000_transmit(const uint8_t *frame, size_t length);
size_t e1000_poll(void);
bool e1000_ready(void);
bool e1000_link_up(void);
const uint8_t *e1000_mac(void);
uintptr_t e1000_mmio_base(void);
uint64_t e1000_transmitted_frames(void);
uint64_t e1000_received_frames(void);

#endif
