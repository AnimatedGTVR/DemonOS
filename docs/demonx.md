# DemonX: native X11 compatibility service

DemonX is a clean, native C/MAKO implementation of an X11-compatible display
service for DemonOS. It is not the upstream X.Org Server and does not depend on
POSIX sockets, `fork`, device files, or a hosted C library.

The freestanding C server owns protocol parsing and resource validation. The
first MAKO client sends byte-for-byte little-endian X11 setup and core requests
inside fixed 64-byte capability-IPC records. The 16-byte `D11X` transport header
contains payload length, flags, client id, and sequence; the remaining 48 bytes
contain unmodified X11 wire data. A bounded continuation flag carries logical
messages of up to 256 bytes across multiple records in either direction.

The initial boot-tested subset implements:

- X11 11.0 little-endian setup negotiation;
- strict continuation reassembly keyed by client, sequence, and base flags,
  with a 256-byte limit and no writes outside the logical message buffer;
- per-client resource IDs with base `0x00200000` and mask `0x001fffff`;
- a synthetic root window (`0x100`);
- `CreateWindow`, `MapWindow`, `GetGeometry`, and `DestroyWindow`;
- `SelectInput` on client windows and real `MapNotify` event delivery;
- eight isolated client slots with separate reply/event channels and resource
  ID ranges;
- exclusive root-window `SubstructureRedirectMask` ownership and cross-client
  `MapRequest` routing, which is the first actual X11 window-manager contract;
- redirected `ConfigureRequest`, WM-applied `ConfigureWindow`, and
  `ConfigureNotify`;
- `UnmapWindow`/`UnmapNotify`, `ReparentWindow`/`ReparentNotify`, and bounded
  root/child `QueryTree`;
- a bounded exact-name atom registry with `InternAtom` and `GetAtomName`;
- per-window/root properties with replace/prepend/append modes through
  `ChangeProperty`, `GetProperty`, `ListProperties`, and `DeleteProperty`;
- `PropertyNotify` delivery to the client selecting `PropertyChangeMask`;
- synthetic `ClientMessage` delivery to a window's owning client, including
  the standard send-event marker used by `WM_PROTOCOLS`;
- bounded graphics-context resources with `CreateGC`, `ChangeGC`, and
  `FreeGC`, currently supporting the X11 foreground value;
- clipped `PolyFillRectangle` rasterization into retained ARGB window backing;
- eight bounded pixmap resources backed by kernel surfaces, with
  `CreatePixmap`, `FreePixmap`, drawable-aware fills, and depth-32
  `PutImage`;
- clipped, overlap-safe `CopyArea` between native window and pixmap
  drawables, using a temporary read-only source mapping and bounded writes;
- background-pixel and background-pixmap window attributes, with
  `ClearArea`/`XClearWindow` performing clipped, tiled native rendering;
- bounded depth-32 `GetImage` readback for regions of up to 56 pixels;
- transparent 5x7 core-font `PolyText8` drawing for ASCII letters and digits;
- fixed core-font metrics and GC font selection through `XLoadQueryFont`,
  `XTextWidth`, `XSetFont`, and `XFreeFont`;
- authoritative geometry, map state, class, root, and selected-mask reporting
  through `GetWindowAttributes`/`XGetWindowAttributes`;
- 16 bounded server-side window resources;
- X11-style 32-byte replies and protocol error records;
- server survival after its test client exits.

