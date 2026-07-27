# Native C applications

DemonOS boots into its native graphical desktop. Its framebuffer-less recovery
path provides MakoBox and can launch freestanding x86-64 C applications under
MAKO-ABI. These are real ring-3 ELF64 processes with private page tables; they
are not Linux programs and do not use a host libc. Desktop launcher integration
for these applications is the next shell milestone.

The first complete example is `apps/tetris`. Its build has three pieces:

- `main.c` contains the game and includes `demon/c_app.h`.
- `entry.S` supplies the tiny ELF entry point and calls the C function.
- `user/linker.ld` places the result inside the current application load window.

The Makefile compiles with `-ffreestanding`, disables host runtime features,
links with `-nostdlib`, strips the ELF, installs it as
`/system/bin/tetris.elf`, and checks its size. `apps` discovers that ELF from
the actual RAMFS. `tetris` is merely the MakoBox shortcut for
`apps launch tetris`.

Applications may currently use the inline wrappers in
`include/demon/c_app.h` for console output, ticks, yielding, process exit,
service handles, and unified input. The complete syscall and capability table
is in `docs/application-abi.md`.

## Test loop

Run:

```sh
make iso
make run
```

For the current recovery-console test path, boot without a framebuffer and at
`mako#` enter `tetris`. Use A/D to move, W to rotate, S to soft-drop, and Q to
return to the shell. `make keyboard-smoke` performs that fallback launch and
exit through emulated PS/2 scan codes, while `make check` also verifies that a
normal graphical boot remains in `desktop.target`.

Foreground applications receive exclusive unified input. The compositor wait
is suspended while they run, and mirrored shell characters are discarded at
both ownership boundaries so game controls cannot later execute as commands.

## Current C runtime boundary

There is no POSIX layer or general libc yet. A larger C port needs explicit
MAKO-ABI backends for its allocator, files, clock, input, graphics, and audio.
Keeping those adapters small preserves the goal: one application binary and
source tree that behaves consistently regardless of whether the development
host is Windows or Linux.
