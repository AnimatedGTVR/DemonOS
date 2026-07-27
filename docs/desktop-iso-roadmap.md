# MAKO Desktop ISO Roadmap

The desktop ISO is the distribution form of MAKO: a small bootable environment
where MKO projects run against MAKO-ABI instead of Windows or Linux APIs. The
desktop is not a theme layered over another operating system. Every milestone
below must boot on the MAKO kernel and expose real, tested kernel facilities.

## Product goals

- One ISO built from Windows or Linux development hosts.
- MKO SDK, examples, documentation, and eventually the compiler available at
  first boot.
- Projects packaged once and launched without host-specific API branches.
- A responsive desktop on modest x86_64 machines and virtual machines.
- A small trusted kernel; graphics policy, shell UI, and applications live in
  isolated userspace processes.
- Serial/headless tests remain available even after graphics becomes primary.

## Planned system shape

```text
GRUB/UEFI ISO
├── MAKO kernel
├── initial project filesystem
│   ├── /system/mako/MAKO-source.tar.zst
│   ├── /system/mako/manifest.txt
│   ├── /system/mko/sdk.mko
│   ├── /system/mko/compiler
│   ├── /apps/desktop
│   └── /projects/hello
└── userspace
    ├── init + service manager
    ├── display/input servers
    ├── desktop shell + launcher
    └── isolated MKO applications
```

The current ISO already implements the first slice: the actual
`AnimatedGTVR/MAKO` source snapshot is stored on the ISO, while its manifest,
`sdk.mko`, `hello.mko`, and `hello.elf` are carried as Multiboot modules,
protected from the physical-frame allocator, and installed into the RAM project
store during boot.

## Phase 0 — bootable developer ISO (current)

Delivered:

- Multiboot2 x86_64 kernel, VGA/serial console, interrupts, timer, and keyboard.
- Isolated ring-3 processes, scheduler, ELF64 loader, capabilities, and RAM files.
- Native MKO application ABI and shared `MakoAbi` SDK.
- Verified `make iso`, boot smoke test, and interactive keyboard smoke test.
- Preinstalled MAKO repository source, SDK, starter source, and starter ELF
  visible through `mko` and `projects`.
- Allocation-free ELF catalog exposed through `apps`, with validated `hello` metadata.
- Native RAM-worktree Git foundation and explicit interoperability milestones.
- `desktop status` reports the live PID-bound graphical target and the remaining
  shared-surface/window-management work.

Exit test: the ISO boots, reports `MKO_SYSTEM_PREINSTALLED`, accepts typed
commands, and completes all kernel self-tests without `[FAILED]`.

## Phase 1 — real initial filesystem and dynamic applications

- Replace fixed Multiboot-file seeding with a read-only initramfs format.
- Add VFS paths, directories, file metadata, seekable handles, and executable
  lookup.
- Extend the ELF loader to separate code/data/BSS segments and dynamic mappings.
- Add user heaps, address-space mapping syscalls, argument vectors, and process
  spawning.
- Launch `/system/init` from the filesystem rather than embedding the init ELF
  in the kernel image.
- Make `mko run /projects/hello/main.mko` invoke a real userspace toolchain path.

Exit test: two unrelated ELF applications are loaded by pathname from initramfs,
receive arguments, allocate memory, exit independently, and leave no handles or
pages behind.

## Phase 2 — self-hosted MKO toolchain

- Separate the compiler core from the current host UI/runtime dependencies.
- Port lexer, parser, type checker, MIR optimizer, and native backend to the
  freestanding MKO subset or a small portable compiler runtime.
- Install `mko check`, `mko build`, and `mko run` under `/system/mko`.
- Add a project manifest describing entry module, ABI version, permissions, and
  requested services.
- Cache build artifacts in a writable project volume.

Exit test: edit a starter source file inside MAKO, compile it inside MAKO, launch
the resulting process, reboot, and rebuild it with identical output.

## Phase 3 — graphics and modern input foundation

Framebuffer portion delivered:

- Optional Multiboot2 640×480×32 mode request and validated RGB tag parsing.
- Safe high-physical-address mapping with VGA/serial fallback.
- Resolution-sized ARGB32 software backbuffer, clipping, primitives, bitmap
  text/blitting, damage tracking, and pitch-aware presentation.
- QEMU-tested native graphical boot diagnostic.

Also delivered:

