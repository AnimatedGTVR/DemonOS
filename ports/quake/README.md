# Quake for DemonOS

This directory is the native port boundary for id Software's Quake (1996),
from the 1999 GPL v2 source release mirrored at
`id-Software/Quake` (pinned revision). It is a freestanding x86_64 ring-3
process: no host libc, SDL, X11, OpenGL, or GPU acceleration. The pure-C
software renderer in `WinQuake/` renders directly into the DemonOS
framebuffer.

No game data is bundled: the shareware or registered PAK files are supplied
separately by the user. The engine and all code are GPL v2 (see the upstream
`gnu.txt`); any distributed binary must make the modified source available.

Run `make quake-source` to obtain the pinned source and
`make quake-port-audit` to verify that the expected upstream platform
contracts are still present. See `docs/quake-port.md` for the staged port.

`platform/sys_demonos.c` implements the vanilla `sys.h` interface
(`Sys_Printf`, `Sys_File*`, `Sys_FloatTime`, `Sys_Error`, ...) on top of
PortKit; it must not return fake success for unimplemented operations.

`make quake-core` builds the freestanding core ELF from genuine upstream
`zone.c`/`mathlib.c`/`crc.c`/`cmd.c`/`cvar.c`; `make quake-smoke` boots it
headless and asserts the D1/D2 markers plus a clean status-zero exit.
`platform/com_demonos.c` is a faithful subset of upstream `common.c` (with
the D2 `COM_WriteFile`/`COM_LoadHunkFile`/`MSG_Write*` glue and an honest
`client_static_t cls;`), and `platform/libm_demonos.c` supplies the
freestanding `sin/cos/tan/sqrt/fabs/floor` subset.
