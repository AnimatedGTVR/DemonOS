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
static void editor_open(const char *name);

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

// WIN_Y must clear the compositor's own reserved top band plus the
// decoration strip it now draws above every window's own (x, y) -- see
// rust/compositor/src/main.rs's RESERVED_TOP/TITLE_HEIGHT (34 + 24 = 58).
// Under the old DemonWM architecture this only had to clear the panel
// itself (36 = kMargin + kPanelHeight + 2) because DemonWM's reparented
// frame absorbed the decoration offset on xterm's behalf; the compositor
// draws decoration directly above whatever position the client requests,
// so xterm has to leave room for it itself now.
#define WIN_X 14
#define WIN_Y 58

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
#define KEYCODE_HOME 0x47u
#define KEYCODE_END 0x4fu
#define KEYCODE_DELETE 0x53u
#define KEYCODE_S 0x1fu
#define KEYCODE_Q 0x10u
// '-' and '=' (the unshifted key '+' lives on) -- same physical scan code
// regardless of shift state, so Ctrl+Minus/Ctrl+Plus both read cleanly off
// keycode alone without caring whether shift was also held.
#define KEYCODE_MINUS 0x0cu
#define KEYCODE_PLUS 0x0du

/* GRID_WIDTH/GRID_HEIGHT stay the fixed pixel budget the window was created
   with (see CLIENT_WIDTH/CLIENT_HEIGHT above) -- Ctrl+Minus/Ctrl+Plus never
   resize the window, they just change how many scaled cells fit inside
   that same budget (see cell_width/cell_height/visible_rows_at_scale/
   visible_cols_at_scale below). font_scale=1 reproduces the original 6x7
   layout; DEMONX_POLY_TEXT8_SCALED has no real reason to support more than
   a handful of steps for a terminal this size, so 3 is a generous top end
   (18x21 cells, ~21x7 visible) and 1 is the natural floor (the font has no
   smaller real representation to fall back to). Plain '-'/'+' (no Ctrl)
   deliberately still type literal characters -- only the Ctrl chord zooms,
   matching the browser/terminal convention rather than stealing keys users
   need for normal input (filenames, math expressions, command flags). */
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

/* Full-screen "edit <file>" editor. Reuses the same LINE_CAPACITY/font_scale
   layout machinery as the scrollback terminal (see render_editor), but owns
   its own line buffer and cursor -- unlike the shell's single input line,
   this is a real multi-line, savable document. EDITOR_MAX_LINES x
   LINE_CAPACITY is a plain static array, same style as `scrollback`, sized
   for real config-file-sized editing rather than anything approaching a
   full text file -- and kept deliberately small: this "ordinary
   application" process slot only gets a fixed 12-page (48 KiB) code+data
   budget (see kernel.c's per-slot code_pages table), the one larger 48-page
   slot is permanently held by the long-running compositor by the time
   xterm spawns, and every static byte here folds into that same one
   segment alongside xterm's own code (see load_process's own comment on
   why). 48 lines comfortably covers real config-file editing while leaving
   headroom in that budget; editor_open/editor_save also now share one
   scratch buffer instead of one each, for the same reason. */