`include/X11/Xlib.h` and `lib/demonx/xlib.c` are the first freestanding client
library slice. They expose normal Xlib names over the DemonX transport:
`XOpenDisplay`, root/resource allocation, simple-window creation, input-mask
selection, mapping, `XNextEvent`, geometry, destruction, and display teardown.
It also exposes `XInternAtom`, `XGetAtomName`, `XChangeProperty`,
`XGetWindowProperty`, `XListProperties`, `XDeleteProperty`, and `XStoreName`.
Window-manager messaging is exposed through `XSendEvent` and
`XSetWMProtocols`, including correct LP64 conversion of 32-bit client-message
data onto the X11 wire. The first drawing slice exposes `XCreateGC`,
`XSetForeground`, `XFillRectangle`, `XFillRectangles`, and `XFreeGC`.
Pixmap/image coverage includes `XCreatePixmap`, `XFreePixmap`,
`XCreateImage`, `XDestroyImage`, row-chunked `XPutImage`, and `XCopyArea`.
Window background coverage includes `XChangeWindowAttributes`,
`XSetWindowBackground`, `XSetWindowBackgroundPixmap`, and `XClearWindow`.
`XGetImage` returns a managed `XImage` backed by the display's bounded
readback slot.
`XDrawString` provides the first native title/menu text path without a
host font dependency. Native compositor keyboard and button packets are
routed into the blocking DemonX display channel and translated into Xlib
`KeyPress`, `KeyRelease`, `ButtonPress`, `ButtonRelease`, and `MotionNotify`
events for the client that selected the corresponding mask.
`XSetInputFocus` now maintains server-side focus and delivers paired
`FocusOut`/`FocusIn` transitions using the normal Xlib focus event structure.
The PekWM compatibility surface also includes full-window creation,
move/resize/raise/lower helpers, border-width changes, `XMapRaised`,
`XClearArea`, batched atom interning, GC mutation, line and rectangle
outlines, direct `XImage` pixel access, and the fixed-TrueColor colormap and
named-color operations appropriate to DemonX's single native visual.
`build/demonx-xlib-test.elf` is a linkable C proof for this boundary.

As measured against PekWM's source tree and the complete DemonOS X11 header
surface, all 159 directly referenced calls are now declared and implemented:
**100% tracked API coverage**. This is an API-count milestone, not a claim
that PekWM is ready to run unchanged: some X11 semantics remain intentionally
bounded, including complete font shaping, crossing events, queued synchronous
grabs, and automatic save-set rescue when a transport disconnect is reported.
The latest slice adds subwindow mapping and ordered restacking, connected-line
drawing, direct-TrueColor release semantics, a compact core keysym/keycode
translation path, `XLookupString`, and the stable native visual identifier.
Window-manager metadata now covers reading `WM_PROTOCOLS`,
`WM_TRANSIENT_FOR`, text properties, `WM_CLASS`, and the compact native
`WM_HINTS` representation, plus class/hint setters and size-hint allocation.
The library also supports one-event pushback and Xlib-compatible error and
after-function registration used by PekWM's diagnostic path.

The final tracked lifecycle calls are functional rather than link-only stubs.
`XAddToSaveSet` and `XRemoveFromSaveSet` maintain server-side membership,
`XKillClient` resolves the owning client from a window, pixmap, GC, or cursor
and releases all of that client's native resources, and `XWarpPointer`
resolves window-relative coordinates before forwarding an absolute native
cursor move to the compositor. Save-set membership is retained today; rescue
and reparenting will activate once DemonX receives explicit client-disconnect
notifications from the transport.

Active `XGrabPointer` and `XGrabKeyboard` requests now create exclusive
server-side grab state, reject a competing client with `AlreadyGrabbed`, route
native compositor input to the grabbing client/window, honor the pointer
event mask, and release automatically when the grab window is destroyed.
Ungrab requests return routing to the selected-input owner. Sync-mode freezing
and `owner_events=True` propagation are not implemented yet; grabs currently
use asynchronous delivery to the grab window. `XAllowEvents` validates the
standard modes in preparation for the sync-mode queue.
Passive key and button grabs are stored server-side, reject conflicting
registrations, route matching native presses to the registering client, and
are removed explicitly or when their window is destroyed. Keyboard grabs
match exact modifiers or `AnyModifier`. The compositor's current native
button packet does not carry modifier state, so button grabs presently match
unmodified or `AnyModifier` registrations; extending that packet is required
before modified mouse bindings can be claimed.

