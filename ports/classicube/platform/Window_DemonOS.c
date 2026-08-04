#include <demon/c_app.h>
#include <demon/input.h>
#include <demon/portkit.h>

#include "Bitmap.h"
#include "Errors.h"
#include "Event.h"
#include "Input.h"
#include "Platform.h"
#include "String_.h"
#include "Window.h"
#include "Window_DemonOS.h"

#include <stddef.h>
#include <stdint.h>

#define DEMON_INVALID_HANDLE UINT64_MAX
#define DEMON_DISPLAY_SERVICE 7u
#define DEMON_INPUT_SERVICE 8u
#define DEMON_SURFACE_SERVICE 9u

struct demon_display_info {
    uint64_t width, height, stride_pixels, format, max_transfer_pixels;
};

struct demon_display_submit {
    uint64_t x, y, width, height, pixels, flags;
};

struct _DisplayData DisplayInfo;
struct cc_window WindowInfo;
struct cc_window Window_Alt;

static uint64_t display_handle = DEMON_INVALID_HANDLE;
static uint64_t input_handle = DEMON_INVALID_HANDLE;
static uint64_t surface_factory = DEMON_INVALID_HANDLE;
static uint64_t surface_handle = DEMON_INVALID_HANDLE;
static uint64_t present_count;
static uint64_t input_count;
static uint32_t last_checksum;
static bool display_available;

static int translate_key(uint16_t code) {
    static const cc_uint8 letters[0x33] = {
        [0x10] = CCKEY_Q, [0x11] = CCKEY_W, [0x12] = CCKEY_E,
        [0x13] = CCKEY_R, [0x14] = CCKEY_T, [0x15] = CCKEY_Y,
        [0x16] = CCKEY_U, [0x17] = CCKEY_I, [0x18] = CCKEY_O,
        [0x19] = CCKEY_P, [0x1E] = CCKEY_A, [0x1F] = CCKEY_S,
        [0x20] = CCKEY_D, [0x21] = CCKEY_F, [0x22] = CCKEY_G,
        [0x23] = CCKEY_H, [0x24] = CCKEY_J, [0x25] = CCKEY_K,
        [0x26] = CCKEY_L, [0x2C] = CCKEY_Z, [0x2D] = CCKEY_X,
        [0x2E] = CCKEY_C, [0x2F] = CCKEY_V, [0x30] = CCKEY_B,
        [0x31] = CCKEY_N, [0x32] = CCKEY_M,
    };
    if (code < sizeof(letters) && letters[code]) return letters[code];
    if (code >= 0x02u && code <= 0x0Au) return CCKEY_1 + (int)code - 0x02;
    switch (code) {
    case 0x0Bu: return CCKEY_0;
    case 0x01u: return CCKEY_ESCAPE;
    case 0x0Eu: return CCKEY_BACKSPACE;
    case 0x0Fu: return CCKEY_TAB;
    case 0x1Cu: return CCKEY_ENTER;
    case 0x1Du: return CCKEY_LCTRL;
    case 0x2Au: return CCKEY_LSHIFT;
    case 0x36u: return CCKEY_RSHIFT;
    case 0x38u: return CCKEY_LALT;
    case 0x39u: return CCKEY_SPACE;
    case INPUT_KEY_ARROW_UP: return CCKEY_UP;
    case INPUT_KEY_ARROW_DOWN: return CCKEY_DOWN;
    case INPUT_KEY_ARROW_LEFT: return CCKEY_LEFT;
    case INPUT_KEY_ARROW_RIGHT: return CCKEY_RIGHT;
    case INPUT_KEY_SUPER_LEFT: return CCKEY_LWIN;
    case INPUT_KEY_SUPER_RIGHT: return CCKEY_RWIN;
    default: return INPUT_NONE;
    }
}

