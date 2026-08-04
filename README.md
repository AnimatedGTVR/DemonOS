# MAKO Kernel Project

An x86_64 freestanding kernel and portable project runtime. Applications
target MAKO-ABI instead of Windows or Linux APIs, and kernel/user components
can be compiled from native MKO as the MAKO compiler matures.

**Issues!**: If you find any issues I would love it if you could please report it, if you want to fix them look at - [Contributing](#Contributing)

## Table of contents

- [Native display stack](#native-display-stack)
- [DemonX: X11 compatibility](#demonx-x11-compatibility)
- [Demon Web](#demon-web)
- [Bootable ISO distribution](#bootable-iso-distribution)
- [Current milestone](#current-milestone)
- [MakoBox](#makobox)
- [Scheduler, syscalls, and user mode](#scheduler-syscalls-and-user-mode)
- [Build and run](#build-and-run)
- [Boot status and self-tests](#boot-status-and-self-tests)
- [The native bridge](#the-native-bridge)
- [Roadmaps: better MKO userspace, C++ in the kernel, and Wine](#roadmaps-better-mko-userspace-c-in-the-kernel-and-wine)

## Native display stack

The native display path has a Multiboot linear-framebuffer backend and a
hardware-independent Stage 2 ARGB software renderer. See
[framebuffer Stage 1](docs/framebuffer-stage1.md) and
[graphics Stage 2](docs/graphics-stage2.md).

A native MKO ring-3 display server now owns bounded framebuffer transfers and
input capabilities; see [compositor Stage 6](docs/compositor-stage6.md).
Every graphical boot exercises:

- three isolated creation clients, creation z-order, crash containment,
  atomic presentation, and pointer-driven focus;
- a fourth client that moves a window and triggers frame two;
- an atomic IPC/input waitset, where real pointer press, motion, and release
  events drag the focused window and trigger frame three;
- bottom-right capture that resizes the window within minimum/screen bounds
  as frame four;
- a separate persistent ring-3 client that receives and validates focused
  `KEY` packets;
- init binding the live display-server PID to `desktop-compositor.service`,
  reaching `desktop.target`, and the panel launcher toggling as frame five;
- cursor-only motion repainting frame six before the userspace scene is
  restored as frame seven after the boot diagnostic.

Dirty scenes use timer deadlines for measured two-tick frame pacing. The
shell pumps only IRQ-woken desktop work, so the compositor stays interactive
without polling. A bounded 64×64 client-owned surface is transferred and
mapped read-only into the compositor at a fixed userspace address;
client-owned damage metadata is consumed exactly once, and final-handle
close clears and reclaims the arena slot.

PS/2 mouse decoding and the common keyboard/mouse event ABI are documented in
[input Stage 3](docs/input-stage3.md). Bounded dynamic ELF64 spawning,
process waiting, cleanup, and capability policy are documented in
[process Stage 4](docs/process-stage4.md). Named capability channels and
blocking message delivery are documented in [IPC Stage 5](docs/ipc-stage5.md).

Graphical boots finish in the native userspace desktop: `desktop.target`
retains framebuffer and unified-input ownership through the ring-3
compositor. MakoBox remains the recovery console for framebuffer-less boots
rather than covering or suspending the graphical session.

The normal ISO presents a native wallpaper-backed loading transition and a
local login/lock gate — the current preview credential is `demon` / `demon`,
and Super+L relocks the live session. The unattended test ISO alone bypasses
that gate. The `terminal-smoke` compatibility target verifies the desktop
remains visible and compositor-owned after boot and responds to pointer
motion without entering terminal ownership.

The ISO also includes a freestanding C Tetris application, launchable from
the recovery console with `tetris` or `apps launch tetris` (A/D, W, S, and
Q). The reproducible freestanding C build contract is in
[C applications](docs/c-apps.md). The interactive image includes the real
DoomGeneric engine and checksum-pinned Freedoom 0.13.0 Phase 1 IWAD; launch it
with `doom` at MakoBox. See the [Freedoom port](docs/freedoom-port.md).
`make freedoom-smoke` additionally builds an unattended image and validates
real gameplay, rendering, configuration writes, and clean exit in ring 3.

## DemonX: X11 compatibility

The ISO boots [DemonX](docs/demonx.md), a clean native X11 compatibility
service. Its freestanding C server parses X11 11.0 wire requests transported
over capability IPC, while a MAKO client proves setup, resource allocation,
window creation/mapping, geometry replies, destruction, and error handling.

## Demon Web

The desktop WEB launcher starts **Demon Web**, a dedicated native MKO
document-browser process rather than the old counter-app placeholder. It has:

- an editable location field;
- a four-entry back/forward history;
- home and reload controls;
- mouse and keyboard navigation;
- rendered `demon://home`, `demon://about`, `demon://network`, and
  `demon://files` pages on a client-owned shared surface.

`file:///` addresses are capability-checked reads from the real RAMFS —
press `5` on the home page to open the ISO-seeded `/README.md`. Reads are
bounded to a 512-byte preview so a document never forces the lightweight
client to mirror a large kernel object in RAM.

This is the real browser/UI foundation, but it is intentionally not
described as internet-capable yet: NIC, TCP/IP, DNS, HTTP, and TLS remain
platform work.

## Bootable ISO distribution

`make iso` produces `build/kernel.iso` and verifies that it contains the
kernel, the shared MKO SDK, starter project source, compiled starter ELF,
native compositor/client ELFs, and desktop roadmap. GRUB loads them as
Multiboot2 modules; the kernel reserves their physical ranges and installs
them into the RAM project store. At the interactive prompt, `mko` shows the
installed environment and `projects` lists the actual files.

`make session-login-smoke` boots the normal `build/kernel-run.iso`, verifies
wrong-password rejection, unlocks the desktop through real PS/2 key events,
and verifies Super+L returns to the lock screen.

`make run` enables QEMU GTK input grabbing so the host pointer does not
remain visible beside the DemonOS PS/2 cursor. On a Wayland desktop it
deliberately runs QEMU's GTK window through XWayland: QEMU's native
GTK/Wayland path can deliver only the entry motion before relative-pointer
capture is established. Press `Ctrl+Alt+G` to release input. PS/2 reports
relative motion only; absolute host/guest synchronization requires the
planned USB HID/tablet driver.

The SDK and native runtime are preinstalled today. The compiler is still a
host tool until the self-hosted compiler core and its required runtime
services are ported. That work is specified in the
[desktop ISO roadmap](docs/desktop-iso-roadmap.md).

## Current milestone

- GRUB Multiboot2 boot
- 32-bit bootstrap into x86_64 long mode
- MKO-built four-level page tables identity-mapping the first 4 MiB with 4 KiB leaves
- Separate bootstrap, syscall/TSS, and user stacks
- Freestanding C entry point
- COM1 serial output and VGA text output
- Multiboot2 bootloader and memory-map reporting
- Kernel-safe MAKO compiler probe lowered through optimized MIR
- System V x86_64 assembly emission for the freestanding integer subset
- MKO-generated `mako_add` and `mako_mix` linked into and called by C
- Multiboot2 usable-memory accounting implemented by MKO-generated code
- Kernel-only volatile memory intrinsics and direct VGA writes from MKO
- Native MKO decoding of packed Multiboot2 memory-map entries via typed pointers
- MKO 4 KiB physical frame bump allocator initialized from firmware memory
- MKO-built four-level x86_64 page tables with 4 KiB leaves and live CR3 switch
- x86_64 IDT, returning breakpoint self-test, and CR2 page-fault diagnostics
- MakoBox BusyBox-style kernel applet dispatcher with live system commands
- Remapped legacy PIC, 100 Hz PIT timer, initialized PS/2 controller, and interactive shell
- DPL3 syscall gate with validated user buffers and `write`, `yield`, `exit`,
  `ticks`, and `getpid`
- GDT/TSS setup, user-only code/stack pages, and a real ring-3 init program
- Preemptive round-robin scheduler with two ring-3 tasks, per-task kernel stacks,
  saved interrupt frames, PIT quanta, and live counters
- Validated ELF64 loader with native MKO code inside the user executable
- A private four-level page-table hierarchy, code/heap/stack frames, and CR3
  for every user process
- Owned, generation-checked capability handles for console, clock, and process
  services, including foreign/stale-handle rejection
- Compact 288 KiB RAMFS payload arena with 20 file slots, owned file capabilities,
  and bounded reads/writes
- Typed MKO wrappers for every MAKO-ABI 0.1 syscall; only ELF entry glue is assembly
- Recursive native `use` module linking with namespaced symbols and cycle checks
- Standalone portable project ELF built against the shared `MakoAbi` SDK
- Arch-style colored boot status, MAKO banner, and kernel welcome screen
- Dependency-aware init system with ten graphical units, PID-bound
  `desktop-compositor.service`, and console-only fallback
- `runit` init/unit inspection and lifecycle transactions
- `runas` administrative allowlist with grant/denial auditing
- Allocation-free ELF application discovery through `apps`
- Native Git worktree/snapshot foundation without a host Linux binary
- `desktop status` readiness contract for the graphical stack
- Multiboot2 640×480×32 linear framebuffer discovery and safe high-memory mapping
- Clipped ARGB software backbuffer, dirty presentation, primitives, blitting, and bitmap text
- Native graphical boot diagnostic with serial/VGA fallback
- Ring-3 MKO compositor with three protocol clients, atomic tiled presentation,
  pointer hit-testing, click-to-focus routing, and client crash containment
- Persistent compositor receive/repaint loop with retained window geometry and
  a later isolated `MOVE` client verified across two atomic frames
- Atomic blocking IPC/input waitset with title-bar drag driven by pointer
  press/motion/release, verified as a third atomic frame
- Bounded bottom-right pointer resize from 320×240 to 376×296, with adjacent
  motion events coalesced into one final-position repaint
- Focused keyboard routing into a persistent isolated `window-event-client`
  through a client-owned named channel
- Timer-deadline dirty-scene wakeups that enforce a measured two-tick minimum
  between atomic presents without compositor polling
- Native compositor title bar, resize grip, panel launcher, and a real
  pointer-driven launcher-open repaint
- Read-only zero-copy compositor mapping of the retained surface, with
  capability validation and client-owned damage submission/consumption
- Compositor-owned arrow cursor whose motion alone schedules a paced repaint,
  plus compact native glyph rendering for the launcher label
- IRQ-driven shell/desktop event pump that resumes ready ring-3 work without
  polling or idle CPU burn
- Live init-owned `desktop.target` with a seventh post-diagnostic repaint that
  leaves the compositor blocked and ready beside the console
- Capability-transferred client surfaces backed by a bounded 160 KiB physical arena,
  retained after creator exit and reclaimed/cleared on final-handle close
- Persistent freestanding C DemonX server with a boot-tested MAKO client and
  an initial X11 11.0 core subset (`CreateWindow`, `MapWindow`, `GetGeometry`,
  `DestroyWindow`, resource IDs, replies, and errors)

## MakoBox

MakoBox is the kernel's single-binary command toolkit. `makobox_run()`
dispatches `help`, `uname`, `status`, `mem`, `frames`, `paging`, `ticks`,
`ps`, `abi`, `caps`, `projects`, `runit`, `runas`, `mko`, `input`,
`fetch`, and `clear` without a C library. The diagnostic applets read live
kernel state and write to both VGA and serial. Boot executes the dispatcher
through all read-only applets and requires `MAKOBOX_SELF_TEST_OK`. The
keyboard shell reuses this same dispatcher rather than implementing commands
twice.

The dependency-aware init system and its deliberately smaller systemd-like
operator surface are documented in
[MAKO Init and Service Management](docs/init-system.md). Graphical boot
resolves ten units through `desktop.target`; framebuffer fallback resolves
the original eight through `default.target`. Read-only `runit`
operations are public; mutations require the local-console `runas` policy
boundary.

Application discovery, the native Git foundation, and the boundary for
dynamic launch/desktop work are documented in
[Applications and Git Foundation](docs/apps-and-git.md).
The staged route from that recovery-oriented snapshot engine to an
interoperable upstream fork is in
[Forking and Porting Git to DemonOS](docs/git-port.md).

The first real graphics milestone is documented in
[Native Framebuffer Stage 1](docs/framebuffer-stage1.md). It maps QEMU's
high physical framebuffer, allocates a bounded backbuffer only when
graphics is available, and renders a live graphical boot diagnostic without
exposing device memory to userspace.

`make run` opens QEMU's VGA window; click it and type at the `mako#` prompt.
The PS/2 IRQ driver supports US set-1 keys, Enter, Backspace, Shift, and Caps
Lock. The kernel explicitly enables the i8042 first port, set-1 translation,
IRQ1, and keyboard scanning instead of assuming the bootloader left them
active. The `input` applet reports live IRQ and decoded character counts;
`make keyboard-smoke` types `help` through QEMU and verifies the command
actually runs.

`fetch` is MakoBox's fastfetch-style summary. It reads CPUID, the live
timer, memory-map totals, active CR3, allocator cursor, and linked kernel
size. Kernel C uses `-Os`; functions and data get individual ELF sections,
and the linker garbage-collects unreachable sections. Run `make size` for
the image footprint.

## Scheduler, syscalls, and user mode

The scheduler owns PID 0 (`idle`) plus PID 1 (`init`) and PID 2 (`worker`).
Each user task has a separate 4 KiB ring-0 interrupt stack and 8 KiB user
stack. The PIT charges CPU ticks to the running task and performs
round-robin preemption every four ticks. The IRQ stub saves all
general-purpose registers, C selects the next task and updates the TSS
stack, then assembly restores the selected interrupt frame and returns with
`iretq`. `yield` uses the same switch path; `exit` records the status and
schedules the remaining task or returns to idle. The `ps` applet reports
state, charged ticks, yields, quantum expirations, and dispatches.

The loader validates and copies an embedded ELF64 executable to `0x300000`
in each private address space. Its capability, project-storage, scheduling,
isolation, and exit logic comes from [native MKO source](user/init.mko)
linked into the ELF. Timer IRQs arriving in ring 3 switch to the current
task's TSS `rsp0` stack. Both processes remain live across timer
preemption, yield, and exit with statuses 41 and 42. They store different
PIDs at the same virtual stack offset, proving the physical mappings are
isolated.

The current `int 0x80` ABI is:

| RAX | Call | Arguments | Result |
| ---: | --- | --- | --- |
| 0 | `write` | RDI=user address, RSI=length | bytes written or `-1` |
| 1 | `yield` | none | 0 |
| 2 | `exit` | RDI=status | does not return to ring 3 |
| 3 | `ticks` | none | live 100 Hz PIT tick count |
| 4 | `getpid` | none | current process ID |
| 5 | `service_open` | RDI=service ID | owned handle or `-1` |
| 6 | `handle_write` | RDI=handle, RSI=address, RDX=length | bytes or `-1` |
| 7 | `handle_query` | RDI=handle, RSI=property | value or `-1` |
| 8 | `handle_close` | RDI=handle | 0 or `-1` |
| 9 | `file_open` | storage handle, name, length, create flag | file handle or `-1` |
| 10 | `handle_read` | file handle, destination, capacity | bytes or `-1` |

The full compatibility boundary is documented in
[MAKO Application ABI 0.1](docs/application-abi.md). It keeps projects
independent of host operating system APIs while remaining explicit that
current binaries are x86-64. The typed MKO userspace SDK is live; multiple
ELF segments, copy-on-write, and persistent storage are the next larger
runtime steps.

The ISO also includes a compressed snapshot of the actual
`https://github.com/AnimatedGTVR/MAKO.git` checkout at
`/system/mako/MAKO-source.tar.zst`. Its boot-visible `mako.txt` manifest
records the origin, exact commit, working-tree state, archive size, and
SHA-256. This is the compiler repository source, not a substitute SDK.
Running that .NET-based compiler inside the freestanding kernel remains a
separate VFS/runtime port.

The reusable SDK lives in [`user/sdk.mko`](user/sdk.mko). The independent
[`projects/hello`](projects/hello/main.mko) example imports it with normal
MAKO `use` syntax and builds to a 1,031-byte ELF. This is the project-home
model: application code targets one small MAKO contract regardless of
whether its development tools are running on Windows or Linux.

## Build and run

```bash
make                 # build build/kernel.elf
make project         # build build/portable_hello.elf against the MKO SDK
make iso             # build and verify the complete bootable distribution ISO
make iso-check       # verify required files inside the existing ISO
make run             # boot interactive ISO (including Freedoom) in QEMU
make run-doom        # explicit alias for the Freedoom-capable interactive ISO
make smoke           # headless boot assertion via serial output
make keyboard-smoke  # inject PS/2 keys and verify interactive command input
make mako-check      # kernel-profile check + optimized MIR lowering
make footprint-check # enforce init/runas and total-kernel memory budgets
make check           # smoke + MAKO checks
```

## Boot status and self-tests

The boot console reports each initialized subsystem with a colored `[ OK ]`
line on both VGA text mode and COM1 serial, followed by a MAKO welcome
banner. Serial output retains stable `MKO_*_OK` and `KERNEL_BOOT_OK` markers
so the human-friendly startup remains fully smoke-testable.

Status lines are check results, not simulated progress. Boot:

- reads CR0/CR3/CR4 to validate paging;
- requires a usable Multiboot2 memory map;
- derives the frame allocator floor from the real linked `kernel_end` and
  boot-information end;
- checks consecutive physical allocations;
- executes native MKO arithmetic;
- reads the MKO-written VGA cells back before reporting success.

A failed check prints `[FAILED]` and halts.

## The native bridge

The C/assembly bootstrap is temporary infrastructure. The first native
bridge is live: MAKO emits `build/kernel_probe.S`, GCC assembles it into an
ELF64 relocatable object, and the C kernel calls its exported functions. The
intended migration path is now to expand native types and memory
operations, then replace kernel subsystems with MAKO one at a time.

## Roadmaps: better MKO userspace, C++ in the kernel, and Wine

- [Better MAKO in userspace](docs/mako-userspace-improvements.md) — U0/U1
  implemented: real string literals (kernel-profile MAKO compiler work) and
  a shared `MakoStd`/`MakoWire`/`MakoBytes` stdlib closing the gap between
  "MKO can technically do this" and code that doesn't reach for C/C++ as
  soon as it outgrows a small utility.
- [C++ in the kernel](docs/kernel-cxx-port.md) — K0 through K3 implemented:
  a kernel-side freestanding C++ runtime, and `src/ramfs.c`/`src/capability.c`
  migrated onto two distinct generic templates (`bounded_table<T,N>` for
  name-keyed slots, `slot_table<T,N>` for generation-checked handles) where
  templates and RAII are a genuine fit, while boot, interrupts, and the
  scheduler stay C/assembly.
- [Wine (Windows compatibility)](docs/wine-port.md) — W0-W4 implemented: a
  real PE/COFF header parser; explicit `reserve`/`commit` virtual-memory
  syscalls (chosen deliberately over a riskier recoverable page-fault
  handler this kernel doesn't have yet); a section loader that maps,
  zero-fills, and populates a PE32+ image's sections (permission
  enforcement deliberately deferred — found a real page fault proving why
  during its own first boot test); export/import name resolution with IAT
  patching, proven against two synthetic images sharing one process
  address space (which only became possible after finding, mid-test, that
  the original loader design could never have loaded more than one image
  per process at all); and base relocation application, correcting every
  absolute pointer a linker baked in for a preferred `ImageBase` DemonOS's
  anonymous region essentially never matches. Framed honestly as a much
  longer effort than the game-engine ports above — Wine assumes a POSIX
  host DemonOS doesn't have yet, so this starts from provable foundations
  rather than a pinned upstream commit.
