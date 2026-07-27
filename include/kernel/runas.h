#ifndef KERNEL_RUNAS_H
#define KERNEL_RUNAS_H

#include <stdbool.h>
#include <stdint.h>

void runas_init(void);
bool runas_authorize(const char *command);
bool runas_self_test(void);
uint64_t runas_grants(void);
uint64_t runas_denials(void);
const char *runas_last_command(void);

#endif
