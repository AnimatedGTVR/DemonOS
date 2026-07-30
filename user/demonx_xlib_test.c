#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xdbe.h>
#include <X11/extensions/Xinerama.h>
#include <stddef.h>
#include <stdint.h>

uint64_t demonx_xlib_test_main(void) {
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) return 160u;
    int extension_major = -1, extension_minor = -1;
    int screen_count = -1;
    if (XdbeQueryExtension(display, &extension_major, &extension_minor) ||
        extension_major != 0 || extension_minor != 0 ||
        XineramaIsActive(display) ||
        XineramaQueryScreens(display, &screen_count) != NULL ||
        screen_count != 0)
        return 173u;
    XVisualInfo visual_template = {
        .visualid = XVisualIDFromVisual(DefaultVisual(display, 0))
    };
    int visual_count = 0;
    XVisualInfo *visual_info =
        XGetVisualInfo(display, VisualIDMask, &visual_template, &visual_count);
    if (DefaultScreen(display) != 0 || DefaultDepth(display, 0) != 32 ||
        DisplayWidth(display, 0) != 640 || DisplayHeight(display, 0) != 480 ||
        visual_info == NULL || visual_count != 1 ||
        visual_info->class != TrueColor ||
        visual_info->red_mask != 0x00ff0000u ||
        visual_info->green_mask != 0x0000ff00u ||
        visual_info->blue_mask != 0x000000ffu)
        return 174u;
    const Window root = XDefaultRootWindow(display);
    const Window window = XCreateSimpleWindow(display, root, 80, 60, 240, 160,
                                               0, 0, 0);
    if (window == None) return 161u;
    const Cursor cursor = XCreateFontCursor(display, XC_left_ptr);
    if (cursor == None || !XDefineCursor(display, window, cursor) ||
        !XFreeCursor(display, cursor))
        return 176u;
    if (!XGrabServer(display) || !XUngrabServer(display))
        return 177u;
    if (!XGrabKey(display, 30, AnyModifier, window, False,
                  GrabModeAsync, GrabModeAsync) ||
        !XUngrabKey(display, 30, AnyModifier, window) ||
        !XGrabButton(display, 1u, AnyModifier, window, False,
                     ButtonPressMask | ButtonReleaseMask,
                     GrabModeAsync, GrabModeAsync, None, None) ||
        !XUngrabButton(display, 1u, AnyModifier, window))
        return 175u;
    const long event_mask = StructureNotifyMask | KeyPressMask |
        KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | FocusChangeMask;
    if (!XSelectInput(display, window, event_mask)) return 162u;
    Window pointer_root, pointer_child;
    int root_x, root_y, window_x, window_y;
    unsigned int pointer_mask;
    if (!XWarpPointer(display, None, root, 0, 0, 0, 0, 100, 90) ||
        !XQueryPointer(display, root, &pointer_root, &pointer_child,
                       &root_x, &root_y, &window_x, &window_y,
                       &pointer_mask) ||
        pointer_root != root || root_x != 100 || root_y != 90)
        return 179u;
    XGCValues values = {.foreground = 0x003b82f6u};
    GC gc = XCreateGC(display, window, GCForeground, &values);
    XRectangle clip = {4, 4, 220, 140};
    XRectangle rectangles[7] = {
        {8, 40, 12, 12}, {24, 40, 12, 12}, {40, 40, 12, 12},
        {56, 40, 12, 12}, {72, 40, 12, 12}, {88, 40, 12, 12},
        {104, 40, 12, 12},
    };
    if (gc == NULL ||
        !XSetClipRectangles(display, gc, 0, 0, &clip, 1, Unsorted) ||
        !XFillRectangle(display, window, gc, 8, 8, 48, 24) ||
        !XSetForeground(display, gc, 0x00ef4444u) ||
        !XFillRectangle(display, window, gc, 64, 8, 32, 24) ||
        !XFillRectangles(display, window, gc, rectangles, 7))
        return 168u;
    if (!XSetClipRectangles(display, gc, 0, 0, NULL, 0, Unsorted))
        return 178u;
    const Pixmap pixmap = XCreatePixmap(display, window, 8, 4, 32);
    XGCValues pixmap_values = {.foreground = 0x0010b981u};
    GC pixmap_gc = XCreateGC(display, pixmap, GCForeground, &pixmap_values);
    uint32_t image_pixels[8] = {
        0xffef4444u, 0xfff59e0bu, 0xffeab308u, 0xff22c55eu,
        0xff06b6d4u, 0xff3b82f6u, 0xff8b5cf6u, 0xffec4899u,
    };
    XImage *image = XCreateImage(display, NULL, 32, ZPixmap, 0,
                                 (char *)image_pixels, 8, 1, 32, 0);
    XFontStruct *font = XLoadQueryFont(display, "fixed");
    char **missing = NULL;
    int missing_count = 0;
    char *font_default = NULL;
    XFontSet font_set = XCreateFontSet(display, "fixed", &missing,
                                       &missing_count, &font_default);
    XImage *captured = NULL;
    XRectangle ink, logical;
    unsigned char property_bytes[] = "DemonX";
    XTextProperty text_property = {
        property_bytes, XA_STRING, 8, sizeof(property_bytes) - 1u
    };
    char **converted = NULL;
    int converted_count = 0;
    if (pixmap == None || pixmap_gc == NULL || image == NULL || font == NULL ||
        font_set == NULL || missing_count != 0 ||
        XTextWidth(font, "DemonX", 6) != 36 ||
        XmbTextExtents(font_set, "DemonX", 6, &ink, &logical) != 36 ||
        logical.width != 36u ||
        XmbTextPropertyToTextList(display, &text_property, &converted,
                                  &converted_count) != 0 ||
        converted_count != 1 || converted == NULL ||
        converted[0][0] != 'D' ||
        !XSetFont(display, gc, font->fid) ||
        !XPutImage(display, pixmap, pixmap_gc, image,
                   0, 0, 0, 0, 8, 1) ||
        !XCopyArea(display, pixmap, pixmap, pixmap_gc,
                   0, 0, 7, 1, 1, 1) ||
        !XSetWindowBackgroundPixmap(display, window, pixmap) ||
        !XClearWindow(display, window) ||
        !XDrawString(display, window, gc, 8, 32, "DemonX", 6) ||
        (XmbDrawString(display, window, font_set, gc, 8, 48,
                       "DemonX", 6), 0) ||
        (Xutf8DrawString(display, window, font_set, gc, 8, 64,
                         "DemonX", 6), 0) ||
        (captured = XGetImage(display, pixmap, 0, 0, 4, 2,
                              0xffffffffu, ZPixmap)) == NULL ||
        ((uint32_t *)captured->data)[0] != image_pixels[0] ||
        ((uint32_t *)captured->data)[5] != image_pixels[0] ||
        !XDestroyImage(captured) || !XDestroyImage(image) ||
        !XFreeGC(display, pixmap_gc) ||
        !XFreePixmap(display, pixmap))
        return 170u;
    XFreeStringList(converted);
    XFreeFontSet(display, font_set);
    if (!XMapWindow(display, window)) return 163u;
    XEvent event;
    if (XNextEvent(display, &event) != 0 || event.type != MapNotify ||
        event.xmap.window != window)
        return 164u;
    if (!XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime) ||
        XNextEvent(display, &event) != 0 || event.type != FocusIn ||
        event.xfocus.window != window ||
        event.xfocus.mode != NotifyNormal ||
        event.xfocus.detail != NotifyNonlinear)
        return 172u;
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, window, &attributes) ||
        attributes.x != 80 || attributes.y != 60 ||
        attributes.width != 240 || attributes.height != 160 ||
        attributes.root != root || attributes.map_state != IsViewable ||
        (attributes.your_event_mask & event_mask) != event_mask)
        return 171u;
    Window geometry_root = None;
    int x = 0, y = 0;
    unsigned int width = 0u, height = 0u, border = 0u, depth = 0u;
    if (!XGetGeometry(display, window, &geometry_root, &x, &y, &width,
                      &height, &border, &depth) ||
        geometry_root != root || x != 80 || y != 60 ||
        width != 240u || height != 160u)
        return 165u;
    if (!XFreeGC(display, gc)) return 169u;
    if (!XDestroyWindow(display, window)) return 166u;
    if (XCloseDisplay(display) != 0) return 167u;
    return 0u;
}
