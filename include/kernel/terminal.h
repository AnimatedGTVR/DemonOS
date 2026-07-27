#ifndef KERNEL_TERMINAL_H
#define KERNEL_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>

void terminal_init(void);
void terminal_set_color(uint8_t foreground, uint8_t background);
void terminal_set_output(bool enabled);
void terminal_write(const char *text);
void terminal_write_line(const char *text);
void terminal_write_u64(uint64_t value);
void terminal_write_hex(uint64_t value);
void terminal_backspace(void);
void terminal_graphical_enable(void);
void terminal_graphical_refresh(void);
bool terminal_graphical_active(void);

#endif
