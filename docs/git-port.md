# Forking and Porting Git to DemonOS

## Goal

DemonOS already has **MAKO Git stage 1** in `src/git.c`: a useful native,
bounded RAM snapshot engine with `init`, `add`, `commit`, `status`, `log`,
`diff`, and `show`. Its records are not canonical Git objects, however, so an
ordinary Git installation cannot open them.

The compatible port is finished only when repositories can move between
DemonOS, Linux, Windows, and macOS and pass unmodified upstream `git fsck`.
Keep stage 1 as the lightweight recovery implementation; build the compatible
fork as an unprivileged user-space program rather than adding upstream Git to
the kernel.

## Fork layout and policy

```text
ports/git/
├── README.md
├── VERSION
├── upstream/              # pinned official git/git release
├── platform/demonos/      # OS compatibility layer
├── config.mak.demonos
├── patches/               # small, reviewable upstream patches
├── tests/
└── LICENSES/GPL-2.0
```

1. Fork the official `git/git` repository and pin a release tag, commit ID,
   and archive SHA-256.
2. Keep adaptation under `platform/demonos` when practical; do not scatter
   permanent `#ifdef`s through unrelated upstream code.
3. Rebase patches regularly and preserve Git's GPL-2.0-only notices and
   corresponding-source obligations.
4. Never import host binaries or anonymous source snapshots into the ISO.
5. Name the early executable `dgit-dev`; it must not replace `git` merely
   because it compiles.

## Initial upstream feature profile

Upstream Git is a POSIX C application plus helper processes and optional shell,
Perl, Python, Tcl/Tk, localization, TLS, HTTP, and XML features. Begin with a
small offline build:

```make
NO_CURL = YesPlease
NO_EXPAT = YesPlease
NO_GETTEXT = YesPlease
NO_ICONV = YesPlease
NO_OPENSSL = YesPlease
NO_PERL = YesPlease
NO_PYTHON = YesPlease
NO_TCLTK = YesPlease
NO_UNIX_SOCKETS = YesPlease
```

These switches remove features, not core requirements. A real filesystem,
process ABI, libc, zlib-compatible DEFLATE, SHA-1, and SHA-256 are still needed.
No configure check may be satisfied by a function that falsely returns success.

## DemonOS platform requirements

### Writable hierarchical VFS

The bounded boot RAMFS is not a Git repository filesystem. Add persistent:

- directories, regular files, deletion, and rename;
- `open`, `close`, `read`, `write`, `pread`, `lseek`, and `ftruncate`;
- `stat`, `lstat`, `fstat`, directory iteration, and working directories;
- exclusive creation for lock files and atomic same-filesystem rename;
- honest `fsync`/durability behavior;
- dotfiles, long paths, and storage suitable for packfiles.

Symlinks, executable bits, timestamps, case sensitivity, and path normalization
affect tree contents and checkout. Unsupported behavior must be advertised and
tested rather than silently changing repository data.

### Process and environment ABI

Git expects `argc`/`argv`, environment variables, child exit status, pipes, and
`fork`+`exec` or a carefully audited spawn replacement. Start by compiling
necessary helpers as built-ins, but keep Git in user space. Support at least
`HOME`, `PATH`, `TMPDIR`, `USER`, `GIT_DIR`, and `GIT_WORK_TREE`; repository
discovery cannot depend on MAKOBOX's currently fixed `/` directory.

### libc, hashing, and compression

Port tested user-space allocation, strings, buffered I/O, errno, directories,
time, sorting, parsing, process, pipe, and environment APIs. A temporary stub
must fail with `ENOSYS`; fake success from lock, write, rename, or flush can
corrupt a repository.

Object IDs hash `"<type> <length>\0<content>"`. Provide streaming SHA-1 and
SHA-256 with known-answer tests. The final SHA-1 build should use Git's
collision-detecting implementation. Loose objects require zlib-compatible
compression; test truncated streams, oversized output, corruption, and failed
allocation.

## Milestones

### G0 — Plumbing executable

- Cross-compile a pinned upstream revision as x86_64 DemonOS user space.
- Disable networking, localization, scripting, hooks, and paging initially.
- `dgit-dev --version` runs and exits without Linux syscalls or host paths.

Acceptance marker: `DGIT_EXEC_READY`.

### G1 — Canonical loose objects

- Support `init`, `hash-object -w`, `cat-file`, and `fsck`.
- Write standard `.git/HEAD`, `config`, `refs`, and `objects` paths.
- Compare IDs byte-for-byte against host Git fixtures.
- Copy the repository to a host and require `git fsck --full` to pass.

