/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) DemonOS contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
/* sys_demonos.c -- vanilla Quake sys.h interface on top of DemonOS PortKit. */

#include <demon/portkit.h>

#include "quakedef.h"

#include <stdarg.h>
#include <stdint.h>

extern void IN_AccumulateMouse(int delta_x, int delta_y);

#define QUAKE_MAX_HANDLES 10u

struct demonos_file {
    struct demon_port_file port;
    int open;
};

static struct demonos_file sys_handles[QUAKE_MAX_HANDLES];
static int sys_handle_base = -1; /* -1: not initialised yet */

static void sys_handle_init(void) {
    unsigned i;
    for (i = 0u; i < QUAKE_MAX_HANDLES; ++i) sys_handles[i].open = 0;
    sys_handle_base = 0;
}

static int find_handle(void) {
    unsigned i;
    if (sys_handle_base < 0) sys_handle_init();
    for (i = 0u; i < QUAKE_MAX_HANDLES; ++i) {
        if (!sys_handles[i].open) return (int)i;
    }
    return -1;
}

int Sys_FileOpenRead(char *path, int *hndl) {
    int slot;
    if (path == NULL || hndl == NULL) return -1;
    slot = find_handle();
    if (slot < 0) return -1;
    if (!demon_port_open(&sys_handles[slot].port, path)) return -1;
    sys_handles[slot].open = 1;
    *hndl = slot;
    return (int)sys_handles[slot].port.size;
}

int Sys_FileOpenWrite(char *path) {
    int slot;
    if (path == NULL) return -1;
    slot = find_handle();
    if (slot < 0) return -1;
    if (!demon_port_create(&sys_handles[slot].port, path)) return -1;
    sys_handles[slot].open = 1;
    return slot;
}

void Sys_FileClose(int handle) {
    if (handle < 0 || (unsigned)handle >= QUAKE_MAX_HANDLES) return;
    if (!sys_handles[handle].open) return;
    demon_port_close(&sys_handles[handle].port);
    sys_handles[handle].open = 0;
}

void Sys_FileSeek(int handle, int position) {
    if (handle < 0 || (unsigned)handle >= QUAKE_MAX_HANDLES) return;
    if (!sys_handles[handle].open) return;
    if (position < 0) return;
    (void)demon_port_seek(&sys_handles[handle].port, (size_t)position);
}

int Sys_FileRead(int handle, void *dest, int count) {
    if (handle < 0 || (unsigned)handle >= QUAKE_MAX_HANDLES) return 0;
    if (!sys_handles[handle].open) return 0;
    if (count < 0 || dest == NULL) return 0;
    return (int)demon_port_read(&sys_handles[handle].port, dest, (size_t)count);
}

int Sys_FileWrite(int handle, void *data, int count) {
    if (handle < 0 || (unsigned)handle >= QUAKE_MAX_HANDLES) return 0;
    if (!sys_handles[handle].open) return 0;
    if (count < 0 || data == NULL) return 0;
    return (int)demon_port_write_file(&sys_handles[handle].port, data, (size_t)count);
}

int Sys_FileTime(char *path) {
    struct demon_port_file probe;
    if (path == NULL) return -1;
    if (!demon_port_open(&probe, path)) return -1;
    {
        int size = (int)probe.size;
        demon_port_close(&probe);
        return size;
    }
}

void Sys_mkdir(char *path) {
    /* The RAMFS backing store has no directories to create yet. */
    (void)path;
}

void Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length) {
    (void)startaddr;
    (void)length;
}

void Sys_DebugLog(char *file, char *fmt, ...) {
    char buf[512];
    va_list args;
    if (file == NULL) return;
    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    demon_port_write(buf);
}

void Sys_Error(char *error, ...) {
    char buf[512];
    va_list args;
    va_start(args, error);
    (void)vsnprintf(buf, sizeof(buf), error, args);
    va_end(args);
    demon_port_write("QUAKE_SYS_ERROR: ");
    demon_port_write(buf);
    demon_port_write("\n");
    demon_port_exit(1u);
}

void Sys_Printf(char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    demon_port_write(buf);
}

void Sys_Quit(void) {
    demon_port_write("QUAKE_SYS_QUIT\n");
    demon_port_exit(0u);
}

double Sys_FloatTime(void) {
    return (double)demon_port_ticks_ms() / 1000.0;
}

