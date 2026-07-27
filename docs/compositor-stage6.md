# Stage 6: native userspace compositor boundary

The first DemonOS display server is compiled from `user/compositor.mko` and
runs at ring 3 as an isolated ELF64 process. It receives the display and input
capabilities explicitly; normal applications cannot open either service.

The compositor owns scene policy and software rasterization. It draws the
wallpaper, panel, and overlapping window surfaces into a private 64x32 tile
(8 KiB), then submits that damage through MAKO-ABI syscalls 18-21. The kernel
validates the capability, user address range, dimensions, overflow, and a
4,096-pixel transfer ceiling before copying to the framebuffer. Tiles
accumulate in the backbuffer. A final client surface tile carries the
atomic-present bit, so a 640x480 scene performs one hardware present rather
than many small ones. This keeps memory bounded and avoids exposing physical
framebuffer memory.

`include/demon/window.h` defines protocol version 1. Every control packet is
exactly 64 bytes and fits the bounded IPC transport. Creation, damage,
presentation, focus, pointer, keyboard, close, and move operations all have
stable opcodes.

## Real window table, not a scripted scenario

The compositor keeps a genuine window table: a fixed-capacity, 8-slot region
addressed by vptr offset arithmetic (MKO's `--kernel` compilation profile has
no structs or arrays, so the table is packed the same way x/y/width/height
were already packed into single u64 words -- a slot-stride memory region, not
a boot-time scripted sequence of three named clients). Each slot tracks
normal/minimized/maximized state, window id, z-order, current and restore
geometry, and (if the window shares a real client surface) the surface object
id and its mapped address.

`CREATE`, `CLOSE`, `FOCUS`, `MOVE`, `POINTER`, and `KEY` are all dispatched
generically against this table from the compositor's single steady-state
event loop (`compositor_main`), driven by `MakoAbi.compositor_wait`. There is
no boot-time client quota and no per-window hardcoded logic: any client can
send `CREATE` at any point after the compositor starts, bounded only by table
capacity (a full table is rejected gracefully, not treated as fatal). Clicking
any window's title bar or content refocuses it and raises its z-order at
runtime; clicking its close button removes it from the table, unmaps its
surface if it had one, and reassigns focus to the next-highest window (or
none, if the table is now empty) -- all while the compositor keeps running
and compositing whatever windows remain.

`window_client.mko` is a generic "create a window with some content" client:
any process running it creates exactly one window at a geometry derived from
its own pid (so several instances spawned back-to-back land at different, all
in-bounds positions) and shares a real drawn surface with the compositor.
`window_move_client.mko` sends a generic `MOVE` for whatever window id it is
told to target. `window_event_client.mko` is a generic focused-event
receiver, listening on a channel name the compositor derives deterministically
from the window id it targets (`desktop.winN`), rather than a single
hardcoded channel baked in at boot. `window_crash_client.mko` is a small,
clearly-named crash-containment test client: it creates a window and then
exits with a deliberately abnormal status, to prove the compositor survives a
client dying immediately after handing off a `CREATE`.

Up to `USER_SURFACE_MAP_SLOTS` (4) real client surfaces can be mapped
read-only into the compositor process at once, matching the kernel's
system-wide `SURFACE_LIMIT` (4). Every window that shares a surface within
that budget gets a genuine zero-copy mapping; a window created beyond that
budget (or with no surface at all, like the crash-containment client) is
composited with flat placeholder content instead of forged pixels -- the
compositor never claims a window has real client pixels when it does not.

A new syscall (31, `compositor_report`) lets the compositor self-report its
live window count and currently focused window id into kernel-side counters
after every table change, gated on the same DISPLAY capability write right
`display_submit` already requires. This is compositor introspection, not
kernel window-manager policy: the kernel stores exactly what it is told, the
same pattern `surface_created()`/`surface_reclaimed()` already used for
compositor-adjacent state. The boot test asserts on this real, dynamic state
(windows actually created, actually focused, actually closed) instead of
scripted pixel/frame counts tied to a fixed three-client scenario.

The persistent loop uses MAKO-ABI syscall 26, an atomic wait over the
compositor IPC channel, unified input, and an optional relative timer
deadline. The kernel registers the waiters, blocks the task, cancels the
losing registrations on delivery, and returns source 1 for IPC, source 2 for
input, or source 3 for the deadline. The loop uses a bounded five-tick (20 Hz)
heartbeat, so the visible desktop continues refreshing even if an emulator
misses or coalesces an input wakeup; it sleeps between deadlines instead of
busy-polling. Pointer press in a
window's title bar captures a drag; motion updates bounded geometry and
release ends capture, refocusing and raising whichever window was dragged.
Bottom-right press captures resize instead of drag, clamped to a 160x120
minimum and the remaining display bounds.

