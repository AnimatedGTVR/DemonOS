# Better MAKO in userspace

## Scope

This is a roadmap for improving native MKO itself as a userspace application
language on DemonOS, not a new port. Today's MKO userspace story is real but
thin: `user/sdk.mko` gives typed wrappers over every MAKO-ABI syscall
(console, ticks, capabilities, files, IPC, surfaces, input), `user/init.mko`
drives process/service lifecycle, and the compositor and its protocol clients
under `sidelined/user/*.mko` show MKO carrying real ring-3 application logic
(window creation, IPC waitsets, damage tracking). The gap is everything around
that core: ergonomics, a real standard library, and tooling — the difference
between "MKO can technically do this" and "writing an MKO app doesn't feel
worse than writing a C one."

## Why this, and why now

Every native port so far (FreeDOOM, ClassiCube, Quake) is C, ported onto
PortKit by hand. MKO is DemonOS's own language and the intended long-term
target for first-party apps and services (see the "native bridge" section of
the top-level README) — the SDK, init, and the sidelined compositor are proof
it can carry non-trivial applications. But the sidelined desktop shell (file
manager, settings, DemonWM) shows a pattern worth noticing: as soon as an app
outgrows a small utility, its authors reach for C or C++ instead of MKO. That
is a userspace-ergonomics gap, not a capability gap, and it is worth closing
before more first-party apps are built, rather than after.

## Current state

| Area | State |
|---|---|
| Typed MAKO-ABI wrappers | Complete — `user/sdk.mko` covers console/tick/yield/exit, capabilities, files, spawn/wait, IPC channels, surfaces, unified input |
| Process/service lifecycle | `user/init.mko` — real init written in MKO, not C |
| Non-trivial app logic in MKO | Demonstrated by the sidelined MKO compositor and its protocol clients (window creation/move/crash/event clients, `browser_client.mko`, `counter_client.mko`, `terminal_client.mko`) |
| Standard library | None beyond the raw ABI wrappers — no collections, string/formatting helpers, or error-handling convention shared across MKO apps |
| Tooling | `make project` builds one starter app against the SDK; no MKO-native package/module story beyond that |
| Debuggability | Serial `_OK`/`_FAIL` markers per boot stage; no MKO-level stack traces or symbolized panics |

## Milestones

### U0 — stdlib survey and gap list (implemented)

Read every existing `.mko` file (`user/`, `sidelined/user/`, `mako/`,
`projects/hello/`) and grepped for repetition across them. The findings
below are the actual scope for U1 — concrete duplicated logic, not a
speculative "what would be nice" list.

#### Finding 1: the 32-bit pack/unpack pair is reimplemented four times

`fn low32(value: u64) -> u64 { return value - (value / 4294967296) * 4294967296; }`
appears **byte-for-byte identical** in `sidelined/user/counter_client.mko`,
`sidelined/user/browser_client.mko`, `sidelined/user/terminal_client.mko`,
and `sidelined/user/compositor.mko` — four copies of the same six-line
function, because MKO has no native way to extract the low 32 bits of a u64
(no bitwise AND, no truncating cast). Its inverse — packing two 32-bit
halves into one u64 — is reimplemented **three more times** under two
different names: `fn pack_words(low, high)` in `demonx_client.mko`,
`browser_client.mko`, and `compositor.mko`, plus a fourth variant
`fn user_u64(high, low)` in `user/init.mko` with the argument order
silently flipped relative to `pack_words`. This is exactly the kind of
duplication a stdlib's integer-packing helpers should own — and the
inconsistent argument order across the four reimplementations is itself a
live footgun this survey caught.

#### Finding 2: every IPC client hand-builds the same 64-byte packet header

The identical 4-line "zero-fill 8 words" loop —
```
word: u64 = 0;
while word < 8 {
    vptr_write_u64(vptr_offset_u64(packet, word), 0);
    word = word + 1;
}
```
appears in **seven** files: `window_move_client.mko`, `demonx_client.mko`,
`window_client.mko`, `browser_client.mko`, `window_crash_client.mko`,
`counter_client.mko`, `terminal_client.mko`. Three of those
(`window_client.mko`, `counter_client.mko`, `window_crash_client.mko`) go
further and duplicate the *entire* CREATE-packet-plus-2x2-tiling-math block
near-verbatim (version/opcode encoding, `cell_index`/`column`/`row`/`x`/`y`
derivation from pid). A packet-builder helper (zero, write header fields,
send) would collapse this into one call site per client instead of a
hand-copied block per client.

#### Finding 3: no string/byte-literal support at all

