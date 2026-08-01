# MAKO Application ABI 0.1

MAKO-ABI is the stable boundary between a project and the MAKO kernel. A
project targets this contract instead of calling Windows, Linux, or a host C
library. Builds may be produced on either development host; the resulting
application runs directly on MAKO.

Version 0.1 is intentionally small. Compatibility is easier to preserve when
new services are added deliberately instead of inheriting a host operating
system's entire API.

## Executable contract

- Container: little-endian ELF64 `ET_EXEC`
- Machine: x86-64 (`EM_X86_64`)
- Required content: at least one executable `PT_LOAD` segment
- Current load window: up to 30 private code pages beginning at `0x300000`
- Current writable heap: five private pages beginning at `0x31E000`
- Current user stack: five private pages above the 20 KiB heap
- Entry state: RDI contains the process ID and RSP is 16-byte aligned
- Privilege: ring 3 with a private CR3 and private code/heap/stack frames

Physical executable capacity is tiered to remain lightweight: the two
bootstrap slots own four pages each, the persistent system/compositor slot
owns the full 24-page window, and four ordinary application slots own twelve
pages each. Dynamic spawning tries free slots until it finds one whose capacity
fits the validated ELF. This allocates 80 executable frames instead of 112.

The loader validates ELF identity, class, encoding, type, machine, header
sizes, program-table bounds, segment file/memory sizes, destination bounds, and
that the entry point belongs to an executable segment. It zeros destination
memory before copying loadable bytes.

## Syscall contract

Applications issue `int 0x80`. RAX selects the call, arguments begin in RDI and
RSI, and results return in RAX.

| RAX | Service | Arguments | Result |
| ---: | --- | --- | --- |
| 0 | `write` | RDI=user pointer, RSI=byte count | bytes written or `-1` |
| 1 | `yield` | none | 0 after the task is scheduled again |
| 2 | `exit` | RDI=status | does not return |
| 3 | `ticks` | none | monotonic 100 Hz kernel tick count |
| 4 | `getpid` | none | current process ID |
| 5 | `service_open` | RDI=service ID | owned opaque handle or `-1` |
| 6 | `handle_write` | RDI=handle, RSI=pointer, RDX=count | bytes written or `-1` |
| 7 | `handle_query` | RDI=handle, RSI=property | property value or `-1` |
| 8 | `handle_close` | RDI=handle | 0 or `-1` |
| 9 | `file_open` | RDI=storage, RSI=name, RDX=name length, R10=create | file handle or `-1` |
| 10 | `handle_read` | RDI=file, RSI=destination, RDX=capacity | bytes read or `-1` |
| 11 | `spawn` | RDI=path, RSI=length, RDX=service mask | child PID or `-1` |
| 12 | `wait` | RDI=child PID | child PID; status in RDX |
| 13 | `terminate` | RDI=PID, RSI=status | 0 or `-1` |
| 14 | `channel_create` | RDI=name, RSI=length | receive handle or `-1` |
| 15 | `channel_connect` | RDI=name, RSI=length | send handle or `-1` |
| 16 | `channel_send` | RDI=channel, RSI=message, RDX=length | bytes or `-1` |
| 17 | `channel_receive` | RDI=channel, RSI=destination, RDX=capacity, R10=nonblocking | bytes or blocks |
| 18 | `display_info` | RDI=display, RSI=destination | packed dimensions or `-1` |
| 19 | `display_submit` | RDI=display, RSI=request | 0 or `-1` |
| 20 | `input_poll` | RDI=input, RSI=destination | 0, empty, or `-1` |
| 21 | `display_dimensions` | RDI=display | `width * 1000 + height` |
| 22 | `surface_create` | RDI=factory, RSI=width, RDX=height | writable surface handle or `-1` |
| 23 | `surface_write` | RDI=surface, RSI=pixels, RDX=count, R10=offset | pixels written or `-1` |
| 24 | `surface_share` | RDI=surface, RSI=IPC channel | destination-owned read handle or `-1` |
| 25 | `surface_read` | RDI=surface, RSI=destination, RDX=capacity | pixels copied or `-1` |
| 26 | `compositor_wait` | RDI=IPC channel, RSI=input, RDX=message destination, R10=event destination, R8=relative timeout ticks (0=infinite) | 1 for IPC, 2 for input, 3 for deadline, or `-1` |
| 27 | `surface_map` | RDI=readable surface | fixed read-only mapping address or `-1` |
| 28 | `surface_damage` | RDI=writable surface, RSI=x, RDX=y, R10=width, R8=height | 0 or `-1` |
| 29 | `surface_take_damage` | RDI=queryable surface | packed x/y/width/height or `-1` |
| 30 | `surface_unmap` | RDI=readable surface | 0 or `-1` |
| 31 | `compositor_report` | RDI=display, RSI=window count, RDX=focused window id | 0 or `-1` |
| 33 | `display_submit_effect` | RDI=display, RSI=request | 0 or `-1` |
| 42 | `handle_read_at` | RDI=file, RSI=destination, RDX=capacity, R10=absolute byte offset | bytes read or `-1` |

