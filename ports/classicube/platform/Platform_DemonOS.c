/*
 * ClassiCube native DemonOS platform backend -- implementation boundary.
 *
 * This file is kept separate from upstream and will be compiled alongside
 * ClassiCube after the D1 runtime milestone. The authoritative interfaces are
 * Platform.h, Window.h, Graphics.h, Audio.h, and _PlatformBase.h at the pinned
 * upstream revision. Do not add POSIX emulation here: map each operation to a
 * capability-backed DemonOS service or report ERR_NOT_SUPPORTED.
 */

#include "Platform.h"
#include "Errors.h"
#include "String_.h"

#include <demon/portkit.h>

/* Backend implementation order:
 * D1: Platform_Log, Mem_*, Stopwatch_Measure, DateTime_CurrentUTC,
 *     Process_Exit, Thread_Sleep (cooperative).
 * D2: File_*, Directory_*, Path_*, options/map persistence.
 * D3: Window_* and software framebuffer presentation through surfaces.
 * D4: keyboard, text, buttons, wheel, and relative mouse event translation.
 * D5: Socket_*, DNS, HTTP asset download and Classic Protocol multiplayer.
 * D6: Audio_* through the 44.1 kHz stereo PCM capability.
 */

const char *DemonOS_ClassiCubeBackendRevision =
    "ClassiCube 9a3101c00330 / DemonOS backend D0";

cc_bool Platform_ReadonlyFilesystem = false;
const cc_result ReturnCode_FileShareViolation = 1001;
const cc_result ReturnCode_FileNotFound       = 1002;
const cc_result ReturnCode_PathNotFound       = 1003;
const cc_result ReturnCode_DirectoryExists    = 1004;

#define DEMONOS_FILE_LIMIT 8
static struct demon_port_file files[DEMONOS_FILE_LIMIT];
static cc_bool file_used[DEMONOS_FILE_LIMIT];

static int file_slot_take(void) {
    for (int i = 0; i < DEMONOS_FILE_LIMIT; ++i) {
        if (file_used[i]) continue;
        file_used[i] = true;
        files[i].handle = DEMON_PORT_INVALID_HANDLE;
        return i;
    }
    return -1;
}

static struct demon_port_file *file_slot(cc_file file) {
    int index = file - 1;
    if (index < 0 || index >= DEMONOS_FILE_LIMIT || !file_used[index]) return NULL;
    return &files[index];
}

void Platform_Log(const char *message, int length) {
    char chunk[128];
    while (length > 0) {
        int take = length < (int)sizeof(chunk) - 1 ? length : (int)sizeof(chunk) - 1;
        for (int i = 0; i < take; ++i) chunk[i] = message[i];
        chunk[take] = '\0';
        demon_port_write(chunk);
        message += take;
        length -= take;
    }
}

void *Mem_TryAlloc(cc_uint32 count, cc_uint32 size) {
    if (size != 0u && count > (cc_uint32)-1 / size) return NULL;
    return demon_port_malloc((size_t)count * size);
}

void *Mem_TryAllocCleared(cc_uint32 count, cc_uint32 size) {
    return demon_port_calloc(count, size);
}

void *Mem_Alloc(cc_uint32 count, cc_uint32 size, const char *place) {
    void *memory = Mem_TryAlloc(count, size);
    if (memory != NULL) return memory;
    demon_port_fatal(place != NULL ? place : "ClassiCube allocation", 70u);
    return NULL;
}

void *Mem_AllocCleared(cc_uint32 count, cc_uint32 size, const char *place) {
    void *memory = Mem_TryAllocCleared(count, size);
    if (memory != NULL) return memory;
    demon_port_fatal(place != NULL ? place : "ClassiCube cleared allocation", 71u);
    return NULL;
}

void *Mem_TryRealloc(void *memory, cc_uint32 count, cc_uint32 size) {
    if (size != 0u && count > (cc_uint32)-1 / size) return NULL;
    return demon_port_realloc(memory, (size_t)count * size);
}

void Mem_Free(void *memory) { demon_port_free(memory); }

void *Mem_Set(void *destination, cc_uint8 value, unsigned count) {
    cc_uint8 *bytes = destination;
    for (unsigned i = 0u; i < count; ++i) bytes[i] = value;
    return destination;
}

void *Mem_Copy(void *destination, const void *source, unsigned count) {
    cc_uint8 *dst = destination;
    const cc_uint8 *src = source;
    for (unsigned i = 0u; i < count; ++i) dst[i] = src[i];
    return destination;
}

void *Mem_Move(void *destination, const void *source, unsigned count) {
    cc_uint8 *dst = destination;
    const cc_uint8 *src = source;
    if (dst < src) {
        for (unsigned i = 0u; i < count; ++i) dst[i] = src[i];
    } else if (dst > src) {
        while (count > 0u) { --count; dst[count] = src[count]; }
    }
    return destination;
}

