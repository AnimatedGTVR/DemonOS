# Dynamic processes Stage 4

Stage 4 replaces the two-entry scheduler with a bounded dynamic process manager.
It supports seven concurrent ring-3 processes plus the kernel idle task while retaining
predictable memory use.

Implemented behavior:

- reusable PID slots with parent PID tracking;
- ELF64 spawning from absolute RAMFS paths;
- independently mapped code, heap, user stack, page tables, and kernel entry stack;
- ready, running, blocked, exited, and unused lifecycle states;
- preemptive round-robin scheduling across every live process;
- wait with parent/child validation and blocked-parent wakeup;
- external termination and normal exit status collection;
- process reaping, capability cleanup, and address-space slot reuse;
- per-spawn service-capability policy assignment;
- `spawn`, `wait`, `getpid`, `yield`, and `exit` syscall wrappers in the MKO SDK,
  plus the kernel termination syscall used by process supervisors.

To remain lightweight and deterministic, physical frames for seven process address spaces
are reserved once from the boot frame allocator. Spawn dynamically assigns and reuses those
isolated resources; there is no general-purpose kernel heap and no allocation in IRQ context.

The boot test loads `/projects/hello/main.elf` through the RAM filesystem, creates PID 3,
runs it in ring 3, collects status zero, and reuses its slot after cleanup. The interactive
`apps launch hello` command exercises the same path.