- Display and input capability isolation for a ring-3 MKO compositor.
- Bounded copied damage tiles, resolution discovery, and atomic presentation.
- PS/2 mouse input and timestamped unified keyboard/pointer events.
- Software cursor rendering on the kernel recovery/boot surface.

Remaining:

- Add UEFI GOP as the native boot path.
- Add page-flip/vsync support and display-resize events.
- Add USB HID keyboard/mouse and absolute tablet input.
- Retain VGA text and serial as recovery consoles.

Exit test: a ring-3 graphics test draws, flips frames, tracks a mouse cursor,
receives keyboard focus, and cannot write outside its owned surface.

## Phase 4 — compositor and window system

Delivered foundation:

- Userspace display server owning exclusive display/input capabilities.
- Three isolated clients using a stable 64-byte window protocol over named IPC.
- Copied tile surfaces, clipping, creation z-order, pointer hit-testing,
  click-to-focus, bounded damage, and one atomic present per scene.
- Crash containment: a queued request survives its sender exiting with status
  77, while the compositor and other clients complete normally.
- Persistent blocking compositor loop with retained geometry. A later fourth
  process sends a validated `MOVE`, triggers a second atomic frame, exits, and
  leaves the compositor blocked and ready for another request.
- Init-managed `desktop-compositor.service` and `desktop.target`. Activation is
  conditional on a real framebuffer and a scheduler-verified blocked compositor
  PID; console fallback still boots without inventing a graphical service.
- A final post-diagnostic repaint restores the userspace scene and leaves the
  compositor alive after `KERNEL_BOOT_OK`.
- Bounded kernel-backed client surfaces with write ownership and read-only
  capability transfer to the compositor through an authenticated IPC channel.
  The creating process exits before the compositor's retained mapping drives
  every subsequent atomic frame.
- Atomic blocking wait across compositor IPC and unified input, with losing
  registrations cancelled on delivery or process teardown.
- Pointer title-bar capture, bounded drag motion, release, and a verified third
  atomic frame from real input records.
- Bottom-right resize capture with minimum/screen bounds and a verified fourth
  frame at 376×296.
- Adjacent pointer-motion coalescing that preserves button/key ordering and
  avoids redundant burst repaints.
- Focused KEY packet delivery to a persistent isolated, client-owned IPC
  endpoint, including scan code and modifier validation.
- Reference-counted surface handles with zero-on-reclaim fixed-slot reuse.
- IRQ-driven userspace event pumping from the interactive shell, with no idle
  compositor polling.
- Timer-backed dirty-scene deadlines with a measured two-tick present floor.
- Four fixed 64×64 client surfaces, native title/resize decorations, and a
  pointer-driven bottom-panel launcher toggle.
- Read-only surface-page mapping at a fixed compositor address, independently
  retained mapping lifetime, and client-owned damage submission/consumption.
- Compositor-owned arrow cursor with immediate bounded scanout-overlay updates,
  clean-backbuffer recomposition, and compact launcher glyph rendering.
- Eight-slot native window state model with retained normal geometry,
  maximize/restore into the panel-safe work area, and non-destructive minimize.
- Live-table taskbar buttons with focus, raise, minimize, and restore policy;
  focused/inactive/minimized windows have distinct compositor-owned styling.

Desktop-demo kernel/platform status: complete. Full font shaping, cursor theme
packs, acceleration, and the application suite continue in later phases.

Exit test: three isolated clients animate concurrently, overlap correctly,
receive focus selectively, and recover cleanly when one is killed.

## Phase 5 — desktop environment preview

In progress:

- Native workspace background, persistent panel, launcher, focused-window
  styling, working titlebar controls, and live task switching through taskbar
  buttons.
- Native wallpaper-backed loading transition with display/input/shell phase
  indicators, followed by a real editable local login gate rather than a
  timed mock screen. The normal ISO requires the `demon`/`demon` preview
  credential; the test ISO alone has a deterministic unattended bypass.
- Failed authentication remains locked with visible error treatment. Super+L
  clears the credential buffer and returns to the login card while preserving
  the live desktop session. A QEMU visual regression test covers reject,
  unlock, and relock.
- Floating glass top and bottom bars, inset shadows, focused accent borders,
  neutral compact window controls, an asset-backed desktop identity tile, and
  consistent hover states create depth without a full-screen blur buffer.