int DemonOS_TranslateInputEvent(const struct input_event *event,
                                struct demoni_cc_input *out) {
    if (event == NULL || out == NULL) return 0;
    out->key = INPUT_NONE; out->pressed = 0;
    out->pointer_x = event->x; out->pointer_y = event->y;
    out->delta_x = 0; out->delta_y = 0; out->wheel = 0;
    switch (event->type) {
    case INPUT_KEY_DOWN:
    case INPUT_KEY_UP:
        out->key = translate_key(event->code);
        out->pressed = event->type == INPUT_KEY_DOWN;
        return out->key != INPUT_NONE;
    case INPUT_MOUSE_BUTTON_DOWN:
    case INPUT_MOUSE_BUTTON_UP:
        out->key = event->code == INPUT_MOUSE_LEFT ? CCMOUSE_L :
                   event->code == INPUT_MOUSE_RIGHT ? CCMOUSE_R :
                   event->code == INPUT_MOUSE_MIDDLE ? CCMOUSE_M : INPUT_NONE;
        out->pressed = event->type == INPUT_MOUSE_BUTTON_DOWN;
        return out->key != INPUT_NONE;
    case INPUT_MOUSE_MOVE:
        out->delta_x = event->delta_x; out->delta_y = event->delta_y;
        return 1;
    case INPUT_MOUSE_SCROLL:
        out->wheel = event->value != 0 ? event->value : event->delta_y;
        return out->wheel != 0;
    default: return 0;
    }
}

int DemonOS_ApplyInputEvent(const struct input_event *event) {
    struct demoni_cc_input translated;
    if (!DemonOS_TranslateInputEvent(event, &translated)) return 0;

    switch (event->type) {
    case INPUT_KEY_DOWN:
    case INPUT_KEY_UP:
    case INPUT_MOUSE_BUTTON_DOWN:
    case INPUT_MOUSE_BUTTON_UP:
        Input_Set(translated.key, translated.pressed);
        if (translated.key == CCKEY_ESCAPE && translated.pressed)
            Window_RequestClose();
        break;
    case INPUT_MOUSE_MOVE:
        Pointer_SetPosition(0, translated.pointer_x, translated.pointer_y);
        if (translated.delta_x || translated.delta_y) {
            Event_RaiseRawMove(&PointerEvents.RawMoved,
                               (float)translated.delta_x,
                               (float)translated.delta_y);
        }
        break;
    case INPUT_MOUSE_SCROLL:
        Mouse_ScrollVWheel((float)translated.wheel);
        break;
    default: return 0;
    }
    return 1;
}

static void close_handle(uint64_t *handle) {
    if (*handle != DEMON_INVALID_HANDLE) (void)demon_handle_close(*handle);
    *handle = DEMON_INVALID_HANDLE;
}

void Window_PreInit(void) { }

void Window_Init(void) {
    struct demon_display_info info;
    display_handle  = demon_service_open(DEMON_DISPLAY_SERVICE);
    input_handle    = demon_service_open(DEMON_INPUT_SERVICE);
    surface_factory = demon_service_open(DEMON_SURFACE_SERVICE);
    display_available = display_handle != DEMON_INVALID_HANDLE &&
        demon_display_info(display_handle, &info) != DEMON_INVALID_HANDLE;
    if (!display_available) {
        info.width = 640u; info.height = 480u;
    }

    DisplayInfo.Depth = 32;
    DisplayInfo.ScaleX = 1.0f; DisplayInfo.ScaleY = 1.0f;
    DisplayInfo.Width = (int)info.width; DisplayInfo.Height = (int)info.height;
    DisplayInfo.CursorVisible = true;
    DisplayInfo.FullRedraw = false;

    Window_Main.Width = DisplayInfo.Width;
    Window_Main.Height = DisplayInfo.Height;
    Window_Main.Exists = false;
    Window_Main.Focused = true;
    Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
    Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;
    present_count = 0u;
    input_count = 0u;
    last_checksum = 0u;
}

void Window_Free(void) {
    close_handle(&surface_handle);
    close_handle(&surface_factory);
    close_handle(&input_handle);
    close_handle(&display_handle);
}

static void create_window(int width, int height, cc_bool is3D) {
    Window_Main.Width  = width;
    Window_Main.Height = height;
    Window_Main.Is3D   = is3D;
    Window_Main.Exists = true;
    Window_Main.Focused = true;
}

void Window_Create2D(int width, int height) { create_window(width, height, false); }
void Window_Create3D(int width, int height) { create_window(width, height, true); }
void Window_Destroy(void) { close_handle(&surface_handle); Window_Main.Exists = false; }
void Window_SetTitle(const cc_string *title) { (void)title; }
void Clipboard_GetText(cc_string *value) { value->length = 0; }
void Clipboard_SetText(const cc_string *value) { (void)value; }
int Window_GetWindowState(void) { return WINDOW_STATE_NORMAL; }
cc_result Window_EnterFullscreen(void) { return ERR_NOT_SUPPORTED; }
cc_result Window_ExitFullscreen(void) { return ERR_NOT_SUPPORTED; }
int Window_IsObscured(void) { return 0; }
void Window_Show(void) { Window_Main.Focused = true; }
void Window_SetSize(int width, int height) { Window_Main.Width = width; Window_Main.Height = height; }
void Window_RequestClose(void) { Window_Main.Exists = false; }

