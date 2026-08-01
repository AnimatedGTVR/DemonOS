# doomgeneric for DemonOS

The engine source is fetched explicitly and pinned to upstream commit
`dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284`. It is never downloaded by the
kernel, ISO, or ordinary smoke targets.

```text
make doom-source
make doom-engine-audit
make doom-runtime-audit
```

`doom-engine-audit` compiles every upstream core translation unit with the
DemonOS freestanding x86_64 constraints and combines them into
`build/doom-engine/doomgeneric-core.o`. This deliberately stops before the
final executable until the platform and stdio/file shims replace every host
symbol reported in `build/doom-engine/unresolved.txt`.

The audit also compiles `platform/doomgeneric_demonos.c`. It provides the six
upstream `DG_*` hooks using PortKit timing/input and a callback-based compositor
backend. The final window client will install the presentation callback; the
engine never writes the framebuffer or embeds a second window protocol.

`doom-runtime-audit` adds the owned read-only stdio adapter, Doom libc, and
PortKit, then requires the combined object to have an empty undefined-symbol
table. Persistent file writes intentionally fail during D0; WAD streaming and
console diagnostics are functional.

Doom-specific compilation enables SSE2 because the x86_64 ABI passes upstream
floating-point diagnostic values in SSE registers. Kernel compilation remains
soft-float-only.

Do not edit the fetched checkout. DemonOS-owned code belongs in
`apps/doom/` (runtime/parser) and `ports/doom/platform/` (engine adapters).
