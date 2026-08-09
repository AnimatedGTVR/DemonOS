# Butterscotch game compatibility targets

DemonOS has two named, native compatibility targets:

| Command | Target | Expected package generation | Current result |
| --- | --- | --- | --- |
| `deltarune-ch1` | DELTARUNE Chapter 1 / SURVEY_PROGRAM | GameMaker bytecode 17 | Full structural probe passes against the real, legitimately-owned data.win (`BUTTERSCOTCH_GAME_PROBE_OK`: 14129 strings, 1680 code entries, 353 objects, 147 rooms, 9 textures, 151 sounds, 56 audio entries). A real room (room 54: 640x480, 16 placed instances) composites correctly into one real frame (`BUTTERSCOTCH_D4_ROOM_COMPOSITE_OK`) and is now actually presented on screen (`BUTTERSCOTCH_D5_DISPLAY_PRESENT_OK`) via the same raw display path Doom/ClassiCube use -- confirmed with a real QEMU screenshot showing the correct, recognizable scene. The bytecode scanner validates every one of the game's real CODE entries end to end (`BUTTERSCOTCH_D6_BYTECODE_SCAN_OK`: 1680/1680 entries, 419564 instructions, 35865 branches, 0 unknown opcodes). The VM now actually *executes* real compiled scripts, not just validates their shape (`BUTTERSCOTCH_D8_VM_SCRIPT_OK`: 3/3 real scripts run their entire compiled GMS2.3 preamble correctly) -- see "Real VM execution" and "Executing real user scripts" below. Full gameplay (movement, interaction, dialogue, and the hundreds of native builtins real scripts call beyond their compiled preamble) is not enabled yet. |
| `undertale-demo` | UNDERTALE Demo | older GameMaker Studio | Native package probe (not yet tested against a real file) |

Neither game nor its assets are distributed with DemonOS. The user must supply
a legitimately obtained `data.win`. A free download is not the same thing as a
redistribution license, so the ISO build deliberately keeps these files local.

## Importing game data

The import command accepts either `data.win` itself or a directory containing
it. It checks the GameMaker `FORM` header and declared package length before
copying anything.

```sh
make butterscotch-deltarune-data SOURCE="$HOME/Games/SURVEY_PROGRAM"
make butterscotch-undertale-data SOURCE="$HOME/Games/UNDERTALE-Demo/data.win"
```

Imported data is stored below `build/local-games/`, alongside a SHA-256
manifest. The directory is build output and must never be committed.

Build the two freestanding profile executables with:

```sh
make butterscotch-game-probes
```

After importing either or both packages, build and boot the local-only image:

```sh
make butterscotch-games-iso
make run-butterscotch-games
```

At the `mako#` prompt, run `deltarune-ch1` or `undertale-demo`. The ordinary
public ISO contains both native executables but deliberately omits the game
data, so those commands report the exact missing path instead of crashing.

The compatibility probes stream reads and seeks through PortKit. They do not
copy the complete package into the process heap, which is important because a
real game package can exceed DemonOS's bounded 24 MiB anonymous-memory window.

The profiles use separate package and configuration paths. They intentionally
stop after strict package/resource inspection today; they do not claim that
the focused fixture VM can execute either complete game. A successful probe
prints `BUTTERSCOTCH_GAME_PROBE_OK` with the actual WAD version, game ID, and
resource counts. That report is the input for the next compatibility work:

1. capture the first unsupported structure or opcode from each supplied file;
2. implement it in the common runner rather than adding game-specific hacks;
3. rerun the fixture smoke tests and both game probes;
4. enable full execution only after room creation, scripts, drawing, audio,
   saves, and clean shutdown pass on the real package.

## Bytecode 17 format quirks found against the real DELTARUNE Chapter 1 file

