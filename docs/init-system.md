# MAKO Init and Service Management

MAKO's first init layer is a small kernel-resident dependency engine. It is not
a renamed print sequence: each unit has live state, an ordering dependency,
start/stop counters, mutability policy, and transaction accounting. Boot must
resolve every dependency and reach `default.target`; graphical boots additionally
adopt the already-validated ring-3 compositor and reach `desktop.target`. A cycle,
failed dependency, dead compositor, or incomplete target makes the kernel halt
before presenting the console.

## Boot units

| Unit | Dependency | Policy |
| --- | --- | --- |
| `kernel.target` | none | boot-critical, immutable |
| `hardware.target` | `kernel.target` | boot-critical, immutable |
| `project-store.service` | `kernel.target` | boot-critical, immutable |
| `userspace.target` | `project-store.service` | boot-critical, immutable |
| `project-host.service` | `userspace.target` | administrator-manageable |
| `app-manager.service` | `project-host.service` | boot-critical, immutable |
| `console.service` | `hardware.target` | boot-critical, immutable |
| `default.target` | `app-manager.service` | boot-critical, immutable |
| `desktop-compositor.service` | `default.target` | graphical, immutable, PID-bound |
| `desktop.target` | `desktop-compositor.service` | graphical, immutable |

`desktop-compositor.service` is not a decorative status record. Before the unit
can activate, init queries the scheduler, verifies that the bound PID is named
`compositor`, and requires it to be blocked in its IPC receive loop. The PID is
reported by `runit list-units`. Headless/VGA fallback boots leave both
graphical units inactive and still reach the eight-unit console target.

`runit` is MAKO's init/unit operator surface -- the deliberate, small,
systemd-shaped slice of unit inspection and lifecycle transactions, without
importing systemd or its Linux assumptions:

```text
runit list-units
runit status project-host.service
runas runit stop project-host.service
runas runit start project-host.service
runas runit restart project-host.service
```

Read-only inspection is available directly. Lifecycle mutations are rejected
unless they enter through `runas`. Boot self-tests verify dependency resolution,
restart state transitions, immutable-unit protection, direct-mutation rejection,
the runas allowlist, and the complete privileged dispatcher path.

## `runas` security boundary

The recovery console has no users, credentials, or persistent policy database.
The graphical desktop's `demon`/`demon` preview gate owns only session input
and does not yet issue an administrator identity or survive reboot. `runas`
therefore does not pretend to authenticate a Unix account. It
grants a narrowly defined local-console administrator role only for unit
start/stop/restart transactions, rejects other commands, and counts grants and
denials for auditing.

The next security milestone is to move policy to a signed RAMFS/ISO file, attach
an identity and role to each process, issue a short-lived administration
capability after authentication, and record an append-only audit journal.

## Memory discipline

The manager is deliberately not a process-per-unit design. Ten fixed unit
records live in one static table; graphical boots activate all ten, while
headless boots activate the original eight. Dependency traversal uses the C
call stack, and there is no heap allocation, daemon bus, unit-file parser, or
background polling. The compositor is an existing isolated process referenced
by PID rather than copied into the manager.
`runas` retains only counters and a bounded 64-byte last-command audit field.

`make footprint-check` enforces three regression budgets: combined init/runas
object footprint at most 4 KiB, combined BSS at most 256 bytes, and total linked
kernel memory footprint at most 1 MiB (including embedded native desktop
assets). These are build failures, not advisory numbers.