void Window_ProcessEvents(float delta) {
    struct input_event event;
    (void)delta;
    if (input_handle == DEMON_INVALID_HANDLE) return;
    while (demon_input_poll(input_handle, &event) == 0u) {
        if (DemonOS_ApplyInputEvent(&event)) ++input_count;
    }
}

void Gamepads_PreInit(void) { }
void Gamepads_Init(void) { }
void Gamepads_Process(float delta) { (void)delta; }
void Cursor_SetPosition(int x, int y) { (void)x; (void)y; }
void Window_EnableRawMouse(void) { }
void Window_UpdateRawMouse(void) { }
void Window_DisableRawMouse(void) { }

void Window_AllocFramebuffer(struct Bitmap *bmp, int width, int height) {
    Bitmap_TryAllocate(bmp, width, height);
}

void Window_DrawFramebuffer(Rect2D r, struct Bitmap *bmp) {
    if (bmp == NULL || bmp->scan0 == NULL || bmp->width <= 0 || bmp->height <= 0)
        return;
    uint64_t count = (uint64_t)(unsigned)bmp->width * (uint64_t)(unsigned)bmp->height;
    uint32_t checksum = 2166136261u;
    for (uint64_t i = 0u; i < count; ++i) {
        checksum ^= bmp->scan0[i]; checksum *= 16777619u;
    }
    last_checksum = checksum;
    if (display_handle == DEMON_INVALID_HANDLE) return;

    /* The retained surface feeds the compositor's shared-window path, but a
       full-size 640x480 surface can exceed the kernel's shared surface arena,
       so a failed create/write must not block presentation. Fall back to a
       direct submit (the same route doom uses), which needs no surface. */
    if (surface_factory != DEMON_INVALID_HANDLE &&
        r.x >= 0 && r.y >= 0 && r.width >= 0 && r.height >= 0 &&
        r.x + r.width <= bmp->width && r.y + r.height <= bmp->height) {
        if (surface_handle == DEMON_INVALID_HANDLE) {
            surface_handle = demon_surface_create(surface_factory,
                                  (uint64_t)bmp->width, (uint64_t)bmp->height);
        }
        if (surface_handle != DEMON_INVALID_HANDLE &&
            demon_surface_write(surface_handle, bmp->scan0, count, 0u) == count) {
            (void)demon_surface_damage(surface_handle, (uint64_t)r.x, (uint64_t)r.y,
                                       (uint64_t)r.width, (uint64_t)r.height);
        }
    }
    struct demon_display_submit request = {
        .x = DisplayInfo.Width > bmp->width ?
             (uint64_t)(DisplayInfo.Width - bmp->width) / 2u : 0u,
        .y = DisplayInfo.Height > bmp->height ?
             (uint64_t)(DisplayInfo.Height - bmp->height) / 2u : 0u,
        .width = (uint64_t)bmp->width, .height = (uint64_t)bmp->height,
        .pixels = (uint64_t)(uintptr_t)bmp->scan0, .flags = 1u,
    };
    if (demon_display_submit(display_handle, &request) == 0u) {
        ++present_count;
    }
}

void Window_FreeFramebuffer(struct Bitmap *bmp) {
    if (bmp == NULL) return;
    Mem_Free(bmp->scan0);
    bmp->scan0 = NULL; bmp->width = 0; bmp->height = 0;
    close_handle(&surface_handle);
}

void OnscreenKeyboard_Open(struct OpenKeyboardArgs *args) { (void)args; }
void OnscreenKeyboard_SetText(const cc_string *text) { (void)text; }
void OnscreenKeyboard_Close(void) { }
void Window_LockLandscapeOrientation(cc_bool lock) { (void)lock; }
void Window_ShowDialog(const char *title, const char *msg) {
    Platform_LogConst(title); Platform_LogConst(msg);
}
cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs *args) {
    (void)args; return ERR_NOT_SUPPORTED;
}
cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs *args) {
    (void)args; return ERR_NOT_SUPPORTED;
}

uint64_t DemonOS_WindowPresentCount(void) { return present_count; }
uint64_t DemonOS_WindowInputCount(void) { return input_count; }
uint32_t DemonOS_WindowLastChecksum(void) { return last_checksum; }
int DemonOS_WindowDisplayAvailable(void) { return display_available ? 1 : 0; }
