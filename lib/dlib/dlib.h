#ifndef DLIB_H
#define DLIB_H

/* Dlib: an Xlib-API-compatible shim for real DemonOS/EDDE apps (like a
   hypothetical native PekWM port) to link against instead of talking to
   MAKO-ABI syscalls directly.

   This is NOT the real X11 wire protocol, and it does not talk to
   demonx_server.c ("DemonX") either -- DemonX is a standalone protocol-
   conformance test harness with its own in-memory window table; it never
   calls demon_surface_create or reaches the real compositor, so nothing
   built on it would ever appear on screen. Dlib instead wraps the same
   real rendering path window_client.mko and apps/ede_calc already use:
   demon_window_message (include/demon/window.h) sent to the compositor's
   "desktop.compositor" channel, backed by a real demon_surface_create/
   write/share/damage-mapped surface.

   Scope of this first slice: window lifecycle and a minimal event queue
   only (XOpenDisplay/XCreateSimpleWindow/XMapWindow/XDestroyWindow/
   XNextEvent/XPending/XFlush/XCloseDisplay). No drawing primitives, no
   GC, no fonts, no atoms/properties yet -- PekWM's source calls roughly
   158 distinct Xlib functions total; this covers a small first fraction
   of them, enough to create, show, and receive events for one window.
   Everything here is real: it creates a real compositor-visible window
   and receives real forwarded input, it just doesn't cover fonts/drawing
   yet. */

#include <stdint.h>

typedef uint32_t Window;
typedef uint32_t Colormap;
typedef uint32_t Cursor;
typedef struct Display Display;

enum {
    DlibNone = 0,
};

/* Minimal event set, named to match the real Xlib constants PekWM's
   source switches on, but only the fields Dlib actually fills in are
   valid -- this is not the real 177-member Xlib XEvent union. */
enum DlibEventType {
    DlibKeyPress = 2,
    DlibKeyRelease = 3,
    DlibButtonPress = 4,
    DlibButtonRelease = 5,
    DlibMotionNotify = 6,
    DlibFocusIn = 9,
    DlibFocusOut = 10,
    DlibConfigureNotify = 22,
    DlibClientMessage = 33,
};

typedef struct {
    int type;
    Window window;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    unsigned int keycode;
    unsigned int button;
} XEvent;

Display *XOpenDisplay(const char *display_name);
int XCloseDisplay(Display *display);

/* Records geometry locally only -- no compositor message is sent until
   XMapWindow, matching real Xlib's create-then-map semantics. */
Window XCreateSimpleWindow(Display *display, Window parent, int x, int y,
                            unsigned int width, unsigned int height,
                            unsigned int border_width, unsigned long border,
                            unsigned long background);

/* Sends the real DEMON_WINDOW_CREATE message and shares a real backing
   surface -- this is the call that actually makes the window exist for
   the compositor and become visible. */
int XMapWindow(Display *display, Window window);

/* No separate "hide" opcode exists in the real protocol yet (see
   window.h's demon_window_opcode) -- this sends DEMON_WINDOW_CLOSE, same
   as XDestroyWindow, and is documented here as a known limitation rather
   than silently pretending to hide without really doing so. */
int XUnmapWindow(Display *display, Window window);
int XDestroyWindow(Display *display, Window window);

/* Blocks until a real forwarded event (key/pointer/focus/close) arrives
   on this window's own event channel and translates it into `event`. */
int XNextEvent(Display *display, XEvent *event);
/* Non-blocking: returns 1 and fills `event` if one is already queued,
   0 otherwise. */
int XPending(Display *display);
int XFlush(Display *display);
int XSync(Display *display, int discard);

/* Minimal GC: foreground color only (no line width/style/fill-rule/font
   state real Xlib's GC carries -- those are unused by anything wired up
   so far, added when a real caller needs them rather than speculatively).
   Draws land in the window's own local pixel buffer; nothing reaches the
   compositor until DlibPresent re-submits the surface, the same
   write+damage pair XMapWindow itself performs. */
typedef struct DlibGC DlibGC;
DlibGC *XCreateGC(Display *display);
int XFreeGC(DlibGC *gc);
int XSetForeground(Display *display, DlibGC *gc, unsigned long color);
int XFillRectangle(Display *display, Window window, DlibGC *gc,
                   int x, int y, unsigned int width, unsigned int height);
int XDrawLine(Display *display, Window window, DlibGC *gc,
             int x1, int y1, int x2, int y2);
int XDrawString(Display *display, Window window, DlibGC *gc,
               int x, int y, const char *text, int length);

/* Not a real Xlib call -- there is no XFlush-triggers-network-round-trip
   equivalent here since Dlib's transport is a direct syscall, not a
   buffered socket. Draw calls only touch the local pixel buffer; call
   this to actually push them to the compositor. */
int DlibPresent(Display *display);

#endif
