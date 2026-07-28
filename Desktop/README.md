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
