#ifndef DOOMGENERIC_DEMONOS_H
#define DOOMGENERIC_DEMONOS_H

#include <stddef.h>
#include <stdint.h>

/* The window/surface owner installs these callbacks before
   doomgeneric_Create().  Doom remains a normal compositor client: it never
   writes the physical framebuffer and it does not know DemonX wire details. */
struct demon_doom_backend {
    void (*present)(const uint32_t *pixels, uint32_t width, uint32_t height,
                    void *context);
    void (*set_title)(const char *title, void *context);
    void *context;
};

void demon_doom_install_backend(const struct demon_doom_backend *backend);

#endif