PekWM's standard cursor-font shapes now have owned DemonX resource IDs,
per-window assignment, conflict validation, release, and window-reference
cleanup. The assignments are retained by DemonX; forwarding a window's shape
change into the compositor's native cursor overlay remains the final visual
cursor integration step.

`XGrabServer`/`XUngrabServer` now establish a server-side mutation barrier:
the owner may issue its atomic request sequence while competing clients
receive `BadAccess` instead of interleaving changes. This compact server does
not yet retain and replay blocked-client requests. GC clipping supports the
single rectangular clip used by PekWM's renderer and applies before native
fill submission; derived line, outline, arc, and polygon drawing therefore
inherits the same clip.

The compatibility headers now include RandR, Shape, ScreenSaver, Xinerama,
and Xdbe extension boundaries. DemonX reports these extensions as unavailable,
deliberately selecting PekWM's single-screen, rectangular-window,
software-buffered, no-idle-monitor fallback paths. Extension operations return
their standard failure or empty results instead of claiming unsupported
accelerated services. Core keyboard discovery
exposes the compositor's compact modifier map and one-symbol-per-keycode
mapping. `XQueryPointer` reports the most recently delivered native pointer
position and translates it into top-level window coordinates.
The default screen now exposes a queryable 640x480, 32-bit TrueColor visual
with explicit RGB masks, so PekWM and native clients receive consistent
screen/depth/visual metadata rather than relying on opaque placeholders.

ICCCM coverage now includes cross-client selection ownership and a compact
24-byte `WM_NORMAL_HINTS` representation carrying the flags, minimum/maximum
sizes, resize increments, base size, and gravity used by PekWM. Selection
ownership is released when its window is destroyed. Top-level coordinate
translation, core-font-set lifecycle, full-ellipse arc drawing, convex polygon
filling, and nonblocking typed/masked/window event checks are also available.
The font-set path now includes byte-oriented multibyte/UTF-8 drawing, text
extent measurement, and bounded text-property conversion. ASCII UTF-8 uses
the native core font directly; codepoints outside that compact font remain
replacement glyphs rather than pretending full Unicode shaping exists.
The blocking event helpers preserve a nonmatching head event and return `-1`;
the planned multi-event client queue will replace that temporary bounded
behavior without discarding events.

The scripted boot client establishes two independent connections: client 1
claims the root redirect as the window manager, client 2 requests a map,
client 1 receives `MapRequest`, and client 2 receives `MapNotify` only after
the WM maps it. It then verifies redirected geometry changes, authoritative
tree enumeration, root-delivered unmap notification, an atom/property
create-read-list-delete lifecycle, both property-notify states, and a
cross-client synthetic client message. It also creates a foreground GC, fills
individual rectangles, sends a seven-rectangle 68-byte request over two
transport records, synchronizes with the server to catch deferred protocol
errors, and releases the GC. The same boot client creates an 8x4 native
pixmap, uploads real ARGB pixels, and frees both its GC and pixmap.
It also copies the uploaded row within that pixmap before releasing it, so
the native surface read/write path is exercised during every smoke boot.
Before release, that pixmap is installed as a window background and tiled by
a real clear request.
The copied row is then read back through `GetImage`.
The boot path also renders a core-font glyph into a native pixmap.

Drawing targets the kernel's native surface service and publishes that
surface to the desktop compositor. If that service is unavailable during
early boot, DemonX uses a bounded 56x56 retained ARGB backing store so drawing
still performs real clipped pixel writes instead of returning fake success.
The fallback is intentionally not presented as a visible compositor window.

The compositor now closes granted surface handles as well as unmapping them,
so destroyed X11 windows no longer retain kernel backing storage.

The remaining major PekWM input work is active/passive grabs and pointer
enter/leave crossing events. Keyboard, button, and pointer-motion input use
the compositor's native event source without polling or a second input stack;
explicit focus changes use the same bounded DemonX event delivery machinery.
The native compositor remains the sole owner of framebuffer presentation.
