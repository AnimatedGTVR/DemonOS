#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IPC_NAME_MAX 24u
#define IPC_MESSAGE_MAX 64u

void ipc_init(void);
uint64_t ipc_create(uint32_t owner_pid, const char *name, size_t name_length);
uint64_t ipc_connect(uint32_t pid, const char *name, size_t name_length);
bool ipc_send(uint32_t pid, uint64_t handle, const uint8_t *data, size_t length);
/* 1 = delivered, 2 = parked on a full queue (caller must block the process),
   0 = failed/dropped. */
int ipc_send_block(uint32_t pid, uint64_t handle, const uint8_t *data,
                   size_t length);
bool ipc_receive(uint32_t pid, uint64_t handle, uint8_t *data, size_t capacity,
                 size_t *length, uint32_t *sender_pid);
bool ipc_wait(uint32_t pid, uint64_t handle, uint64_t user_address, size_t capacity);
bool ipc_wait_select(uint32_t pid, uint64_t handle, uint64_t user_address,
                     size_t capacity);
bool ipc_cancel_wait(uint32_t pid);
bool ipc_channel_owner(uint32_t pid, uint64_t handle, uint32_t *owner_pid);
void ipc_process_cleanup(uint32_t pid);
bool ipc_self_test(void);
uint64_t ipc_messages_sent(void);
uint64_t ipc_messages_received(void);
uint64_t ipc_messages_dropped(void);
size_t ipc_channel_count(void);

#endif