These were found by hex-dumping real object/texture records from the
legitimately-owned `data.win` and cross-checking against
[UndertaleModLib](https://github.com/UnderminersTeam/UndertaleModTool)'s
source and full git history, not by guessing:

- **OBJT records** (`DemonDataWinIndex_inspectObjects`,
  `DemonDataWinIndex_eventCode`): there is an undocumented extra 4-byte
  field between `Persistent` and `ParentId` that UndertaleModLib does not
  describe anywhere in its history. Confirmed via three real object
  records: skipping this field is what makes `ParentId`'s well-known
  `-100` sentinel and `TextureMaskId`'s well-known `-1` sentinel land
  exactly where expected, and makes the event-type bucket count come out
  to exactly 15 every time. Currently gated on `wadVersion == 17`.
- **TXTR metadata** (`txtr_metadata`): the `blockSize` and `dimensions`
  fields are version-gated on GameMaker's newer year.month scheme
  (`>= 2022.5`, `>= 2022.9`), but this file's GEN8 major/minor use the
  old plain-integer scheme (`2.0`) despite being wadVersion 17 -- both
  fields are actually present. Confirmed via real 64x64 texture
  dimensions landing exactly where expected once both are counted.
- **Texture pixel data isn't PNG**: DELTARUNE's TXTR blobs use
  GameMaker's BZip2-compressed texture-group format (a 12-byte
  magic/width/height/decompressed-size header followed by a raw `BZh`
  stream), not the raw PNG the original upstream test fixture uses.
  `parse_texture_blob` peeks the signature and dispatches to `parse_png`
  or `parse_compressed_texture` accordingly for structural validation.
- **Audio inspection needed to become streaming-compatible**:
  `bounded_audio_blob` and the audio checksum loop originally indexed
  `reader->buffer` directly, which only exists in fully-buffered mode --
  incompatible with the compatibility probe's deliberate streaming mode
  (see above, for files too large to buffer). Rewritten to go through
  `BinaryReader`'s seek/read API, which works in both modes.

## Real BZip2 decompression (`bzip2_decode.c`)

`DemonDataWinIndex_decompressTexture` (`datawin_index.c`) now actually
decompresses a texture page's pixel data, not just validates its header
-- a from-scratch BZip2 decoder (Huffman decode, inverse Burrows-Wheeler
Transform, RLE1/RLE2) with no dependency on any external library.
Verified byte-identical to the reference `bunzip2` tool against every
one of the 9 real texture pages in DELTARUNE Chapter 1's data.win,
including multi-megabyte pages that span multiple BZip2 blocks, and
against synthetic random/repetitive/multi-block test vectors on the
host before ever touching real game data.

Decoding a single texture needs its own scratch buffer sized for the
stream's declared block-size level (`BZIP2_SCRATCH_BYTES`) -- worst
case (level 9) that's ~4.5 MiB on its own. Butterscotch's dynamic heap
arena (`BUTTERSCOTCH_ARENA_BYTES` in `core_main.c`) was bumped from
4 MiB to 12 MiB to fit that plus the largest real texture's compressed
and decompressed buffers, still comfortably under DemonOS's 24 MiB
per-process anonymous-heap ceiling.

## GameMaker's custom "QOI" pixel format (`qoi_decode.c`)

The BZip2-decompressed bytes aren't raw RGBA -- they're a second,
*different* compression layer: GameMaker's own custom QOI-variant image
codec, used for BZip2+QOI compressed texture groups (default since
GameMaker 2022.5, but present in this exact form in this 2018-era
bytecode-17 game too). This is not standard QOI: it has its own
12-byte header (magic `"fioq"` -- literally reversed "qoif", not a
byte-order artifact; uint16 LE width/height; uint32 LE compressed-data
length) and its own opcode set (`INDEX`/`RUN_8`/`RUN_16`/`DIFF_8`/
`DIFF_16`/`DIFF_24`/`COLOR`), different from real QOI's spec.

Found and implemented by locating the authoritative real decoder --
UndertaleModLib's `Util/QoiConverter.cs` (ported there from DogScepter)
-- and porting it field-for-field into freestanding C
(`ports/butterscotch/platform/qoi_decode.c`). Verified two ways before
touching the kernel: (1) a Python transliteration of the same reference
decoder, run against real decompressed texture pages and rendered to
PNG for visual inspection -- the images are genuinely, correctly the
game's own art (the "Field of Hopes and Dreams" room, card-suit enemy
sprites, full character sprite sheets), not noise; (2) the C
implementation cross-checked byte-for-byte against that Python
reference across all 9 real texture pages. The kernel build then
reproduced the exact same FNV-1a checksum as the host-side reference
for the same texture, end to end.