char *Sys_ConsoleInput(void) {
    /* Console input is owned by MakoBox; games use the event queue. */
    return NULL;
}

void Sys_Sleep(void) {
    demon_port_sleep_ms(1u);
}

/* Special keys identified by raw scancode (or the unified ABI's extended
   arrow codes); anything not listed falls through to event->value (already
   layout-translated ASCII, lowercase to match Quake's bind names like "w"). */
static int translate_special(uint16_t code) {
    switch (code) {
        case 0x01u: return K_ESCAPE;
        case 0x0Eu: return K_BACKSPACE;
        case 0x0Fu: return K_TAB;
        case 0x1Cu: return K_ENTER;
        case 0x39u: return K_SPACE;
        case 0x1Du: case 0x9Du: return K_CTRL;
        case 0x2Au: case 0x36u: return K_SHIFT;
        case 0x38u: case 0xB8u: return K_ALT;
        case 0x3Bu: return K_F1;
        case 0x3Cu: return K_F2;
        case 0x3Du: return K_F3;
        case 0x3Eu: return K_F4;
        case 0x3Fu: return K_F5;
        case 0x40u: return K_F6;
        case 0x41u: return K_F7;
        case 0x42u: return K_F8;
        case 0x43u: return K_F9;
        case 0x44u: return K_F10;
        case 0x57u: return K_F11;
        case 0x58u: return K_F12;
        case INPUT_KEY_ARROW_UP: return K_UPARROW;
        case INPUT_KEY_ARROW_DOWN: return K_DOWNARROW;
        case INPUT_KEY_ARROW_LEFT: return K_LEFTARROW;
        case INPUT_KEY_ARROW_RIGHT: return K_RIGHTARROW;
        default: return 0;
    }
}

/* Key-up events carry no translated ASCII in the unified ABI (event->value
   is only meaningful on key-down), so recover a stable identity for
   releases from the physical set-1 code -- same technique as the Doom
   port's released_ascii table, lowercase here to match Quake's keynums. */
static int translate_released_ascii(uint16_t code) {
    static const unsigned char released_ascii[0x3Au] = {
        [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',
        [0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',[0x0C]='-',[0x0D]='=',
        [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',
        [0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',[0x1A]='[',[0x1B]=']',
        [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',
        [0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',
        [0x29]='`',[0x2B]='\\',[0x2C]='z',[0x2D]='x',[0x2E]='c',
        [0x2F]='v',[0x30]='b',[0x31]='n',[0x32]='m',[0x33]=',',
        [0x34]='.',[0x35]='/'
    };
    if (code < sizeof(released_ascii)) return released_ascii[code];
    return 0;
}

static int translate_key(const struct input_event *event) {
    int special = translate_special(event->code);
    if (special != 0) return special;
    if (event->type == INPUT_KEY_DOWN) {
        if (event->value == '\n' || event->value == '\r') return K_ENTER;
        if (event->value == '\b') return K_BACKSPACE;
        if (event->value > 0 && event->value < 128) return event->value;
        return 0;
    }
    return translate_released_ascii(event->code);
}

void Sys_SendKeyEvents(void) {
    struct input_event event;
    while (demon_port_poll_input(&event)) {
        switch (event.type) {
            case INPUT_KEY_DOWN:
            case INPUT_KEY_UP: {
                int key = translate_key(&event);
                if (key != 0)
                    Key_Event(key, event.type == INPUT_KEY_DOWN);
                break;
            }
            case INPUT_MOUSE_MOVE:
                IN_AccumulateMouse(event.delta_x, event.delta_y);
                break;
            case INPUT_MOUSE_BUTTON_DOWN:
            case INPUT_MOUSE_BUTTON_UP: {
                int key = 0;
                if (event.code == INPUT_MOUSE_LEFT) key = K_MOUSE1;
                else if (event.code == INPUT_MOUSE_RIGHT) key = K_MOUSE2;
                else if (event.code == INPUT_MOUSE_MIDDLE) key = K_MOUSE3;
                if (key != 0)
                    Key_Event(key, event.type == INPUT_MOUSE_BUTTON_DOWN);
                break;
            }
            default:
                break;
        }
    }
}

void Sys_LowFPPrecision(void) { }
void Sys_HighFPPrecision(void) { }
void Sys_SetFPCW(void) { }
