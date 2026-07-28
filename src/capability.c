#include <kernel/capability.h>
#include <kernel/surface.h>

#include <stddef.h>

#define PROCESS_LIMIT 8u
#define SLOTS_PER_PROCESS 8u

struct capability_slot {
    enum capability_service service;
    uint32_t rights;
    uint16_t generation;
    uint32_t object_id;
    bool open;
};

static struct capability_slot tables[PROCESS_LIMIT][SLOTS_PER_PROCESS];
static uint64_t opened_count;
static uint64_t closed_count;
static uint64_t denied_count;
static uint32_t assigned_services[PROCESS_LIMIT];

static uint64_t make_handle(uint32_t pid, uint32_t slot, uint16_t generation) {
    return ((uint64_t)generation << 16u) | ((uint64_t)pid << 8u) | slot;
}

static bool decode_handle(uint64_t handle, uint32_t *pid, uint32_t *slot,
                          uint16_t *generation) {
    if ((handle >> 32u) != 0u) return false;
    *slot = (uint32_t)(handle & 0xFFu);
    *pid = (uint32_t)((handle >> 8u) & 0xFFu);
    *generation = (uint16_t)(handle >> 16u);
    return *pid < PROCESS_LIMIT && *slot < SLOTS_PER_PROCESS && *generation != 0u;
}

static uint32_t service_rights(uint32_t service) {
    if (service == CAPABILITY_SERVICE_CONSOLE) return CAPABILITY_RIGHT_WRITE;
    if (service == CAPABILITY_SERVICE_CLOCK || service == CAPABILITY_SERVICE_PROCESS)
        return CAPABILITY_RIGHT_QUERY;
    if (service == CAPABILITY_SERVICE_STORAGE)
        return CAPABILITY_RIGHT_OPEN | CAPABILITY_RIGHT_QUERY;
    if (service == CAPABILITY_SERVICE_DISPLAY)
        return CAPABILITY_RIGHT_WRITE | CAPABILITY_RIGHT_QUERY;
    if (service == CAPABILITY_SERVICE_INPUT)
        return CAPABILITY_RIGHT_READ | CAPABILITY_RIGHT_QUERY;
    if (service == CAPABILITY_SERVICE_SURFACE)
        return CAPABILITY_RIGHT_OPEN | CAPABILITY_RIGHT_QUERY;
    if (service == CAPABILITY_SERVICE_NETWORK)
        return CAPABILITY_RIGHT_READ | CAPABILITY_RIGHT_QUERY;
    return 0u;
}

static uint64_t allocate_slot(uint32_t pid, enum capability_service service,
                              uint32_t rights, uint32_t object_id) {
    for (uint32_t slot = 0u; slot < SLOTS_PER_PROCESS; ++slot) {
        struct capability_slot *entry = &tables[pid][slot];
        if (entry->open) continue;
        entry->service = service;
        entry->rights = rights;
        entry->object_id = object_id;
        entry->open = true;
        ++opened_count;
        return make_handle(pid, slot, entry->generation);
    }
    ++denied_count;
    return UINT64_MAX;
}

void capabilities_init(void) {
    for (uint32_t pid = 0u; pid < PROCESS_LIMIT; ++pid) {
        for (uint32_t slot = 0u; slot < SLOTS_PER_PROCESS; ++slot) {
            tables[pid][slot] = (struct capability_slot){ .generation = 1u };
        }
    }
    opened_count = 0u;
    closed_count = 0u;
    denied_count = 0u;
    for (uint32_t pid = 0u; pid < PROCESS_LIMIT; ++pid) assigned_services[pid] = 0u;
    assigned_services[1] = UINT32_MAX;
    assigned_services[2] = UINT32_MAX;
}

bool capability_assign_services(uint32_t pid, uint32_t service_mask) {
    if (pid == 0u || pid >= PROCESS_LIMIT) return false;
    assigned_services[pid] = service_mask; return true;
}

uint64_t capability_open(uint32_t pid, uint32_t service) {
    const uint32_t rights = service_rights(service);
    if (pid == 0u || pid >= PROCESS_LIMIT || rights == 0u ||
        (assigned_services[pid] & CAPABILITY_SERVICE_BIT(service)) == 0u) {
        ++denied_count;
        return UINT64_MAX;
    }
    return allocate_slot(pid, (enum capability_service)service, rights, 0u);
}

uint64_t capability_open_file(uint32_t pid, uint32_t object_id) {
    if (pid == 0u || pid >= PROCESS_LIMIT || object_id == 0u) {
        ++denied_count;
        return UINT64_MAX;
    }
    return allocate_slot(pid, CAPABILITY_SERVICE_FILE,
        CAPABILITY_RIGHT_READ | CAPABILITY_RIGHT_WRITE | CAPABILITY_RIGHT_QUERY,
        object_id);
}

uint64_t capability_open_ipc(uint32_t pid, uint32_t channel_id, uint32_t rights) {
    const uint32_t allowed = CAPABILITY_RIGHT_SEND | CAPABILITY_RIGHT_RECEIVE | CAPABILITY_RIGHT_QUERY;
    if (pid == 0u || pid >= PROCESS_LIMIT || channel_id == 0u || rights == 0u ||
        (rights & ~allowed) != 0u ||
        (assigned_services[pid] & CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_IPC)) == 0u) {
        ++denied_count;
        return UINT64_MAX;
    }
    return allocate_slot(pid, CAPABILITY_SERVICE_IPC, rights, channel_id);
}

