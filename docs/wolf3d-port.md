# Native Wolfenstein 3D port (scoping)

## Scope

The port target is Wolf4SDL (`mozzwald/wolf4sdl`), an SDL-ported build of id
Software's original Wolfenstein 3D engine, pinned to
`5387b99d32fc5bac39c87defcb0abbf1018d8083`. This is a native DemonOS
application, not a Linux binary or SDL wrapper.

No game data is bundled: the shareware or registered VSWAP/maps files are
supplied separately by the user. The underlying id Wolfenstein 3D source is
still under id's original 1995 "Limited Use Software License Agreement" --
personal/educational use is fine, but unlike Doom and Quake it was never
relicensed under GPL. Treat this the same way as the Doom/Quake shareware
data: pinned locally for a hobby build, not redistributed as source.

## Why this differs from the Quake/Doom platform boundary

WinQuake (`sys.h`/`vid.h`) and doomgeneric (`DG_*`) both expose a thin,
already-abstracted platform interface: a handful of `Sys_*`/`VID_*`/`DG_*`
functions to implement, upstream otherwise unaware of the host. Wolf4SDL has
no equivalent seam -- `id_vl.cpp` (video), `id_in.cpp` (input), `id_sd.cpp`
(sound), and `id_pm.cpp` (the page/file manager) call `SDL_Surface`,
`SDL_Color`, `SDL_RWops`, and SDL_mixer channels directly throughout.

The DemonOS platform layer will therefore replace the *bodies* of those four
files wholesale (same shape as `sys_demonos.c`/`vid_demonos.c` in the Quake
port: same function names and signatures the rest of the engine calls,
DemonOS syscalls underneath) rather than filling in a small stub header.
`wl_main.cpp`'s `main()` (in `sdl_winmain.cpp`) becomes the equivalent of
`core_main.c`.

## Status

- `tools/fetch-wolf4sdl.sh` pins the upstream commit (`make wolf3d-source`).
- `make wolf3d-source-audit` proves the pinned tree still exposes the
  `VL_SetPalette` / `IN_ProcessEvents`+`IN_ReadControl` / `PM_Startup` /
  `SD_Startup` symbols the platform replacement will target.
- No `ports/wolf3d/platform` files exist yet -- next step is `id_vl.cpp`
  (video: 320x200 paletted framebuffer onto `demon_display_submit`, same
  path `vid_demonos.c` already uses) since it is the smallest of the four
  (~740 lines) and has no data-file dependency, so it can be proven before
  touching sound or the page manager.
- Shareware VSWAP/game data source has not been resolved: the usual
  `idgames`-style mirrors don't currently have a working Wolf3D shareware
  archive from this environment (unlike `tools/fetch-quake.sh`'s
  `idgames.cyberd.org` mirror, which does). Needs a verified URL + checksum
  before a `wolf3d-data`/`wolf3d-play-smoke` target can exist.