`DemonDataWinIndex_decodeTexturePixels` (`datawin_index.c`) chains both
layers: BZip2-decompress, then QOI-variant-decode, into real BGRA8
pixel bytes.

## Real room compositing (`DemonDataWinIndex_roomScene` + the room demo in `core_main.c`)

A real room now assembles correctly: `DemonDataWinIndex_roomScene`
resolves a room's placed instances (position, object, and -- following
object to sprite to TPAG page-item -- which texture page and source
rectangle each one draws from), and the probe decodes whichever real
texture pages those instances actually reference and blits each one's
source rectangle into a room-sized BGRA8 frame with alpha compositing.
No script execution is involved -- this is pure asset placement, using
data the ROOM/OBJT/SPRT/TPAG chunks already describe.

Room 54 (640x480, 16 placed instances, found by scanning all 147 real
rooms for one with a full scene and mostly-visible sprites) was used as
the proof case. Verified two ways: an independent Python
reimplementation of the same room/object/sprite/texture resolution
chain, rendered to PNG and visually confirmed as a real, recognizable
in-game scene (a character sprite and several real enemy sprites,
correctly positioned); and an exact FNV-1a checksum match between that
Python reference and the real kernel build once both iterated
instances in the same order (draw order matters when sprites overlap,
same as any painter's-algorithm compositor -- this is not a bug, just
something to hold constant between the two implementations being
compared).

Two more real, reproducible bugs were found and fixed along the way:

- **Sprite (SPRT) frame-list offset**: `DemonDataWinIndex_roomScene`
  assumed a sprite's texture-frame-count/pointer-list sits at
  `sprite+56`/`+60`. Checked against 8 real sprites, that location held
  a constant, meaningless value; the real GMS2-era SPRT record (with
  its extra bbox/origin/playback fields) has it at `+84`/`+88` instead
  -- confirmed by every one of those 8 sprites yielding a small,
  plausible frame count followed by exactly that many real, in-bounds
  TPAG pointers. Gated on `wadVersion == 17` like the earlier fixes, so
  `DemonDataWinIndex_roomScene` gained a `gen8` parameter to make that
  check possible (its one existing caller, the fixture's own room
  renderer, was updated to match and is unaffected since the fixture
  uses wadVersion 16).
- **Heap allocator fragmentation, not exhaustion**: decoding a large
  real texture page (BZip2-decompress into a QOI-encoded intermediate,
  then QOI-decode into a final RGBA buffer) failed for some pages but
  not others even with plenty of total free memory. Root cause: with
  the arena's simple coalescing free-list allocator
  (`user/portkit.c`), freeing a transient buffer that sits *before* a
  still-live one can't merge forward past it -- the freed space becomes
  a stranded island, unusable for a later, larger allocation even
  though the arena has enough free bytes overall. Fixed by allocating
  whichever buffer *outlives* a function first, so the buffers freed
  later always sit after it and merge forward into the untouched tail
  instead. `BUTTERSCOTCH_ARENA_BYTES` also had to go all the way to
  DemonOS's hard 24 MiB per-process ceiling (`USER_ANON_MAX_BYTES`) --
  DELTARUNE Chapter 1's largest real texture page's true peak memory
  need (frame buffer + BZip2 scratch + QOI intermediate + final RGBA,
  all real, simultaneously-live buffers) measures right around 24.18
  MiB even with zero fragmentation.

All fixes so far are narrowly gated (`wadVersion == 17`) rather than
wide version ranges, since this is the only real file verified against
so far -- widen them once a second real game confirms the actual
boundary.

## Real on-screen presentation

The composited room frame is now actually put on screen -- not just
checksummed. Deliberately *not* through DemonX/Xlib the way
`video.c`'s existing (fixture-only) windowing path works, which
requires a live DemonX server and therefore a full desktop/compositor
session: instead this uses the same raw `demon_display_submit` path
Doom and ClassiCube already use (see
`ports/classicube/platform/Window_DemonOS.c`), which works from the
plain `mako#` console with no desktop session required at all -- the
same environment every other test in this document runs in.

