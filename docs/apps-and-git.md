# Applications and Git Foundation

## Application catalog

`app-manager.service` scans the installed RAM filesystem without copying app
images. Every `.elf` candidate is checked for the ELF magic, 64-bit little-endian
class, and x86-64 machine ID. The fixed catalog holds at most eight records and
stores pointers to immutable RAMFS names, so it adds no heap or image-sized
buffers.

```text
apps list
apps info hello
apps launch hello
```

Listing and inspection are live. `apps launch` deliberately remains blocked
until the existing boot-time ELF loader is generalized into a dynamic process
spawn service with per-process frame allocation and scheduler registration.

## Native Git foundation

The `git` applet is native freestanding code, not an unusable Linux binary. It
currently implements repository initialization, live clean/modified worktree
status, bounded snapshot commits, and a commit counter over the RAM project
store:

```text
git version
git init
git status
git commit
git log
```

This is intentionally identified as a worktree foundation, not Git format or
protocol compatibility. Real Git interoperability requires SHA-1/SHA-256 object
IDs, canonical tree/commit serialization, zlib loose objects, pack index/delta
support, persistent storage, TLS, DNS, and HTTP/SSH transports. Those pieces
will be added behind the same command surface as their kernel prerequisites
land.

## Desktop boundary

`desktop status` reports tested prerequisites separately from remaining desktop
work. Input, process isolation, the app catalog, storage, capabilities,
framebuffer transfer, mouse events, and the compositor/window protocol are now
ready. Graphical boot reaches a PID-bound `desktop.target`; the persistent
compositor blocks on IPC and input together, moves a client window from real
press/motion/release events, and reclaims surfaces when their last capability
closes. It also supports bounded pointer resize and focused keyboard delivery
to an isolated persistent client. It also reports real 64×64 capability-backed surfaces,
timer-backed frame pacing, decorations, and the pointer-driven launcher as
ready. The final desktop-demo contract also includes a read-only zero-copy
surface mapping, owned damage metadata, compositor-owned cursor repaint, and
compact native launcher glyphs. Full font shaping and a broader application
suite remain Phase 5 work rather than kernel/platform blockers.
