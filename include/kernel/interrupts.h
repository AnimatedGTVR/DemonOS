#ifndef KERNEL_INTERRUPTS_H
#define KERNEL_INTERRUPTS_H

#include <stdbool.h>
#include <stdint.h>

void interrupts_init(void);
bool interrupts_breakpoint_self_test(void);
bool interrupts_hardware_start(void);
uint64_t interrupts_timer_ticks(void);
bool keyboard_read_char(char *value);
void keyboard_discard_chars(void);
#define KEYBOARD_CHAR_HISTORY_UP ((char)0x11)
#define KEYBOARD_CHAR_HISTORY_DOWN ((char)0x12)
// 0x1E/0x1F are unreachable through any other path this driver produces --
// Ctrl+letter only covers 'a'..'z' (see interrupt_keyboard_handler), which
// tops out at 0x1A, and no punctuation Ctrl-combo is translated.
#define KEYBOARD_CHAR_LEFT ((char)0x1E)
#define KEYBOARD_CHAR_RIGHT ((char)0x1F)
#define KEYBOARD_CHAR_HOME ((char)0x1B)
#define KEYBOARD_CHAR_END ((char)0x1C)
#define KEYBOARD_CHAR_DELETE ((char)0x7F)
bool keyboard_controller_ready(void);
uint64_t keyboard_irq_count(void);
uint64_t keyboard_character_count(void);
bool mouse_controller_ready(void);
bool mouse_scroll_ready(void);
uint64_t mouse_irq_count(void);
uint64_t mouse_packet_count(void);
int32_t mouse_x(void);
int32_t mouse_y(void);
uint8_t mouse_buttons(void);
void mouse_set_bounds(uint32_t width, uint32_t height);
// Absolute-position report from a source other than PS/2 (see src/usb_uhci.c):
// x/y are already in screen coordinates (clamped to mouse_set_bounds' own
// pointer_max_x/y), buttons is the same 3-bit left/right/middle mask
// decode_mouse_packet derives from PS/2's own flags byte. Feeds the exact
// same pointer_x/y state and input_publish path PS/2 motion uses, so every
// consumer (the compositor, MakoBox, etc.) sees one pointer regardless of
// which physical device moved it.
void mouse_report_absolute(int32_t x, int32_t y, uint8_t buttons);

#endif
