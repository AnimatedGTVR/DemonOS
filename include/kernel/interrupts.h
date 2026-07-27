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

#endif
