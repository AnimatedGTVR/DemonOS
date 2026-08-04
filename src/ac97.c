#include <kernel/ac97.h>
#include <kernel/pci.h>
#include <kernel/serial.h>

#include <stdint.h>

/* Intel ICH AC'97 native-audio mixer and PCM-out bus-master registers. */
#define AC97_NAM_RESET 0x00u
#define AC97_NAM_MASTER_VOLUME 0x02u
#define AC97_NAM_PCM_VOLUME 0x18u
#define AC97_NAM_EXTENDED_STATUS 0x2Au
#define AC97_NAM_FRONT_DAC_RATE 0x2Cu
#define AC97_PO_BDBAR 0x10u
#define AC97_PO_CIV 0x14u
#define AC97_PO_LVI 0x15u
#define AC97_PO_SR 0x16u
#define AC97_PO_CR 0x1Bu
#define AC97_SR_DCH 0x01u
#define AC97_SR_CLEAR 0x1Cu
#define AC97_CR_RUN 0x01u
#define AC97_CR_RESET 0x02u

struct ac97_descriptor {
    uint32_t address;
    uint32_t sample_count;
} __attribute__((packed));

static struct ac97_descriptor descriptor __attribute__((aligned(8)));
static int16_t pcm[AC97_MAX_FRAMES * 2u] __attribute__((aligned(16)));
static uint16_t mixer_base;
static uint16_t bus_master_base;
static bool ready;
static uint64_t submitted;

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline void out16(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline void out32(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

bool ac97_init(void) {
    struct pci_device device;
    ready = false;
    submitted = 0u;
    if (!pci_find_class(4u, 1u, 0u, &device) ||
        (device.bars[0] & 1u) == 0u || (device.bars[1] & 1u) == 0u)
        return false;
    mixer_base = (uint16_t)(device.bars[0] & ~3u);
    bus_master_base = (uint16_t)(device.bars[1] & ~3u);
    uint32_t command = pci_config_read32(device.bus, device.slot,
                                          device.function, 4u);
    command |= 0x00000005u; /* I/O space and bus mastering. */
    pci_config_write32(device.bus, device.slot, device.function, 4u, command);

    out16(mixer_base + AC97_NAM_RESET, 0u);
    out16(mixer_base + AC97_NAM_MASTER_VOLUME, 0u);
    out16(mixer_base + AC97_NAM_PCM_VOLUME, 0u);
    out16(mixer_base + AC97_NAM_EXTENDED_STATUS, 1u); /* variable-rate PCM */
    out16(mixer_base + AC97_NAM_FRONT_DAC_RATE, AC97_SAMPLE_RATE);

    out8(bus_master_base + AC97_PO_CR, AC97_CR_RESET);
    for (size_t spin = 0u; spin < 100000u; ++spin)
        if ((in8(bus_master_base + AC97_PO_CR) & AC97_CR_RESET) == 0u) break;
    out16(bus_master_base + AC97_PO_SR, AC97_SR_CLEAR);
    descriptor.address = (uint32_t)(uintptr_t)&pcm[0];
    descriptor.sample_count = 0u;
    out32(bus_master_base + AC97_PO_BDBAR,
          (uint32_t)(uintptr_t)&descriptor);
    ready = true;
    serial_write("AC97_AUDIO_READY rate=44100 channels=2 bits=16\n");
    return true;
}

bool ac97_available(void) { return ready; }

bool ac97_submit(const int16_t *samples, size_t frame_count) {
    if (!ready || samples == 0 || frame_count == 0u ||
        frame_count > AC97_MAX_FRAMES) return false;
    /* Do not overwrite a buffer that the controller is still consuming.
       Dropping a late slice is preferable to blocking the entire kernel. */
    if (submitted != 0u &&
        (in8(bus_master_base + AC97_PO_SR) & AC97_SR_DCH) == 0u) return false;
    for (size_t index = 0u; index < frame_count * 2u; ++index)
        pcm[index] = samples[index];
    descriptor.address = (uint32_t)(uintptr_t)&pcm[0];
    descriptor.sample_count = (uint32_t)(frame_count * 2u);
    out8(bus_master_base + AC97_PO_CR, AC97_CR_RESET);
    for (size_t spin = 0u; spin < 10000u; ++spin)
        if ((in8(bus_master_base + AC97_PO_CR) & AC97_CR_RESET) == 0u) break;
    out16(bus_master_base + AC97_PO_SR, AC97_SR_CLEAR);
    out32(bus_master_base + AC97_PO_BDBAR,
          (uint32_t)(uintptr_t)&descriptor);
    out8(bus_master_base + AC97_PO_LVI, 0u);
    out8(bus_master_base + AC97_PO_CR, AC97_CR_RUN);
    ++submitted;
    return true;
}

uint64_t ac97_buffers_submitted(void) { return submitted; }
