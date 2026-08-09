# Butterscotch port

This is the native DemonOS port of
[AnimatedGTVR/Butterscotch](https://github.com/AnimatedGTVR/Butterscotch), an
MPL-2.0 open-source GameMaker runner. It is not a mock game and it does not
reimplement the runner under another name.

The source is pinned to commit
`9cb6287ec6b400b7b0ef8d4668e91ebea75761c3`. Run
`make butterscotch-source` to obtain that exact revision and
`make butterscotch-port-audit` to check the source/platform contract.

## Native platform foundation (complete)

The DemonOS platform foundation is complete and covered by the kernel smoke
test. This does **not** mean arbitrary GameMaker games are compatible yet: the
remaining work is runner semantics and game-by-game compatibility, not missing
OS plumbing.

`butterscotch-core.elf` is a freestanding 64-bit userspace process. It uses
PortKit for a bounded dynamic heap and RAMFS access, and compiles
Butterscotch's real `binary_reader.c` plus `binary_utils.h` to parse the legal
test package shipped in the upstream repository. The executable validates:

- little-endian integer and floating-point reads from the upstream code;
- upstream buffered reads, cursor movement, and bounds checks;
- the GameMaker `FORM` header and declared file size;
- a reusable, allocation-free index of every top-level chunk;
- duplicate chunk rejection and per-chunk bounds;
- boot-time negative tests for truncation and duplicate chunks;
- the `STRG`, `CODE`, `OBJT`, and `ROOM` pointer tables and entry counts;
- every `STRG` length, terminator, and chunk-relative boundary, plus a stable
  checksum over the decoded strings;
- `GEN8` WAD version, game ID, release version, and default resolution for
  both compact and common GameMaker layouts;
- WAD 8-14 inline and WAD 15+ relative `CODE` record layouts, including code
  names, header bounds, signed bytecode targets, locals/arguments, bytecode
  spans, and a stable checksum over the instruction bytes;
- `OBJT` records, legacy and current event-type list widths, physics vertex
  arrays, parent links, nested event/action pointer lists, and action-to-CODE
  references;
- `ROOM` headers and legacy payload lists, including room dimensions,
  background/view slots, placed instances, object bindings, creation-code
  references, and tile-record bounds;
- `TPAG` atlas items and `TXTR` metadata, including page references, source
  rectangles, PNG framing through `IEND`, image dimensions, blob boundaries,
  and a stable checksum over the embedded PNG bytes;
- native memory-only PNG decompression through Butterscotch's vendored
  `stb_image`, producing a 128x128 RGBA8 texture page and a stable checksum
  over all 65,536 decoded pixel bytes;
- strict decoding of the fixture's 52 genuine GameMaker VM instructions,
  including variable-width operands, opcode classes, and validation that all
  conditional branch destinations land on instruction boundaries;
- guarded stack duplication and terminal `Ret`/`Exit` control opcodes now
  complement the arithmetic and branch subset; synthetic execution verifies
  stack underflow/overflow rejection instead of allowing memory corruption;
- integer VM execution now covers multiply, divide, remainder, modulo,
  addition, subtraction, bitwise operations, shifts, unary negation/not, all
  six comparisons, and unconditional/true/false branches, with explicit
  divide-by-zero and shift-range rejection;
- the focused `RValue` layer now carries integers, booleans, real doubles,
  strings, and undefined values; numeric promotion supports mixed integer/real
  add, subtract, multiply, divide, negate, comparison, and checked conversion;
- execution of all four fixture CODE entries with reference-chain resolution,
  typed stack values, conversions, branches, integer arithmetic, instance
  variable stores, and native `keyboard_check`/`show_debug_message` builtins;
  deterministic left+down input proves that `x` becomes -4 and `y` becomes 4;
- a native retained ARGB surface populated from the decoded upstream texture,
  with explicit RGBA-to-ARGB conversion and damage publication; its bounded
  conversion buffer is allocated once per surface instead of once per frame;
- a bounded software-render command list with clear and TPAG sprite-blit
  operations, source/target rectangle scaling, clipping, alpha blending, and
  a deterministic 320x240 composed frame driven by the VM's `x`/`y` result;
  subsequent scene updates recompose in place, so neither rendering nor
  surface upload allocates memory in the interactive frame loop;
- native resolution of ROOM instances through OBJT sprite IDs, SPRT frame
  pointers, and TPAG records; the fixture's five real instances and four
  drawable objects now determine scene placement without hard-coded slots;
- strict AABB collision state derived from each TPAG target rectangle; the
  fixture player resolves as object 0 and correctly overlaps its three wall
  objects, ready for selective collision-event dispatch;
- collision event/action tables resolved from OBJT event type 4 to their CODE
  entries; live updates execute Step first, recompute overlaps, then dispatch
  only the collision handlers associated with objects still touching;
- generic OBJT event/subtype/action lookup now discovers the player's Step
  and collision CODE entries from the resource graph; the frame loop no
  longer assumes that Step is CODE entry zero;
- a fixed 60 Hz scheduler executes Step even with no input, then performs
  collision dispatch and rendering; idle-tick validation proves that one
  Step runs while preserving an unmoving instance's coordinates;
- VM instance variables now persist across event executions instead of being
  reconstructed each tick; a two-tick right-then-down test proves both Step
  and collision handlers share and commit the same instance state;
- native pointer coordinates and left/middle/right button state are retained
  for mouse built-ins, while focus loss atomically releases keyboard and
  mouse state so inputs cannot remain stuck after switching windows;
- the runner owns a native 44.1 kHz signed-16-bit stereo audio stream backed
  by DemonOS AC'97; its boot test submits a generated PCM buffer through the
  real audio syscall when hardware is present, while device-less systems use
  a nonfatal silent fallback so graphics-only machines can still run games;
- `SOND` and `AUDO` tables are parsed with strict pointer, string, record, and
  blob bounds; embedded audio sizes and hashes are collected without copying;
  a non-empty truncation test proves malformed blobs are rejected even though
  the legal upstream fixture intentionally contains zero sound resources;
- an allocation-free RIFF/WAVE PCM decoder accepts 8/16-bit mono/stereo input,
  resamples it to the native 44.1 kHz stereo stream, and mixes voices with
  signed-16-bit saturation into caller-owned buffers;
- a fixed-capacity eight-voice mixer provides play, pause/resume, stop,
  looping, Q8 gain and stereo pan; finished one-shot voices are reclaimed and
  looping voices wrap without allocating or copying their source samples;
- a versioned, checksummed `BSCF/1` per-game configuration stores window size,
  master gain, and fullscreen preference using a staged RAMFS write; malformed,
  truncated, out-of-range, and checksum-corrupt files are rejected;
- a scaled DemonX application window titled `Butterscotch Runner`, receiving
  keyboard, mouse motion, mouse button, close-button, and Escape events;
- restored window dimensions are used at launch and live `ConfigureNotify`
  events update the retained window state before it is saved on clean exit;
- live arrow/WASD state wired into the fixture's genuine `keyboard_check`
  calls; held movement reruns the Step bytecode from the current instance
  coordinates and republishes the surface at a capped 60 Hz;
- an offscreen surface boot test so graphics transfer is verified without
  making the automated kernel smoke test wait on an interactive window;
- allocation, file access, execution, and clean exit in ring 3.

The ISO includes only an upstream test `data.win`, not Undertale or any other
commercial game data. Users must supply game data they are legally allowed to
run when the full runner is available.

## Remaining stages

1. **D3 — Full VM:** replace the focused fixture executor with upstream
   `VMContext`; complete reference-counted `RValue` ownership, nested function
   call frames, arrays, structs, exceptions, and general object-event dispatch.
2. **D4 — Renderer integration:** the native window, retained surface, texture
   upload, software command list, scaling, resize tracking, and unified input
   transport are working. Next, translate the rest of upstream's general draw
   commands and resource variants instead of the currently supported subset.
3. **D5 — Runtime services:** native `SOND`/`AUDO` parsing, WAV PCM decoding,
   resampling, an eight-voice lifecycle mixer, AC'97 output, silent fallback,
   fixed-step timing, checksummed configuration, resize-state persistence, and
   clean window quit are working. Next add compressed OGG decoding, general
   game save slots, controller input, and fullscreen switching.
4. **D6 — Compatibility:** exercise supported GameMaker data versions and
   publish per-game test results without distributing proprietary assets.

Use `butterscotch` (or `butterscotch-core`) in MakoBox to launch the installed
native executable.
