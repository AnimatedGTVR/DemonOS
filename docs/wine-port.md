# Wine (Windows compatibility) port

## Scope, honestly

This is **not** a port in the same sense as Quake/FreeDOOM/ClassiCube. Those
are self-contained game engines with minimal OS assumptions — the D0-style
"fetch pinned upstream, compile it unchanged against a platform shim" model
worked because the engine itself barely touches the platform. Wine is the
opposite: it *is* an operating-system compatibility layer. Upstream Wine's C
source assumes a working POSIX host underneath it — `fork`/`exec`, a real
`mmap` with reserve/commit semantics across a large address space, threads,
signals, sockets, dynamic linking (`dlopen`), and historically an X11 (or
now Wayland) display server — to implement Win32 on top of. DemonOS has
none of that yet, and `docs/native-porting.md` is explicit that what it
does have (`syscalls 40/41`'s single 24 MiB on-demand anonymous arena) is
"deliberately not advertised as full POSIX `mmap`."

Compiling real upstream Wine source unmodified against a platform shim,
the way Quake's `ports/quake/platform/` does, is not realistic any time
soon — it would need this kernel to grow a real process/thread/virtual-memory
model first. This doc is scoped accordingly: it starts from "what would
even need to exist," not from a pinned Wine commit and a fetch script.

## Why this, and why now

The user asked for it directly, with Wine named alongside a possible future
Xorg/Wayland port as long-term goals for this kernel (see
[[kernelproject-demonos]] memory: "planned after Quake milestones land").
Given the honest scope above, the right first move is the same kind of
foundational, provable step every other port here started with — prove the
one piece of the problem that's actually tractable today (parsing the
foreign binary format), rather than either refusing to start or pretending
a full port is imminent.

## Current DemonOS readiness

| Requirement | State | Gap |
|---|---|---|
| PE/COFF executable format parsing | **Available (W0)** | `ports/wine/platform/pe.c` |
| Large reserve/commit virtual memory | Not available | Only a single ≤24 MiB on-demand anonymous arena (syscalls 40/41), no reserve-without-commit |
| Threads | Not available | `docs/kernel-cxx-port.md`/`docs/quake-port.md` already note the kernel has no general userspace threading ABI |
| Dynamic linking / DLL loading | Not available | ELF64 loader only; no PE import-table resolution, no relocation processing |
| Win32 API surface (kernel32/ntdll/user32/gdi32/...) | Not available | This is most of what "Wine" actually *is* — millions of lines, not started |
| POSIX-like syscall layer Wine's own build assumes | Not available | No `fork`/`exec`/signals/sockets; DemonOS's own capability-based syscall ABI is a different model entirely |
| Display server for a Win32 GUI surface | Not available | The GUI/compositor stack is currently sidelined (TTY-only pivot); would also gate any real Xorg/Wayland port |

## Milestones

### W0 — PE/COFF header parsing (implemented)

