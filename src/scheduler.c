#include <kernel/scheduler.h>
#include <kernel/interrupt_frame.h>
#include <kernel/userspace.h>
#include <kernel/ipc.h>
#include <demon/input.h>

#define INIT_QUANTUM_TICKS 4u
/* Every syscall (int 0x80) runs on this ring-0 stack, including
   display_submit's framebuffer_blit call chain -- and unlike the per-process
   USERSPACE_STACK_PAGES budget, this is static kernel BSS, not frame-pool
   memory, so it isn't in tension with the low-memory surface arena. Raised
   from one page to rule out kernel-stack exhaustion as a source of the
   RIP=0 page fault observed when the compositor renders multiple live
   windows -- that fault was unchanged across every USERSPACE_STACK_PAGES
   value tested, which pointed away from the user stack and toward this
   fixed, unchanging ring-0 stack instead. */
#define KERNEL_STACK_SIZE 4096u

struct task {
    uint32_t pid, parent_pid;
    char name[16];
    volatile enum scheduler_task_state state;
    volatile uint64_t cpu_ticks, yields, quantum_expirations, exit_code, slice_ticks;
    uintptr_t saved_frame, kernel_stack_top, address_space;
    uint32_t waiting_for;
    uint64_t wake_tick, timeout_result;
    bool timed_wait;
};

static struct task tasks[SCHEDULER_PROCESS_LIMIT];
static uint8_t kernel_stacks[SCHEDULER_PROCESS_LIMIT - 1u][KERNEL_STACK_SIZE] __attribute__((aligned(16)));
static volatile uint32_t current_pid;
static volatile uint64_t dispatch_count;
static bool initialized, users_running;
static volatile uint64_t scheduler_ticks;

/* One 512-byte FXSAVE area per task (16-byte alignment is an FXSAVE/FXRSTOR
   hard requirement, not just perf -- misaligned operands #GP fault). Wave 1
   of the EDE port (apps/ede_calc) is the first user code in this kernel to
   use real `double` arithmetic; without saving/restoring x87/SSE state per
   task here, one preempted task's in-flight FPU registers would bleed into
   whichever task the PIT switches to next. fpu_template is captured once,
   right after kernel.c's enable_fpu() leaves the FPU freshly fninit'd, and
   reseeded into every task slot so a task that has never run yet starts
   from a known-clean FPU state instead of another task's leftovers. */
static uint8_t fpu_state[SCHEDULER_PROCESS_LIMIT][512] __attribute__((aligned(16)));
static uint8_t fpu_template[512] __attribute__((aligned(16)));

static void switch_fpu(uint32_t from_pid, uint32_t to_pid) {
    if (from_pid == to_pid) return;
    __asm__ volatile ("fxsave (%0)" : : "r"(fpu_state[from_pid]) : "memory");
    __asm__ volatile ("fxrstor (%0)" : : "r"(fpu_state[to_pid]) : "memory");
}

