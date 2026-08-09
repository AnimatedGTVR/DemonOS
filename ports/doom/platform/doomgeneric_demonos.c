#include "doomgeneric_demonos.h"

#include <demon/input.h>
#include <demon/portkit.h>
#include <stdint.h>

#include "doomgeneric.h"
#include "doomkeys.h"

static struct demon_doom_backend active_backend;

void demon_doom_install_backend(const struct demon_doom_backend *backend) {
    if (backend == 0) {
        active_backend.present = 0;
        active_backend.set_title = 0;
        active_backend.poll_input = 0;
        active_backend.context = 0;
        return;
    }
    active_backend = *backend;
}

void DG_Init(void) {
    /* PortKit is initialized by the executable before doomgeneric_Create so
       its bounded heap size remains an explicit application policy. */
}

void DG_DrawFrame(void) {
    if (active_backend.present != 0 && DG_ScreenBuffer != 0) {
        active_backend.present(DG_ScreenBuffer, DOOMGENERIC_RESX,
                               DOOMGENERIC_RESY, active_backend.context);
    }
}

void DG_SleepMs(uint32_t milliseconds) {
    demon_port_sleep_ms(milliseconds);
}

uint32_t DG_GetTicksMs(void) {
    return (uint32_t)demon_port_ticks_ms();
}

static unsigned char translate_key(const struct input_event *event) {
    switch (event->code) {
        case 0x01u: return KEY_ESCAPE;
        case 0x0Eu: return KEY_BACKSPACE;
        case 0x0Fu: return KEY_TAB;
        case 0x1Cu: return KEY_ENTER;
        case 0x1Du: return KEY_FIRE;
        case 0x2Au:
        case 0x36u: return KEY_RSHIFT;
        case 0x38u: return KEY_RALT;
        case 0x39u: return KEY_USE;
        case 0x3Bu: return KEY_F1;
        case 0x3Cu: return KEY_F2;
        case 0x3Du: return KEY_F3;
        case 0x3Eu: return KEY_F4;
        case 0x3Fu: return KEY_F5;
        case 0x40u: return KEY_F6;
        case 0x41u: return KEY_F7;
        case 0x42u: return KEY_F8;
        case 0x43u: return KEY_F9;
        case 0x44u: return KEY_F10;
        case 0x57u: return KEY_F11;
        case 0x58u: return KEY_F12;
        case INPUT_KEY_ARROW_UP: return KEY_UPARROW;
        case INPUT_KEY_ARROW_DOWN: return KEY_DOWNARROW;
        case INPUT_KEY_ARROW_LEFT: return KEY_LEFTARROW;
        case INPUT_KEY_ARROW_RIGHT: return KEY_RIGHTARROW;
        default: break;
    }

    /* Printable key events already carry the layout-translated character.
       Doom expects alphabetic bindings in upper case. */
    if (event->value >= 'a' && event->value <= 'z') {
        return (unsigned char)(event->value - 'a' + 'A');
    }
    if (event->value == '\n' || event->value == '\r') return KEY_ENTER;
    if (event->value == '\b') return KEY_BACKSPACE;
    if (event->value > 0 && event->value <= 255) {
        return (unsigned char)event->value;
    }

    /* Key-up events intentionally have no translated character in the
       unified ABI. Derive their stable binding from the physical set-1 code
       so Doom receives matching press/release identities instead of leaving
       movement and weapon keys stuck down. */
    static const unsigned char released_ascii[0x3Au] = {
        [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',
        [0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',[0x0C]='-',[0x0D]='=',
        [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',[0x15]='Y',
        [0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',[0x1A]='[',[0x1B]=']',
        [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',
        [0x24]='J',[0x25]='K',[0x26]='L',[0x27]=';',[0x28]='\'',
        [0x29]='`',[0x2B]='\\',[0x2C]='Z',[0x2D]='X',[0x2E]='C',
        [0x2F]='V',[0x30]='B',[0x31]='N',[0x32]='M',[0x33]=',',
        [0x34]='.',[0x35]='/'
    };
    if (event->code < sizeof(released_ascii)) return released_ascii[event->code];
    return 0;
}

int DG_GetKey(int *pressed, unsigned char *key) {
    struct input_event event;
    for (;;) {
        const int received = active_backend.poll_input != 0 ?
            active_backend.poll_input(&event, active_backend.context) :
            demon_port_poll_input(&event);
        if (!received) break;
        if (event.type != INPUT_KEY_DOWN && event.type != INPUT_KEY_UP) continue;
        const unsigned char translated = translate_key(&event);
        if (translated == 0) continue;
        *pressed = event.type == INPUT_KEY_DOWN;
        *key = translated;
        return 1;
    }
    return 0;
}

void DG_SetWindowTitle(const char *title) {
    if (active_backend.set_title != 0) {
        active_backend.set_title(title, active_backend.context);
    }
}
