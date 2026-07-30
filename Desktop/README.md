# Demon Desktop

`Desktop/` owns the graphical shell that is presented by the DemonOS
compositor.  It is deliberately separate from `user/compositor.mko`:

- the compositor owns display capabilities, surfaces, z-order, damage,
  composition layers, focus, centralized hit-testing, input routing, moving,
  resizing, and presentation;
- the desktop owns wallpaper treatment, panels, launch surfaces, desktop
  shortcuts, status presentation, and the visual identity of the session;
- applications continue to own their client surfaces.

The remade shell is intentionally a Windows Vista/macOS hybrid rather than a
clone of either desktop. Vista contributes depth, icy glass rims, glossy
highlights, a strong application switcher and an orb-like launcher. macOS
contributes the floating menu strip and dock, calmer spacing, rounded
surfaces and restrained use of color. DemonOS keeps the controls on the right
for familiarity, but gives them the red/yellow/green traffic-light semantics
as part of its own visual identity.

The design is still a native lightweight desktop. Every glass effect is a
bounded gradient, border, shadow or translucent fill; it does not allocate a
second screen-sized blur surface and remains usable without graphics
acceleration.

The current layout provides:

- a floating glass system/quick-launch menu strip;
- a floating glass task dock generated from the live compositor window table;
- separate DemonOS and Home desktop shortcuts;
- a four-action applications menu;
- hover feedback driven by the real compositor cursor;
- glossy focused titlebars and quieter unfocused titlebars;
- colored minimize, maximize and close controls with unchanged hit targets;
- application-specific terminal, generic-app, and Web icon treatments;
- a compact native atlas sourced from `assets/IconPack/Fluent-icon-theme`
  for cursors, window controls, launchers, applications, settings, and
  desktop locations;
- retained window controls, keyboard shortcuts, moving, resizing, snapping,
  minimization, and surface-backed client contents.

Client windows now carry an explicit composition layer alongside their
application kind. Normal application windows use the normal layer today;
menu and overlay layers are reserved for native shell surfaces. Rendering,
focus selection, and pointer targeting all compare the same layer-aware stack
key, so the visible top window and the window receiving input cannot disagree.

## EDDE (Equinox Desktop DemonOS Environment)

`desktop_edde.mko` (paired with `user/compositor_edde.mko`) is a second,
independently-evolvable desktop shell, selected by its own GRUB menu entry
("EDDE (PekWM-derived Workspaces)" -- see `grub/grub.cfg`). It boots the same
kernel and app catalog as the DemonOS entry above; only the ELF mapped to
`/system/bin/compositor.elf` differs, since the kernel spawns whatever binary
sits at that fixed RAMFS path.

Where the DemonOS shell above is a Vista/macOS hybrid, EDDE is visually
matched against a real screenshot of the Equinox Desktop Environment (EDE) --
a slim single panel (start menu, taskbar, clock, battery, activity applets),
not the floating glass strip/dock pair. Several of its features are genuine
ports of real EDE/PekWM algorithms rather than lookalikes built from scratch:

- the taskbar's right-click context menu ports `TaskButton::display_menu()`
  from real EDE's `ede-panel` (`Desktop/EDE`, vendored reference source);
- the panel applet layout is computed the same way `ede-panel`'s
  `Panel.cpp::do_layout()` computes it (`INITIAL_SPACING`/`DEFAULT_SPACING`
  walks from each edge), not hand-picked pixel constants;
- multi-workspace switching (Super+1..4) and Alt+Tab's MRU focus order are
  ported from PekWM's real `Workspaces` class (`Workspaces::setWorkspace`,
  `Workspaces::_mru`/`addToMRUFront` -- see `Desktop/pekwm`, also vendored
  as reference source only, nothing from it compiles or runs directly).

`Desktop/EDE` and `Desktop/pekwm` are real upstream source trees kept in this
repo purely as reading material for these ports -- neither builds against
this kernel (EDE needs FLTK/X11, PekWM needs Xlib/libstdc++/sockets, none of
which exist here). Every actual line of behavior they inspired was
hand-translated into `desktop_edde.mko`/`user/compositor_edde.mko`'s own MKO
code; nothing from either tree is linked into the ISO.
