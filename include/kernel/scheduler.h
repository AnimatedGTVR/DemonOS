#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCHEDULER_PROCESS_LIMIT 8u

struct interrupt_frame;

enum scheduler_task_state {
    SCHEDULER_TASK_UNUSED,
    SCHEDULER_TASK_READY,
    SCHEDULER_TASK_RUNNING,
    SCHEDULER_TASK_BLOCKED,
    SCHEDULER_TASK_EXITED,
};

struct scheduler_task_snapshot {
    uint32_t pid;
    uint32_t parent_pid;
    const char *name;
    enum scheduler_task_state state;
    uint64_t cpu_ticks;
    uint64_t yields;
    uint64_t quantum_expirations;
    uint64_t exit_code;
    uint64_t address_space;
};

void scheduler_init(uintptr_t kernel_address_space);
uint32_t scheduler_create_user(uint32_t parent_pid, const char *name, uint64_t entry,
                               uint64_t user_stack, uintptr_t address_space);
uintptr_t scheduler_start_users(void);
uintptr_t scheduler_on_timer_tick(uintptr_t frame_address);
uintptr_t scheduler_on_yield(uintptr_t frame_address);
uintptr_t scheduler_on_exit(uintptr_t frame_address, uint64_t exit_code);
uintptr_t scheduler_on_wait(uintptr_t frame_address, uint32_t child_pid);
uintptr_t scheduler_block_current(uintptr_t frame_address);
uintptr_t scheduler_block_current_timeout(uintptr_t frame_address,
                                          uint64_t delay_ticks,
                                          uint64_t timeout_result);
bool scheduler_wake(uint32_t pid, uint64_t result, uint64_t detail);
bool scheduler_terminate(uint32_t pid, uint64_t exit_code);
bool scheduler_reap(uint32_t parent_pid, uint32_t child_pid, uint64_t *exit_code);
uint32_t scheduler_reap_exited(uint32_t parent_pid, uint64_t *exit_code);
bool scheduler_has_live_users(void);
bool scheduler_has_ready_users(void);

bool scheduler_self_test(void);
size_t scheduler_task_count(void);
bool scheduler_snapshot(size_t index, struct scheduler_task_snapshot *snapshot);
const char *scheduler_state_name(enum scheduler_task_state state);
uint64_t scheduler_dispatches(void);
uint32_t scheduler_current_pid(void);

#endif
