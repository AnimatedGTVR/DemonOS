#include <kernel/pci.h>

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

static struct pci_device devices[PCI_DEVICE_LIMIT];
static size_t device_count;

static inline void out32(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset) {
    const uint32_t address = 0x80000000u | ((uint32_t)bus << 16u) |
        ((uint32_t)(slot & 0x1Fu) << 11u) |
        ((uint32_t)(function & 7u) << 8u) | (offset & 0xFCu);
    out32(PCI_CONFIG_ADDRESS, address);
    return in32(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint32_t value) {
    const uint32_t address = 0x80000000u | ((uint32_t)bus << 16u) |
        ((uint32_t)(slot & 0x1Fu) << 11u) |
        ((uint32_t)(function & 7u) << 8u) | (offset & 0xFCu);
    out32(PCI_CONFIG_ADDRESS, address);
    out32(PCI_CONFIG_DATA, value);
}

static bool present(uint8_t bus, uint8_t slot, uint8_t function) {
    return (pci_config_read32(bus, slot, function, 0u) & 0xFFFFu) != 0xFFFFu;
}

static void record(uint8_t bus, uint8_t slot, uint8_t function) {
    if (device_count >= PCI_DEVICE_LIMIT) return;
    const uint32_t identity = pci_config_read32(bus, slot, function, 0u);
    const uint32_t class_info = pci_config_read32(bus, slot, function, 8u);
    const uint32_t header = pci_config_read32(bus, slot, function, 12u);
    const uint32_t interrupt = pci_config_read32(bus, slot, function, 60u);
    struct pci_device *device = &devices[device_count++];
    *device = (struct pci_device){
        .bus = bus, .slot = slot, .function = function,
        .vendor_id = (uint16_t)identity,
        .device_id = (uint16_t)(identity >> 16u),
        .revision = (uint8_t)class_info,
        .programming_interface = (uint8_t)(class_info >> 8u),
        .subclass = (uint8_t)(class_info >> 16u),
        .class_code = (uint8_t)(class_info >> 24u),
        .header_type = (uint8_t)(header >> 16u),
        .interrupt_line = (uint8_t)interrupt,
    };
    const size_t bar_count = (device->header_type & 0x7Fu) == 0u ? 6u : 2u;
    for (size_t bar = 0u; bar < bar_count; ++bar)
        device->bars[bar] = pci_config_read32(bus, slot, function,
                                              (uint8_t)(16u + bar * 4u));
}

void pci_init(void) {
    device_count = 0u;
    for (uint16_t bus = 0u; bus < 256u; ++bus) {
        for (uint8_t slot = 0u; slot < 32u; ++slot) {
            if (!present((uint8_t)bus, slot, 0u)) continue;
            record((uint8_t)bus, slot, 0u);
            const uint8_t header_type =
                (uint8_t)(pci_config_read32((uint8_t)bus, slot, 0u, 12u) >> 16u);
            if ((header_type & 0x80u) == 0u) continue;
            for (uint8_t function = 1u; function < 8u; ++function)
                if (present((uint8_t)bus, slot, function))
                    record((uint8_t)bus, slot, function);
        }
    }
}

size_t pci_device_count(void) { return device_count; }

bool pci_snapshot(size_t index, struct pci_device *device) {
    if (index >= device_count || device == NULL) return false;
    *device = devices[index];
    return true;
}

bool pci_find_class(uint8_t class_code, uint8_t subclass, size_t occurrence,
                    struct pci_device *device) {
    for (size_t index = 0u; index < device_count; ++index) {
        if (devices[index].class_code != class_code ||
            devices[index].subclass != subclass) continue;
        if (occurrence != 0u) {
            --occurrence;
            continue;
        }
        if (device != NULL) *device = devices[index];
        return true;
    }
    return false;
}