Keyboard focus crosses a real process boundary and follows live focus, not a
boot-time snapshot. For key down/up, the compositor connects (by the focused
window's derived channel name) with send-only authority and forwards a
version-1 `KEY` packet containing type, scan code, modifiers, and the decoded
character value. The persistent terminal consumes that packet, redraws its
own full-width 256x64 surface, and submits damage without kernel-owned
terminal pixels.

The compositor has native title-bar and resize-grip decorations. Each window
has working minimize, maximize/restore, and close controls. Maximize saves the
exact normal geometry and fills the 624x388 work area between the top system
bar and bottom dock;
minimize removes only that window from composition while retaining its table
entry, surface mapping, and process-visible identity. A second taskbar click
restores the same normal or maximized mode and raises the window.

Desktop shortcuts are compositor policy and are consumed before ordinary key
forwarding. Alt+Tab cycles across visible live-table slots and Alt+F4 closes
the focused window through the same surface-reclaiming path as its title-bar
button. A bare left or right Super key toggles the launcher; using Super as a
modifier suppresses that bare-key action. Super+D reversibly minimizes or
restores every window, while Super+Left/Right snaps the focused window to an
exact half-work-area layout. Super+Up maximizes/restores and Super+Down first
restores a maximized window, then minimizes it. Restoring a snapped window
recovers its exact saved geometry. All other keyboard events remain routed to
the focused client. The desktop DemonOS/Home tile opens the same native
launcher when it is not covered by a window.

## Native startup, login, and lock

Normal graphical boots no longer drop directly into an application or recovery
terminal. Once `desktop.target` gives the ring-3 compositor the display and
input capabilities, it renders a short native session-preparation transition
over the real wallpaper. Its progress and three state lamps represent the
display, input, and shell phases that have actually been opened; the sequence
does not invent kernel hardware messages.

The transition hands off to a compositor-owned local session gate. The fixed
preview account is `demon` with password `demon`. Printable keys edit an
eight-character bounded password buffer, Backspace removes a character, and
Enter or the pointer-driven Enter button validates it. The renderer receives
only the password length and error bit, so secret bytes are never copied into
the drawing path. A failed attempt clears the entered value and leaves a
visible red error state. Super+L clears the credential buffer and returns to
the gate while preserving the live window session behind it.

This is a real desktop input gate, but it is deliberately not described as a
persistent multi-user security boundary: the preview still has no writable
account database, password hashing service, session capability issuer, or
disk-backed policy. The test ISO uses a short deterministic bypass so kernel
contract tests remain unattended; the normal run ISO never auto-unlocks.
`make session-login-smoke` boots that normal ISO, proves rejection, successful
unlock, and Super+L relock from framebuffer screenshots, and rejects any
page-fault/panic marker.

The bottom dock is built from the live window table rather than fixed sample
rectangles. Every occupied slot has a stable task button with distinct
focused, inactive, and minimized visuals. Clicking a focused task minimizes
it and transfers focus to the highest visible window; clicking an inactive
task raises it; clicking a minimized task restores it. A pointer
press on the bottom-left panel button toggles compositor-owned launcher
state, opens its menu, and marks the scene dirty. The same path is reachable
from the physical Super key. This is an interactive
shell path, not a kernel-painted overlay. Five compact 5x7 compositor glyphs
label the menu `DEMON` through bounded damage submissions. Its first two rows
restore/focus live windows, while the third uses the same reversible
show-desktop action as Super+D. The status cluster is composed over a persistent
top system bar.

The arrow cursor remains compositor policy but uses a dedicated bounded
scanout-overlay syscall. Pointer motion updates only the old/new 16x24 cursor
damage instead of repainting the entire desktop. The overlay is blended from
the clean scene backbuffer after every present, so cursor pixels never pollute
application content, leave stale saved pixels, or flash during window frames.

The compositor is not terminated at any point during the boot contract test;
it remains alive as the desktop service through window creation, focus
changes, moves, drags, keyboard routing, and a close, and stays running
afterward beside the interactive console. MakoBox checks for IRQ-woken
userspace work on each shell iteration and resumes it until it blocks again,
allowing live mouse events without a kernel polling thread.

A window's client owns a real surface rather than only a compositor-selected
fill color. Its client creates a bounded kernel surface, writes a checker
pattern, and transfers a read-only capability specifically to the owner of
its connected IPC channel. The `CREATE` packet carries the destination-owned
handle. The client submits a bounded 64x64 damage rectangle and closes its
local handles. The compositor maps the surface's physical pages read-only
into its own address space and submits that address directly, once per
window that has one -- there is no per-frame surface pixel copy. Handles and
mappings retain the underlying object independently; closing the final
reference clears its slot and returns it to the fixed pool; closing a window
that owns a mapped surface releases that mapping.

Blocking delivery uncovered a low-memory aliasing bug in kernel-to-user
copies. Process page tables remap part of the identity-mapped 3-4 MiB region,
so treating a physical frame number as a kernel pointer could write through
the sender's mapping. `userspace_copy_to` stages bounded chunks on the kernel
stack, activates the destination CR3, writes through the validated user
virtual range, and restores the caller CR3. IPC, input, and display metadata
therefore reach the intended isolated address space.

This is now the window-manager foundation plus a native desktop-shell preview,
not the finished desktop. Notifications, a clock service, workspaces,
persistent accounts, and full applications remain later work. The boot contract directly exercises
taskbar minimize/restore, titlebar maximize/restore, Alt+F4, reversible
Super+D, half-screen snap/restore, and bare-Super launcher toggle while keeping
two live windows and the compositor's executable-code hash intact. A separate
QEMU visual smoke test injects the real extended PS/2 left-Super scan code and
requires the resulting framebuffer to change.
