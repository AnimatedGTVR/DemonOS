#include <demon/c_app.h>
#include <demon/input.h>
#include <stddef.h>
#include <stdint.h>

#define BOARD_WIDTH 10
#define BOARD_HEIGHT 20
/* USER_HEAP moved from 0x318000 to 0x31E000, to 0x322000, to 0x328000
   (40 code pages) for the native session loading/login work, and now to
   0x330000 (48 code pages) for EDDE's real-ported taskbar context menu. */
#define EVENT_ADDRESS ((struct input_event *)(uintptr_t)0x330000u)
#define BOARD ((uint8_t *)(uintptr_t)0x330100u)
#define OUTPUT ((char *)(uintptr_t)0x330400u)

static const uint16_t pieces[7] = {
    0x00f0u, /* I */ 0x0660u, /* O */ 0x0270u, /* T */ 0x0710u,
    0x0740u, /* J */ 0x0360u, /* S */ 0x0630u, /* Z */
};

static uint16_t rotated(uint16_t mask) {
    uint16_t result = 0u;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            if ((mask & (uint16_t)(1u << (y * 4 + x))) != 0u)
                result |= (uint16_t)(1u << (x * 4 + (3 - y)));
    return result;
}

static int collides(uint16_t mask, int piece_x, int piece_y) {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            if ((mask & (uint16_t)(1u << (y * 4 + x))) == 0u) continue;
            const int board_x = piece_x + x;
            const int board_y = piece_y + y;
            if (board_x < 0 || board_x >= BOARD_WIDTH || board_y >= BOARD_HEIGHT)
                return 1;
            if (board_y >= 0 && BOARD[board_y * BOARD_WIDTH + board_x] != 0u)
                return 1;
        }
    }
    return 0;
}

static void settle(uint16_t mask, int piece_x, int piece_y) {
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            if ((mask & (uint16_t)(1u << (y * 4 + x))) != 0u && piece_y + y >= 0)
                BOARD[(piece_y + y) * BOARD_WIDTH + piece_x + x] = 1u;
}

static unsigned clear_lines(void) {
    unsigned cleared = 0u;
    for (int y = BOARD_HEIGHT - 1; y >= 0; --y) {
        int full = 1;
        for (int x = 0; x < BOARD_WIDTH; ++x)
            if (BOARD[y * BOARD_WIDTH + x] == 0u) full = 0;
        if (!full) continue;
        ++cleared;
        for (int copy_y = y; copy_y > 0; --copy_y)
            for (int x = 0; x < BOARD_WIDTH; ++x)
                BOARD[copy_y * BOARD_WIDTH + x] =
                    BOARD[(copy_y - 1) * BOARD_WIDTH + x];
        for (int x = 0; x < BOARD_WIDTH; ++x) BOARD[x] = 0u;
        ++y;
    }
    return cleared;
}

static void append_text(size_t *length, const char *text) {
    while (*text != '\0') OUTPUT[(*length)++] = *text++;
}

static void append_number(size_t *length, unsigned value) {
    if (value >= 10u) OUTPUT[(*length)++] = (char)('0' + value / 10u);
    OUTPUT[(*length)++] = (char)('0' + value % 10u);
}

static void render(uint16_t mask, int piece_x, int piece_y,
                   unsigned score, unsigned lines) {
    size_t length = 0u;
    OUTPUT[length++] = '\f';
    append_text(&length, "DEMONOS C TETRIS  A/D MOVE  W ROTATE  S DROP  Q QUIT\n");
    append_text(&length, "SCORE "); append_number(&length, score);
    append_text(&length, "  LINES "); append_number(&length, lines);
    append_text(&length, "\n+----------+\n");
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        OUTPUT[length++] = '|';
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            int occupied = BOARD[y * BOARD_WIDTH + x] != 0u;
            const int local_x = x - piece_x;
            const int local_y = y - piece_y;
            if (local_x >= 0 && local_x < 4 && local_y >= 0 && local_y < 4 &&
                (mask & (uint16_t)(1u << (local_y * 4 + local_x))) != 0u)
                occupied = 1;
            OUTPUT[length++] = occupied ? '#' : ' ';
        }
        append_text(&length, "|\n");
    }
    append_text(&length, "+----------+\n");
    (void)demon_write(OUTPUT, length);
}

uint64_t tetris_main(void) {
    for (size_t cell = 0u; cell < BOARD_WIDTH * BOARD_HEIGHT; ++cell)
        BOARD[cell] = 0u;
    uint64_t input = demon_service_open(8u);
    if (input == UINT64_MAX) return 20u;
    unsigned piece_number = 0u;
    unsigned score = 0u;
    unsigned lines = 0u;
    uint16_t mask = pieces[0];
    int piece_x = 3;
    int piece_y = -1;
    uint64_t last_fall = demon_ticks();
    uint64_t deadline = last_fall + 3000u;
    render(mask, piece_x, piece_y, score, lines);
    while (demon_ticks() < deadline) {
        if (demon_input_poll(input, EVENT_ADDRESS) == 0u &&
            EVENT_ADDRESS->type == INPUT_KEY_DOWN) {
            const char key = (char)EVENT_ADDRESS->value;
            if (key == 'q' || key == 'Q') break;
            if ((key == 'a' || key == 'A') && !collides(mask, piece_x - 1, piece_y))
                --piece_x;
            if ((key == 'd' || key == 'D') && !collides(mask, piece_x + 1, piece_y))
                ++piece_x;
            if (key == 'w' || key == 'W') {
                const uint16_t next = rotated(mask);
                if (!collides(next, piece_x, piece_y)) mask = next;
            }
            if (key == 's' || key == 'S') last_fall = 0u;
            render(mask, piece_x, piece_y, score, lines);
        }
        const uint64_t now = demon_ticks();
        if (now - last_fall >= 25u) {
            last_fall = now;
            if (!collides(mask, piece_x, piece_y + 1)) {
                ++piece_y;
            } else {
                settle(mask, piece_x, piece_y);
                const unsigned removed = clear_lines();
                lines += removed;
                score += 4u + removed * 10u;
                ++piece_number;
                mask = pieces[piece_number % 7u];
                piece_x = (int)(piece_number * 3u % 7u);
                piece_y = -1;
                if (collides(mask, piece_x, piece_y)) break;
            }
            render(mask, piece_x, piece_y, score, lines);
        }
        (void)demon_yield();
    }
    (void)demon_handle_close(input);
    static const char done[] = "TETRIS EXITED CLEANLY\n";
    (void)demon_write(done, sizeof(done) - 1u);
    return 0u;
}
