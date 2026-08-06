#include <kernel/interrupts.h>
#include <kernel/scheduler.h>
#include <kernel/serial.h>
#include <kernel/terminal.h>
#include <kernel/usb_uhci.h>
#include <demon/input.h>

#include <stddef.h>
#include <stdint.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void interrupt_breakpoint_stub(void);
extern void interrupt_page_fault_stub(void);
extern void interrupt_timer_stub(void);
extern void interrupt_keyboard_stub(void);
extern void interrupt_mouse_stub(void);
extern void interrupt_syscall_stub(void);

static struct idt_entry idt[256] __attribute__((aligned(16)));
static volatile uint64_t breakpoint_count;
static volatile uint64_t timer_ticks;
static volatile char keyboard_buffer[64];
static volatile uint8_t keyboard_read;
static volatile uint8_t keyboard_write;
static volatile uint64_t keyboard_irqs;
static volatile uint64_t keyboard_characters;
static bool keyboard_ready;
static bool keyboard_shift;
static bool keyboard_caps_lock;
static bool keyboard_alt;
static bool keyboard_super;
static bool keyboard_ctrl;
static bool keyboard_extended;
static volatile uint64_t mouse_irqs;
static volatile uint64_t mouse_packets;
static bool mouse_ready;
static bool mouse_wheel;
static uint8_t mouse_packet[4];
static uint8_t mouse_packet_index;
static void drain_mouse_bytes(uint32_t limit);
static uint8_t mouse_button_state;
static int32_t pointer_x;
static int32_t pointer_y;
static int32_t pointer_max_x = 639;
static int32_t pointer_max_y = 479;

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void io_wait(void) { out8(0x80u, 0u); }