MKO has no way to write a string or byte literal — every fixed byte
sequence is built one `vptr_write_u8` call per byte. `write_event_name`
(building the literal bytes of `"desktop.win<pid>"`) is reimplemented
separately in `counter_client.mko` and `browser_client.mko` this way — 12
separate `vptr_write_u8` calls each, for a 12-character constant string.
`user/init.mko` goes a different, arguably worse route for the same
underlying need (encoding a fixed byte string as data): it hand-encodes two
ASCII strings as raw decimal `u64` literals (`user_u64(1162625601,
1414680400)` etc.) with no comment-adjacent indication of what text they
actually spell out. Either representation is opaque and error-prone by
hand; a real string-literal syntax lowering to the same
`vptr_write_u8`/word-packed bytes is the highest-leverage single item on
this list — it would also let Finding 2's packet builder take a `service
name` as an actual string instead of a raw address+length pair the caller
assembled by hand.

#### Finding 4: hand-rolled memset/memcpy and a from-scratch bitmap font, twice

`browser_client.mko` defines its own `clear_bytes`/`copy_bytes`
(byte-at-a-time zero/copy loops — the stdlib gap U1 already names as
"string/formatting helpers" should really be framed as "byte-buffer
helpers," since none of this is text-string-specific). Separately,
`browser_client.mko` (`glyph_mask`/`draw_character`/`draw_text`) and
`compositor.mko` (`draw_glyph`) each implement their own from-scratch
bitmap-font rendering rather than sharing one. `browser_client.mko` also
has a small fixed-capacity record store (`history_store`/`history_restore`
for browser back/forward history) — a bounded array of fixed-size records,
the same general shape U1's "bounded collections" item already targets, now
with a concrete real user.

#### Finding 5: no consistent error-handling convention (as U1 already predicted)

Confirmed in practice, not just predicted: call sites alternate between
`if MakoAbi.some_call(...) != 64 { MakoAbi.exit(94); return 94; }` (exact
expected-byte-count checks) and `if handle <= 4294967295 { ... }` (the
"is this handle valid" idiom, since failed opens return a sentinel above
that range) with no shared name or helper for either — each call site
reinvents both the check and its own numeric exit code. A `MakoAbi`-level
`must(condition, exit_code)` helper and a named `handle_valid(handle)`
predicate would remove both classes of hand-copied boilerplate at once.

#### What U1 should actually build, in priority order (by how many call
sites each removes)

1. ~~String/byte literals (Finding 3)~~ — **implemented**, see below.
2. ~~32-bit pack/unpack helpers (Finding 1)~~ — **implemented**, see below.
3. ~~Packet-builder helper for the compositor wire protocol (Finding 2)~~ —
   **implemented**, see below.
4. ~~`must()`/`handle_valid()` error-handling convention (Finding 5)~~ —
   **implemented**, see below.
5. ~~Byte-buffer helpers (Finding 4, buffer half)~~ — **implemented**, see
   below. The shared bitmap-font renderer half of Finding 4 is
   **deliberately not done** — see the note at the end of this section.

#### Finding 3 fix — string literals as compile-time byte-blob addresses (implemented)

This required real compiler work in the separate MAKO repo
(`../MAKO`/`src/Mako`), not just a KernelProject-side change, since the
kernel/freestanding profile deliberately has no runtime string type
(`docs/systems-language.md`: *"It rejects runtime-dependent features such
as packages, strings, ... "*). The fix stays inside that boundary rather
than widening it:

- A string literal directly assigned/passed/returned into a `u64`/`usize`
  position now compiles — its value is the address of a `.rodata` byte blob
  (no NUL terminator; every existing MAKO-ABI call already takes an
  explicit length, matching the codebase's actual convention). Any other
  use (concatenation, comparison, a `u8`-typed binding, a bare expression
  statement) is still rejected exactly as before — this is not general
  string support, on purpose.
- `SystemsTypeChecker.Assignable` (`src/Mako/SystemsTypeChecker.cs`) gained
  one narrow, `kernelProfile`-gated exception: a bare `StringLit` is
  assignable to `u64`/`usize`. `kernelProfile` is a new parameter threaded
  through `Analyze`/`Check` from `Program.cs`'s two `--kernel` call sites
  only — the general/interpreted checker path is completely unaffected.
- `KernelProfileChecker.CheckExpression` lets a `StringLit` bypass the
  generic "string type is unsupported" rejection *only for the literal
  node itself*; anything built out of one (e.g. `"a" + "b"`) still has
  overall inferred type `"string"` and is still caught by the existing
  check.
- `Mir.cs`'s `Coerce()` labels the fallback case `"convert.identity"`
  (previously bare `"convert"`, which nothing produced before and would
  have been rejected as unsupported at emission anyway) — the string's
  address doesn't need any real conversion, just a type reinterpretation.
- `X64AssemblyEmitter.cs` gained a `StringLiteralPool` (dedupes identical
  literals to one blob across the whole program) and lowers a
  `string`-typed `const` to `lea rax, [rip + label]` instead of an
  immediate `mov`, plus one `.section .rodata` emission pass at the end
  covering every literal used across all functions.

Verified: all 30 existing MAKO tests still pass (`mko test tests`); the
real `mako/kernel_probe.mko` → `build/kernel_probe.S` → `build/kernel.elf`
pipeline (the Makefile's actual `dotnet run --project ../MAKO/src/Mako --
native ... --kernel` invocation) still builds unchanged; and
`sidelined/user/counter_client.mko`/`browser_client.mko`'s
`write_event_name` (Finding 3's original example — 11 hand-written
`vptr_write_u8` calls building `"desktop.win"` one byte at a time) now
reads the prefix from a real string literal, checked and compiled cleanly
through the real `--kernel` pipeline and confirmed to assemble and link.
Deferred: a `string_len(<literal>)` compile-time-constant builtin — every
real call site today already supplies its byte count as a separate,
manually-counted integer literal (unchanged practice), so it wasn't needed
to ship this fix; worth adding later if it turns out to remove real
duplication rather than just being satisfying to have.

#### Finding 1 fix — one shared `MakoStd.low32`/`MakoStd.pack32` (implemented)

`user/mako_std.mko` (`namespace MakoStd;`) replaces the four hand-copied
reimplementations with one `low32(value)` and one `pack32(low, high)`,
using the majority argument order (three `pack_words(low, high)` call
sites) rather than `user_u64`'s inverted `(high, low)`.

- `user/init.mko` — the one live, actively-built copy of this duplication —
  now imports it (`use "mako_std.mko";`) and calls `MakoStd.pack32(low,
  high)` with arguments swapped to match the new order, not just renamed.
  Verified two ways: `mko check --kernel` reports no issues, and the real
  `user/init.mko` → `build/user_init_mko.S` → `build/user_program.elf` →
  `build/kernel.elf` pipeline builds unchanged and **boots correctly** —
  `USERSPACE_SYSCALLS_OK` requires `user_project_valid()` (the function
  that now calls `MakoStd.pack32`) to reconstruct the exact same two
  expected `u64` values from swapped arguments and compare them
  byte-for-byte against real RAMFS-round-tripped data, so this is a real
  runtime confirmation the argument swap was done correctly, not just a
  compile-time check. (Confirmed via a manually-staged boot ISO, since
  `make build/kernel.iso` currently fails downstream at an unrelated,
  in-progress `quake-core.elf` link error — not something this fix touched
  or introduced.)
- `sidelined/user/counter_client.mko` also migrated (`use
  "../../user/mako_std.mko";`, proving the shared module resolves
  correctly from a different directory), `mko check --kernel` clean.

Also migrated: `browser_client.mko`'s own `low32`/`pack_words` (60+ call
sites, mechanical rename since `pack_words(low, high)`'s argument order
already matched `MakoStd.pack32`'s) — see below. Not yet migrated:
`terminal_client.mko`'s `low32` and `compositor.mko`'s `low32`/
`pack_words`, plus `demonx_client.mko`'s remaining `pack_words` call sites.
All of this is parked, not-currently-built `sidelined/` code (dead since
the GUI/compositor stack was sidelined for the TTY-only pivot) — the
mechanical migration is the same pattern demonstrated above. Worth
finishing in one pass whenever/if that code is reactivated, not before.

#### Finding 2 fix — `MakoWire.zero_packet`/`MakoWire.header` (implemented)

`user/mako_wire.mko` (`namespace MakoWire;`, imports `mako_std.mko` for
`pack32`): `zero_packet(packet_address)` replaces the 4-line "zero 8 words"
loop duplicated in seven files; `header(packet_address, version, opcode,
serial)` replaces the packed-literal pattern (e.g. `131073 + 4294967296`
for "version 1, CREATE opcode 2, serial 1" becomes `MakoWire.header(addr,
1, 2, 1)`) — decoded from the existing scheme (version in bits 0-15, opcode
in bits 16-31, serial in bits 32-63) rather than guessed. Not a general
"packet builder" covering every field (geometry/window-id words still use
`MakoStd.pack32` directly) — just the two truly-duplicated pieces.

#### Finding 5 fix — `MakoStd.require_bytes`/`MakoStd.handle_valid` (implemented)

Added to `mako_std.mko` (which gained `use "sdk.mko";` for this, since
`require_bytes` calls `MakoAbi.exit` — verified the diamond import
resolves to one copy of `sdk.mko`, not a duplicate-symbol error, by reading
`NativeModuleLoader.cs`'s `loaded`/`active` path-dedup logic before relying
on it). `require_bytes(actual, expected, exit_code)` collapses `if
MakoAbi.some_call(...) != N { MakoAbi.exit(code); return code; }` into one
call — callers don't need their own `return` after it since `MakoAbi.exit`
never returns in practice. `handle_valid(handle)` names the
`<= 4294967295` sentinel-range check instead of every call site repeating
the magic number.

#### Finding 4 fix — `MakoBytes.zero`/`MakoBytes.copy`/`MakoBytes.byte_at` (implemented, buffer half only)

`user/mako_bytes.mko`: `browser_client.mko`'s `clear_bytes`/`copy_bytes`/
`get_byte` (named as if string-specific but actually generic byte-buffer
operations — Finding 4's own note) moved here and renamed to match what
they actually do, with all ~14 call sites in `browser_client.mko` updated.

**Deliberately not done: unifying the two bitmap-font renderers.**
`browser_client.mko` (`glyph_mask`/`draw_character`/`draw_text`) and
`compositor.mko` (`draw_glyph`) each hand-roll their own from-scratch
bitmap font. Merging them was in scope for Finding 4, but doing it blind —
without a way to visually compare the two glyph sets pixel-by-pixel, which
isn't possible for parked code with no boot test — risks a silent visual
regression neither this session nor the user could catch. Worth doing once
either file is reactivated and can actually be looked at rendering on
screen, not as a paper refactor now.

#### What's verified vs. not, for all of Findings 1/2/4/5

Every new/changed file passed `mko check --kernel` (clean) and `mko native
--kernel -o *.S` (emits valid assembly, confirmed to assemble with the same
freestanding `gcc` flags this project's Makefile uses) — including
`window_client.mko`, rewritten end-to-end onto `MakoWire`+`MakoStd` as a
combined proof all four fixes compose together in one real file. None of
this reaches a real boot test, because none of `sidelined/user/*.mko` is
in the active build (confirmed: only `mako/kernel_probe.mko` and
`user/init.mko` are). That's a materially weaker verification bar than
Findings 1 (partial: `user/init.mko`, boot-tested) and 3 (boot-tested) got,
and is why this work was gated behind an explicit choice rather than done
by default.

### U1 — a real MKO standard library

A `mako/std/` (or equivalent) module covering the gaps found in U0: bounded
collections (the fixed-capacity kind PortKit itself favors — see
`RAMFS_FILES`/`QUAKE_MAX_HANDLES`-style bounded arrays elsewhere in this
codebase — not unbounded generic containers), string/formatting helpers for
console and log output, and one consistent error-handling convention (result
type or errno-style, matching whatever MAKO-ABI already returns via `UINT64_MAX`
sentinels) instead of each app inventing its own.

### U2 — ergonomics pass on the SDK surface

Revisit `user/sdk.mko` itself once U1 exists: are the raw wrappers composable
with the new stdlib, or does every real app still reimplement a thin
convenience layer on top of them (as the compositor and its clients do today)?
Fold any such repeated layer back into the SDK.

### U3 — tooling: more than one project

`make project` currently proves the SDK against a single starter app. This
milestone is about N apps: a documented, minimal project layout (mirroring
`docs/c-apps.md`'s three-piece C app pattern) so a new MKO app doesn't need to
reverse-engineer `projects/hello` or the sidelined compositor sources to get
started.

### U4 — debuggability

MKO-level panics/asserts that map back to source locations, at least in the
`make check`-style headless test harness, instead of relying purely on manual
`_OK`/`_FAIL` markers threaded through each app by hand.

## Non-goals

- Rewriting existing native C ports (FreeDOOM/ClassiCube/Quake) in MKO — those
  stay C, matching upstream, per `docs/native-porting.md`.
- An unbounded/dynamic-allocation-heavy stdlib — DemonOS's fixed, auditable
  budgets (RAMFS file slots, per-process heap/stack pages, PortKit's static
  arena) are a deliberate design choice elsewhere in this codebase; the MKO
  stdlib should reflect that, not import GC'd or heap-hungry idioms.