Syscall 31 is compositor introspection: it publishes the caller's live window
count and focused window id into kernel-side counters (read back via
`display_compositor_window_count`/`display_compositor_focused_window`), gated
on the same DISPLAY capability write right `display_submit` requires. The
kernel stores exactly what it is told and implements no window-manager policy;
this exists so a boot test can assert on real dynamic compositor state
(windows created/closed, focus changed) instead of pixel/frame counts from a
scripted scenario.

Syscall 33 exposes the kernel's existing rounded-rect/border/gradient/shadow
compositing (`libs/graphics/graphics.c`, previously only used by the boot
screen) to the compositor, so window chrome can use real corner rounding,
alpha-blended borders, vertical gradients, soft drop-shadows, and the
kernel-embedded desktop wallpaper instead of only flat rectangles. It draws
straight onto the kernel-resident backbuffer; although it takes no
caller-space pixel buffer, the dispatcher still switches to the kernel
address space around the draw, because the backbuffer's physical range
aliases part of `USER_CODE`'s virtual window — drawing under the caller's
own CR3 corrupts the calling process's code pages. Gated on the same DISPLAY
capability write right as `display_submit`/`compositor_report`. The 10-word
request (`struct display_user_effect` in `include/kernel/display.h`) is
`kind, x, y, width, height, radius, arg1, arg2, arg3, flags`, where `kind`
selects the operation and the meaning of `radius`/`arg1`/`arg2`/`arg3` varies
by kind:

| kind | operation | radius | arg1 | arg2 | arg3 |
|---|---|---|---|---|---|
| 0 | rounded rect | corner radius | argb | — | — |
| 1 | rounded border | corner radius | border thickness | border argb | fill argb |
| 2 | vertical gradient | — | top argb | bottom argb | — |
| 3 | soft shadow | corner radius | argb | spread | — |
| 4 | wallpaper blit | — | — | — | — |
| 5 | UI icon blit | — | icon index (enum `demon_ui_icon`) | — | — |
| 6 | start logo blit | — | — | — | — |

