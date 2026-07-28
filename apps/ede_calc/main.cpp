// Wave 1 of the EDE port: a native MAKO-ABI window wrapping CalcEngine
// (engine.cpp, ported from Desktop/EDE/ede-2.1/ede-calc/SciCalc.cpp). This
// file replaces SciCalc.cpp's FLTK widget tree with the same compositor
// surface + click-event pattern apps/calculator/main.c already established
// for native C apps -- a pixel buffer this app owns, drawn with
// libs/graphics/graphics.c's primitives, shared with the compositor and
// re-submitted on every redraw.
#include <demon/c_app.h>
#include <demon/window.h>
#include <demon/graphics.h>

#include "engine.h"

namespace {

// Every MAKO-ABI process gets a fixed, small physical code+data+bss budget
// (kernel.c's per-process "generic app" slot: 12 pages = 49152 bytes), and
// this pixel buffer is by far the biggest thing in this app's footprint.
// Raising that shared budget to fit a full desktop-calculator-sized window
// turned out to destabilize an unrelated compositor focus-routing self-test
// (still not fully root-caused -- see the session notes), so instead of
// touching kernel-wide scheduling behavior, ede-calc's own window and
// button count are trimmed to comfortably fit the existing budget: a
// reduced 30-button set (single BASE-cycle button instead of four,
// brackets/exchange/reciprocal/factorial/DR/extra memory ops dropped from
// the UI) rather than SciCalc's full ~45. CalcEngine itself still has all
// of that logic -- only the button grid exposing it is pared down.
constexpr unsigned W = 87u;
constexpr unsigned H = 93u;
constexpr int WIN_X = 140;
constexpr int WIN_Y = 60;
constexpr int TITLEBAR = 32; // matches apps/calculator/main.c's compositor chrome offset

enum Action {
    ACT_BLANK,
    ACT_BASE, ACT_INV, ACT_DRG, ACT_AC, ACT_C,
    ACT_DIGIT7, ACT_DIGIT8, ACT_DIGIT9, ACT_DIV, ACT_SQRT, ACT_SIN,
    ACT_DIGIT4, ACT_DIGIT5, ACT_DIGIT6, ACT_MULT, ACT_COS, ACT_TAN,
    ACT_DIGIT1, ACT_DIGIT2, ACT_DIGIT3, ACT_MINUS, ACT_LOG, ACT_LN,
    ACT_DIGIT0, ACT_DOT, ACT_SIGN, ACT_PLUS, ACT_PI, ACT_MPLUS,
    ACT_EVAL,
};

constexpr unsigned COLUMNS = 6u;
constexpr unsigned ROWS = 5u;
constexpr Action layout[ROWS * COLUMNS] = {
    ACT_BASE, ACT_INV, ACT_DRG, ACT_AC, ACT_C, ACT_EVAL,
    ACT_DIGIT7, ACT_DIGIT8, ACT_DIGIT9, ACT_DIV, ACT_SQRT, ACT_SIN,
    ACT_DIGIT4, ACT_DIGIT5, ACT_DIGIT6, ACT_MULT, ACT_COS, ACT_TAN,
    ACT_DIGIT1, ACT_DIGIT2, ACT_DIGIT3, ACT_MINUS, ACT_LOG, ACT_LN,
    ACT_DIGIT0, ACT_DOT, ACT_SIGN, ACT_PLUS, ACT_PI, ACT_MPLUS,
};

constexpr int GRID_X = 2;
constexpr int GRID_Y = 20;
constexpr int CELL_W = 13;
constexpr int CELL_H = 13;
constexpr int GAP = 1;

uint32_t pixels[W * H];
struct graphics_surface surface;
struct demon_window_message packet;
char event_name[13] = "desktop.win0";

struct graphics_rect cell_rect(unsigned index) {
    unsigned row = index / COLUMNS, col = index % COLUMNS;
    return {GRID_X + (int32_t)(col * (CELL_W + GAP)), GRID_Y + (int32_t)(row * (CELL_H + GAP)),
            CELL_W, CELL_H};
}

// Every label below only uses characters the bitmap font actually renders
// (space/-/./: /digits/uppercase A-Z -- libs/graphics/graphics.c's
// rows_for()), so '+', '*', '/', '=', '(', ')', '!' all get spelled out.
// Two-character abbreviations: cells are 13px wide (2 glyphs = 12px at
// scale1), which is as much label text as fits.
void button_label(Action action, const CalcEngine &engine, char out[8]) {
    const bool inv = engine.inverse_active();
    const bool hex = engine.base() > 10;
    const char *text = "";
    switch (action) {
        case ACT_BASE: {
            const int base = engine.base();
            text = base == 2 ? "2" : base == 8 ? "8" : base == 10 ? "10" : "16";
            break;
        }
        case ACT_INV: text = "IN"; break;
        case ACT_DRG: text = engine.drg_label(); break; // "DEG"/"RAD"/"GRAD", clipped to 2 chars below
        case ACT_SQRT: text = hex ? "A" : (inv ? "X2" : "SR"); break;
        case ACT_SIN: text = hex ? "C" : (inv ? "AS" : "SN"); break;
        case ACT_COS: text = hex ? "D" : (inv ? "AC" : "CO"); break;
        case ACT_TAN: text = hex ? "E" : (inv ? "AT" : "TN"); break;
        case ACT_LOG: text = hex ? "F" : (inv ? "PW" : "LG"); break;
        case ACT_LN: text = inv ? "EX" : "LN"; break;
        case ACT_MPLUS: text = inv ? "MS" : "MA"; break;
        case ACT_PI: text = "PI"; break;
        case ACT_C: text = "CE"; break;
        case ACT_AC: text = "CA"; break;
        case ACT_DIV: text = "DV"; break;
        case ACT_MULT: text = "MU"; break;
        case ACT_MINUS: text = "-"; break;
        case ACT_PLUS: text = "AD"; break;
        case ACT_SIGN: text = "NG"; break;
        case ACT_DOT: text = "."; break;
        case ACT_EVAL: text = "EQ"; break;
        case ACT_DIGIT0: text = "0"; break;
        case ACT_DIGIT1: text = "1"; break;
        case ACT_DIGIT2: text = "2"; break;
        case ACT_DIGIT3: text = "3"; break;
        case ACT_DIGIT4: text = "4"; break;
        case ACT_DIGIT5: text = "5"; break;
        case ACT_DIGIT6: text = "6"; break;
        case ACT_DIGIT7: text = "7"; break;
        case ACT_DIGIT8: text = "8"; break;
        case ACT_DIGIT9: text = "9"; break;
        case ACT_BLANK: text = ""; break;
    }
    unsigned i = 0;
    for (; text[i] != '\0' && i < 2u; ++i) out[i] = text[i];
    out[i] = '\0';
}

uint32_t button_color(Action action, const CalcEngine &engine) {
    switch (action) {
        case ACT_BLANK: return 0xFF101722u;
        case ACT_INV: return engine.inverse_active() ? 0xFF4CC2FFu : 0xFF2A3B52u;
        case ACT_DIGIT0: case ACT_DIGIT1: case ACT_DIGIT2: case ACT_DIGIT3: case ACT_DIGIT4:
        case ACT_DIGIT5: case ACT_DIGIT6: case ACT_DIGIT7: case ACT_DIGIT8: case ACT_DIGIT9:
        case ACT_DOT:
            return 0xFF34465Cu;
        case ACT_PLUS: case ACT_MINUS: case ACT_MULT: case ACT_DIV: case ACT_EVAL:
            return 0xFFE29B32u;
        case ACT_MPLUS:
            return 0xFF35A768u;
        case ACT_AC: case ACT_C:
            return 0xFFE05260u;
        default:
            return 0xFF2479D8u;
    }
}

void redraw(const CalcEngine &engine) {
    graphics_clear(&surface, 0xFF0B0F16u);
    graphics_fill_rect(&surface, {1, 1, (int32_t)W - 2, 16}, 0xFF16202Cu);
    graphics_text(&surface, 3, 2, engine.display(), 1u, 0xFF6FE08Fu);

    for (unsigned i = 0; i < ROWS * COLUMNS; ++i) {
        const struct graphics_rect rect = cell_rect(i);
        if (layout[i] == ACT_BLANK) continue;
        graphics_rounded_rect(&surface, rect, 2u, button_color(layout[i], engine));
        char label[8];
        button_label(layout[i], engine, label);
        unsigned length = 0; while (label[length] != '\0') ++length;
        const int32_t text_x = rect.x + (rect.width - (int32_t)(length * 6u)) / 2;
        const int32_t text_y = rect.y + (rect.height - 7) / 2;
        graphics_text(&surface, text_x, text_y, label, 1u, 0xFFF8FAFCu);
    }
}

void dispatch(CalcEngine &engine, Action action) {
    switch (action) {
        case ACT_BASE: {
            const int base = engine.base();
            engine.change_base(base == 2 ? 8 : base == 8 ? 10 : base == 10 ? 16 : 2);
            break;
        }
        case ACT_INV: engine.toggle_inv(); break;
        case ACT_DRG: engine.drg_key(); break;
        case ACT_SQRT: engine.sqrt_key(); break;
        case ACT_SIN: engine.sin_key(); break;
        case ACT_COS: engine.cos_key(); break;
        case ACT_TAN: engine.tan_key(); break;
        case ACT_LOG: engine.log_key(); break;
        case ACT_LN: engine.ln_key(); break;
        case ACT_MPLUS: engine.mplus(); break;
        case ACT_PI: engine.pi_key(); break;
        case ACT_C: engine.clear_entry(); break;
        case ACT_AC: engine.clear_all(); break;
        case ACT_DIV: engine.op(CalcEngine::DIV); break;
        case ACT_MULT: engine.op(CalcEngine::MULT); break;
        case ACT_MINUS: engine.op(CalcEngine::MINUS); break;
        case ACT_PLUS: engine.op(CalcEngine::PLUS); break;
        case ACT_SIGN: engine.sign(); break;
        case ACT_DOT: engine.dot(); break;
        case ACT_EVAL: engine.op(CalcEngine::EVAL); break;
        case ACT_DIGIT0: engine.digit(0.0); break;
        case ACT_DIGIT1: engine.digit(1.0); break;
        case ACT_DIGIT2: engine.digit(2.0); break;
        case ACT_DIGIT3: engine.digit(3.0); break;
        case ACT_DIGIT4: engine.digit(4.0); break;
        case ACT_DIGIT5: engine.digit(5.0); break;
        case ACT_DIGIT6: engine.digit(6.0); break;
        case ACT_DIGIT7: engine.digit(7.0); break;
        case ACT_DIGIT8: engine.digit(8.0); break;
        case ACT_DIGIT9: engine.digit(9.0); break;
        case ACT_BLANK: break;
    }
}

} // namespace

