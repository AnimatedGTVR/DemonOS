# Porting Doom and Freedoom to DemonOS

## What is being ported

Freedoom is a libre IWAD: it supplies maps, sprites, sounds, and other game
data, but it is not an executable engine. DemonOS therefore needs both:

1. a Doom-compatible engine compiled as a native x86_64 DemonOS process;
2. `freedoom1.wad`, `freedoom2.wad`, or `freedm.wad` as separately packaged
   game data.

Do not replace either half with a Doom-themed demo. The port is complete only
when unmodified engine game logic loads an official IWAD and enters a playable
level.

The recommended first engine is **doomgeneric**. Its platform boundary is much
smaller than SDL-based Chocolate Doom, which is useful while DemonOS still has
a deliberately small libc and no SDL port. Chocolate Doom remains the later
compatibility target once the native multimedia APIs are stable.

## Repository layout

Keep third-party code isolated from kernel code:

```text
ports/doom/
├── README.md
├── platform/
│   ├── doomgeneric_demonos.c
│   └── doomgeneric_demonos.h
├── patches/                  # minimal, reviewable upstream patches
└── LICENSES/
    ├── doomgeneric-COPYING
    └── freedoom-COPYING

assets/games/freedoom/
├── freedoom1.wad
├── freedoom2.wad
├── freedm.wad
├── VERSION
└── SHA256SUMS
```

Generated WADs and cloned upstream trees should not be committed accidentally.
The packaging target downloads a pinned release only when explicitly asked.

The implemented asset pipeline pins Freedoom 0.13.0 and the official
`freedoom-0.13.0.zip` SHA-256
`3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59`.
It extracts only the two IWADs and `COPYING.txt`:

```text
make freedoom-assets   # explicit network operation
make freedoom-iso      # separate WAD-enabled test ISO
make freedoom-smoke    # boot and validate the official Phase 1 IWAD
```

Ordinary `make`, `make iso`, and `make smoke` remain offline and do not require
or download game data. The separate ISO installs the license at
`/licenses/doom/Freedoom-COPYING.txt`.

## Native platform contract

The small doomgeneric backend is implemented in
`ports/doom/platform/doomgeneric_demonos.c`:

```c
void DG_Init(void);
void DG_DrawFrame(void);
void DG_SleepMs(uint32_t milliseconds);
uint32_t DG_GetTicksMs(void);
int DG_GetKey(int *pressed, unsigned char *key);
void DG_SetWindowTitle(const char *title);
```

Map these functions to DemonOS as follows.

The timing and key-event paths now use PortKit. Frame presentation and title
changes use an installed callback backend so the eventual window client can
speak the compositor protocol without teaching the upstream engine about it.

### Video

- Engine framebuffer: 320x200, 32-bit ARGB.
- Create one retained process surface through the surface service.
- Copy or convert `DG_ScreenBuffer` into that surface once per rendered frame.
- Mark the whole 320x200 surface damaged for the initial port.
- Present it centered with integer 2x scaling at 640x400 when possible.
- Keep scaling in the compositor/display path; do not move the engine into
  kernel or framebuffer memory.
- Add dirty rectangles only after the full-frame path is correct.

The first build may run directly on the console display while MAKOBOX owns the
foreground. A later build can be an ordinary window client. Both must use the
same engine/platform code.

### Input

- Use the unified input ABI already exercised by native C Tetris.
- Request exclusive foreground ownership while Doom runs.
- Translate key-down and key-up events, not only characters.
- Minimum bindings: arrows, Ctrl/fire, Space/use, Shift/run, Escape/menu,
  Enter/select, number-row weapons, and Y/N confirmations.
- Drain pending shell characters when ownership transfers in either direction
  so gameplay cannot become a shell command afterward.
- Mouse support is a second milestone; keyboard-only gameplay is the bring-up
  requirement.

### Time

`demon_ticks()` is a monotonic 100 Hz source. Convert without floating point:

```c
uint32_t DG_GetTicksMs(void) {
    return (uint32_t)(demon_ticks() * 10u);
}
```

`DG_SleepMs` should block/yield until its deadline. A pure busy loop is allowed
only for the earliest diagnostic build and must not ship as the final port.

### Memory and libc

