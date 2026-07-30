// Real end-to-end proof that lib/dlib/dlib.c does what it claims: create a
// real compositor window, draw into it with the Xlib-named calls, and
// present it -- not a fabricated success, an actual on-screen window this
// program's own boot-test smoke check can screendump and verify.
#include "../../lib/dlib/dlib.h"

int dlib_hello_main(void) {
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) return 1;

    Window window = XCreateSimpleWindow(display, DlibNone, 40, 40, 80, 60,
                                         0, 0x000000u, 0x0B0F16u);
    if (window == DlibNone) return 2;

    DlibGC *gc = XCreateGC(display);
    if (gc == NULL) return 3;
    XSetForeground(display, gc, 0x6FE08Fu);
    XFillRectangle(display, window, gc, 4, 4, 72, 20);
    XSetForeground(display, gc, 0xF8FAFCu);
    XDrawString(display, window, gc, 6, 8, "DLIB", 4);
    XDrawLine(display, window, gc, 4, 28, 76, 28);

    if (XMapWindow(display, window) != 0) return 4;
    if (DlibPresent(display) != 0) return 5;

    XEvent event;
    while (XNextEvent(display, &event) == 0) {
        if (event.type == DlibClientMessage) break;
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}