int Mem_Equal(const void *left, const void *right, cc_uint32 count) {
    const cc_uint8 *a = left;
    const cc_uint8 *b = right;
    for (cc_uint32 i = 0u; i < count; ++i) if (a[i] != b[i]) return false;
    return true;
}

cc_uint64 Stopwatch_Measure(void) { return demon_port_ticks_ms(); }

cc_uint64 Stopwatch_ElapsedMicroseconds(cc_uint64 begin, cc_uint64 end) {
    return (end - begin) * 1000u;
}

int Stopwatch_ElapsedMS(cc_uint64 begin, cc_uint64 end) {
    return (int)(end - begin);
}

void Platform_EncodePath(cc_filepath *destination, const cc_string *source) {
    int length = source->length;
    if (length >= NATIVE_STR_LEN) length = NATIVE_STR_LEN - 1;
    for (int i = 0; i < length; ++i) destination->buffer[i] = source->buffer[i];
    destination->buffer[length] = '\0';
}

void Platform_DecodePath(cc_string *destination, const cc_filepath *path) {
    String_AppendConst(destination, path->buffer);
}

cc_result File_Create(cc_file *file, const cc_filepath *path) {
    int index = file_slot_take();
    if (index < 0) return ReturnCode_FileShareViolation;
    if (!demon_port_create(&files[index], path->buffer)) {
        file_used[index] = false;
        return ReturnCode_PathNotFound;
    }
    *file = index + 1;
    return 0;
}

cc_result File_Open(cc_file *file, const cc_filepath *path) {
    int index = file_slot_take();
    if (index < 0) return ReturnCode_FileShareViolation;
    if (!demon_port_open(&files[index], path->buffer)) {
        file_used[index] = false;
        return ReturnCode_FileNotFound;
    }
    *file = index + 1;
    return 0;
}

cc_result File_OpenOrCreate(cc_file *file, const cc_filepath *path) {
    cc_result result = File_Open(file, path);
    return result == 0 ? 0 : File_Create(file, path);
}

cc_result File_Read(cc_file file, void *data, cc_uint32 count, cc_uint32 *bytesRead) {
    struct demon_port_file *port_file = file_slot(file);
    if (port_file == NULL) return ERR_INVALID_ARGUMENT;
    *bytesRead = (cc_uint32)demon_port_read(port_file, data, count);
    return 0;
}

cc_result File_Write(cc_file file, const void *data, cc_uint32 count, cc_uint32 *bytesWrote) {
    struct demon_port_file *port_file = file_slot(file);
    if (port_file == NULL) return ERR_INVALID_ARGUMENT;
    *bytesWrote = (cc_uint32)demon_port_write_file(port_file, data, count);
    return *bytesWrote == count ? 0 : ERR_END_OF_STREAM;
}

cc_result File_Close(cc_file file) {
    int index = file - 1;
    struct demon_port_file *port_file = file_slot(file);
    if (port_file == NULL) return ERR_INVALID_ARGUMENT;
    demon_port_close(port_file);
    file_used[index] = false;
    return 0;
}

cc_result File_Seek(cc_file file, int offset, int seekType) {
    struct demon_port_file *port_file = file_slot(file);
    if (port_file == NULL) return ERR_INVALID_ARGUMENT;
    int64_t base = seekType == FILE_SEEKFROM_BEGIN ? 0 :
        seekType == FILE_SEEKFROM_CURRENT ? (int64_t)port_file->position :
        seekType == FILE_SEEKFROM_END ? (int64_t)port_file->size : -1;
    int64_t position = base + offset;
    if (base < 0 || position < 0 || (uint64_t)position > port_file->size)
        return ERR_INVALID_ARGUMENT;
    return demon_port_seek(port_file, (size_t)position) ? 0 : ERR_INVALID_ARGUMENT;
}

cc_result File_Position(cc_file file, cc_uint32 *position) {
    struct demon_port_file *port_file = file_slot(file);
    if (port_file == NULL) return ERR_INVALID_ARGUMENT;
    *position = (cc_uint32)demon_port_tell(port_file);
    return 0;
}

cc_result File_Length(cc_file file, cc_uint32 *length) {
    struct demon_port_file *port_file = file_slot(file);
    if (port_file == NULL || port_file->size > UINT32_MAX) return ERR_INVALID_ARGUMENT;
    *length = (cc_uint32)port_file->size;
    return 0;
}

void Thread_Sleep(cc_uint32 milliseconds) { demon_port_sleep_ms(milliseconds); }

void Process_Exit(cc_result code) { demon_port_exit((uint64_t)(cc_uint32)code); }

void Process_Abort2(cc_result result, const char *message) {
    demon_port_write("ClassiCube fatal: ");
    demon_port_write(message);
    demon_port_write("\n");
    demon_port_exit(result != 0 ? (uint64_t)(cc_uint32)result : 255u);
}