- Midnight visual pass based on the DemonOS target: top system bar, bottom
  dock, dark window chrome, blue application surfaces, DEMON launcher branding,
  two live-window launcher shortcuts, and a show-desktop action.
- Refined midnight chrome with a floating inset dock, softer navy window
  shadows, brighter focus treatment, a cleaner launcher card, and a unified
  deep-blue workspace palette without adding compositor framebuffer memory.
- Launcher rows expose separately colored terminal, application, and
  show-desktop actions backed by their existing live window behavior.
- The Terminal launcher resolves persistent window ID 7 instead of assuming a
  table position; the application row deliberately targets the first retained
  application slot, so the visible identities now match their actual actions.
- The visual identity is intentionally a classic hybrid: Windows 7-inspired
  blue focused chrome and full-width taskbar, IceWM/JWM-style compact square
  controls, and the practical dual-panel structure of LXQt/XFCE. This is the
  native DemonOS style direction rather than a clone of any one desktop.
- Palette refinement replaces the overly saturated royal-blue draft with a
  muted blue workspace, steel inactive chrome, charcoal panels, and focused
  blue accents that remain readable at the native 640x480 demo resolution.
- Window chrome, launcher surfaces, the taskbar, and task tabs now use the
  framebuffer's real ARGB blending path. Layered translucency produces a
  lightweight glass treatment while avoiding a full-screen spatial blur pass
  and its RAM/bandwidth cost in the software compositor.
- Glass treatment now extends through the complete window frame and each
  taskbar tab. Tabs carry state-sensitive top-edge sheen (bright focused,
  muted inactive, dim minimized), matching the title-bar highlight language.
- The WEB launcher now starts a dedicated native MKO document browser instead
  of rebranding the counter demo. Demon Web owns a 256x64 shared surface,
  accepts address-bar keyboard input and content clicks, keeps bounded
  back/forward history, and navigates real `demon://home`, `demon://about`,
  `demon://network`, and `demon://files` documents. Capability-gated
  `file:///` locations read a bounded preview from the real RAMFS, including
  the ISO-seeded `/README.md`. HTTP
  locations remain explicitly unavailable until the NIC/TCP/DNS/HTTP layers
  are implemented; the UI does not fabricate successful internet access.
- Cursor scanout restore/blend now runs under the kernel CR3 and is isolated
  from ordinary scene damage. This fixes the intermittent colored cursor trail
  and prevents the low physical backbuffer address from aliasing a
  compositor-code mapping while syscall 32 runs.
- ARGB glass now covers the remaining interactive chrome: compact window
  controls, Start button, launcher rows, top panel, status cluster, and the
  desktop shortcut tile. Opaque glyphs and control marks retain contrast.
- Window title bars now carry native transparent bitmap labels: `TERM` for
  persistent terminal ID 7 and `APP` for ordinary clients. Glyph background
  pixels use alpha zero, so labels sit directly on the glass chrome.
- Taskbar identity markers now distinguish the persistent terminal in green
  from ordinary blue application tasks; button fill and sheen communicate
  minimization independently.
- Compact DemonOS top-bar wordmark and native Alt+Tab focus cycling across the
  live visible window table.
- Native DemonOS desktop shortcut tile with uncovered-window hit testing; a
  click opens the native launcher without creating a click-through hot zone
  beneath overlapping application windows.
- Compact 16-byte-aligned userspace ELF images, reclaiming roughly 36 KiB of
  fixed RAMFS capacity for desktop applications without increasing RAM usage.
- Graphical boots now remain in `desktop.target`; the compositor retains display
  and unified-input ownership while MakoBox is reserved for recovery boots.
- Persistent native terminal process installed in the ISO and launched by
  `desktop.target`, with its own shared surface and focused IPC endpoint.
- Live printable-key, Backspace, and Enter handling: decoded characters cross
  the compositor boundary, update the terminal's client-owned pixels, submit
  bounded damage, and appear on the next paced desktop frame.
- Enter-driven native terminal built-ins: `help` lists available commands,
  `fetch` renders the OS/kernel/architecture identity, `clear` clears output,
  and unknown input is reported without falling back into the recovery console.
- Executed input is retained above its output and a fresh editable prompt is
  rendered below, giving the desktop client a real command/result flow instead
  of replacing the active line after every Enter press.
- Line editing now uses the terminal's full 1536-byte line-storage region with
  multi-row wrapping and a rolling screen tail, removing the former short
  command cutoff and horizontal-only viewport while staying inside the fixed
  lightweight userspace heap.