static void set_gate_with_attributes(uint8_t vector, void (*handler)(void), uint8_t attributes) {
    const uintptr_t address = (uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = 0x08u;
    idt[vector].ist = 0u;
    idt[vector].attributes = attributes;
    idt[vector].offset_middle = (uint16_t)(address >> 16u);
    idt[vector].offset_high = (uint32_t)(address >> 32u);
    idt[vector].reserved = 0u;
}

static void set_gate(uint8_t vector, void (*handler)(void)) {
    set_gate_with_attributes(vector, handler, 0x8Eu);
}

void interrupts_init(void) {
    for (size_t i = 0; i < 256u; ++i) idt[i] = (struct idt_entry){0};
    set_gate(3u, interrupt_breakpoint_stub);
    set_gate(14u, interrupt_page_fault_stub);
    set_gate(32u, interrupt_timer_stub);
    set_gate(33u, interrupt_keyboard_stub);
    set_gate(44u, interrupt_mouse_stub);
    set_gate_with_attributes(128u, interrupt_syscall_stub, 0xEEu);
    const struct idt_pointer pointer = {
        .limit = (uint16_t)(sizeof(idt) - 1u),
        .base = (uintptr_t)&idt[0],
    };
    __asm__ volatile ("lidt %0" : : "m"(pointer) : "memory");
}

static void pic_remap(void) {
    out8(0x20u, 0x11u); io_wait();
    out8(0xA0u, 0x11u); io_wait();
    out8(0x21u, 0x20u); io_wait();
    out8(0xA1u, 0x28u); io_wait();
    out8(0x21u, 0x04u); io_wait();
    out8(0xA1u, 0x02u); io_wait();
    out8(0x21u, 0x01u); io_wait();
    out8(0xA1u, 0x01u); io_wait();
    out8(0x21u, 0xF8u);
    out8(0xA1u, 0xEFu);
}

static void pit_start_100hz(void) {
    const uint16_t divisor = 11932u;
    out8(0x43u, 0x36u);
    out8(0x40u, (uint8_t)divisor);
    out8(0x40u, (uint8_t)(divisor >> 8u));
}

static bool ps2_wait_input_empty(void) {
    for (uint32_t attempts = 0u; attempts < 100000u; ++attempts)
        if ((in8(0x64u) & 0x02u) == 0u) return true;
    return false;
}

static bool ps2_wait_output_full(void) {
    for (uint32_t attempts = 0u; attempts < 100000u; ++attempts)
        if ((in8(0x64u) & 0x01u) != 0u) return true;
    return false;
}

static bool ps2_command(uint8_t command) {
    if (!ps2_wait_input_empty()) return false;
    out8(0x64u, command);
    return true;
}

static bool ps2_data(uint8_t value) {
    if (!ps2_wait_input_empty()) return false;
    out8(0x60u, value);
    return true;
}

static bool ps2_read(uint8_t *value) {
    if (value == NULL || !ps2_wait_output_full()) return false;
    *value = in8(0x60u);
    return true;
}

static bool ps2_mouse_command(uint8_t command, uint8_t *reply) {
    uint8_t response;
    if (!ps2_command(0xD4u) || !ps2_data(command) || !ps2_read(&response) || response != 0xFAu)
        return false;
    if (reply != NULL && !ps2_read(reply)) return false;
    return true;
}

static void ps2_flush_output(void) {
    for (unsigned reads = 0u; reads < 32u && (in8(0x64u) & 0x01u) != 0u; ++reads)
        (void)in8(0x60u);
}

static bool ps2_keyboard_start(void) {
    /* GRUB may leave the first PS/2 port disabled. Configure it explicitly,
       request set-1 translation, enable IRQ1, then enable keyboard scanning. */
    if (!ps2_command(0xADu) || !ps2_command(0xA7u)) return false;
    ps2_flush_output();
    if (!ps2_command(0x20u) || !ps2_wait_output_full()) return false;
    uint8_t configuration = in8(0x60u);
    configuration |= 0x43u;
    configuration &= (uint8_t)~0x30u;
    if (!ps2_command(0x60u) || !ps2_data(configuration)) return false;
    if (!ps2_command(0xAEu) || !ps2_command(0xA8u)) return false;
    if (!ps2_data(0xF4u) || !ps2_wait_output_full()) return false;
    if (in8(0x60u) != 0xFAu) return false;
    keyboard_read = 0u;
    keyboard_write = 0u;
    keyboard_shift = false;
    keyboard_caps_lock = false;
    keyboard_alt = false;
    keyboard_super = false;
    keyboard_ctrl = false;
    keyboard_extended = false;
    return true;
}

static bool ps2_mouse_rate(uint8_t rate) {
    return ps2_mouse_command(0xF3u, NULL) && ps2_mouse_command(rate, NULL);
}

static bool ps2_mouse_start(void) {
    uint8_t device_id = 0u;
    if (!ps2_mouse_command(0xF6u, NULL)) return false;
    /* IntelliMouse negotiation is harmless on a standard three-byte mouse. */
    if (ps2_mouse_rate(200u) && ps2_mouse_rate(100u) && ps2_mouse_rate(80u) &&
        ps2_mouse_command(0xF2u, &device_id))
        mouse_wheel = device_id == 3u || device_id == 4u;
    /* Keep the device's default resolution: QEMU already converts host motion
       into guest counts and can counter-scale an explicit resolution change,
       making the visible cursor unexpectedly slow. Retain 1:1 device scaling
       and the low-latency 200 Hz sample rate. */
    if (!ps2_mouse_command(0xE6u, NULL) || !ps2_mouse_rate(200u)) return false;
    if (!ps2_mouse_command(0xF4u, NULL)) return false;
    mouse_packet_index = 0u;
    mouse_button_state = 0u;
    pointer_x = pointer_max_x / 2;
    pointer_y = pointer_max_y / 2;
    return true;
}

static void publish_key(uint16_t code, bool released, char character) {
    struct input_event event = {.timestamp = timer_ticks,
        .type = released ? INPUT_KEY_UP : INPUT_KEY_DOWN, .code = code,
        .value = (unsigned char)character,
        .modifiers = (keyboard_shift ? INPUT_MOD_SHIFT : 0u) |
            (keyboard_caps_lock ? INPUT_MOD_CAPS_LOCK : 0u) |
            (keyboard_alt ? INPUT_MOD_ALT : 0u) |
            (keyboard_super ? INPUT_MOD_SUPER : 0u) |
            (keyboard_ctrl ? INPUT_MOD_CTRL : 0u)};
    (void)input_publish(&event);
}

static void keyboard_queue_char(char character) {
    const uint8_t next = (uint8_t)((keyboard_write + 1u) % sizeof(keyboard_buffer));
    if (next == keyboard_read) return;
    keyboard_buffer[keyboard_write] = character;
    keyboard_write = next;
    ++keyboard_characters;
}

uintptr_t interrupt_timer_handler(uintptr_t frame_address) {
    ++timer_ticks;
    /* Some emulators expose the PS/2 auxiliary output as level-triggered
       state behind an edge-triggered legacy PIC. If more bytes arrive after
       IRQ12's handler observed an empty controller, the output-full bit can
       remain asserted without another usable edge. Pumping a small bounded
       number of auxiliary bytes from the timer prevents the cursor from
       moving once and then stalling. This is not polling the pointer state:
       it only drains bytes the controller already reports as available. */
    drain_mouse_bytes(8u);
    usb_uhci_poll();
    const uintptr_t next_frame = scheduler_on_timer_tick(frame_address);
    out8(0x20u, 0x20u);
    return next_frame;
}

void interrupt_keyboard_handler(void) {
    static const char unshifted[128] = {
        [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',
        [0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',[0x0C]='-',[0x0D]='=',
        [0x0E]='\b',[0x0F]='\t',[0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',
        [0x14]='t',[0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
        [0x1A]='[',[0x1B]=']',[0x1C]='\n',[0x1E]='a',[0x1F]='s',[0x20]='d',
        [0x21]='f',[0x22]='g',[0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',
        [0x27]=';',[0x28]='\'',[0x29]='`',[0x2B]='\\',[0x2C]='z',[0x2D]='x',
        [0x2E]='c',[0x2F]='v',[0x30]='b',[0x31]='n',[0x32]='m',[0x33]=',',
        [0x34]='.',[0x35]='/',[0x39]=' '
    };
    static const char shifted[128] = {
        [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',[0x07]='^',
        [0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',[0x0C]='_',[0x0D]='+',
        [0x0E]='\b',[0x0F]='\t',[0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',
        [0x14]='T',[0x15]='Y',[0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',
        [0x1A]='{',[0x1B]='}',[0x1C]='\n',[0x1E]='A',[0x1F]='S',[0x20]='D',
        [0x21]='F',[0x22]='G',[0x23]='H',[0x24]='J',[0x25]='K',[0x26]='L',
        [0x27]=':',[0x28]='"',[0x29]='~',[0x2B]='|',[0x2C]='Z',[0x2D]='X',
        [0x2E]='C',[0x2F]='V',[0x30]='B',[0x31]='N',[0x32]='M',[0x33]='<',
        [0x34]='>',[0x35]='?',[0x39]=' '
    };
    ++keyboard_irqs;
    if ((in8(0x64u) & 0x01u) == 0u) {
        out8(0x20u, 0x20u);
        return;
    }
    const uint8_t scan = in8(0x60u);
    if (scan == 0xE0u) {
        keyboard_extended = true;
        out8(0x20u, 0x20u);
        return;
    }
    if (keyboard_extended) {
        keyboard_extended = false;
        const bool released = (scan & 0x80u) != 0u;
        const uint8_t code = (uint8_t)(scan & 0x7Fu);
        if (code == 0x5Bu || code == 0x5Cu) keyboard_super = !released;
        if (code == 0x38u) keyboard_alt = !released;
        if (code == 0x1Du) keyboard_ctrl = !released;
        if (!released && code == 0x48u)
            keyboard_queue_char(KEYBOARD_CHAR_HISTORY_UP);
        if (!released && code == 0x50u)
            keyboard_queue_char(KEYBOARD_CHAR_HISTORY_DOWN);
        publish_key((uint16_t)(0x100u | code), released, '\0');
        out8(0x20u, 0x20u);
        return;
    }
    const bool released = (scan & 0x80u) != 0u;
    const uint8_t code = (uint8_t)(scan & 0x7Fu);
    if (code == 0x2Au || code == 0x36u) {
        keyboard_shift = !released;
        publish_key(code, released, '\0');
        out8(0x20u, 0x20u);
        return;
    }
    if (code == 0x38u) {
        keyboard_alt = !released;
        publish_key(code, released, '\0');
        out8(0x20u, 0x20u);
        return;
    }
    if (code == 0x1Du) {
        keyboard_ctrl = !released;
        publish_key(code, released, '\0');
        out8(0x20u, 0x20u);
        return;
    }
    if (code == 0x3Au && !released) keyboard_caps_lock = !keyboard_caps_lock;
    char character = '\0';
    if (!released && code < 128u && unshifted[code] != '\0') {
        const bool letter = unshifted[code] >= 'a' && unshifted[code] <= 'z';
        const bool use_shift = letter ? keyboard_shift != keyboard_caps_lock : keyboard_shift;
        character = use_shift ? shifted[code] : unshifted[code];
        keyboard_queue_char(character);
    }
    publish_key(code, released, character);
    out8(0x20u, 0x20u);
}

static void publish_mouse_button(uint8_t mask, uint16_t code, uint8_t buttons) {
    bool was_down = (mouse_button_state & mask) != 0u;
    bool is_down = (buttons & mask) != 0u;
    if (was_down == is_down) return;
    struct input_event event = {.timestamp = timer_ticks,
        .type = is_down ? INPUT_MOUSE_BUTTON_DOWN : INPUT_MOUSE_BUTTON_UP,
        .code = code, .x = pointer_x, .y = pointer_y, .value = is_down ? 1 : 0};
    (void)input_publish(&event);
}

static void decode_mouse_packet(void) {
    uint8_t flags = mouse_packet[0];
    if ((flags & 0xC0u) != 0u) return;
    /* Reject a packet whose sign bits disagree with its movement bytes. If a
       byte was lost, this prevents a malformed partial packet from becoming
       a large visible jump; the IRQ collector then resynchronizes at the next
       first byte carrying the mandatory always-one bit. */
    if (((flags & 0x10u) != 0u) != ((mouse_packet[1] & 0x80u) != 0u) ||
        ((flags & 0x20u) != 0u) != ((mouse_packet[2] & 0x80u) != 0u)) return;
    int32_t dx = (int32_t)(int8_t)mouse_packet[1];
    int32_t dy = -(int32_t)(int8_t)mouse_packet[2];
    const int32_t magnitude_x = dx < 0 ? -dx : dx;
    const int32_t magnitude_y = dy < 0 ? -dy : dy;
    const int32_t magnitude = magnitude_x > magnitude_y ? magnitude_x : magnitude_y;
    /* A compact, deterministic acceleration curve for a 640x480 desktop.
       Single-count motion remains pixel-precise; ordinary motion is doubled;
       fast motion is tripled so crossing the screen does not require lifting
       and recapturing the host mouse repeatedly. */
    int32_t scale = 1;
    if (magnitude >= 2) scale = 2;
    if (magnitude >= 7) scale = 3;
    dx *= scale;
    dy *= scale;
    int32_t next_x = pointer_x + dx, next_y = pointer_y + dy;
    if (next_x < 0) next_x = 0; else if (next_x > pointer_max_x) next_x = pointer_max_x;
    if (next_y < 0) next_y = 0; else if (next_y > pointer_max_y) next_y = pointer_max_y;
    if (dx != 0 || dy != 0) {
        pointer_x = next_x; pointer_y = next_y;
        struct input_event event = {.timestamp = timer_ticks, .type = INPUT_MOUSE_MOVE,
            .x = pointer_x, .y = pointer_y, .delta_x = dx, .delta_y = dy};
        (void)input_publish(&event);
    }
    uint8_t buttons = flags & 0x07u;
    publish_mouse_button(1u, INPUT_MOUSE_LEFT, buttons);
    publish_mouse_button(2u, INPUT_MOUSE_RIGHT, buttons);
    publish_mouse_button(4u, INPUT_MOUSE_MIDDLE, buttons);
    mouse_button_state = buttons;
    if (mouse_wheel) {
        int32_t scroll = (int32_t)(int8_t)(mouse_packet[3] << 4u) >> 4u;
        if (scroll != 0) {
            struct input_event event = {.timestamp = timer_ticks, .type = INPUT_MOUSE_SCROLL,
                .x = pointer_x, .y = pointer_y, .value = scroll};
            (void)input_publish(&event);
        }
    }
    ++mouse_packets;
}

void mouse_report_absolute(int32_t x, int32_t y, uint8_t buttons) {
    if (x < 0) x = 0; else if (x > pointer_max_x) x = pointer_max_x;
    if (y < 0) y = 0; else if (y > pointer_max_y) y = pointer_max_y;
    if (x != pointer_x || y != pointer_y) {
        const int32_t dx = x - pointer_x;
        const int32_t dy = y - pointer_y;
        pointer_x = x;
        pointer_y = y;
        struct input_event event = {.timestamp = timer_ticks, .type = INPUT_MOUSE_MOVE,
            .x = pointer_x, .y = pointer_y, .delta_x = dx, .delta_y = dy};
        (void)input_publish(&event);
    }
    publish_mouse_button(1u, INPUT_MOUSE_LEFT, buttons);
    publish_mouse_button(2u, INPUT_MOUSE_RIGHT, buttons);
    publish_mouse_button(4u, INPUT_MOUSE_MIDDLE, buttons);
    mouse_button_state = buttons;
}

static void drain_mouse_bytes(uint32_t limit) {
    for (uint32_t drained = 0u; drained < limit; ++drained) {
        const uint8_t status = in8(0x64u);
        if ((status & 0x01u) == 0u || (status & 0x20u) == 0u) break;
        const uint8_t byte = in8(0x60u);
        if (mouse_packet_index != 0u || (byte & 0x08u) != 0u) {
            mouse_packet[mouse_packet_index++] = byte;
            const uint8_t length = mouse_wheel ? 4u : 3u;
            if (mouse_packet_index == length) {
                mouse_packet_index = 0u;
                decode_mouse_packet();
            }
        }
    }
}

void interrupt_mouse_handler(void) {
    ++mouse_irqs;
    /* Drain every auxiliary byte already queued by the controller. Reading a
       single byte per edge can strand the remaining packet bytes after a
       longer userspace/compositor timeslice (notably during terminal startup):
       the output-full line stays asserted, so no new IRQ edge is guaranteed.
       The bound prevents a broken controller from trapping us in IRQ context. */
    drain_mouse_bytes(32u);
    out8(0xA0u, 0x20u);
    out8(0x20u, 0x20u);
}

bool interrupts_hardware_start(void) {
    pic_remap();
    input_init();
    keyboard_ready = ps2_keyboard_start();
    if (!keyboard_ready) return false;
    mouse_ready = ps2_mouse_start();
    pit_start_100hz();
    const uint64_t before = timer_ticks;
    __asm__ volatile ("sti");
    for (unsigned waits = 0; waits < 8u && timer_ticks == before; ++waits)
        __asm__ volatile ("hlt");
    return timer_ticks > before;
}

uint64_t interrupts_timer_ticks(void) { return timer_ticks; }

bool keyboard_read_char(char *value) {
    if (keyboard_read == keyboard_write) return false;
    *value = keyboard_buffer[keyboard_read];
    keyboard_read = (uint8_t)((keyboard_read + 1u) % sizeof(keyboard_buffer));
    return true;
}

void keyboard_discard_chars(void) {
    /* The IRQ handler and MakoBox share this small ring. Foreground graphical
       applications consume unified input events instead of shell characters,
       so discard the mirrored characters atomically when ownership changes. */
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    keyboard_read = keyboard_write;
    if ((flags & (1ull << 9u)) != 0u) __asm__ volatile ("sti" ::: "memory");
}

bool keyboard_controller_ready(void) { return keyboard_ready; }
uint64_t keyboard_irq_count(void) { return keyboard_irqs; }
uint64_t keyboard_character_count(void) { return keyboard_characters; }
bool mouse_controller_ready(void) { return mouse_ready; }
bool mouse_scroll_ready(void) { return mouse_wheel; }
uint64_t mouse_irq_count(void) { return mouse_irqs; }
uint64_t mouse_packet_count(void) { return mouse_packets; }
int32_t mouse_x(void) { return pointer_x; }
int32_t mouse_y(void) { return pointer_y; }
uint8_t mouse_buttons(void) { return mouse_button_state; }
void mouse_set_bounds(uint32_t width, uint32_t height) {
    if (width == 0u || height == 0u || width > 32768u || height > 32768u) return;
    pointer_max_x = (int32_t)width - 1; pointer_max_y = (int32_t)height - 1;
    if (pointer_x > pointer_max_x) pointer_x = pointer_max_x;
    if (pointer_y > pointer_max_y) pointer_y = pointer_max_y;
}

void interrupt_breakpoint_handler(void) {
    ++breakpoint_count;
}

bool interrupts_breakpoint_self_test(void) {
    const uint64_t before = breakpoint_count;
    __asm__ volatile ("int3" : : : "memory");
    return breakpoint_count == before + 1u;
}

__attribute__((noreturn))
void interrupt_page_fault_handler(uint64_t error_code, const uint64_t *stub_rsp) {
    uintptr_t fault_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
    serial_write("\nPAGE FAULT: address=");
    serial_write_hex(fault_address);
    serial_write(" error=");
    serial_write_hex(error_code);
    serial_write(" pid=");
    serial_write_u64(scheduler_current_pid());
    serial_write("\n");
    serial_write("CROSS_CHECK stub_rsp[15]="); serial_write_hex(stub_rsp[15]);
    serial_write(" (should equal error above)\n");

    /* stub_rsp = %rsp captured immediately after PUSH_GPRS in
       interrupt_page_fault_stub -- i.e. it points at the FIRST (oldest,
       lowest-address) pushed GPR, not at the CPU-pushed exception frame.
       PUSH_GPRS pushes rax first (highest address among the 15 GPRs) and
       r15 last (lowest address, closest to stub_rsp), so in word-index
       terms relative to stub_rsp[0]:
         [0]=r15 [1]=r14 [2]=r13 [3]=r12 [4]=r11 [5]=r10 [6]=r9 [7]=r8
         [8]=rbp [9]=rdi [10]=rsi [11]=rdx [12]=rcx [13]=rbx [14]=rax
       and the CPU-pushed frame begins right after those 15 words, at
       [15]=error_code [16]=RIP [17]=CS [18]=RFLAGS [19]=RSP [20]=SS.
       (An earlier version of this handler indexed stub_rsp[0..5] directly
       as the CPU frame, which actually read into the GPR region instead --
       every RIP/RSP/CS value that diagnostic ever printed was wrong.) */
    const uint64_t user_rip = stub_rsp[16];
    const uint64_t user_cs = stub_rsp[17];
    const uint64_t user_rsp = stub_rsp[19];
    serial_write("USER_RIP="); serial_write_hex(user_rip);
    serial_write(" CS="); serial_write_hex(user_cs);
    serial_write(" USER_RSP="); serial_write_hex(user_rsp);
    serial_write(" rsp_mod16="); serial_write_hex(user_rsp & 15u);
    serial_write("\n");

    /* Dump the actual PTE for pixel_address (0x322080, virtual -- shifted
       with USER_HEAP, see the comment on USERSPACE_CODE_PAGES in
       kernel/userspace.h) and for user_rip's own page, by walking the
       currently-active page table directly -- this settles definitively
       what physical frame each virtual address really resolves to, instead
       of trusting the values computed at process-creation time. */
    {
        uint64_t cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
        /* mako_build_identity_4m's 4-level layout: PML4 -> PDPT -> PD ->
           PT1 covering 0x200000-0x400000 (the second 2 MiB region) with
           4 KiB leaves -- the same pt1 that load_process() writes into
           directly by index. Walk from CR3 down to find that PT1's base,
           rather than assuming a fixed offset, since PML4/PDPT/PD entries
           are themselves per-process allocated frames. */
        const uint64_t *pml4 = (const uint64_t *)(uintptr_t)(cr3 & ~0xFFFull);
        const uint64_t pml4e = pml4[0];
        const uint64_t *pdpt = (const uint64_t *)(uintptr_t)(pml4e & ~0xFFFull);
        const uint64_t pdpte = pdpt[0];
        const uint64_t *pd = (const uint64_t *)(uintptr_t)(pdpte & ~0xFFFull);
        const uint64_t pde = pd[1]; /* index 1 = the 0x200000-0x400000 region */
        const uint64_t *pt1 = (const uint64_t *)(uintptr_t)(pde & ~0xFFFull);
        const uint64_t pixel_addr_pte = pt1[(0x322080u - 0x200000u) / 4096u];
        serial_write("PTE_WALK cr3="); serial_write_hex(cr3);
        serial_write(" pml4e="); serial_write_hex(pml4e);
        serial_write(" pdpte="); serial_write_hex(pdpte);
        serial_write(" pde="); serial_write_hex(pde);
        serial_write("\n");
        serial_write("PTE_WALK pixel_address(0x322080)_pte="); serial_write_hex(pixel_addr_pte);
        serial_write(" -> physical="); serial_write_hex(pixel_addr_pte & ~0xFFFull);
        serial_write("\n");
        if (user_rip >= 0x200000u && user_rip < 0x400000u) {
            const uint64_t rip_page_pte = pt1[(user_rip - 0x200000u) / 4096u];
            serial_write("PTE_WALK rip_page_pte="); serial_write_hex(rip_page_pte);
            serial_write(" -> physical="); serial_write_hex(rip_page_pte & ~0xFFFull);
            serial_write("\n");
        } else {
            serial_write("PTE_WALK rip_page_pte skipped, user_rip out of range\n");
        }
    }

    /* Read the ACTUAL bytes mapped at the faulting RIP, not what the ELF on
       disk says should be there -- the low 4 MiB is identity-mapped in the
       faulting process's own (still-active) CR3, so this is a direct read
       through the same mapping the CPU itself just used to fetch RIP. */
    if (user_rip >= 0x300000u && user_rip < 0x400000u) {
        const uint8_t *code = (const uint8_t *)(uintptr_t)(user_rip - 8u);
        serial_write("CODE_BYTES_AT_RIP-8..+16:");
        for (int i = 0; i < 24; ++i) {
            serial_write(" ");
            serial_write_hex(code[i]);
        }
        serial_write("\n");
    }
    /* Also dump the first 4 words at the fixed base 0x300000 (USER_CODE),
       exactly what code_checksum()/dump_code_head() in compositor.mko reads,
       for a direct word-for-word comparison against what that userspace
       reader reported just before the fault. */
    {
        const uint64_t *code_head = (const uint64_t *)(uintptr_t)0x300000u;
        serial_write("CODE_HEAD_AT_0x300000:");
        for (int i = 0; i < 4; ++i) {
            serial_write(" "); serial_write_hex(code_head[i]);
        }
        serial_write("\n");
    }

    static const char *const gpr_names[15] = {
        "r15", "r14", "r13", "r12", "r11", "r10", "r9", "r8",
        "rbp", "rdi", "rsi", "rdx", "rcx", "rbx", "rax"
    };
    serial_write("GPRS:");
    for (int i = 0; i < 15; ++i) {
        serial_write(" "); serial_write(gpr_names[i]); serial_write("=");
        serial_write_hex(stub_rsp[i]);
    }
    serial_write("\n");

    /* Dump user memory around the user RSP, if it looks like a plausible
       user-stack address (inside the fixed 0x300000-0x400000 process
       window this kernel uses) -- do not blindly dereference an
       attacker/bug-controlled address from ring 0. */
    if (user_rsp >= 0x300000u && user_rsp < 0x400000u && (user_rsp & 7u) == 0u) {
        const uint64_t *words = (const uint64_t *)(uintptr_t)user_rsp;
        serial_write("USER_STACK_DUMP (offset: value):\n");
        for (int i = -4; i <= 12; ++i) {
            serial_write("  [");
            if (i < 0) serial_write("-");
            serial_write_hex((uint64_t)(i < 0 ? -i : i) * 8u);
            serial_write("] = ");
            serial_write_hex(words[i]);
            if (i == 0) serial_write("  <- RSP");
            serial_write("\n");
        }
    } else {
        serial_write("USER_RSP out of expected process window, not dumping\n");
    }

    terminal_set_color(12u, 0u);
    terminal_write_line("PAGE FAULT -- see COM1 serial diagnostics");
    for (;;) __asm__ volatile ("cli; hlt");
}
