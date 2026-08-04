# Native Quake (1996) port

## Scope

The port target is id Software's Quake from the 1999 GPL v2 source release
mirrored at `id-Software/Quake`, the single-player `WinQuake/` directory.
This is a native DemonOS application, not a Linux binary, SDL/OpenGL wrapper,
or reimplementation. The source is pinned to
`bf4ac424ce754894ac8f1dae6a3981954bc9852d`.

No game data is bundled: the shareware or registered PAK files are supplied
separately by the user, exactly as with the GPL release (which ships no PAK
content). Any distributed binary must make the modified GPL v2 source
available. Quake is copyright 1996-1997 id Software, Inc., GPL v2.

## Why this upstream

Quake's software renderer (`d_*.c` surface caches, span/warping, and the
C-only rasterizer path) is a self-contained, well-documented rendering
pipeline with no external dependencies beyond a few libc/libm calls. That
fits DemonOS much better than OpenGL ports (QuakeGL) or wrappers. The
`sys.h`/`vid.h`/`d_iface.h` interfaces cleanly isolate the platform layer.

## Current DemonOS readiness

| Requirement | State | Port decision |
|---|---|---|
| ELF64 processes and bounded heap | Available | Arena via PortKit `demon_port_init_dynamic` |
| Monotonic clock and sleep/yield | Available | `Sys_FloatTime`/`Sys_Sleep` through PortKit |
| Keyboard and mouse | Available | Unified input events into Quake's `KBD_*` |
| 320x200 8-bit pixel surfaces | Available | `viddef_t.buffer` presented to the framebuffer |
| Writable files | Available | `Sys_File*` over PortKit handles |
| WAD/PAK loading | Available | PAK loader over PortKit `Sys_File*` |
| Software renderer CPU budget | Soft-float only in Doom libc | Quake builds its libc with real SSE doubles |
| PCM sound | Available | Stub first (`snd_null`), mixer later |
| Threads | Not a general userspace ABI yet | Cooperative/single-threaded first |
| OpenGL/GPU acceleration | Not available | Not required for the software-rendered milestone |

## Milestones

### D0 — source and contract (implemented)

- Reproducible, explicit fetch at a pinned commit via `tools/fetch-quake.sh`.
- Dedicated `ports/quake/platform` boundary with `sys_demonos.c` (the vanilla
  `sys.h` shim over PortKit).
- Audit target checks upstream `sys.h`/`vid.h` symbols and the shim file.
- No Quake game data silently bundled into normal builds.

### D1 — freestanding core (implemented)

- Build a native `quake-core.elf` from genuine upstream units:
  `zone.c`, `mathlib.c`, `crc.c`, `cmd.c`, `cvar.c`.
- `com_demonos.c` is a faithful subset of upstream `common.c` (`Q_*` helpers,
  `COM_Parse`, `COM_CheckParm`, `SZ_*`) plus console routing; the only stubs
  are honest ones for features D1 does not wire up (PAK file load, server
  broadcast).
- `libm_demonos.c` provides `sin/cos/tan/sqrt/fabs/floor`; the shared
  `apps/doom/libc.c` gained an optional `%f` formatter (`QUAKE_LIBC_FLOAT`)
  so `Cvar_SetValue` and console logging format real floats.
- The ELF is installed as `/system/bin/quake-core.elf` and runs as a normal
  ring-3 process from the MakoBox console (`quake-core`).

Acceptance markers from the headless smoke boot (`make quake-smoke`):

```
QUAKE_D1_OK zone zero-fill
QUAKE_D1_OK hunk alignment
QUAKE_D1_OK hunk lowmark/free
QUAKE_D1_OK crc bytes
QUAKE_D1_OK vector normalize
QUAKE_D1_OK cvar register
QUAKE_D1_OK cvar setvalue
QUAKE_D1_CMD_RAN argc=1 argv0=quake_selftest
QUAKE_D1_SUBSYSTEMS_READY zone crc mathlib cmd cvar
apps: process exited with status 0
```

### D2 — memory, console, and command core (implemented)

- `Cmd_Init()` registers the genuine upstream `exec`, `echo`, `alias`, `cmd`,
  `wait`, and `stuffcmds` commands from `cmd.c`; the D2 core drives the full
  Cbuf→`Cmd_ExecuteString` command stream with function→alias→cvar dispatch.
  Supporting the real `cmd` forwarding needed `client_static_t cls;`,
  `MSG_WriteByte`, and `MSG_WriteString` (faithful common.c ports).
- `COM_WriteFile` and `COM_LoadHunkFile` are faithful common.c ports over the
  vanilla `Sys_File*` shim, resolving relative names against `com_gamedir`
  (`/home/demon`). No PAK search path yet — that is the D3 asset phase.
- First PortKit-backed file round-trip: config file written, closed,
  reopened, read back byte-for-byte, and `exec`'d through the console.

Acceptance markers from the headless smoke boot:

```
QUAKE_D2_OK cmd init
COM_WriteFile: /home/demon/quake-test.cfg
QUAKE_D2_FILE_ROUNDTRIP_OK bytes=35
QUAKE_D2_OK com loadhunkfile
inline_echo
execing quake-test.cfg
config_loaded
QUAKE_D2_OK console exec+set
QUAKE_D2_OK console alias
QUAKE_D2_CONSOLE_OK cmd_file=/home/demon
QUAKE_D2_SUBSYSTEMS_READY console cmd files
```

### D3 — asset pipeline

- PAK loader (`wad.c`/`cmd.c` packfile path) over PortKit `Sys_File*`.
- PAL/LMP colormap + `Draw_*` and texture cache (`surfcache`) bring-up so a
  known PAK's palette and a few lumps load and CRC-verify.

Marker: `QUAKE_D3_PAK_OK pak=id1 crc=verified`.

### D4 — video surface

- `vid_demonos.c`: `VID_Init`/`VID_SetPalette` filling `viddef_t.buffer`
  (320x200 8-bit), translated to the DemonOS framebuffer.
- First frame presented from real renderer state.

Marker: `QUAKE_D4_VID_OK surface=320x200 frames=N`.

### D5 — interactive software-rendered game

- Input layer (`in_demonos.c`) mapping unified events to Quake's `KBD_*`
  constants and mouse look.
- Real `d_*` software rasterizer, BSP world rendering, and timed
  `Sys_SendKeyEvents`/frame loop; `Sys_Quit` returns to the MakoBox console.

Markers: `QUAKE_D5_FRAME_OK map=start surface=320x200` and
`QUAKE_D5_INPUT_OK key=W look=...` with deterministic quit.

### D6 — sound and polish

- `snd_null` first, then PCM mixer over PortKit audio once the AC97 path is
  wired for game use.
- Save/load (`saverestore`), config write-back, performance tuning within the
  CPU-only renderer budget.

## Build

- `make quake-source` — fetch and pin the upstream tree.
- `make quake-port-audit` — verify upstream platform contracts.
- `make quake-core` — build the D1 freestanding core ELF.
- `make quake-smoke` — boot headless QEMU, launch `quake-core`, assert the D1
  markers and a clean status-zero exit.
