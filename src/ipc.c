#include <kernel/ipc.h>
#include <kernel/capability.h>
#include <kernel/scheduler.h>
#include <kernel/userspace.h>
#include <demon/input.h>

#define IPC_CHANNEL_LIMIT 16u
/* 8 was too tight for a real client doing several one-way Xlib calls back
   to back before the receiving process (e.g. demonx_server) gets scheduled
   to drain any of them -- a window-manager panel's own setup burst
   (CreateWindow, SelectInput, CreateGC, several Fill/DrawString calls,
   Raise, Map) alone is a dozen-plus messages with no reply/yield between
   them. 32 gives real headroom for that without meaningfully growing the
   fixed channel table (16 channels * 32 slots * ~72 bytes/message is still
   under 40 KiB). Sustained overruns are no longer dropped silently:
   ipc_send_block parks the overflow message and sleeps the sender until the
   owner drains a slot, giving every message to the receiver (bounded-pipe
   backpressure, see ipc_send_impl/ipc_receive). */
#define IPC_QUEUE_DEPTH 32u

struct ipc_message { uint32_t sender; uint8_t length; uint8_t data[IPC_MESSAGE_MAX]; };
struct ipc_waiter { uint32_t pid; uint64_t address; size_t capacity; bool active, multiplex; };
/* One staged message per channel for bounded-pipe send semantics: when a
   client fills the queue faster than the owner drains it (e.g. an Xlib
   client bursting requests at DemonX), ipc_send_block parks the message
   here and sleeps the sender; ipc_receive hands it into the freed slot and
   wakes the sender. Only one sender can be parked per channel -- a second
   concurrent sender still drops, which matches the pre-existing datagram
   behaviour for that (rare, tiny) case. */
struct ipc_send_waiter { uint32_t pid; uint8_t data[IPC_MESSAGE_MAX]; uint8_t length; bool active; };
struct ipc_channel {
    uint32_t id, owner;
    char name[IPC_NAME_MAX];
    uint8_t name_length, read, write, count;
    struct ipc_message messages[IPC_QUEUE_DEPTH];
    struct ipc_waiter waiter;
    struct ipc_send_waiter sender;
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
/* Returns 1 = delivered, 0 = failed/dropped, 2 = parked (sender blocked). */
static int ipc_send_impl(uint32_t pid, uint64_t handle, const uint8_t *data,
                         size_t length, bool block) {
    struct ipc_channel *c = resolve(pid, handle, CAPABILITY_RIGHT_SEND);
    if (c == NULL || data == NULL || length == 0u || length > IPC_MESSAGE_MAX) return 0;
    if (c->waiter.active) {
        struct ipc_waiter waiter=c->waiter; c->waiter.active=false;
        if (waiter.multiplex) (void)input_cancel_wait(waiter.pid);
        const uint64_t result = waiter.multiplex ? 1u : length;
        const uint64_t detail = waiter.multiplex ? length : pid;
        if (length > waiter.capacity || !userspace_copy_to(waiter.pid, waiter.address, data, length) ||
            !scheduler_wake(waiter.pid, result, detail)) { ++dropped_count; return 0; }
        ++sent_count; ++received_count; return 1;
    }
    if (c->count == IPC_QUEUE_DEPTH) {
        if (!block || c->sender.active) { ++dropped_count; return 0; }
        struct ipc_send_waiter *s = &c->sender;
        s->pid = pid; s->length = (uint8_t)length; s->active = true;
        for (size_t i = 0u; i < length; ++i) s->data[i] = data[i];
        return 2;
    }
    struct ipc_message *m=&c->messages[c->write]; m->sender=pid; m->length=(uint8_t)length;
    for (size_t i=0u;i<length;++i) m->data[i]=data[i];
    c->write=(uint8_t)((c->write+1u)%IPC_QUEUE_DEPTH); ++c->count; ++sent_count; return 1;
}

bool ipc_send(uint32_t pid, uint64_t handle, const uint8_t *data, size_t length) {
    return ipc_send_impl(pid, handle, data, length, false) == 1;
}

/* Try to deliver; on a full queue park the message and return 2 (the caller
   then blocks the process and the message is handed over when the owner
   drains a slot). Returns 1 on delivery, 0 on failure. Dropping is counted
   only when delivery is genuinely impossible (parking slot already taken by
   a second concurrent sender), so a blocked send never shows up as a loss. */
int ipc_send_block(uint32_t pid, uint64_t handle, const uint8_t *data,
                   size_t length) {
    return ipc_send_impl(pid, handle, data, length, true);
}
bool ipc_receive(uint32_t pid, uint64_t handle, uint8_t *data, size_t capacity,
                 size_t *length, uint32_t *sender) {
    struct ipc_channel *c=resolve(pid,handle,CAPABILITY_RIGHT_RECEIVE);
    if (c==NULL || data==NULL || c->count==0u) return false;
    struct ipc_message *m=&c->messages[c->read]; if (m->length>capacity) return false;
    for (size_t i=0u;i<m->length;++i) data[i]=m->data[i];
    if (length != NULL) *length = m->length;
    if (sender != NULL) *sender = m->sender;
    c->read=(uint8_t)((c->read+1u)%IPC_QUEUE_DEPTH); --c->count; ++received_count;
    /* A slot just freed: hand a parked sender's staged message into it and
       wake that sender. The handoff is invisible to both sides -- the
       sender's syscall returns success with its message delivered, and the
       queue stays full from the receiver's perspective, so bursts are
       serialized through the bounded pipe instead of being dropped. */
    if (c->sender.active) {
        struct ipc_send_waiter parked = c->sender;
        c->sender.active = false;
        struct ipc_message *out=&c->messages[c->write];
        out->sender = parked.pid; out->length = parked.length;
        for (size_t i=0u;i<parked.length;++i) out->data[i]=parked.data[i];
        c->write=(uint8_t)((c->write+1u)%IPC_QUEUE_DEPTH); ++c->count;
        (void)scheduler_wake(parked.pid, parked.length, 0u);
    }
    return true;
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
        if (c->sender.active && c->sender.pid==pid) {
            /* A sender parked on a full queue exited; drop its staged
               message so it can never be handed to the receiver. */
            c->sender.active=false;
        }
        if (c->owner==pid) { if (c->waiter.active) {
                if (c->waiter.multiplex) (void)input_cancel_wait(c->waiter.pid);
                (void)scheduler_wake(c->waiter.pid,UINT64_MAX,0u);
            }
            if (c->sender.active) {
                /* The channel the sender was blocked on is gone; wake it
                   with failure so it does not sleep forever. */
                const uint32_t parked = c->sender.pid;
                c->sender.active = false;
                (void)scheduler_wake(parked, UINT64_MAX, 0u);
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
