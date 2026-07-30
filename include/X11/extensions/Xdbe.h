#ifndef DEMONOS_X11_EXTENSIONS_XDBE_H
#define DEMONOS_X11_EXTENSIONS_XDBE_H

#include <X11/Xlib.h>

typedef Drawable XdbeBackBuffer;
typedef unsigned char XdbeSwapAction;

#define XdbeUndefined 0
#define XdbeBackground 1
#define XdbeUntouched 2
#define XdbeCopied 3

typedef struct {
    Window swap_window;
    XdbeSwapAction swap_action;
} XdbeSwapInfo;

Status XdbeQueryExtension(Display *display, int *major_version_return,
                          int *minor_version_return);
XdbeBackBuffer XdbeAllocateBackBufferName(Display *display, Window window,
                                           XdbeSwapAction swap_action);
Status XdbeDeallocateBackBufferName(Display *display,
                                     XdbeBackBuffer back_buffer);
Status XdbeSwapBuffers(Display *display, XdbeSwapInfo *swap_info,
                       int count);

#endif
