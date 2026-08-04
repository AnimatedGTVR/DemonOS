# Native ClassiCube (Minecraft Classic) port

## Scope

The port target is [ClassiCube](https://github.com/ClassiCube/ClassiCube), a
Minecraft Classic-compatible client written in C. This is a native DemonOS
application, not a Linux binary, Java VM, browser wrapper, or mock game. The
source is pinned to `9a3101c00330aa6ca0e091bcd5c76d019ee85b7e`.

No proprietary Minecraft client, textures, sounds, or account credentials are
placed in the ISO. ClassiCube is independently developed and is not affiliated
with Mojang or Microsoft.

## Why this upstream

ClassiCube already isolates operating-system behavior behind platform,
window, graphics, audio, and networking interfaces. It also contains software
renderers used by constrained targets. That fits DemonOS much better than Java
Minecraft, which would first require a JVM, AWT, OpenGL bindings, and a much
larger runtime.

## Current DemonOS readiness

| Requirement | State | Port decision |
|---|---|---|
| ELF64 processes and bounded heap | Available | Raise the per-app arena only after profiling |
| Monotonic clock and sleep/yield | Available | Map directly through PortKit |
| Keyboard and mouse | Available | Translate unified input events, including relative look |
| 32-bit pixel surfaces | Available | Start with `Graphics_SoftGPU.c` |
| Writable files | Available | Store options/maps below `/home/mako/.classicube` |
| PCM sound | Available | Add after stable rendering/input |
| TCP, DNS, HTTP | Partial native stack exists | Offline first; multiplayer after socket ABI completion |
| Threads | Not a general userspace ABI yet | Cooperative/single-process first |
| OpenGL/GPU acceleration | Not available | Not required for the software-rendered milestone |

## Milestones

### D0 — source and contract (implemented)

- Reproducible, explicit source fetch at a pinned commit.
- Dedicated `ports/classicube/platform` boundary.
- Audit target checks upstream platform/window/software-renderer symbols.
- No ClassiCube or Mojang assets silently bundled into normal builds.

### D1 — freestanding core (implemented foundation)

- Build a native `classicube-core.elf` from the pinned upstream `String.c` and
  the separate DemonOS platform backend.
- Implement logging, checked allocation, memory operations, monotonic timing,
  cooperative sleep and clean process exit over PortKit.
- Install the ELF as `/system/bin/classicube-core.elf` and execute it as a
  normal ring-3 process during the kernel smoke boot.
- Keep the proof deliberately narrow: world, stream, bitmap and generator
  modules are the next expansion of D1, not falsely reported as complete.

Current acceptance: `CLASSICUBE_CORE_READY upstream=10` from real upstream
code, plus live checks of upstream memory streams, endian decoding, terrain
RNG, bitmap allocation/scaling, followed by `CLASSICUBE_CORE_RING3_OK` and
status zero. The mapped 4 MiB arena is released during process shutdown.

### D2 — persistent offline world (filesystem foundation implemented)

- File/path operations now map to capability-backed RAMFS through an eight-slot
  native handle table; the smoke process creates, writes, closes, reopens,
  reads, verifies, and closes a file through ClassiCube's `Stream_*` API.
- The real upstream `FlatgrassGen` creates a deterministic 32x32x32 block
  volume. DemonOS verifies its grass/dirt/air layers, saves all 32,768 raw
  blocks, reloads them, and compares FNV-1a checksums in ring 3.
- Directory enumeration and the ClassicWorld map container remain pending.
- Save options, maps, texture packs and screenshots under a dedicated root.
- Validate imported map and texture lengths before allocation/decompression.

Foundation marker: `CLASSICUBE_D2_FILE_ROUNDTRIP_OK bytes=8`.

World marker: `CLASSICUBE_D2_WORLD_OK flatgrass=32x32x32 bytes=32768`.

Raw-world acceptance is complete. D2 finishes fully when this round-trip uses
ClassiCube's ClassicWorld serializer instead of the temporary raw block file.

Edited blocks are also saved to `/home/demon/classicube-edited.raw`, reopened,
and checksum-verified through the native stream/RAMFS path. Marker:
`CLASSICUBE_D2_EDITED_WORLD_OK bytes=32768 checksum=verified`.
The startup path now loads that edited-world file back into a cleared world
buffer before mesh creation. Marker:
`CLASSICUBE_D2_STARTUP_LOAD_OK source=edited-world bytes=32768`.

### D3 — software-rendered game window

- D3 bootstrap is live: the generated world is converted into a 256x192
  isometric ARGB preview, written to a retained userspace surface, marked as
  damaged, and submitted for 120 consecutive frames from ring 3. The camera
  drifts during the run, so every frame is rendered and copied independently.
- A native `Window_DemonOS.c` now owns display, surface and input capabilities
  and implements ClassiCube's real framebuffer allocation/presentation API.
  The preview no longer bypasses the upstream `Window_*` platform boundary.
- The pinned upstream `Graphics_SoftGPU.c` is compiled into the native ELF and
  executes its real resize, color/depth clear, vertex-buffer, perspective
  transform, indexed-quad rasterization, begin-frame and end-frame paths for
  a continuous interactive frame loop in ring 3. It counts and builds all
  exposed top, bottom and side faces from the reloaded upstream flatgrass
  world into a tightly sized SoftGPU vertex buffer. Directional face shading
  makes the result readable as actual voxels, while hidden internal faces are
  never allocated. An upstream `Matrix_LookRot` camera moves between frames.
  Framebuffer checksums prove that rasterized world geometry changed the
  cleared frame.
- This preview validates the display/surface transport and bounds before the
  considerably larger SoftGPU renderer is linked.
- Implement `Window_*` against a userspace surface.
- Use ClassiCube's SoftGPU backend at a conservative initial resolution.
- Submit bounded damage rectangles; never pass unchecked pixel counts.
- Profile heap and frame time before increasing the current arena limit.

Acceptance: title/menu and a generated world render for 600 frames without
page faults, kernel memory writes, or unbounded allocation growth.

Bootstrap marker:
`CLASSICUBE_D3_WINDOW_BACKEND_OK surface=256x192 frames=120 input-drain=1`.

`CLASSICUBE_D3_SOFTGPU_OK surface=256x192 loop=interactive idle-exit=180 geometry=voxels faces=exposed shaded=1 camera=player upstream=1`.

`CLASSICUBE_D3_PREVIEW_OK surface=256x192 frames=120 animated=1 renderer=isometric`.

### D4 — playable input

- The native translation boundary now maps DemonOS key press/release events,
  absolute and relative pointer motion, three mouse buttons, and wheel input
  into ClassiCube button identifiers. A ring-3 smoke check covers W press and
  release, signed mouse deltas, left click, and downward wheel movement.
- `Window_ProcessEvents` now dispatches those translated events through the
  real upstream `Input.c` and `Event.c`: `Input.Pressed`, pointer coordinates,
  mouse-button state, and down/up/move/raw-motion/wheel callbacks are all
  exercised in ring 3. This is the live game-facing path, not a parallel shim.
- The perspective camera now consumes that state for WASD movement and raw
  mouse-look. A lightweight player body applies jump velocity, gravity,
  flatgrass height collision and map-edge bounds while rendering each frame.
- A bounded six-block camera ray now selects solid voxels and remembers the
  adjacent empty cell. Upstream left/right mouse-button state removes and
  places blocks respectively; exposed geometry is recounted and rebuilt from
  the mutated world before rendering.
- A centered high-contrast crosshair is rendered as an upstream SoftGPU 2D
  overlay after the perspective world pass.
- The game now polls DemonOS events and advances camera/physics continuously.
  Escape closes the window and exits cleanly. An unattended boot with no real
  input exits after 180 frames so the kernel smoke suite remains deterministic;
  receiving real input switches the process into interactive mode (with a
  distant hard safety cap rather than an ordinary gameplay timeout).
- Mouse-button press edges are handled inside that loop. Successful camera-ray
  edits immediately discard and rebuild the bounded exposed-face mesh, then
  autosave and checksum the edited world. Holding a button does not repeat an
  edit every frame.
- Number keys 1, 2 and 3 select grass, dirt and stone. Right-click placement
  consumes the selected material, and the ring-3 test selects dirt before
  exercising the live placement/rebuild/autosave path.
- Translate keys, text, buttons, wheel, focus and close events.
- Add relative mouse-look without cursor acceleration or first-movement stalls.
- Provide Escape/pause and clean return to MakoBox.

Acceptance: walk, look, jump, place/remove blocks, open menus and quit.

Foundation marker:
`CLASSICUBE_D4_INPUT_TRANSLATION_OK key=W mouse=-7,4 button=left wheel=-1`.

Upstream-state marker:
`CLASSICUBE_D4_INPUT_UPSTREAM_OK state=keys,pointer,buttons events=down,up,move,raw,wheel`.

Player-controller marker:
`CLASSICUBE_D4_PLAYER_OK controls=wasd,mouse-look,jump,sprint physics=gravity,aabb-3d,wall-slide,normalised`.

Movement uses a player-sized collision box for walls, floors, and ceilings.
Diagonal input is normalised, horizontal axes resolve separately for wall
sliding, and either Shift key increases movement speed while held.

Block-edit marker:
`CLASSICUBE_D4_BLOCK_EDIT_OK targeting=camera-ray reach=6 actions=remove,place,pick mesh=dirty-rebuild`.

Left click removes the targeted block, right click places the selected block,
and middle click picks a supported targeted block into the hotbar. Mesh rebuild
and persistence only run after an actual world mutation.

HUD marker: `CLASSICUBE_D4_HUD_OK crosshair=target-aware hotbar=visible renderer=softgpu`.

The crosshair changes from white to gold when the six-block camera ray has a
valid block target.

Loop marker: `CLASSICUBE_D4_GAME_LOOP_OK input=continuous exit=escape frame-cap=36000`.

Live-edit marker:
`CLASSICUBE_D4_LIVE_EDIT_OK input=edge-triggered mesh=chunk-neighbourhood autosave=1`.

The world is split into a 4x4 grid of 8x8-column meshes. An edit rebuilds only
the bounded neighbouring chunks needed to update exposed boundary faces:
`CLASSICUBE_D4_CHUNKS_OK grid=4x4 size=8 rebuild=neighbourhood`.

Rendering fix (verified on the interactive ISO):

- The 2D overlay pass (`Gfx_Begin2D`) replaces `MATRIX_PROJ` with an ortho
  matrix, and `Gfx_End2D` does not restore the perspective projection. Without
  a per-frame reload, every frame after the first rasterized the terrain with
  `view * ortho`, producing degenerate screen-space triangles and blank ground.
  Both the interactive and smoke loops now call
  `Gfx_LoadMatrix(MATRIX_PROJ, &projection)` at the start of each 3D pass.
  Screendumps show textured green terrain in the lower screen band
  (y ~393-479), the gold target crosshair at centre, and 567 distinct colours.


Hotbar marker:
`CLASSICUBE_D4_HOTBAR_OK slots=grass,dirt,stone keys=1,2,3 wheel=cycle placement=safe`.

The three-slot hotbar is drawn by SoftGPU at the bottom centre of the game
surface. A bright outline follows the active slot, so number-key selection is
visible as well as affecting right-click placement.
The mouse wheel cycles the same slots, and placement is rejected when the new
block would overlap the player's body.

Interactive launcher (MakoBox):

- The `classicube` MakoBox command spawns this same ELF with the
  console/process/storage/display/surface capability set and runs the real
  game until the player quits. The game loop renders at the current
  `DisplayInfo` resolution with a 640x480 fallback.
- ESC toggles a pause overlay (dim quad plus "PAUSED" / "ESC CONTINUE   Q QUIT"
  lines drawn from a procedural 5x7 font), and Q quits back to the MakoBox
  console prompt.
- On the scripted smoke ISO the kernel spawns the core itself and asserts its
  exit status; on the interactive ISO that spawn is gated so the boot never
  blocks waiting for a player. Both paths are covered by
  `src/kernel.c`'s `test_mode || doom_frame_test` gate and the process's own
  `demon_boot_test_mode()` branch.
- The demo atlas is fully procedural: a single 256x256 texture with speckled
  grass/dirt/stone tiles (no Mojang assets), and textured voxel meshes use
  per-face vertex colours for directional shading.
- `apps_find("classicube")` resolves `/system/bin/classicube-core.elf`; the
  catalog name is `classicube`, so the launch command and the boot smoke name
  differ from the ELF filename.

### D5 — networking

- Finish capability-scoped userspace TCP sockets and DNS.
- Map ClassiCube `Socket_*`; then add HTTP(S) resource acquisition.
- Keep offline mode functional when no NIC or network capability exists.
- Treat server packets, compressed maps, URLs and downloaded packs as
  untrusted bounded data.

Acceptance: join a test Classic Protocol server, exchange world/chat/block
updates, disconnect, and reconnect without leaked handles.

### D6 — audio and packaging

- Adapt ClassiCube's mixer to DemonOS signed 16-bit 44.1 kHz stereo PCM.
- The `classicube` MakoBox launcher is now shipped (D3/D4 are playable); only
  the mixer/audio milestone above remains before the port is complete.
- Package only redistributable upstream code/data with license notices.

## Commands

```sh
make classicube-source
make classicube-port-audit
```

Normal `make`, `make iso`, and `make run` remain network-independent.
