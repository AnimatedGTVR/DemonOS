#ifndef DEMON_INPUT_H
#define DEMON_INPUT_H

#include <stdbool.h>
#include <stdint.h>

enum input_event_type {
    INPUT_KEY_DOWN = 1,
    INPUT_KEY_UP,
    INPUT_MOUSE_MOVE,
    INPUT_MOUSE_BUTTON_DOWN,
    INPUT_MOUSE_BUTTON_UP,
    INPUT_MOUSE_SCROLL,
};

enum input_mouse_button {
    INPUT_MOUSE_LEFT = 1,
    INPUT_MOUSE_RIGHT = 2,
    INPUT_MOUSE_MIDDLE = 3,
};

enum input_modifiers {
    INPUT_MOD_SHIFT = 1u << 0,
    INPUT_MOD_CAPS_LOCK = 1u << 1,
    INPUT_MOD_ALT = 1u << 2,
    INPUT_MOD_SUPER = 1u << 3,
};

/* Set-1 extended scan codes retain their E0 prefix in bit 8. Keeping the
   physical key identity in the device-independent event avoids collisions
   with keypad keys while applications remain isolated from raw PS/2 packets. */
enum input_key_code {
    INPUT_KEY_ARROW_UP = 0x148,
    INPUT_KEY_ARROW_LEFT = 0x14B,
    INPUT_KEY_ARROW_RIGHT = 0x14D,
    INPUT_KEY_ARROW_DOWN = 0x150,
    INPUT_KEY_SUPER_LEFT = 0x15B,
    INPUT_KEY_SUPER_RIGHT = 0x15C,
};

struct input_event {
    uint64_t timestamp;
    uint16_t type;
    uint16_t code;
    int32_t x;
    int32_t y;
    int32_t delta_x;
    int32_t delta_y;
    int32_t value;
    uint32_t modifiers;
};

void input_init(void);
bool input_publish(const struct input_event *event);
bool input_poll(struct input_event *event);
bool input_wait(uint32_t pid, uint64_t user_address);
bool input_cancel_wait(uint32_t pid);
void input_discard_pending(void);
uint32_t input_pending(void);
uint64_t input_dropped(void);
uint64_t input_userspace_deliveries(void);
uint64_t input_coalesced(void);
bool input_self_test(void);

#endif
