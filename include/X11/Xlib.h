#ifndef DEMONOS_X11_XLIB_H
#define DEMONOS_X11_XLIB_H

/*
 * DemonOS Xlib compatibility surface.
 *
 * This intentionally starts with the core calls needed to prove a real
 * connection to DemonX.  It uses X11's wire values and public type names so
 * applications can migrate incrementally without acquiring hosted libX11.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _XDisplay Display;
typedef uint32_t XID;
typedef XID Window;
typedef XID Pixmap;
typedef XID Atom;
typedef XID Drawable;
typedef XID Font;
typedef XID Colormap;
typedef XID Cursor;
typedef struct _XFontSet *XFontSet;
typedef unsigned long KeySym;
typedef unsigned char KeyCode;
typedef struct _XGC *GC;
typedef int Bool;
typedef int Status;
typedef struct _XVisual {
    XID visualid;
#ifdef __cplusplus
    int c_class;
#else
    int class;
#endif
    unsigned long red_mask, green_mask, blue_mask;
    int bits_per_rgb;
    int map_entries;
} Visual;
typedef struct {
    Visual *visual;
    XID visualid;
    int screen, depth;
#ifdef __cplusplus
    int c_class;
#else
    int class;
#endif
    unsigned long red_mask, green_mask, blue_mask;
    int colormap_size, bits_per_rgb;
} XVisualInfo;
typedef struct {
    unsigned char *value;
    Atom encoding;
    int format;
    unsigned long nitems;
} XTextProperty;
typedef struct { char *res_name, *res_class; } XClassHint;
typedef struct {
    long flags;
    Bool input;
    int initial_state;
    Pixmap icon_pixmap;
    Window icon_window;
    int icon_x, icon_y;
    Pixmap icon_mask;
    XID window_group;
} XWMHints;
typedef struct {
    long flags;
    int x, y, width, height;
    int min_width, min_height, max_width, max_height;
    int width_inc, height_inc;
    struct { int x, y; } min_aspect, max_aspect;
    int base_width, base_height, win_gravity;
} XSizeHints;
typedef struct {
    int type;
    Display *display;
    XID resourceid;
    unsigned long serial;
    unsigned char error_code, request_code, minor_code;
} XErrorEvent;
typedef struct {
    int max_keypermod;
    KeyCode *modifiermap;
} XModifierKeymap;
typedef int (*XErrorHandler)(Display *, XErrorEvent *);
typedef int (*XAfterFunction)(Display *);

#define None 0u
#define False 0
#define True 1
#define AnyPropertyType 0u
#define ZPixmap 2
#define ParentRelative 1u
#define CopyFromParent 0u
#define InputOutput 1u
#define Above 0
#define Below 1
#define AllocNone 0
#define NoSymbol 0u
#define CoordModeOrigin 0
#define CoordModePrevious 1
#define Unsorted 0
#define TrueColor 4
#define VisualIDMask 0x1L

#define PropModeReplace 0
#define PropModePrepend 1
#define PropModeAppend 2

#define XA_ATOM 4u
#define XA_STRING 31u
#define XA_WINDOW 33u
#define XA_WM_NAME 39u

#define GCForeground (1L << 2)
#define GCFont (1L << 14)

#define MapNotify 19
#define KeyPress 2
#define KeyRelease 3
#define ButtonPress 4
#define ButtonRelease 5
#define MotionNotify 6
#define FocusIn 9
#define FocusOut 10
#define MapRequest 20
#define ReparentNotify 21
#define ConfigureNotify 22
#define ConfigureRequest 23
#define PropertyNotify 28
#define ClientMessage 33
#define UnmapNotify 18
#define IsUnmapped 0
#define IsUnviewable 1
#define IsViewable 2
#define StructureNotifyMask (1L << 17)
#define KeyPressMask (1L << 0)
#define KeyReleaseMask (1L << 1)
#define ButtonPressMask (1L << 2)
#define ButtonReleaseMask (1L << 3)
#define PointerMotionMask (1L << 6)
#define ExposureMask (1L << 15)
#define FocusChangeMask (1L << 21)
#define SubstructureNotifyMask (1L << 19)
#define SubstructureRedirectMask (1L << 20)
#define PropertyChangeMask (1L << 22)

#define PropertyNewValue 0
#define PropertyDelete 1
#define CurrentTime 0u
#define RevertToNone 0
#define RevertToPointerRoot 1
#define RevertToParent 2
#define NotifyNormal 0
#define NotifyNonlinear 3
#define GrabModeSync 0
#define GrabModeAsync 1
#define AnyKey 0
#define AnyButton 0
#define AnyModifier (1U << 15)
#define GrabSuccess 0
#define AlreadyGrabbed 1
#define GrabInvalidTime 2
#define GrabNotViewable 3
#define GrabFrozen 4
#define AsyncPointer 0
#define SyncPointer 1
#define ReplayPointer 2
#define AsyncKeyboard 3
#define SyncKeyboard 4
#define ReplayKeyboard 5
#define AsyncBoth 6
#define SyncBoth 7

#define CWX (1u << 0)
#define CWY (1u << 1)
#define CWWidth (1u << 2)
#define CWHeight (1u << 3)
#define CWBorderWidth (1u << 4)
#define CWSibling (1u << 5)
#define CWStackMode (1u << 6)
#define CWBackPixmap (1u << 0)
#define CWBackPixel (1u << 1)

typedef struct {
    short lbearing, rbearing, width, ascent, descent;
    unsigned short attributes;
} XCharStruct;

typedef struct {
    Font fid;
    unsigned int direction;
    unsigned int min_char_or_byte2, max_char_or_byte2;
    int min_byte1, max_byte1;
    Bool all_chars_exist;
    unsigned int default_char;
    int n_properties;
    void *properties;
    XCharStruct min_bounds, max_bounds;
    XCharStruct *per_char;
    int ascent, descent;
} XFontStruct;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window event;
    Window window;
    Bool override_redirect;
} XMapEvent;
typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
} XAnyEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root, subwindow;
    unsigned long time;
    int x, y, x_root, y_root;
    unsigned int state;
    unsigned int keycode;
    Bool same_screen;
    unsigned int value;
} XKeyEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root, subwindow;
    unsigned long time;
    int x, y, x_root, y_root;
    unsigned int state;
    unsigned int button;
    Bool same_screen;
} XButtonEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root, subwindow;
    unsigned long time;
    int x, y, x_root, y_root;
    unsigned int state;
    char is_hint;
    Bool same_screen;
} XMotionEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    int mode;
    int detail;
} XFocusChangeEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window parent;
    Window window;
} XMapRequestEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window event;
    Window window;
    int x, y;
    int width, height;
    int border_width;
    Window above;
    Bool override_redirect;
} XConfigureEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window parent;
    Window window;
    int x, y;
    int width, height;
    int border_width;
    Window above;
    int detail;
    unsigned long value_mask;
} XConfigureRequestEvent;

typedef struct {
    Pixmap background_pixmap;
    unsigned long background_pixel;
} XSetWindowAttributes;

typedef struct {
    int x, y;
    int width, height;
    int border_width;
    int depth;
    Visual *visual;
    Window root;
#ifdef __cplusplus
    int c_class;
#else
    int class;
#endif
    int bit_gravity, win_gravity;
    int backing_store;
    unsigned long backing_planes, backing_pixel;
    Bool save_under;
    unsigned long colormap;
    Bool map_installed;
    int map_state;
    long all_event_masks, your_event_mask, do_not_propagate_mask;
    Bool override_redirect;
    void *screen;
} XWindowAttributes;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window event;
    Window window;
    Window parent;
    int x, y;
    Bool override_redirect;
} XReparentEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window event;
    Window window;
    Bool from_configure;
} XUnmapEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Atom atom;
    unsigned long time;
    int state;
} XPropertyEvent;

typedef union {
    char b[20];
    short s[10];
    long l[5];
} XClientMessageData;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Atom message_type;
    int format;
    XClientMessageData data;
} XClientMessageEvent;

typedef struct {
    int x, y;
    int width, height;
    int border_width;
    Window sibling;
    int stack_mode;
} XWindowChanges;

typedef struct {
    unsigned long function;
    unsigned long plane_mask;
    unsigned long foreground;
    unsigned long background;
    Font font;
} XGCValues;

typedef struct {
    short x, y;
    unsigned short width, height;
} XRectangle;
typedef struct {
    XRectangle max_ink_extent;
    XRectangle max_logical_extent;
} XFontSetExtents;

typedef struct {
    short x, y;
} XPoint;

typedef struct {
    unsigned long pixel;
    unsigned short red, green, blue;
    char flags, pad;
} XColor;

typedef struct _XImage {
    int width, height;
    int xoffset;
    int format;
    char *data;
    int byte_order;
    int bitmap_unit;
    int bitmap_bit_order;
    int bitmap_pad;
    int depth;
    int bytes_per_line;
    int bits_per_pixel;
} XImage;

typedef union _XEvent {
    int type;
    XAnyEvent xany;
    XMapEvent xmap;
    XKeyEvent xkey;
    XButtonEvent xbutton;
    XMotionEvent xmotion;
    XFocusChangeEvent xfocus;
    XMapRequestEvent xmaprequest;
    XConfigureEvent xconfigure;
    XConfigureRequestEvent xconfigurerequest;
    XReparentEvent xreparent;
    XUnmapEvent xunmap;
    XPropertyEvent xproperty;
    XClientMessageEvent xclient;
    long pad[24];
} XEvent;

Display *XOpenDisplay(const char *display_name);
int XCloseDisplay(Display *display);
int XDefaultScreen(Display *display);
int XDefaultDepth(Display *display, int screen);
Visual *XDefaultVisual(Display *display, int screen);
int XDisplayWidth(Display *display, int screen);
int XDisplayHeight(Display *display, int screen);
#define DefaultScreen(dpy) XDefaultScreen(dpy)
#define DefaultDepth(dpy, screen) XDefaultDepth((dpy), (screen))
#define DefaultVisual(dpy, screen) XDefaultVisual((dpy), (screen))
#define DisplayWidth(dpy, screen) XDisplayWidth((dpy), (screen))
#define DisplayHeight(dpy, screen) XDisplayHeight((dpy), (screen))
Window XDefaultRootWindow(Display *display);
Window XCreateSimpleWindow(Display *display, Window parent, int x, int y,
                           unsigned int width, unsigned int height,
                           unsigned int border_width, unsigned long border,
                           unsigned long background);
Window XCreateWindow(Display *display, Window parent, int x, int y,
                     unsigned int width, unsigned int height,
                     unsigned int border_width, int depth,
                     unsigned int class_, Visual *visual,
                     unsigned long value_mask,
                     XSetWindowAttributes *attributes);
int XChangeWindowAttributes(Display *display, Window window,
                            unsigned long value_mask,
                            XSetWindowAttributes *attributes);
Status XGetWindowAttributes(Display *display, Window window,
                            XWindowAttributes *attributes);
int XSetWindowBackground(Display *display, Window window,
                         unsigned long background);
int XSetWindowBackgroundPixmap(Display *display, Window window,
                               Pixmap background_pixmap);
int XClearWindow(Display *display, Window window);
int XClearArea(Display *display, Window window, int x, int y,
               unsigned int width, unsigned int height, Bool exposures);
int XSelectInput(Display *display, Window window, long event_mask);
int XSetInputFocus(Display *display, Window focus, int revert_to,
                   unsigned long time);
int XGrabPointer(Display *display, Window window, Bool owner_events,
                 unsigned int event_mask, int pointer_mode,
                 int keyboard_mode, Window confine_to, Cursor cursor,
                 unsigned long time);
int XUngrabPointer(Display *display, unsigned long time);
int XGrabKeyboard(Display *display, Window window, Bool owner_events,
                  int pointer_mode, int keyboard_mode, unsigned long time);
int XUngrabKeyboard(Display *display, unsigned long time);
int XAllowEvents(Display *display, int mode, unsigned long time);
int XGrabServer(Display *display);
int XUngrabServer(Display *display);
Cursor XCreateFontCursor(Display *display, unsigned int shape);
int XDefineCursor(Display *display, Window window, Cursor cursor);
int XFreeCursor(Display *display, Cursor cursor);
int XGrabKey(Display *display, int keycode, unsigned int modifiers,
             Window window, Bool owner_events, int pointer_mode,
             int keyboard_mode);
int XUngrabKey(Display *display, int keycode, unsigned int modifiers,
               Window window);
int XGrabButton(Display *display, unsigned int button,
                unsigned int modifiers, Window window, Bool owner_events,
                unsigned int event_mask, int pointer_mode,
                int keyboard_mode, Window confine_to, Cursor cursor);
int XUngrabButton(Display *display, unsigned int button,
                  unsigned int modifiers, Window window);
int XMapWindow(Display *display, Window window);
int XMapRaised(Display *display, Window window);
int XUnmapWindow(Display *display, Window window);
int XReparentWindow(Display *display, Window window, Window parent,
                    int x, int y);
int XConfigureWindow(Display *display, Window window,
                     unsigned int value_mask, XWindowChanges *changes);
int XMoveResizeWindow(Display *display, Window window, int x, int y,
                      unsigned int width, unsigned int height);
int XMoveWindow(Display *display, Window window, int x, int y);
int XResizeWindow(Display *display, Window window,
                  unsigned int width, unsigned int height);
int XRaiseWindow(Display *display, Window window);
int XLowerWindow(Display *display, Window window);
int XSetWindowBorderWidth(Display *display, Window window,
                          unsigned int width);
int XMapSubwindows(Display *display, Window window);
int XRestackWindows(Display *display, Window *windows, int count);
int XDestroyWindow(Display *display, Window window);
int XAddToSaveSet(Display *display, Window window);
int XRemoveFromSaveSet(Display *display, Window window);
int XKillClient(Display *display, XID resource);
int XWarpPointer(Display *display, Window source, Window destination,
                 int source_x, int source_y, unsigned int source_width,
                 unsigned int source_height, int destination_x,
                 int destination_y);
Status XQueryTree(Display *display, Window window, Window *root_return,
                  Window *parent_return, Window **children_return,
                  unsigned int *child_count_return);
int XFree(void *data);
Atom XInternAtom(Display *display, const char *atom_name,
                 Bool only_if_exists);
Status XInternAtoms(Display *display, char **names, int count,
                    Bool only_if_exists, Atom *atoms_return);
char *XGetAtomName(Display *display, Atom atom);
int XChangeProperty(Display *display, Window window, Atom property,
                    Atom type, int format, int mode,
                    const unsigned char *data, int element_count);
int XDeleteProperty(Display *display, Window window, Atom property);
int XGetWindowProperty(Display *display, Window window, Atom property,
                       long long_offset, long long_length, Bool delete_after,
                       Atom requested_type, Atom *actual_type_return,
                       int *actual_format_return,
                       unsigned long *item_count_return,
                       unsigned long *bytes_after_return,
                       unsigned char **property_return);
Atom *XListProperties(Display *display, Window window,
                      int *property_count_return);
int XStoreName(Display *display, Window window, const char *name);
GC XCreateGC(Display *display, Drawable drawable,
             unsigned long value_mask, XGCValues *values);
int XChangeGC(Display *display, GC gc, unsigned long value_mask,
              XGCValues *values);
int XSetForeground(Display *display, GC gc, unsigned long foreground);
int XSetFont(Display *display, GC gc, Font font);
int XSetClipRectangles(Display *display, GC gc, int clip_x_origin,
                       int clip_y_origin, XRectangle *rectangles,
                       int count, int ordering);
XFontStruct *XLoadQueryFont(Display *display, const char *name);
int XFreeFont(Display *display, XFontStruct *font);
int XTextWidth(XFontStruct *font, const char *text, int count);
int XFreeGC(Display *display, GC gc);
int XFillRectangle(Display *display, Drawable drawable, GC gc,
                   int x, int y, unsigned int width, unsigned int height);
int XFillRectangles(Display *display, Drawable drawable, GC gc,
                    XRectangle *rectangles, int count);
int XDrawLine(Display *display, Drawable drawable, GC gc,
              int x1, int y1, int x2, int y2);
int XDrawRectangle(Display *display, Drawable drawable, GC gc,
                   int x, int y, unsigned int width, unsigned int height);
int XDrawLines(Display *display, Drawable drawable, GC gc,
               XPoint *points, int count, int mode);
int XDrawArc(Display *, Drawable, GC, int, int,
             unsigned int, unsigned int, int, int);
int XFillArc(Display *, Drawable, GC, int, int,
             unsigned int, unsigned int, int, int);
int XFillPolygon(Display *, Drawable, GC, XPoint *, int, int, int);
int XCopyArea(Display *display, Drawable source, Drawable destination, GC gc,
              int source_x, int source_y, unsigned int width,
              unsigned int height, int destination_x, int destination_y);
Pixmap XCreatePixmap(Display *display, Drawable drawable,
                     unsigned int width, unsigned int height,
                     unsigned int depth);
int XFreePixmap(Display *display, Pixmap pixmap);
XImage *XCreateImage(Display *display, Visual *visual, unsigned int depth,
                     int format, int offset, char *data,
                     unsigned int width, unsigned int height,
                     int bitmap_pad, int bytes_per_line);
int XDestroyImage(XImage *image);
unsigned long XGetPixel(XImage *image, int x, int y);
int XPutPixel(XImage *image, int x, int y, unsigned long pixel);
int XPutImage(Display *display, Drawable drawable, GC gc, XImage *image,
              int src_x, int src_y, int dest_x, int dest_y,
              unsigned int width, unsigned int height);
XImage *XGetImage(Display *display, Drawable drawable, int x, int y,
                  unsigned int width, unsigned int height,
                  unsigned long plane_mask, int format);
int XDrawString(Display *display, Drawable drawable, GC gc, int x, int y,
                const char *text, int length);
void XmbDrawString(Display *display, Drawable drawable, XFontSet font_set,
                   GC gc, int x, int y, const char *text, int length);
void Xutf8DrawString(Display *display, Drawable drawable, XFontSet font_set,
                     GC gc, int x, int y, const char *text, int length);
int XmbTextExtents(XFontSet font_set, const char *text, int length,
                   XRectangle *ink_return, XRectangle *logical_return);
int Xutf8TextExtents(XFontSet font_set, const char *text, int length,
                     XRectangle *ink_return, XRectangle *logical_return);
int XmbTextPropertyToTextList(Display *display, XTextProperty *property,
                              char ***list_return, int *count_return);
Status XSetWMProtocols(Display *display, Window window,
                       Atom *protocols, int count);
Status XGetWMProtocols(Display *display, Window window,
                       Atom **protocols, int *count);
Status XGetTransientForHint(Display *display, Window window,
                            Window *owner);
Status XGetClassHint(Display *display, Window window, XClassHint *hint);
Status XSetClassHint(Display *display, Window window, XClassHint *hint);
Status XGetTextProperty(Display *display, Window window,
                        XTextProperty *property, Atom atom);
XWMHints *XGetWMHints(Display *display, Window window);
int XSetWMHints(Display *display, Window window, XWMHints *hints);
XSizeHints *XAllocSizeHints(void);
void XFreeStringList(char **list);
Status XSendEvent(Display *display, Window window, Bool propagate,
                  long event_mask, XEvent *event);
Status XGetGeometry(Display *display, Window drawable, Window *root_return,
                    int *x_return, int *y_return,
                    unsigned int *width_return, unsigned int *height_return,
                    unsigned int *border_width_return,
                    unsigned int *depth_return);
Bool XQueryPointer(Display *display, Window window, Window *root,
                   Window *child, int *root_x, int *root_y,
                   int *window_x, int *window_y, unsigned int *mask);
XModifierKeymap *XGetModifierMapping(Display *display);
int XFreeModifiermap(XModifierKeymap *mapping);
KeySym *XGetKeyboardMapping(Display *display, KeyCode first, int count,
                            int *symbols_per_keycode);
int XRefreshKeyboardMapping(XEvent *event);
Bool XTranslateCoordinates(Display *, Window, Window, int, int,
                           int *, int *, Window *);
Window XGetSelectionOwner(Display *, Atom);
int XSetSelectionOwner(Display *, Atom, Window, unsigned long);
int XWindowEvent(Display *, Window, long, XEvent *);
Bool XCheckMaskEvent(Display *, long, XEvent *);
int XMaskEvent(Display *, long, XEvent *);
Bool XCheckTypedEvent(Display *, int, XEvent *);
Bool XCheckTypedWindowEvent(Display *, Window, int, XEvent *);
Bool XCheckWindowEvent(Display *, Window, long, XEvent *);
XFontSet XCreateFontSet(Display *, const char *, char ***, int *, char **);
int XFontsOfFontSet(XFontSet, XFontStruct ***, char ***);
void XFreeFontSet(Display *, XFontSet);
int XSetNormalHints(Display *, Window, XSizeHints *);
Status XGetWMNormalHints(Display *, Window, XSizeHints *, long *);
int XNextEvent(Display *display, XEvent *event_return);
int XPending(Display *display);

/* DemonX extension: attach a client-owned retained DemonOS surface to a
   window before XMapWindow. Returns non-zero on success. */
