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
- strict AABB collision state derived from each TPAG target rectangle;
- collision event/action tables resolved from OBJT event type 4 to their CODE
  entries; live updates execute Step first, recompute overlaps, then dispatch
  only the collision handlers associated with objects still touching;
- generic OBJT event/subtype/action lookup discovers each instance's own
  Step and collision CODE entries from the resource graph;
- **general per-instance Step and Collision dispatch**: every real placed
  instance in the loaded room gets its own persistent VM state (seeded from
  its own real room position, not a shared/arbitrary offset) and its own
  Step handler resolved from its own real `objectId` via
  `DemonDataWinIndex_eventCode`, instead of the previous hard-coded
  single "object 0 is the only movable instance" assumption (which also
  hard-required the room to have exactly 5 render commands, so it could
  not even boot against a real game's differently-shaped room). Collision
  resolution is now all-pairs: every ordered instance pair with a real
  handler and an overlapping TPAG rectangle dispatches to whichever
  instance(s) actually declared that handler, matching
  `DemonDataWinIndex_collisionCode(selfId, otherId)`'s real asymmetric
  semantics rather than assuming one fixed "self." This is exercised by
  the boot-time fixture scene (`BUTTERSCOTCH_D4_COLLISION_OK`/
  `_EVENT_MAP_OK`/`_LIFECYCLE_OK`, now reporting real instance/steppable/
  collidable-pair counts instead of fixed "object0" text). Not yet
  covered: room transitions, event types other than Step/Collision
  (Create, Destroy, Alarm, Draw, etc.), parent-chain event inheritance,
  and more than one room loaded at once — and the *live/rendered* dispatch
  path is currently only reached by the `fixture` build profile, since the
  `deltarune-ch1`/`undertale-demo` profiles exit after the D1-D8 probes
  before reaching it (real multi-page texture atlas support and a
  full-buffer-VM-vs-decoded-textures memory budget the current renderer
  doesn't have yet — see the headless dispatch proof below for the
  decoupled-from-rendering alternative that *does* run against real data);
- **real per-instance dispatch proof (headless, no rendering)**: a new
  boot-time probe (`BUTTERSCOTCH_D9_REAL_DISPATCH_OK`/`_PARTIAL`) runs the
  same per-instance Step/Collision dispatch above against one full real
  room (Deltarune Ch1's room 54, 16 real placed instances) — decoupled
  from rendering entirely, since collision AABB checks only need each
  instance's TPAG rectangle metadata, not decoded pixels. Confirmed
  against the real, legitimately-owned data: of 16 real instances, 2
  declare a real Step handler; 1 runs its entire real compiled Step script
  to completion end to end (the first real per-object gameplay script, not
  just a compiled-preamble fragment, to fully execute), the other fails
  partway through a real `CALL` instruction at a real code address —
  reported with the failing instance/object/code id and VM diagnostic
  fields (`BUTTERSCOTCH_D9_STEP_FAIL`/`_COLLISION_FAIL`) rather than a
  bare failure, so what's missing is identifiable rather than opaque. 0 of
  the 16 instances had a resolved collision handler against another
  instance in this specific room;
- **`call_builtin` arity-gate fix + four GMS2.3 marker builtins**: the
  D9 probe's real `CALL` failure above was traced by hand (walking the
  same FUNC occurrence-chain format `build_references` parses, against the
  real `data.win`) to a call to `@@NullObject@@` with zero arguments — a
  trivial GMS2.3-compiler-emitted marker (pushed before `method()` for
  struct literals/anonymous constructors, confirmed against upstream
  `vm_builtins.c:14451-14455`) that just returns a constant, not a deep
  semantic gap. The real bug was structural: `call_builtin` hardcoded every
  non-`method`/non-array builtin behind a single `arguments == 1u` gate, so
  any 0-argument (or 2+ argument) builtin failed closed before its name was
  even checked. Fixed by gating each builtin's own arity individually, and
  added the four trivial 0-argument GMS2.3 markers this exposed:
  `@@NullObject@@` (`INSTANCE_NOONE = -4`), `@@Global@@`
  (`INSTANCE_GLOBAL = -5`), `@@This@@`/`@@Other@@` (the real current/other
  instance ID, with `@@Other@@` falling back to self when there's no
  "other" — matches upstream's real behavior). `@@This@@`/`@@Other@@`
  needed threading a real GameMaker `instanceId` into `DemonVmInstanceState`/
  `Vm` for the first time (`DemonVm_setInstanceContext`), wired from the
  real per-instance dispatch sites (the live loop and the D9 probe).
  Proven by `DemonVm_markerBuiltinSelfTest` (all four, including the
  fallback case) and confirmed against real data: re-running the D9 probe
  after this fix showed the previously-failing `@@NullObject@@` call
  succeed and execution advance further into the same real script before
  hitting what looked like a stack-underflow at a later `POPZ` — this
  turned out to be a red herring, see the `codeId` bullet below. Not yet
  covered: `@@NewGMLObject@@`/`@@CopyStatic@@`/`@@GetInstance@@` (real
  struct/instance semantics), `@@try_hook@@`/`@@try_unhook@@`/
  `@@finish_catch@@`/`@@finish_finally@@`/`@@throw@@` (exceptions), and
  per-pair (rather than merged-array) collision dispatch granularity for
  `@@Other@@`'s accuracy when several instances collide with one instance
  in the same tick;
- **`codeMask` bitmask → explicit `codeIds` list (a real, previously
  silent, correctness bug)**: chasing the stack-underflow above with live
  per-instruction tracing showed the VM executing a *completely different,
  unrelated real script* than the one actually dispatched. Root cause:
  every per-instance dispatch call selected its target via `1u << codeId`,
  and Deltarune's real player Step handler is CODE index **761** —
  `1u << 761` is undefined behavior in C (shift ≥ type width), silently
  wrapping to some unrelated small bit rather than failing closed. This
  wasn't a corner case: a `uint32_t` bitmask can only ever address 32 of a
  real game's CODE entries (Deltarune has 1680), so *any* object whose
  Step/Collision handler landed at CODE index ≥ 32 was silently running
  the wrong script entirely. Fixed by replacing `codeMask: uint32_t`
  throughout `DemonVm_executeEventsState`/`DemonVm_executeEvents` with an
  explicit `const uint32_t *codeIds, uint32_t codeIdCount` — no bit-width
  ceiling, no bit tricks on either side of the call. Confirmed against
  real data: re-running the D9 probe now shows dispatch genuinely reaching
  real CODE 761 (`gml_Object_obj_mainchara_Step_0`, the actual player Step
  script) and a second previously-misdispatched real instance (CODE 1142),
  both failing on *new, real, identifiable* opcode gaps instead —
  `OP_PUSHGLB` (`0xC2`, a dedicated global-variable-read opcode real
  bytecode uses instead of the generic variable-push path this VM already
  handles) and `OP_PUSH` with an Int64-typed operand (`type1==3`, only the
  Double case of that extra-operand-size class is handled today);
- **`OP_PUSHLOC`/`OP_PUSHGLB` + missing `OP_PUSH` literal types**: checked
  against upstream (`vm.c:2965-2995`, `vm.h:93-95`) — `OP_PUSHLOC`/
  `OP_PUSHGLB` (`0xC1`/`0xC2`) are dedicated local/global variable-push
  opcodes the real compiler emits instead of the generic `OP_PUSH
  type1==5` path (no corresponding `OP_POPLOC`/`OP_POPGLB` exists; writes
  always go through the one existing `OP_POP`, unaffected). Their embedded
  operand encodes a real VARI table index directly
  (`resolveVarDef`/`resolveVarOperand`, `vm.c:220-223,449-454`), but VARI
  occurrence chains already record every address a variable is referenced
  from regardless of opcode, so both reuse this file's existing
  address-based `resolve()`/VARTYPE-tag-aware clone-or-share logic
  unchanged (extracted into a shared `push_variable_reference` helper) —
  no new resolution mechanism needed. Also added the three `OP_PUSH`
  literal types that were missing cases despite this file's extra-operand
  sizing already handling their byte widths correctly: `type1==3` (Int64,
  confirmed at `rvalue.h:31`, stored directly since `Value.value` is
  already `int64_t` — no truncation risk unlike `PUSHI`'s 16-bit or
  `type1==2`'s 32-bit literals), `type1==1` (Float, promoted to this VM's
  one double-typed `VALUE_REAL`, matching how upstream's `GMLReal` is
  uniformly a double internally too), and `type1==4` (Bool). `OP_PUSHBLTN`
  (`0xC3`, real engine builtin-variable accessors like `room_speed`,
  resolved through a getter dispatch table — a meaningfully different
  mechanism) is not yet implemented. Proven by `DemonVm_wideOpcodeSelfTest`
  (round-trips a value through `PUSHGLB`/`PUSHLOC` and a real >32-bit
  Int64 literal that would visibly fail if truncated, plus an independent
  Float-literal check) and confirmed against real data: re-running the D9
  probe shows both previously-failing real instances advance further still
  (their addresses moved forward again) before hitting `OP_CONV` gaps —
  the natural next investigation;
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
- allocation, file access, execution, and clean exit in ring 3;
- real nested GML script/function calls: `OP_CALL` resolves to a user-defined
  `CODE` entry (by name, the same way builtins already were) when it isn't
  one of the three hardcoded builtins, instead of only ever failing closed.
  A bounded call-frame stack (8 deep) gives each nested call its own
  argument/local storage, popped from and restored to the caller's shared
  value stack rather than recursing through C; arguments are passed
  positionally and locals/arguments are addressed through the active
  frame using each variable's real `VARI` `instanceType` (confirmed against
  real Deltarune Ch1 data: `-6`/`argument0`, `-7`/local, distinct from the
  existing `-1` self and `-5` global paths, which are unchanged). Proven by
  `DemonVm_callFrameSelfTest` (synthetic two-script call/return/argument
  round trip) and by the boot-time `BUTTERSCOTCH_D8_VM_SCRIPT_OK` probe
  against real Deltarune Ch1 CODE entries. Not yet covered: structs,
  exceptions, and stack isolation between frames (a callee can currently
  read a caller's leftover stack values below its own call base if it pops
  more than it pushed — a correctness gap for buggy/malicious bytecode, not
  a crash);
- basic single-dimension GML arrays: `OP_BREAK`'s `BREAK_PUSHAF`/`BREAK_POPAF`
  sub-opcodes (confirmed against real Deltarune Ch1 data: 358/589
  occurrences respectively in its CODE chunk) read and write array elements,
  and the `@@NewGMLArray@@`/`array_create` builtins construct them (array
  literals compile to a plain call, confirmed against upstream
  `vm_builtins.c`). Rather than porting upstream's reference-counted,
  copy-on-write `GMLArray` model, arrays here are always deep-copied
  whenever they enter persistent storage (a variable, an array element, a
  call argument), so each storage location uniquely owns its array and a
  plain release-before-overwrite is always memory-safe — the one exception
  is the array-reference operand of a `BREAK_PUSHAF`/`BREAK_POPAF` pair,
  which stays a live shared reference (identified by the real
  `VARTYPE_ARRAYPUSHAF`/`VARTYPE_ARRAYPOPAF` tag GameMaker embeds in the
  preceding `PUSH` instruction's own operand) so in-place element writes
  are actually visible afterward. Proven by `DemonVm_arraySelfTest`
  (synthetic array-create/read/write/read-back round trip). Not yet
  covered: multi-dimensional arrays (`BREAK_PUSHAC` — zero occurrences in
  the real fixture data scanned), compound-assignment reference chains
  (`BREAK_SAVEAREF`/`BREAK_RESTOREAREF`), real by-reference array arguments
  (this file's always-copy simplification means a callee mutating an array
  argument does not affect the caller's copy, unlike upstream's default),
  and values that are read but then discarded off a control-flow path other
  than `OP_POPZ`/`BREAK_SETOWNER`/top-level `RET` (all of which release
  them) — those leak rather than double-freeing, a bounded, documented gap
  consistent with this increment's scope.

The ISO includes only an upstream test `data.win`, not Undertale or any other
commercial game data. Users must supply game data they are legally allowed to
run when the full runner is available.

## Remaining stages

1. **D3 — Full VM:** nested function call frames and basic single-dimension
   arrays are working (see above). Still remaining: replace the focused
   fixture executor with upstream `VMContext` semantics more broadly,
   complete reference-counted `RValue` ownership, multi-dimensional arrays,
   structs, exceptions, and per-frame stack isolation.
2. **D4 — Renderer integration:** the native window, retained surface, texture
   upload, software command list, scaling, resize tracking, unified input
   transport, and general per-instance Step/Collision dispatch (see above)
   are working. Next, translate the rest of upstream's general draw commands
   and resource variants instead of the currently supported subset; wire a
   real game profile through to the live dispatch loop instead of stopping
   after the D1-D8 compatibility probes; add the remaining event types
   (Create, Destroy, Alarm, Draw, room transitions); and raise/replace the
   16-instance room cap for real rooms that exceed it.
3. **D5 — Runtime services:** native `SOND`/`AUDO` parsing, WAV PCM decoding,
   resampling, an eight-voice lifecycle mixer, AC'97 output, silent fallback,
   fixed-step timing, checksummed configuration, resize-state persistence, and
   clean window quit are working. Next add compressed OGG decoding, general
   game save slots, controller input, and fullscreen switching.
4. **D6 — Compatibility:** exercise supported GameMaker data versions and
   publish per-game test results without distributing proprietary assets.

Use `butterscotch` (or `butterscotch-core`) in MakoBox to launch the installed
native executable.