extern "C" uint64_t ede_calc_main(void) {
    const uint64_t pid = demon_getpid();
    event_name[11] = (char)('0' + pid % 10u);
    const uint64_t events = demon_channel_create(event_name, 12u);
    const uint64_t compositor =
        demon_channel_connect(DEMON_WINDOW_SERVICE, sizeof(DEMON_WINDOW_SERVICE) - 1u);
    const uint64_t factory = demon_service_open(9u);
    const uint64_t surface_handle = demon_surface_create(factory, W, H);
    if (events > UINT32_MAX || compositor > UINT32_MAX ||
        factory > UINT32_MAX || surface_handle > UINT32_MAX)
        return 1u;

    if (!graphics_surface_init(&surface, pixels, W, H, W)) return 2u;

    CalcEngine engine;
    redraw(engine);
    if (demon_surface_write(surface_handle, pixels, W * H, 0u) != W * H) return 3u;
    demon_surface_damage(surface_handle, 0u, 0u, W, H);

    packet = (struct demon_window_message){
        .version = DEMON_WINDOW_PROTOCOL_VERSION,
        .opcode = DEMON_WINDOW_CREATE,
        .serial = 1u,
        .window_id = (uint32_t)pid,
        .flags = DEMON_WINDOW_RESIZABLE,
        .x = WIN_X, .y = WIN_Y, .width = W, .height = H,
        .surface_id = (uint32_t)demon_surface_share(surface_handle, compositor),
        .payload_length = 0u, .payload = {0},
    };
    if (demon_channel_send(compositor, &packet, sizeof(packet)) != sizeof(packet))
        return 4u;
    demon_handle_close(compositor);
    demon_handle_close(factory);

    for (;;) {
        if (demon_channel_receive(events, &packet, sizeof(packet), 0u) != sizeof(packet))
            return 0u;
        if (packet.version != DEMON_WINDOW_PROTOCOL_VERSION || packet.window_id != pid)
            continue;
        if (packet.opcode == DEMON_WINDOW_CLOSE) return 0u;
        if (packet.opcode == 11u) {
            const int local_x = packet.x > WIN_X ? packet.x - WIN_X : -1;
            const int local_y = packet.y > WIN_Y + TITLEBAR ? packet.y - (WIN_Y + TITLEBAR) : -1;
            if (local_x >= 0 && local_y >= 0) {
                for (unsigned i = 0; i < ROWS * COLUMNS; ++i) {
                    if (layout[i] == ACT_BLANK) continue;
                    const struct graphics_rect rect = cell_rect(i);
                    if (local_x >= rect.x && local_x < rect.x + rect.width &&
                        local_y >= rect.y && local_y < rect.y + rect.height) {
                        dispatch(engine, layout[i]);
                        break;
                    }
                }
            }
            redraw(engine);
            demon_surface_write(surface_handle, pixels, W * H, 0u);
            demon_surface_damage(surface_handle, 0u, 0u, W, H);
        }
    }
}