#define EDITOR_MAX_LINES 48u
static char editor_lines[EDITOR_MAX_LINES][LINE_CAPACITY];
static char editor_scratch[EDITOR_MAX_LINES * LINE_CAPACITY];
static uint32_t editor_line_count;
static uint32_t editor_cursor_row;
static uint32_t editor_cursor_col;
static uint32_t editor_top_row;
static char editor_filename[LINE_CAPACITY];
static int editor_active;
static int editor_dirty;
static int editor_status_is_error;
static char editor_status[LINE_CAPACITY];

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
        append_scrollback("Commands: help, clear, echo <text>, edit <file>, about, exit");
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
    } else if (command_matches(command, "edit")) {
        const char *name = command + 4u;
        while (*name == ' ') ++name;
        if (*name == '\0') {
            append_scrollback("usage: edit filename");
        } else {
            editor_open(name);
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

static void editor_set_status(const char *text, int is_error) {
    copy_string(editor_status, text);
    editor_status_is_error = is_error;
}

// "edit <file>" opens a real RAMFS file (creating it if it doesn't exist
// yet -- create=1u, matching every other app's demon_file_open call) for a
// real multi-line, savable editing session. demon_handle_read always reads
// from byte 0 (see copy_ramfs_to_user's own fixed offset), so this is a
// single bounded read, not a loop -- there is no advancing file position to
// exhaust here.
static void editor_open(const char *name) {
    uint32_t length = string_length(name);
    if (length >= LINE_CAPACITY) length = LINE_CAPACITY - 1u;
    for (uint32_t i = 0u; i < length; ++i) editor_filename[i] = name[i];
    editor_filename[length] = '\0';

    editor_line_count = 0u;
    editor_cursor_row = 0u;
    editor_cursor_col = 0u;
    editor_top_row = 0u;
    editor_dirty = 0;
    editor_active = 1;

    const uint64_t storage = demon_service_open(4u); /* CAPABILITY_SERVICE_STORAGE */
    const uint64_t handle = storage == UINT64_MAX ? UINT64_MAX :
        demon_file_open(storage, editor_filename, length, 1u);
    uint64_t total_read = 0u;
    if (handle != UINT64_MAX) {
        const uint64_t read = demon_handle_read(handle, editor_scratch, sizeof(editor_scratch));
        if (read != UINT64_MAX) total_read = read;
        demon_handle_close(handle);
    }

    uint32_t line = 0u;
    uint32_t col = 0u;
    for (uint64_t i = 0u; i < total_read && line < EDITOR_MAX_LINES; ++i) {
        const char character = editor_scratch[i];
        if (character == '\n') {
            editor_lines[line][col] = '\0';
            ++line;
            col = 0u;
        } else if (col < LINE_CAPACITY - 1u) {
            editor_lines[line][col] = character;
            ++col;
        }
    }
    if (line < EDITOR_MAX_LINES) {
        editor_lines[line][col] = '\0';
        ++line;
    }
    editor_line_count = line;

    if (handle == UINT64_MAX)
        editor_set_status("new file (storage unavailable)", 1);
    else if (total_read == 0u)
        editor_set_status("new file", 0);
    else
        editor_set_status("loaded", 0);
}

// Every line, rejoined with '\n', in one demon_handle_write call: ramfs's
// own store() (src/ramfs.cpp) replaces the whole file's content and length
// per call, so partial/incremental writes would need their own explicit
// truncate step this ABI doesn't expose -- one full-content write is both
// simpler and the only way this actually replaces stale trailing bytes from
// a previous, longer version of the file.
static void editor_save(void) {
    uint32_t total = 0u;
    for (uint32_t line = 0u; line < editor_line_count; ++line) {
        const uint32_t length = string_length(editor_lines[line]);
        for (uint32_t i = 0u; i < length && total < sizeof(editor_scratch); ++i)
            editor_scratch[total++] = editor_lines[line][i];
        if (total < sizeof(editor_scratch)) editor_scratch[total++] = '\n';
    }
    const uint64_t storage = demon_service_open(4u);
    if (storage == UINT64_MAX) {
        editor_set_status("save failed: no storage", 1);
        return;
    }
    const uint64_t handle = demon_file_open(storage, editor_filename,
        string_length(editor_filename), 1u);
    if (handle == UINT64_MAX) {
        editor_set_status("save failed: open", 1);
        return;
    }
    const uint64_t written = demon_handle_write(handle, editor_scratch, total);
    demon_handle_close(handle);
    if (written != total) {
        editor_set_status("save failed: write", 1);
        return;
    }
    editor_dirty = 0;
    editor_set_status("saved", 0);
}

static void editor_key(uint32_t value, uint32_t keycode, uint32_t modifiers) {
    if ((modifiers & INPUT_MOD_CTRL) != 0u) {
        if (keycode == KEYCODE_S) { editor_save(); return; }
        if (keycode == KEYCODE_Q) {
            editor_active = 0;
            append_scrollback(editor_dirty ? "editor closed unsaved changes discarded"
                                           : "editor closed");
            return;
        }
        return;
    }
    char *line = editor_lines[editor_cursor_row];
    const uint32_t length = string_length(line);
    if (value == '\n') {
        if (editor_line_count < EDITOR_MAX_LINES) {
            for (uint32_t i = editor_line_count; i > editor_cursor_row + 1u; --i)
                copy_string(editor_lines[i], editor_lines[i - 1u]);
            copy_string(editor_lines[editor_cursor_row + 1u], line + editor_cursor_col);
            line[editor_cursor_col] = '\0';
            ++editor_line_count;
            ++editor_cursor_row;
            editor_cursor_col = 0u;
            editor_dirty = 1;
        }
        return;
    }
    if (value == '\b') {
        if (editor_cursor_col > 0u) {
            uint32_t index = editor_cursor_col - 1u;
            while (index < length) { line[index] = line[index + 1u]; ++index; }
            --editor_cursor_col;
            editor_dirty = 1;
        } else if (editor_cursor_row > 0u) {
            const uint32_t previous_length =
                string_length(editor_lines[editor_cursor_row - 1u]);
            if (previous_length + length < LINE_CAPACITY - 1u) {
                copy_string(editor_lines[editor_cursor_row - 1u] + previous_length, line);
                for (uint32_t i = editor_cursor_row; i + 1u < editor_line_count; ++i)
                    copy_string(editor_lines[i], editor_lines[i + 1u]);
                --editor_line_count;
                --editor_cursor_row;
                editor_cursor_col = previous_length;
                editor_dirty = 1;
            }
        }
        return;
    }
    if (value >= 0x20u && value < 0x7fu) {
        if (length < LINE_CAPACITY - 1u) {
            uint32_t index = length;
            while (index > editor_cursor_col) {
                line[index] = line[index - 1u];
                --index;
            }
            line[editor_cursor_col] = (char)value;
            ++editor_cursor_col;
            editor_dirty = 1;
        }
        return;
    }
    if (value == 0u) {
        const uint32_t rows = visible_rows_at_scale();
        if (keycode == KEYCODE_UP && editor_cursor_row > 0u) {
            --editor_cursor_row;
            const uint32_t new_length = string_length(editor_lines[editor_cursor_row]);
            if (editor_cursor_col > new_length) editor_cursor_col = new_length;
            if (editor_cursor_row < editor_top_row) editor_top_row = editor_cursor_row;
        } else if (keycode == KEYCODE_DOWN &&
                   editor_cursor_row + 1u < editor_line_count) {
            ++editor_cursor_row;
            const uint32_t new_length = string_length(editor_lines[editor_cursor_row]);
            if (editor_cursor_col > new_length) editor_cursor_col = new_length;
            if (editor_cursor_row >= editor_top_row + rows)
                editor_top_row = editor_cursor_row - rows + 1u;
        } else if (keycode == KEYCODE_LEFT) {
            if (editor_cursor_col > 0u) {
                --editor_cursor_col;
            } else if (editor_cursor_row > 0u) {
                --editor_cursor_row;
                editor_cursor_col = string_length(editor_lines[editor_cursor_row]);
                if (editor_cursor_row < editor_top_row) editor_top_row = editor_cursor_row;
            }
        } else if (keycode == KEYCODE_RIGHT) {
            if (editor_cursor_col < length) {
                ++editor_cursor_col;
            } else if (editor_cursor_row + 1u < editor_line_count) {
                ++editor_cursor_row;
                editor_cursor_col = 0u;
                if (editor_cursor_row >= editor_top_row + rows)
                    editor_top_row = editor_cursor_row - rows + 1u;
            }
        } else if (keycode == KEYCODE_HOME) {
            editor_cursor_col = 0u;
        } else if (keycode == KEYCODE_END) {
            editor_cursor_col = length;
        } else if (keycode == KEYCODE_DELETE) {
            if (editor_cursor_col < length) {
                uint32_t index = editor_cursor_col;
                while (index < length) { line[index] = line[index + 1u]; ++index; }
                editor_dirty = 1;
            } else if (editor_cursor_row + 1u < editor_line_count) {
                const uint32_t next_length = string_length(editor_lines[editor_cursor_row + 1u]);
                if (length + next_length < LINE_CAPACITY - 1u) {
                    copy_string(line + length, editor_lines[editor_cursor_row + 1u]);
                    for (uint32_t i = editor_cursor_row + 1u; i + 1u < editor_line_count; ++i)
                        copy_string(editor_lines[i], editor_lines[i + 1u]);
                    --editor_line_count;
                    editor_dirty = 1;
                }
            }
        }
    }
}

static void render_editor(Display *display, Window window, GC gc) {
    XSetForeground(display, gc, COLOR_BORDER);
    XFillRectangle(display, window, gc, 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT);

    XSetForeground(display, gc, COLOR_TITLE_BAR);
    XFillRectangle(display, window, gc, 1, 1, CLIENT_WIDTH - 2u, TITLE_HEIGHT);
    XSetForeground(display, gc, editor_status_is_error ? COLOR_CURSOR : COLOR_TITLE_TEXT);
    char title[LINE_CAPACITY];
    copy_string(title, "EDIT ");
    uint32_t title_length = string_length(title);
    uint32_t name_length = string_length(editor_filename);
    if (title_length + name_length >= LINE_CAPACITY) name_length = LINE_CAPACITY - 1u - title_length;
    for (uint32_t i = 0u; i < name_length; ++i) title[title_length + i] = editor_filename[i];
    title_length += name_length;
    title[title_length++] = ' ';
    uint32_t status_length = string_length(editor_status);
    if (title_length + status_length >= LINE_CAPACITY) status_length = LINE_CAPACITY - 1u - title_length;
    for (uint32_t i = 0u; i < status_length; ++i) title[title_length + i] = editor_status[i];
    title_length += status_length;
    title[title_length] = '\0';
    XDrawString(display, window, gc, PADDING, 9, title, (int)title_length);

    XSetForeground(display, gc, COLOR_BACKGROUND);
    XFillRectangle(display, window, gc, 1, TITLE_HEIGHT + 1u,
                   CLIENT_WIDTH - 2u, CLIENT_HEIGHT - TITLE_HEIGHT - 2u);

    const uint32_t scaled_w = cell_width();
    const uint32_t scaled_h = cell_height();
    const uint32_t cols = visible_cols_at_scale();
    const uint32_t rows = visible_rows_at_scale();
    uint32_t shown = editor_line_count - editor_top_row;
    if (shown > rows) shown = rows;

    for (uint32_t row = 0u; row < shown; ++row) {
        const char *line = editor_lines[editor_top_row + row];
        uint32_t length = string_length(line);
        if (length > cols) length = cols;
        XSetForeground(display, gc, COLOR_TEXT);
        demonx_draw_string_scaled(display, window, gc, PADDING,
            PADDING + TITLE_HEIGHT + (int)(row * scaled_h) + 6,
            line, (int)length, (uint8_t)font_scale);
    }

    const int cursor_x = PADDING + (int)(editor_cursor_col * scaled_w);
    const int cursor_y = PADDING + TITLE_HEIGHT +
        (int)((editor_cursor_row - editor_top_row) * scaled_h);
    XSetForeground(display, gc, COLOR_CURSOR);
    XFillRectangle(display, window, gc, cursor_x, cursor_y, scaled_w, scaled_h);
    if (editor_cursor_col < string_length(editor_lines[editor_cursor_row])) {
        const char cursor_char[2] =
            { editor_lines[editor_cursor_row][editor_cursor_col], '\0' };
        XSetForeground(display, gc, COLOR_CURSOR_CHAR);
        demonx_draw_string_scaled(display, window, gc, cursor_x,
            cursor_y + 6 * (int)font_scale, cursor_char, 1, (uint8_t)font_scale);
    }
    XFlush(display);
}

static void terminal_key(uint32_t value, uint32_t keycode, uint32_t modifiers) {
    if ((modifiers & INPUT_MOD_CTRL) != 0u) {
        if (keycode == KEYCODE_MINUS) {
            if (font_scale > FONT_SCALE_MIN) --font_scale;
            return;
        }
        if (keycode == KEYCODE_PLUS) {
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

    /* Ctrl+Minus/Ctrl+Plus never resize the window (see font_scale's own
       comment) -- they change how many of these cells fit in the same
       fixed pixel budget, so every position below is scale-aware and every
       drawn string is clipped to whatever now fits, rather than assuming
       the scale=1 column/row counts GRID_COLS/GRID_ROWS baked the window
       size around. */
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
            /* Re-check editor_active after handling the key, not before:
               "edit foo.txt\n" flips it from inside terminal_key's own
               call to editor_open, and that same keypress's render must
               already show the editor, not the terminal view from a
               state that's no longer current. */
            if (editor_active) {
                editor_key(event.xkey.value, event.xkey.keycode,
                          (uint32_t)event.xkey.state);
            } else {
                terminal_key(event.xkey.value, event.xkey.keycode,
                            (uint32_t)event.xkey.state);
            }
            if (editor_active) render_editor(display, window, gc);
            else render_terminal(display, window, gc);
        } else if (event.type == ConfigureNotify) {
            if (editor_active) render_editor(display, window, gc);
            else render_terminal(display, window, gc);
        }
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0u;
}