The reusable first layer now lives in `include/demon/portkit.h` and
`user/portkit.c`. `make portkit-check` links a real freestanding userspace ELF
against it. New ports should depend on this layer rather than copying Doom's
temporary libc/syscall glue. It currently provides a caller-bounded,
coalescing allocator, streaming seek/read adapter, monotonic millisecond
clock, yielding sleep, input polling, diagnostics, and fatal process exit.
`demon_port_init_dynamic(24u * 1024u * 1024u)` uses the bounded anonymous
mapping ABI, so the Doom heap no longer consumes RAM in unrelated processes.

Large immutable ISO modules now remain in their Multiboot-reserved frames and
are exposed through bounded `handle_read_at` calls. PortKit keeps only a file
handle, size, and position; it no longer copies a file into the application
heap. The boot regression test seeks through the real 5+ MiB compressed MAKO
source module using a 1 MiB heap. This completes the transport pattern needed
for a Freedoom WAD while keeping RAM usage proportional to the engine's read
buffer rather than the asset size.

The engine's first freestanding C runtime is implemented in
`apps/doom/libc.c` and linked into the ring-3 PortKit regression program. It
provides:

- `malloc`, `calloc`, `realloc`, and `free`;
- `memcpy`, `memmove`, `memset`, and `memcmp`;
- bounded string functions and integer parsing;
- `qsort` if required by the pinned engine revision;
- fatal-error output followed by the process exit syscall.

Start with a fixed 24 MiB process heap and record the high-water mark. Refuse
allocation cleanly instead of corrupting adjacent code or surfaces.

The allocator entry points delegate to PortKit's mapped arena; there is no
second fixed-address Doom heap. Smoke boot checks overlapping `memmove`,
integer parsing, `qsort`, bounded formatting, zeroed allocation, and content
preservation across `realloc` before exercising the large-file path.

### Files

The RAMFS now supports read-only, reference-backed boot assets and absolute
offset reads, including files larger than its internal 512 KiB copy arena.
The engine-facing operations are:

```c
doom_file *doom_open(const char *path);
size_t doom_read(doom_file *, void *buffer, size_t bytes);
bool doom_seek(doom_file *, uint32_t absolute_offset);
uint32_t doom_size(doom_file *);
void doom_close(doom_file *);
```

The first supported command line is:

```text
doom -iwad /games/freedoom/freedoom1.wad
```

On the normal interactive ISO and the dedicated playable Freedoom ISO, the
MakoBox command is simply:

```text
doom
```

The application registry resolves that command to the isolated large-image
executable and grants only that installed app the storage, input, display, and
surface capabilities needed by the port. Exiting Doom returns to MakoBox after
discarding stale keyboard/input events.

Validate every seek and read against the file size. WAD offsets are
untrusted data even when the archive shipped on the ISO.

`apps/doom/wad.c` now implements the allocation-free validation layer. It
accepts IWAD and PWAD headers, bounds the directory using division before any
multiplication, streams each 16-byte entry, and verifies each lump with
subtraction-based range checks. The ring-3 test covers a valid one-lump IWAD,
a near-4-GiB directory offset, and a lump extending beyond EOF. The remaining
official-asset boot test runs this reader across the 28,795,076-byte Phase 1
IWAD and validates all 3,163 directory entries in ring 3.

### Audio and saves

The full engine has native PCM sound effects. Doom's mixer resamples DMX sound
lumps into signed 16-bit, 44.1 kHz stereo and submits bounded buffers through
an audio capability. The kernel validates the userspace range before handing
the buffer to its AC'97 DMA driver. QEMU run targets attach an AC'97 device to
the host PipeWire backend; headless tests use the same device with a silent
backend. Music is intentionally disabled until a native sequencer is added.
Later audio work requires a PCM ring buffer, mixer, and an emulated device such
as AC'97 or Intel HDA.

Configuration and save writes now use buffered stdio over writable RAMFS
handles. Doom's temporary-save rename sequence is supported atomically by the
RAMFS namespace. Files survive repeated launches during the current boot;
persistence across reboot still requires the planned writable block-filesystem
layer.

## Build integration

Add a dedicated target instead of compiling Doom into `kernel.elf`:

```make
build/doom.elf: $(DOOM_OBJECTS) user/linker.ld
	$(LD) -nostdlib -T user/linker.ld $(DOOM_OBJECTS) -o $@

doom-assets:
	./tools/fetch-freedoom.sh 0.13.0
```

The fetch script must:

1. download the pinned official release archive;
2. verify a repository-owned SHA-256 value;
3. extract only the expected WAD and license files;
4. fail rather than silently accepting a different archive;
5. never run as part of an ordinary offline `make` or smoke test.

