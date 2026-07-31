#ifndef DEMONOS_X11_EXTENSIONS_SCRNSAVER_H
#define DEMONOS_X11_EXTENSIONS_SCRNSAVER_H

#include <X11/Xlib.h>

typedef struct { Window window; int state, kind; unsigned long idle; } XScreenSaverInfo;
Bool XScreenSaverQueryExtension(Display *, int *, int *);
Status XScreenSaverQueryInfo(Display *, Drawable, XScreenSaverInfo *);

#endif
