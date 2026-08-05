#ifndef DEMONOS_X11_EXTENSIONS_XINERAMA_H
#define DEMONOS_X11_EXTENSIONS_XINERAMA_H

#include <X11/Xlib.h>

typedef struct {
    int screen_number;
    short x_org, y_org;
    short width, height;
} XineramaScreenInfo;

Bool XineramaIsActive(Display *display);
XineramaScreenInfo *XineramaQueryScreens(Display *display,
                                         int *screen_count_return);

#endif
