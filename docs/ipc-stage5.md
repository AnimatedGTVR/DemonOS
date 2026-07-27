# Kernel IPC channels Stage 5

Stage 5 introduces dedicated message IPC. RAMFS files are no longer needed as a
process-to-process transport.

The implementation provides sixteen named channels, capability-protected send and
receive endpoints, eight queued messages per channel, and a 64-byte message bound.
Messages include sender PID metadata. Receive supports blocking and non-blocking modes;
a blocking task enters the scheduler's blocked state and is woken by direct delivery.

Delivery into a waiting process resolves its isolated heap or stack frames before copying.
Channel storage is fixed, and process exit cleans owned services, queued state, endpoint
capabilities, and blocked waiters without heap allocation.

MKO userspace exposes `channel_create`, `channel_connect`, `channel_send`, and
`channel_receive` through syscalls 14–17. The boot test has PID 1 register
`desktop.test` and block. PID 2 connects and sends `IPC-OK!!`; the kernel transfers the
message and wakes PID 1. A second test covers queued delivery, lookup, rights, and cleanup.
