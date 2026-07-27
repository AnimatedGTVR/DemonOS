#include <kernel/serial.h>

#define COM1 0x3F8u

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void) {
    out8(COM1 + 1u, 0x00u);
    out8(COM1 + 3u, 0x80u);
    out8(COM1 + 0u, 0x03u);
    out8(COM1 + 1u, 0x00u);
    out8(COM1 + 3u, 0x03u);
    out8(COM1 + 2u, 0xC7u);
    out8(COM1 + 4u, 0x0Bu);
}

void serial_write_char(char value) {
    while ((in8(COM1 + 5u) & 0x20u) == 0u) { }
    out8(COM1, (uint8_t)value);
}

void serial_write(const char *text) {
    while (*text != '\0') {
        if (*text == '\n') serial_write_char('\r');
        serial_write_char(*text++);
    }
}

void serial_write_hex(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_write_char(digits[(value >> (unsigned)shift) & 0xFu]);
}

void serial_write_u64(uint64_t value) {
    char buffer[21];
    unsigned index = sizeof(buffer);
    buffer[--index] = '\0';
    do {
        buffer[--index] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    serial_write(&buffer[index]);
}
