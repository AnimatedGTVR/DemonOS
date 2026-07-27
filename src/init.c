#include <kernel/init.h>
#include <kernel/scheduler.h>

#define NO_DEPENDENCY 255u

struct init_unit {
    const char *name;
    const char *description;
    uint8_t dependency;
    enum init_unit_state state;
    bool mutable;
    uint64_t starts;
    uint64_t stops;
    uint32_t process_id;
};

static struct init_unit units[] = {
    { "kernel.target", "Core kernel facilities", NO_DEPENDENCY, INIT_UNIT_INACTIVE, false, 0u, 0u, 0u },
    { "hardware.target", "Interrupt, timer, and keyboard drivers", 0u, INIT_UNIT_INACTIVE, false, 0u, 0u, 0u },
    { "project-store.service", "RAM project filesystem", 0u, INIT_UNIT_INACTIVE, false, 0u, 0u, 0u },
    { "userspace.target", "ELF loader and isolated process runtime", 2u, INIT_UNIT_INACTIVE, false, 0u, 0u, 0u },
    { "project-host.service", "Portable MKO project host", 3u, INIT_UNIT_INACTIVE, true, 0u, 0u, 0u },
    { "app-manager.service", "Installed application catalog", 4u, INIT_UNIT_INACTIVE, false, 0u, 0u, 0u },
    { "console.service", "MakoBox interactive console", 1u, INIT_UNIT_INACTIVE, false, 0u, 0u, 0u },
    { "default.target", "Multi-user MAKO system", 5u, INIT_UNIT_INACTIVE, false, 0u, 0u, 0u },
    { "desktop-compositor.service", "Persistent ring-3 MKO display server", 7u, INIT_UNIT_INACTIVE, false, 0u, 0u, 0u },
    { "desktop.target", "Native MAKO graphical session", 8u, INIT_UNIT_INACTIVE, false, 0u, 0u, 0u },
};

static uint64_t transactions;
static bool graphical_boot;

static bool text_equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static struct init_unit *find_unit(const char *name) {
    for (size_t i = 0u; i < sizeof(units) / sizeof(units[0]); ++i)
        if (text_equal(units[i].name, name)) return &units[i];
    return NULL;
}

static bool start_index(size_t index, uint32_t depth) {
    if (index >= sizeof(units) / sizeof(units[0]) || depth > sizeof(units) / sizeof(units[0]))
        return false;
    struct init_unit *unit = &units[index];
    if (unit->state == INIT_UNIT_ACTIVE) return true;
    unit->state = INIT_UNIT_WAITING;
    if (unit->dependency != NO_DEPENDENCY &&
        !start_index(unit->dependency, depth + 1u)) {
        unit->state = INIT_UNIT_FAILED;
        return false;
    }
    if (index == 8u) {
        struct scheduler_task_snapshot process;
        if (unit->process_id == 0u ||
            !scheduler_snapshot(unit->process_id, &process) ||
            process.state != SCHEDULER_TASK_BLOCKED ||
            !text_equal(process.name, "compositor")) {
            unit->state = INIT_UNIT_FAILED;
            return false;
        }
    }
    unit->state = INIT_UNIT_ACTIVE;
    ++unit->starts;
    ++transactions;
    return true;
}

void init_system_init(void) {
    for (size_t i = 0u; i < sizeof(units) / sizeof(units[0]); ++i) {
        units[i].state = INIT_UNIT_INACTIVE;
        units[i].starts = 0u;
        units[i].stops = 0u;
        units[i].process_id = 0u;
    }
    transactions = 0u;
    graphical_boot = false;
}

bool init_system_reach_boot_target(uint32_t compositor_pid) {
    if (!start_index(7u, 0u) || !start_index(6u, 0u)) return false;
    if (compositor_pid == 0u) return true;
    units[8].process_id = compositor_pid;
    graphical_boot = true;
    return start_index(9u, 0u);
}

size_t init_system_unit_count(void) {
    return sizeof(units) / sizeof(units[0]);
}

bool init_system_snapshot(size_t index, struct init_unit_snapshot *snapshot) {
    if (index >= init_system_unit_count() || snapshot == NULL) return false;
    const struct init_unit *unit = &units[index];
    snapshot->name = unit->name;
    snapshot->description = unit->description;
    snapshot->state = unit->state;
    snapshot->mutable = unit->mutable;
    snapshot->starts = unit->starts;
    snapshot->stops = unit->stops;
    snapshot->process_id = unit->process_id;
    return true;
}

const char *init_system_state_name(enum init_unit_state state) {
    if (state == INIT_UNIT_ACTIVE) return "active";
    if (state == INIT_UNIT_WAITING) return "waiting";
    if (state == INIT_UNIT_FAILED) return "failed";
    return "inactive";
}

bool init_system_start(const char *name) {
    struct init_unit *unit = find_unit(name);
    if (unit == NULL || !unit->mutable) return false;
    return start_index((size_t)(unit - units), 0u);
}

bool init_system_stop(const char *name) {
    struct init_unit *unit = find_unit(name);
    if (unit == NULL || !unit->mutable || unit->state != INIT_UNIT_ACTIVE)
        return false;
    unit->state = INIT_UNIT_INACTIVE;
    ++unit->stops;
    ++transactions;
    return true;
}

bool init_system_restart(const char *name) {
    struct init_unit *unit = find_unit(name);
    if (unit == NULL || !unit->mutable) return false;
    if (unit->state == INIT_UNIT_ACTIVE && !init_system_stop(name)) return false;
    return init_system_start(name);
}

uint64_t init_system_active_count(void) {
    uint64_t active = 0u;
    for (size_t i = 0u; i < init_system_unit_count(); ++i)
        if (units[i].state == INIT_UNIT_ACTIVE) ++active;
    return active;
}

uint64_t init_system_transaction_count(void) { return transactions; }
uint32_t init_system_desktop_pid(void) { return units[8].process_id; }
bool init_system_desktop_active(void) {
    if (!graphical_boot || units[8].state != INIT_UNIT_ACTIVE ||
        units[9].state != INIT_UNIT_ACTIVE) return false;
    struct scheduler_task_snapshot process;
    return scheduler_snapshot(units[8].process_id, &process) &&
        process.state == SCHEDULER_TASK_BLOCKED;
}

bool init_system_self_test(void) {
    const uint64_t expected_active = graphical_boot ? 10u : 8u;
    if (init_system_active_count() != expected_active || transactions != expected_active ||
        (graphical_boot && !init_system_desktop_active()))
        return false;
    if (!init_system_restart("project-host.service")) return false;
    return init_system_active_count() == expected_active &&
        units[4].starts == 2u && units[4].stops == 1u &&
        transactions == expected_active + 2u;
}
