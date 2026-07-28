#ifndef KERNEL_PCI_H
#define KERNEL_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PCI_DEVICE_LIMIT 32u

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t revision;
    uint8_t header_type;
    uint8_t interrupt_line;
    uint32_t bars[6];
};

void pci_init(void);
size_t pci_device_count(void);
bool pci_snapshot(size_t index, struct pci_device *device);
bool pci_find_class(uint8_t class_code, uint8_t subclass, size_t occurrence,
                    struct pci_device *device);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value);

#endif
