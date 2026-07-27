#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum init_unit_state {
    INIT_UNIT_INACTIVE,
    INIT_UNIT_WAITING,
    INIT_UNIT_ACTIVE,
    INIT_UNIT_FAILED,
};

struct init_unit_snapshot {
    const char *name;
    const char *description;
    enum init_unit_state state;
    bool mutable;
    uint64_t starts;
    uint64_t stops;
    uint32_t process_id;
};

void init_system_init(void);
bool init_system_reach_boot_target(uint32_t compositor_pid);
size_t init_system_unit_count(void);
bool init_system_snapshot(size_t index, struct init_unit_snapshot *snapshot);
const char *init_system_state_name(enum init_unit_state state);
bool init_system_start(const char *name);
bool init_system_stop(const char *name);
bool init_system_restart(const char *name);
bool init_system_self_test(void);
uint64_t init_system_active_count(void);
uint64_t init_system_transaction_count(void);
uint32_t init_system_desktop_pid(void);
bool init_system_desktop_active(void);

#endif
