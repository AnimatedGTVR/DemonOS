#include <kernel/e1000.h>
#include <kernel/network.h>
#include <kernel/pci.h>

#define E1000_VENDOR_INTEL 0x8086u
#define E1000_DEVICE_82540EM 0x100Eu
#define E1000_CTRL 0x0000u
#define E1000_STATUS 0x0008u
#define E1000_IMC 0x00D8u
#define E1000_RAL0 0x5400u
#define E1000_RAH0 0x5404u
#define E1000_RCTL 0x0100u
#define E1000_TCTL 0x0400u
#define E1000_TIPG 0x0410u
#define E1000_RDBAL 0x2800u
#define E1000_RDBAH 0x2804u
#define E1000_RDLEN 0x2808u
#define E1000_RDH 0x2810u
#define E1000_RDT 0x2818u
#define E1000_TDBAL 0x3800u
#define E1000_TDBAH 0x3804u
#define E1000_TDLEN 0x3808u
#define E1000_TDH 0x3810u
#define E1000_TDT 0x3818u
#define E1000_CTRL_RST (1u << 26u)
#define E1000_STATUS_LU (1u << 1u)
#define DESCRIPTOR_COUNT 8u
#define BUFFER_BYTES 2048u
#define DMA_ARENA_BYTES (9u * 4096u)

struct receive_descriptor {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct transmit_descriptor {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed));

static volatile uint8_t *registers;
static uint8_t address[6];
static bool initialized;
static bool rings_started;
static struct receive_descriptor *receive_ring;
static struct transmit_descriptor *transmit_ring;
static uint8_t *receive_buffers;
static uint8_t *transmit_buffers;
static size_t receive_index;
static size_t transmit_index;
static uint64_t transmitted;
static uint64_t received;

static uint32_t read_register(uint32_t offset) {
    return *(volatile uint32_t *)(registers + offset);
}

static void write_register(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(registers + offset) = value;
    (void)read_register(E1000_STATUS);
}

static void delay(void) {
    for (volatile uint32_t count = 0u; count < 1000000u; ++count)
        __asm__ volatile ("pause");
}

bool e1000_probe(const struct pci_device *device, uintptr_t mmio_base) {
    initialized = false;
    rings_started = false;
    registers = NULL;
    if (device == NULL || device->vendor_id != E1000_VENDOR_INTEL ||
        device->device_id != E1000_DEVICE_82540EM || mmio_base == 0u ||
        (device->bars[0] & 1u) != 0u) return false;
    registers = (volatile uint8_t *)mmio_base;
    uint32_t command = pci_config_read32(device->bus, device->slot,
                                         device->function, 4u);
    command = (command & 0xFFFF0000u) | ((command & 0xFFFFu) | 0x0006u);
    pci_config_write32(device->bus, device->slot, device->function, 4u, command);
    write_register(E1000_IMC, 0xFFFFFFFFu);
    write_register(E1000_CTRL, read_register(E1000_CTRL) | E1000_CTRL_RST);
    delay();
    write_register(E1000_IMC, 0xFFFFFFFFu);
    const uint32_t low = read_register(E1000_RAL0);
    const uint32_t high = read_register(E1000_RAH0);
    address[0] = (uint8_t)low;
    address[1] = (uint8_t)(low >> 8u);
    address[2] = (uint8_t)(low >> 16u);
    address[3] = (uint8_t)(low >> 24u);
    address[4] = (uint8_t)high;
    address[5] = (uint8_t)(high >> 8u);
    bool nonzero = false;
    for (size_t index = 0u; index < 6u; ++index)
        if (address[index] != 0u && address[index] != 0xFFu) nonzero = true;
    if (!nonzero) {
        registers = NULL;
        return false;
    }
    network_set_device(address);
    network_set_link((read_register(E1000_STATUS) & E1000_STATUS_LU) != 0u);
    initialized = true;
    return true;
}