This came together cheaply because the composited frame is already in
the right shape: it's exactly 640x480 (the kernel's own display
resolution) and its per-pixel byte order is B,G,R,A -- which, read as
a native little-endian `uint32_t`, is `A<<24 | R<<16 | G<<8 | B`,
i.e. ARGB32 -- exactly the format `demon_display_submit` expects. No
pixel format conversion was needed.

Confirmed with a real QEMU screenshot (`screendump` via QMP) taken
while the frame was on screen, showing the correct scene: the same
character and enemy sprites, in the same positions, as the
independently-rendered reference PNG from the room-compositing work
above. The frame is held on screen for several seconds after a real
(non-scripted-boot-test) run so there's time to actually see it before
the process exits and MakoBox's console redraws over it; the
automated `boottest` self-test skips this step entirely since nothing
is watching the screen there.

## Bytecode scanning against real scripts (`DemonBytecode_scan`)

`bytecode_scan.c` implements a strict, allocation-free scanner over
GameMaker bytecode: it decodes every instruction in every real `CODE`
entry, rejects any opcode it doesn't recognize, and verifies every
branch (`B`/`BT`/`BF`/`PUSHENV`/`POPENV`) target lands exactly on a
real instruction boundary rather than into the middle of one. This
function existed since an earlier stage but had only ever been
exercised against the tiny 4-entry fixture package -- never against a
real game's scripts, which are far larger and more varied.

Run against Deltarune Chapter 1's real 1680 `CODE` entries, it failed
immediately on entry index 36: a 5912-byte script (`length=5912`, likely
a large dialogue/state-machine script) that has more than 128 branch
instructions. This traced back to `MAX_BRANCHES_PER_CODE`, a fixed
128-entry stack array the scanner used to buffer branch targets during
a single pass so it could validate all of them in one pass at the end.
That cap was an implementation artifact, not a real bytecode
constraint, and real scripts exceed it.

Fixed by removing the buffer entirely: each branch instruction's
target is now validated immediately, in place, the moment it's
decoded, rather than being collected for a later pass. `is_boundary`
is a self-contained function that independently re-walks the whole
instruction stream from offset 0, so calling it eagerly per branch
(instead of once per unique buffered target) is correct with no
capacity limit of any kind -- at some cost in redundant work for
scripts with many branches to the same target, which is negligible at
these script sizes (largest real script found: a few KB).

Verified on the host against the real `data.win` (via a small driver
program linking `datawin_index.c`/`bytecode_scan.c`/`bzip2_decode.c`/
`qoi_decode.c` directly, with a tiny host stub for
`demon_port_malloc`/`free` in place of the DemonOS syscalls those
files also use for texture decoding) and then in the actual kernel
build via QEMU, booting the real `deltarune-ch1` profile from
`build/kernel-butterscotch-games.iso`. Both runs report an identical
result byte-for-byte:

```
BUTTERSCOTCH_D6_BYTECODE_SCAN_OK entries=1680 instructions=419564 pushes=198118 arithmetic=34845 calls=29688 branches=35865 exits=3035
```

All 1680 real `CODE` entries decode cleanly with zero unknown opcodes,
and all 35865 real branch instructions validate as landing on genuine
instruction boundaries. This confirms the opcode set and instruction
widths in `bytecode_scan.c` match the real bytecode 17 encoding for
every instruction the actual game uses -- not just the fixture's
narrow sample. Actual VM *execution* of this bytecode (as opposed to
static validation) is the next milestone.

## Real VM execution: the GMS2.3/wad17 function-reference encoding change

`vm_fixture.c` implements a small, strict, fail-closed bytecode
interpreter (as opposed to `bytecode_scan.c`'s pure structural
validator): it actually runs instructions, maintains a value stack,
resolves variable/function references, and calls a tiny builtin
library. Like `DemonBytecode_scan` before Stage 6, it had only ever
been exercised against the 4-entry wad16 fixture.

Running it against the real Deltarune Chapter 1 file immediately
failed inside `build_references`, the function that walks the `VARI`
and `FUNC` chunks to resolve every reference to a real instruction
address. Two real bugs were found and fixed:

