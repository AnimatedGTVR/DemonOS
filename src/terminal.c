#include <kernel/terminal.h>
#include <kernel/framebuffer.h>

#include <stddef.h>

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u

static volatile uint16_t *const buffer = (volatile uint16_t *)0xB8000;
/* VGA text memory is a write target, not a dependable readable backing store
   once firmware/GRUB selects a linear framebuffer mode. Keep the canonical
   terminal cells in ordinary kernel memory and mirror every update to VGA for
   the fallback console. */
static uint16_t cells[VGA_WIDTH * VGA_HEIGHT];
static size_t row;
static size_t column;
static uint8_t color;
static bool output_enabled = true;
static bool automatic_wrap;
static bool graphical;

#define GRAPHICAL_X 40
#define GRAPHICAL_Y 72
#define GRAPHICAL_CELL_WIDTH 7
#define GRAPHICAL_CELL_HEIGHT 14

static void graphical_cell(size_t y, size_t x) {
    if (!graphical || !framebuffer_available()) return;
    const int32_t pixel_x = GRAPHICAL_X + (int32_t)x * GRAPHICAL_CELL_WIDTH;
    const int32_t pixel_y = GRAPHICAL_Y + (int32_t)y * GRAPHICAL_CELL_HEIGHT;
    framebuffer_fill_rect((struct framebuffer_rect){pixel_x, pixel_y,
        GRAPHICAL_CELL_WIDTH, GRAPHICAL_CELL_HEIGHT}, 0xFF0B1020u);
    char text[2] = {(char)(cells[y * VGA_WIDTH + x] & 0xffu), '\0'};
    framebuffer_text(pixel_x, pixel_y + 2, text, 1u, 0xFFE5E7EBu);
}

static void store_cell(size_t y, size_t x, uint16_t value) {
    const size_t index = y * VGA_WIDTH + x;
    cells[index] = value;
    buffer[index] = value;
}

void terminal_graphical_refresh(void) {
    if (!graphical || !framebuffer_available()) return;
    framebuffer_clear(0xFF080C16u);
    framebuffer_fill_rect((struct framebuffer_rect){24, 24, 592, 424},
                          0xFF111827u);
    framebuffer_border_rect((struct framebuffer_rect){24, 24, 592, 424},
                            0xFF3B82F6u);
    framebuffer_fill_rect((struct framebuffer_rect){25, 25, 590, 32},
                          0xFF1E3A5Fu);
    framebuffer_text(40, 37, "DEMONOS TERMINAL - C APP WORKSPACE", 1u,
                     0xFFF8FAFCu);
    for (size_t y = 0u; y < VGA_HEIGHT; ++y)
        for (size_t x = 0u; x < VGA_WIDTH; ++x)
            graphical_cell(y, x);
    framebuffer_present();
}

void terminal_graphical_enable(void) {
    if (!framebuffer_available()) return;
    graphical = true;
    terminal_graphical_refresh();
}

bool terminal_graphical_active(void) { return graphical; }

static uint16_t entry(char character) {
    return (uint16_t)(uint8_t)character | (uint16_t)color << 8u;
}

void terminal_init(void) {
    row = 0;
    column = 0;
    color = 0x0Fu;
    automatic_wrap = false;
    for (size_t y = 0; y < VGA_HEIGHT; ++y)
        for (size_t x = 0; x < VGA_WIDTH; ++x)
            store_cell(y, x, entry(' '));
    if (graphical) terminal_graphical_refresh();
}

void terminal_set_color(uint8_t foreground, uint8_t background) {
    color = (uint8_t)(foreground | background << 4u);
}

void terminal_set_output(bool enabled) { output_enabled = enabled; }

static void newline(void) {
    column = 0;
    if (++row < VGA_HEIGHT) return;

    for (size_t y = 1; y < VGA_HEIGHT; ++y)
        for (size_t x = 0; x < VGA_WIDTH; ++x)
            store_cell(y - 1u, x, cells[y * VGA_WIDTH + x]);
    row = VGA_HEIGHT - 1u;
    for (size_t x = 0; x < VGA_WIDTH; ++x)
        store_cell(row, x, entry(' '));
    if (graphical) terminal_graphical_refresh();
}

void terminal_write(const char *text) {
    if (!output_enabled) return;
    bool changed = false;
    while (*text != '\0') {
        if (*text == '\f') {
            row = 0u;
            column = 0u;
            for (size_t y = 0u; y < VGA_HEIGHT; ++y)
                for (size_t x = 0u; x < VGA_WIDTH; ++x)
                    store_cell(y, x, entry(' '));
            if (graphical) terminal_graphical_refresh();
            ++text;
            continue;
        }
        if (*text == '\n') {
            newline();
            automatic_wrap = false;
            ++text;
            continue;
        }
        if (*text == '\r') {
            column = 0u;
            automatic_wrap = false;
            ++text;
            continue;
        }
        if (*text == '\t') {
            const size_t spaces = 4u - column % 4u;
            ++text;
            for (size_t i = 0u; i < spaces; ++i) terminal_write(" ");
            continue;
        }
        const uint8_t byte = (uint8_t)*text++;
        const char visible = byte >= 32u && byte <= 126u ? (char)byte : '?';
        automatic_wrap = false;
        store_cell(row, column, entry(visible));
        graphical_cell(row, column);
        changed = true;
        if (++column == VGA_WIDTH) {
            newline();
            automatic_wrap = true;
        }
    }
    if (changed && graphical) framebuffer_present();
}

void terminal_write_line(const char *text) {
    if (!output_enabled) return;
    terminal_write(text);
    if (!automatic_wrap) newline();
    automatic_wrap = false;
}

void terminal_write_u64(uint64_t value) {
    char digits[21];
    size_t index = sizeof(digits);
    digits[--index] = '\0';
    do {
        digits[--index] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    terminal_write(&digits[index]);
}

void terminal_write_hex(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char output[19] = "0x0000000000000000";
    for (unsigned index = 0; index < 16u; ++index) {
        const unsigned shift = (15u - index) * 4u;
        output[index + 2u] = digits[(value >> shift) & 0xFu];
    }
    terminal_write(output);
}

void terminal_backspace(void) {
    if (!output_enabled) return;
    if (column == 0u) return;
    --column;
    store_cell(row, column, entry(' '));
    graphical_cell(row, column);
    if (graphical) framebuffer_present();
}
