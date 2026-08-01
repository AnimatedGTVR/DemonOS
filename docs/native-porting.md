# Native application porting kit

`demon/portkit.h` is the common platform boundary for freestanding C ports.
Use it for Doom, utilities, emulators, and games instead of embedding raw
syscalls in upstream code.

## Minimal application

```c
#include <demon/portkit.h>

#define ARENA_SIZE (4u * 1024u * 1024u)

uint64_t app_main(void) {
    if (!demon_port_init_dynamic(ARENA_SIZE)) return 1;
    demon_port_write("hello from a native port\n");
    demon_port_shutdown();
    return 0;
}
```

Use an `_start` like `apps/portcheck/entry.S`, compile with the repository's
`CFLAGS`, and link with `user/linker.ld`, `user/portkit.c`, and `-nostdlib`.
Run `make portkit-check` to validate this exact path.

## What the kit guarantees

- a bounded 16-byte-aligned heap with allocation-failure behavior and a peak
  usage counter;
- capability-backed whole-file open/read/seek/close for small boot assets;
- monotonic milliseconds and a yielding sleep;
- unified key/mouse event polling;
- console diagnostics and a process-local fatal exit.

Nothing grants implicit kernel authority. `demon_port_init` only opens the
storage and input capabilities assigned to that process. A missing capability
fails cleanly.

## Porting workflow

1. Keep upstream source below `ports/<name>/upstream` and pin a revision.
2. Put all DemonOS-specific code below `ports/<name>/platform`.
3. Compile upstream unchanged first; record every unresolved symbol.
4. Implement symbols in the common kit when they are broadly useful, or in
   the platform directory when they are engine-specific.
5. Add a compile/link target that never downloads network content implicitly.
6. Add malformed-input, allocation-failure, repeated-launch, and clean-exit
   tests before registering the app in the boot image.

## Current hard limit

The kernel now supplies one on-demand anonymous arena of up to 24 MiB per
process through syscalls 40/41. Pages are zero-filled, mapped only for a
requesting process, and returned to a reusable kernel frame list on unmap or
process exit. Lightweight applications therefore keep their small fixed
footprint. This is deliberately not advertised as full POSIX `mmap`.

The arena guarantees:

- page-aligned mappings owned and released per PID;
- a per-process byte/page limit;
- zero-filled pages and checked overflow at every boundary;
- clean reclamation on process exit;
- a 24 MiB Doom limit without charging idle/lightweight applications for it.

Large immutable ISO assets are reference-backed by their reserved Multiboot
frames and streamed with bounded positional reads. PortKit's file adapter does
not copy them into the heap: open records a handle and size, read issues
`handle_read_at`, and seek changes only the logical position. The ring-3
portcheck seeks across the real 5+ MiB MAKO source archive on every smoke boot.
This is the same transport a WAD port should use.

`apps/doom/libc.c` is the current freestanding compatibility subset for larger
C engines. It supplies memory/string/ctype/numeric helpers, `qsort`, bounded
`snprintf`, and standard allocation names backed by PortKit. Keep OS-facing
stdio and platform behavior in the port shim; do not add host libc or POSIX
dependencies to this computation layer.

Binary asset parsers should follow `apps/doom/wad.c`: decode fixed-width
little-endian fields explicitly, prove table bounds before multiplication,
validate each referenced range with subtraction, and stream fixed-size records
instead of mapping or allocating an untrusted table wholesale.
