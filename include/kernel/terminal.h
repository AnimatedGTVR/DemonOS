#ifndef KERNEL_TERMINAL_H
#define KERNEL_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
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

// Direct cell access for full-screen UIs (e.g. MakoBox's "edit" command)
// that manage their own cursor and redraw a whole screen at a time, rather
// than appending through terminal_write's own scrolling cursor.
size_t terminal_rows(void);
size_t terminal_columns(void);
void terminal_put_char(size_t row, size_t column, char character);
void terminal_present(void);

#endif