**1. `REFERENCE_MAX` was a 32-entry fixed array shared across the
entire file's variable and function reference table** (the same class
of bug as Stage 6's `MAX_BRANCHES_PER_CODE`) -- sized for the fixture,
nowhere near enough for a real file with 5178 variables and 741
functions.

**2. `FUNC`'s stored "first occurrence address" points at the wrong
byte for wad17.** Confirmed directly against real bytes: `FUNC[0]`'s
raw stored address (`0x257128`) decodes to opcode `0x00`, which does
not exist -- but `address - 4` (`0x257124`) decodes to a real `PUSH`
instruction. Cross-checked against UndertaleModTool's own C# source
(`UndertaleFunction.cs`): `if (writer.undertaleData.IsVersionAtLeast(2,
3)) writer.Write((addr == 0) ? 0 : (addr + 4)); // in GMS 2.3, it
points to the actual reference rather than the instruction`. GameMaker
Studio 2.3 (bytecode 17) changed function call sites to support
functions as first-class values (a function reference can be pushed
onto the stack and called indirectly, not just invoked directly by a
`CALL` with the function baked into its own operand), and that reworked
what `FUNC`'s address field points at. `VARI` is unaffected -- confirmed
against real data (`VARI[3]`'s raw address decodes correctly with no
adjustment) and consistent with `UndertaleVariable.cs`, which has no
such version-gated adjustment at all. Fixed by subtracting 4 from
`FUNC`'s stored address before walking its chain, gated on
`wadVersion == 17u`.

With both fixed, `build_references` now resolves the entire real
file's reference tables cleanly: 74667 variable references and 19948
function references, all validated as landing on in-bounds addresses
with sane chain deltas.

Execution then reached a real `PUSH` instruction with an operand type
(`Int32`) the interpreter didn't handle at all, added as a
straightforward literal-push case -- except that in GMS2.3, a
type-Int32 `PUSH` at a registered function-reference address doesn't
carry a literal integer, it carries the same chained reference field a
`CALL` does (this is exactly the "function as value" mechanism the
address-field change above exists for). Confirmed against real bytes:
`FUNC[0]`'s first occurrence is such a `PUSH`. Getting this wrong would
mean silently treating a function-reference word as a bogus integer,
which conflicts with this file's explicit fail-closed design
principle -- so the fix checks whether the current address resolves as
a function reference first, and fails closed rather than guessing if
so; plain integer literals elsewhere still push correctly.

All of this was verified with a standalone host build linking the real
`vm_fixture.c`/`datawin_index.c`/`bytecode_scan.c` sources directly
against the real `data.win`, and then confirmed with `make smoke`
(the wad16 fixture's `BUTTERSCOTCH_D3_VM_OK` assertion, unaffected by
the `wadVersion == 17u`-gated changes, still passes with identical
output).

**What's still missing before a real script can execute end to end:**
`call_builtin` only implements two functions (`show_debug_message`,
`keyboard_check`), each hardcoded to exactly one argument, and the
interpreter still has no support for indirect calls through a pushed
function value, no array/struct types, and only 8 flat instance-int
variable slots against GameMaker's real, much larger variable model.
Real scripts call hundreds of distinct builtins with varying arity.
This -- a real, wide builtin library plus a real variable/instance
model -- is the actual remaining scope of "make the game run," not a
handful of further bug fixes.

## Executing real user scripts: from "one bug" to a real interpreter

Getting the tiny GMS2.3 preamble fix above to actually run a real script
end to end required a chain of further fixes, each found the same way as
every prior stage: run the real thing, find exactly where it breaks, fix
that one thing, verify against real bytes, repeat.

**FUNC-table occurrence analysis.** Before touching any more code, the
real file's `FUNC` table was dumped and sorted by occurrence count. This
was the single most useful thing done in this stage: it showed that the
overwhelming majority of "function calls" in real scripts
(`gml_Script_scr_84_get_lang_string`, called 6254 times; `instance_create`,
1304; and dozens more) are not native engine builtins at all -- they are
the game's *own* compiled scripts, callable by name. Confirmed directly:
`gml_Script_scr_84_get_lang_string` (FUNC index, 6254 occurrences) matches
`CODE[560]`'s name byte-for-byte. This reframes "implement every builtin
GameMaker function" (genuinely months of work) into "make script-to-script
calls recurse into another already-decodable CODE entry" (a much smaller,
well-scoped problem) plus a real but far shorter list of true native
builtins (`instance_create`, `draw_sprite_ext`, `random`, `ds_map_add`,
math functions, and so on) -- not yet implemented, but now a bounded list
instead of an open-ended one.

