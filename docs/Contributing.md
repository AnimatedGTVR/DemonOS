# Contributing to DemonOS

Thank you for your interest in contributing to DemonOS.

DemonOS is an experimental operating system and custom kernel focused on being lightweight, understandable, and capable of running real software without relying on a traditional Linux or Unix userspace.

The project is still under heavy development. Large parts of the kernel, drivers, userspace, graphics stack, and tooling may change as the system grows.

## Ways to Contribute

Contributions are welcome in areas such as:

* Kernel development
* Device drivers
* Memory management
* Process scheduling
* Filesystems
* Networking
* Graphics and framebuffer support
* Mouse and keyboard input
* Userspace programs
* Libraries and system APIs
* Documentation
* Build system improvements
* Testing on real hardware and virtual machines
* Bug reports and debugging

Small improvements are welcome. You do not need to implement an entire subsystem to contribute.

## Before You Start

Before working on a large feature, open an issue or discussion describing what you plan to build.

This helps prevent duplicated work and makes sure the feature fits the direction of DemonOS.

For small bug fixes, documentation corrections, or cleanup work, you may submit a pull request directly.

## Development Requirements

The exact tools may change, but DemonOS development generally requires:

* A Unix-like development environment
* GNU Make
* A C compiler
* An assembler
* A linker capable of producing freestanding binaries
* QEMU
* Git

Depending on the part of the project, additional tools may be required.

Check the main `README.md` and build scripts for the current setup instructions.

## Building DemonOS

Clone the repository:

```bash
git clone https://github.com/AnimatedGTVR/DemonOS.git
cd DemonOS
```

Build the project:

```bash
make
```

Run it in QEMU:

```bash
make run
```

Available Make targets may change. Run the following command to inspect the build system:

```bash
make help
```

If `make help` is unavailable, check the `Makefile` directly.

## Code Guidelines

DemonOS values readable and maintainable low-level code.

When contributing:

* Prefer simple code over clever code.
* Keep functions focused on one responsibility.
* Use descriptive names.
* Add comments when the reasoning is not obvious.
* Avoid unnecessary dependencies.
* Do not assume a hosted C environment.
* Avoid using standard-library functions unless DemonOS provides them.
* Check pointer validity and memory boundaries carefully.
* Treat warnings as potential bugs.
* Preserve compatibility with the existing architecture unless a change has been discussed.

Kernel code must remain freestanding and should not depend on Linux-specific APIs.

## Formatting

Follow the style already used in the surrounding files.

For C code:

* Use four spaces for indentation.
* Do not use tabs for indentation.
* Place opening braces on the same line as declarations and conditions.
* Keep lines reasonably short.
* Use `snake_case` for functions and variables.
* Use uppercase names for constants and macros.
* Use fixed-width integer types where the size matters.

Example:

```c
static int process_create(const char *path) {
    if (path == NULL) {
        return -1;
    }

    return 0;
}
```

Consistency with nearby code is more important than applying a different personal style.

## Kernel Safety

Kernel mistakes can corrupt memory, crash the system, or silently damage unrelated components.

Take extra care when modifying:

* Page tables
* Physical memory allocation
* Interrupt handlers
* Context switching
* Userspace transitions
* Framebuffer memory
* DMA
* Filesystem writes
* Privilege checks
* System calls

Test memory-related changes with assertions, logging, and multiple workloads whenever possible.

Never trust pointers or values provided by userspace.

## Architecture

Avoid placing architecture-specific code inside generic kernel components.

Architecture-specific code should remain inside the appropriate architecture directory whenever possible.

Generic systems should communicate with architecture code through clear interfaces.

For example:

```text
kernel/
arch/
drivers/
userspace/
libs/
tools/
```

The actual repository structure may differ. Follow the existing layout unless a restructure has been discussed.

## Commit Messages

Use clear commit messages that explain what changed.

Good examples:

```text
Add PS/2 mouse packet parsing
Fix user stack alignment during ELF loading
Prevent framebuffer writes under user CR3
Document the process scheduler
```

Avoid vague messages such as:

```text
update
stuff
fix
changes
```

A commit should ideally represent one logical change.

## Pull Requests

Pull requests should include:

* A clear explanation of the change
* Why the change is needed
* How the change was tested
* Any known limitations
* Screenshots or logs when relevant
* Links to related issues

Keep pull requests focused. Avoid combining unrelated features, formatting changes, and refactors into one pull request.

Draft pull requests are welcome for unfinished work that needs feedback.

## Testing

Test your changes before submitting them.

At minimum:

1. Confirm the project builds successfully.
2. Boot DemonOS in QEMU.
3. Test the feature you changed.
4. Check that existing functionality still works.
5. Review compiler warnings and runtime logs.

When possible, test using multiple QEMU configurations or real hardware.

Include your test environment in the pull request.

Example:

```text
Tested with:

- QEMU 10.x
- x86_64
- 512 MB RAM
- PS/2 keyboard and mouse
- Standard VGA framebuffer
```

## Reporting Bugs

When reporting a bug, include:

* A clear description of the problem
* Steps to reproduce it
* What you expected to happen
* What actually happened
* The commit or version tested
* Your host operating system
* Your compiler and QEMU versions
* Logs, screenshots, or crash information
* Whether the issue occurs on real hardware, QEMU, or both

Do not report only that something “does not work.” Provide enough information for another developer to reproduce the problem.

## Feature Requests

Feature requests are welcome, but DemonOS has limited development resources.

A useful feature request should explain:

* What the feature does
* Why it belongs in DemonOS
* How it could interact with existing systems
* Whether you are interested in implementing it

Features may be declined if they add excessive complexity, conflict with the project direction, or require dependencies that DemonOS cannot support.

## Generated and Imported Code

Do not submit code copied from another project unless its license is compatible with DemonOS and proper attribution is included.

When adapting external code:

* Include the original copyright notice.
* Preserve required license information.
* Link to the original project.
* Explain what was changed.
* Confirm that redistribution is allowed.

AI-assisted contributions are allowed, but contributors are responsible for understanding, testing, and maintaining everything they submit.

Do not submit large amounts of generated code without reviewing it carefully.

> **AI-generated code is welcome.** If you use AI tools to help write code, you are responsible for understanding, reviewing, testing, and maintaining anything you submit.
>
> DemonOS itself also uses AI as a development tool in places. Every AI-generated contribution is reviewed before being accepted into the project.
>
> — AnimatedGTVR

## Licensing

By contributing to DemonOS, you agree that your contribution may be distributed under the license used by the project.

Do not submit code that you do not have permission to contribute.

Third-party code must be clearly identified and must use a compatible license.

## Conduct

Be respectful and constructive.

Disagreement is normal, especially in systems programming, but personal attacks, harassment, discrimination, and intentionally disruptive behavior are not acceptable.

Critique the code and the technical decision—not the person who wrote it.

## Getting Help

When asking for help:

* Explain what you are trying to do.
* Show the relevant code.
* Include compiler errors or logs.
* Mention what you have already tried.
* Keep the question focused.

DemonOS is a learning-focused project, so questions are welcome. Contributors are still expected to make a reasonable attempt to investigate problems before asking others to solve them.

## Final Note

DemonOS is ambitious, experimental, and constantly evolving.

Contributions should move the project toward a system that is small, understandable, reliable, and genuinely useful.

Thank you for helping build it.
