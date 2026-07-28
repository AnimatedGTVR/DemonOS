#include <kernel/ahci.h>

#define AHCI_VENDOR_ANY 0u

/* Generic Host Control block, offsets from mmio_base. */
#define AHCI_REG_CAP 0x00u
#define AHCI_REG_GHC 0x04u
#define AHCI_REG_IS 0x08u
#define AHCI_REG_PI 0x0Cu
#define AHCI_GHC_AE (1u << 31u)
#define AHCI_GHC_HR (1u << 0u)

/* Port registers: base 0x100, 0x80 bytes per port. */
#define AHCI_PORT_BASE 0x100u
#define AHCI_PORT_STRIDE 0x80u
#define AHCI_PORT_CLB 0x00u
#define AHCI_PORT_CLBU 0x04u
#define AHCI_PORT_FB 0x08u
#define AHCI_PORT_FBU 0x0Cu
#define AHCI_PORT_IS 0x10u
#define AHCI_PORT_IE 0x14u
#define AHCI_PORT_CMD 0x18u
#define AHCI_PORT_TFD 0x20u
#define AHCI_PORT_SIG 0x24u
#define AHCI_PORT_SSTS 0x28u
#define AHCI_PORT_SCTL 0x2Cu
#define AHCI_PORT_SERR 0x30u
#define AHCI_PORT_CI 0x38u

#define AHCI_PORT_CMD_ST (1u << 0u)
#define AHCI_PORT_CMD_FRE (1u << 4u)
#define AHCI_PORT_CMD_FR (1u << 14u)
#define AHCI_PORT_CMD_CR (1u << 15u)

#define AHCI_TFD_BSY (1u << 7u)
#define AHCI_TFD_DRQ (1u << 3u)

#define ATA_CMD_IDENTIFY 0xECu
#define ATA_CMD_READ_DMA_EXT 0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u

#define FIS_TYPE_REG_H2D 0x27u
#define FIS_H2D_COMMAND (1u << 7u)

/* Command list: 32 entries * 32 bytes, must be 1024-byte aligned.
   Command table (slot 0 only, this driver never issues more than one
   outstanding command): CFIS(64) + ACMD(16) + reserved(48) + one PRDT
   entry(16), 128-byte aligned. FIS receive area: 256 bytes, 256-byte
   aligned. All three fit inside AHCI_DMA_ARENA_BYTES (two 4KiB pages) with
   room to spare, laid out at fixed offsets so no runtime allocator is
   needed for this single-port, single-outstanding-command driver. */
#define CMD_LIST_OFFSET 0x0000u
#define FIS_RECEIVE_OFFSET 0x0400u
#define CMD_TABLE_OFFSET 0x0800u

struct ahci_command_header {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t prd_byte_count;
    uint32_t command_table_base;
    uint32_t command_table_base_upper;
    uint32_t reserved[4];
} __attribute__((packed));

struct ahci_fis_reg_h2d {
    uint8_t fis_type;
    uint8_t flags;
    uint8_t command;
    uint8_t feature_low;
    uint8_t lba0, lba1, lba2;
    uint8_t device;
    uint8_t lba3, lba4, lba5;
    uint8_t feature_high;
    uint8_t count_low, count_high;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved[4];
} __attribute__((packed));

struct ahci_prdt_entry {
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;
    uint32_t byte_count_and_interrupt;
} __attribute__((packed));

static volatile uint8_t *registers;
static uintptr_t port_base;
static uintptr_t dma_base;
static bool initialized;
static bool started;
static uint64_t sector_count;

static uint32_t read32(uint32_t offset) {
    return *(volatile uint32_t *)(registers + offset);
}

static void write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(registers + offset) = value;
}

static uint32_t port_read(uint32_t offset) {
    return read32((uint32_t)(port_base - (uintptr_t)registers) + offset);
}

static void port_write(uint32_t offset, uint32_t value) {
    write32((uint32_t)(port_base - (uintptr_t)registers) + offset, value);
}

/* Waits (bounded) for BSY and DRQ to both clear -- the standard "drive is
   idle and ready for a new command" gate every AHCI command issue and every
   port reset needs before touching PxCI/PxCMD. */
static bool wait_not_busy(void) {
    for (uint32_t spins = 0u; spins < 1000000u; ++spins) {
        if ((port_read(AHCI_PORT_TFD) & (AHCI_TFD_BSY | AHCI_TFD_DRQ)) == 0u)
            return true;
    }
    return false;
}