Install the engine as `/system/bin/doom.elf`, the WADs below
`/games/freedoom/`, and both license notices below `/licenses/doom/`. Register
`doom` in the application table and add a MAKOBOX command that launches it with
display, input, storage, and timer rights—no kernel-only authority.

## Milestones and acceptance tests

### D0 — Engine compiles

- [x] Pin upstream doomgeneric commit
  `dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284` explicitly.
- [x] Compile every pinned engine translation unit with freestanding x86_64
  flags and combine them into `build/doom-engine/doomgeneric-core.o`.
- [x] Implement and compile all six `DG_*` platform hooks against PortKit.
- [x] Replace read-only stdio, console formatting, ctype, numeric parsing, and
  unsupported write operations with a DemonOS-owned compatibility layer.
- [x] Link the engine, platform adapter, libc, and PortKit into one relocatable
  image with zero unresolved host POSIX, SDL, or libc symbols (`make
  doom-runtime-audit`).
- [x] Package `/system/bin/doom.elf`, run its deterministic version entry in
  ring 3, print `DOOM_ENGINE_READY`, and reap it with status zero.

The normal process layout still reserves only 192 KiB for executable images.
The complete `build/doom-full.elf` is 606,690 bytes across 149 pages, so the
loader now gives large executables an isolated, page-backed mapping beginning
at `0x20000000`. The fixed low code, heap, stack, and surface ABI remains
unchanged for existing applications. The boot smoke maps the full upstream
engine image, enters it in ring 3, prints `DOOM_FULL_IMAGE_MAPPED`, and reaps it
with status zero before continuing.

### D1 — WAD access

- [x] Parse and bounds-check valid, truncated, and malformed WAD structures in
  a native ring-3 process.
- [x] Open the official pinned IWAD through the large-file API.
- [x] Read and validate its `IWAD` header and directory bounds.
- [x] Print `FREEDOOM_IWAD_READY lumps=<count>`.
- [x] A truncated or malformed WAD fails without a kernel fault.

### D2 — Title frame

- [x] Allocate the 320x200 engine buffer and native surface.
- [x] Reach the real engine title loop using the official Freedoom Phase 1
  IWAD and the pinned upstream renderer.
- [x] Publish a deterministic FNV-1a screenshot hash and
  `FREEDOOM_TITLE_READY`.
- [x] Submit the complete frame through bounded kernel-side row chunks rather
  than granting the process direct framebuffer access.
- [x] Run for 300 frames with one retained surface and report the bounded
  PortKit heap high-water mark (`FREEDOOM_300_FRAMES_OK`).

### D3 — Playable keyboard build

- [x] Give the dedicated `doomtest` session its own boot-mode value so test
  automation never changes a normal interactive Doom launch.
- [x] Start episode 1/map 1 through the engine's real `-warp` command-line
  path and print `FREEDOOM_GAME_READY` only after `gamestate == GS_LEVEL`.
- [x] Translate arrows, letters, number-row weapons, Ctrl/fire, Space/use,
  Shift/run, Alt/strafe, Escape, Enter, Tab, Backspace, and F1-F12.
- [x] Derive key-up identities from physical scan codes so released movement
  and action keys cannot remain stuck when their character value is empty.
- Manual keyboard movement, firing, use, pause, and menu navigation work in a
  visible QEMU session.
- Escape returns safely to MAKOBOX and leaves no queued keystrokes.

### D4 — Robustness

- Repeatedly launch and exit Doom ten times.
- Heap high-water mark remains bounded.
- Invalid WAD paths and allocation failure produce user-space errors.
- The scheduler, shell, and filesystem remain usable afterward.

### D5 — Optional features

- Mouse capture and sensitivity.
- [x] Session-persistent configuration and saves on writable RAMFS.
- Reboot-persistent saves after the block-filesystem layer is writable.
- Music sequencing (PCM sound effects are implemented).
- Windowed compositor presentation.
- Chocolate Doom after the underlying libc/SDL-style platform services exist.

## Licensing and naming

The engine source and Freedoom data have separate licenses and notices; ship
both. Do not ship commercial Doom IWADs. Users may provide legally obtained
IWADs themselves, but DemonOS packaging and tests must use Freedoom. Keep the
product wording precise: **Doom-compatible engine running Freedoom**, not
“Freedoom engine.”

Official references:

- <https://github.com/ozkl/doomgeneric>
- <https://github.com/freedoom/freedoom/releases>
- <https://github.com/chocolate-doom/chocolate-doom>
