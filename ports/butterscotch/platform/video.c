/* Native retained-surface backend for the Butterscotch runner. */

#include "video.h"
#include "vm_fixture.h"

#include <X11/Xlib.h>
#include <demon/c_app.h>
#include <demon/demonx.h>
#include <demon/portkit.h>
#include <string.h>

#define INVALID_HANDLE UINT64_MAX

bool DemonButterscotchVideo_open(DemonButterscotchVideo *video,
                                 const DemonDecodedImage *image,
                                 bool createWindow, uint32_t windowWidth,
                                 uint32_t windowHeight) {
    if (video == NULL || image == NULL || image->pixels == NULL ||
        image->width == 0u || image->height == 0u) return false;
    memset(video, 0, sizeof(*video));
    video->factory = demon_service_open(9u);
    video->surface = INVALID_HANDLE;
    if (video->factory == INVALID_HANDLE) return false;
    video->surface = demon_surface_create(video->factory, image->width,
                                          image->height);
    if (video->surface == INVALID_HANDLE) goto fail;
    video->width = image->width;
    video->height = image->height;
    video->uploadPixelCapacity = (uint64_t)image->width * image->height;
    if (video->uploadPixelCapacity > SIZE_MAX / sizeof(*video->uploadPixels))
        goto fail;
    video->uploadPixels = demon_port_malloc((size_t)video->uploadPixelCapacity *
                                             sizeof(*video->uploadPixels));
    if (video->uploadPixels == NULL) goto fail;
    video->uploadAllocations = 1u;
    if (!DemonButterscotchVideo_present(video, image)) goto fail;

    if (!createWindow) return true;
    Display *display = XOpenDisplay(":5");
    if (display == NULL) goto fail;
    video->display = display;
    const unsigned int scale = image->width <= 160u ? 3u : 1u;
    if (windowWidth == 0u) windowWidth = image->width * scale;
    if (windowHeight == 0u) windowHeight = image->height * scale;
    Window window = XCreateSimpleWindow(display, XDefaultRootWindow(display),
        96, 88, windowWidth, windowHeight,
        0u, 0u, 0xff09111fu);
    if (window == None || !DemonXAttachSurface(display, window,
            video->surface, image->width, image->height)) goto fail;
    (void)XStoreName(display, window, "Butterscotch Runner");
    (void)XSelectInput(display, window, KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
        FocusChangeMask | StructureNotifyMask);
    (void)XMapWindow(display, window);
    video->window = window;
    video->windowWidth = windowWidth;
    video->windowHeight = windowHeight;
    video->focused = true;
    return true;

fail:
    DemonButterscotchVideo_close(video);
    return false;
}

bool DemonButterscotchVideo_present(DemonButterscotchVideo *video,
                                    const DemonDecodedImage *image) {
    if (video == NULL || image == NULL || image->pixels == NULL ||
        video->surface == INVALID_HANDLE || image->width != video->width ||
        image->height != video->height) return false;
    const uint64_t count = (uint64_t)image->width * image->height;
    if (video->uploadPixels == NULL || count > video->uploadPixelCapacity)
        return false;
    for (uint64_t i = 0u; i < count; ++i) {
        const uint8_t *rgba = image->pixels + i * 4u;
        video->uploadPixels[i] = ((uint32_t)rgba[3] << 24u) |
                                 ((uint32_t)rgba[0] << 16u) |
                                 ((uint32_t)rgba[1] << 8u) | rgba[2];
    }
    const bool written = demon_surface_write(video->surface,
                                               video->uploadPixels, count,
                                               0u) == count;
    if (!written || demon_surface_damage(video->surface, 0u, 0u,
            image->width, image->height) == INVALID_HANDLE) return false;
    video->uploadedPixels += count;
    ++video->frames;
    return true;
}

static uint32_t key_mask(unsigned int code) {
    if (code == 0x4bu || code == 0x1eu) return DEMON_VM_KEY_LEFT;
    if (code == 0x4du || code == 0x20u) return DEMON_VM_KEY_RIGHT;
    if (code == 0x48u || code == 0x11u) return DEMON_VM_KEY_UP;
    if (code == 0x50u || code == 0x1fu) return DEMON_VM_KEY_DOWN;
    return 0u;
}

void DemonButterscotchVideo_pump(DemonButterscotchVideo *video) {
    if (video == NULL || video->display == NULL) return;
    Display *display = (Display *)video->display;
    while (XPending(display)) {
        XEvent event;
        if (XNextEvent(display, &event) != 0) break;
        ++video->events;
        if (event.type == ClientMessage &&
            event.xclient.message_type == DEMONX_CLIENT_CLOSE_MESSAGE) {
            video->quit = true;
        } else if (event.type == KeyPress || event.type == KeyRelease) {
            ++video->keys;
            const uint32_t mask = key_mask(event.xkey.keycode);
            const uint32_t previous = video->keyMask;
            if (event.type == KeyPress) video->keyMask |= mask;
            else video->keyMask &= ~mask;
            video->inputChanged |= previous != video->keyMask;
            if (event.type == KeyPress && event.xkey.keycode == 0x01u)
                video->quit = true;
        } else if (event.type == MotionNotify) {
            ++video->pointerEvents;
            video->pointerX = event.xmotion.x;
            video->pointerY = event.xmotion.y;
            video->inputChanged = true;
        } else if (event.type == ButtonPress || event.type == ButtonRelease) {
            ++video->pointerEvents;
            video->pointerX = event.xbutton.x;
            video->pointerY = event.xbutton.y;
            if (event.xbutton.button >= 1u && event.xbutton.button <= 3u) {
                const uint32_t mask = 1u << (event.xbutton.button - 1u);
                if (event.type == ButtonPress) video->buttonMask |= mask;
                else video->buttonMask &= ~mask;
            }
            video->inputChanged = true;
        } else if (event.type == FocusIn) {
            video->focused = true;
        } else if (event.type == FocusOut) {
            video->focused = false;
            if (video->keyMask != 0u || video->buttonMask != 0u)
                video->inputChanged = true;
            video->keyMask = 0u;
            video->buttonMask = 0u;
        } else if (event.type == ConfigureNotify &&
                   event.xconfigure.width > 0 && event.xconfigure.height > 0) {
            video->windowWidth = (uint32_t)event.xconfigure.width;
            video->windowHeight = (uint32_t)event.xconfigure.height;
        }
    }
}

void DemonButterscotchVideo_close(DemonButterscotchVideo *video) {
    if (video == NULL) return;
    if (video->display != NULL) (void)XCloseDisplay((Display *)video->display);
    if (video->surface != INVALID_HANDLE) demon_handle_close(video->surface);
    if (video->factory != INVALID_HANDLE) demon_handle_close(video->factory);
    demon_port_free(video->uploadPixels);
    video->uploadPixels = NULL;
    video->uploadPixelCapacity = 0u;
    video->display = NULL;
    video->surface = INVALID_HANDLE;
    video->factory = INVALID_HANDLE;
}
