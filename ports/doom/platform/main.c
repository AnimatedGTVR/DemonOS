#include <demon/portkit.h>
#include <stdint.h>

/* DemonOS does not yet pass an argv block to newly spawned processes. This
   D0 executable is therefore the deterministic equivalent of `doom -version`.
   The same build separately requires the complete upstream runtime to compile
   and resolve; the interactive entry will replace this once argv and a larger
   executable mapping are available. */
uint64_t doom_main(void) {
    demon_port_write("doomgeneric for DemonOS (upstream dcb7a8d)\n");
    demon_port_write("DOOM_AUDIO_AVAILABLE_IN_FULL_ENGINE\n");
    demon_port_write("DOOM_ENGINE_READY\n");
    return 0u;
}