Kind 4 blits the kernel-embedded desktop wallpaper (`assets/Flowers.jpg` →
`build/wallpaper.argb`, the same link-time embedding as the mouse cursor)
into the request rect. The image is stored at 320x240 and upscaled 2x
nearest-neighbor at blit time (a full-size raw image would overflow the
kernel's 4 MiB identity-mapped physical window); each destination pixel
reads the source at (x/2, y/2), so partial-region requests stay aligned
with the full picture.

Kind 5 blits one titlebar window-control icon (`assets/Mouse/New Icons/` →
`build/ui_icons.argb`) at its native 10x10 size, no upscaling — `arg1`
selects which one via `enum demon_ui_icon` (0=close, 1=close-hover,
2=minimize, 3=minimize-hover). There is no maximize icon in the source pack
(its "maximize program button" files are mislabeled duplicates of the close
X), so the maximize titlebar control stays a plain colored dot rather than
using a wrong glyph.

Kind 6 blits the diamond "Liquid OS" start-button logo
(`build/start_logo.argb`) at its native 22x22 size, no upscaling.

`flags` bit 0 is `DISPLAY_SUBMIT_PRESENT`, same as `display_submit`. The
[`MakoAbi` SDK](../user/sdk.mko) binds this as `display_submit_effect`;
`compositor.mko` wraps it further into purpose-named helpers
(`draw_rounded_rect`, `draw_rounded_border`, `draw_gradient_v`, `draw_shadow`,
`draw_wallpaper`, `draw_ui_icon`, `draw_start_logo`) that hide the
request-buffer bookkeeping.

All user pointers are range-checked before the kernel reads them. Task context
is preserved across both voluntary and timer-driven switches.

### MKO SDK boundary

Native MKO exposes `abi_syscall0` through `abi_syscall5` as compiler intrinsics.
The suffix states how many syscall arguments follow the syscall number. They
return `u64` and lower directly to the register convention above, including
R10 for argument four. The compiler rejects incorrect arity and non-integer
arguments.

Applications import the typed [`MakoAbi` SDK](../user/sdk.mko), which covers
every ABI 0.1 call, including process lifecycle, named IPC, bounded display and
input, surfaces, and the compositor waitset. Native compilation recursively
resolves the module and emits its
namespaced functions into the application assembly. Only ELF `_start` glue
remains in assembly; the live two-process capability, storage, scheduling, and
isolation exercise is implemented in [`init.mko`](../user/init.mko).

[`projects/hello`](../projects/hello/main.mko) is a separate bounded ELF
project built against this SDK. It demonstrates the intended portability
boundary: project source depends on MAKO-ABI, never a Windows or Linux API.

## Capability services

| Service ID | Name | Rights | Property 0 |
| ---: | --- | --- | --- |
| 1 | console | write | unavailable |
| 2 | clock | query | monotonic ticks |
| 3 | process | query | process ID |
| 4 | project storage | open, query | reserved |
| 6 | named IPC | send, receive, query | channel metadata |
| 7 | display | write, query | display metadata |
| 8 | unified input | read, query | input metadata |
| 9 | surface factory | open, query | reserved |

Surface creation is bounded to four 64×64 ARGB objects backed by a 64 KiB
physical arena allocated after the process-frame pool. The arena is not linked
into kernel BSS. A client holds read/write/query rights; `surface_share` resolves
the owner of an existing IPC channel and installs a separate read/query handle
in that process. Sending the returned destination-owned handle in a window
packet does not make it usable by any third process. The compositor continues
reading the surface after the creating client exits, proving that the object and
transferred capability have independent lifetimes. Surface handles are
reference counted; closing the final handle clears and returns the fixed arena
slot. Boot creates a second temporary object and verifies that it is reclaimed
while the compositor-owned object stays live. `surface_map` installs the four
physical arena pages read-only at `0x380000` in the compositor's private page
table and retains the object independently of its handle. Display submission
can consume that mapping directly, eliminating the per-frame pixel copy. The
mapping is removed and released during process teardown. The creating client
publishes a bounded damage rectangle; the compositor consumes its packed
metadata once before presenting the mapped pixels.

`compositor_wait` is an atomic blocking select over one receive channel and the
exclusive unified-input capability. Delivery writes into a validated user heap
or stack buffer, cancels the losing waiter, wakes the task once, and identifies
the ready source in RAX. A nonzero R8 also arms a scheduler deadline and returns
source 3 after that many 100 Hz ticks; delivery cancels the deadline. It removes
the compositor's former poll/yield loop and provides bounded dirty-frame pacing;
process teardown cancels both registrations so a crashed server cannot leave a
stale input waiter.

File handles are returned by `file_open` rather than `service_open`. They carry
read, write, and query rights; property 0 is the current byte length. A file
write replaces its contents. Reads may target writable user heap/stack memory
only, so a file capability cannot be abused to overwrite executable pages. A
read copies at most the supplied capacity and returns that byte count; large
RAMFS objects can therefore be inspected through a small bounded buffer.

Handles are opaque values containing a kernel-controlled owner, slot, and
generation. Resolution rejects foreign PIDs, missing rights, closed slots, and
stale generations. Process exit tests close every handle. The boot contract
also requires both a forged cross-process query and reuse of a closed console
handle to be denied.

The current RAM store holds up to 20 files, with absolute or bare paths up to
63 characters using portable `A-Z`, `a-z`, `0-9`, `.`, `_`, `-`, and `/`
characters and 144 KiB per file. File payloads share one 288 KiB append-only
arena instead of reserving the per-file maximum in every slot, keeping the
kernel image small while preserving deterministic bounds.
It is deliberately non-persistent until a block-device layer exists. Kernel
process teardown closes any handles an application forgot to release.

The distribution ISO preloads `sdk.mko`, `hello.mko`, `hello.elf`, the
compositor/client ELFs, and a repository-provenance manifest as Multiboot2
modules. Their physical ranges are included in the boot reservation
floor before frame allocation begins, then their exact bytes are seeded into
the project store without being counted as application writes. The boot fails
if any required module is absent, unreachable, oversized, or invalidly named.
The complete `AnimatedGTVR/MAKO` working-tree source snapshot is stored at
`/system/mako/MAKO-source.tar.zst`; it becomes directly accessible after the
ISO/VFS milestone lands.

## Isolation contract

Each process receives separate four-level page tables. The kernel remains
supervisor-only and identity-mapped in the low 4 MiB; user code, heap, and stack
leaves are the only ring-3 mappings. A context switch changes CR3 and the TSS ring-0
stack before restoring the selected interrupt frame.

The boot self-test runs two copies of the same ELF at identical virtual
addresses. Each stores its PID at the same virtual stack offset, survives timer
preemption, and verifies it through `getpid`. The test fails if their private
frames or address spaces are accidentally shared.

## Portability boundary

MAKO-ABI makes projects independent of Windows and Linux APIs. Version 0.1 is
not architecture-neutral binary code: its ELF files contain x86-64 machine
instructions. Future architecture ports can keep the service semantics while
using architecture-specific executable slices or a portable MKO bytecode form.

Planned compatible extensions include multiple load segments, heap mappings,
persistent block storage, directories, graphics, audio, input, and networking.
Existing syscall numbers will not be repurposed.
