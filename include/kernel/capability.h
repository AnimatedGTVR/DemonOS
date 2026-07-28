#ifndef KERNEL_CAPABILITY_H
#define KERNEL_CAPABILITY_H

#include <stdbool.h>
#include <stdint.h>

enum capability_service {
    CAPABILITY_SERVICE_CONSOLE = 1,
    CAPABILITY_SERVICE_CLOCK = 2,
    CAPABILITY_SERVICE_PROCESS = 3,
    CAPABILITY_SERVICE_STORAGE = 4,
    CAPABILITY_SERVICE_FILE = 5,
    CAPABILITY_SERVICE_IPC = 6,
    CAPABILITY_SERVICE_DISPLAY = 7,
    CAPABILITY_SERVICE_INPUT = 8,
    CAPABILITY_SERVICE_SURFACE = 9,
    CAPABILITY_SERVICE_NETWORK = 10,
};

enum capability_right {
    CAPABILITY_RIGHT_WRITE = 1u << 0,
    CAPABILITY_RIGHT_QUERY = 1u << 1,
    CAPABILITY_RIGHT_READ = 1u << 2,
    CAPABILITY_RIGHT_OPEN = 1u << 3,
    CAPABILITY_RIGHT_SEND = 1u << 4,
    CAPABILITY_RIGHT_RECEIVE = 1u << 5,
};

void capabilities_init(void);
bool capability_assign_services(uint32_t pid, uint32_t service_mask);
uint64_t capability_open(uint32_t pid, uint32_t service);
uint64_t capability_open_file(uint32_t pid, uint32_t object_id);
uint64_t capability_open_ipc(uint32_t pid, uint32_t channel_id, uint32_t rights);
uint64_t capability_open_surface(uint32_t pid, uint32_t surface_id,
                                 uint32_t rights);
uint64_t capability_grant(uint32_t source_pid, uint64_t source_handle,
                          uint32_t target_pid, uint32_t rights);
bool capability_resolve(uint32_t pid, uint64_t handle, uint32_t required_right,
                        enum capability_service *service);
bool capability_resolve_object(uint32_t pid, uint64_t handle,
                               uint32_t required_right,
                               enum capability_service *service,
                               uint32_t *object_id);
bool capability_close(uint32_t pid, uint64_t handle);
void capabilities_close_all(uint32_t pid);
bool capabilities_self_test(void);
uint64_t capabilities_opened(void);
uint64_t capabilities_closed(void);
uint64_t capabilities_denied(void);
uint64_t capabilities_live(void);

#define CAPABILITY_SERVICE_BIT(service) (1u << (service))

#endif
