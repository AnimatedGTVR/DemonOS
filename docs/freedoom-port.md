# Freedoom native port

Freedoom is free Doom-compatible game data, not a game engine. A genuine
DemonOS port therefore has two separately versioned inputs:

1. an engine port that executes as a MAKO-ABI C process;
2. the official Freedoom IWAD data (currently the 0.13 release line).

The kernel does not claim that Freedoom runs yet. Copying a WAD onto the ISO and
drawing a Doom-like screen would not exercise the real game. The following
gates must be implemented and boot-tested first.

## Required platform work

- Read-only ISO9660 or block-backed VFS access so a process can stream files
  larger than the current 48 KiB RAMFS per-file limit.
- A bounded userspace heap allocator and the small libc subset needed by the
  chosen engine.
- A 320x200 indexed or 32-bit process surface that the compositor can scale and
  present without copying a full frame through console text.
- Keyboard event translation and exclusive foreground input. The latter already
  exists and is proved by native C Tetris.
- A monotonic timer backend. MAKO-ABI ticks already provide 100 Hz time.
- PCM audio service, mixer, and a QEMU-supported output driver. Initial bring-up
  may be silent, but it must report that honestly.
- Engine save/config files on a writable filesystem; these may remain disabled
  until persistent block storage lands.

## Port order

1. Add large read-only boot assets without copying them into kernel BSS.
2. Port a compact Doom-compatible C engine behind a six-function platform shim:
   video, input, ticks, file reads, allocation, and exit.
3. Run the engine's title loop against an official Freedoom IWAD and require a
   serial `FREEDOOM_TITLE_READY` contract.
4. Add deterministic keyboard smoke input that starts a game and reaches the
   first playable tic.
5. Add PCM only after video/input/file correctness is stable.

The canonical project and downloads are `https://freedoom.github.io/` and
`https://github.com/freedoom/freedoom/releases`. Release archives should be
downloaded during explicit packaging, pinned by version and SHA-256, and kept
out of the kernel image. Their licenses and notices must ship beside the data.