**`REFERENCE_MAX`/`variables[8]` were both fixture-sized fixed arrays** --
the same class of bug as Stage 6's `MAX_BRANCHES_PER_CODE`. The real file
needs 5178 variable slots and 74667+19948 reference-chain entries, not 8
and 32. Rather than pick another arbitrary constant, `build_references`
now does a two-pass sizing: sum every real occurrence count first, then
`demon_port_malloc` the exact capacity needed, freed again via a new
`vm_destroy` on every exit path.

**A dynamically-typed variable store.** The very first thing a real
script does after its `method(self, <itself>)` preamble (see above) is
`DUP` the bound method and `POP` it into a variable -- storing a callable
*value*, not a number. The interpreter's variables were `int32_t`; they
are now full tagged `Value`s, matching GameMaker's actual dynamic typing.

**FUNC dispatch was by raw table index, not name -- a real, general
correctness bug, not a file-specific one.** `call_builtin` treated
`function == 0` as `show_debug_message` and `function == 1` as
`keyboard_check`, which only ever happened to be true for the tiny
synthetic fixture's own table layout. In the real file, index 1 is
`method`. FUNC indices are positions in *that file's* table, not a stable
ID -- dispatch now resolves and compares the actual name string.

**`method(scope, func)`** is a real, well-defined GameMaker builtin (not
a hack): every GMS2.3-compiled script opens by binding itself as a
callable value this way. Implemented as: drop `scope` (this interpreter
has no multi-instance model yet, so there is only ever one active scope)
and forward `func` -- correct for the universal self-binding preamble
this file actually contains, confirmed against real bytecode traces of
several different real scripts (`down_p`, `up_p`, `ossafe_init`, and
others spanning completely different game systems).

**A script may legally end with one value on the stack.** Event code
always balances to an empty stack; a script's implicit return value
(no explicit `return`) does not. Confirmed against a real script
(`gml_Script_down_p`) that ends exactly this way. Relaxed the top-level
balance check from "exactly 0" to "0 or 1" accordingly.

**A latent, independent, pre-existing bug, only surfaced by a real
1680-entry file:** `codeMask` is a `uint32_t` bitmask, but the code that
walks it looped `i` all the way to `count` (not capped at 32), so
`1u << i` for `i >= 32` is undefined behavior -- on this platform it
wraps modulo 32, meaning bits for entries 1/6/22 spuriously re-matched
at i=33/38/54/... and silently re-executed unrelated CODE entries far
beyond the ones actually requested. The tiny 4-entry fixture never had
`count > 32`, so this had never fired before. Fixed by capping the walk
at `min(count, 32)`.

With all of the above, three small real scripts (`gml_Script_scr_debug_save_trophies`,
`gml_Script_ossafe_init`, `gml_Script_scr_test_save_data`) now run their
entire compiled bytecode correctly through the public
`DemonVm_executeEventsState` API -- verified identically on the host and
in the actual kernel via QEMU:

```
BUTTERSCOTCH_D8_VM_SCRIPT_OK entries=3 instructions=30 builtins=3
```

**What's still missing for real gameplay:** these three scripts only
exercise their shared compiled preamble. The actual body of most real
scripts lives past an unconditional branch this preamble jumps over,
reachable only via a real script-to-script *call* (the FUNC-occurrence
analysis above shows this is the dominant case, not native builtins) --
which needs actual call-frame support (arguments, locals, a return
value threaded back to the caller) that does not exist yet. Beyond that,
a real but now bounded list of native builtins, and an object/instance
model, remain before any actual gameplay logic can run.

## Why Undertale Demo is the second target

Spelunky Classic was considered first, but the original release is a Game
Maker 8 executable rather than the modern standalone `data.win` format parsed
by Butterscotch. Undertale Demo is therefore the useful second target: it
shares the relevant GameMaker Studio family while providing a different real
resource graph from DELTARUNE.