static void port_stop(void) {
    uint32_t cmd = port_read(AHCI_PORT_CMD);
    cmd &= ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE);
    port_write(AHCI_PORT_CMD, cmd);
    for (uint32_t spins = 0u; spins < 1000000u; ++spins) {
        if ((port_read(AHCI_PORT_CMD) & (AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) == 0u)
            return;
    }
}

static void port_start(void) {
    uint32_t cmd = port_read(AHCI_PORT_CMD);
    cmd |= AHCI_PORT_CMD_FRE;
    port_write(AHCI_PORT_CMD, cmd);
    cmd |= AHCI_PORT_CMD_ST;
    port_write(AHCI_PORT_CMD, cmd);
}

/* Finds the first port that both PI (implemented) and SSTS.DET report as a
   live SATA drive (device detected, PHY communication established) --
   PI alone only means the HBA wired the port up, not that anything is
   actually plugged into it. */
static bool select_port(uint32_t *port_index) {
    const uint32_t implemented = read32(AHCI_REG_PI);
    for (uint32_t index = 0u; index < 32u; ++index) {
        if ((implemented & (1u << index)) == 0u) continue;
        const uintptr_t candidate =
            (uintptr_t)registers + AHCI_PORT_BASE + index * AHCI_PORT_STRIDE;
        const uint32_t status =
            *(volatile uint32_t *)(candidate + AHCI_PORT_SSTS);
        if ((status & 0x0Fu) == 0x03u) {
            *port_index = index;
            return true;
        }
    }
    return false;
}

bool ahci_probe(const struct pci_device *device, uintptr_t mmio_base) {
    initialized = false;
    started = false;
    registers = NULL;
    sector_count = 0u;
    if (device == NULL || mmio_base == 0u) return false;
    registers = (volatile uint8_t *)mmio_base;

    write32(AHCI_REG_GHC, read32(AHCI_REG_GHC) | AHCI_GHC_AE);

    uint32_t port_index;
    if (!select_port(&port_index)) {
        registers = NULL;
        return false;
    }
    port_base = (uintptr_t)registers + AHCI_PORT_BASE +
        port_index * AHCI_PORT_STRIDE;

    port_stop();
    port_write(AHCI_PORT_SERR, 0xFFFFFFFFu);
    initialized = true;
    return true;
}

bool ahci_start(uintptr_t dma_arena, size_t arena_size) {
    if (!initialized || dma_arena == 0u || (dma_arena & 4095u) != 0u ||
        arena_size < AHCI_DMA_ARENA_BYTES) return false;
    dma_base = dma_arena;
    uint8_t *arena = (uint8_t *)dma_arena;
    for (size_t index = 0u; index < AHCI_DMA_ARENA_BYTES; ++index) arena[index] = 0u;

    const uintptr_t cmd_list = dma_arena + CMD_LIST_OFFSET;
    const uintptr_t fis_receive = dma_arena + FIS_RECEIVE_OFFSET;
    const uintptr_t cmd_table = dma_arena + CMD_TABLE_OFFSET;

    port_write(AHCI_PORT_CLB, (uint32_t)cmd_list);
    port_write(AHCI_PORT_CLBU, (uint32_t)(cmd_list >> 32u));
    port_write(AHCI_PORT_FB, (uint32_t)fis_receive);
    port_write(AHCI_PORT_FBU, (uint32_t)(fis_receive >> 32u));

    struct ahci_command_header *header = (struct ahci_command_header *)cmd_list;
    header[0].command_table_base = (uint32_t)cmd_table;
    header[0].command_table_base_upper = (uint32_t)(cmd_table >> 32u);

    port_start();

    if (!wait_not_busy()) {
        port_stop();
        return false;
    }

    /* IDENTIFY DEVICE into a scratch page borrowed from the tail of the
       arena (well past the command table/PRDT the read/write path also
       uses at CMD_TABLE_OFFSET, so this one-time probe read can't alias a
       live request). */
    uint8_t *identify_buffer = arena + AHCI_DMA_ARENA_BYTES - 512u;
    struct ahci_command_header *slot = &header[0];
    struct ahci_fis_reg_h2d *cfis = (struct ahci_fis_reg_h2d *)cmd_table;
    struct ahci_prdt_entry *prdt =
        (struct ahci_prdt_entry *)(cmd_table + 0x80u);

    for (size_t index = 0u; index < sizeof(*cfis); ++index)
        ((uint8_t *)cfis)[index] = 0u;
    cfis->fis_type = FIS_TYPE_REG_H2D;
    cfis->flags = FIS_H2D_COMMAND;
    cfis->command = ATA_CMD_IDENTIFY;
    cfis->device = 0u;

    const uintptr_t identify_phys = (uintptr_t)identify_buffer;
    prdt[0].data_base = (uint32_t)identify_phys;
    prdt[0].data_base_upper = (uint32_t)(identify_phys >> 32u);
    prdt[0].byte_count_and_interrupt = 511u; /* byte count - 1, per spec */

    slot->flags = (uint16_t)(sizeof(*cfis) / 4u); /* CFL in DWORDs */
    slot->prdt_length = 1u;
    slot->prd_byte_count = 0u;

    port_write(AHCI_PORT_CI, 1u);
    bool completed = false;
    for (uint32_t spins = 0u; spins < 2000000u; ++spins) {
        if ((port_read(AHCI_PORT_CI) & 1u) == 0u) { completed = true; break; }
    }
    if (!completed) {
        port_stop();
        return false;
    }

    /* IDENTIFY word 100-103 (byte offset 200) is the 48-bit LBA sector
       count, little-endian words -- the standard way to size a drive that
       reports LBA48 support (word 83 bit 10), which every AHCI-attached
       SATA disk QEMU/real hardware presents here does. */
    const uint16_t *words = (const uint16_t *)identify_buffer;
    sector_count = (uint64_t)words[100] | ((uint64_t)words[101] << 16u) |
        ((uint64_t)words[102] << 32u) | ((uint64_t)words[103] << 48u);

    started = true;
    return true;
}

