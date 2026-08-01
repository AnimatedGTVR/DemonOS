#ifndef DOOM_LIBC_H
#define DOOM_LIBC_H

#include <stddef.h>
#include <stdarg.h>

/* Freestanding libc subset for native engine ports. Most functions are pure
   computation; allocation delegates to the initialized PortKit runtime.
   Console-facing stdio belongs in the platform shim on top of vsnprintf. */

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);
int memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strtok(char *str, const char *delim);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
char *strdup(const char *s);

int tolower(int c);
int toupper(int c);
int isalnum(int c);
int isalpha(int c);
int isdigit(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int isprint(int c);
int iscntrl(int c);
int ispunct(int c);

long strtol(const char *s, char **endptr, int base);
int atoi(const char *s);
long atol(const char *s);
int abs(int v);
long labs(long v);

void srand(unsigned seed);
int rand(void);

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));

int vsnprintf(char *buf, size_t capacity, const char *fmt, va_list ap);
int snprintf(char *buf, size_t capacity, const char *fmt, ...);

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

#endif