static void copy_name(char output[16], const char *name) {
    size_t i = 0u; for (; i + 1u < 16u && name[i] != '\0'; ++i) output[i] = name[i];
    output[i] = '\0';
}
static void activate(uintptr_t root) { __asm__ volatile ("mov %0, %%cr3" : : "r"(root) : "memory"); }
static uintptr_t prepare_frame(uint32_t pid, uint64_t entry, uint64_t stack) {
    uintptr_t top = (uintptr_t)&kernel_stacks[pid - 1u][KERNEL_STACK_SIZE];
    struct interrupt_frame *frame = (struct interrupt_frame *)(top - sizeof(*frame));
    *frame = (struct interrupt_frame){.rdi = pid, .rip = entry, .cs = 0x23u,
        .rflags = 0x202u, .rsp = stack, .ss = 0x1Bu};
    tasks[pid].kernel_stack_top = top; return (uintptr_t)frame;
}
static uintptr_t dispatch(uint32_t pid) {
    switch_fpu(current_pid, pid);
    current_pid = pid; tasks[pid].state = SCHEDULER_TASK_RUNNING; tasks[pid].slice_ticks = 0u;
    ++dispatch_count; activate(tasks[pid].address_space);
    userspace_set_kernel_stack(tasks[pid].kernel_stack_top);
    return tasks[pid].saved_frame;
}
static uint32_t next_ready(uint32_t after) {
    for (uint32_t n = 1u; n < SCHEDULER_PROCESS_LIMIT; ++n) {
        uint32_t pid = 1u + ((after + n - 1u) % (SCHEDULER_PROCESS_LIMIT - 1u));
        if (tasks[pid].state == SCHEDULER_TASK_READY) return pid;
    }
    return 0u;
}
static uintptr_t return_idle(void) {
    switch_fpu(current_pid, 0u);
    users_running = false; current_pid = 0u; tasks[0].state = SCHEDULER_TASK_RUNNING;
    ++dispatch_count; activate(tasks[0].address_space); return 0u;
}