`ports/wine/platform/pe.c`/`pe.h`: a real PE32+ (x86-64 only — PE32/32-bit
is deliberately out of scope, matching this kernel's own architecture)
header parser, following the exact discipline `apps/doom/wad.c` already
established for foreign binary formats in this codebase: decode
fixed-width little-endian fields explicitly, prove every offset+size stays
inside the file before trusting it, no allocation. Validates the DOS
header, `"PE\0\0"` signature, COFF header, PE32+ optional header magic, and
every section header's file-backed range.

`apps/wine_pe_probe/` (`main.c`+`entry.S`, built via `make wine-pe-check`,
standalone — not yet wired into the main `$(ISO)`, see below) proves it
against **synthetic, self-authored fixtures built at runtime**, not a real
Windows or Wine binary — the same legal hygiene as FreeDOOM standing in for
DOOM elsewhere in this codebase. Covers: a structurally valid minimal
PE32+ image, a corrupted DOS magic, a corrupted PE signature, and a section
table whose declared raw-data range runs past a truncated file. Verified
via a real boot-time ring-3 spawn (temporarily added to `kernel.c`,
confirmed `WINE_PE_PROBE_READY dos-header pe-signature coff-header
optional-header section-table` with exit status 0, then reverted — this
milestone is a standalone smoke check, not part of the permanent boot
sequence yet).

Not wired into `$(ISO)`/`(check)` yet: intentional, to avoid touching the
shared ISO target and boot sequence while Quake work is active in the same
tree (see [[kernelproject-demonos]]) — `make wine-pe-check` builds and
links the ELF standalone.

### W1 — reserve/commit virtual memory (implemented, smaller than real demand paging)

Investigation first (as planned): `src/arch/x86_64/interrupts.c`'s
`interrupt_page_fault_handler` is `__attribute__((noreturn))` and ends in
`for (;;) __asm__ volatile ("cli; hlt");` — it is diagnostic-only and
cannot resume a faulting instruction. Real `VirtualAlloc(MEM_RESERVE)` +
commit-on-touch semantics need a *recoverable* page fault handler (commit a
frame and `iretq` back on a fault inside a reserved-but-uncommitted range,
while keeping every other fault fatal exactly as today) — genuine
exception-handling surgery on a shared, safety-critical path, qualitatively
riskier than anything else built in this repo's C++/MKO-compiler work.
Given that, the explicit choice made here was the smaller, safer feature
instead: **an explicit `commit()` syscall, no page-fault handler changes.**
This does not give real demand-paging semantics (nothing commits itself on
first touch), but is fully verifiable independently and carries none of the
risk of touching interrupt dispatch.

**Implementation** (`src/arch/x86_64/userspace.c`): `anonymous_reserved_pages[pid]`
sits alongside the existing `anonymous_page_count[pid]` (which keeps its
meaning: pages actually committed). `anonymous_reserve(pid, byte_count)` is
pure bookkeeping — no frame allocation, no page table entries at all, so it
costs nothing until committed. `anonymous_commit(pid, byte_count)` extends
the committed region by exactly that many more bytes, always starting where
the previous commit (or the reservation itself, on the first call) left
off — never sparse, never out of order — and returns the address the new
bytes start at. The original all-at-once `anonymous_map` (syscall 40, used
by `apps/portcheck` and `apps/doom` today, unchanged) now shares its
per-page mapping loop with the new path via an extracted
`anonymous_commit_pages(pid, start_page, end_page)` helper — correct for
any contiguous, monotonically increasing range because the loop always
walks every intermediate page one at a time, so the "allocate this 2MB
chunk's page table" condition still fires at the right index regardless of
where a given call's range starts. `reserve`/`commit` are mutually
exclusive with the old `map` on the same process (still one anonymous
region per process, just two ways to fill it), and `unmap` (syscall 41)
now also releases a reservation that was never committed into.

New syscalls 46 (`memory_reserve`) and 47 (`memory_commit`); C wrappers
`demon_memory_reserve`/`demon_memory_commit` in `include/demon/c_app.h`,
mirroring the existing `demon_memory_map`/`demon_memory_unmap`.

**The sharp edge, stated where any future caller will actually read it**
(the field comment on `anonymous_reserved_pages`, the function comment on
`anonymous_commit`, and the doc comment on `demon_memory_commit`): a
reserved-but-uncommitted page is genuinely unmapped. Touching it doesn't
crash the offending process — it halts the entire kernel, because the page
fault handler that would catch it can't recover. Any future caller
(a real PE loader, eventually) must track its own committed high-water
mark and never read or write past it.

**Verified**: `make mem-reserve-check` builds `build/mem_reserve_check.elf`
standalone (same pattern as W0's `wine-pe-check`, not wired into the shared
`$(ISO)`). Boot-tested the same way as W0 (temporarily spawned from
`kernel.c`, confirmed, reverted): reserve-then-commit-in-two-steps with
real read/write verification that the two committed pages are genuinely
distinct physical frames (not the same page mapped twice), an
overrun-rejected commit that doesn't corrupt the high-water mark (a
further small commit after the rejected one still succeeds, at the
correct address), a reserve-only release with nothing ever committed, and
reuse of the old all-at-once `demon_memory_map` path after a full release
— all passed on the first real boot attempt, exit status 0.

### W2 — minimal PE loader (headers-only, no imports; permission enforcement deferred)

`ports/wine/platform/pe_load.c`/`pe_load.h`: `wine_pe_load(file, image)`
validates a PE32+ image (W0), reserves `SizeOfImage` bytes (W1), then walks
its section table committing, zero-filling, and copying each section's raw
file data into place — one `demon_memory_commit` call per section (covering
any alignment gap before it too), so a section's memory is always genuinely
backed by real, distinct physical frames before anything reads or writes it.
Sections are required to appear in non-decreasing `VirtualAddress` order and
stay inside the declared `SizeOfImage`; a violation is rejected rather than
silently reordered or clamped — real invariants every linker upholds, not
arbitrary strictness.