- Terminal now uses a full-width 256x64 logical console presented by the
  compositor as a 320x80 canvas beneath the title bar. Its compact 5:4 scale
  provides roughly 42 columns instead of the former narrow 20-column island;
  the remainder of the standard 320x240 client is continuous terminal space.
- Shared surfaces use page-aligned variable arena allocations. A 64 KiB
  terminal surface can coexist with three ordinary 16 KiB window surfaces
  inside the original fixed 128 KiB arena, avoiding any increase in the
  kernel's low-memory footprint.
- Normal windows are constrained to the desktop work area while dragging and
  resizing: title bars remain below the 28-pixel top panel and client bottoms
  remain above the 48-pixel taskbar. Maximized windows retain their existing
  inset geometry.
- Alt+Tab cycles through all visible native windows, raises the selected
  client, and closes the launcher so keyboard focus has one clear owner.
- The keyboard decoder preserves extended PS/2 scan codes and reports Super as
  a first-class modifier. Alt+F4 closes the focused client; bare Super opens
  the launcher; Super+D reversibly shows the desktop; and Super+Arrow provides
  half-screen snap, maximize/restore, and minimize behavior with exact normal
  geometry recovery.
- Desktop shortcut behavior is covered twice: directly by the kernel boot
  contract and end-to-end by a QEMU visual test that injects a physical
  left-Super event and verifies a real framebuffer change.
- The native executable window now supports a 144 KiB compositor without
  assigning that maximum to every process. Tiered code-frame pools keep
  bootstrap tasks at 16 KiB and ordinary application slots at 48 KiB; only
  the persistent compositor slot receives the full 36-page window. The current
  executable-frame pool is 92 pages, and direct read-only RAMFS loading avoids
  the former 64 KiB spawn staging buffer.

Remaining:

- Clock service, notifications, and multi-workspace controls.
- Replace the fixed preview credential with a disk-backed account database,
  password hashing, per-session identity capabilities, roles, and an audit
  journal once persistent storage exists.
- Connect the terminal's proven built-in input/render path to MakoBox command dispatch,
  persistent scrollback, richer line editing, and a userspace console service.
  The current native client accepts a 1,536-character wrapped line and provides
  built-in `help`, `fetch`, and `clear` commands.
- Project browser/editor with build, run, stop, and diagnostics actions.
- Settings for display, keyboard, pointer, theme, and accessibility scaling.
- First-run welcome application explaining the SDK and starter project.

Exit test: booting the desktop ISO reaches the graphical shell automatically;
the user opens the starter project, builds it with preinstalled MKO, launches a
window, and views compiler diagnostics without a host OS.

## Phase 6 — persistence, packages, and installation

- Virtio block driver first, followed by AHCI/NVMe as hardware coverage grows.
- Writable filesystem with atomic replacement and crash-consistent metadata.
- Project import/export, package cache, user configuration, and build cache.
- Network service and signed package/repository metadata.
- Installer that partitions a target disk, copies the system, and installs a
  UEFI boot entry while preserving an ISO live mode.

Exit test: a project created in the live desktop can be saved to disk, reopened
after reboot, updated from a signed package source, and exported intact.

## Phase 7 — hardware and release readiness

- ACPI shutdown/reboot, SMP, APIC timers/interrupts, power management, and RTC.
- Virtio GPU/network/storage for excellent VM support.
- Selected real GPU modesetting, USB controllers, audio, and network devices.
- Recovery boot entry, safe graphics mode, crash logs, and ABI compatibility
  tests across releases.
- Reproducible release ISO with checksums and an automated VM test matrix.

## Budgets and rules

- Kernel features require a serial marker and an automated failure test.
- Interactive features require QEMU input/display automation where possible.
- The kernel remains freestanding and avoids embedding desktop policy.
- No status line may claim a service is ready until its real initialization and
  behavioral test pass.
- Initial desktop target: under 64 MiB idle RAM excluding file cache, under two
  seconds from kernel entry to shell in the reference VM, and usable at 60 Hz
  with software composition at 1280×720.

## Immediate next implementation order

1. Replace Multiboot file seeding with initramfs/VFS-backed executable lookup.
2. Port the MKO compiler core and launch the first native project editor.
3. Add full font shaping and richer launcher applications.
4. Begin virtio block/network/GPU support for persistent accelerated previews.