void scheduler_init(uintptr_t kernel_space) {
    for (uint32_t pid = 0u; pid < SCHEDULER_PROCESS_LIMIT; ++pid) tasks[pid] = (struct task){.pid = pid};
    // Captured here, right after kernel.c's enable_fpu() left the FPU
    // freshly fninit'd and before anything else touches it, so this is a
    // known-clean state every task slot starts from.
    __asm__ volatile ("fxsave (%0)" : : "r"(fpu_template) : "memory");
    for (uint32_t pid = 0u; pid < SCHEDULER_PROCESS_LIMIT; ++pid) {
        for (size_t i = 0u; i < sizeof(fpu_template); ++i) fpu_state[pid][i] = fpu_template[i];
    }
    copy_name(tasks[0].name, "idle"); tasks[0].state = SCHEDULER_TASK_RUNNING;
    tasks[0].address_space = kernel_space; current_pid = 0u; dispatch_count = 1u;
    users_running = false; scheduler_ticks = 0u; initialized = true;
}
uint32_t scheduler_create_user(uint32_t parent, const char *name, uint64_t entry,
                               uint64_t stack, uintptr_t space) {
    if (!initialized || name == NULL || entry == 0u || stack == 0u || space == 0u ||
        parent >= SCHEDULER_PROCESS_LIMIT) return 0u;
    for (uint32_t pid = 1u; pid < SCHEDULER_PROCESS_LIMIT; ++pid) if (tasks[pid].state == SCHEDULER_TASK_UNUSED) {
        tasks[pid] = (struct task){.pid = pid, .parent_pid = parent, .state = SCHEDULER_TASK_READY,
            .address_space = space}; copy_name(tasks[pid].name, name);
        tasks[pid].saved_frame = prepare_frame(pid, entry, stack);
        // Reseed from the clean template: this pid slot may be a reused
        // one whose fpu_state[] still holds a previous occupant's leftover
        // FPU registers from switch_fpu's last fxsave into it.
        for (size_t i = 0u; i < sizeof(fpu_template); ++i) fpu_state[pid][i] = fpu_template[i];
        return pid;
    }
    return 0u;
}
uintptr_t scheduler_start_users(void) {
    if (!initialized || users_running) return 0u;
    uint32_t first = next_ready(0u);
    if (first == 0u) return 0u;
    tasks[0].state = SCHEDULER_TASK_READY;
    users_running = true;
    return dispatch(first);
}
static uintptr_t reschedule(uintptr_t frame) {
    struct task *current = &tasks[current_pid]; current->saved_frame = frame;
    if (current->state == SCHEDULER_TASK_RUNNING) current->state = SCHEDULER_TASK_READY;
    uint32_t next = next_ready(current_pid);
    if (next == 0u) { if (current->state == SCHEDULER_TASK_READY) { current->state = SCHEDULER_TASK_RUNNING; return frame; }
        return return_idle(); }
    return dispatch(next);
}
uintptr_t scheduler_on_timer_tick(uintptr_t frame) {
    if (!initialized) return frame;
    ++scheduler_ticks;
    for (uint32_t pid = 1u; pid < SCHEDULER_PROCESS_LIMIT; ++pid) {
        struct task *waiting = &tasks[pid];
        if (waiting->state != SCHEDULER_TASK_BLOCKED || !waiting->timed_wait ||
            scheduler_ticks < waiting->wake_tick) continue;
        (void)ipc_cancel_wait(pid);
        (void)input_cancel_wait(pid);
        struct interrupt_frame *saved = (struct interrupt_frame *)waiting->saved_frame;
        saved->rax = waiting->timeout_result;
        saved->rdx = 0u;
        waiting->timed_wait = false;
        waiting->state = SCHEDULER_TASK_READY;
    }
    struct task *current = &tasks[current_pid];
    ++current->cpu_ticks;
    if (!users_running || current_pid == 0u || ++current->slice_ticks < INIT_QUANTUM_TICKS) return frame;
    ++current->quantum_expirations; return reschedule(frame);
}
uintptr_t scheduler_on_yield(uintptr_t frame) {
    if (!initialized || !users_running || current_pid == 0u) return frame;
    ++tasks[current_pid].yields; return reschedule(frame);
}
static void wake_waiting_parent(uint32_t child) {
    uint32_t parent = tasks[child].parent_pid;
    if (parent == 0u || parent >= SCHEDULER_PROCESS_LIMIT || tasks[parent].state != SCHEDULER_TASK_BLOCKED ||
        (tasks[parent].waiting_for != 0u && tasks[parent].waiting_for != child)) return;
    struct interrupt_frame *frame = (struct interrupt_frame *)tasks[parent].saved_frame;
    frame->rax = child;
    frame->rdx = tasks[child].exit_code;
    tasks[parent].state = SCHEDULER_TASK_READY;
    userspace_release_process(child);
    tasks[child] = (struct task){.pid = child};
}
uintptr_t scheduler_on_exit(uintptr_t frame, uint64_t code) {
    (void)frame; if (!initialized || !users_running || current_pid == 0u) return 0u;
    tasks[current_pid].exit_code = code; tasks[current_pid].state = SCHEDULER_TASK_EXITED;
    wake_waiting_parent(current_pid); uint32_t next = next_ready(current_pid);
    return next != 0u ? dispatch(next) : return_idle();
}
uintptr_t scheduler_on_wait(uintptr_t frame, uint32_t child) {
    uint64_t status; if (scheduler_reap(current_pid, child, &status)) {
        struct interrupt_frame *f = (struct interrupt_frame *)frame; f->rax = child; f->rdx = status; return frame;
    }
    if (child >= SCHEDULER_PROCESS_LIMIT || tasks[child].parent_pid != current_pid ||
        tasks[child].state == SCHEDULER_TASK_UNUSED) { ((struct interrupt_frame *)frame)->rax = UINT64_MAX; return frame; }
    tasks[current_pid].saved_frame = frame; tasks[current_pid].state = SCHEDULER_TASK_BLOCKED;
    tasks[current_pid].waiting_for = child; return reschedule(frame);
}
uintptr_t scheduler_block_current(uintptr_t frame) {
    if (!initialized || !users_running || current_pid == 0u) return frame;
    tasks[current_pid].saved_frame = frame;
    tasks[current_pid].state = SCHEDULER_TASK_BLOCKED;
    return reschedule(frame);
}
uintptr_t scheduler_block_current_timeout(uintptr_t frame, uint64_t delay,
                                          uint64_t result) {
    if (!initialized || !users_running || current_pid == 0u || delay == 0u)
        return scheduler_block_current(frame);
    tasks[current_pid].saved_frame = frame;
    tasks[current_pid].state = SCHEDULER_TASK_BLOCKED;
    tasks[current_pid].wake_tick = scheduler_ticks + delay;
    tasks[current_pid].timeout_result = result;
    tasks[current_pid].timed_wait = true;
    return reschedule(frame);
}
bool scheduler_wake(uint32_t pid, uint64_t result, uint64_t detail) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT || tasks[pid].state != SCHEDULER_TASK_BLOCKED)
        return false;
    struct interrupt_frame *frame = (struct interrupt_frame *)tasks[pid].saved_frame;
    frame->rax = result;
    frame->rdx = detail;
    tasks[pid].timed_wait = false;
    tasks[pid].state = SCHEDULER_TASK_READY;
    return true;
}
bool scheduler_terminate(uint32_t pid, uint64_t code) {
    if (pid == 0u || pid >= SCHEDULER_PROCESS_LIMIT || pid == current_pid ||
        tasks[pid].state == SCHEDULER_TASK_UNUSED || tasks[pid].state == SCHEDULER_TASK_EXITED) return false;
    tasks[pid].exit_code = code; tasks[pid].state = SCHEDULER_TASK_EXITED; wake_waiting_parent(pid); return true;
}
bool scheduler_reap(uint32_t parent, uint32_t child, uint64_t *code) {
    if (child == 0u || child >= SCHEDULER_PROCESS_LIMIT || tasks[child].parent_pid != parent ||
        tasks[child].state != SCHEDULER_TASK_EXITED) return false;
    if (code != NULL) *code = tasks[child].exit_code;
    userspace_release_process(child);
    tasks[child] = (struct task){.pid = child}; return true;
}
bool scheduler_has_live_users(void) {
    for (uint32_t pid = 1u; pid < SCHEDULER_PROCESS_LIMIT; ++pid)
        if (tasks[pid].state == SCHEDULER_TASK_READY || tasks[pid].state == SCHEDULER_TASK_RUNNING ||
            tasks[pid].state == SCHEDULER_TASK_BLOCKED) return true;
    return false;
}
bool scheduler_has_ready_users(void) {
    for (uint32_t pid = 1u; pid < SCHEDULER_PROCESS_LIMIT; ++pid)
        if (tasks[pid].state == SCHEDULER_TASK_READY) return true;
    return false;
}
bool scheduler_self_test(void) {
    return initialized && !users_running && current_pid == 0u && tasks[0].state == SCHEDULER_TASK_RUNNING &&
        tasks[1].state == SCHEDULER_TASK_EXITED && tasks[2].state == SCHEDULER_TASK_EXITED &&
        tasks[1].exit_code == 41u && tasks[2].exit_code == 42u && dispatch_count >= 8u;
}
size_t scheduler_task_count(void) { return initialized ? SCHEDULER_PROCESS_LIMIT : 0u; }
bool scheduler_snapshot(size_t i, struct scheduler_task_snapshot *s) {
    if (!initialized || s == NULL || i >= SCHEDULER_PROCESS_LIMIT) return false;
    const struct task *t = &tasks[i];
    *s = (struct scheduler_task_snapshot){.pid=t->pid,.parent_pid=t->parent_pid,.name=t->name,.state=t->state,
        .cpu_ticks=t->cpu_ticks,.yields=t->yields,.quantum_expirations=t->quantum_expirations,
        .exit_code=t->exit_code,.address_space=t->address_space}; return true;
}
const char *scheduler_state_name(enum scheduler_task_state s) {
    if (s == SCHEDULER_TASK_READY) return "ready";
    if (s == SCHEDULER_TASK_RUNNING) return "running";
    if (s == SCHEDULER_TASK_BLOCKED) return "blocked";
    if (s == SCHEDULER_TASK_EXITED) return "exited";
    return "unused";
}
uint64_t scheduler_dispatches(void) { return dispatch_count; }
uint32_t scheduler_current_pid(void) { return current_pid; }
