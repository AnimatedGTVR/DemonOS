#include <kernel/ipc.h>
#include <kernel/capability.h>
#include <kernel/scheduler.h>
#include <kernel/userspace.h>
#include <demon/input.h>

#define IPC_CHANNEL_LIMIT 16u
#define IPC_QUEUE_DEPTH 8u

struct ipc_message { uint32_t sender; uint8_t length; uint8_t data[IPC_MESSAGE_MAX]; };
struct ipc_waiter { uint32_t pid; uint64_t address; size_t capacity; bool active, multiplex; };
struct ipc_channel {
    uint32_t id, owner;
    char name[IPC_NAME_MAX];
    uint8_t name_length, read, write, count;
    struct ipc_message messages[IPC_QUEUE_DEPTH];
    struct ipc_waiter waiter;
    bool active;
};
static struct ipc_channel channels[IPC_CHANNEL_LIMIT];
static uint64_t sent_count, received_count, dropped_count;

static bool same_name(const struct ipc_channel *c, const char *name, size_t length) {
    if (c->name_length != length) return false;
    for (size_t i = 0u; i < length; ++i) if (c->name[i] != name[i]) return false;
    return true;
}
static struct ipc_channel *by_id(uint32_t id) {
    if (id == 0u || id > IPC_CHANNEL_LIMIT || !channels[id - 1u].active) return NULL;
    return &channels[id - 1u];
}
static struct ipc_channel *resolve(uint32_t pid, uint64_t handle, uint32_t right) {
    enum capability_service service; uint32_t id;
    if (!capability_resolve_object(pid, handle, right, &service, &id) || service != CAPABILITY_SERVICE_IPC)
        return NULL;
    return by_id(id);
}
void ipc_init(void) {
    for (size_t i = 0u; i < IPC_CHANNEL_LIMIT; ++i) channels[i] = (struct ipc_channel){.id=(uint32_t)i+1u};
    sent_count = received_count = dropped_count = 0u;
}
uint64_t ipc_create(uint32_t owner, const char *name, size_t length) {
    if (owner == 0u || name == NULL || length == 0u || length >= IPC_NAME_MAX) return UINT64_MAX;
    for (size_t i = 0u; i < IPC_CHANNEL_LIMIT; ++i)
        if (channels[i].active && same_name(&channels[i], name, length)) return UINT64_MAX;
    for (size_t i = 0u; i < IPC_CHANNEL_LIMIT; ++i) if (!channels[i].active) {
        struct ipc_channel *c = &channels[i]; *c = (struct ipc_channel){.id=(uint32_t)i+1u,.owner=owner,
            .name_length=(uint8_t)length,.active=true};
        for (size_t n=0u;n<length;++n) c->name[n]=name[n];
        uint64_t handle = capability_open_ipc(owner, c->id,
            CAPABILITY_RIGHT_SEND | CAPABILITY_RIGHT_RECEIVE | CAPABILITY_RIGHT_QUERY);
        if (handle == UINT64_MAX) c->active=false;
        return handle;
    }
    return UINT64_MAX;
}
uint64_t ipc_connect(uint32_t pid, const char *name, size_t length) {
    if (pid == 0u || name == NULL || length == 0u || length >= IPC_NAME_MAX) return UINT64_MAX;
    for (size_t i=0u;i<IPC_CHANNEL_LIMIT;++i) if (channels[i].active && same_name(&channels[i],name,length))
        return capability_open_ipc(pid, channels[i].id, CAPABILITY_RIGHT_SEND | CAPABILITY_RIGHT_QUERY);
    return UINT64_MAX;
}
bool ipc_send(uint32_t pid, uint64_t handle, const uint8_t *data, size_t length) {
    struct ipc_channel *c = resolve(pid, handle, CAPABILITY_RIGHT_SEND);
    if (c == NULL || data == NULL || length == 0u || length > IPC_MESSAGE_MAX) return false;
    if (c->waiter.active) {
        struct ipc_waiter waiter=c->waiter; c->waiter.active=false;
        if (waiter.multiplex) (void)input_cancel_wait(waiter.pid);
        const uint64_t result = waiter.multiplex ? 1u : length;
        const uint64_t detail = waiter.multiplex ? length : pid;
        if (length > waiter.capacity || !userspace_copy_to(waiter.pid, waiter.address, data, length) ||
            !scheduler_wake(waiter.pid, result, detail)) { ++dropped_count; return false; }
        ++sent_count; ++received_count; return true;
    }
    if (c->count == IPC_QUEUE_DEPTH) { ++dropped_count; return false; }
    struct ipc_message *m=&c->messages[c->write]; m->sender=pid; m->length=(uint8_t)length;
    for (size_t i=0u;i<length;++i) m->data[i]=data[i];
    c->write=(uint8_t)((c->write+1u)%IPC_QUEUE_DEPTH); ++c->count; ++sent_count; return true;
}
bool ipc_receive(uint32_t pid, uint64_t handle, uint8_t *data, size_t capacity,
                 size_t *length, uint32_t *sender) {
    struct ipc_channel *c=resolve(pid,handle,CAPABILITY_RIGHT_RECEIVE);
    if (c==NULL || data==NULL || c->count==0u) return false;
    struct ipc_message *m=&c->messages[c->read]; if (m->length>capacity) return false;
    for (size_t i=0u;i<m->length;++i) data[i]=m->data[i];
    if (length != NULL) *length = m->length;
    if (sender != NULL) *sender = m->sender;
    c->read=(uint8_t)((c->read+1u)%IPC_QUEUE_DEPTH); --c->count; ++received_count; return true;
}
bool ipc_wait(uint32_t pid, uint64_t handle, uint64_t address, size_t capacity) {
    struct ipc_channel *c=resolve(pid,handle,CAPABILITY_RIGHT_RECEIVE);
    if (c==NULL || c->waiter.active || capacity==0u || capacity>IPC_MESSAGE_MAX) return false;
    c->waiter=(struct ipc_waiter){.pid=pid,.address=address,.capacity=capacity,.active=true}; return true;
}
bool ipc_wait_select(uint32_t pid, uint64_t handle, uint64_t address, size_t capacity) {
    struct ipc_channel *channel=resolve(pid,handle,CAPABILITY_RIGHT_RECEIVE);
    if (channel==NULL || channel->waiter.active || capacity==0u || capacity>IPC_MESSAGE_MAX) return false;
    channel->waiter=(struct ipc_waiter){.pid=pid,.address=address,.capacity=capacity,
        .active=true,.multiplex=true};
    return true;
}
bool ipc_cancel_wait(uint32_t pid) {
    for (size_t index=0u;index<IPC_CHANNEL_LIMIT;++index) {
        if (channels[index].active && channels[index].waiter.active &&
            channels[index].waiter.pid==pid) {
            channels[index].waiter.active=false;
            return true;
        }
    }
    return false;
}
bool ipc_channel_owner(uint32_t pid, uint64_t handle, uint32_t *owner_pid) {
    struct ipc_channel *channel = resolve(pid, handle, CAPABILITY_RIGHT_QUERY);
    if (channel == NULL || owner_pid == NULL || channel->owner == 0u) return false;
    *owner_pid = channel->owner;
    return true;
}
void ipc_process_cleanup(uint32_t pid) {
    /* A select waiter is registered in both subsystems. Always remove the
       input half when its process exits, including crash/terminate paths. */
    (void)input_cancel_wait(pid);
    for (size_t i=0u;i<IPC_CHANNEL_LIMIT;++i) {
        struct ipc_channel *c=&channels[i]; if (!c->active) continue;
        if (c->waiter.active && c->waiter.pid==pid) c->waiter.active=false;
        if (c->owner==pid) { if (c->waiter.active) {
                if (c->waiter.multiplex) (void)input_cancel_wait(c->waiter.pid);
                (void)scheduler_wake(c->waiter.pid,UINT64_MAX,0u);
            }
            c->active=false; c->count=0u; c->waiter.active=false; }
        /* Queued payloads are channel-owned copies. They remain valid after a
           short-lived sender exits, matching datagram/message-queue semantics. */
    }
}
size_t ipc_channel_count(void) { size_t n=0u; for(size_t i=0u;i<IPC_CHANNEL_LIMIT;++i) if(channels[i].active)++n; return n; }
uint64_t ipc_messages_sent(void){return sent_count;} uint64_t ipc_messages_received(void){return received_count;}
uint64_t ipc_messages_dropped(void){return dropped_count;}
bool ipc_self_test(void) {
    static const char name[]="test.service"; static const uint8_t payload[]={0x44,0x4D,0x4E}; uint8_t out[3];
    uint32_t sender=0u; size_t length=0u;
    const size_t baseline_channels = ipc_channel_count();
    if (!capability_assign_services(1u,CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_IPC)) ||
        !capability_assign_services(2u,CAPABILITY_SERVICE_BIT(CAPABILITY_SERVICE_IPC))) return false;
    uint64_t server=ipc_create(1u,name,sizeof(name)-1u), client=ipc_connect(2u,name,sizeof(name)-1u);
    bool ok=server!=UINT64_MAX && client!=UINT64_MAX && ipc_send(2u,client,payload,sizeof(payload)) &&
        ipc_receive(1u,server,out,sizeof(out),&length,&sender) && length==3u && sender==2u &&
        out[0]==payload[0] && out[2]==payload[2];
    ipc_process_cleanup(1u); capabilities_close_all(1u); capabilities_close_all(2u);
    return ok && ipc_channel_count()==baseline_channels;
}
