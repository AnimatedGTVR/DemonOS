// K3 of docs/kernel-cxx-port.md: src/capability.c's per-process capability
// table migrated onto kernel::slot_table<T,N> (include/kernel/slot_table.h).
// Every function keeps its exact original extern "C" signature, so every
// existing caller (src/arch/x86_64/userspace.c's syscall dispatch,
// src/apps.c, src/init.c, ...) calls the same names unchanged. See
// slot_table.h's file comment for why this is a distinct generic type from
// K1's bounded_table rather than a forced reuse of it: capability handles
// are (pid, slot, generation)-addressed, not name-addressed, and need the
// generation counter bounded_table has no concept of.
#include <kernel/capability.h>
#include <kernel/slot_table.h>
#include <kernel/surface.h>

#include <stddef.h>

namespace {

constexpr uint32_t kProcessLimit = 8u;
constexpr uint32_t kSlotsPerProcess = 8u;

// The part of the old struct capability_slot that isn't open/generation
// bookkeeping (slot_table now owns that half).
struct capability_payload {
    enum capability_service service = static_cast<enum capability_service>(0);
    uint32_t rights = 0u;
    uint32_t object_id = 0u;
};

kernel::slot_table<capability_payload, kSlotsPerProcess> tables[kProcessLimit];
uint64_t opened_count = 0u;
uint64_t closed_count = 0u;
uint64_t denied_count = 0u;
uint32_t assigned_services[kProcessLimit];

uint64_t make_handle(uint32_t pid, uint32_t slot, uint16_t generation) {
    return (static_cast<uint64_t>(generation) << 16u) | (static_cast<uint64_t>(pid) << 8u) | slot;
}

bool decode_handle(uint64_t handle, uint32_t *pid, uint32_t *slot, uint16_t *generation) {
    if ((handle >> 32u) != 0u) return false;
    *slot = static_cast<uint32_t>(handle & 0xFFu);
    *pid = static_cast<uint32_t>((handle >> 8u) & 0xFFu);
    *generation = static_cast<uint16_t>(handle >> 16u);
    return *pid < kProcessLimit && *slot < kSlotsPerProcess && *generation != 0u;
}

uint32_t service_rights(uint32_t service) {
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
    if (service == CAPABILITY_SERVICE_AUDIO)
        return CAPABILITY_RIGHT_WRITE | CAPABILITY_RIGHT_QUERY;
    return 0u;
}

uint64_t allocate_slot(uint32_t pid, enum capability_service service,
                       uint32_t rights, uint32_t object_id) {
    uint32_t slot;
    uint16_t generation;
    capability_payload *payload;
    if (!tables[pid].allocate(&slot, &generation, &payload)) {
        ++denied_count;
        return UINT64_MAX;
    }
    payload->service = service;
    payload->rights = rights;
    payload->object_id = object_id;
    ++opened_count;
    return make_handle(pid, slot, generation);
}

}  // namespace

extern "C" void capabilities_init(void) {
    for (uint32_t pid = 0u; pid < kProcessLimit; ++pid) {
        tables[pid] = kernel::slot_table<capability_payload, kSlotsPerProcess>();
        assigned_services[pid] = 0u;
    }
    opened_count = 0u;
    closed_count = 0u;
    denied_count = 0u;
    assigned_services[1] = UINT32_MAX;
    assigned_services[2] = UINT32_MAX;
}

extern "C" bool capability_assign_services(uint32_t pid, uint32_t service_mask) {
    if (pid == 0u || pid >= kProcessLimit) return false;
    assigned_services[pid] = service_mask;
    return true;
}

extern "C" uint64_t capability_open(uint32_t pid, uint32_t service) {
    const uint32_t rights = service_rights(service);
    if (pid == 0u || pid >= kProcessLimit || rights == 0u ||
        (assigned_services[pid] & CAPABILITY_SERVICE_BIT(service)) == 0u) {
        ++denied_count;
        return UINT64_MAX;
    }
    return allocate_slot(pid, static_cast<enum capability_service>(service), rights, 0u);
}

extern "C" uint64_t capability_open_file(uint32_t pid, uint32_t object_id) {
    if (pid == 0u || pid >= kProcessLimit || object_id == 0u) {
        ++denied_count;
        return UINT64_MAX;
    }
    return allocate_slot(pid, CAPABILITY_SERVICE_FILE,
        CAPABILITY_RIGHT_READ | CAPABILITY_RIGHT_WRITE | CAPABILITY_RIGHT_QUERY,
        object_id);
}

