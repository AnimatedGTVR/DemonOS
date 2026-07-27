#include <demon/input.h>
#include <kernel/ipc.h>
#include <kernel/scheduler.h>
#include <kernel/userspace.h>

#include <stddef.h>

#define INPUT_QUEUE_CAPACITY 128u

static volatile struct input_event queue[INPUT_QUEUE_CAPACITY];
static volatile uint16_t read_index;
static volatile uint16_t write_index;
static volatile uint64_t dropped_events;
static volatile uint64_t userspace_deliveries;
static volatile uint64_t coalesced_events;
struct input_waiter {
    uint32_t pid;
    uint64_t address;
    bool active;
};
static struct input_waiter waiter;

void input_init(void) {
    read_index = 0u;
    write_index = 0u;
    dropped_events = 0u;
    userspace_deliveries = 0u;
    coalesced_events = 0u;
    waiter = (struct input_waiter){0};
}

bool input_publish(const struct input_event *event) {
    if (event == NULL || event->type < INPUT_KEY_DOWN || event->type > INPUT_MOUSE_SCROLL)
        return false;
    if (waiter.active) {
        const uint32_t pid = waiter.pid;
        const uint64_t address = waiter.address;
        waiter.active = false;
        (void)ipc_cancel_wait(pid);
        if (userspace_copy_to(pid, address, (const uint8_t *)event, sizeof(*event)) &&
            scheduler_wake(pid, 2u, sizeof(*event))) {
            ++userspace_deliveries;
            return true;
        }
    }
    /* Only the newest position matters inside one uninterrupted motion burst.
       Never cross a button/key/scroll boundary, because those establish input
       ordering and capture state. */
    if (event->type == INPUT_MOUSE_MOVE && read_index != write_index) {
        const uint16_t previous = (uint16_t)((write_index + INPUT_QUEUE_CAPACITY - 1u) %
                                             INPUT_QUEUE_CAPACITY);
        if (queue[previous].type == INPUT_MOUSE_MOVE) {
            queue[previous] = *event;
            ++coalesced_events;
            return true;
        }
    }
    uint16_t next = (uint16_t)((write_index + 1u) % INPUT_QUEUE_CAPACITY);
    if (next == read_index) { ++dropped_events; return false; }
    queue[write_index] = *event;
    write_index = next;
    return true;
}

bool input_poll(struct input_event *event) {
    if (event == NULL || read_index == write_index) return false;
    *event = queue[read_index];
    read_index = (uint16_t)((read_index + 1u) % INPUT_QUEUE_CAPACITY);
    return true;
}

bool input_wait(uint32_t pid, uint64_t user_address) {
    if (pid == 0u || user_address == 0u || waiter.active) return false;
    waiter = (struct input_waiter){.pid = pid, .address = user_address, .active = true};
    return true;
}

bool input_cancel_wait(uint32_t pid) {
    if (!waiter.active || waiter.pid != pid) return false;
    waiter.active = false;
    return true;
}

void input_discard_pending(void) {
    /* Queue indices are also touched from IRQ context. Terminal-mode keyboard
       input is read from the shell character ring, so its mirrored unified
       events (and unused pointer motion) must not accumulate for the next
       foreground application. */
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    read_index = write_index;
    if ((flags & (1ull << 9u)) != 0u) __asm__ volatile ("sti" ::: "memory");
}

uint32_t input_pending(void) {
    int32_t pending = (int32_t)write_index - (int32_t)read_index;
    if (pending < 0) pending += INPUT_QUEUE_CAPACITY;
    return (uint32_t)pending;
}

uint64_t input_dropped(void) { return dropped_events; }
uint64_t input_userspace_deliveries(void) { return userspace_deliveries; }
uint64_t input_coalesced(void) { return coalesced_events; }

bool input_self_test(void) {
    struct input_event sent = {.timestamp = 7u, .type = INPUT_MOUSE_MOVE,
        .x = 10, .y = 11, .delta_x = 2, .delta_y = -1};
    struct input_event received;
    input_init();
    if (!input_publish(&sent) || input_pending() != 1u || !input_poll(&received)) return false;
    bool passed = received.timestamp == 7u && received.type == INPUT_MOUSE_MOVE &&
        received.x == 10 && received.delta_y == -1 && input_pending() == 0u;
    input_init();
    return passed;
}
