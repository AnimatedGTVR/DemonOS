#include <demon/c_app.h>
#include <demon/portkit.h>

struct port_block {
    size_t size;
    struct port_block *previous;
    struct port_block *next;
    bool free;
};

static struct demon_port_runtime runtime;
static struct port_block *heap_first;
static void *mapped_arena;

static size_t text_length(const char *text) {
    size_t length = 0u;
    if (text != NULL) while (text[length] != '\0') ++length;
    return length;
}

static void bytes_copy(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0u; i < length; ++i) out[i] = in[i];
}

static void bytes_zero(void *destination, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    for (size_t i = 0u; i < length; ++i) out[i] = 0u;
}

static size_t aligned_size(size_t size) {
    if (size > (size_t)-1 - 15u) return 0u;
    return (size + 15u) & ~(size_t)15u;
}

bool demon_port_init(void *arena, size_t arena_size) {
    if (arena == NULL || ((uintptr_t)arena & 15u) != 0u ||
        arena_size <= sizeof(struct port_block) + 16u)
        return false;
    runtime.storage = demon_service_open(4u);
    runtime.input = demon_service_open(8u);
    runtime.heap_used = 0u;
    runtime.heap_peak = 0u;
    runtime.heap_capacity = arena_size - sizeof(struct port_block);
    heap_first = (struct port_block *)arena;
    heap_first->size = runtime.heap_capacity;
    heap_first->previous = NULL;
    heap_first->next = NULL;
    heap_first->free = true;
    mapped_arena = NULL;
    return runtime.storage != DEMON_PORT_INVALID_HANDLE;
}

bool demon_port_init_dynamic(size_t arena_size) {
    void *arena = demon_memory_map(arena_size);
    if (arena == NULL) return false;
    if (!demon_port_init(arena, arena_size)) {
        (void)demon_memory_unmap(arena);
        return false;
    }
    mapped_arena = arena;
    return true;
}

const struct demon_port_runtime *demon_port_status(void) { return &runtime; }

void demon_port_shutdown(void) {
    if (runtime.input != DEMON_PORT_INVALID_HANDLE)
        (void)demon_handle_close(runtime.input);
    if (runtime.storage != DEMON_PORT_INVALID_HANDLE)
        (void)demon_handle_close(runtime.storage);
    runtime.input = DEMON_PORT_INVALID_HANDLE;
    runtime.storage = DEMON_PORT_INVALID_HANDLE;
    if (mapped_arena != NULL) {
        (void)demon_memory_unmap(mapped_arena);
        mapped_arena = NULL;
        heap_first = NULL;
    }
}

void *demon_port_malloc(size_t size) {
    const size_t needed = aligned_size(size);
    if (needed == 0u || heap_first == NULL) return NULL;
    for (struct port_block *block = heap_first; block != NULL; block = block->next) {
        if (!block->free || block->size < needed) continue;
        if (block->size >= needed + sizeof(struct port_block) + 16u) {
            struct port_block *rest = (struct port_block *)
                ((uint8_t *)(block + 1) + needed);
            rest->size = block->size - needed - sizeof(struct port_block);
            rest->previous = block;
            rest->next = block->next;
            rest->free = true;
            if (rest->next != NULL) rest->next->previous = rest;
            block->next = rest;
            block->size = needed;
        }
        block->free = false;
        runtime.heap_used += block->size;
        if (runtime.heap_used > runtime.heap_peak)
            runtime.heap_peak = runtime.heap_used;
        return block + 1;
    }
    return NULL;
}

void demon_port_free(void *pointer) {
    if (pointer == NULL) return;
    struct port_block *block = (struct port_block *)pointer - 1;
    if (block->free) return;
    block->free = true;
    if (runtime.heap_used >= block->size) runtime.heap_used -= block->size;
    if (block->next != NULL && block->next->free) {
        block->size += sizeof(struct port_block) + block->next->size;
        block->next = block->next->next;
        if (block->next != NULL) block->next->previous = block;
    }
    if (block->previous != NULL && block->previous->free) {
        block->previous->size += sizeof(struct port_block) + block->size;
        block->previous->next = block->next;
        if (block->next != NULL) block->next->previous = block->previous;
    }
}

void *demon_port_calloc(size_t count, size_t size) {
    if (count != 0u && size > (size_t)-1 / count) return NULL;
    const size_t total = count * size;
    void *result = demon_port_malloc(total);
    if (result != NULL) bytes_zero(result, total);
    return result;
}