bool e1000_start(uintptr_t dma_arena, size_t arena_size) {
    if (!initialized || dma_arena == 0u || (dma_arena & 4095u) != 0u ||
        arena_size < DMA_ARENA_BYTES) return false;
    uint8_t *arena = (uint8_t *)dma_arena;
    for (size_t index = 0u; index < DMA_ARENA_BYTES; ++index) arena[index] = 0u;
    receive_ring = (struct receive_descriptor *)arena;
    transmit_ring = (struct transmit_descriptor *)(arena + 256u);
    receive_buffers = arena + 4096u;
    transmit_buffers = arena + 5u * 4096u;
    for (size_t index = 0u; index < DESCRIPTOR_COUNT; ++index) {
        receive_ring[index].address =
            (uintptr_t)(receive_buffers + index * BUFFER_BYTES);
        transmit_ring[index].address =
            (uintptr_t)(transmit_buffers + index * BUFFER_BYTES);
        transmit_ring[index].status = 1u;
    }
    const uintptr_t rx_address = (uintptr_t)receive_ring;
    const uintptr_t tx_address = (uintptr_t)transmit_ring;
    write_register(E1000_RDBAL, (uint32_t)rx_address);
    write_register(E1000_RDBAH, (uint32_t)(rx_address >> 32u));
    write_register(E1000_RDLEN, DESCRIPTOR_COUNT * sizeof(*receive_ring));
    write_register(E1000_RDH, 0u);
    write_register(E1000_RDT, DESCRIPTOR_COUNT - 1u);
    write_register(E1000_TDBAL, (uint32_t)tx_address);
    write_register(E1000_TDBAH, (uint32_t)(tx_address >> 32u));
    write_register(E1000_TDLEN, DESCRIPTOR_COUNT * sizeof(*transmit_ring));
    write_register(E1000_TDH, 0u);
    write_register(E1000_TDT, 0u);
    write_register(E1000_TIPG, 0x0060200Au);
    write_register(E1000_TCTL, 0x0004010Au);
    write_register(E1000_RCTL, 0x04008002u);
    receive_index = 0u;
    transmit_index = 0u;
    transmitted = 0u;
    received = 0u;
    rings_started = true;
    return true;
}

bool e1000_transmit(const uint8_t *frame, size_t length) {
    if (!rings_started || frame == NULL || length < 14u ||
        length > BUFFER_BYTES) return false;
    struct transmit_descriptor *descriptor = &transmit_ring[transmit_index];
    if ((descriptor->status & 1u) == 0u) return false;
    uint8_t *buffer = transmit_buffers + transmit_index * BUFFER_BYTES;
    for (size_t index = 0u; index < length; ++index) buffer[index] = frame[index];
    descriptor->length = (uint16_t)length;
    descriptor->checksum_offset = 0u;
    descriptor->command = 0x0Bu;
    descriptor->status = 0u;
    descriptor->checksum_start = 0u;
    descriptor->special = 0u;
    transmit_index = (transmit_index + 1u) % DESCRIPTOR_COUNT;
    write_register(E1000_TDT, (uint32_t)transmit_index);
    ++transmitted;
    return true;
}

size_t e1000_poll(void) {
    if (!rings_started) return 0u;
    size_t handled = 0u;
    while (handled < DESCRIPTOR_COUNT) {
        struct receive_descriptor *descriptor = &receive_ring[receive_index];
        if ((descriptor->status & 1u) == 0u) break;
        if (descriptor->errors == 0u && descriptor->length >= 60u &&
            descriptor->length <= BUFFER_BYTES) {
            (void)network_receive_ethernet(
                receive_buffers + receive_index * BUFFER_BYTES,
                descriptor->length);
            ++received;
        }
        descriptor->status = 0u;
        const size_t released = receive_index;
        receive_index = (receive_index + 1u) % DESCRIPTOR_COUNT;
        write_register(E1000_RDT, (uint32_t)released);
        ++handled;
    }
    return handled;
}

bool e1000_ready(void) { return initialized; }
bool e1000_link_up(void) {
    return initialized && (read_register(E1000_STATUS) & E1000_STATUS_LU) != 0u;
}
const uint8_t *e1000_mac(void) { return initialized ? address : NULL; }
uintptr_t e1000_mmio_base(void) { return (uintptr_t)registers; }
uint64_t e1000_transmitted_frames(void) { return transmitted; }
uint64_t e1000_received_frames(void) { return received; }
