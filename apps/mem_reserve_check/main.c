#include <demon/portkit.h>
#include <demon/c_app.h>
#include <stddef.h>
#include <stdint.h>

/* W1 of docs/wine-port.md: proves demon_memory_reserve/demon_memory_commit
   (syscalls 46/47) against the real kernel, not just that they link.

   Safety note this test takes seriously: this kernel's page fault handler
   is diagnostic-only (see the comment on anonymous_reserved_pages in
   src/arch/x86_64/userspace.c) -- touching memory past the last successful
   commit halts the WHOLE KERNEL, not just this process. Every read/write
   below is therefore bounds-checked against exactly what the preceding
   commit call reported back, never against the full reservation size. */

uint64_t mem_reserve_check_main(void) {
    /* Reserve 4 MiB of address space -- no physical frames taken yet. This
       alone can't be observed from userspace (there is no "how many frames
       are free" query exposed here), so the real proof is everything below:
       committing, using, and running out of exactly what was reserved. */
    void *base = demon_memory_reserve(4u * 1024u * 1024u);
    if (base == NULL) return 10u;

    /* A second reservation for the same process must fail outright --
       mutual exclusion with the single-region model, not a second arena. */
    if (demon_memory_reserve(4096u) != NULL) return 11u;

    /* First commit: exactly one page, starting at the reservation base. */
    void *first = demon_memory_commit(4096u, true);
    if (first != base) return 12u;

    /* The committed page is real, present memory now -- write and read
       back a marker at both ends of it. */
    uint8_t *first_bytes = (uint8_t *)first;
    first_bytes[0] = 0x5Au;
    first_bytes[4095] = 0xA5u;
    if (first_bytes[0] != 0x5Au || first_bytes[4095] != 0xA5u) return 13u;

    /* Second commit: another page, must start exactly where the first
       one ended, not back at the reservation base. */
    void *second = demon_memory_commit(4096u, true);
    if (second != (uint8_t *)base + 4096) return 14u;
    uint8_t *second_bytes = (uint8_t *)second;
    second_bytes[0] = 0x11u;
    if (second_bytes[0] != 0x11u) return 15u;
    /* The two committed pages are genuinely distinct frames, not the same
       page mapped twice: writing the second must not have touched the
       first's already-verified marker. */
    if (first_bytes[0] != 0x5Au || first_bytes[4095] != 0xA5u) return 16u;

    /* Committing past what remains in the 4 MiB reservation must fail
       cleanly -- 4 MiB - 8 KiB already committed, so asking for all of it
       again overruns by construction. */
    if (demon_memory_commit(4u * 1024u * 1024u, true) != NULL) return 17u;

    /* A third, small commit must still succeed after that failed attempt
       (the failed call must not have corrupted the committed high-water
       mark), continuing from exactly where the second commit left off. */
    void *third = demon_memory_commit(4096u, true);
    if (third != (uint8_t *)base + 8192) return 18u;

    if (demon_memory_unmap(base) != 0u) return 19u;

    /* A fresh reserve-then-release cycle with no commit at all in between
       must also work cleanly (releasing a reservation nothing was ever
       committed into). */
    void *reserved_only = demon_memory_reserve(4096u);
    if (reserved_only == NULL) return 20u;
    if (demon_memory_unmap(reserved_only) != 0u) return 21u;

    /* After a full release, the region is available again for the older
       all-at-once demon_memory_map path -- proves the two APIs share one
       real region rather than silently leaking reservation state. */
    void *mapped = demon_memory_map(4096u);
    if (mapped == NULL) return 22u;
    if (demon_memory_unmap(mapped) != 0u) return 23u;

    demon_port_write("MEM_RESERVE_COMMIT_OK reserve commit distinct-frames "
                     "overrun-rejected release reuse\n");
    return 0u;
}
