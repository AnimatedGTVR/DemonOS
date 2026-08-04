#ifndef CLASSICUBE_WINDOW_DEMONOS_H
#define CLASSICUBE_WINDOW_DEMONOS_H

#include <demon/input.h>
#include <stdint.h>

struct demoni_cc_input {
    int key;
    int pressed;
    int pointer_x, pointer_y;
    int delta_x, delta_y;
    int wheel;
};

int DemonOS_TranslateInputEvent(const struct input_event *event,
                                struct demoni_cc_input *translated);
int DemonOS_ApplyInputEvent(const struct input_event *event);
uint64_t DemonOS_WindowPresentCount(void);
uint64_t DemonOS_WindowInputCount(void);
uint32_t DemonOS_WindowLastChecksum(void);
int DemonOS_WindowDisplayAvailable(void);

#endif