Acceptance marker: `DGIT_LOOSE_OBJECTS_READY`.

### G2 — Index, trees, commits, and checkout

- Read/write the canonical index with checksum verification.
- Enable `add`, `write-tree`, `commit-tree`, atomic refs, `status`, `log`,
  `show`, `diff`, and ordinary-file checkout.
- Include correct identity, timestamps, timezone, and commit messages.
- With fixed metadata, DemonOS and host Git must produce identical tree and
  commit IDs.

Acceptance marker: `DGIT_LOCAL_REPOSITORY_READY`.

### G3 — Persistent crash safety

- Store repositories on persistent writable media, never boot RAMFS.
- Test interruption around object writes, index locks, and ref renames.
- Never expose a partial object under its final ID.
- Recover stale locks without deleting valid data.
- Repeat commit, checkout, reboot, and fsck under allocation pressure.

Acceptance marker: `DGIT_PERSISTENCE_READY`.

### G4 — Packfiles and bundles

Offline interchange comes before networking. Implement pack v2, indexes,
fan-out tables, CRC32, large offsets, bounded `OBJ_OFS_DELTA` and
`OBJ_REF_DELTA`, `index-pack`, `pack-objects`, and bundle import/export.

Treat packs as hostile: bounds-check every offset, size, delta instruction,
inflated length, and allocation. Malformed packs must never fault the kernel.

Acceptance marker: `DGIT_PACK_READY`.

### G5 — Local transport

- Clone and fetch from mounted repositories or `file://`.
- Enable upload-pack/receive-pack helpers.
- Implement protocol-v2 pkt-line framing before adding TCP.
- Verify fetched object connectivity before updating refs.

Acceptance marker: `DGIT_LOCAL_TRANSPORT_READY`.

### G6 — HTTPS remotes

Land and test DNS, TCP streams, certificate-validated TLS with a maintained CA
bundle, HTTP/1.1 redirects/chunking/timeouts, secure credential input, and then
smart HTTP protocol v2. Port libcurl only when its socket, clock, entropy, and
TLS backends are real. Never ship disabled certificate verification, embedded
tokens, or silent plaintext fallback.

Acceptance marker: `DGIT_HTTPS_READY`.

### G7 — Promote the port

Replace stage 1 only when repositories round-trip through at least two host
platforms, `git fsck --full` passes, local commits/branches/merges/tags and
clone/fetch/push work, interrupted writes are safe, and the process has only
filesystem/network rights. Until then expose both accurately:

```text
git       # MAKO snapshot/recovery implementation
dgit-dev  # compatible upstream fork under development
```

## Build and ISO packaging

Downloads must be explicit and verified, never part of normal offline `make`:

```make
GIT_VERSION := <pinned-release>

git-source:
	./tools/fetch-git-source.sh $(GIT_VERSION)

build/dgit-dev.elf: $(DGIT_OBJECTS) user/linker.ld
	$(LD) -nostdlib -T user/linker.ld $(DGIT_OBJECTS) -o $@
```

The fetch tool downloads from the official project, checks a repository-owned
SHA-256, preserves licensing, and unpacks only expected files. Install the
development executable as `/system/bin/dgit-dev.elf`, fixtures below
`/tests/git/`, and port metadata below `/system/ports/git/`.

## Test matrix

1. **Host unit tests:** hashes, zlib, index, pkt-line, locks, and malformed
   input under sanitizers.
2. **DemonOS integration:** commands in QEMU, exit statuses, reboot, storage,
   allocation-failure, and kernel-survival checks.
3. **Cross implementation:** exchange repositories, bundles, then remotes with
   unmodified upstream Git.

Keep deterministic fixture hashes in the repository. Never update an expected
ID merely because the DemonOS implementation generated a different value.

## Deferred features

The first compatible port does not need a GUI, Git LFS, submodules, multiple
worktrees, SSH, signed commits, credential storage, or server hosting. They
follow a correct local and HTTPS client.

## Official technical references

- Upstream source: <https://github.com/git/git>
- Git objects: <https://git-scm.com/book/en/v2/Git-Internals-Git-Objects>
- Index format: <https://git-scm.com/docs/index-format>
- Pack format: <https://git-scm.com/docs/pack-format>
- Protocol v2: <https://git-scm.com/docs/protocol-v2>
- Hash transition: <https://git-scm.com/docs/hash-function-transition>
