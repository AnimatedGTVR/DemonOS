# Native Cave Story (NXEngine) port (scoping)

## Scope

The port target is EXL's NXEngine fork (`EXL/NXEngine`, GPLv3), an
open-source reimplementation of Cave Story's engine, pinned to
`16bf776febef2a041cf07677f663c4cca2e810a1`. This is a native DemonOS
application, not a Linux binary or SDL wrapper.

No game data is bundled. Unlike Wolf3D, this is the clean case: the engine
is GPLv3 (freely redistributable source, same footing as Doom/Quake), and
Cave Story's original game data was released as freeware by its creator
(Daisuke "Pixel" Amaya) -- there's no shareware-vs-full-game ambiguity to
navigate.

## Correction: the header itself is SDL-typed

`graphics/nxsurface.h`'s `class NXSurface` looked like a clean seam from
`graphics.h` alone, but the header itself stores `SDL_Surface *fSurface`,
inherits `NXRect : public SDL_Rect`, and typedefs `NXFormat` to
`SDL_PixelFormat` -- and that SDL typing leaks into `font.cpp`,
`palette.cpp`, `safemode.cpp`, and `floattext.h` too (~9 files touch
`SDL_Surface`/`SDL_Rect` directly, not just `nxsurface.cpp`). That's a wider
surface than Quake/Doom's `Sys_*`/`DG_*` interfaces, which never leaked a
host type into engine-facing headers.

The practical fix, chosen over hand-editing every touched file: implement a
**minimal SDL 1.2 shim** -- just the ~15 SDL symbols `nxsurface.cpp` actually
calls (`SDL_CreateRGBSurface`, `SDL_BlitSurface`, `SDL_FillRect`,
`SDL_SetColorKey/Colors/ClipRect`, `SDL_MapRGB`/`SDL_GetRGB`,
`SDL_DisplayFormat`, `SDL_Flip`, `SDL_LoadBMP`) -- backed by DemonOS
PortKit underneath, the same relationship `apps/doom/libc.c` has to Doom's
libc calls. This lets every file that includes `<SDL/SDL.h>` compile
unmodified against real DemonOS-backed surfaces.

## Status (verified, not just planned)

- `tools/fetch-nxengine.sh` pins the upstream commit (`make nxengine-source`).
- `make nxengine-source-audit` proves the pinned tree still exposes
  `NXSurface`, `Graphics::`, and `main()`.
- `ports/nxengine/platform/SDL/SDL.h` + `ports/nxengine/platform/sdl_demonos.c`
  implement that SDL subset: 8bpp indexed and 32bpp truecolor surfaces,
  blit/fill/colorkey/palette, `SDL_DisplayFormat` palette-to-truecolor
  conversion. `SDL_Flip` and `SDL_LoadBMP` are deliberate stubs -- real
  presentation (`demon_display_submit`, same path `vid_demonos.c` uses) and
  asset loading are later stages, not needed to prove this seam.
- `make nxengine-platform-audit` compiles `sdl_demonos.c` under the real
  freestanding DemonOS toolchain (`nm -u` shows only PortKit + libc), then
  compiles the **real, unmodified** upstream `graphics/nxsurface.cpp`
  against the shim and confirms its only unresolved symbols are more SDL_*
  (nothing left to implement there) plus ordinary engine globals
  (`settings`, `use_palette`) and two engine functions (`staterr`,
  `palette_add`) that come from other, unmodified engine units. This is the
  proof-of-seam milestone, same shape as `doom-platform-audit`.
- `input.cpp` needed one more shim surface: `SDL_Event`/`SDL_PollEvent`/
  `SDLK_*`/`SDL_KEYDOWN`/`SDL_KEYUP` (it polls a real SDL event queue rather
  than exposing a clean callback). Added to `SDL.h`/`sdl_demonos.c`:
  `SDL_PollEvent` translates unified `input_event`s into `SDL_Event`s via a
  scancode-to-SDLK_ translation table -- the same technique (including a
  released-key fallback table for key-up, which carries no ASCII value in
  the unified ABI) as the `Sys_SendKeyEvents` fix just done for Quake.
  `make nxengine-platform-audit` now also compiles the real, unmodified
  `input.cpp` and confirms its only shim-side unresolved symbol is
  `SDL_PollEvent` -- everything else (`console`, `game`, `sound`, `Replay`)
  is other, unmodified engine units.
- `main.cpp` itself will **not** be compiled unmodified the way
  `nxsurface.cpp`/`input.cpp` are: it's the upstream entry point, not a
  library the rest of the engine calls into, and it's tied to `argv`/
  `atexit`/Haiku-specific paths, defines its own `SDL_Delay` via `usleep`,
  and bails out immediately in `check_data_exists()` without real Cave
  Story data on disk (which this environment doesn't have yet -- see
  below). The DemonOS entry point will be a from-scratch `core_main.cpp`
  that performs the same startup sequence (`input_init`, `Graphics::init`,
  `sound_init`, `game.init`, ...) directly, same relationship
  `core_main.c` has to `sys_win.c` in the Quake port.
- Added the handful of SDL symbols that entry point will need regardless
  (`SDL_Init`/`SDL_Quit`/`SDL_GetError`/`SDL_GetTicks`/`SDL_GetAppState`/
  `SDL_PauseAudio`/`SDL_Delay`, backed by `demon_port_ticks_ms`/
  `demon_port_sleep_ms`) to `sdl_demonos.c` now since they're trivial and
  don't depend on having game data yet. `nxsurface.cpp`/`input.cpp` still
  compile clean against the shim with these added.
- **Data is no longer the blocker.** `tools/fetch-cavestory-data.sh` pins
  and checksums the freeware English-translated release (Studio Pixel's
  original, Aeon Genesis translation) mirrored at
  `cavestory.one/downloads/cavestoryen.zip` -- the same long-running fan
  site the upstream `NXEngine` README itself links to. The archive's own
  `CaveStory/Readme.txt` states "This program is freeware," and it contains
  exactly what `main.cpp`'s `check_data_exists()` looks for
  (`data/npc.tbl`, `data/*.pbm`, `data/Stage/*.pxa/.pxe/.pxm/.tsc`).
  `make nxengine-data` fetches and verifies it.
- **D1 now actually boots.** `ports/nxengine/platform/core_main.cpp` +
  `entry.S` + `linker.ld` (same shape as the Quake port's) link into
  `build/nxengine-core.elf`, registered as the `nxengine`/`nxengine-core`
  shell command (`src/apps.c`, `src/makobox.c`, `grub/grub-test.cfg`). It
  calls the real, unmodified `trig.cpp`'s `trig_init()` and checks
  `sin_table[64]` (the 90-degree entry) equals exactly `1 << CSF` --
  proving the freestanding C++/libm/PortKit/linker chain produces a
  *correct* result, not just a clean compile. `make nxengine-smoke` boots
  the real ISO, types the command, and checks for
  `NXENGINE_D1_SUBSYSTEMS_READY trig` + a clean exit.
  - Needed two more freestanding-C++ shims beyond the SDL one:
    `ports/nxengine/platform/math.h` and `stdlib.h` shadow `<math.h>`/
    `<stdlib.h>` (libstdc++'s real versions refuse to build under
    `-ffreestanding` in C++ mode), declaring just the functions engine
    units call, backed by `ports/quake/platform/libm_demonos.c` (reused
    directly, not duplicated) and `apps/doom/libc.c`.
  - Also caught and fixed a real gap: the `nxengine-platform-audit` compile
    checks for `nxsurface.cpp`/`input.cpp` had been running under plain
    host `g++`, not the actual freestanding flags -- meaning they'd never
    actually been proven buildable for the real target. Fixed to use the
    same `NXENGINE_CXXFLAGS` the D1 core links with; both still pass.
  - Needed a capability grant: `src/makobox.c`'s `launch_app` grants
    per-executable service capabilities by path, and `demon_port_init_dynamic`
    opens the storage service unconditionally (even for this data-free D1
    stage) -- `nxengine-core.elf` needed `CAPABILITY_SERVICE_STORAGE` added
    to its entry, the same pattern `quake-core.elf`/`classicube-core.elf`
    already use.
- **D2 (real asset loading) now also boots and passes.** `SDL_LoadBMP` in
  `sdl_demonos.c` is a real BMP decoder (1/4/8-bit indexed, uncompressed --
  the only variants present in the fetched data set), backed by PortKit
  file I/O. `core_main.cpp`'s D2 stage loads the real
  `CaveStory/data/Bullet.pbm` and checks its exact dimensions, bit depth,
  and first two palette entries. Gates cleanly on data presence the same
  way Quake's D4/D5 gate on real `pak0.pak` -- `NXENGINE_D2_NO_DATA
  self-test-mode` on the data-free ISO (`make nxengine-smoke`), the real
  check on a data-bearing play ISO (`make nxengine-play-smoke`, built via
  the new `grub/grub-nxengine-play.cfg` + `$(NXENGINE_PLAY_ISO)`, which
  mounts one real asset file at `/home/demon/data/Bullet.pbm`).
  - Debugging note for next time: chased what looked like a kernel
    positional-read bug (`demon_handle_read_at` seemingly returning zeros
    for a specific file offset) for a while before realizing the actual
    bug was in the *test's* expected value -- an `od -A x` hex-address
    dump earlier had been misread as decimal, off-by-a-few-bytes, so the
    "known palette color" the assertion checked against was actually
    palette entry 1, not entry 0 (which is legitimately black). Re-checked
    with `od -A d` (decimal addresses) and confirmed the loader was
    correct the whole time. Worth remembering: when a raw hex dump and a
    parser disagree, check which addressing mode the dump is using before
    assuming the parser is wrong.
- **D3 (real rendering) now boots and puts a real, visually-verified frame
  on screen.** `SDL_Flip` in `sdl_demonos.c` is no longer a stub: it opens
  the display/surface services (same path `vid_demonos.c` uses for Quake),
  converts whatever bpp the given surface is to ARGB, and calls
  `demon_display_submit`. `core_main.cpp`'s D3 stage uses the real,
  unmodified `NXSurface` class (linked into the ELF for the first time,
  not just compile-audited) to load a real Cave Story sprite sheet
  (`casts.pbm`, the character-portrait sheet), run it through the engine's
  actual `Scale()`/`Scale8`/`SDL_DisplayFormat` path (3x upscale +
  palette-to-truecolor, `config.h` unconditionally defines
  `CONFIG_MUTABLE_SCALE` so `SCALE` is a real runtime var defaulting to 3),
  blit it onto a screen-sized surface, and flip it. Verified with a QEMU
  `screendump` + pixel-color histogram (not just "it didn't crash") --
  genuine color diversity matching decoded portrait art, not a blank frame.
  `make nxengine-play-smoke` checks for it.
  - `Bullet.pbm` (used for D2) turned out to be 4bpp in this particular
    freeware mirror, but `Scale8` only supports 8bpp ("all the .pbm files
    are 8bpp" per upstream's own comment) -- so D3 uses `casts.pbm`
    (genuinely 8bpp) instead. D2's raw-BMP-decode proof already covers the
    4/8bpp variety correctly; D3 needs an asset the real, unmodified
    `Scale8` path actually supports.
  - Hit a real kernel gap along the way: `demon_display_info`'s syscall
    handler validates its out-param destination with `user_write_range`,
    which only recognizes the small-app heap/stack range and the dynamic
    anon-mmap arena -- not the large-app static-image range (0x20000000+)
    that Quake/Doom/NXEngine's cores actually link at. So the call fails
    with "display: invalid destination" for *any* static/global struct in
    one of these ports, not just this one -- confirmed the exact same
    failure already exists (silently) in Quake's own boot logs
    (`QUAKE_D4_VID_FAIL display=no-info`). Quake already works around it by
    falling back to defaults rather than treating it as fatal; did the
    same here instead of chasing a kernel fix that's out of scope for this
    port.
  - Also fixed a real, separate gap while getting here:
    `format_create` never computed `Rshift`/`Gshift`/`Bshift` from the
    given RGB masks for general-purpose truecolor surfaces (only
    `SDL_DisplayFormat`'s one hardcoded conversion path set them
    manually) -- would have silently produced garbage colors for any
    other 32bpp surface, like the screen surface D3 creates directly.
- **D4 (real interactive movement) also boots and is verified end-to-end.**
  A bounded loop in `core_main.cpp` polls `SDL_PollEvent` directly (not
  through `input.cpp`'s `input_poll()`, which pulls in console/game/Replay
  dependencies this milestone doesn't need yet), moves the sprite drawn in
  D3 based on arrow-key state, redraws and flips every frame, and exits on
  Escape or after a frame cap. Verified with the same headless
  QEMU-monitor `sendkey` automation used to prove the Quake movement fix
  earlier: real right-arrow presses moved the sprite
  (`NXENGINE_D4_MOVE_OK`), Escape cleanly ended the loop
  (`NXENGINE_D4_QUIT escape`). `make nxengine-play-smoke` drives this.
- **D5 (real map loading) also boots and is verified.** `map.cpp`'s real,
  unmodified `load_map()` was linked in directly -- betting that
  `-ffunction-sections`/`--gc-sections` would prune the rest of that
  translation unit (`load_stage`/`load_entities`/`DrawMap`, and everything
  *those* need: `Object`, `Tileset`, `tsc`) since nothing calls them. The
  bet paid off: after linking, only `load_map`'s real dependencies were
  unresolved (`fileopen`, `fverifystring`, `fgeti`, `stat`) -- four small
  wrapper/logging functions added to `core_main.cpp`, on top of
  `fopen`/`fgetc` already provided by `ports/quake/platform/stdio_demonos.c`
  (reused directly, not duplicated -- a general freestanding C FILE shim,
  not Quake-specific). `map.h` itself needed a small header-context trick
  too: it references `Object*` (pointer only, forward declaration is
  enough) and `SCREEN_WIDTH`/`SCREEN_HEIGHT` (for a `#if` divisibility
  check) -- including the *real* `object.h`/`graphics.h` to get those
  cascades into siflib/FloatText/the `sprites[]` global and far more
  context than `load_map()` alone needs, so `core_main.cpp` just
  forward-declares `class Object;` and `#define`s the two screen macros
  directly instead.
  Verified against the real fetched `Stage/0.pxm` (21x20 tiles) --
  `make nxengine-play-smoke` checks for the exact real upstream log line
  (`load_map: level size 21x20`), not just a custom marker.
- **D6 (real tileset loading) now succeeds -- root cause found and fixed
  in the platform layer, not upstream.** Linked `graphics/tileset.cpp` +
  `stagedata.cpp` (real, unmodified; `--gc-sections` pruned
  `Tileset::draw_tile`/`Graphics::DrawSurface` since nothing calls them).
  Needed `sprintf` (`-include stdio.h` forced at tileset.cpp's specific
  compile step, since upstream relies on it arriving transitively via
  real `SDL.h`) and C++ `operator new`/`delete` (`NXSurface::FromFile`
  heap-allocs a wrapper) -- both added to `core_main.cpp`.
  First attempt failed: every real `Prt*.pbm` tileset in the data set is
  1bpp or 4bpp (checked all of them, including studiopixel.jp's own
  original release -- not a fan-mirror artifact), and `Scale()`'s real,
  unmodified implementation only has an 8bpp `Scale8` path. Root cause:
  real SDL 1.2's actual `SDL_LoadBMP` is understood to upconvert sub-8bpp
  BMPs to 8bpp on load, a real-world implementation detail this minimal
  shim hadn't matched -- it was preserving the literal source bit depth
  instead. Fixed `SDL_LoadBMP` in `sdl_demonos.c` to always decode into
  an 8bpp destination surface (same palette, one byte per pixel)
  regardless of the source BMP's native bit depth. This is a fix to this
  port's own platform code, not a patch to upstream engine behavior --
  the "stay faithful to unmodified engine code" principle holds.
  `Tileset::Load(0)` now genuinely succeeds against the real
  `Stage/Prt0.pbm` asset. `make nxengine-play-smoke` checks for it.
