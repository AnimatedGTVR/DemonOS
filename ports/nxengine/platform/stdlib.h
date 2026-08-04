/*
Freestanding shadow of <stdlib.h> for the C++ engine units -- see math.h in
this directory for why. Declarations mirror apps/doom/libc.h's existing
subset; malloc/free/realloc/abs/rand are actually implemented there
(apps/doom/libc.c), shared with the Doom and Quake ports.
*/
#ifndef NXENGINE_DEMONOS_STDLIB_H
#define NXENGINE_DEMONOS_STDLIB_H

#include <stddef.h>

/* real glibc value; common/misc_comm.cpp's random() compares a range
   against it directly. */
#define RAND_MAX 2147483647

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

int abs(int v);
long labs(long v);
long strtol(const char *s, char **endptr, int base);
int atoi(const char *s);
long atol(const char *s);
double atof(const char *s);

void srand(unsigned seed);
int rand(void);

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));

/* Declarations only, for graphics.cpp's never-called Graphics::init/
   InitVideo to compile -- see the comment on the SDL_SetVideoMode group
   in SDL.h. */
int putenv(char *string);
void exit(int status);

#ifdef __cplusplus
}
#endif

#endif
