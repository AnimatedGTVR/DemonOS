/*
 * xterm: a freestanding, scrollback-capable terminal window for DemonX.
 *
 * The DemonX core font is a fixed 6x7 glyph (XLoadQueryFont reports width 6,
 * ascent 7, descent 0), so the terminal is just a grid of 6x7 cells drawn
 * through the libxlib compatibility shim. Input arrives as XKeyEvents whose
 * `value` carries the kernel-decoded character ('\n', '\b', '\t', printable)
 * and whose `keycode` carries the raw scan code for extended keys (arrows).
 *
 * Two personalities, chosen at runtime via syscall 34 (demon_boot_test_mode):
 *   - boot-test: inject a scripted session, assert the terminal buffer, and
 *     exit cleanly so the kernel can reap it.
 *   - interactive: run the persistent event loop as the desktop's terminal.
 */

#include <X11/Xlib.h>
#include <demon/c_app.h>
#include <demon/demonx.h>
#include <demon/input.h>
#include <stdint.h>

/* DemonX-specific extension (see lib/demonx/xlib.c's own comment) -- not
   part of the real Xlib surface Xlib.h declares, so declared here instead
   of pulling it into that shared header for every other client. */
extern int demonx_draw_string_scaled(Display *display, Drawable drawable,
                                     GC gc, int x, int y, const char *text,
                                     int length, uint8_t scale);

static void render_terminal(Display *display, Window window, GC gc);

#define XTERM_DISPLAY ":3"

#define CELL_WIDTH 6
#define CELL_HEIGHT 7
#define GRID_COLS 64
#define GRID_ROWS 22
#define GRID_WIDTH (GRID_COLS * CELL_WIDTH)
#define GRID_HEIGHT (GRID_ROWS * CELL_HEIGHT)

#define PADDING 6
#define TITLE_HEIGHT 12
#define CLIENT_WIDTH (GRID_WIDTH + 2 * PADDING)
#define CLIENT_HEIGHT (TITLE_HEIGHT + GRID_HEIGHT + 2 * PADDING)

#define WIN_X 14
#define WIN_Y 36

#define COLOR_BORDER 0xff2b3340u
#define COLOR_BACKGROUND 0xff0d1014u
#define COLOR_TITLE_BAR 0xff161c26u
#define COLOR_TITLE_TEXT 0xff9aa4b2u
#define COLOR_TEXT 0xffd9e0eau
#define COLOR_PROMPT 0xff7fd88au
#define COLOR_CURSOR 0xffe8a33du
#define COLOR_CURSOR_CHAR 0xff0d1014u

#define PROMPT "mako:/ $ "
#define PROMPT_LENGTH 9u

#define SCROLLBACK_LINES 128u
#define LINE_CAPACITY 80u
#define INPUT_CAPACITY 60u
#define COMMAND_HISTORY 16u

#define KEYCODE_UP 0x48u
#define KEYCODE_DOWN 0x50u
#define KEYCODE_LEFT 0x4bu
#define KEYCODE_RIGHT 0x4du
#define KEYCODE_I 0x17u
#define KEYCODE_O 0x18u

/* GRID_WIDTH/GRID_HEIGHT stay the fixed pixel budget the window was created
   with (see CLIENT_WIDTH/CLIENT_HEIGHT above) -- Ctrl+I/Ctrl+O never resize
   the window, they just change how many scaled cells fit inside that same
   budget (see cell_width/cell_height/visible_rows_at_scale/
   visible_cols_at_scale below). font_scale=1 reproduces today's exact 6x7
   layout; DEMONX_POLY_TEXT8_SCALED has no real reason to support more than
   a handful of steps for a terminal this size, so 3 is a generous top end
   (18x21 cells, ~21x7 visible) and 1 is the natural floor (the font has no
   smaller real representation to fall back to). */
#define FONT_SCALE_MIN 1u
#define FONT_SCALE_MAX 3u
static uint32_t font_scale = 1u;

static uint32_t cell_width(void) { return CELL_WIDTH * font_scale; }
static uint32_t cell_height(void) { return CELL_HEIGHT * font_scale; }
static uint32_t visible_rows_at_scale(void) { return GRID_HEIGHT / cell_height(); }
static uint32_t visible_cols_at_scale(void) { return GRID_WIDTH / cell_width(); }

static char scrollback[SCROLLBACK_LINES][LINE_CAPACITY];
static uint32_t scrollback_count;
static uint32_t scrollback_start;

static char input[INPUT_CAPACITY];
static uint32_t input_length;
static uint32_t input_cursor;

