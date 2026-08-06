#include <kernel/usb_uhci.h>
#include <kernel/interrupts.h>
#include <kernel/pci.h>
#include <kernel/serial.h>

// A from-scratch UHCI driver -- see usb_uhci.h's own comment for scope.
// Deliberately narrow: no descriptor parsing, no hub support. QEMU's own
// usb-hid.c tablet emulation is permissive enough to accept SET_ADDRESS and
// SET_CONFIGURATION with no descriptor reads in between, so enumeration
// here is just those two control transfers before the recurring interrupt
// poll starts -- the endpoint number, report format, and coordinate range
// below are QEMU's own well-known, stable constants for this device, not
// discovered from a configuration descriptor.

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void out16(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint16_t in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void out32(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

// UHCI I/O register offsets from the controller's I/O-space BAR.
#define UHCI_USBCMD 0x00u
#define UHCI_USBSTS 0x02u
#define UHCI_USBINTR 0x04u
#define UHCI_FRNUM 0x06u
#define UHCI_FRBASEADD 0x08u
#define UHCI_PORTSC1 0x10u
#define UHCI_PORTSC2 0x12u

#define UHCI_CMD_RUN 0x0001u
#define UHCI_CMD_HCRESET 0x0002u
#define UHCI_CMD_GRESET 0x0004u
#define UHCI_CMD_CONFIGURE 0x0040u

#define UHCI_PORTSC_CONNECTED 0x0001u
#define UHCI_PORTSC_CONNECT_CHANGE 0x0002u
#define UHCI_PORTSC_ENABLE 0x0004u
#define UHCI_PORTSC_ENABLE_CHANGE 0x0008u
#define UHCI_PORTSC_LOW_SPEED 0x0100u
#define UHCI_PORTSC_RESET 0x0200u

// Link-pointer flag bits (physical addresses are always >=16-byte aligned
// here, so the low 4 bits are free for these).
#define UHCI_PTR_TERMINATE 0x1u
#define UHCI_PTR_QH 0x2u
#define UHCI_PTR_DEPTH_FIRST 0x4u

// TD control/status word (word 1).
#define TD_CTRL_ACTUAL_LENGTH_MASK 0x000007FFu
#define TD_CTRL_BITSTUFF (1u << 17)
#define TD_CTRL_CRC_TIMEOUT (1u << 18)
#define TD_CTRL_NAK (1u << 19)
#define TD_CTRL_BABBLE (1u << 20)
#define TD_CTRL_BUFFER_ERROR (1u << 21)
#define TD_CTRL_STALLED (1u << 22)
#define TD_CTRL_ACTIVE (1u << 23)
#define TD_CTRL_LOW_SPEED (1u << 26)
#define TD_CTRL_ERROR_LIMIT (3u << 27)
#define TD_CTRL_SPD (1u << 29)
#define TD_CTRL_ERROR_BITS (TD_CTRL_BITSTUFF | TD_CTRL_CRC_TIMEOUT | \
    TD_CTRL_BABBLE | TD_CTRL_BUFFER_ERROR | TD_CTRL_STALLED)

// TD token word (word 2).
#define USB_PID_OUT 0xE1u
#define USB_PID_IN 0x69u
#define USB_PID_SETUP 0x2Du
#define TD_TOKEN_DEVICE_SHIFT 8u
#define TD_TOKEN_ENDPOINT_SHIFT 15u
#define TD_TOKEN_TOGGLE_SHIFT 19u
#define TD_TOKEN_MAXLEN_SHIFT 21u

// Standard USB requests this driver actually issues.
#define USB_REQUEST_SET_ADDRESS 5u
#define USB_REQUEST_SET_CONFIGURATION 9u

// The tablet's report is 6 real bytes (buttons, x_lo, x_hi, y_lo, y_hi,
// wheel) -- QEMU's own hid.c pointer-event layout for an absolute device.
// Some QEMU versions pad a 7th (hwheel) byte; reading up to 8 and only
// interpreting the first 6 tolerates either. Endpoint 1 IN is QEMU's fixed
// interrupt endpoint for every HID device it emulates.
#define TABLET_ENDPOINT 1u
#define TABLET_REPORT_BYTES 8u
#define TABLET_DEVICE_ADDRESS 1u
#define TABLET_AXIS_MAX 0x7FFFu

struct uhci_qh {
    uint32_t head_link;
    uint32_t element_link;
} __attribute__((aligned(16)));

struct uhci_td {
    uint32_t link;
    uint32_t control_status;
    uint32_t token;
    uint32_t buffer;
} __attribute__((aligned(16)));

struct usb_setup_packet {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} __attribute__((packed));

static bool present;
static uint16_t io_base;
static uint32_t *frame_list;
static struct uhci_qh *control_qh;
static struct uhci_td *control_td0;
static struct uhci_td *control_td1;
static struct uhci_qh *interrupt_qh;
static struct uhci_td *interrupt_td;
static uint8_t *setup_buffer;
static uint8_t *report_buffer;
static uint8_t report_toggle;
static bool poll_active;
static uint32_t screen_bound_x = 639u;
static uint32_t screen_bound_y = 479u;

static uint32_t phys32(const void *pointer) { return (uint32_t)(uintptr_t)pointer; }

static uint32_t make_token(uint8_t pid, uint8_t device, uint8_t endpoint,
                           uint8_t toggle, uint16_t length) {
    const uint32_t max_length_field = length == 0u ? 0x7FFu : (uint32_t)(length - 1u);
    return (uint32_t)pid | ((uint32_t)device << TD_TOKEN_DEVICE_SHIFT) |
        ((uint32_t)endpoint << TD_TOKEN_ENDPOINT_SHIFT) |
        ((uint32_t)(toggle & 1u) << TD_TOKEN_TOGGLE_SHIFT) |
        (max_length_field << TD_TOKEN_MAXLEN_SHIFT);
}

static uint32_t make_control_status(bool low_speed) {
    return TD_CTRL_ACTIVE | TD_CTRL_SPD | TD_CTRL_ERROR_LIMIT |
        (low_speed ? TD_CTRL_LOW_SPEED : 0u);
}

// Polls one TD's Active bit until it clears or `ticks` (100 Hz timer ticks,
// see interrupts_timer_ticks) pass with no progress. Returns false on a
// real transfer error (stall/CRC/babble/buffer) as well as on timeout, so
// every caller can just check the return value without separately
// inspecting control_status itself.
static bool wait_for_td(const struct uhci_td *td, uint64_t ticks) {
    const uint64_t deadline = interrupts_timer_ticks() + ticks;
    while ((td->control_status & TD_CTRL_ACTIVE) != 0u) {
        if (interrupts_timer_ticks() >= deadline) return false;
    }
    return (td->control_status & TD_CTRL_ERROR_BITS) == 0u;
}

// SET_ADDRESS and SET_CONFIGURATION are both zero-data-stage requests: a
// SETUP packet, then a zero-length IN status packet, chained depth-first so
// the host controller runs both in the same frame without this function
// polling twice. device_addr is the address the request is SENT to (0 for
// SET_ADDRESS, since the device hasn't taken its new address yet).
static bool control_request_no_data(uint8_t device_addr, bool low_speed,
                                    uint8_t request, uint16_t value) {
    struct usb_setup_packet *setup = (struct usb_setup_packet *)setup_buffer;
    setup->request_type = 0x00u; // host-to-device, standard, device recipient
    setup->request = request;
    setup->value = value;
    setup->index = 0u;
    setup->length = 0u;

    control_td1->link = UHCI_PTR_TERMINATE;
    control_td1->control_status = make_control_status(low_speed);
    control_td1->token = make_token(USB_PID_IN, device_addr, 0u, 1u, 0u);
    control_td1->buffer = 0u;

    control_td0->link = phys32(control_td1) | UHCI_PTR_DEPTH_FIRST;
    control_td0->control_status = make_control_status(low_speed);
    control_td0->token = make_token(USB_PID_SETUP, device_addr, 0u, 0u, 8u);
    control_td0->buffer = phys32(setup_buffer);

    control_qh->element_link = phys32(control_td0);
    if (!wait_for_td(control_td0, 50u)) {
        control_qh->element_link = UHCI_PTR_TERMINATE;
        return false;
    }
    const bool ok = wait_for_td(control_td1, 50u);
    control_qh->element_link = UHCI_PTR_TERMINATE;
    return ok;
}

static void port_write(uint16_t port, uint16_t value) { out16(port, value); }

// Resets and enables one root port, returning true if a device answered.
// UHCI's reset/enable dance (set RESET, hold >=50ms per spec, clear it,
// give the device a moment, then set ENABLE) needs real wall-clock time,
// not just register pokes -- interrupts_timer_ticks() runs at 100 Hz (see
// pit_start_100hz), so 5/2 ticks below are the real 50ms/20ms this needs.
static bool reset_port(uint16_t port) {
    if ((in16(port) & UHCI_PORTSC_CONNECTED) == 0u) return false;
    port_write(port, UHCI_PORTSC_RESET);
    const uint64_t reset_deadline = interrupts_timer_ticks() + 5u;
    while (interrupts_timer_ticks() < reset_deadline) { }
    port_write(port, 0u);
    const uint64_t settle_deadline = interrupts_timer_ticks() + 2u;
    while (interrupts_timer_ticks() < settle_deadline) { }
    port_write(port, UHCI_PORTSC_ENABLE);
    const uint64_t enable_deadline = interrupts_timer_ticks() + 2u;
    while (interrupts_timer_ticks() < enable_deadline) { }
    return (in16(port) & UHCI_PORTSC_ENABLE) != 0u;
}

bool usb_uhci_start(uintptr_t dma_arena, size_t arena_size,
                    uint32_t screen_width, uint32_t screen_height) {
    present = false;
    if (dma_arena == 0u || arena_size < 8192u || (dma_arena & 0xFFFu) != 0u)
        return false;
    if (screen_width > 0u) screen_bound_x = screen_width - 1u;
    if (screen_height > 0u) screen_bound_y = screen_height - 1u;

    struct pci_device device;
    if (!pci_find_class(0x0Cu, 0x03u, 0u, &device) ||
        device.programming_interface != 0x00u)
        return false;

    uint16_t candidate_io_base = 0u;
    for (size_t bar = 0u; bar < 6u; ++bar) {
        if ((device.bars[bar] & 0x1u) != 0u) {
            candidate_io_base = (uint16_t)(device.bars[bar] & 0xFFFCu);
            break;
        }
    }
    if (candidate_io_base == 0u) return false;
    io_base = candidate_io_base;

    const uint32_t command = pci_config_read32(device.bus, device.slot, device.function, 0x04u);
    pci_config_write32(device.bus, device.slot, device.function, 0x04u,
        command | 0x0005u); // bit0 I/O space, bit2 bus master

    uint8_t *arena = (uint8_t *)dma_arena;
    for (size_t i = 0u; i < arena_size; ++i) arena[i] = 0u;
    frame_list = (uint32_t *)arena;
    control_qh = (struct uhci_qh *)(arena + 4096u);
    control_td0 = (struct uhci_td *)(arena + 4096u + 16u);
    control_td1 = (struct uhci_td *)(arena + 4096u + 32u);
    interrupt_qh = (struct uhci_qh *)(arena + 4096u + 48u);
    interrupt_td = (struct uhci_td *)(arena + 4096u + 64u);
    setup_buffer = arena + 4096u + 128u;
    report_buffer = arena + 4096u + 144u;

    // Global + host-controller reset, then make sure the controller is
    // stopped and not generating real IRQs (see usb_uhci_poll's own comment
    // on why this driver is polled, matching e1000/ahci's precedent).
    out16(io_base + UHCI_USBCMD, UHCI_CMD_GRESET);
    const uint64_t greset_deadline = interrupts_timer_ticks() + 2u;
    while (interrupts_timer_ticks() < greset_deadline) { }
    out16(io_base + UHCI_USBCMD, 0u);
    out16(io_base + UHCI_USBINTR, 0u);
    out16(io_base + UHCI_USBCMD, UHCI_CMD_HCRESET);
    const uint64_t hcreset_deadline = interrupts_timer_ticks() + 5u;
    while ((in16(io_base + UHCI_USBCMD) & UHCI_CMD_HCRESET) != 0u) {
        if (interrupts_timer_ticks() >= hcreset_deadline) return false;
    }

    // Permanent schedule: every one of the 1024 frame list slots points at
    // control_qh, which links horizontally to interrupt_qh. control_qh's
    // own element (the actual TD it runs) sits empty except during a
    // control_request_no_data call; interrupt_qh's element is the one
    // recurring report TD usb_uhci_poll re-arms forever. This never
    // changes after setup, so there is no separate "install the interrupt
    // transfer" step later.
    control_qh->head_link = phys32(interrupt_qh) | UHCI_PTR_QH;
    control_qh->element_link = UHCI_PTR_TERMINATE;
    interrupt_qh->head_link = UHCI_PTR_TERMINATE;
    interrupt_qh->element_link = UHCI_PTR_TERMINATE;
    for (size_t i = 0u; i < 1024u; ++i) frame_list[i] = phys32(control_qh) | UHCI_PTR_QH;

    out32(io_base + UHCI_FRBASEADD, (uint32_t)dma_arena);
    out16(io_base + UHCI_FRNUM, 0u);
    out16(io_base + UHCI_USBCMD, UHCI_CMD_RUN | UHCI_CMD_CONFIGURE);

    bool low_speed = false;
    uint16_t device_port = 0u;
    if (reset_port(io_base + UHCI_PORTSC1)) {
        device_port = io_base + UHCI_PORTSC1;
    } else if (reset_port(io_base + UHCI_PORTSC2)) {
        device_port = io_base + UHCI_PORTSC2;
    } else {
        serial_write("USB_UHCI_NO_DEVICE\n");
        return false;
    }
    low_speed = (in16(device_port) & UHCI_PORTSC_LOW_SPEED) != 0u;

    if (!control_request_no_data(0u, low_speed, USB_REQUEST_SET_ADDRESS,
                                 TABLET_DEVICE_ADDRESS)) {
        serial_write("USB_UHCI_SET_ADDRESS_FAILED\n");
        return false;
    }
    const uint64_t address_settle = interrupts_timer_ticks() + 1u;
    while (interrupts_timer_ticks() < address_settle) { }
    if (!control_request_no_data(TABLET_DEVICE_ADDRESS, low_speed,
                                 USB_REQUEST_SET_CONFIGURATION, 1u)) {
        serial_write("USB_UHCI_SET_CONFIGURATION_FAILED\n");
        return false;
    }

    interrupt_td->link = UHCI_PTR_TERMINATE;
    interrupt_td->control_status = make_control_status(low_speed);
    interrupt_td->token = make_token(USB_PID_IN, TABLET_DEVICE_ADDRESS,
        TABLET_ENDPOINT, report_toggle, TABLET_REPORT_BYTES);
    interrupt_td->buffer = phys32(report_buffer);
    interrupt_qh->element_link = phys32(interrupt_td);
    poll_active = true;

    present = true;
    serial_write("USB_UHCI_TABLET_READY low_speed=");
    serial_write(low_speed ? "1" : "0");
    serial_write("\n");
    return true;
}

bool usb_uhci_present(void) { return present; }

void usb_uhci_poll(void) {
    if (!present || !poll_active) return;
    const uint32_t status = interrupt_td->control_status;
    if ((status & TD_CTRL_ACTIVE) != 0u) return; // still in flight, or NAKed and retrying
    if ((status & TD_CTRL_ERROR_BITS) == 0u) {
        const uint8_t buttons = report_buffer[0];
        const uint16_t raw_x = (uint16_t)report_buffer[1] | ((uint16_t)report_buffer[2] << 8u);
        const uint16_t raw_y = (uint16_t)report_buffer[3] | ((uint16_t)report_buffer[4] << 8u);
        const int32_t screen_x = (int32_t)(((uint32_t)raw_x * screen_bound_x) / TABLET_AXIS_MAX);
        const int32_t screen_y = (int32_t)(((uint32_t)raw_y * screen_bound_y) / TABLET_AXIS_MAX);
        mouse_report_absolute(screen_x, screen_y, buttons & 0x07u);
        report_toggle ^= 1u;
    }
    // Re-arm for the next report regardless of error/success -- a NAK
    // (no new report yet) also clears Active with TD_CTRL_ERROR_BITS unset
    // and TD_CTRL_NAK set, which this branch already tolerates since NAK
    // isn't in TD_CTRL_ERROR_BITS; a real error just retries with the same
    // toggle, matching normal USB retry semantics.
    interrupt_td->token = make_token(USB_PID_IN, TABLET_DEVICE_ADDRESS,
        TABLET_ENDPOINT, report_toggle, TABLET_REPORT_BYTES);
    interrupt_td->control_status = make_control_status(false);
}