**Permission enforcement (read-only sections, execute-vs-data) is
deliberately NOT done in this pass — found out why the hard way, not
guessed in advance.** The original plan (per this milestone's earlier
description) was to commit each section with its own
`IMAGE_SCN_MEM_WRITE`-derived permission. The first real boot test of this
hit an immediate, real page fault: `.text` (correctly non-writable per its
characteristics) was committed read-only, and the loader's own zero-fill
step then tried to *write* zeros into it before copying — because
populating a section during loading requires writing into it regardless of
its final intended permission. Downgrading a range to read-only *after*
populating it needs an `mprotect`-equivalent syscall this kernel doesn't
have (walking already-committed page tables to flip the write bit, plus a
TLB invalidate) — real, bounded kernel work, just not this pass's scope.
Every section is committed writable for now; a section's `Characteristics`
is read during loading but not retained anywhere afterward, so a future
protect-after-populate step would need to re-derive it. Execute-vs-data
enforcement stays out of scope for the same NXE reason W1 already
documented.

Verified via `apps/wine_pe_load_check/` (`make wine-pe-load-check`,
boot-tested the same way as W0/W1: temporarily spawned from `kernel.c`,
confirmed `WINE_PE_LOAD_OK base entry text-data zero-fill writable-section
unmap`, reverted): a synthetic two-section PE32+ (a `.text`-like section
and a `.data`-like section, both self-authored fixture bytes, same legal
hygiene as W0) loads at the correct base and entry point, each section's
raw bytes land at the right virtual address with the tail past
`SizeOfRawData` genuinely zero-filled (not garbage), the writable section
accepts a real write, and unmapping the whole image afterward succeeds.

One more bug caught by this milestone's own boot test, unrelated to
permissions: the test app's first draft used a 256 KiB static array as
`demon_port_init`'s arena. A file-scope static array becomes part of the
ELF's `.bss` and is counted in the loaded image's memory size — this
inflated the executable to ~262 KiB, silently exceeding every process
slot's capacity (checked with `readelf -l`), so `userspace_spawn_path`
failed every candidate slot and returned pid 0 with no other diagnostic.
Fixed by using a 256-byte arena (the test never allocates from it — it
only needs `demon_port_init`'s storage-capability side effect, not a real
heap) instead of `demon_port_init_dynamic`, which itself uses
`demon_memory_map` (syscall 40) and would have conflicted with this test's
own use of `demon_memory_reserve`/`commit` on the same single-region model
anyway.

### W3 — export/import resolution mechanism (implemented; real DLL loading needs more)