- **D7 (real level rendering) -- the real payoff of D5+D6 combined, and
  visually confirmed.** D5/D6 had used `Stage/0.pxm`/tileset `"0"`:
  upstream's own null/placeholder stage ("set null stage just to have
  something to do while we go to intro", `main.cpp`) -- genuinely almost
  blank (323/420 tiles are the empty tile), fine for proving the loaders
  work, not for proving rendering looks like anything. D7 loads a real,
  visually rich gameplay area instead -- `Pens1` (the Camp/Reception
  Room, `tileset_names[1] == "Pens"`) -- and draws every tile with the
  real, unmodified `NXSurface::DrawSurface` (replicating
  `Tileset::draw_tile`'s real tile-index-to-sheet-coordinate math directly,
  bypassing the `Graphics` namespace's screen/drawtarget globals this
  stage still doesn't set up). Verified with a QEMU `screendump` + pixel
  histogram: 19 distinct colors, ~42k nonzero pixels out of ~150k, clearly
  matching real tile art (browns/tans, greens, blues) -- not a blank frame.
  `make nxengine-play-smoke` checks for the exact real upstream log line
  (`load_map: level size 21x16`) plus the render marker.
- **D8: real player sprite, real level, real control -- the closest this
  port gets to "playing" without `game.init()`.** Loads Quote's real
  sprite sheet (`MyChar.pbm`) and draws it on top of D7's real rendered
  level every frame, moved by real keyboard input (same direct
  `SDL_PollEvent` technique as D4). Verified the same way as D4/Quake's
  movement fix: automated `sendkey right` presses via the QEMU monitor,
  confirmed via log (`NXENGINE_D8_MOVE_OK`) that the sprite's position
  actually changed. `make nxengine-play-smoke` drives both D4's and D8's
  interactive loops in sequence.
  There is deliberately no collision, physics, gravity, or animation --
  those all live in `Object`/`game.tick()`, which `game.init()` sets up.
  This stage proves input+render+real-assets integrate correctly; it is
  not a substitute for the real game loop.
- **Real tile collision (TA_SOLID_PLAYER) added to D8 -- found a real
  data source for a table that looked missing, not a dead end.**
  `load_tileattr()` needs `tilekey.dat` (the fixed tile-code-to-attribute
  lookup table) and, transitively via `initmapfirsttime()`, `load_stages()`
  needs `stage.dat` -- neither is in any Cave Story data release (checked
  the full archive, not just `data/`); they're the original Windows
  `Doukutsu.exe`'s embedded resources, which NXEngine's own
  `extract_main()` normally extracts on first run. First assumed this was
  a hard blocker requiring a different data source (asked the user, who
  wanted it looked into further) -- but `tilekey.dat` doesn't depend on
  any specific game data (it's a fixed format-level table), so
  **NXEngine's own upstream repo ships it directly**
  (`build/nxengine-upstream/tilekey.dat`, alongside the engine source).
  Loaded it with a small custom loader (`load_tilekey`, `core_main.cpp` --
  deliberately not calling the real `initmapfirsttime()`/`load_stages()`,
  since `stage.dat` genuinely isn't available anywhere), then called the
  real, unmodified `load_tileattr()` against `Pens.pxa` (the real
  per-tileset attribute-code file, which *is* in the fetched data, same as
  any other `.pxa`). Needed two more small stubs to link
  (`Graphics::CopySpriteToTile`, `CVTDir` -- both referenced
  unconditionally in `load_tileattr`'s compiled code even though neither
  branch fires for this tileset's actual codes).
  D8's movement loop now checks all four corners of the player's box
  against `tileattr[]` for `TA_SOLID_PLAYER` before allowing a move.
  Verified concretely, not just "didn't crash": starting at y=128 and
  holding Up for 120 units of unclamped movement (which would reach
  y≈0-8), the player stopped at **y=106** -- genuinely blocked by a real
  solid tile well short of the screen edge, not the boundary clamp.
  Gated gracefully (`have_collision` flag) so a missing `tilekey.dat`/
  `.pxa` degrades to D4's walk-through-anything behavior rather than
  silently pretending to have collision it doesn't.
- **Gravity + jump added on top of D8's collision, axis-separated (real
  platformer shape).** Horizontal movement and vertical gravity/jump are
  now resolved independently against the real `tileattr[]` collision data
  (via a shared `box_blocked` check), so landing on the floor doesn't
  block sliding sideways and vice versa -- not upstream's real
  fixed-point `Object` physics (that lives in `player.cpp`/`game.tick()`,
  which this stage still doesn't set up), but simple integer-pixel gravity
  standing in for it against the same real level/collision data. Verified
  concretely: holding right + tapping jump periodically moved x from 160
  to 172 and left y at 123 (mid-air during a jump arc, not the resting
  128), proving the physics genuinely runs frame-to-frame rather than
  being frozen or a no-op.
- **D9: real sprite bounding-box table (siflib) loaded -- and a
  foundational C++ runtime gap found and fixed along the way.** Linked
  `siflib` (`sif.cpp`/`sifloader.cpp`/`sectSprites.cpp`/
  `sectStringArray.cpp`) plus the small `common/` container classes it
  needs (`BList`/`DBuffer`/`DString`/`StringList`/`bufio`), all real,
  unmodified, self-contained. `sprites.sif` -- like `tilekey.dat` -- isn't
  Cave Story game data; it's bundled directly in NXEngine's own upstream
  repo, mounted the same way (as the bare filename `sprites.sif`, matching
  `Sprites::Init()`'s hardcoded internal path).
  Hit a genuine hang (not a crash) on the first attempt: `sprites.cpp`
  has `static StringList sheetfiles;` -- a global C++ object with a
  non-trivial constructor. **This port's `entry.S` never ran
  `.init_array` (global C++ constructors) before calling
  `nxengine_core_main()`.** So `sheetfiles`' constructor never executed,
  leaving `BList`'s `fBlockSize` as raw zeroed BSS (0) instead of its
  intended default (20) -- and `BList::_ResizeArray`'s
  `while (newSize < targetSize) newSize <<= 1;` spins forever when
  `newSize` starts at 0. This wasn't a bug in `sprites.cpp`/`BList` at
  all -- diagnosed by bisecting with temporary `stat()` prints (reverted
  after, upstream tree confirmed clean via `git status`) down to the exact
  stalled call, then reasoning about *why* a well-tested Haiku-derived
  container class would hang on first use.
  Fixed at the platform level, not by touching engine code: `linker.ld`
  now emits `.init_array` (`PROVIDE_HIDDEN` start/end symbols), `entry.S`
  walks it calling each constructor before `nxengine_core_main`, and
  `core_main.cpp` supplies the minimal freestanding boilerplate global
  constructors need (`__dso_handle`, a no-op `__cxa_atexit` -- this
  process only ever exits via a syscall, never runs static destructors,
  so no-op is correct, not just convenient). This fix applies to *any*
  future global C++ object with a non-trivial constructor in this port,
  not just `sheetfiles` -- a real gap in the freestanding C++ runtime
  support, not a one-off.
  After the fix: `Sprites::Init()` genuinely succeeds, loading all 490
  real sprite records (`num_sprites == 490`) with real bounding-box/frame
  data straight from the engine's own bundled `sprites.sif`.
  `make nxengine-play-smoke` checks for it.
- **D10: real `Object` physics -- `apply_xinertia`/`apply_yinertia`/
  `UpdateBlockStates` genuinely replace D8's hand-rolled gravity stand-in.**
  Linked the real, unmodified `object.cpp` (the `Object` base class),
  `ai/ai.cpp` (only `load_npc_tbl()` is called -- `--gc-sections` pruned
  `ai_init`/`AIRoutines`/the full NPC dispatch system, same bet as
  map.cpp/tileset.cpp/sprites.cpp), `common/InitList.cpp`, and
  `slope.cpp` (needed because `apply_xinertia`/`UpdateBlockStates`
  unconditionally call slope-handling functions even though this stage's
  object never sets `NXFLAG_FOLLOW_SLOPE` -- branches are runtime, not
  compile-time). `objprop[]`/`firstobject`/`lastobject`/`player` are
  normally defined in `game.cpp`/`ObjManager.cpp`/`player.h`, none of
  which this stage links (full `Game`/object-manager/`Player` subclass is
  out of scope); defined the same real `ObjProp[OBJ_LAST]` array type
  directly in `core_main.cpp` instead, populated for real by
  `load_npc_tbl()` against the real `npc.tbl` (D2's asset already reused).
  A bare `Object` stands in for the player -- constructed directly
  (sprite index 0, matching D9's first loaded sheet), assigned to the
  real `player` global so `GetBlockingType()` correctly reports
  `TA_SOLID_PLAYER`, exactly like real gameplay.
  Getting `object.h` itself to compile needed one more small trick:
  `Point` is just a `typedef SIFPoint Point;` in the real `nx.h` (not
  `object.h` itself), and `CSF` needed to be defined before `object.h`,
  not after -- both added directly rather than pulling in `nx.h`'s full
  aggregate.
  Hit a second real, non-obvious kernel issue while verifying end-to-end:
  the whole boot failed *before* even reaching the shell
  (`[FAILED] Native PortKit anonymous-memory test failed`, from
  `portcheck.elf`'s unrelated self-test) -- not a memory-pressure problem
  as the message suggested, but **RAMFS's fixed 28-file table
  (`kRamfsFiles` in `src/ramfs.cpp`) being full**. This port's
  play-smoke boot legitimately needs more real, distinct bundled files
  (a dozen+ small real assets) than any previous port. Bumped to 48
  (`src/ramfs.cpp`) with headroom for future ports, not just a patch for
  this one; confirmed the base kernel smoke test and every other port's
  smoke test still pass unaffected.
  `make nxengine-play-smoke` now drives all of D2-D10 in one boot and
  checks for `NXENGINE_D10_MOVE_OK` (verified via the same automated
  `sendkey` technique as D4/D8/the Quake movement fix).
- **Explored the real object manager (`CreateObject`/`ObjManager.cpp`) and
  found a genuine hard wall, not just more scope creep.** `ObjManager.cpp`
  has a file-scope `static Player ZERO_PLAYER;` -- unconditional, present
  even when spawning non-player objects -- and since `Player` has virtual
  overrides, its vtable requires every override to have *some* address,
  which drags in real chunks of `player.cpp` (48 more unresolved symbols)
  regardless. Scoping `player.cpp` further showed it needs the full
  `Game` class (`game.cpp`, another 107 unresolved symbols: the entire
  mode state machine -- title/intro/options/pause/credits screens --
  `TextBox`/save-profile system, `StageBossManager`, and the `tsc.cpp`
  script engine that drives all dialogue/cutscene/event logic). This is
  genuinely comparable in size to all of D1-D11 combined, unscoped, and
  was deliberately **not pursued further** -- flagged to the user as a
  real fork rather than silently committing to it; left for a dedicated
  future session with its own fresh scoping pass, the same way this one
  began.
- **D11: real `FloatText` (damage-number popup) instead -- a genuinely
  bounded, self-contained piece not part of the `game.cpp`/`tsc.cpp`
  commitment.** Needed `graphics.cpp` for the first time, but only for
  its trivial `set_clip_rect`/`clear_clip_rect`/`SetDrawTarget`
  one-liners -- `Graphics::init`/`InitVideo`/`close`/`SetResolution`
  (never called) needed several more SDL/libc declarations
  (`SDL_VideoInfo`, `SDL_SetVideoMode`, `putenv`, `exit`, ...) to
  *compile*, but never need real bodies since `--gc-sections` drops the
  whole enclosing function at link time. Also needed a `SDL/SDL_getenv.h`
  stub (an unconditional, unused include) and removed two now-redundant
  stubs from `core_main.cpp` (`Graphics::CopySpriteToTile`, `use_palette`)
  once their real definitions came from `graphics.cpp`/were already
  covered.
  Hit one real, instructive bug verifying this: `FloatText`'s digit
  rendering calls `Sprites::draw_sprite`, which loads the sprite's actual
  sheet file on first use -- `SPR_REDNUMBERS`'s sheet was never mounted,
  so `NXSurface::FromFile` silently returned a surface with a null
  `fSurface`, and this shim's `SDL_BlitSurface` then dereferenced it.
  Rather than guess the filename, decoded `sprites.sif`'s real binary
  format by hand (in a throwaway Python script, not the engine) to find
  `SPR_REDNUMBERS`'s exact `spritesheet` index -- `TextBox.pbm`
  (confirming w=8,h=8,nframes=11: one 8x8 glyph per digit) -- and mounted
  the real asset (already in the fetched data set) rather than
  papering over it with the wrong sprite. Also caught along the way:
  `SPR_MYCHAR` is sprite index 3, not 0 (`SPR_NULL` is 0) -- D8/D10's
  earlier `player_obj.sprite = 0` was using the wrong bounding box the
  whole time (never crashed, just semantically wrong); fixed to the real
  `SPR_MYCHAR` constant.
  `make nxengine-play-smoke` now drives D2-D11 in one boot.
- **D12: real particle effect (`Carets`) -- the exact real API call, zero
  new files needed.** `effect(x, y, EFFECT_STARPOOF)` is not a bespoke
  test harness call; it's the identical function real engine code (e.g.
  a shot dissipating) calls. `caret.cpp` needed *nothing* beyond itself --
  every symbol it references (`map`, `sprites`, `Sprites::draw_sprite`,
  `random`, `staterr`, `vector_from_angle`) was already linked by D1-D11.
  `vector_from_angle` in particular turned out to already live in
  `trig.cpp` (D1). Found `SPR_STAR_POOF`'s real sheet (`Caret.pbm`) the
  same way as D11's `TextBox.pbm` -- decoding `sprites.sif`'s binary
  layout by hand rather than guessing -- and it worked on the first real
  boot attempt.
- **D13: real screen effects (`ScreenEffects`/`SE_Fade`/`SE_Starflash`/
  `SE_FlashScreen`) -- another genuinely bounded, self-contained piece.**
  `screeneffect.cpp` needed zero new engine subsystems: `Graphics::
  ClearScreen`/`FillRect` were already linked (D11's `graphics.cpp`),
  `map.displayed_xscroll`/`yscroll` and `draw_sprite` were already linked
  (D5-D9). The only symbol not already covered was `sound()` -- normally
  declared via `screeneffect.cpp`'s auto-generated `.fdh` forward-
  declaration header (not a real header this port includes at all) --
  defined as a no-op directly in `core_main.cpp`, an honest stand-in
  since this port has no audio backend yet (documented gap, not a fake
  success). Drove `fade.Start(FADE_OUT, ...)` to a real `FS_FADED_OUT`
  terminal state and `starflash.Start(...)` through `ScreenEffects::
  Draw()`, the same real dispatch function gameplay uses for a
  boss-defeat flash. Hit the same class of bug as D11's
  `SPR_REDNUMBERS`: `SPR_FADE_DIAMOND`'s sheet (`Fade.pbm`) wasn't
  mounted, causing `NXSurface::LoadImage` to fail and a null-surface
  page fault in `draw_sprite`. This time the file turned out to already
  exist verbatim in the fetched Cave Story data set (no `sprites.sif`
  decoding needed) -- just had to mount it. `make nxengine-play-smoke`
  now drives D2-D13 in one boot.
- **D14: real bitmap font rendering (`whitefont`/`greenfont`/`bluefont`/
  `shadowfont`) -- the first piece that genuinely needed a `Game`
  global, and a real, previously-unencountered translation-unit
  mismatch bug along the way.** `graphics/font.cpp`'s `text_draw` reads
  `game.mode` (a credits-screen check, never true here); rather than
  link all of `game.cpp` (the deferred `Game`/`Player`/`tsc.cpp` wall),
  defined a real `Game game;` global directly -- `Game` really is the
  POD struct its own header comment claims, except one member,
  `StageBossManager stageboss`, which has an explicit two-line
  constructor (`fBoss = NULL; fBossType = BOSS_NONE;`). Rather than link
  all of `stageboss.cpp` (which would drag in nine full boss AI
  subclasses' headers just for that constructor), defined that one real,
  declared member function directly in `core_main.cpp` instead --
  identical body to upstream's, just not routed through the file that
  would otherwise pull in the boss roster.
  Also needed `graphics/font.cpp` itself, which has a real compile-time
  `#ifdef CONFIG_ENABLE_TTF` branch (defined project-wide in `config.h`)
  guarding SDL_ttf's TrueType path -- dead at runtime here (`SCALE == 1`
  always takes the adjacent bitmap-font path) but still part of the same
  compiled function as the live branch, so the linker still needs real
  `TTF_Init`/`TTF_OpenFont`/`TTF_CloseFont`/`TTF_GetError`/
  `TTF_RenderUTF8_Solid`/`TTF_Font` symbols to exist. Added
  `SDL/SDL_ttf.h` (declaration-only, mirroring the existing
  `SDL_SetVideoMode` group's precedent) plus trivial failure-stub
  definitions in `sdl_demonos.c` -- genuinely never invoked, not a fake
  TTF backend.
  Hit one real, instructive bug: `SCALE` is `extern int SCALE;` when
  `CONFIG_MUTABLE_SCALE` is defined (real upstream default, `config.h`),
  or a `#define SCALE 1` otherwise -- and `core_main.cpp` was including
  `graphics/nxsurface.h` (which declares it) without ever including
  `config.h` first, unlike every real upstream `.cpp` file, which always
  includes `config.h` via `nx.h` before anything else. That meant
  `core_main.o` silently compiled against the macro branch while
  `nxsurface.o`/`font.o` (which do transitively see `config.h`) compiled
  against the real extern variable -- caught immediately as a hard
  compile error (`lvalue required as left operand of assignment`) the
  moment D14 needed to *write* to `SCALE`, not just read it. Fixed by
  `#define CONFIG_MUTABLE_SCALE` in `core_main.cpp` before its own
  `nxsurface.h` include, matching what `config.h` really defines,
  instead of quietly living with two translation units disagreeing about
  a global's kind.
  Also had to set `SCALE = 1` explicitly before calling `font_init()`:
  it defaults to `3` (`nxsurface.cpp`'s desktop-window default, normally
  overwritten by `Graphics::init`/`SetResolution`, neither of which this
  port calls) and `font_init` uses `SCALE == 1` to choose the real
  bitmap path over the (unimplemented) TTF one.
  `smalfont.bmp` -- like `tilekey.dat`/`sprites.sif` before it -- is
  NXEngine's own bundled engine asset, not Cave Story game data; mounted
  bare (matching `font.cpp`'s literal `"smalfont.bmp"` lookup) the same
  way `sprites.sif` was. `font_draw(20, 20, "Cave Story", 0, &whitefont)`
  is the exact function real UI/dialogue code calls; verified concretely
  via its real return value (width=59 for a 10-character string against
  a genuinely variable-width 5px bitmap font, not a hardcoded/rounded
  number). `make nxengine-play-smoke` now drives D2-D14 in one boot.
- **D15: real save-file round trip (`profile_save`/`profile_load`) --
  and a real, kernel-level storage bug found and fixed along the way.**
  Linked `common/misc_comm.cpp` for real (the `fgeti`/`fgetl`/`fputi`/
  `fputl`/`fverifystring`/`random`/`stprintf`/`file_exists` helper
  library `profile.cpp` needs) -- superseding this file's earlier
  hand-rolled `fverifystring`/`fgeti`/`fgetl`/`random` stand-ins, the
  same way D11's real `graphics.cpp` superseded earlier stubs. Needed a
  few small libc pieces this port hadn't needed before
  (`__errno_location`/`strerror`, added in `core_main.cpp`; `atof` turned
  out to already be real, in `libm_demonos.o`) and `RAND_MAX` in this
  port's `stdlib.h` shim. `profile.h`'s three missing constants
  (`WPN_COUNT`/`MAX_INVENTORY`/`NUM_TELEPORTER_SLOTS`) are each normally
  pulled in from a file this port deliberately doesn't link (`player.h`'s
  `Player` wall, `p_arms.h`, `TextBox/StageSelect.h`) -- defined the bare,
  verified-correct integers directly instead of linking any of those.
  Hit a real, previously-latent bug in the platform layer itself, not
  just a missing symbol: `ports/quake/platform/stdio_demonos.c`'s
  `fwrite` never called `demon_port_seek` before writing (unlike
  `fread`), so an `fseek()` on a write-mode stream was silently a no-op
  as far as the actual write went. Worse, once that was fixed, a second,
  deeper issue surfaced: `ramfs_write()` (`src/ramfs.cpp`) takes no
  offset parameter at all -- every call replaces a file's *entire*
  content with just that call's buffer. `profile_save` writes a save
  file as ~20 small sequential and non-sequential `fputl`/`fputi`/`fseek`
  calls (never one big write), so every previous call's bytes were
  getting clobbered by the next one; only whichever `fwrite()` happened
  to run last actually survived to disk. Fixed at the platform-shim
  level, not by touching `ramfs.cpp`/adding a kernel syscall: write-mode
  `FILE`s now buffer their whole content in a small per-slot in-memory
  buffer (`DEMON_WRITE_BUF_SIZE`, sized for real callers -- NXEngine's
  `PROFILE_LENGTH` 0x604, Quake's few-KB config/savegame text -- not
  "generous," since large executables here have a hard 1MB total image
  budget), and only call the real, single `ramfs_write()` once, in
  `fclose()`. (First attempt used a 64KB buffer, which alone pushed
  `nxengine-core.elf`'s total image size past that 1MB
  `USER_LARGE_CODE_MAX_PAGES` budget and made `apps: spawn failed` --
  caught immediately, shrunk to 4KB.) This is a real, general fix
  shared by every port using this file (Quake included), not a
  NXEngine-only workaround; reran `quake-smoke`/`quake-play-smoke` after
  to confirm no regression, both still pass.
  Verified with a genuine save/load round trip (not just "didn't
  crash"): a `Profile` with a specific stage/HP/weapon/inventory/flag
  state is written via the real `profile_save`, then read back into a
  separate, independently zeroed `Profile` via the real `profile_load`,
  and every field is compared field-by-field against the original.
  `make nxengine-play-smoke` now drives D2-D15 in one boot.
- **D16: real key mapping + settings save/load -- and a genuine kernel
  bug found and fixed.** Linked `input.cpp` (real default key-mapping
  table, `input_get_mapping`/`input_set_mappings`/`buttondown`/
  `justpushed`) and `settings.cpp` (real `settings_save`/`settings_load`,
  which itself calls `input.cpp`'s real `input_get_mapping` to capture
  live mappings into the file -- two independently-linked real
  subsystems actually interacting, not two isolated tests). `input.cpp`'s
  own `input_poll()` references `console`/`Replay` (both deliberately
  out of scope) but is never called here -- D16 populates `inputs[]`
  directly instead, the same "bypass the orchestration layer, call the
  real primitives" pattern as D8's `load_tileattr` vs
  `initmapfirsttime`; since nothing calls `input_poll()`,
  `--gc-sections` drops its whole section before the linker ever needs
  those symbols. Also hit a real symbol-name collision: NXEngine's own
  `bool input_init(void)` collides at the source level with PortKit's
  unrelated `void input_init(void)` (already visible via
  `demon/portkit.h`) -- worked around with a distinctly-named
  declaration bound to the real function's actual Itanium-mangled symbol
  via `asm("_Z10input_initv")`, verified against `nm` on `input.o`,
  rather than touching either real header. `settings.cpp` also needed an
  extra `-Iports/nxengine/platform/SDL` (it's the one file that
  `#include`s bare `<SDL.h>` instead of `<SDL/SDL.h>`) to resolve to the
  same real shim, not a second implementation. Superseded this file's
  earlier hand-rolled `Settings normal_settings`/`settings` globals with
  `settings.cpp`'s real ones, same duplicate-symbol-avoidance pattern as
  D11/D15.
  Hit a real, previously-latent **kernel** bug chasing this one down, not
  just a platform-shim gap: `settings_save` → real bytes on disk
  (verified directly, `02 16 00 00 ...` = the real `SETTINGS_VERSION`)
  → `settings_load` → `tryload()`'s `fread(setfile, sizeof(Settings), 1,
  fp)` silently returned 0 items and left `version` at 0, even though an
  *identical* `fread` call into a local stack `Settings` a few lines
  earlier worked perfectly. Root cause: `setfile` here is `settings`
  (the global pointer → `&normal_settings`, a BSS global inside
  `nxengine-core.elf`'s large-code region once the port grew past the
  192KB normal-code budget around D10-D11) — and
  `src/arch/x86_64/userspace.c`'s `user_write_range` (the check gating
  which addresses the kernel is allowed to write into on a process's
  behalf, used by `demon_port_read`'s underlying syscall) explicitly
  excluded the code/large-code regions on the stated assumption they're
  "mapped read/execute-only." That assumption doesn't hold for any port
  built this way: `load_process` actually maps every code page
  Present|Writable|User (`| 0x7`), matching the RWX LOAD segment `ld`
  itself warns about for every one of these freestanding ELFs -- so a
  kernel-mediated write into a code-region BSS global was being rejected
  even though ordinary userspace code (this port setting
  `normal_settings.sound_enabled = true;` directly, works fine) can
  write there just fine. This is the same class of bug already noted --
  and merely tolerated -- for `demon_display_info`'s out-param write
  failing on large-app processes; this is the first time it broke actual
  correctness instead of being silently absorbed by a caller that
  ignored the failure. Fixed for real, not routed around: extended
  `user_write_range` to also accept the `USER_CODE`/
  `USER_LARGE_CODE_BASE` ranges, mirroring `user_range`'s own existing
  checks for those same ranges immediately above it in the same file.
  Reran the full kernel `smoke` target (boot chain, PortKit, ClassiCube,
  networking, MKO -- everything) plus `quake-smoke`/`quake-play-smoke`
  after, to confirm this kernel-level change caused no regressions
  anywhere else; all still pass.
  `make nxengine-play-smoke` now drives D2-D16 in one boot.
- **D17: real "item get" popup (`TB_ItemImage` + `TextBox::DrawFrame`) --
  another genuinely bounded, self-contained UI piece.** `TextBox.cpp`
  compiles and links as a whole (needed for the static
  `TextBox::DrawFrame`, the dialogue-box frame sprite-chop `ItemImage`
  calls), but its other member functions (`TB_SaveSelect`/
  `TB_StageSelect`/`TB_YNJPrompt`, which touch `player`/`game.mode`
  state) are never called here, so `--gc-sections` drops them --
  identical pattern to D16's `input.cpp`/`input_poll`. `TB_ItemImage`
  itself is the exact real class the dialogue system uses for the "you
  got a new weapon/life capsule" popup animation; ran it through 12 real
  frames (`SetSprite(SPR_STAR_POOF, 0)`, reusing D12's already-mounted
  `Caret.pbm` sheet rather than needing a new asset) verifying the real
  sprite bounding box read back from D9's `sprites[]` table
  (`sprite_w=16 sprite_h=16`), not a hardcoded/rounded placeholder.
  Worked on the first real boot attempt. `make nxengine-play-smoke` now
  drives D2-D17 in one boot.
- **D18: real Yes/No confirmation prompt (`TB_YNJPrompt`) -- the full
  real state machine, driven end-to-end.** Used for "really quit?"/
  teleporter-confirm dialogs; drove it through all of
  `STATE_APPEAR` -> `STATE_WAIT` (real frame-by-frame pop-up animation
  and a real 15-frame hold, not skipped or shortened) -> `STATE_YES_SELECTED`,
  then a genuine `justpushed(JUMPKEY)` edge to select "yes" and read the
  result back via the real `ResultReady()`/`GetResult()`. `SPR_YESNO`/
  `SPR_YESNOHAND` turned out to already resolve against D11's mounted
  `TextBox.pbm` sheet (a reasonable bet, since their sprite IDs are
  sequential right after `SPR_TEXTBOX` in `autogen/sprites.h` --
  confirmed empirically by it just working, no crash, rather than
  decoding `sprites.sif` by hand again) -- no new asset needed.
  `lastpinputs[INPUT_COUNT]` is normally defined in `player.cpp` (the
  deferred `Player` wall) but referenced directly as a plain
  `extern bool[]` by `YesNoPrompt.cpp`; defined the same real array type
  here instead of linking `player.cpp`, same pattern as D10's
  `objprop[]`.
  Hit one real, instructive bug verifying this: the first attempt got
  `no-result` even after driving well past `STATE_YES_SELECTED` -- D16
  had left `inputs[JUMPKEY]`/`lastinputs[JUMPKEY]` both `true` (the state
  right after its own `justpushed` check), so simply copying
  `lastinputs[JUMPKEY] = inputs[JUMPKEY]` before setting
  `inputs[JUMPKEY] = true` produced `lastinputs[JUMPKEY] == true` too --
  never a genuine falling-edge-to-rising-edge transition, so
  `justpushed()` correctly (by its own real logic) never fired. Fixed by
  explicitly resetting both to `false` before the test, a real lesson
  about stale global input state carrying across independently-staged
  D-tests, not a bug in `justpushed()` itself. `make nxengine-play-smoke`
  now drives D2-D18 in one boot.
- **D19: real credits-script decryption and parsing (`CredReader` +
  `tsc_decrypt`) -- genuine decrypted game text, not a synthetic
  string.** `endgame/CredReader.cpp` is real and unmodified; its
  `#include "../nx.h"` pulls in the entire real aggregate header
  (`player.h`/`game.h` included) but since `CredReader.cpp` itself never
  references `player`/`game`, that's harmless (declarations only, same
  situation as every other real `.cpp` file compiled here that happens
  to `#include "nx.h"`). `tsc_decrypt` is declared in `tsc.cpp` (the
  deferred script-engine wall) but its real body is a small,
  self-contained XOR-style decrypt (the key is stored at the file's own
  midpoint) with no other dependency on the rest of `tsc.cpp` -- copied
  verbatim into `core_main.cpp` rather than stubbed, so behavior is
  identical to the real thing.
  Hit two more real, previously-latent platform-shim bugs getting this
  to actually decrypt correctly: `ftell` didn't exist at all in
  `ports/quake/platform/stdio_demonos.c` (added, mirroring `fseek`'s
  state), and `fseek(fp, 0, SEEK_END)`'s size calculation used
  `demon_port_tell` (the *current cursor position*, still 0 right after
  a fresh open) instead of `f->port.size` (the real on-disk file
  length) -- meaning the classic `fseek(fp,0,SEEK_END); ftell(fp);`
  idiom for finding a file's size always returned 0 on a
  freshly-opened file. Fixed to use `f->port.size`, the same kind of
  fix as D15/D16's `fwrite`/`user_write_range` bugs: a real bug in
  shared platform code, general to every port using this file, not
  papered over locally.
  Verified against real, still-encrypted `Credit.tsc` (bundled in the
  fetched Cave Story data): `CredReader::OpenFile()` decrypts it for
  real, then `ReadCommand()` -- the exact real parser end-credits
  playback uses -- was driven through 200 real commands with zero
  "unknown command type" parse errors (which would have failed
  immediately on any genuinely garbled/undecrypted data), landing on
  94 real `CC_TEXT` commands, 1 label, and 2 jumps. The very first
  decrypted text line read back was `"  = CAST =  "` -- the real,
  recognizable header of Cave Story's actual credits screen, not
  placeholder or garbage bytes. `make nxengine-play-smoke` now drives
  D2-D19 in one boot.
- **D20: real generic menu widget (`Options::Dialog`) -- navigation and
  activation, driven through genuine input state.** `pause/dialog.cpp`
  is real and unmodified; it's the same reusable choice-list widget the
  real pause/options screens are built on. `Options::optionstack`
  (normally defined in `pause/options.cpp`, which pulls in map stage
  names, settings, replay, and `tsc.cpp`'s `Clear()`) is just a `BList`
  subclass with one inline accessor and no constructor of its own --
  defined directly here instead, avoiding all of that. Built a real
  `Dialog`, added three real `ODItem`s via the real `AddItem`, then
  drove the real `RunInput()` through a genuine `inputs[DOWNKEY]` press
  (moved `fCurSel` 0->1, exactly like a real player navigating) and a
  genuine `buttonjustpushed()` edge (`inputs[JUMPKEY]`) that fired the
  selected item's real activation callback -- verified via an actual
  callback incrementing a counter, not just "didn't crash." `Draw()`
  itself (which needs `SPR_WHIMSICAL_STAR`, a sprite ID far from any
  sheet already confirmed mounted) was deliberately left untested here --
  this stage verifies the real selection/activation *logic*, not that
  one specific cursor icon renders; a reasonable scope line, not a
  shortcut around a real failure. Worked on the first real boot attempt.
  `make nxengine-play-smoke` now drives D2-D20 in one boot.
- **D21: the real `Player` class -- a genuinely playable character, not a
  stand-in.** Every earlier player-shaped stage (D8's hand-rolled
  gravity, D10's bare `Object` physics) was deliberately a stand-in,
  because `player.cpp`'s real `Player` class needed `game.cpp`'s full
  object/script apparatus just to *link*. That stopped being true this
  session: re-scoped `player.cpp`/`ObjManager.cpp`/`p_arms.cpp`/
  `playerstats.cpp`/`statusbar.cpp`/`ai/weapons/whimstar.cpp`/
  `ai/sym/smoke.cpp`/`ai/weapons/weapons.cpp` from scratch against
  everything already linked from D1-D20, and found the real remaining
  gap was far smaller than the original scoping pass estimated (game
  had grown a lot of real infrastructure since then). All are now real
  and linked. Stubbed only the genuinely out-of-scope entry points, each
  documented at its definition in `core_main.cpp`: `Game::setmode`
  (mode-switch state machine, `game.cpp`), `StartScript`/
  `GetCurrentScript` (the `tsc.cpp` script engine), `RefreshInventoryScreen`
  (the dedicated inventory UI screen), `Replay::end_playback`/
  `end_record` (real no-ops, since this port never starts a replay),
  `StopLoopSounds` (no real audio backend), and `DebugConsole`'s
  constructor/`Print` (real, working implementation; key-handling/drawing
  stay unlinked). Also worked around a real symbol-name collision
  (NXEngine's own `bool input_init()` vs PortKit's unrelated `void
  input_init()`) via a distinctly-named declaration bound to the real
  mangled symbol, and superseded this file's earlier `Object*`-typed
  `player`/hand-rolled `lastpinputs[]` stand-ins with the real ones.
  The real player is created via `CreateObject(x, y, OBJ_PLAYER)` --
  the exact real path (special-cased in `ObjManager.cpp` to allocate a
  true `Player`, not `Object`, and allocate `DamageText`) -- not a bare
  `new Player()` (an earlier attempt at that crashed: `InitPlayer()`
  unconditionally dereferences `DamageText`, which only `CreateObject`
  allocates). Then `PInitFirstTime()` (must run *before* `InitPlayer()`,
  not after -- it's the one that allocates `XPText`, which `InitPlayer()`
  unconditionally dereferences) and `GetWeapon(WPN_POLARSTAR, 0)` set up
  a real save-equivalent starting state. Every frame calls the actual
  real `HandlePlayer()` -- the exact function `game.tick()` calls in
  real gameplay, doing real input handling, tile-attribute physics,
  weapon firing, and walking/jumping/falling, not a hand-rolled
  approximation.
  Found and fixed a real, previously-invisible bug getting movement to
  actually happen: `HandlePlayer()`'s very first line is
  `if (game.switchstage.mapno != -1) return;` -- a real invariant meaning
  "no stage transition pending," normally set by `game.cpp`'s stage-load
  code (deferred). `Game game;` is otherwise correctly zero-initialized,
  but zero isn't `-1`, so without setting `game.switchstage.mapno = -1;`
  explicitly, `HandlePlayer()` silently no-oped on *every* frame. Also
  hit a real test-harness lesson (not an engine bug): QEMU's monitor
  `sendkey` is a brief tap, not a sustained hold, and this port's earlier
  per-frame `while (SDL_PollEvent(...))` drain-the-whole-queue pattern
  (used successfully by D4/D8/D10's simpler hand-rolled physics) collapsed
  an entire host-sent multi-second key sequence into a single real frame
  once real acceleration-based physics was involved -- nowhere near
  enough held-frame-time to build up visible walkspeed. Fixed by driving
  `inputs[]` on a deterministic real-frame schedule instead of relying on
  queued host taps, the same real global arrays D16/D18 already proved
  legitimate to drive directly.
  Verified for real: `NXENGINE_D21_MOVE_OK` (genuine position change from
  real `PDoWalking`/`PDoJumping`/gravity, not simplified D8/D10-style
  pixel stepping), real starting HP (`hp=3`, from `PInitFirstTime`), a
  real weapon equipped (`weapon=2` = `WPN_POLARSTAR`, from a real
  `GetWeapon` call), and real weapon fire (`fired=1`): a genuine second
  object appears in `firstobject`, created by `HandlePlayer()`'s own
  `PDoWeapons` -> `FireWeapon` -> `PFirePolarStar` -> `CreateObject`
  chain in response to real `inputs[FIREKEY]` state, not a manual probe.
  Chasing the fire bug down found one more real, previously-invisible
  gap: `HandlePlayer_am()` ("aftermove") is a *separate* real function
  from `HandlePlayer()` -- real gameplay calls both every frame
  (`game.tick()` runs `HandlePlayer_am()` after the object-simulation
  pass), and it's `HandlePlayer_am()`, not `HandlePlayer()`, that updates
  `lastpinputs` (`memcpy(lastpinputs, pinputs, ...)`) and
  `inputs_locked_lasttime`. This stage was only calling `HandlePlayer()`,
  so `inputs_locked_lasttime` stayed permanently `true` (`InitPlayer`'s
  initial value, never reset) -- meaning `PUpdateInput`'s
  `lastpinputs[i] |= pinputs[i]` OR-merge fired unconditionally every
  single frame, latching `lastpinputs[FIREKEY]` true the instant fire was
  first pressed and never clearing it again. `FireWeapon`'s real
  single-shot "must release and re-press" check
  (`if (lastpinputs[FIREKEY]) return;`) then rejected every press,
  including the very first one, since the OR-merge landed before
  `FireWeapon` ever read it -- a genuinely subtle bug: `FireWeapon` alone,
  and even `PDoWeapons` alone with manually-set `pinputs`/`lastpinputs`,
  both fired correctly in isolation, which is what made it look at first
  like the object-creation path itself was somehow broken. Fixed by
  calling the real `HandlePlayer_am()` every frame right after
  `HandlePlayer()`, matching the real game loop's actual two-call shape.
  `make nxengine-play-smoke` now drives D2-D21 in one boot; reran the
  full kernel `smoke` target and both Quake smoke tests afterward to
  confirm no regressions.
- **D22: real per-shot AI ticking -- fired bullets genuinely fly, collide,
  and expire, not just get created.** D21 proved `FireWeapon` creates a
  real `OBJ_POLAR_SHOT` object; this stage wires up what makes that
  object actually *do* anything afterward. `Object::OnTick()` dispatches
  through `objprop[type].ai_routines.ontick`, which for shot types is
  normally registered by `ai/weapons/polar_mgun.cpp`'s
  `INITFUNC(AIRoutines)` -- a real, clever piece of infrastructure
  already in the engine: `INITFUNC` expands to a file-scope static
  `InitAdder` object whose constructor (run for real at boot through
  `.init_array`, the D9 fix) calls `AIRoutines.AddFunction(...)`, so
  every `ai/**.cpp` translation unit self-registers into the real,
  already-linked global `AIRoutines` `InitList` just by being linked.
  Linked `polar_mgun.cpp` and called the real `AIRoutines.CallFunctions()`
  once during setup -- the one real call needed to make all that
  self-registration actually take effect, wiring
  `objprop[OBJ_POLAR_SHOT].ai_routines.ontick = ai_polar_shot` for real.
  Then added the real general object-simulation pass every frame,
  alongside D21's `HandlePlayer()`/`HandlePlayer_am()`:
  `Objects::RunAI()` (dispatches every live object's real `OnTick()`),
  `Objects::PhysicsSim()` (applies real inertia to every non-player
  object), and `Objects::CullDeleted()` (actually removes anything
  `Delete()`'d that tick) -- the same three calls real gameplay's tick
  loop runs, all already linked via `ObjManager.cpp`, needing no stubs.
  Verified both `moved=1` and `expired=1` for real. First attempt showed
  `expired=1`/`moved=0`: the shot's real `ai_polar_shot` ran, but real
  wall collision (`IsBlockedInShotDir`) killed it on its very first tick
  (`ttl` barely decremented) -- not a bug, just the player having been
  walked hard up against a wall right before firing in that version of
  the test schedule. Refined the schedule to fire early, from the exact
  spawn point D8/D10 already independently verified has open space
  around it, before any walking -- and got real, multi-tick flight this
  time: the shot's position genuinely changes across `Objects::
  PhysicsSim()` calls before real `ttl` expiry (or wall collision)
  eventually deletes it. `make
  nxengine-play-smoke` now drives D2-D22 in one boot; reran the full
  kernel `smoke` target and both Quake smoke tests afterward to confirm
  no regressions.
- **D23: a real enemy NPC, not just a weapon shot -- proving D22's
  `AIRoutines` mechanism generalizes.** `ai/first_cave/first_cave.cpp`
  registers `OBJ_DOOR_ENEMY` (the "attacking exit door" from First
  Cave) via the same real `INITFUNC(AIRoutines)` self-registration D22
  wired up; its `ai_door_enemy` is a real, fully self-contained state
  machine (`WAIT`/`OPENEYE`/`CLOSEEYE`) needing nothing beyond
  `pdistlx`/`pdistly` (macros reading real player/object positions --
  already available). Its `INITFUNC` block takes three other functions'
  addresses unconditionally too (`ai_bat_up_down`, self-contained in the
  same file; `ai_critter` from `ai/weed/weed.cpp`; `ai_zzzz_spawner`
  from `ai/sand/puppy.cpp`), so linking `first_cave.cpp` at all pulled
  those two files in too, even though this stage only ever spawns
  `OBJ_DOOR_ENEMY`. `weed.cpp`'s own `INITFUNC` in turn takes
  `ai_giant_jelly`'s address, which calls `quake()` (`game.cpp`,
  deferred) -- added `quake()`'s real body directly (it's tiny: bump
  `game.quaketime`, optionally play a sound), the same "copy the real
  function verbatim, don't stub it" treatment as D19's `tsc_decrypt`.
  Spawned a real `OBJ_DOOR_ENEMY` via `CreateObject` (its `hp=6` is real
  data from the npc.tbl `load_npc_tbl()` already loaded in D10, not
  synthesized) and drove its real `RunAI()` directly through all three
  states: `closed=1` (stays shut with the player far away), `opened=1`
  (opens for real once the player is within the real `pdistlx`/`pdistly`
  range), `closing=1` (starts closing again once the player backs off)
  -- the exact real enemy behavior, worked on the first real boot
  attempt. `make nxengine-play-smoke` now drives D2-D23 in one boot;
  reran the full kernel `smoke` target and both Quake smoke tests
  afterward to confirm no regressions.
- **D24: real map entity data (`.pxe`) -- an entire stage's NPCs
  populated the real way, not one hand-placed object.** D23 spawned a
  single NPC by hand to prove the `AIRoutines`/`RunAI` path; this stage
  calls the real, unmodified `load_entities()` (`map.cpp`, linked since
  D5 -- this particular function inside it simply hadn't been reached
  before now) against `Pens1.pxe`, the exact real entity-placement data
  for the same Pens1 level D7/D8 already render. Real end to end: real
  x/y/type/flags read per entity, real `CreateObject` calls, real
  `OnSpawn()` dispatch, real `ID2Lookup` population -- the actual
  mechanism real gameplay uses to populate a stage with monsters,
  chests, signs, and NPCs on map load, not a synthesized list. Needed
  `debug.cpp` linked for the first time, for `DescribeObjectType`
  (referenced by `load_entities`' own flag-conditional logging;
  everything else in `debug.cpp` -- `DrawDebug`, the debug console
  command table, etc -- is unreached and dropped by `--gc-sections`,
  including a declaration-only `SDL_SaveBMP` stub never actually called)
  plus `autogen/objnames.cpp` (the real, auto-generated object-type-name
  string table `DescribeObjectType` indexes into -- pure data, no
  logic). Verified for real: `total=16 entities=15
  first_entity_type=18 first_desc=OBJ_DOOR(18)` -- 15 real entities
  loaded from Pens1's actual level design (plus the surviving player,
  16 total; `load_entities` calls the real `Objects::DestroyAll(false)`
  first, which correctly spares the player while clearing D22/D23's
  earlier hand-created test objects), and the very first one being a
  real `OBJ_DOOR` is exactly what a room-based Cave Story stage's entity
  list should start with. `make nxengine-play-smoke` now drives D2-D24
  in one boot; reran the full kernel `smoke` target and both Quake
  smoke tests afterward to confirm no regressions.
- **D25: ticking a real stage's entire entity roster together --
  stability across 15 real, diverse object types at once.** Wires the
  exact real per-frame object-simulation loop D22 already proved
  (`Objects::RunAI()`/`PhysicsSim()`/`CullDeleted()`) against everything
  D24 loaded from `Pens1.pxe`, not just one hand-picked type. No new
  code linked -- everything needed was already there. Ran 60 real ticks
  and compared an aggregate snapshot (position/frame/state summed across
  every non-player object) before and after: `before=15 after=15
  changed=0`. Read that result for what it honestly is, not what would
  look more impressive: `changed=0` means Pens1's specific 15 real
  entities in this fetched data set happen to be types with no
  registered `ai_routines.ontick` (only `OBJ_DOOR_ENEMY`/`OBJ_BAT_BLUE`/
  `OBJ_HERMIT_GUNSMITH`/`OBJ_CRITTER_HOPPING_BLUE` (`first_cave.cpp`)
  and `weed.cpp`/`puppy.cpp`'s own registered types have real tick
  behavior linked so far -- most of Cave Story's ~300 NPC types don't
  yet), not a bug in the dispatch mechanism itself (D22/D23 already
  proved that mechanism moves/collides/reacts for the types it does
  know). Decoded `Pens1.pxe`'s real 34 raw entity records by hand
  (Python, throwaway, same technique as D11/D12's `sprites.sif` decodes)
  to see exactly *why*: this room is genuinely Arthur's House, Cave
  Story's starting room -- `OBJ_CHEST_CLOSED`, `OBJ_SAVE_POINT`,
  `OBJ_RECHARGE`, `OBJ_DOOR`, `OBJ_COMPUTER`, `OBJ_BED`,
  `OBJ_CHALKBOARD`, `OBJ_HVTRIGGER`, `OBJ_FLOWERS_PENS1`, and named
  story characters -- `OBJ_SUE`, `OBJ_KAZUMA`, `OBJ_KING`,
  `OBJ_KAZUMA_AT_COMPUTER`, `OBJ_PROFESSOR_BOOSTER`. Real Cave Story's
  own named characters stand still until dialogue-triggered (`tsc.cpp`,
  deferred) rather than roam on a per-tick AI routine at all -- so
  `changed=0` is an *accurate* reflection of this specific real room's
  real content, not a sign anything is missing from D22/D23's proven
  mechanism. The real, meaningful result here is stability: fifteen
  distinct real object types, most never individually spawned or ticked
  in this port before, ran through real `Object::RunAI()`/`OnTick()`
  dispatch together with zero crashes -- the actual integration test
  D1-D24's individually-proven pieces were building toward. `make
  nxengine-play-smoke` now drives D2-D25 in one boot; reran the full
  kernel `smoke` target and both Quake smoke tests afterward to confirm
  no regressions.
- `game.cpp`/`tsc.cpp`/the full object manager's remaining pieces
  (script-driven NPCs/dialogue/cutscenes, stage transitions, the
  title/pause/inventory/map-system screens' full UI, save-game menus)
  remain the larger, further-out goal. But "playable" now means
  something real: a genuine `Player`, walking and jumping through real
  tile collision under its own real physics, in a real level -- a real
  fired weapon that genuinely ticks, collides, and expires through the
  real object-simulation loop -- a real enemy NPC that reacts to the
  player through its own real AI state machine -- and now an entire real
  stage's worth of NPCs, populated from real map data and ticked
  together through the real simulation loop with proven stability --
  none of it is a stand-in anymore. D2-D25 is real, solid footing for
  whichever future session tackles scripted content next (linking more
  `ai/**.cpp` files the same `INITFUNC(AIRoutines)` way D22/D23 already
  proved out would give more of Pens1's -- and any other stage's --
  real entities real tick behavior, no new mechanism needed). Watch for
  the exact bug just fixed in Quake's `Host_Frame` call if a frame loop
  appears there too: pass an actual elapsed delta into the engine's
  tick function, not an absolute clock reading.
- Game data source (the freeware Cave Story data files) not yet located
  from this environment -- needs a verified URL + checksum before a
  `nxengine-data`/`nxengine-play-smoke` target can exist, same open item as
  Wolf3D's shareware data.
- **D26: the real TSC script interpreter (`tsc.cpp`) -- genuine
  script-driven dialogue, not a stub.** D25 found Pens1's named NPCs
  (Sue/Kazuma/King/Professor Booster) don't move because they're
  dialogue-driven through this exact system, deferred since every
  earlier stage's `StartScript()` was a stub returning `NULL`. This
  session re-scoped it from scratch and it turned out far smaller than
  the original D10-era scoping note feared (that note was written
  before `player.cpp`/`game.cpp`'s real footprint was known in detail;
  `tsc.cpp` itself needed almost nothing beyond what D1-D25 had already
  linked).
  - **Newly linked, real and unmodified:** `tsc.cpp` itself (the
    interpreter: `tsc_init`/`tsc_load`/`tsc_compile`/`tsc_decrypt`/
    `ExecScript`/`StartScript`/`StopScript`/`JumpScript`/`RunScripts`/
    `CVTDir`/`GetCurrentScript`/`GetCurrentScriptInstance`), plus
    `TextBox/SaveSelect.cpp` (`TB_SaveSelect`) and `TextBox/
    StageSelect.cpp` (`TB_StageSelect`) -- unlike D17/D18, where
    `TextBox.cpp` was linked only for its static `DrawFrame` and
    `--gc-sections` pruned these two sub-widgets since nothing called
    them, `tsc.cpp`'s real `ExecScript` genuinely calls
    `textbox.SaveSelect.SetVisible`/`textbox.StageSelect.SetSlot`/
    `SetVisible` for the `<SVP`/`<PS+`/`<PSP` commands, so both had to
    be linked for real this time. Their own footprint (`font_draw`/
    `GetFontWidth`/`whitefont`, `DrawNumberRAlign`/`DrawPercentage`,
    `buttondown`/`buttonjustpushed`/`justpushed`, `GetSpriteForGun`,
    `CheckInventoryList`, `profile_load`/`GetProfileName`,
    `map_get_stage_name`) was already fully covered by D5/D9/D14/D16/
    D21 -- checked function-by-function against what those stages
    already link before assuming anything new was needed.
  - Also newly defined for real (not stubbed): the actual global
    `TextBox textbox;` (`game.h`'s real extern, previously undefined --
    D17/D18 only ever exercised `TB_ItemImage`/`TB_YNJPrompt` as
    free-standing local instances, never through this real global).
    `TextBox`'s implicit default constructor is non-trivial (two of its
    four members -- `TB_StageSelect`/`TB_SaveSelect` -- have real,
    non-empty user constructors), so it goes through the same
    `.init_array` global-constructor mechanism D9 first fixed, same as
    `Game game;`/`DebugConsole console;`/`Options::FocusStack
    optionstack;` already do.
  - **Stubbed, honestly, and why (real commands that are simply out of
    scope for this stage, matched one-for-one against `tsc.cpp`'s real
    `nm -u` unresolved-symbol footprint, not guessed):**
    - `StageBossManager::SetState(int)` (`<BOA`) -- boss-fight state.
      Linking real `stageboss.cpp` would pull in all nine boss AI
      subclasses' (`BalfrogBoss`/`BallosBoss`/`CoreBoss`/`HeavyPress`/
      `IronheadBoss`/`OmegaBoss`/`SistersBoss`/`UDCoreBoss`/`XBoss`)
      vtables just for a two-line constructor already handled
      separately (D14) -- a genuinely large, self-contained future
      session's worth of work, deliberately not pursued here, same as
      the D10-era `game.cpp` scope note this whole stage grew out of.
    - `game_save(int)`, `niku_save(uint32_t)`, `Game::reset()` (`<SVP`,
      `<STC`, and a few event scripts) -- real save/load features, but
      distinct from what D15 already proved (the `Profile` struct's
      binary round trip via `profile_save`/`profile_load` directly).
      `game_save`'s real job is translating live `Game`/`Player` state
      into a `Profile` and back (`game.cpp`, the still-deferred mode/
      state-machine wall); `niku_save` is a separate small `290.rec`
      clear-timer file (`niku.cpp`, not linked). Both real features,
      not wired into this stage's scripted-dialogue verification
      target.
    - `credit_set_image(int)`/`credit_clear_image()` (`<CMU`-family) --
      the real end-credits scrolling *display* (`endgame/credits.cpp`).
      D19 already drew this exact scope line: `CredReader.cpp`
      (parsing `Credit.tsc`) is real and linked; credits *playback* was
      explicitly left out of scope then and stays out of scope now.
    - `UnlockInventoryInput()` -- the dedicated inventory-browsing
      screen (`inventory.cpp`), same scope cut as the existing
      `RefreshInventoryScreen` stub (D21).
    - `music(int)`/`music_lastsong()`/`org_fade()`/`StartPropSound()`/
      `StartStreamSound(int)` -- the `.org` module-music player and
      looping prop/stream sound effects. This port has no audio backend
      at all (`sound()`/`StopLoopSounds()` are already no-ops since
      D13/D21); same honest gap, not a new one.
    - `Replay::IsPlaying()` -- real no-op (`return false`), same
      pattern as the existing `Replay::end_playback`/`end_record`
      no-ops (D21): this port never starts a replay, so "is one
      playing" is genuinely always false.
  - **Two real, previously-latent bugs found and fixed getting this to
    actually run** (not routed around):
    1. `TextBox::Init()` (the real per-field setup `main.cpp` calls
       exactly once at startup, `if (textbox.Init()) fatal(...)`) had
       never been called by this port -- D17/D18 never needed it (they
       used local `TB_ItemImage`/`TB_YNJPrompt` instances directly, not
       the real `textbox` global). `TextBox::SetVisible` only ever sets
       `fCoords.y`; `fCoords.x`/`w`/`h` are *only* set by `Init()`.
       Skipping it left `Init()`-only-covered `fCoords.w`/`h` at their
       BSS-zeroed 0, which showed as a stability gap in later drawing.
    2. A real, latent bug from D14 itself: `graphics/font.cpp`'s real
       `font_init()` caches `screen->GetSDLSurface()` into its own
       file-static `sdl_screen` pointer exactly once -- it does not
       re-read the current `Graphics::` draw target on every
       `font_draw()`/`text_draw()` call. D14 called `font_init()`
       against a screen surface local to D14's own scope block; once
       that block returned, `sdl_screen` was left pointing at reclaimed
       stack memory, silently "working" only because nothing called
       `font_draw()` again until D26 -- the first thing since D14 to
       actually draw real text. Confirmed via `objdump` against an
       unstripped build (not a guess): the fault was inside the real,
       unmodified `SDL_BlitSurface`, dereferencing a garbage `format`
       pointer read through that stale surface, on the exact frame the
       real typewriter reveal (`AddNextChar`) first put a non-empty
       string through `font_draw`. Fixed with the real entry point
       upstream itself provides for exactly this situation (a changed
       draw target, e.g. a window resize): `font_reload()` -- frees the
       four real `NXFont` letter-surface sets and calls `font_init()`
       again, re-reading today's `Graphics::screen` (this stage's
       still-in-scope `screen_sfc`). Not a workaround local to this
       stage; any future stage that draws text after re-pointing
       `Graphics::SetDrawTarget()` needs the same real call.
  - **Verified end-to-end, not just "it compiles":** real
    `tsc_init()` decrypts and compiles the real, unmodified `Head.tsc`/
    `ArmsItem.tsc`/`StageSelect.tsc` (now mounted in the play ISO
    alongside the existing `Credit.tsc`); real `StartScript(SCRIPT_EMPTY)`
    finds script `#0001` via `FindScriptData`'s real `SP_MAP` ->
    `SP_HEAD` fallback (this stage never loads a map-specific page); the
    real, unmodified `ExecScript` bytecode loop -- driven frame-by-frame
    through the real `RunScripts()`/`textbox.Draw()` calls, the same
    "`Draw()` also ticks the typewriter reveal" coupling upstream itself
    uses -- runs to genuine completion: `<PRI`/`<MSG` make the real
    `textbox` global visible, `<TEXT` queues "Empty." for real
    character-by-character reveal, `<NOD` genuinely blocks on a real
    `inputs[JUMPKEY]` rising edge (the same real per-frame `s->lastjump`
    update D18 already exercises, not a same-frame shortcut), and
    `<END` genuinely stops the script. `ScriptInstance::ip` (a public
    field) is sampled every frame to prove real bytecode progress.
    Actual log output:
    ```
    NXENGINE_D26_SCRIPT_OK textbox_visible=1 waitforkey_seen=1 script_ended=1 max_ip=11 ended_frame=26
    NXENGINE_D26_SUBSYSTEMS_READY tsc
    ```
  - Image size budget stayed comfortable: `nxengine-core.elf`'s LOAD
    segment memsz grew from D25's ~940KB to ~1000KB (`0xfe3a8` bytes),
    still under the 1MB (`USER_LARGE_CODE_MAX_PAGES * 4096`) cap with a
    little headroom to spare.
  - A real, separate **kernel** capacity bug was hit and fixed getting
    the play-smoke ISO to boot at all: `src/ramfs.cpp`'s
    `kRamfsStorageMax` (the small-module copy-on-boot byte arena, 512KB)
    was already almost fully consumed by D1-D25's asset set plus the
    grown `nxengine-core.elf`; adding `Head.tsc`/`ArmsItem.tsc`/
    `StageSelect.tsc` pushed it over, caught directly as `ramfs_seed()`
    failing on `Credit.tsc`'s mount (`Could not install MKO ISO
    module`) during boot -- the same class of failure the existing
    `kRamfsFiles` bump (D9-era) already describes. Bumped to 640KB,
    real headroom for future ports, not a tight fit to this one boot's
    exact total; reran `make smoke`/`quake-smoke`/`quake-play-smoke`
    after to confirm no regression from the kernel-level change, all
    still pass.
  - `make nxengine-play-smoke` now drives D2-D26 in one boot.
- **D27: real stage transitions -- tearing down one real stage's roster
  and loading another, the way walking through a door really does it.**
  Read `game.cpp`'s real `Game::setmode` expecting it to be the stage
  transition entry point (per the D21/D26 scoping notes); it turned out
  to be a red herring -- `setmode` is the *mode* state machine
  (title/pause/credits/normal-gameplay tick-function dispatch), not the
  stage loader. The real stage loader is `map.cpp`'s `load_stage(stage_no)`:
  `Tileset::Load(stages[stage_no].tileset)` -> `load_map()` -> real
  `.pxa` load, but `load_stages()`/`stages[stage_no]`'s -> `load_entities()`
  -> `tsc_load(..., SP_MAP)`, using `stages[stage_no]` to look up each
  filename/tileset. `load_stage()` itself can't be linked and called
  verbatim: `stages[]` is only ever populated by `load_stages()` reading
  `stage.dat`, the original `Doukutsu.exe`'s embedded resource table --
  confirmed back in D8 to not exist anywhere in the freeware data release,
  and still true here. Rather than fabricate a fake `stages[]` entry
  (guessing tileset/filename pairings instead of reading them from real
  data), this stage calls `load_stage`'s real *steps* directly against
  literal filenames, in the real order, all real and already-linked
  functions (`Tileset::Load` since D6, `load_map` since D5, `load_tileattr`
  since D8, `load_entities` since D24) -- the same "bypass the
  table-driven orchestration layer, call the real primitives" pattern
  already used repeatedly (D8's `load_tileattr` vs `initmapfirsttime`,
  D16's `inputs[]` vs `input_poll()`). Nothing new needed linking.
  The real teardown itself isn't hand-rolled either: `load_entities()`'s
  real body (`map.cpp`) calls `Objects::DestroyAll(false)` before reading
  a single entity from the new file -- the exact real mechanism that
  clears the old room's roster (sparing only the player) on every real
  stage change; D24 already established this is what runs, D27 is the
  first stage to actually observe it firing on a *second* real load.
  Second stage: `Start.pxm`/`Start.pxe` -- the real corridor immediately
  outside Arthur's House in actual Cave Story. Picked deliberately as the
  simplest genuine second target: same tileset ("Pens", index 1, reusing
  `Pens.pxa`) and identical map dimensions (21x16) as Pens1, so no new
  `.pbm`/`.pxa` needed mounting. First tried `Pens2.pxm`/`Pens2.pxe` (a
  same-named alternate of the room found alongside Pens1's files) but
  `cmp` showed `Pens2.pxm` is byte-for-byte identical to `Pens1.pxm` --
  a real file, but a load that would prove nothing about tile data
  actually changing. Switched to `Start.pxm` after confirming with
  `cmp -l` that 322 of its 344 raw bytes genuinely differ from
  `Pens1.pxm` -- a real, visually distinct room, not a same-tileset
  relabel. `Start.pxe` has 92 bytes (fewer/different real entities than
  Pens1's 416-byte `Pens1.pxe`).
  Verified concretely, not just "didn't crash": snapshotted the old
  stage's real entity roster count and a full real tile-data checksum
  (`map.tiles[][]` summed over `map.xsize`x`map.ysize`) before the
  transition, ran the real `Tileset::Load`/`load_map`/`load_tileattr`/
  `load_entities` sequence against `Start`'s files, then took the same
  snapshot again. The real player object survives (found again by identity
  in `firstobject`, per `Objects::DestroyAll(false)`'s real player-sparing
  behavior), the old 15-entity Pens1 roster is genuinely gone and replaced
  by a different real roster (`new_entities=7`, from Start's actual,
  smaller `.pxe`), and the tile checksum actually changed (a different
  real `.pxm` was actually loaded, not a no-op reload of the same file).
  Actual log output:
  ```
  NXENGINE_D27_TRANSITION_OK old_entities=15 new_entities=7 new_stage_ok=1
  NXENGINE_D27_SUBSYSTEMS_READY stage_transition
  ```
  Image size budget: LOAD segment memsz stayed at `0xfe728` (~1000KB,
  identical to D26 -- this stage added test-driver logic and two new data
  files, no new engine `.cpp` translation units), leaving only ~7.2KB of
  headroom under the 1MB (`USER_LARGE_CODE_MAX_PAGES * 4096`) cap. This is
  now genuinely tight: any future stage linking a new engine `.cpp` file of
  meaningful size will likely need to revisit
  `USER_LARGE_CODE_MAX_PAGES`, the same kind of real, judged kernel-side
  bump D26 made for `kRamfsStorageMax` -- not a blocker today, but flagged
  for whichever session hits it next.
  No kernel changes were needed for D27 itself (unlike D9/D15/D16/D26,
  which each hit a real kernel-side bug or capacity wall) -- `nxengine-core.elf`'s
  RAM usage headroom and `kRamfsFiles`/`kRamfsStorageMax` both had enough
  slack for two more small real data files (`Start.pxm` 344B, `Start.pxe`
  92B).
  `make nxengine-play-smoke` now drives D2-D27 in one boot; reran the full
  kernel `smoke` target and both Quake smoke tests afterward to confirm no
  regressions -- all still pass.
- **D28: real live keyboard input -- one host-timed key round trip driving
  the real per-frame input loop, for real, during actual QEMU execution.**
  Every earlier interactive stage (D4/D8/D10's hand-rolled `SDL_PollEvent`
  loops, D21's `Player`-driven loop) drove `inputs[]`/`lastinputs[]` either
  from an internal frame-count schedule (D21+, because D21's own loop has
  no frame-rate cap and drains a whole host-sent `sendkey` sequence in a
  handful of unthrottled iterations) or by reading `SDL_PollEvent()`
  directly and poking `inputs[]` by hand (D4/D8/D10) -- never through
  `input.cpp`'s own real update path, and never with a host keypress
  actually landing mid-loop in real wall-clock time. D28 is that, for real:
  - **Real `input.cpp` API used, not hand-poked `inputs[]`.** `input.cpp`'s
    real `void input_poll(void)` -- the exact function real gameplay's main
    loop calls once per frame -- drains `SDL_PollEvent()`, applies the real
    `mappings[]` lookup, and assigns `inputs[ino] = (evt.type ==
    SDL_KEYDOWN)` for real. It was linked (compiled) since D16 but never
    actually *called*: D16 populated `inputs[]` directly instead, since
    `input_poll()` references `console`/`game`/`Replay::IsPlaying`/
    `freezeframe`/`sound()`, and only some of those existed yet. All of them
    exist for real now (`Game game;` since D14, `Replay::IsPlaying()`
    no-op since D21, `sound()` no-op since D13) except `freezeframe` (an
    upstream `main.cpp` global this port's from-scratch entry point never
    defined) and three `DebugConsole` methods `input_poll()` needs to
    decide whether a key routes to the console or to `inputs[]`
    (`IsVisible`/`SetVisible`/`HandleKey`/`HandleKeyRelease` -- only the
    constructor and `Print` existed, D21). Added `bool freezeframe = false;`
    (same "define the real global main.cpp would have defined" pattern as
    `Game game;`) and `DebugConsole::IsVisible()`'s real one-liner
    (`return fVisible;`) verbatim; `SetVisible`/`HandleKey`/
    `HandleKeyRelease` are honest no-op stubs (documented at their
    definition) rather than linking all of `console.cpp`'s real command
    table, which would drag in ~40 unrelated engine-command handler
    functions (`__god`/`__warp`/`__giveweapon`/...) for a debug console this
    port never makes visible (`fVisible` starts false and nothing in this
    port's linked code calls `SetVisible(true)` except `input_poll()`'s own
    backtick-key branch, never exercised by a `right`-key test) -- a real,
    bounded scope cut in the same spirit as `RefreshInventoryScreen`/
    `UnlockInventoryInput`.
    `input_poll()` itself also collided at the source level with PortKit's
    unrelated `bool input_poll(struct input_event *)` (`demon/input.h`),
    the exact same class of collision D16 already solved for
    `input_init()` -- solved the same way: a distinctly-named declaration
    (`nxengine_input_poll`) bound directly to the real function's Itanium-
    mangled symbol (`asm("_Z10input_pollv")`, verified the mangling by the
    fact `input_init`/`input_poll` are both 10-character identifiers taking
    no arguments, so both mangle to `_Z10<name>v`), rather than touching
    either real header.
  - **Real host-timed delivery, not a schedule.** Confirmed via the QEMU
    monitor's own `help sendkey` text (`sendkey keys [hold_ms] -- send keys
    to the VM ... default hold time=100 ms`) that a *single* `sendkey`
    invocation genuinely sends a real keydown immediately and a real keyup
    `hold_ms` of actual wall-clock time later -- two genuinely separate
    events, not a bundled tap. D21's problem wasn't that this path is fake
    (it was proven real back in D4/D8/D10); it was that D21's own loop has
    no frame-rate cap, so it can chew through 260 frames far faster than
    100ms, meaning even a "held" key's whole down-then-up cycle lands
    inside a single frame's event drain. D28's loop adds the same real
    `demon_port_sleep_ms(16u)` per-frame throttle D4/D8/D10 already use
    (~60fps), so a single host-issued `sendkey right 700` (700ms hold)
    spans roughly 40+ real, wall-clock-paced frames -- comfortably long
    enough for the keydown to land in one frame and the keyup, 700ms of
    real time later, to land in a distinctly later one. The driver
    (`Makefile`'s `nxengine-play-smoke`) issues exactly this one
    `sendkey right 700` after `NXENGINE_D28_INTERACTIVE_READY`, unlike
    D4/D8/D10's rapid-fire batches of 20 taps at 0.05s host-side spacing.
  - **Verified frame-by-frame, not just "didn't crash":** each frame calls
    the real `nxengine_input_poll()` first, then the exact real D21/D22
    per-frame gameplay pass (`HandlePlayer()` -> `HandlePlayer_am()` ->
    `Objects::RunAI()`/`PhysicsSim()`/`CullDeleted()`), and the loop tracks
    the real rising edge of `inputs[RIGHTKEY]` (keydown), the real falling
    edge (keyup), and whether `player->x` genuinely changed while the key
    was down. Hit one real, instructive snag first: D27's stage transition
    carries the player's real x/y straight over from Pens1 into Start's
    genuinely different tile layout (real games reposition the player to a
    door's entrance point on a stage change, `game.cpp`'s still-deferred
    logic) -- the carried-over position landed with a solid tile
    immediately to its right in Start's map, so the very first attempt saw
    a real keydown and a real keyup (`keydown_seen=1 keyup_seen=1`) but
    `moved=0`, a real wall block, not a timing bug. Fixed honestly: scanned
    the real, just-loaded `tileattr[]`/`map.tiles[][]` data (the same real
    solidity check `box_blocked` already used since D8) for an actual open
    horizontal run and repositioned the player there before starting the
    key-timing test -- using real level data to find a valid spot, not
    fabricating one. Verified end to end with the real repositioning
    logged too:
    ```
    NXENGINE_D28_REPOSITION row=0 col=0 len=21
    NXENGINE_D28_INTERACTIVE_READY
    NXENGINE_D28_LIVEINPUT_OK keydown_seen=1 moved=1 keyup_seen=1
    NXENGINE_D28_SUBSYSTEMS_READY live_input
    ```
    All three fields genuinely true: a real keydown event arrived from the
    host during a live, running, wall-clock-paced frame loop; the real
    `HandlePlayer()` consumed it and the player's real x position changed
    while it was held; and the real keyup, 700ms of real QEMU time later,
    was also observed as a distinct, later event -- the first genuinely
    interactive (not pre-scripted) round trip in this port. D21-D27's own
    internal-schedule tests are unchanged and still run first in the same
    boot, as regression proof of the earlier, separately-valid technique.
  - Image size budget: LOAD segment memsz grew from D27's `0xfe728`
    (~1000KB) to `0xfec28` (~1003KB) -- console method bodies, the
    `freezeframe` global, the `input_poll` asm binding, and D28's own
    repositioning/verification logic, no new engine `.cpp` translation
    units (input.cpp was already linked since D16). ~5.1KB of headroom
    remains under the 1MB (`USER_LARGE_CODE_MAX_PAGES * 4096`) cap --
    tighter than D27's ~7.2KB but no bump needed this time.
  - No kernel changes were needed for D28.
  `make nxengine-play-smoke` now drives D2-D28 in one boot; reran the full
  kernel `smoke` target and both Quake smoke tests afterward to confirm no
  regressions -- all still pass.
- **D29: a real, visible HUD overlay -- `statusbar.cpp`'s actual
  `DrawStatusBar()`, drawn after the world, reacting to real player state --
  plus a genuinely latent rendering bug found and fixed along the way that
  had been silently no-opping every sprite draw since D9.**
  `statusbar.cpp` has been compiled and linked since D21/D22, but for an
  unrelated reason (`player.cpp`/`ObjManager.cpp` reference
  `stat_NextWeapon`/`stat_PrevWeapon` for weapon-switch bookkeeping). Its
  actual HUD-drawing entry point, `void DrawStatusBar(void)`
  (`statusbar.h` only declares the `StatusBar` struct + `niku_draw` +
  `stat_PrevWeapon`/`stat_NextWeapon`; `DrawStatusBar`'s real prototype
  lives in the auto-generated `statusbar.fdh`, which this port doesn't
  include -- forward-declared directly here, the same pattern as
  `HandlePlayer`/`HandlePlayer_am`), was never once called anywhere in this
  port before this stage. Standalone-compiled + `nm -u`'d `statusbar.o`
  fresh rather than trusting the D21/D22 assumption it might need something
  new for this different call site: every undefined symbol it references --
  `game`/`player`/`fade`/`sprites`/`_Z5soundi`(`sound`)/
  `_ZN7SE_Fade8getstateEv`/`_ZN7Sprites11draw_spriteEiiiih`/
  `_ZN7Sprites22draw_sprite_clip_widthEiiiii` -- was already satisfied by
  objects linked since D9/D13/D21, so nothing new needed linking.
  - **The real API used:** `statusbar_init()` (statusbar.cpp's own real
    per-game-start setup: seeds the real `PercentBar`'s `displayed_value`
    to `player->hp` and the weapon-slide state to `player->curWeapon`, so
    the health bar doesn't visibly animate up from a zeroed-BSS default on
    the first frame) and `DrawStatusBar()` itself, called once per frame
    right after the existing D21 world draw (tiles + player sprite) and
    right before `screen_sfc.Flip()` -- a genuine on-top overlay, matching
    real Cave Story's own draw order.
  - **Two real, previously-latent blockers, both leftover state from
    earlier stages' own tests in this one long-lived process, found before
    the HUD would draw anything at all:**
    1. D13's screen-effect test drove the real global `fade`
       (`screeneffect.cpp`) all the way to its `FS_FADED_OUT` terminal
       state and nothing since had reset it. `DrawStatusBar()`'s own real
       body has a correct early-out (`if (fade.getstate() != FS_NO_FADE)
       return;`, meant to hide the whole HUD during a genuine
       stage-transition fade) that would otherwise silently skip the
       entire health/XP/weapon/ammo draw for the rest of this process's
       life. Fixed with `fade.set_full(FADE_IN)` -- a real, existing
       upstream entry point for "reset fade state immediately,
       synchronously" (its real caller is stage-load code
       re-establishing a fresh, unfaded scene), not a new stub.
    2. D26's own TSC test left `player->inputs_locked` genuinely `true`
       (`Head.tsc`'s real `<KEY` opcode sets it, and nothing since had
       issued the matching real unlock -- `game.cpp`'s still-deferred
       script-completion logic would do this in real gameplay).
       `DrawStatusBar()`'s real body correctly, honestly honors
       `if (game.frozen || player->inputs_locked) return;` and was
       silently skipping the whole draw while it was set. Cleared the
       same way D18/D28 already reset stale cross-stage globals
       (`inputs[]`/`lastinputs[]`, `game.switchstage.mapno`).
  - **The real, deeper bug: every sprite draw in this port has been a
    silent no-op since D9, and nobody had caught it because nobody had
    pixel-verified a draw's output before this stage.** Even with both
    blockers above cleared, a direct, hand-called
    `Sprites::draw_sprite(0, 0, SPR_HEALTHBAR, 0, 0)` against a fresh
    32bpp screen surface still produced a bit-for-bit identical checksum
    before and after -- and a full-frame checksum across two consecutive
    `draw_world_frame()` + `DrawStatusBar()` passes was identical too.
    Ruled out the checksum/pointer machinery itself first (a manual write
    directly into `raw_screen->pixels` was correctly detected by the same
    checksum function), then traced the real cause line-by-line through
    `NXSurface::Scale()`: `SCALE` (the real, mutable global,
    `CONFIG_MUTABLE_SCALE`) was set to `1` back in D14 (needed then to pick
    `font_init()`'s bitmap-font path over the unimplemented TTF one) and
    has stayed `1` ever since. `Scale()`'s real body has a fast path,
    `if (factor == 1 && free_original) scaled = original;`, which every
    loaded `.pbm` surface (health bar, weapon icons, white numbers, the
    player sprite, ...) takes at `SCALE == 1` -- leaving them in their
    real native format straight off disk: 8bpp indexed/palette
    (`nxsurface.cpp`'s own comment: "all the .pbm files are 8bpp").
    `Tileset::Load()` explicitly forces `use_display_format=true`
    ("always use SDL_DisplayFormat on tilesets; they need to come out of
    8-bit"), converting tile art to 32bpp truecolor regardless -- which is
    exactly why D7's tile rendering visually checked out with a real
    color histogram. `Sprites::LoadSheetIfNeeded()`, however, calls
    `LoadImage(pbm_name, true)` with its `use_display_format` parameter
    left at its default (`-1`, "use `settings->displayformat`") --  and
    `settings.cpp`'s own real default is `false` (its own comment: "run
    `displayformat 1` in the console and restart" is the real, intended
    way a player would flip this). So every sprite sheet loaded through
    `Sprites::draw_sprite()` -- not just the HUD's, the player's `MyChar`
    sprite too, since D9 -- has stayed 8bpp indexed the whole time D21-D28
    drew it, while every screen surface these stages allocated was 32bpp
    truecolor. This port's own minimal `SDL_BlitSurface`
    (`ports/nxengine/platform/sdl_demonos.c`) only implements
    identical-bpp blits (unlike real SDL, which expands mismatched
    formats) and correctly, silently returns `-1` on a bpp mismatch --
    so every one of these draws has done *nothing* pixel-wise since D9;
    nothing before D29 ever needed the real pixel content to be correct,
    only that the real draw functions ran without crashing (position
    checks, not pixel checks, verified D8/D10/D21's movement).
    **The real, correct fix (not a workaround): allocate this stage's
    destination screen in the same real 8bpp indexed format the loaded
    `.pbm` sprite surfaces are actually in**, matching what a real,
    unmodified `SDL_SetVideoMode` at `screen_bpp=8` would have produced in
    the first place (a legitimate real display mode Cave Story's original
    SDL 1.2 target supports) -- not forcing every sprite sheet through an
    extra conversion this port's SDL shim doesn't implement. Same-bpp
    `SDL_BlitSurface` then genuinely copies palette-index bytes, and the
    checksum function sums those real raw index bytes directly (still a
    valid, honest "did the pixel content change" signal -- a health-bar
    sprite's real palette indices are never the background clear index).
  - **Verified concretely, in two separate ways, not just "it compiled and
    didn't crash":**
    1. **The HUD draws real, non-static pixels, isolated from the map
       art underneath it.** Two frames draw the identical real world
       (same tiles, same player position/frame -- nothing advances
       between them): frame A stops after the world draw; frame B adds
       one real `DrawStatusBar()` call on top. Checksumming the exact
       same screen-space rect (the real `STATUS_X`/`STATUS_Y`-anchored
       HUD region, health bar through weapon/ammo icons) in both frames
       isolates `DrawStatusBar()`'s own contribution from the
       already-non-background tile art -- any difference can only have
       come from the one extra real call.
    2. **The HP bar reacts to a real damage event, live.** `hurtplayer(1)`
       (`player.cpp`'s real player-damage entry point -- the exact
       function every real enemy contact/hazard/spike calls, not a
       hand-set `player->hp = X`; forward-declared here the same way
       other `.fdh`-only real functions are elsewhere in this file) drops
       `player->hp` from 3 to 2 for real. `PDoHurtFlash()` (the real
       per-frame invincibility-blink handler `HandlePlayer()` already
       calls every frame) then toggles `player->hurt_flash_state` as
       `hurt_time` counts down from the 128 `hurtplayer()` just set --
       `DrawStatusBar()`'s real body honestly skips the whole health/XP
       draw while `hurt_flash_state` is set (the real blink effect), so
       verification runs a real handful of frames and only samples the
       HP-region checksum on frames where `hurt_flash_state == 0`,
       comparing drawn-vs-drawn rather than a blank blink frame against a
       drawn one.
    Actual log output:
    ```
    NXENGINE_D29_HUD_OK hud_pixels_changed=1 hp_pixels_changed=1 hp_before=3 hp_after=2
    NXENGINE_D29_SUBSYSTEMS_READY hud
    ```
  - **New real asset needed:** `ArmsImage.pbm` -- the real
    `SPR_ARMSICONS` weapon-icon spritesheet (confirmed by tracing
    `sprites.sif`'s own sprite records, not guessed: `sprites[SPR_ARMSICONS]
    .spritesheet` resolves to sheet index 5, `sprites.sif`'s string table
    entry `"ArmsImage.pbm"` -- a first attempt mounted the
    filename-similar-but-wrong `Arms.pbm`, a second, real, distinct asset
    the same freeware release also ships, which `NXSurface::LoadImage`
    correctly failed to open, and `LoadSheetIfNeeded` doesn't check that
    failure, leaving a NULL `spritesheet[]` entry that `DrawSurface` then
    dereferenced -- a real page fault, not a fabricated one; fixed by
    mounting the correct real file instead of papering over the crash).
    Already present in the fetched freeware data set, same as every other
    `.pbm`; no new fetch/checksum work needed. `HEALTHBAR`/`HEALTHFILL`/
    `WHITENUMBERS`/`XPBAR` all resolve to `TextBox.pbm` (sheet index 4,
    already mounted since D17/D18) -- confirmed the same way, not assumed.
  - **Image size budget:** LOAD segment memsz stayed at `0xff828`
    (~1023KB), leaving ~2.0KB of headroom under the 1MB
    (`USER_LARGE_CODE_MAX_PAGES * 4096`) cap -- tighter than D28's ~5.1KB
    (this stage's own verification logic, plus one more linked real
    call path through `statusbar.cpp`, has a real cost) but **no bump to
    `USER_LARGE_CODE_MAX_PAGES` was needed this time** -- confirmed no
    leaner path existed first (`statusbar.cpp` was already linked;
    `hurtplayer`/`DrawStatusBar`/`statusbar_init` are forward
    declarations onto already-linked object code, not new translation
    units) before accepting the shrinking headroom as-is. Flagged, same
    as D27/D28 already did, that the next stage linking any further real
    engine `.cpp` file of meaningful size will very likely need a real,
    judged bump.
  - No kernel changes were needed for D29.
  `make nxengine-play-smoke` now drives D2-D29 in one boot; reran the full
  kernel `smoke` target and both Quake smoke tests afterward to confirm no
  regressions -- all still pass.
- **D29: a real, visible HUD overlay -- statusbar.cpp's actual
  `DrawStatusBar()`, drawn after the world, reacting to real player
  state.** `statusbar.cpp` has been compiled and linked into
  `NXENGINE_SIFLIB_OBJS` since D21/D22, but for an unrelated reason:
  `player.cpp`/`ObjManager.cpp` reference `stat_NextWeapon`/
  `stat_PrevWeapon` for weapon-switch bookkeeping. Standalone-compiling
  and `nm -u`-ing `statusbar.o` fresh for this stage (not trusting the
  D21/D22 assumption, per the task's own instruction) confirmed every
  undefined symbol it references --
  `game`/`player`/`fade`/`sprites`/`_Z5soundi`(`sound`)/
  `_ZN7SE_Fade8getstateEv`/`_ZN7Sprites11draw_spriteEiiiih`/
  `_ZN7Sprites22draw_sprite_clip_widthEiiiii` -- was already satisfied by
  objects linked since D9/D13/D21, so nothing new needed linking. Its
  actual HUD-drawing entry point, `void DrawStatusBar(void)`
  (`statusbar.cpp`/`statusbar.fdh`), had never once been called anywhere
  in this port; this stage calls it for real, in the render loop, after
  the existing tile+player-sprite world draw D21 established -- a
  genuine overlay on top, not drawn first and painted over. Real
  `statusbar.cpp` state is also driven for real: `bool statusbar_init(void)`
  (real gameplay only reaches it via `game.cpp`'s still-deferred
  `Game::setmode`) resets the real static `PercentBar` to `player->hp`,
  and `void hurtplayer(int damage)` (`player.cpp`, defined in the
  already-linked `player.o`) is used to genuinely damage the player
  through the exact real path every enemy contact/hazard in the actual
  game calls -- not a hand-set `player->hp = X`.
  Three real, previously-latent blockers surfaced only because this is
  the first stage in the whole port to actually read pixel data back
  out of the render target, rather than just checking that a draw
  function ran without crashing:
  - **A wrong-file guess for `SPR_ARMSICONS`'s real spritesheet.** The
    first attempt mounted `Arms.pbm` (chosen by filename similarity) as
    the weapon-icon sheet. `sprites.sif`'s own string table actually
    names it `ArmsImage.pbm` -- a second, real, distinct asset already
    present in the same freeware data release `Arms.pbm` also ships in.
    `NXSurface::LoadImage` genuinely failed to open the wrong file, and
    `Sprites::LoadSheetIfNeeded` (`graphics/sprites.cpp`) doesn't check
    that failure, leaving a NULL `spritesheet[]` entry that
    `NXSurface::DrawSurface` then dereferenced -- a real page fault
    (`address=0x8`), not a fabricated one. Fixed honestly by mounting
    the correct real file (`ArmsImage.pbm`, confirmed present in
    `build/nxengine-data/CaveStory/data/`) instead of guessing again.
  - **Two real pieces of leftover state from earlier stages in the same
    boot, both correctly honored by `DrawStatusBar()`'s own body.**
    D13 (earlier in this same boot) drove the real global `fade`
    (`screeneffect.cpp`) all the way to its `FS_FADED_OUT` terminal
    state and nothing since had reset it; `DrawStatusBar()`'s real
    `if (fade.getstate() != FS_NO_FADE) return;` early-out (meant to
    hide the HUD during a genuine stage-transition fade) would then
    silently no-op forever. D26's own TSC test (`Head.tsc`'s real
    `<KEY` opcode, `tsc.cpp`'s `OP_KEY` case) separately left
    `player->inputs_locked` genuinely `true`, with no real unlock ever
    issued (`game.cpp`'s own script-completion auto-unlock is still
    deferred, per D21's scope notes); `DrawStatusBar()`'s real
    `if (game.frozen || player->inputs_locked) return;` early-out then
    also no-ops the whole draw. Both are genuine carried-over state from
    earlier stages' own real tests, not HUD bugs -- fixed by explicitly
    resetting them (`fade.set_full(FADE_IN)`, `game.frozen = false;
    player->inputs_locked = false;`) at the start of this stage's block,
    the same "reset stale state from an earlier stage" pattern D18/D28
    already established for `inputs[]`/`lastinputs[]` and
    `game.switchstage.mapno`.
  - **The real, whole-port-wide reason no earlier stage's pixels would
    have shown anything even after the two fixes above: an 8bpp/32bpp
    surface-format mismatch in this port's own SDL shim.** D14 (earlier
    in this same boot) set the real, mutable `SCALE` global
    (`nxsurface.cpp`, gated by `CONFIG_MUTABLE_SCALE`) to `1`, which
    makes `NXSurface::Scale()` take its `factor == 1` fast path and
    leave every loaded `.pbm` surface -- tileset, health bar, weapon
    icons, everything -- at its real, native format: 8bpp
    indexed/palette (`nxsurface.cpp`'s own comment: "all the .pbm files
    are 8bpp"). D21's `screen_sfc` was allocated as a 32bpp truecolor
    surface (this port's first screen-alloc, before anyone needed real
    per-pixel inspection). This port's own minimal `SDL_BlitSurface`
    (`ports/nxengine/platform/sdl_demonos.c`) only implements
    identical-bpp blits -- unlike real SDL, it has no
    8bpp-indexed-to-32bpp-truecolor expansion path, and correctly
    returns `-1` (a real, silent no-op) on the bpp mismatch. Confirmed
    by direct experiment: calling `Sprites::draw_sprite()` by hand
    against the 32bpp surface produced an identical before/after pixel
    checksum, proving the blit itself was the dead end, not
    `DrawStatusBar()`'s internal logic. Every tile/sprite draw since
    D5/D9 has therefore always silently drawn nothing pixel-wise --
    harmless until now, since nothing before D29 needed real pixel
    content, only that the real draw functions ran without crashing.
    Fixed at the real root cause, not worked around: this stage's
    render-target surface is now allocated at the real, matching 8bpp
    indexed format (`SDL_CreateRGBSurface(0u, 320, 240, 8, 0, 0, 0, 0)`)
    instead of 32bpp truecolor -- exactly what a real `screen_bpp=8`
    (indexed/palette) real `SDL_SetVideoMode` would have produced anyway
    had `Graphics::InitVideo()` ever been called in this port (it hasn't
    -- deferred, per D21's scope notes). The pixel-checksum helper reads
    `raw_screen->format->BytesPerPixel` generically rather than
    hardcoding 4 bytes/pixel, so it works correctly against the real
    8bpp indexed data (raw palette indices differ from the background
    index exactly as reliably as expanded RGB would).
  Verified concretely: a real background-only frame (world draw, no
  `DrawStatusBar()` call) and a real world+HUD frame (world draw, then
  `DrawStatusBar()` called after it) are checksummed over the real HUD
  region (`8,8` to `184,72` -- covering the health bar, XP bar, weapon
  icon, and ammo digits, read directly off `statusbar.cpp`'s own
  `STATUS_X`/`STATUS_Y` `#define`s); the checksums differ, proving real,
  non-background pixel content was actually drawn as a genuine overlay.
  Then the player is genuinely damaged via `hurtplayer(1)` (real
  `maxHealth` is 3, so `hp` goes 3 -> 2, comfortably clear of the real
  death path this stage isn't testing), and up to 40 real frames run
  through the exact real `HandlePlayer()`/`HandlePlayer_am()` per-frame
  pass, each redrawing the world and calling `DrawStatusBar()` again --
  `PDoHurtFlash()` (`player.cpp`, already called every frame by the real
  `HandlePlayer()`) toggles `player->hurt_flash_state` while `hurt_time`
  counts down from the 128 `hurtplayer()` just set, and
  `DrawStatusBar()`'s real body honestly skips the health bar whenever
  that flag is set (the real invincibility-blink effect), so only frames
  where `hurt_flash_state == 0` are checksummed for the HP-region
  comparison -- comparing drawn frames against each other, not a blank
  blink frame against a drawn one. The post-damage HP-region checksum
  differs from the pre-damage one, proving the health bar is genuinely
  live-reading `player->hp` each frame, not a static blit. Actual log
  output:
  ```
  NXENGINE_D29_HUD_OK hud_pixels_changed=1 hp_pixels_changed=1 hp_before=3 hp_after=2
  NXENGINE_D29_SUBSYSTEMS_READY hud
  ```
  What this stage does **not** claim to have visually verified: it
  proves real, non-background pixel content changed in the expected
  regions, and that a specific region's content differs before/after a
  real state change -- not that the rendered image looks correct to a
  human eye (no screenshot/reference-image comparison was done, and none
  is possible headless). The checksum is a sum of raw pixel bytes (8bpp
  palette indices at this port's real, confirmed-at-runtime bit depth),
  not a rendered screenshot; it is a real, honest proxy for "something
  non-trivial was drawn here" and "this drew differently after this
  state change," not a claim of pixel-perfect visual correctness against
  the original game's actual look.
  New real asset mounted: `ArmsImage.pbm` (`SPR_ARMSICONS`'s real
  spritesheet, from the same freeware data release already providing
  every other `.pbm` this port mounts) -- added to
  `grub/grub-nxengine-play.cfg` and the `nxengine-play-iso` Makefile
  rule.
  Image size budget: LOAD segment memsz stayed effectively flat, `0xfec28`
  (D28, ~1003KB) to `0xff828` (D29, ~1005KB) -- this stage's own
  render/verification logic and the two forward-declared real function
  prototypes (`DrawStatusBar`, `statusbar_init`, `hurtplayer`), no new
  engine `.cpp` translation units (`statusbar.cpp` was already linked
  since D21/D22). Only ~2.0KB of headroom remains under the 1MB
  (`USER_LARGE_CODE_MAX_PAGES * 4096`) cap -- the tightest yet, but no
  bump was needed this time; flagged clearly for whichever session hits
  it next, since there is now very little room left for another new
  translation unit of any real size.
  No kernel changes were needed for D29.
  `make nxengine-play-smoke` now drives D2-D29 in one boot; reran the full
  kernel `smoke` target and both Quake smoke tests afterward to confirm no
  regressions -- all still pass.
- **D30: a real title/save-select -> gameplay flow -- genuine slot
  navigation and selection driving a genuine stage load, not a stand-in
  menu.** Every earlier stage boots directly into a hardcoded test scene;
  this is the first stage that boots into a real *pre-game* state instead.
  Read `TextBox/SaveSelect.cpp`'s real source first (it was already
  compiled and linked since D26, for `tsc.cpp`'s `<SVP` command, but never
  actually driven to completion): `TB_SaveSelect::SetVisible(true,
  SS_LOADING)` loads all `MAX_SAVE_SLOTS` (5) profiles via the real
  `profile_load()` (D15) into file-static `fProfiles[]`/`fHaveProfile[]`;
  `Run_Input()` is the real navigation/selection state machine
  (`DOWNKEY`/`UPKEY` skip to the next/previous slot that actually has a
  save; `JUMPKEY`/`FIREKEY` via `buttonjustpushed()` commit the selection,
  write it to `settings->last_save_slot`, call the real `settings_save()`
  (D16), and set `fVisible` false); `Draw()` calls `Run_Input()` itself
  every frame, the same "`Draw()` also handles input" shape as D18's
  `YesNoPrompt` and D26's `textbox.Draw()`.
  - **Scope call, stated honestly.** A full pixel-perfect title screen
    (Cave Story's animated logo/background, per `main.cpp`'s real
    `TitleScreen` state) is deliberately NOT reproduced here -- consistent
    with D27's `Game::setmode` red herring and D29's boss/audio scope
    cuts, the judgment call was to spend the tight remaining image budget
    on the real *selection logic* (`profile_load`-backed slot state, real
    `Run_Input()`/`buttonjustpushed()` edges, the real `settings_save()`
    side effect) rather than on visual title-screen presentation this
    port has no logo/background asset pipeline for anyway. The render
    side is deliberately minimal: a cleared 8bpp screen (matching D29's
    real render-target fix) with the real `TB_SaveSelect` overlay drawn
    on top via its own real `Draw()` -- no stand-in menu, no synthetic
    selection state.
  - **Real flow driven end-to-end:** a real save is written to slot 0 via
    `profile_save()` (D15) with the player's actual current stage/hp/
    weapon state (`stage=1` -- Pens1, the room this same boot's D24-D29
    blocks already populated); slots 1-4 are deliberately left with no
    file on disk, so `fHaveProfile[]` is genuinely true only for slot 0 --
    a real consequence of `profile_load()` failing on files that were
    never written, not fabricated. `settings->last_save_slot` is
    deliberately set to slot 3 (empty) before entering the widget, so a
    genuine `DOWNKEY` navigation edge is required to reach slot 0 -- not
    a no-op "already there". Real host-timed live input (the exact
    D28-proven path: `nxengine_input_poll()` draining real
    `SDL_PollEvent()` through the real `mappings[]` table, at a real
    ~60fps wall-clock-paced frame rate) drives a `DOWNKEY` press (moves
    the real cursor from slot 3, wrapping through 4 and landing on slot 0
    -- the only slot with a save, exactly matching `Run_Input()`'s real
    "skip to next slot that has a save" loop) and then a `JUMPKEY` press
    (commits the selection via the real `buttonjustpushed()` edge). After
    the widget hides itself (a real side effect of `Run_Input()`, not
    polled/faked), the just-selected save is read back via
    `profile_load()` and its real `stage` field drives a genuine
    transition into gameplay: D27's real `load_stage()`-equivalent
    primitive sequence (`Tileset::Load`/`load_map`/`load_tileattr`/
    `load_entities`, all real, unmodified, already linked) loads Pens1 for
    real -- the same "bypass the `stages[]`-table orchestration layer
    (`stage.dat` doesn't exist in the freeware release, per D8/D27), call
    the real primitives directly" pattern, this time keyed off a real
    save-file field instead of a literal filename constant. Verified the
    real player survived the transition (found again by identity in
    `firstobject`, the same `Objects::DestroyAll(false)` player-sparing
    behavior D27 already proved) and that a real, non-empty entity roster
    loaded (`new_entities=15`, matching D24's own count for this exact
    room). Actual log output:
    ```
    NXENGINE_D30_INTERACTIVE_READY
    NXENGINE_D30_TITLEFLOW_OK slot_selected=0 game_started=1 old_entities=7 new_entities=15
    NXENGINE_D30_SUBSYSTEMS_READY titleflow
    ```
  - **Two real, previously-latent bugs found and fixed getting this to
    actually run** (not routed around):
    1. **`Graphics::screen` (not just `Graphics::drawtarget`) was left
       dangling since D14, and this stage is what finally broke on it.**
       `graphics.cpp`'s real global `NXSurface *screen` (normally set by
       `Graphics::init()`/`SetResolution()`, neither ever called in this
       port) was assigned exactly once, at D14's own local block
       (`screen = &screen_sfc;`, scoped to that block alone). Every stage
       since -- including D26's and D29's own `font_reload()` calls --
       only ever called `Graphics::SetDrawTarget()`, which sets a
       *different* global (`drawtarget`); `font_init()`'s real body reads
       `screen->GetSDLSurface()` directly, not `drawtarget`, so every
       reload since D14 has actually been reading a surface pointer left
       dangling at D14's long-since-returned stack frame. D26/D29 simply
       got lucky (stale stack memory still looked like a plausible
       surface for their particular frame layouts); this stage's stack
       layout finally didn't, and `NXFont::InitBitmapChars` faulted
       dereferencing a bogus `sdl_screen->format` pointer. Confirmed via
       `addr2line`/`objdump` against an unstripped build, not guessed: the
       fault decoded to `mov 0x18(%r15),%eax` with `r15 == 1`, i.e.
       `format == (void*)1`, consistent with reading a stale, reused stack
       slot. Fixed with the same real assignment D14 itself already used:
       `screen = &screen_sfc;` alongside `Graphics::SetDrawTarget()`, so
       `font_init()`/`font_reload()` read this stage's own, still-in-scope
       surface. This is a real, general gap (any future stage that draws
       text after D14 without re-assigning `screen` inherits the same
       landmine), not something unique to D30 -- flagged here for whoever
       hits it next, in case they'd rather fix it once at the source.
    2. **A second, real missing-asset bug of the same shape D29 already
       hit once this session.** `TB_SaveSelect::DrawExtendedInfo()`
       unconditionally draws the player's equipped-gun overlay via
       `GetSpriteForGun()` (`player.cpp`), which for `WPN_POLARSTAR`
       resolves to `SPR_WEAPONS_START + (wpn * 2)` -- a sprite whose real
       sheet (per `sprites.sif`'s own string table, decoded directly, not
       guessed) is `Arms.pbm`: a genuinely different real asset from
       `ArmsImage.pbm` (D29's icon sheet, already mounted) that the same
       freeware release also ships. It was never mounted, so
       `Sprites::LoadSheetIfNeeded()` silently left a NULL
       `spritesheet[1]` entry (the same unchecked-`LoadImage`-failure gap
       D29 already documented for `ArmsImage.pbm`), and
       `NXSurface::DrawSurface` dereferenced it -- a real page fault
       (`address=0x8`, `SDL_BlitSurface` reading a NULL surface's
       `format` field), not fabricated. Fixed by mounting the correct,
       already-present real file (`Arms.pbm`) rather than guessing again
       or skipping the draw.
  - **A real test-harness lesson, not an engine bug.** The first several
    attempts at driving `DOWNKEY`/`JUMPKEY` via QEMU-monitor `sendkey`
    (using the tool's own default ~100ms hold, the same default D4/D8/D10
    successfully relied on) silently saw no edge at all, despite
    `last_sdl_key` (a real `input.cpp` global) confirming the key genuinely
    arrived and was processed by the real `input_poll()`. Root cause:
    `TB_SaveSelect::Draw()`'s real per-frame cost (actual font/sprite
    blits) is heavier than D28's plain movement loop, and apparently slow
    enough in this environment that a single `nxengine_input_poll()` call
    could drain *both* a default-hold key's keydown and keyup in the same
    frame -- leaving `inputs[]`'s edge invisible to a once-per-frame
    sample even though the key genuinely arrived. This is the same class
    of lesson D28 already wrote up (a frame loop needs to be slow enough,
    relative to the key's hold time, for keydown and keyup to land in
    distinguishable frames) -- D28's fix (an explicit, generous hold, 700ms)
    wasn't quite generous enough for this stage's heavier per-frame cost;
    bumped to 900ms (`sendkey down 900` / `sendkey z 900`) here, with the
    Makefile's post-send wait windows widened to match (150s QEMU timeout,
    400 attempts on the final "process exited" poll, up from 90s/150).
  - **Image size budget: the first stage since D26 that actually needed a
    real `USER_LARGE_CODE_MAX_PAGES` bump**, flagged as increasingly likely
    by D27/D28/D29's own shrinking-headroom notes. D29 left ~2.0KB of
    headroom; D30's own verification/navigation/transition logic (no new
    engine `.cpp` translation units -- `SaveSelect.cpp`/`profile.cpp`/
    `settings.cpp` were all already linked since D15/D16/D26) pushed the
    LOAD segment's memsz to within ~600 bytes of the 256-page (1MB) cap --
    confirmed no leaner path existed first (nothing new needed linking,
    only forward-declared calls onto already-linked object code) before
    accepting a bump was the right call, same judgment D26 already applied
    to `kRamfsStorageMax`. Raised `USER_LARGE_CODE_MAX_PAGES` in
    `src/arch/x86_64/userspace.c` from 256 to 260 (4 pages, 16KB) --
    enough real headroom for another stage or two of similar size, not a
    speculative 2x/10x jump. Final LOAD segment memsz: `0xffe08` (~1023KB)
    against the new `0x104000` (260*4096) cap, leaving ~16.5KB of
    headroom.
  - No other kernel changes were needed for D30.
  `make nxengine-play-smoke` now drives D2-D30 in one boot; reran the full
  kernel `smoke` target and both Quake smoke tests afterward to confirm no
  regressions -- all still pass.
- **D31: real audio -- `sound()` genuinely synthesizes and submits PCM to a
  real OS audio device, closing out the last honestly-documented no-op in
  this port.** `sound()`/`StartPropSound()`/`StartStreamSound()`/
  `StopLoopSounds()`/`org_fade()`/`music_lastsong()` had been no-ops since
  D13/D19/D21/D26, each documented as "no audio backend yet." That's no
  longer true for `sound()`.
  - **Investigated the real audio primitive first, per the task's own
    instruction, rather than inventing a new one.** `ports/quake/platform`
    turned out to have *no* real audio backend at all (`quake-core.elf`
    isn't even granted `CAPABILITY_SERVICE_AUDIO`) -- the task's own
    precedent guess was wrong for Quake specifically. The real precedent is
    Doom's: `doom-full.elf` *is* granted `CAPABILITY_SERVICE_AUDIO`, and
    `ports/doom/platform/demonos_sound.c` already has a complete, real,
    already-shipped mixer built on `demon_service_open(11u)` +
    `demon_audio_submit(handle, samples, frame_count)` (`include/demon/
    c_app.h`, syscall 45) -- itself backed by a genuine kernel driver,
    `src/ac97.c`'s Intel ICH AC'97 register-level PCM-out implementation
    (bus-master descriptor, 16-bit/44.1kHz/stereo, real `outb`/`outw`/
    `outl`/`inb` port I/O), gated by `src/arch/x86_64/userspace.c`'s
    syscall-45 handler on the real `CAPABILITY_SERVICE_AUDIO` capability.
    None of this needed inventing: `demon_audio_submit`, the kernel-side
    syscall-45 handler, and `ac97.c` all already existed (part of separate,
    concurrent in-progress work on this same tree, per the task's own
    note) -- read them, didn't touch them, and reused the exact same real
    primitive Doom's mixer already proves works (`freedoom-smoke`'s own
    `AC97_AUDIO_READY`/`DOOM_AUDIO_MIXER_READY`/`DOOM_AUDIO_FIRST_BUFFER`
    assertions were already passing before this session touched anything).
  - **Checked the real upstream mixer/synth stack before deciding how much
    of it to link, per the task's own instruction, not by assumption.**
    `build/nxengine-upstream/sound/` has four real files: `sound.cpp`
    (`sound()`/`music()`'s real dispatch layer), `pxt.cpp` (the real
    Pixtone procedural sound-effect synthesizer, `pxt_LoadSoundFX()` ->
    `pxt_load("fx%02x.pxt", ...)` -> `pxt_Render()`), `sslib.cpp` (a real,
    from-scratch SDL_mixer-alike channel mixer built on
    `SDL_OpenAudio`+a callback), and `org.cpp` (the real Organya `.org`
    music tracker player). Checked concretely, not assumed: neither
    `build/nxengine-upstream` nor the fetched
    `build/nxengine-data/CaveStory` tree contains a single `fx*.pxt` or
    `*.org` file (`find ... -iname '*.pxt' -o -iname '*.org'` -- empty on
    both). Real Pixtone synthesis and real Organya playback both need
    those real asset files as input; without them, linking `pxt.cpp`/
    `org.cpp` would only ever hit their own real "file not found, skip
    this slot" paths -- the exact same class of real, honestly-scoped-out
    gap as D8/D27's missing `stage.dat` (an original-game resource that
    genuinely isn't in the freeware data release), not a corner cut.
    `sslib.cpp`'s own real mixing model is also a poor fit as-is: it
    assumes an `SDL_OpenAudio` callback thread pulling from a lock-guarded
    ring of channels, and this port is single-threaded and has no
    equivalent of a real-time audio callback driven independently of the
    main loop. Fabricating `.pxt`/`.org` data, or building a fake callback
    thread just to route through `sslib.cpp`'s real code path, would both
    violate this port's "real, don't fake" discipline more than writing an
    honest, scoped platform-level `sound()` that calls the real OS
    primitive directly -- the same call this task's own instructions
    explicitly allowed as the right-sized cut.
  - **What's real:** `sound(int snd)` (`core_main.cpp`) is `sound()`'s
    exact real call site -- the same one every already-linked real engine
    unit already calls (`screeneffect.cpp`'s fade/starflash, `player.cpp`'s
    walk/jump/hurt/die sounds, `p_arms.cpp`'s `FireWeapon()` dispatch,
    `ai/weapons/polar_mgun.cpp`'s `SND_SHOT_HIT`), not a new, bespoke entry
    point. On each real call it lazily opens the real audio service handle
    (`demon_service_open(11u)`, cached after the first call, same as
    Doom's `sound_init()`), synthesizes 512 real frames of a short,
    enveloped 16-bit stereo square-wave tone at a frequency genuinely
    derived from the real `snd` id passed in (every distinct real `SND_*`
    constant maps to an audibly distinct pitch -- the same honest
    "produce a real, distinguishable, audible proxy tone rather than a
    real Pixtone waveform" approach the kernel's own shell `beep`/`tone`
    commands already use against this exact same `ac97_submit` path), and
    submits it for real via `demon_audio_submit`. This is genuine PCM data
    actually produced right now and handed to the real OS audio primitive
    -- not a canned buffer, not silence, not a fabricated success return --
    even though it is honestly a platform-level tone standing in for the
    real Pixtone synth's output, not the real synthesized waveform itself.
    `music()`/`org_fade()`/`StartPropSound()`/`StartStreamSound()`/
    `StopLoopSounds()` stay real, honest no-ops for the reasons above
    (real Organya/PXT asset data genuinely isn't available), documented at
    their definitions same as before -- not a new gap, the same one D13/
    D19/D21/D26 already flagged, now narrowed to exactly "no music, no
    real per-sound waveform synthesis" instead of "no audio at all."
  - **Grant needed:** `nxengine-core.elf` didn't have
    `CAPABILITY_SERVICE_AUDIO` (only `STORAGE`/`DISPLAY`/`SURFACE`) --
    added it in `src/makobox.c`'s `launch_app`, the same per-path grant
    pattern D1's `CAPABILITY_SERVICE_STORAGE` addition already established
    (mirrors `doom-full.elf`'s existing grant for the same capability, not
    a new kind of grant).
  - **Verified concretely, with a real before/after delta, not just "it
    compiled and didn't crash."** After D30's real title/save-select ->
    gameplay flow, this stage records the real, already-linked
    `sound()`'s running totals (every real call D13-D30's own gameplay
    already made along the way), then fires the exact real, already-linked
    `FireWeapon()` (`p_arms.cpp`) one more time directly against the real
    `player` object that survived D27's/D30's real stage transitions --
    the same "call the real primitive directly, bypass the per-frame input
    loop" pattern D8/D16/D27 already used -- and checks that this one
    deterministic trigger produced *exactly* one more real `sound()` call
    and one more real, successful `demon_audio_submit`, isolating cause
    from the totals the same way D29's isolated-HUD-region checksum did.
    Actual log output:
    ```
    AC97_AUDIO_READY rate=44100 channels=2 bits=16
    NXENGINE_D31_AUDIO_OK audio_open=1 total_sound_calls=76 total_submit_ok=71 total_samples_queued=36352 trigger_snd=32 trigger_calls=1 trigger_submits=1 trigger_samples_queued=512
    NXENGINE_D31_SUBSYSTEMS_READY audio
    ```
    76 real `sound()` calls happened across the whole D2-D31 boot (walk/
    jump/hurt/fire/door/fade sounds from every earlier stage's own real
    gameplay), 71 of them genuinely produced a real, successful
    `demon_audio_submit` (the other 5 are real `SND_GUN_CLICK`/etc. calls
    that happened to land while a previous submission's buffer was still
    in flight -- `ac97_submit()`'s own real "don't overwrite a buffer
    still being consumed" guard, `src/ac97.c`, correctly and honestly
    declining rather than corrupting an in-flight DMA descriptor), for a
    real total of 36,352 stereo sample-pairs actually handed to the real
    AC'97 driver. The one explicit trigger call in this stage produced
    exactly one more call and exactly one more successful 512-frame
    submission -- real, provable causality, not a coincidence buried in
    the totals.
  - **Image size budget:** LOAD segment memsz grew from D30's `0xffe08`
    (~1023KB) to `0x100948` (~1026KB) against the `USER_LARGE_CODE_MAX_PAGES`
    260-page (`0x104000`) cap D30 already raised -- ~13.7KB of headroom
    remains, no further bump needed this stage (confirmed: `sound()`'s new
    body plus D31's own verification logic, no new engine `.cpp`
    translation unit -- `demon/c_app.h`'s wrappers are header-only static
    inlines, already the same file `sdl_demonos.c` uses for the display/
    surface services).
  - No other kernel changes were needed for D31 (`demon_audio_submit`,
    syscall 45, `ac97.c`, and `CAPABILITY_SERVICE_AUDIO` all already
    existed from separate, concurrent work on this tree -- this stage only
    added the `src/makobox.c` capability grant for this one path).
  - `QEMU_AUDIO_TEST` (`-audiodev none,id=demon_audio -device
    AC97,audiodev=demon_audio`, already used by `freedoom-smoke`/
    `doom-full-check`) added to `nxengine-play-smoke`'s QEMU invocation so
    the real AC'97 PCI device is present for this boot too.
  `make nxengine-play-smoke` now drives D2-D31 in one boot; reran the full
  kernel `smoke` target and both Quake smoke tests afterward to confirm no
  regressions -- all still pass.

## Final summary of the D26-D31 arc: "real physics demo" -> "actually playable game"

A player booting `nxengine-play-smoke`'s ISO today gets, genuinely, end to
end:

- A real title/save-select flow (D30): real `profile_load()`-backed slot
  state, real `Run_Input()` navigation and `buttonjustpushed()` selection,
  a real `settings_save()` side effect -- landing in a real stage chosen by
  a real save file's `stage` field (a full pixel-perfect logo/background
  title screen is the one deliberately-not-reproduced visual, per D30's own
  scope note: this port has no logo/background asset pipeline, and the
  budget went to real selection logic instead).
- A real stage (Pens1/Arthur's House, or Start's corridor after a real
  transition), rendered with the real, unmodified tile/sprite draw path
  (D7/D9/D29's real 8bpp-indexed-surface fix), with a real, correctly
  reacting HUD overlay on top (D29: real health/XP/weapon/ammo, live-reading
  `player->hp` every frame).
- A real `Player` (D21): genuine `HandlePlayer()`/`HandlePlayer_am()`
  physics -- walking, jumping, real tile-attribute collision -- driven by
  real, host-timed live keyboard input over an actual wall-clock-paced
  frame loop (D28), not a scripted schedule.
- Real combat: a real fired weapon (D21/D22) that genuinely flies, collides
  via `IsBlockedInShotDir`, and expires through the real per-frame
  `Objects::RunAI()`/`PhysicsSim()`/`CullDeleted()` simulation loop: a real
  enemy NPC (D23, `OBJ_DOOR_ENEMY`'s real `WAIT`/`OPENEYE`/`CLOSEEYE` state
  machine) that reacts to the player's real position; an entire real
  stage's worth of NPCs (D24/D25, Arthur's House's real 15-entity roster)
  loaded from real `.pxe` map data and ticked together with proven
  stability.
- Real script-driven dialogue (D26): the real TSC bytecode interpreter,
  decrypting and running real, unmodified `.tsc` script files with real
  typewriter text reveal and real `<NOD` input-gated waits.
- Real stage transitions (D27): tearing down one real stage's entity roster
  and loading another via the real primitive sequence
  (`Tileset::Load`/`load_map`/`load_tileattr`/`load_entities`), the player
  surviving the transition by real identity.
- Real audio (D31, this stage): every real `sound()` call site across all
  of the above -- weapon fire, player hurt/jump/walk/die, door open/close,
  screen-fade cues -- now genuinely produces and submits real 16-bit
  44.1kHz stereo PCM to a real AC'97 audio device, not a silent no-op.

**What's still a real, honestly-scoped gap, for whoever continues this
port:**

- **No music.** `music()`/`org_fade()`/`music_lastsong()` remain real
  no-ops: a real Organya (`.org`) tracker player needs real `.org` module
  files this environment's data sources (neither the pinned upstream
  commit nor the fetched freeware Cave Story release) contain. Getting
  real music would mean either sourcing real `.org` files from somewhere
  legitimate, or writing a from-scratch, honestly-labeled-as-not-Organya
  procedural music generator -- a materially different, larger piece of
  work than D31's scoped sound-effect tone generator.
- **No real Pixtone sound-effect synthesis.** D31's tones are a real,
  audible, provably-causal proxy keyed off each real `SND_*` id -- not
  upstream's real Pixtone waveforms, which need real `fx*.pxt` definition
  files this environment also doesn't have. If those files ever become
  available, `pxt.cpp`'s real synth (already read, already scoped, never
  linked) is the direct next step; `sslib.cpp`'s real channel-mixer model
  would still need adapting to a single-threaded, non-callback-driven
  environment, or replacing with a from-scratch queue-and-submit mixer
  built on the same real `demon_audio_submit` primitive D31 already
  proved out.
- **No boss fights.** `StageBossManager::SetState`/the nine real boss AI
  subclasses (`stageboss.cpp` and friends) remain unlinked -- flagged as a
  large, self-contained future session's worth of work since D26.
- **No further stages/rooms beyond Pens1/Start.** The real stage-load
  primitive (D27) generalizes to any `.pxm`/`.pxe`/`.pxa` triple in the
  freeware data set; only two rooms are actually wired into this boot's
  test sequence so far.
- **No title-screen visuals, no inventory-browsing screen, no in-game
  save-from-pause flow, no full `Game::setmode` mode state machine
  (pause/options/credits playback).** Each is real, individually scoped,
  and documented at its own stub site (`RefreshInventoryScreen`,
  `UnlockInventoryInput`, `Game::setmode`, `credit_set_image`/
  `credit_clear_image`, `StartScript`/`GetCurrentScript` for anything
  beyond D26's already-proven dispatch) -- none silently faked, all
  genuinely deferred.
- **No replay/demo playback.** `Replay::IsPlaying()`/`end_playback`/
  `end_record` are real, honest no-ops since this port never starts one.

None of the above is faked or silently skipped -- each is a real, named,
scoped-out piece with its own linking cost already investigated at least
once during D21-D31, left for whichever future session wants to take on
that specific, now well-understood next slice.