uint64_t capability_open_surface(uint32_t pid, uint32_t surface_id,
                                 uint32_t rights) {
    const uint32_t allowed = CAPABILITY_RIGHT_READ | CAPABILITY_RIGHT_WRITE |
        CAPABILITY_RIGHT_QUERY;
    if (pid == 0u || pid >= PROCESS_LIMIT || surface_id == 0u || rights == 0u ||
        (rights & ~allowed) != 0u ||
        (assigned_services[pid] & CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE)) == 0u) {
        ++denied_count;
        return UINT64_MAX;
    }
    if (!surface_retain(surface_id)) {
        ++denied_count;
        return UINT64_MAX;
    }
    const uint64_t handle = allocate_slot(pid, CAPABILITY_SERVICE_SURFACE,
                                          rights, surface_id);
    if (handle == UINT64_MAX) (void)surface_release(surface_id);
    return handle;
}

uint64_t capability_grant(uint32_t source_pid, uint64_t source_handle,
                          uint32_t target_pid, uint32_t rights) {
    enum capability_service service;
    uint32_t object_id;
    if (target_pid == 0u || target_pid >= PROCESS_LIMIT || rights == 0u ||
        !capability_resolve_object(source_pid, source_handle, rights,
                                   &service, &object_id) ||
        service != CAPABILITY_SERVICE_SURFACE ||
        (rights & ~(CAPABILITY_RIGHT_READ | CAPABILITY_RIGHT_QUERY)) != 0u ||
        (assigned_services[target_pid] & CAPABILITY_SERVICE_BIT(service)) == 0u) {
        ++denied_count;
        return UINT64_MAX;
    }
    if (!surface_retain(object_id)) {
        ++denied_count;
        return UINT64_MAX;
    }
    const uint64_t granted = allocate_slot(target_pid, service, rights, object_id);
    if (granted == UINT64_MAX) (void)surface_release(object_id);
    return granted;
}

bool capability_resolve_object(uint32_t caller_pid, uint64_t handle,
                               uint32_t required_right,
                               enum capability_service *service,
                               uint32_t *object_id) {
    uint32_t owner;
    uint32_t slot;
    uint16_t generation;
    if (!decode_handle(handle, &owner, &slot, &generation) ||
        owner != caller_pid) {
        ++denied_count;
        return false;
    }
    const struct capability_slot *entry = &tables[owner][slot];
    if (!entry->open || entry->generation != generation ||
        (entry->rights & required_right) != required_right) {
        ++denied_count;
        return false;
    }
    if (service != NULL) *service = entry->service;
    if (object_id != NULL) *object_id = entry->object_id;
    return true;
}

bool capability_resolve(uint32_t caller_pid, uint64_t handle,
                        uint32_t required_right,
                        enum capability_service *service) {
    return capability_resolve_object(caller_pid, handle, required_right,
                                     service, NULL);
}

bool capability_close(uint32_t caller_pid, uint64_t handle) {
    enum capability_service ignored;
    if (!capability_resolve(caller_pid, handle, 0u, &ignored)) return false;
    const uint32_t slot = (uint32_t)(handle & 0xFFu);
    struct capability_slot *entry = &tables[caller_pid][slot];
    const enum capability_service service = entry->service;
    const uint32_t object_id = entry->object_id;
    entry->open = false;
    entry->rights = 0u;
    entry->object_id = 0u;
    ++entry->generation;
    if (entry->generation == 0u) entry->generation = 1u;
    ++closed_count;
    if (service == CAPABILITY_SERVICE_SURFACE && object_id != 0u)
        (void)surface_release(object_id);
    return true;
}

void capabilities_close_all(uint32_t pid) {
    if (pid == 0u || pid >= PROCESS_LIMIT) return;
    for (uint32_t slot = 0u; slot < SLOTS_PER_PROCESS; ++slot) {
        struct capability_slot *entry = &tables[pid][slot];
        if (!entry->open) continue;
        const enum capability_service service = entry->service;
        const uint32_t object_id = entry->object_id;
        entry->open = false;
        entry->rights = 0u;
        entry->object_id = 0u;
        ++entry->generation;
        if (entry->generation == 0u) entry->generation = 1u;
        ++closed_count;
        if (service == CAPABILITY_SERVICE_SURFACE && object_id != 0u)
            (void)surface_release(object_id);
    }
    assigned_services[pid] = 0u;
}

bool capabilities_self_test(void) {
    if (opened_count < 9u || opened_count != closed_count || denied_count < 2u)
        return false;
    for (uint32_t pid = 1u; pid < 3u; ++pid)
        for (uint32_t slot = 0u; slot < SLOTS_PER_PROCESS; ++slot)
            if (tables[pid][slot].open) return false;
    return true;
}

uint64_t capabilities_opened(void) { return opened_count; }
uint64_t capabilities_closed(void) { return closed_count; }
uint64_t capabilities_denied(void) { return denied_count; }

uint64_t capabilities_live(void) {
    uint64_t count = 0u;
    for (uint32_t pid = 1u; pid < PROCESS_LIMIT; ++pid)
        for (uint32_t slot = 0u; slot < SLOTS_PER_PROCESS; ++slot)
            if (tables[pid][slot].open) ++count;
    return count;
}