/* width/height describe the retained source surface. They may differ from
   the X window's logical size; DemonOS scales presentation and input. */
int DemonXAttachSurface(Display *display, Window window,
                        uint64_t surface, unsigned int width,
                        unsigned int height);
int XFlush(Display *display);
int XSync(Display *display, Bool discard);
int XPutBackEvent(Display *display, XEvent *event);
int XGetErrorText(Display *display, int code, char *buffer, int length);
XAfterFunction XSynchronize(Display *display, Bool enabled);
XAfterFunction XSetAfterFunction(Display *display, XAfterFunction function);
XErrorHandler XSetErrorHandler(XErrorHandler handler);
long XMaxRequestSize(Display *display);
Colormap XCreateColormap(Display *display, Window window, Visual *visual,
                         int allocation);
int XFreeColormap(Display *display, Colormap colormap);
int XInstallColormap(Display *display, Colormap colormap);
Status XParseColor(Display *display, Colormap colormap,
                   const char *specification, XColor *color);
Status XAllocNamedColor(Display *display, Colormap colormap,
                        const char *name, XColor *screen, XColor *exact);
int XFreeColors(Display *display, Colormap colormap,
                unsigned long *pixels, int count, unsigned long planes);
KeySym XStringToKeysym(const char *name);
KeyCode XKeysymToKeycode(Display *display, KeySym keysym);
KeySym XKeycodeToKeysym(Display *display, KeyCode keycode, int index);
int XLookupString(XKeyEvent *event, char *buffer, int bytes,
                  KeySym *keysym_return, void *compose);
unsigned long XVisualIDFromVisual(Visual *visual);
XVisualInfo *XGetVisualInfo(Display *display, long mask,
                            XVisualInfo *template_info,
                            int *count_return);

/* DemonX uses explicit client slots instead of Unix-domain display sockets.
 * NULL and ":1" select slot 1; ":2" through ":8" select independent slots. */

#ifdef __cplusplus
}
#endif

#endif
