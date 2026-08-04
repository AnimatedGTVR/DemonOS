# C++ in the kernel (partial)

## Scope

This is a plan for letting **selected** kernel subsystems be written in C++,
not a rewrite of the kernel. The boot path, interrupt/exception entry,
scheduler context switch, and anything touching raw CPU state
(`src/arch/x86_64/*`, `boot.o`, `interrupt_stubs`) stay C/assembly exactly as
they are today — that code is timing- and layout-sensitive in ways that
benefit from C's predictability, not from C++ abstraction. The candidates for
C++ are the higher-level, data-structure-heavy subsystems that currently
hand-roll the same bounded-table pattern in C: `src/ramfs.c` (a fixed-size
file table with linear scans and manual name/slot bookkeeping),
`src/capability.c` (per-process capability tables resolving handles to
objects+rights), and similar bounded-array services. C++'s templates and RAII
are a genuine fit there — C is not "worse" everywhere in this kernel, it is
worse specifically at expressing "the same generic bounded table, four
times, with hand-copied bookkeeping."

## Why this, and why now

DemonOS already proves the freestanding-C++ constraints work end to end, just
in userspace: `apps/cxx_hello` links `src/cxx_runtime.cpp` under
`-ffreestanding -fno-exceptions -fno-rtti -nostdlib -nostdinc++`, backed by a
real first-fit free-list allocator over a static arena
(`include/demon/cxx_runtime.h`), and exercises heap allocation, vtables, and
a virtual destructor as a boot-time proof (`make check`'s cxx_hello checks).
Bringing the same discipline into the kernel is a smaller step than it
sounds, and the payoff is concrete: `ramfs.c`'s `find_handle`/`file_for_id`
style linear-scan-over-a-fixed-array pattern already appears more than once
in this codebase (`sys_demonos.c`'s `QUAKE_MAX_HANDLES` table is the same
shape at the userspace/PortKit layer) — a single templated bounded-table type
used from both sides would delete duplicated bookkeeping instead of hand
porting it.

## What "way better at doing" concretely means here

- **RAII for capability/handle lifetimes.** `capability_open`/
  `capability_close` and `ramfs_open`/`ramfs_delete` are manually paired
  today; a scope-guard/RAII wrapper makes "close on every exit path,
  including error returns" a property of the type instead of something every
  caller must remember (relevant given `src/arch/x86_64/userspace.c`'s
  syscall dispatch has many early `frame->rax = UINT64_MAX; return` exits per
  syscall number).
- **Templates for the repeated bounded-table shape.** `ramfs.c`'s
  `RAMFS_FILES`-sized array + linear `name_equal` scan, and the equivalent
  object-table pattern in `capability.c`, are the same generic structure
  (fixed capacity, slot reuse, linear lookup) with different payloads. One
  `template<typename T, size_t N> class bounded_table` used by both removes
  the duplication without giving up the fixed, auditable capacity this
  codebase deliberately favors everywhere (RAMFS file slots, per-process
  heap/stack page counts, PortKit's static arena) — no `std::vector`,
  no unbounded growth.
- **Destructors for the "undo partial setup on failure" pattern** that shows
  up in multi-step init code (e.g. `ramfs_seed`/`ramfs_seed_reference`'s
  open-then-populate-then-fail sequences).

**Correction from K3:** the assumption above — that `ramfs.c` and
`capability.c` share literally *one* generic bounded-table shape — turned out
to be wrong once K3 actually inspected `capability.c` closely. They're
related but distinct: `ramfs.c` is name-keyed (open-or-create by string,
`ramfs_open("id1/pak0.pak", ...)`), while `capability.c` is
(pid, slot, generation)-keyed with no names at all, specifically to make a
closed-then-reopened slot's *old* handle stop resolving — a stale-handle
guard `ramfs.c` has no equivalent need for. Forcing `capability.c` onto K1's
`bounded_table<T,N>` would have meant either inventing fake per-slot names
nobody needs or stripping the name concept out of `bounded_table` until it
stopped being the same type `ramfs.c` uses. K3 instead introduced a second,
sibling template (`kernel::slot_table<T,N>`, `include/kernel/slot_table.h`)
for the index+generation shape. The genuine templates/RAII win survives this
correction intact — it just turned out to require two related types instead
of one.

## Non-goals

- No exceptions, no RTTI, no `std::` containers, no dynamic `new`/`delete`
  backed by an unbounded heap — same constraints as the existing userspace
  C++ runtime, for the same reason (fixed, auditable memory budgets).
- No touching boot, interrupts, or the scheduler context switch. Those are
  correctly C/asm and should stay that way.
- Not a "convert the kernel to C++" project. If a subsystem doesn't have a
  genuine templates/RAII win, it stays C.

## Milestones

### K0 — kernel-side freestanding C++ runtime (implemented)

Ports the shape of `src/cxx_runtime.cpp`/`include/demon/cxx_runtime.h` into a
kernel-side equivalent: `include/kernel/cxx_runtime.h` / `src/kernel_cxx_runtime.cpp`,
a first-fit coalescing allocator over a 16 KiB static arena backing
`operator new`/`delete`, plus `__cxa_pure_virtual` (halts ring 0 loudly
instead of falling into garbage — there is no process to exit from ring 0)
and an inert `__cxa_atexit`. Built via the Makefile's `KERNEL_CXXFLAGS`
(mirrors `CXXFLAGS`: `-ffreestanding -fno-exceptions -fno-rtti
-fno-stack-protector -mno-red-zone -nostdinc++`, plus the kernel's own
soft-float ABI flags). `kernel_cxx_self_test()` runs at boot right after the
capability self-test and prints `KERNEL_CXX_RUNTIME_OK`; it proves
allocation, an indirect vtable call through a base pointer, a virtual
destructor call, and that a freed block gets reused by the next same-size
allocation (not naive "total bytes returns to baseline" — a single isolated
freed block's header overhead is only reclaimed by coalescing with an
adjacent freed neighbor, so that's the wrong invariant to check).

### K1 — bounded_table<T, N> (implemented)

`include/kernel/bounded_table.h`: the templated bounded-table type described
above, covering the `ramfs.c` file-table shape (highest-value target: most
duplicated logic, most callers). Not migrated into `ramfs.c` yet (that stays
K2) — proven standalone in `src/kernel_bounded_table_test.cpp` against the
same semantics `ramfs.c` already has: open-or-create, name-based lookup,
delete-frees-slot-not-storage (the template only owns the slot; any bytes a
payload references, like ramfs.c's separate bump-allocated storage arena,
stay the caller's concern), slot reuse after release, and hard capacity
enforcement. `kernel_bounded_table_self_test()` runs at boot right after the
K0 check and prints `KERNEL_BOUNDED_TABLE_OK`. Because the header itself
uses templates and namespaces, it cannot be included from `kernel.c` (C11);
the self-test's prototype lives in a separate, plain-C-includable
`include/kernel/bounded_table_test.h` instead.

### K2 — migrate one real subsystem (implemented)

`src/ramfs.c` is now `src/ramfs.cpp`, its file table (used-flag, name
storage, slot reuse, capacity) implemented on top of `bounded_table` from
K1; the separately bump-allocated byte arena (`storage`/`storage_used`)
stayed hand-rolled C++, unchanged in behavior, since that was always a
distinct concern from the slot table (see the file-level comment in
`src/ramfs.cpp`). `include/kernel/ramfs.h` gained the same `extern "C"`
guard added to `include/kernel/serial.h` in K0 (without it, every existing
C caller's unmangled symbol reference would fail to link against the new
C++ definitions). Every function keeps its exact original signature, so
every existing caller (`src/apps.c`, `src/git.c`, `src/makobox.c`,
`src/arch/x86_64/userspace.c`'s syscalls 6/9/10/42/43/44, ...) is unchanged.
`bounded_table` itself gained one addition during this migration —
`rename(id, new_name, new_name_length)`, needed for `ramfs_rename`'s
"same slot, new name, payload untouched" contract — covered by a new case
in `kernel_bounded_table_test.cpp` alongside the K1 self-test.

Verified beyond the boot-time `ramfs_self_test()` (`PROJECT_STORE_OK`): with
the unrelated ClassiCube D1 boot gate (see Known pre-existing issue below)
temporarily short-circuited, boot reaches all the way to `KERNEL_BOOT_OK`,
and `GIT_WORKTREE_OK`/`APP_REGISTRY_OK`/`RUNAS_POLICY_OK` — which exercise
`ramfs_rename`/`ramfs_delete` far more heavily via real git-worktree
operations than the basic self-test does — all pass unmodified. This is the
real proof of the K2 milestone: the C++ subsystem underneath is a drop-in
internal implementation change, not an ABI change.

#### Known pre-existing issue (unrelated to K0/K1/K2)

`framebuffer-fallback-smoke`, `vfs-smoke`, and other full-boot smoke targets
currently fail on `[FAILED] ClassiCube D1 core did not exit cleanly`. This
predates all of K0–K2 (reproduces identically via `git stash` against the
unmodified tree) and is not caused by anything in this doc's milestones —
tracked separately, not blocking further C++ kernel work.

### K3 — capability.c (implemented)

Investigation first (see the "Correction from K3" note above): `capability.c`
turned out not to fit K1's `bounded_table<T,N>` at all — it's
(pid, slot, generation)-addressed, not name-addressed, specifically to make
a closed-then-reopened slot's old handle stop resolving. Rather than force a
bad fit, K3 introduced a sibling type, `kernel::slot_table<T,N>`
(`include/kernel/slot_table.h`): `allocate()` claims the first free slot and
returns a generation-stamped handle triple; `resolve(slot, generation)` is
the handle-checked lookup (wrong generation on a real slot correctly fails);
`peek(slot)` is the unchecked "I already own this index" lookup
`capabilities_close_all` needs; `release(slot, &payload_out)` frees the slot,
bumps its generation (skipping 0, reserved as never-valid), and hands back
what the slot held so the caller can run service-specific cleanup (e.g.
`surface_release` for a closed `CAPABILITY_SERVICE_SURFACE` handle) — this
collapses logic that was hand-duplicated between `capability_close` and
`capabilities_close_all` in the original C into one place. Proven standalone
in `src/kernel_slot_table_test.cpp` (boots to `KERNEL_SLOT_TABLE_OK`) before
`src/capability.c` became `src/capability.cpp` on top of it, extern-"C"
signatures unchanged. `include/kernel/capability.h` and
`include/kernel/surface.h` (the latter called into by
`capability_open_surface`/`capability_grant`/`capability_close` for
surface retain/release) both gained the same `extern "C"` guard as
`ramfs.h` in K2.

Verified the same way as K2: `CAPABILITY_ABI_OK` passes on every normal boot
(it runs before the unrelated ClassiCube gate), and with that gate
temporarily short-circuited, `IPC_CHANNEL_SELF_TEST_OK`/`MKO_NATIVE_OK`/
`DISPLAY_DEVICE_TEST_OK` — which exercise `capability_open_surface` and
`capability_grant` far more heavily than the basic self-test — all pass,
reaching `KERNEL_BOOT_OK`.

No more milestones are currently planned past K3; the next candidate
subsystem for this pattern, if one comes up, should get the same
investigate-before-forcing-a-fit treatment K3 needed.

## Build integration

Kernel `.cpp` translation units compile with a new `KERNEL_CXXFLAGS` (parallel
to the existing `CXXFLAGS`, but freestanding against the kernel's own headers
rather than `include/demon/c_app.h`'s userspace ABI) and link straight into
`build/kernel.elf` alongside the existing `.o` list — no new ELF, no new
syscalls, no change to the boot/link sequence beyond adding object files to
the existing `ld -nostdlib ... -o build/kernel.elf` command in the Makefile.
