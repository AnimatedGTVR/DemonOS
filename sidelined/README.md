# Sidelined

Everything under this directory is the previous graphical desktop stack --
sidelined, not deleted, on request, in favor of a plain TTY-only console
kernel (see `src/kernel.c`'s `main()`: every boot now goes straight to
`makobox_shell()`, no compositor, no framebuffer graphics mode).

Nothing here is part of the active build. It's kept in the tree (and in
full git history either way) in case the graphical direction gets picked
back up later, rather than lost.

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
protocol, and stayed in the active build.
