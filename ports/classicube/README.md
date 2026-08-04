# ClassiCube for DemonOS

This directory is the native port boundary for ClassiCube, a C client
compatible with Minecraft Classic. It is not Java Minecraft and does not ship
Mojang assets. Upstream remains in `build/classicube-upstream` at a pinned
revision and is never downloaded by an ordinary kernel build.

Initial target:

- freestanding x86_64 ring-3 process;
- offline single-player/classic generation first;
- ClassiCube's software renderer into a DemonOS surface;
- native keyboard and relative mouse events;
- RAMFS-backed options, maps, screenshots, and texture packs;
- bounded memory with no host libc, SDL, X11, or OpenGL dependency.

Run `make classicube-source` to obtain the pinned source and
`make classicube-port-audit` to verify that the expected upstream platform
contracts are still present. See `docs/classicube-port.md` for the staged port.

`platform/Platform_DemonOS.c` is intentionally an interface skeleton. Each
section becomes buildable as the corresponding DemonOS ABI is proven; it must
not return fake success for unimplemented filesystem, socket, or rendering
operations.
