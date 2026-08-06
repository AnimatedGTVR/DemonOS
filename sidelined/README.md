# Sidelined

Everything under this directory is an *earlier* graphical desktop stack --
sidelined, not deleted, on request, in favor of a plain TTY-only console
kernel at the time. That decision has since been reversed: the graphical
direction was picked back up with a from-scratch rewrite (a Rust
compositor at `rust/compositor/`, plus a new C DemonX server at
`user/demonx_server.c`, window manager at `Desktop/demonwm/demonwm.cc`,
and `apps/xterm/xterm.c`), and `src/kernel.c`'s `main()` now does spawn a
real compositor and reach a graphical desktop session before falling
through to `makobox_shell()` as the recovery console alongside it (see
`DESKTOP_SESSION_READY` at boot).

Nothing under *this* directory is part of the active build -- it's the
older, now-superseded implementation (Xlib-shim based, MKO-scripted
compositor), kept in the tree (and in full git history either way) rather
than lost, in case anything here is useful reference for the current one.

## What's here

- `demonwm/` -- the native C++ window manager (panel, launcher, window
  decorations, resize)
- `demonx-lib/`, `X11-headers/` -- the Xlib compatibility shim and headers
  DemonWM and the windowed apps below were built against
- `dlib/`, `dlib_hello/` -- an alternate, simpler windowing shim and its
  demo app
- `calculator/`, `filemanager/`, `settings/` -- windowed apps (Xlib/surface
  based)
- `desktop.mko` -- the native MKO desktop shell script
- `user/` -- the ring-3 compositor (`compositor.mko`/`.S`), the DemonX X11
  server and its Xlib test client, and every windowed `.mko` client
  (window/window-move/window-event/window-crash/browser/counter/terminal)

## What's NOT here (still active)

`apps/cxx_hello`, `apps/tetris`, and `user/init.mko`/`sdk.mko` are plain
console/input-driven, don't depend on the compositor or any windowing
protocol, and stayed in the active build regardless of the desktop stack's
own state.

The *current* graphical desktop stack -- distinct from everything sidelined
above, not a revival of it -- lives at `rust/compositor/` (compositor),
`user/demonx_server.c` (X11 server), `Desktop/demonwm/demonwm.cc` (window
manager), and `apps/xterm/xterm.c` (terminal client). `user/compositor.mko`/
`.S` at the top-level `user/` directory are themselves now dead leftovers
from before the Rust rewrite -- not part of this sidelined/ stack, but not
built by anything active either.