`ports/wine/platform/pe_export.c`/`pe_export.h`: `wine_pe_read_exports`
reads an already-loaded image's `IMAGE_EXPORT_DIRECTORY` (the export data
directory W3 added to `wine_pe_info`/`pe.c` — parsed from the file at
validate time, since `wine_pe_validate` never required the full 240-byte
optional header before; W0/W2's original 112-byte fixtures still validate
fine, absent directories just read as zero); `wine_pe_resolve_export` does
a linear by-name search over it. `ports/wine/platform/pe_import.c`/
`pe_import.h`: `wine_pe_resolve_imports` walks an image's import
descriptor table, resolves each name-based thunk against a caller-supplied
export table, and patches the resolved address directly into the image's
Import Address Table. Ordinal-only imports (thunk's top bit set) are
rejected outright — name-based resolution only, in this pass.

**This proves the resolution mechanism, not real DLL loading — and finding
out precisely where the gap is was the actual point of this milestone.**
There is no real DLL to export anything a DemonOS program would want
(`kernel32.dll`, `ntdll.dll`, ... — each individually larger than this
whole codebase); W3's test instead builds two synthetic, self-authored
PE32+ images (a fake "exporter" with one named export, a fake "importer"
that imports it by name — same legal hygiene as every other fixture in
this port) and proves the by-name resolution + IAT-patch mechanism against
them in isolation.

**Real architectural finding, hit while writing the two-image test, not
predicted in advance:** loading an EXE and at least one DLL into the same
process — the single most basic real DLL-loading scenario — needs both
images' memory to coexist in one address space. W1/W2's original design
(`wine_pe_load` calling `demon_memory_reserve` for itself) allows exactly
one reservation per process, so it could only ever load one image per
process, full stop — there was no way to load a second image at all,
because the single-region model has nowhere to put it. Fixed by changing
`wine_pe_load`'s signature to take an explicit `base` address instead of
reserving for itself: the caller now reserves once, sized for everything
it plans to load, and calls `wine_pe_load` once per image with a distinct,
non-overlapping base inside that one reservation. `apps/wine_pe_load_check`
(W2's existing test) was updated to do its own single-image reserve
accordingly, and still passes unmodified otherwise.

Verified via `apps/wine_pe_import_check/` (`make wine-pe-import-check`,
boot-tested the same way as W0-W2): one reservation sized for both
synthetic images; the exporter loads and its export directory reads back
exactly one name; resolving that name returns the correct absolute address
(image base + the placeholder function's RVA); resolving a nonexistent
name correctly returns not-found; the importer loads at a second base
within the same reservation; resolving its imports against the exporter's
table succeeds; and — the actual proof — reading the importer's IAT slot
directly out of memory this test never wrote to itself confirms
`wine_pe_resolve_imports` patched in exactly the exporter's resolved
address. Exit status 0 on the first real boot attempt after the
architecture fix above.

### W4 — base relocations (implemented)

`ports/wine/platform/pe_reloc.c`/`pe_reloc.h`: `wine_pe_apply_relocations`
walks an already-loaded image's `IMAGE_BASE_RELOCATION` blocks (the reloc
data directory, entry 5, added to `wine_pe_info`/`pe.c` the same way W3
added export/import — `pe.c`'s directory read widened from 16 bytes/2
entries to the full standard 128-byte/16-directory array in one read, since
adding a third directory index one at a time wasn't worth another size
threshold; W0/W2's original 112-byte fixtures are unaffected, still below
even the old threshold) and adjusts every `IMAGE_REL_BASED_DIR64` (PE32+'s
64-bit absolute relocation type) entry by `image->base - image->info.image_base`
— the difference between where the image actually landed (W1's reservation)
and the preferred `ImageBase` its linker assumed. `IMAGE_REL_BASED_ABSOLUTE`
(a documented no-op padding entry) is skipped; any other relocation type is
rejected outright rather than silently mishandled — this port only ever
targets x86-64, so only the one 64-bit absolute type is in scope.

This is real, necessary machinery, not optional polish: DemonOS's anonymous
region (`USER_ANON_BASE`, `0x10000000`) essentially never coincides with a
real Windows binary's preferred `ImageBase` (commonly `0x140000000` for a
64-bit EXE), so every absolute pointer a real linker baked in would be
wrong without this pass.

Verified via `apps/wine_pe_reloc_check/` (`make wine-pe-reloc-check`,
boot-tested the same way as W0-W3): a synthetic image with one baked-in
"absolute pointer" (as if computed at link time against the preferred
`ImageBase`) and a real relocation block gets that pointer correctly
rewritten to point at its actual, current address; a fixture with no
relocation directory at all correctly reports "nothing to do" and leaves
memory untouched (checked explicitly, not just trusted); and a fixture
using an unsupported relocation type (`IMAGE_REL_BASED_HIGHLOW`, the
32-bit variant) is correctly rejected rather than silently misapplied.
Caught one bug in the test itself before it passed — read from the
image's base address (RVA 0) instead of the actual VA the baked-in pointer
lived at (`0x1000`), a copy-paste-shaped mistake the first boot attempt
surfaced immediately as a value mismatch, not a crash.

### W5+ — everything past this is not yet scoped

A real DLL search/load path (there is still no actual
`kernel32.dll`/`ntdll.dll` to provide — W3 only proves resolution
mechanics against a fixture), `ntdll`'s syscall surface, and any
GUI-facing Win32 API are each individually larger than every other port
in this repo combined. Scoping them now would be speculation, not
planning — revisit once it's clear which of them DemonOS's process/memory
model can actually support by then.

## Non-goals (for now)

- Compiling any real upstream Wine `.c` file. There is nothing for it to
  link against yet.
- Running any real Windows or Wine-provided binary, including the small
  builtin `.exe`/`.dll` files a local Wine installation happens to ship —
  those are useful for a developer to manually cross-check a parser
  against during work on this port, but must never be bundled into this
  repository (proprietary/non-redistributable), matching this codebase's
  existing FreeDOOM-not-DOOM legal hygiene.
- A GUI. That needs the sidelined compositor stack reactivated (or a real
  Xorg/Wayland port) first, independent of anything in this doc.

## Build

- `make wine-pe-check` — builds `build/wine_pe_probe.elf` standalone.
- `make mem-reserve-check` — builds `build/mem_reserve_check.elf` standalone.
- `make wine-pe-load-check` — builds `build/wine_pe_load_check.elf` standalone.
- `make wine-pe-import-check` — builds `build/wine_pe_import_check.elf` standalone.
- `make wine-pe-reloc-check` — builds `build/wine_pe_reloc_check.elf` standalone.