void *demon_port_realloc(void *pointer, size_t size) {
    if (pointer == NULL) return demon_port_malloc(size);
    if (size == 0u) { demon_port_free(pointer); return NULL; }
    struct port_block *block = (struct port_block *)pointer - 1;
    if (block->size >= size) return pointer;
    void *replacement = demon_port_malloc(size);
    if (replacement == NULL) return NULL;
    bytes_copy(replacement, pointer, block->size);
    demon_port_free(pointer);
    return replacement;
}

bool demon_port_open(struct demon_port_file *file, const char *path) {
    if (file == NULL || path == NULL || runtime.storage == DEMON_PORT_INVALID_HANDLE)
        return false;
    file->size = 0u; file->position = 0u;
    file->handle = demon_file_open(runtime.storage, path, text_length(path), 0u);
    if (file->handle == DEMON_PORT_INVALID_HANDLE) return false;
    const uint64_t length = demon_handle_query(file->handle, 0u);
    if (length == DEMON_PORT_INVALID_HANDLE || length > (uint64_t)(size_t)-1) {
        demon_port_close(file); return false;
    }
    file->size = (size_t)length;
    return true;
}

bool demon_port_create(struct demon_port_file *file, const char *path) {
    if (file == NULL || path == NULL || runtime.storage == DEMON_PORT_INVALID_HANDLE)
        return false;
    file->size = 0u; file->position = 0u;
    file->handle = demon_file_open(runtime.storage, path, text_length(path), 1u);
    return file->handle != DEMON_PORT_INVALID_HANDLE;
}

size_t demon_port_write_file(struct demon_port_file *file, const void *source,
                             size_t bytes) {
    if (file == NULL || source == NULL || file->handle == DEMON_PORT_INVALID_HANDLE)
        return 0u;
    const uint64_t result = demon_handle_write(file->handle, source, bytes);
    if (result == DEMON_PORT_INVALID_HANDLE || result > bytes) return 0u;
    file->size = (size_t)result; file->position = (size_t)result;
    return (size_t)result;
}

bool demon_port_delete(const char *path) {
    return path != NULL && runtime.storage != DEMON_PORT_INVALID_HANDLE &&
        demon_file_delete(runtime.storage, path, text_length(path)) == 0u;
}

bool demon_port_rename(const char *old_path, const char *new_path) {
    return old_path != NULL && new_path != NULL &&
        runtime.storage != DEMON_PORT_INVALID_HANDLE &&
        demon_file_rename(runtime.storage, old_path, text_length(old_path),
            new_path, text_length(new_path)) == 0u;
}

size_t demon_port_read(struct demon_port_file *file, void *destination, size_t bytes) {
    if (file == NULL || destination == NULL || file->position > file->size) return 0u;
    const size_t remaining = file->size - file->position;
    if (bytes > remaining) bytes = remaining;
    if (bytes == 0u) return 0u;
    const uint64_t result = demon_handle_read_at(file->handle, destination,
                                                 bytes, file->position);
    if (result == DEMON_PORT_INVALID_HANDLE || result > bytes) return 0u;
    file->position += (size_t)result;
    return (size_t)result;
}

bool demon_port_seek(struct demon_port_file *file, size_t absolute_offset) {
    if (file == NULL || absolute_offset > file->size) return false;
    file->position = absolute_offset;
    return true;
}

size_t demon_port_tell(const struct demon_port_file *file) {
    return file == NULL ? 0u : file->position;
}

void demon_port_close(struct demon_port_file *file) {
    if (file == NULL) return;
    if (file->handle != DEMON_PORT_INVALID_HANDLE)
        (void)demon_handle_close(file->handle);
    file->size = 0u; file->position = 0u;
    file->handle = DEMON_PORT_INVALID_HANDLE;
}

uint64_t demon_port_ticks_ms(void) { return demon_ticks() * 10u; }

void demon_port_sleep_ms(uint32_t milliseconds) {
    const uint64_t deadline = demon_port_ticks_ms() + milliseconds;
    while (demon_port_ticks_ms() < deadline) (void)demon_yield();
}

bool demon_port_poll_input(struct input_event *event) {
    return event != NULL && runtime.input != DEMON_PORT_INVALID_HANDLE &&
        demon_input_poll(runtime.input, event) == 0u;
}

void demon_port_write(const char *text) {
    if (text != NULL) (void)demon_write(text, text_length(text));
}

void demon_port_exit(uint64_t status) {
    demon_port_shutdown();
    (void)demon_exit(status);
    for (;;) (void)demon_yield();
}

void demon_port_fatal(const char *message, uint64_t status) {
    demon_port_write("port: fatal: "); demon_port_write(message);
    demon_port_write("\n");
    demon_port_exit(status);
}