static char command_history[COMMAND_HISTORY][INPUT_CAPACITY];
static uint32_t command_history_count;
static uint32_t history_index; /* 0..count-1 while recalling, count = live */

static uint32_t string_length(const char *text) {
    uint32_t length = 0u;
    while (text[length] != '\0') ++length;
    return length;
}

static void copy_string(char *destination, const char *source) {
    uint32_t index = 0u;
    while (source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static void append_scrollback(const char *line) {
    copy_string(scrollback[(scrollback_start + scrollback_count) %
                           SCROLLBACK_LINES], line);
    if (scrollback_count < SCROLLBACK_LINES) {
        ++scrollback_count;
    } else {
        scrollback_start = (scrollback_start + 1u) % SCROLLBACK_LINES;
    }
}

static uint32_t visible_line(uint32_t index) {
    const uint32_t rows = visible_rows_at_scale();
    const uint32_t visible = scrollback_count < rows ? scrollback_count : rows;
    const uint32_t offset = scrollback_count - visible;
    return (scrollback_start + offset + index) % SCROLLBACK_LINES;
}

static void reset_input(void) {
    input_length = 0u;
    input_cursor = 0u;
    history_index = command_history_count;
    input[0] = '\0';
}

static void input_insert(uint32_t character) {
    if (input_length >= INPUT_CAPACITY - 1u) return;
    uint32_t index = input_length;
    while (index > input_cursor) {
        input[index] = input[index - 1u];
        --index;
    }
    input[input_cursor] = (char)character;
    ++input_length;
    ++input_cursor;
    input[input_length] = '\0';
}

static void input_backspace(void) {
    if (input_cursor == 0u) return;
    uint32_t index = input_cursor - 1u;
    while (index < input_length - 1u) {
        input[index] = input[index + 1u];
        ++index;
    }
    --input_length;
    --input_cursor;
    input[input_length] = '\0';
}

static void input_tab(void) {
    for (uint32_t index = 0u; index < 4u; ++index) input_insert(' ');
}

static void remember_command(void) {
    if (input_length == 0u) return;
    if (command_history_count < COMMAND_HISTORY) {
        copy_string(command_history[command_history_count], input);
        ++command_history_count;
    } else {
        for (uint32_t index = 1u; index < COMMAND_HISTORY; ++index)
            copy_string(command_history[index - 1u], command_history[index]);
        copy_string(command_history[COMMAND_HISTORY - 1u], input);
    }
}

static void recall_older(void) {
    if (command_history_count == 0u) return;
    if (history_index == 0u) return;
    --history_index;
    copy_string(input, command_history[history_index]);
    input_length = string_length(input);
    input_cursor = input_length;
}

static void recall_newer(void) {
    if (history_index >= command_history_count) return;
    ++history_index;
    if (history_index >= command_history_count) {
        input[0] = '\0';
        input_length = 0u;
        input_cursor = 0u;
    } else {
        copy_string(input, command_history[history_index]);
        input_length = string_length(input);
        input_cursor = input_length;
    }
}

static int command_matches(const char *line, const char *name) {
    uint32_t index = 0u;
    while (name[index] != '\0') {
        if (line[index] != name[index]) return 0;
        ++index;
    }
    return line[index] == '\0' || line[index] == ' ';
}

static void terminal_execute(void) {
    char line[LINE_CAPACITY];
    copy_string(line, PROMPT);
    uint32_t index = 0u;
    while (index < input_length) {
        line[PROMPT_LENGTH + index] = input[index];
        ++index;
    }
    line[PROMPT_LENGTH + index] = '\0';
    append_scrollback(line);

    const char *command = input;
    if (command[0] == '\0') {
        reset_input();
        return;
    }
    remember_command();

    if (command_matches(command, "help")) {
        append_scrollback("Commands: help, clear, echo <text>, about, exit");
    } else if (command_matches(command, "clear")) {
        scrollback_count = 0u;
        scrollback_start = 0u;
    } else if (command_matches(command, "about")) {
        append_scrollback("MAKO xterm 1.0 -- DemonX X11 wire client");
        append_scrollback("kernel: MAKO microkernel, 640x480 ARGB desktop");
    } else if (command_matches(command, "exit")) {
        append_scrollback("(window stays; kill the process to close)");
    } else if (command_matches(command, "echo")) {
        const char *text = command + 4u;
        while (*text == ' ') ++text;
        if (*text == '\0') {
            append_scrollback("");
        } else {
            append_scrollback(text);
        }
    } else {
        char reply[LINE_CAPACITY];
        copy_string(reply, "mako: command not found: ");
        uint32_t length = string_length(reply);
        index = 0u;
        while (command[index] != '\0' && length < LINE_CAPACITY - 1u) {
            reply[length] = command[index];
            ++length;
            ++index;
        }
        reply[length] = '\0';
        append_scrollback(reply);
    }
    reset_input();
}

static void terminal_clear_screen(void) {
    scrollback_count = 0u;
    scrollback_start = 0u;
}

static void terminal_key(uint32_t value, uint32_t keycode, uint32_t modifiers) {
    if ((modifiers & INPUT_MOD_CTRL) != 0u) {
        if (keycode == KEYCODE_I) {
            if (font_scale > FONT_SCALE_MIN) --font_scale;
            return;
        }
        if (keycode == KEYCODE_O) {
            if (font_scale < FONT_SCALE_MAX) ++font_scale;
            return;
        }
    }
    if (value == '\n') {
        terminal_execute();
        return;
    }
    if (value == '\b') {
        input_backspace();
        return;
    }
    if (value == '\t') {
        input_tab();
        return;
    }
    if (value == 3u) {
        append_scrollback("^C");
        reset_input();
        return;
    }
    if (value == 12u) {
        terminal_clear_screen();
        return;
    }
    if (value >= 0x20u && value < 0x7fu) {
        input_insert(value);
        return;
    }
    if (value == 0u) {
        if (keycode == KEYCODE_UP) recall_older();
        else if (keycode == KEYCODE_DOWN) recall_newer();
        else if (keycode == KEYCODE_LEFT && input_cursor > 0u)
            --input_cursor;
        else if (keycode == KEYCODE_RIGHT && input_cursor < input_length)
            ++input_cursor;
    }
}

static void terminal_feed_text(Display *display, Window window, GC gc,
                               const char *text) {
    uint32_t index = 0u;
    while (text[index] != '\0') {
        terminal_key((uint32_t)text[index], 0u, 0u);
        ++index;
    }
    render_terminal(display, window, gc);
}

static void render_terminal(Display *display, Window window, GC gc) {
    XSetForeground(display, gc, COLOR_BORDER);
    XFillRectangle(display, window, gc, 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT);

    XSetForeground(display, gc, COLOR_TITLE_BAR);
    XFillRectangle(display, window, gc, 1, 1, CLIENT_WIDTH - 2u, TITLE_HEIGHT);

    XSetForeground(display, gc, COLOR_TITLE_TEXT);
    XDrawString(display, window, gc, PADDING, 9, "MAKO xterm -- DemonX", 20);

    XSetForeground(display, gc, COLOR_BACKGROUND);
    XFillRectangle(display, window, gc, 1, TITLE_HEIGHT + 1u,
                   CLIENT_WIDTH - 2u, CLIENT_HEIGHT - TITLE_HEIGHT - 2u);

    /* Ctrl+I/Ctrl+O never resize the window (see font_scale's own comment)
       -- they change how many of these cells fit in the same fixed pixel
       budget, so every position below is scale-aware and every drawn
       string is clipped to whatever now fits, rather than assuming the
       scale=1 column/row counts GRID_COLS/GRID_ROWS baked the window size
       around. */
    const uint32_t scaled_w = cell_width();
    const uint32_t scaled_h = cell_height();
    const uint32_t cols = visible_cols_at_scale();
    const uint32_t visible = scrollback_count < visible_rows_at_scale()
        ? scrollback_count : visible_rows_at_scale();
    for (uint32_t row = 0u; row < visible; ++row) {
        const char *line = scrollback[visible_line(row)];
        uint32_t length = string_length(line);
        if (length > cols) length = cols;
        XSetForeground(display, gc, COLOR_TEXT);
        demonx_draw_string_scaled(display, window, gc, PADDING,
            PADDING + TITLE_HEIGHT + (int)(row * scaled_h) + 6,
            line, (int)length, (uint8_t)font_scale);
    }

    const int input_y = PADDING + TITLE_HEIGHT + (int)(visible * scaled_h) + 6;
    uint32_t prompt_length = PROMPT_LENGTH;
    if (prompt_length > cols) prompt_length = cols;
    XSetForeground(display, gc, COLOR_PROMPT);
    demonx_draw_string_scaled(display, window, gc, PADDING, input_y, PROMPT,
        (int)prompt_length, (uint8_t)font_scale);
    if (input_length > 0u && prompt_length < cols) {
        uint32_t shown = input_length;
        if (prompt_length + shown > cols) shown = cols - prompt_length;
        XSetForeground(display, gc, COLOR_TEXT);
        demonx_draw_string_scaled(display, window, gc,
            PADDING + (int)(PROMPT_LENGTH * scaled_w), input_y,
            input, (int)shown, (uint8_t)font_scale);
    }

    const int cursor_x = PADDING + (int)((PROMPT_LENGTH + input_cursor) * scaled_w);
    const int cursor_y = PADDING + TITLE_HEIGHT + (int)(visible * scaled_h);
    XSetForeground(display, gc, COLOR_CURSOR);
    XFillRectangle(display, window, gc, cursor_x, cursor_y, scaled_w, scaled_h);
    if (input_cursor < input_length) {
        const char cursor_char[2] = { input[input_cursor], '\0' };
        XSetForeground(display, gc, COLOR_CURSOR_CHAR);
        demonx_draw_string_scaled(display, window, gc, cursor_x,
            cursor_y + 6 * (int)font_scale, cursor_char, 1, (uint8_t)font_scale);
    }
    XFlush(display);
}

static int smoke_assert_contains(const char *needle) {
    for (uint32_t index = 0u; index < scrollback_count; ++index) {
        const char *line = scrollback[(scrollback_start + index) %
                                      SCROLLBACK_LINES];
        uint32_t offset = 0u;
        while (line[offset] != '\0') {
            uint32_t match = 0u;
            while (needle[match] != '\0' &&
                   line[offset + match] == needle[match])
                ++match;
            if (needle[match] == '\0') return 1;
            ++offset;
        }
    }
    return 0;
}

static void smoke_write(const char *text) {
    (void)demon_write(text, string_length(text));
}

static uint64_t xterm_smoke(Display *display, Window window, GC gc) {
    terminal_feed_text(display, window, gc, "help\n");
    if (!smoke_assert_contains("Commands:")) {
        smoke_write("XTERM_SMOKE_FAIL help-output-missing\n");
        return 210u;
    }
    terminal_feed_text(display, window, gc, "echo hello\n");
    if (!smoke_assert_contains("hello")) {
        smoke_write("XTERM_SMOKE_FAIL echo-output-missing\n");
        return 211u;
    }
    terminal_feed_text(display, window, gc, "about\n");
    if (!smoke_assert_contains("MAKO xterm")) {
        smoke_write("XTERM_SMOKE_FAIL about-output-missing\n");
        return 212u;
    }
    smoke_write("XTERM_SMOKE_OK grid=");
    smoke_write("64x22");
    smoke_write(" lines=");
    smoke_write("\n");
    return 0u;
}

uint64_t xterm_main(void) {
    Display *display = XOpenDisplay(XTERM_DISPLAY);
    if (display == 0) {
        smoke_write("XTERM_DISPLAY_FAIL\n");
        return 200u;
    }
    Window window = XCreateSimpleWindow(display, DEMONX_ROOT_WINDOW,
                                        WIN_X, WIN_Y, CLIENT_WIDTH,
                                        CLIENT_HEIGHT, 1, COLOR_BORDER,
                                        COLOR_BACKGROUND);
    if (window == 0) {
        XCloseDisplay(display);
        smoke_write("XTERM_CREATE_FAIL\n");
        return 201u;
    }
    XSelectInput(display, window,
                 KeyPressMask | KeyReleaseMask | ButtonPressMask |
                 ButtonReleaseMask | ExposureMask | StructureNotifyMask);
    XStoreName(display, window, "xterm");
    XMapWindow(display, window);
    XFlush(display);

    GC gc = XCreateGC(display, window, 0u, 0);
    if (gc == 0) {
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        smoke_write("XTERM_GC_FAIL\n");
        return 202u;
    }
    XSetForeground(display, gc, COLOR_TEXT);
    XFontStruct *font = XLoadQueryFont(display, "fixed");
    if (font != 0) XSetFont(display, gc, font->fid);
    XFlush(display);

    if (demon_boot_test_mode() != 0u) {
        const uint64_t status = xterm_smoke(display, window, gc);
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return status;
    }

    smoke_write("XTERM_READY display=:3 grid=64x22\n");

    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == KeyPress) {
            if (event.xkey.keycode == 0x01u) break;
            terminal_key(event.xkey.value, event.xkey.keycode,
                        (uint32_t)event.xkey.state);
            render_terminal(display, window, gc);
        } else if (event.type == ConfigureNotify) {
            render_terminal(display, window, gc);
        }
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0u;
}