static bool issue_data_command(uint64_t lba, uint32_t count, void *buffer,
                                bool write) {
    if (!started || count == 0u || count > AHCI_MAX_SECTORS_PER_REQUEST ||
        buffer == NULL) return false;
    if (!wait_not_busy()) return false;

    struct ahci_command_header *header =
        (struct ahci_command_header *)(dma_base + CMD_LIST_OFFSET);
    const uintptr_t cmd_table = dma_base + CMD_TABLE_OFFSET;
    struct ahci_fis_reg_h2d *cfis = (struct ahci_fis_reg_h2d *)cmd_table;
    struct ahci_prdt_entry *prdt =
        (struct ahci_prdt_entry *)(cmd_table + 0x80u);

    for (size_t index = 0u; index < sizeof(*cfis); ++index)
        ((uint8_t *)cfis)[index] = 0u;
    cfis->fis_type = FIS_TYPE_REG_H2D;
    cfis->flags = FIS_H2D_COMMAND;
    cfis->command = write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    cfis->device = 0x40u; /* LBA mode */
    cfis->lba0 = (uint8_t)lba;
    cfis->lba1 = (uint8_t)(lba >> 8u);
    cfis->lba2 = (uint8_t)(lba >> 16u);
    cfis->lba3 = (uint8_t)(lba >> 24u);
    cfis->lba4 = (uint8_t)(lba >> 32u);
    cfis->lba5 = (uint8_t)(lba >> 40u);
    cfis->count_low = (uint8_t)count;
    cfis->count_high = (uint8_t)(count >> 8u);

    const uintptr_t buffer_phys = (uintptr_t)buffer;
    prdt[0].data_base = (uint32_t)buffer_phys;
    prdt[0].data_base_upper = (uint32_t)(buffer_phys >> 32u);
    prdt[0].byte_count_and_interrupt = count * 512u - 1u;

    header[0].flags = (uint16_t)(sizeof(*cfis) / 4u) | (write ? (1u << 6u) : 0u);
    header[0].prdt_length = 1u;
    header[0].prd_byte_count = 0u;

    port_write(AHCI_PORT_CI, 1u);
    for (uint32_t spins = 0u; spins < 4000000u; ++spins) {
        if ((port_read(AHCI_PORT_CI) & 1u) == 0u) return true;
    }
    return false;
}

bool ahci_read_sectors(uint64_t lba, uint32_t count, void *buffer) {
    return issue_data_command(lba, count, buffer, false);
}

bool ahci_write_sectors(uint64_t lba, uint32_t count, const void *buffer) {
    return issue_data_command(lba, count, (void *)(uintptr_t)buffer, true);
}

bool ahci_ready(void) { return started; }
uint64_t ahci_sector_count(void) { return sector_count; }
uintptr_t ahci_mmio_base(void) { return (uintptr_t)registers; }