extern "C" uint64_t capability_open_ipc(uint32_t pid, uint32_t channel_id, uint32_t rights) {
    const uint32_t allowed = CAPABILITY_RIGHT_SEND | CAPABILITY_RIGHT_RECEIVE | CAPABILITY_RIGHT_QUERY;
    if (pid == 0u || pid >= kProcessLimit || channel_id == 0u || rights == 0u ||
        (rights & ~allowed) != 0u ||
        (assigned_services[pid] & CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_IPC)) == 0u) {
        ++denied_count;
        return UINT64_MAX;
    }
    return allocate_slot(pid, CAPABILITY_SERVICE_IPC, rights, channel_id);
}

extern "C" uint64_t capability_open_surface(uint32_t pid, uint32_t surface_id, uint32_t rights) {
    const uint32_t allowed = CAPABILITY_RIGHT_READ | CAPABILITY_RIGHT_WRITE | CAPABILITY_RIGHT_QUERY;
    if (pid == 0u || pid >= kProcessLimit || surface_id == 0u || rights == 0u ||
        (rights & ~allowed) != 0u ||
        (assigned_services[pid] & CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_SURFACE)) == 0u) {
        ++denied_count;
        return UINT64_MAX;
    }
    if (!surface_retain(surface_id)) {
        ++denied_count;
        return UINT64_MAX;
    }
    const uint64_t handle = allocate_slot(pid, CAPABILITY_SERVICE_SURFACE, rights, surface_id);
    if (handle == UINT64_MAX) (void)surface_release(surface_id);
    return handle;
}

extern "C" uint64_t capability_grant(uint32_t source_pid, uint64_t source_handle,
                                     uint32_t target_pid, uint32_t rights) {
    enum capability_service service;
    uint32_t object_id;
    if (target_pid == 0u || target_pid >= kProcessLimit || rights == 0u ||
        !capability_resolve_object(source_pid, source_handle, rights, &service, &object_id) ||
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

extern "C" bool capability_resolve_object(uint32_t caller_pid, uint64_t handle,
                                          uint32_t required_right,
                                          enum capability_service *service,
                                          uint32_t *object_id) {
    uint32_t owner;
    uint32_t slot;
    uint16_t generation;
    if (!decode_handle(handle, &owner, &slot, &generation) || owner != caller_pid) {
        ++denied_count;
        return false;
    }
    capability_payload *grant = tables[owner].resolve(slot, generation);
    if (grant == nullptr || (grant->rights & required_right) != required_right) {
        ++denied_count;
        return false;
    }
    if (service != nullptr) *service = grant->service;
    if (object_id != nullptr) *object_id = grant->object_id;
    return true;
}

extern "C" bool capability_resolve(uint32_t caller_pid, uint64_t handle,
                                   uint32_t required_right,
                                   enum capability_service *service) {
    return capability_resolve_object(caller_pid, handle, required_right, service, nullptr);
}

extern "C" bool capability_close(uint32_t caller_pid, uint64_t handle) {
    enum capability_service ignored;
    if (!capability_resolve(caller_pid, handle, 0u, &ignored)) return false;
    const uint32_t slot = static_cast<uint32_t>(handle & 0xFFu);
    capability_payload released;
    if (!tables[caller_pid].release(slot, &released)) return false;
    ++closed_count;
    if (released.service == CAPABILITY_SERVICE_SURFACE && released.object_id != 0u)
        (void)surface_release(released.object_id);
    return true;
}

extern "C" void capabilities_close_all(uint32_t pid) {
    if (pid == 0u || pid >= kProcessLimit) return;
    for (uint32_t slot = 0u; slot < kSlotsPerProcess; ++slot) {
        if (!tables[pid].is_open(slot)) continue;
        capability_payload released;
        if (!tables[pid].release(slot, &released)) continue;
        ++closed_count;
        if (released.service == CAPABILITY_SERVICE_SURFACE && released.object_id != 0u)
            (void)surface_release(released.object_id);
    }
    assigned_services[pid] = 0u;
}

extern "C" bool capabilities_self_test(void) {
    if (opened_count < 9u || opened_count != closed_count || denied_count < 2u)
        return false;
    for (uint32_t pid = 1u; pid < 3u; ++pid)
        for (uint32_t slot = 0u; slot < kSlotsPerProcess; ++slot)
            if (tables[pid].is_open(slot)) return false;
    return true;
}

extern "C" uint64_t capabilities_opened(void) { return opened_count; }
extern "C" uint64_t capabilities_closed(void) { return closed_count; }
extern "C" uint64_t capabilities_denied(void) { return denied_count; }

extern "C" uint64_t capabilities_live(void) {
    uint64_t count = 0u;
    for (uint32_t pid = 1u; pid < kProcessLimit; ++pid)
        for (uint32_t slot = 0u; slot < kSlotsPerProcess; ++slot)
            if (tables[pid].is_open(slot)) ++count;
    return count;
}
