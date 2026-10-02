#include "internal.h"
#include "windows_kernel.h"
#include "qa/text.h"
#include <unicode/ustring.h>
#include <stdio.h>
#include <math.h>
#include <fenv.h>

enum kernel_operation {
    K_ENCODE = 1, K_HEAP, K_LAST_ERROR, K_SET_ERROR, K_THREAD, K_PROCESS, K_CURRENT_PROCESS,
    K_VERSION, K_COMMAND_A, K_COMMAND_W, K_ACP, K_OEM, K_VALID_CP, K_CP_INFO,
    K_ENV_A, K_ENV_W, K_FREE_ENV, K_MODULE_A, K_MODULE_W, K_LOAD_A, K_LOAD_W,
    K_LOAD_EX_A, K_LOAD_EX_W, K_FREE_LIBRARY, K_PROC_ADDRESS, K_DISABLE_THREAD,
    K_FILENAME_A, K_FILENAME_W, K_HEAP_CREATE, K_HEAP_ALLOC, K_HEAP_FREE,
    K_HEAP_REALLOC, K_HEAP_DESTROY, K_VIRTUAL_ALLOC, K_VIRTUAL_FREE,
    K_TLS_ALLOC, K_TLS_FREE, K_TLS_GET, K_TLS_SET, K_FLS_ALLOC, K_FLS_GET, K_FLS_SET,
    K_FLS_FREE, K_CRITICAL_INIT, K_CRITICAL_SPIN_INIT, K_CRITICAL_EX_INIT,
    K_CRITICAL_SPIN, K_CRITICAL_DELETE, K_CRITICAL_ENTER, K_CRITICAL_LEAVE,
    K_INCREMENT, K_DECREMENT, K_SRW_ACQUIRE, K_SRW_RELEASE, K_SLIST_INIT,
    K_SLIST_FLUSH, K_WAKE, K_DEBUGGER, K_FEATURE, K_FILE_TIME, K_COUNTER, K_FREQUENCY,
    K_SYSTEM_TIME, K_LOCAL_TIME, K_TIMEZONE, K_EXCEPTION, K_UNWIND,
    K_STD_GET, K_STD_SET, K_HANDLE_COUNT, K_FILE_TYPE, K_STARTUP, K_FILE_CREATE,
    K_FILE_READ, K_FILE_WRITE, K_FILE_CLOSE, K_FILE_FLUSH, K_FILE_SEEK, K_FILE_END,
    K_MULTI_WIDE, K_WIDE_MULTI, K_STRING_TYPE_A, K_STRING_TYPE_W, K_MAP_A, K_MAP_W,
    K_LOCALE_INFO_A, K_LOCALE_INFO_W
};
typedef struct kernel_descriptor {
    const char *name; uint32_t operation; qa_native_value_type result;
    size_t count; qa_native_value_type parameters[8];
} kernel_descriptor;
#define P QA_NATIVE_ADDRESS
#define I QA_NATIVE_I32
#define U QA_NATIVE_U32
#define Z QA_NATIVE_BYTES
#define V QA_NATIVE_VOID
#define D(n,o,r,c,...) {n,o,r,c,{__VA_ARGS__}}
static const kernel_descriptor descriptors[] = {
    D("EncodePointer",K_ENCODE,P,1,P), D("DecodePointer",K_ENCODE,P,1,P),
    D("GetProcessHeap",K_HEAP,P,0,0), D("GetLastError",K_LAST_ERROR,U,0,0), D("SetLastError",K_SET_ERROR,V,1,U),
    D("GetCurrentThreadId",K_THREAD,U,0,0), D("GetCurrentProcessId",K_PROCESS,U,0,0), D("GetCurrentProcess",K_CURRENT_PROCESS,P,0,0),
    D("GetVersion",K_VERSION,U,0,0), D("GetCommandLineA",K_COMMAND_A,P,0,0), D("GetCommandLineW",K_COMMAND_W,P,0,0),
    D("GetACP",K_ACP,U,0,0), D("GetOEMCP",K_OEM,U,0,0), D("IsValidCodePage",K_VALID_CP,I,1,U), D("GetCPInfo",K_CP_INFO,I,2,U,P),
    D("GetEnvironmentStrings",K_ENV_A,P,0,0), D("GetEnvironmentStringsA",K_ENV_A,P,0,0), D("GetEnvironmentStringsW",K_ENV_W,P,0,0),
    D("FreeEnvironmentStringsA",K_FREE_ENV,I,1,P), D("FreeEnvironmentStringsW",K_FREE_ENV,I,1,P),
    D("GetModuleHandleA",K_MODULE_A,P,1,P), D("GetModuleHandleW",K_MODULE_W,P,1,P),
    D("LoadLibraryA",K_LOAD_A,P,1,P), D("LoadLibraryW",K_LOAD_W,P,1,P),
    D("LoadLibraryExA",K_LOAD_EX_A,P,3,P,P,U), D("LoadLibraryExW",K_LOAD_EX_W,P,3,P,P,U),
    D("FreeLibrary",K_FREE_LIBRARY,I,1,P), D("GetProcAddress",K_PROC_ADDRESS,P,2,P,P), D("DisableThreadLibraryCalls",K_DISABLE_THREAD,I,1,P),
    D("GetModuleFileNameA",K_FILENAME_A,U,3,P,P,U), D("GetModuleFileNameW",K_FILENAME_W,U,3,P,P,U),
    D("HeapCreate",K_HEAP_CREATE,P,3,U,Z,Z), D("HeapAlloc",K_HEAP_ALLOC,P,3,P,U,Z), D("HeapFree",K_HEAP_FREE,I,3,P,U,P),
    D("HeapReAlloc",K_HEAP_REALLOC,P,4,P,U,P,Z), D("HeapDestroy",K_HEAP_DESTROY,I,1,P),
    D("VirtualAlloc",K_VIRTUAL_ALLOC,P,4,P,Z,U,U), D("VirtualFree",K_VIRTUAL_FREE,I,3,P,Z,U),
    D("TlsAlloc",K_TLS_ALLOC,U,0,0), D("TlsFree",K_TLS_FREE,I,1,U), D("TlsGetValue",K_TLS_GET,P,1,U), D("TlsSetValue",K_TLS_SET,I,2,U,P),
    D("FlsAlloc",K_FLS_ALLOC,U,1,P), D("FlsGetValue",K_FLS_GET,P,1,U), D("FlsSetValue",K_FLS_SET,I,2,U,P), D("FlsFree",K_FLS_FREE,I,1,U),
    D("InitializeCriticalSection",K_CRITICAL_INIT,V,1,P), D("InitializeCriticalSectionAndSpinCount",K_CRITICAL_SPIN_INIT,I,2,P,U),
    D("InitializeCriticalSectionEx",K_CRITICAL_EX_INIT,I,3,P,U,U), D("SetCriticalSectionSpinCount",K_CRITICAL_SPIN,U,2,P,U),
    D("DeleteCriticalSection",K_CRITICAL_DELETE,V,1,P), D("EnterCriticalSection",K_CRITICAL_ENTER,V,1,P), D("LeaveCriticalSection",K_CRITICAL_LEAVE,V,1,P),
    D("InterlockedIncrement",K_INCREMENT,I,1,P), D("InterlockedDecrement",K_DECREMENT,I,1,P),
    D("AcquireSRWLockExclusive",K_SRW_ACQUIRE,V,1,P), D("ReleaseSRWLockExclusive",K_SRW_RELEASE,V,1,P),
    D("InitializeSListHead",K_SLIST_INIT,V,1,P), D("InterlockedFlushSList",K_SLIST_FLUSH,P,1,P),
    D("WakeAllConditionVariable",K_WAKE,V,1,P), D("IsDebuggerPresent",K_DEBUGGER,I,0,0), D("IsProcessorFeaturePresent",K_FEATURE,I,1,U),
    D("GetSystemTimeAsFileTime",K_FILE_TIME,V,1,P), D("QueryPerformanceCounter",K_COUNTER,I,1,P), D("QueryPerformanceFrequency",K_FREQUENCY,I,1,P),
    D("GetSystemTime",K_SYSTEM_TIME,V,1,P), D("GetLocalTime",K_LOCAL_TIME,V,1,P), D("GetTimeZoneInformation",K_TIMEZONE,U,1,P),
    D("SetUnhandledExceptionFilter",K_EXCEPTION,P,1,P), D("RtlLookupFunctionEntry",K_UNWIND,P,3,QA_NATIVE_U64,P,P),
    D("GetStdHandle",K_STD_GET,P,1,I), D("SetStdHandle",K_STD_SET,I,2,I,P), D("SetHandleCount",K_HANDLE_COUNT,U,1,U),
    D("GetFileType",K_FILE_TYPE,U,1,P), D("GetStartupInfoA",K_STARTUP,V,1,P), D("GetStartupInfoW",K_STARTUP,V,1,P),
    D("CreateFileA",K_FILE_CREATE,P,7,P,U,U,P,U,U,P), D("ReadFile",K_FILE_READ,I,5,P,P,U,P,P),
    D("WriteFile",K_FILE_WRITE,I,5,P,P,U,P,P), D("CloseHandle",K_FILE_CLOSE,I,1,P), D("FlushFileBuffers",K_FILE_FLUSH,I,1,P),
    D("SetFilePointer",K_FILE_SEEK,U,4,P,I,P,U), D("SetEndOfFile",K_FILE_END,I,1,P),
    D("MultiByteToWideChar",K_MULTI_WIDE,I,6,U,U,P,I,P,I), D("WideCharToMultiByte",K_WIDE_MULTI,I,8,U,U,P,I,P,I,P,P),
    D("GetStringTypeA",K_STRING_TYPE_A,I,5,U,U,P,I,P), D("GetStringTypeW",K_STRING_TYPE_W,I,4,U,P,I,P),
    D("LCMapStringA",K_MAP_A,I,6,U,U,P,I,P,I), D("LCMapStringW",K_MAP_W,I,6,U,U,P,I,P,I),
    D("GetLocaleInfoA",K_LOCALE_INFO_A,I,4,U,U,P,I), D("GetLocaleInfoW",K_LOCALE_INFO_W,I,4,U,U,P,I)
};
#undef D
#undef P
#undef I
#undef U
#undef Z
#undef V

static uint64_t integer(const qa_native_value *value)
{
    switch (value->type) {
    case QA_NATIVE_ADDRESS: return value->as.address;
    case QA_NATIVE_I32: return (uint64_t)(int64_t)value->as.i32;
    case QA_NATIVE_U32: return value->as.u32;
    case QA_NATIVE_I64: return (uint64_t)value->as.i64;
    default: return value->as.u64;
    }
}

static void result(qa_native_value *out, qa_native_value_type type, uint64_t value)
{
    *out = (qa_native_value){.type = type};
    if (type == QA_NATIVE_ADDRESS) out->as.address = value;
    else if (type == QA_NATIVE_U32) out->as.u32 = (uint32_t)value;
    else if (type == QA_NATIVE_I32) { uint32_t bits = (uint32_t)value; memcpy(&out->as.i32, &bits, 4); }
}

bool windows_kernel_descriptors(guest_windows *owner, bool bind, qa_error *error)
{
    for (size_t i = 0; i < sizeof(descriptors) / sizeof(*descriptors); ++i) {
        const kernel_descriptor *entry = descriptors + i; qa_native_value_type types[8];
        for (size_t j = 0; j < entry->count; ++j) types[j] = entry->parameters[j] == QA_NATIVE_BYTES ?
            owner->target.pointer_bytes == 4 ? QA_NATIVE_U32 : QA_NATIVE_U64 : entry->parameters[j];
        if (!windows_service_add(owner, UINT64_C(0x57494e0100000000) + i + 1, 1, entry->operation,
            "kernel32.dll", entry->name, types, entry->count, entry->result,
            owner->target.pointer_bytes == 4 ? GUEST_ABI_STDCALL : GUEST_ABI_DEFAULT, bind, error)) return false;
    }
    return true;
}

static bool contains(const uint64_t *values, size_t count, uint64_t value)
{ for (size_t i = 0; i < count; ++i) if (values[i] == value) return true; return false; }

static bool add(uint64_t **values, size_t *count, size_t *capacity, uint64_t value, qa_error *error)
{
    if (contains(*values, *count, value)) return true;
    if (!guest_grow((void **)values, capacity, *count + 1, sizeof(**values), error)) return false;
    (*values)[(*count)++] = value; return true;
}

static bool erase(uint64_t *values, size_t *count, uint64_t value)
{
    for (size_t i = 0; i < *count; ++i) if (values[i] == value) {
        memmove(values + i, values + i + 1, (*count - i - 1) * sizeof(*values)); --*count; return true;
    }
    return false;
}

static bool standard_set(guest_windows_kernel *kernel, int32_t id, uint64_t handle, qa_error *error)
{
    for (size_t i = 0; i < kernel->standard_count; ++i) if (kernel->standards[i].id == id) {
        kernel->standards[i].handle = handle; return true;
    }
    if (!guest_grow((void **)&kernel->standards, &kernel->standard_capacity,
        kernel->standard_count + 1, sizeof(*kernel->standards), error)) return false;
    kernel->standards[kernel->standard_count++] = (windows_standard){id, handle}; return true;
}

static uint64_t standard_get(guest_windows *owner, int32_t id, bool invalid)
{
    for (size_t i = 0; i < owner->kernel->standard_count; ++i)
        if (owner->kernel->standards[i].id == id) return owner->kernel->standards[i].handle;
    return invalid ? owner->target.pointer_bytes == 4 ? UINT32_MAX : UINT64_MAX : 0;
}

static bool handle_add(guest_windows_kernel *kernel, uint64_t handle, int32_t stream, qa_error *error)
{
    if (!guest_grow((void **)&kernel->handles, &kernel->handle_capacity,
        kernel->handle_count + 1, sizeof(*kernel->handles), error)) return false;
    kernel->handles[kernel->handle_count++] = (windows_file_handle){handle, stream, 0}; return true;
}

static windows_file_handle *handle_at(guest_windows_kernel *kernel, uint64_t handle)
{ for (size_t i = 0; i < kernel->handle_count; ++i) if (kernel->handles[i].handle == handle) return kernel->handles + i; return NULL; }

bool windows_kernel_initialize(guest_windows *owner, const guest_windows_options *options, qa_error *error)
{
    guest_windows_kernel *kernel = owner->kernel; size_t width = owner->target.pointer_bytes;
    static const uint16_t default_command[] = {'"','q','u','a','k','e','-','t','y','p','e','s','c','r','i','p','t','.','e','x','e','"'};
    const uint16_t *command = options->command_line ? options->command_line : default_command;
    size_t command_length = options->command_line ? options->command_line_units : sizeof(default_command) / 2;
    if (options->environment_units == SIZE_MAX) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows environment extent overflows");
    uint16_t *environment = calloc(options->environment_units + 1, 2);
    if (!environment) return guest_fail(error, QA_ERROR_MEMORY, 0, "encoding Windows environment block");
    if (options->environment_units) {
        if (!options->environment) { free(environment); return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows environment has no source bytes"); }
        memcpy(environment, options->environment, options->environment_units * 2);
    }
    bool okay = windows_store_string(owner, command, command_length, false, &kernel->command_line_a, error) &&
        windows_store_string(owner, command, command_length, true, &kernel->command_line_w, error) &&
        windows_store_string(owner, environment, options->environment_units + 1, false, &kernel->environment_a, error) &&
        windows_store_string(owner, environment, options->environment_units + 1, true, &kernel->environment_w, error);
    free(environment);
    uint8_t secret[8] = {0};
    if (!okay || !owner->capabilities.entropy(owner->capabilities.context, secret, width, error)) return false;
    kernel->pointer_secret = (width == 4 ? qa_load_u32le(secret) : qa_load_u64le(secret)) | 1;
    if (!windows_storage(owner, 16, &kernel->process_heap, error) ||
        !add(&kernel->heaps, &kernel->heap_count, &kernel->heap_capacity, kernel->process_heap, error) ||
        !windows_storage(owner, width * 1024, &kernel->dynamic_tls, error) ||
        !windows_write(owner, owner->teb + (width == 4 ? 0xf94 : 0x1780), width, kernel->dynamic_tls, error)) return false;
    const char *names[] = {"stdin", "stdout", "stderr"};
    const guest_windows_stream_capability *caps[] = {&owner->capabilities.standard_input, &owner->capabilities.standard_output, &owner->capabilities.standard_error};
    for (size_t i = 0; i < 3; ++i) {
        uint16_t text[7] = {0}; size_t length = strlen(names[i]); uint64_t handle;
        for (size_t j = 0; j < length; ++j) text[j] = (uint8_t)names[i][j];
        if (!windows_store_string(owner, text, length, false, &handle, error) ||
            !standard_set(kernel, -10 - (int32_t)i, handle, error) || !handle_add(kernel, handle, (int32_t)i, error)) return false;
        if ((caps[i]->id != 0) != (i == 0 ? caps[i]->read != NULL : caps[i]->write != NULL))
            return guest_fail(error,QA_ERROR_ARGUMENT,i,"Windows stream identity must name its actual process callback");
        owner->stream_ids[i] = caps[i]->id;
    }
    return true;
}

void windows_kernel_dispose(guest_windows_kernel *kernel)
{
    if (!kernel) return;
    for (size_t i=0;i<kernel->pending_count;++i) { free(kernel->pending_files[i]->path); free(kernel->pending_files[i]); }
    free(kernel->pending_files);
    free(kernel->heaps); free(kernel->locks); free(kernel->reservations); free(kernel->standards); free(kernel->handles); free(kernel);
}

static void pending_remove(guest_windows_kernel *kernel, windows_pending_file *record)
{
    for (size_t i=0;i<kernel->pending_count;++i) if (kernel->pending_files[i]==record) {
        memmove(kernel->pending_files+i,kernel->pending_files+i+1,(kernel->pending_count-i-1)*sizeof(*kernel->pending_files));
        --kernel->pending_count; free(record->path); free(record); return;
    }
}

bool windows_kernel_close_pending(guest_windows *owner, qa_error *error)
{
    guest_windows_kernel *kernel=owner->kernel;
    if (!kernel) return true;
    while (kernel->pending_count) {
        windows_pending_file *record=kernel->pending_files[0];
        if (record->opened) {
            if (!record->capability.id || !record->capability.close)
                return guest_fail(error,QA_ERROR_ARGUMENT,record->capability.id,"opened Windows file has no actual close capability");
            ++owner->busy;
            bool closed=record->capability.close(record->capability.context,error);
            --owner->busy;
            if (!closed) return false;
            record->opened=false;
        }
        pending_remove(kernel,record);
    }
    return true;
}

static bool native_count(uint64_t value, size_t *out, qa_error *error)
{
    if (value > 0x10000000) {
        guest_fail(error, QA_ERROR_ARGUMENT, value, "Windows byte count exceeds source native allocation limit");
        return false;
    }
    *out = (size_t)value; return true;
}

static bool narrow_text(guest_windows *owner, uint64_t address, bool wide, char **out, qa_error *error)
{
    uint16_t *text = NULL; size_t length;
    if (!windows_string(owner, address, wide, &text, &length, error)) return false;
    UErrorCode status = U_ZERO_ERROR; int32_t bytes = 0;
    u_strToUTF8(NULL, 0, &bytes, text, (int32_t)length, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) { free(text); return guest_fail(error, QA_ERROR_FORMAT, address, "Windows path has invalid UTF-16"); }
    char *result = malloc((size_t)bytes + 1);
    if (!result) { free(text); return guest_fail(error, QA_ERROR_MEMORY, 0, "encoding Windows path"); }
    status = U_ZERO_ERROR; u_strToUTF8(result, bytes + 1, NULL, text, (int32_t)length, &status); free(text);
    if (U_FAILURE(status)) { free(result); return guest_fail(error, QA_ERROR_FORMAT, address, "Windows path conversion failed"); }
    *out = result; return true;
}

static bool invalid_result(guest_windows *owner, qa_native_value *out,
    qa_native_value_type type, uint32_t code, uint64_t value, qa_error *error)
{ result(out, type, value); return windows_last_error(owner, code, error); }

static bool reservation_contains(const windows_reservation *region, uint64_t base, uint64_t bytes)
{ return base >= region->base && base - region->base <= region->bytes && bytes <= region->bytes - (base - region->base); }

static uint32_t protection(uint32_t value)
{
    switch (value) {
    case 1: return 0; case 2: return QA_NATIVE_GUEST_READ;
    case 4: return QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE;
    case 0x10: return QA_NATIVE_GUEST_EXECUTE; case 0x20: return QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_EXECUTE;
    case 0x40: return QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE | QA_NATIVE_GUEST_EXECUTE;
    default: return UINT32_MAX;
    }
}

static bool virtual_allocate(guest_windows *owner, const qa_native_value *args, qa_native_value *out, qa_error *error)
{
    guest_windows_kernel *kernel = owner->kernel;
    uint64_t requested = integer(args), size = integer(args + 1); uint32_t flags = (uint32_t)integer(args + 2);
    uint32_t permissions = protection((uint32_t)integer(args + 3)); size_t checked;
    if (!native_count(size, &checked, error)) return false;
    if (!size || flags & ~(0x1000u | 0x2000u | 0x100000u) || permissions == UINT32_MAX)
        return guest_fail(error, QA_ERROR_UNSUPPORTED, requested, "unsupported Windows VirtualAlloc flags/protection");
    uint64_t base = requested, bytes = (size + 4095) & ~UINT64_C(4095); bool allocated = false;
    if ((flags & 0x2000) || !base) {
        if (!guest_grow((void **)&kernel->reservations, &kernel->reservation_capacity,
            kernel->reservation_count + 1, sizeof(*kernel->reservations), error)) return false;
        if (!base) {
            if (!qa_native_guest_allocate_aligned(owner->guest, (size_t)bytes, 65536, 0,
                0x575652, &base, error)) return false;
            allocated = true;
        } else {
            base &= ~UINT64_C(65535); bytes = (requested - base + size + 4095) & ~UINT64_C(4095);
        }
        qa_native_guest_mapping mapping; qa_error failure = {0};
        if (!allocated && !qa_native_guest_map(owner->guest, base, (size_t)bytes, 0, (qa_bytes){0}, &mapping, &failure)) {
            if (!owner->guest->failed) return invalid_result(owner, out, QA_NATIVE_ADDRESS, 487, 0, error);
            if (error) *error = failure;
            return false;
        }
        kernel->reservations[kernel->reservation_count++] = (windows_reservation){base, bytes, allocated};
    } else {
        base &= ~UINT64_C(4095); bytes = (requested - base + size + 4095) & ~UINT64_C(4095);
        bool found = false;
        for (size_t i = 0; i < kernel->reservation_count; ++i) if (reservation_contains(kernel->reservations + i, base, bytes)) found = true;
        if (!found) return invalid_result(owner, out, QA_NATIVE_ADDRESS, 487, 0, error);
    }
    if ((flags & 0x1000) && !qa_native_guest_protect_range(owner->guest, base, (size_t)bytes, permissions, error)) return false;
    result(out, QA_NATIVE_ADDRESS, base); return true;
}

static bool virtual_free(guest_windows *owner, const qa_native_value *args, qa_native_value *out, qa_error *error)
{
    guest_windows_kernel *kernel = owner->kernel; uint64_t base = integer(args), size = integer(args + 1);
    uint32_t flags = (uint32_t)integer(args + 2); result(out, QA_NATIVE_I32, 0);
    size_t checked;
    if (!base) return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows VirtualFree address is null");
    if (!native_count(size,&checked,error)) return false;
    if (flags == 0x8000 && !size) {
        for (size_t i = 0; i < kernel->reservation_count; ++i) if (kernel->reservations[i].base == base) {
            bool okay = kernel->reservations[i].allocated ? qa_native_guest_free(owner->guest, base, error) :
                qa_native_guest_unmap_range(owner->guest, base, (size_t)kernel->reservations[i].bytes, error);
            if (!okay) return false;
            memmove(kernel->reservations + i, kernel->reservations + i + 1,
                (kernel->reservation_count - i - 1) * sizeof(*kernel->reservations)); --kernel->reservation_count;
            result(out, QA_NATIVE_I32, 1); return true;
        }
        return true;
    }
    if (flags == 0x4000 && size) {
        bool found = false;
        for (size_t i = 0; i < kernel->reservation_count; ++i) if (reservation_contains(kernel->reservations + i, base, size)) found = true;
        if (!found) return true;
        uint64_t page = base & ~UINT64_C(4095), bytes = (base - page + size + 4095) & ~UINT64_C(4095);
        if (!qa_native_guest_protect_range(owner->guest, page, (size_t)bytes, QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, error) ||
            !windows_zero(owner, base, (size_t)size, error) || !qa_native_guest_protect_range(owner->guest, page, (size_t)bytes, 0, error)) return false;
        result(out, QA_NATIVE_I32, 1); return true;
    }
    return windows_last_error(owner, 87, error);
}

static uint64_t tls_slot(guest_windows *owner, uint32_t index)
{
    size_t width = owner->target.pointer_bytes;
    return index < 64 ? owner->teb + (width == 4 ? 0xe10 : 0x1480) + (uint64_t)index * width :
        owner->kernel->dynamic_tls + (uint64_t)(index - 64) * width;
}

static bool file_operation(windows_service *service, const qa_native_value *args,
    qa_native_value *out, qa_error *error)
{
    guest_windows *owner = service->owner; guest_windows_kernel *kernel = owner->kernel;
    uint64_t address = integer(args); windows_file_handle *handle = handle_at(kernel, address);
    uint32_t operation = service->operation; size_t width = owner->target.pointer_bytes;
    if (operation == K_STD_GET) { result(out, QA_NATIVE_ADDRESS, standard_get(owner, args[0].as.i32, true)); return true; }
    if (operation == K_STD_SET) {
        result(out, QA_NATIVE_I32, integer(args + 1) != 0);
        return !integer(args + 1) || standard_set(kernel, args[0].as.i32, integer(args + 1), error);
    }
    if (operation == K_HANDLE_COUNT) { result(out, QA_NATIVE_U32, address); return true; }
    if (operation == K_FILE_TYPE) { result(out, QA_NATIVE_U32, !handle ? 0 : handle->stream >= 0 ? 2 : 1); return true; }
    if (operation == K_STARTUP) {
        size_t bytes = width == 4 ? 68 : 104;
        if (!windows_zero(owner, address, bytes, error) || !windows_write(owner, address, 4, bytes, error)) return false;
        for (size_t i = 0; i < 3; ++i)
            if (!windows_write(owner, address + (width == 4 ? 56 : 80) + i * width,
                width, standard_get(owner, -10 - (int32_t)i, false), error)) return false;
        result(out, QA_NATIVE_VOID, 0); return true;
    }
    if (operation == K_FILE_CREATE) {
        char *path = NULL;
        if (!narrow_text(owner, address, false, &path, error)) return false;
        uint32_t access = (uint32_t)integer(args + 1), creation = (uint32_t)integer(args + 4);
        uint32_t mode = ((access & 0x80000000u) ? (uint32_t)GUEST_RUNTIME_FILE_READ : 0u) |
            ((access & 0x40000000u) ? (uint32_t)GUEST_RUNTIME_FILE_WRITE : 0u);
        windows_pending_file *pending=calloc(1,sizeof(*pending));
        if (!pending || !guest_grow((void **)&kernel->pending_files,&kernel->pending_capacity,
            kernel->pending_count+1,sizeof(*kernel->pending_files),error)) {
            free(pending); free(path); return guest_fail(error,QA_ERROR_MEMORY,address,"retaining provisional opened Windows file owner");
        }
        pending->path=path; pending->creation=creation;
        kernel->pending_files[kernel->pending_count++]=pending;
        /* Stable record storage survives a nested service callback and owns an
         * actual opened capability before any guest allocation can fail. */
        bool okay = !owner->capabilities.open_file || owner->capabilities.open_file(owner->capabilities.context,
            path,mode,creation,&pending->capability,&pending->opened,error);
        if (!okay) { if (!pending->opened) pending_remove(kernel,pending); return false; }
        if (!pending->opened) { pending_remove(kernel,pending); return invalid_result(owner,out,QA_NATIVE_ADDRESS,2,width==4?UINT32_MAX:UINT64_MAX,error); }
        uint64_t new_handle;
        okay = windows_storage(owner, 16, &new_handle, error) &&
            guest_runtime_resources_file(owner->resources,new_handle,path,creation,&pending->capability,error);
        if (!okay) return false;
        pending->opened=false; pending_remove(kernel,pending);
        if (!handle_add(kernel,new_handle,-1,error)) return false;
        result(out, QA_NATIVE_ADDRESS, new_handle); return true;
    }
    if (!handle) {
        if (operation == K_FILE_FLUSH || operation == K_FILE_END) { result(out, QA_NATIVE_I32, 0); return true; }
        return invalid_result(owner, out, operation == K_FILE_SEEK ? QA_NATIVE_U32 : QA_NATIVE_I32,
            6, operation == K_FILE_SEEK ? UINT32_MAX : 0, error);
    }
    guest_runtime_file_view view = {0}; qa_error missing = {0};
    bool registered = guest_runtime_resources_find(owner->resources, address, &view, &missing);
    if (!registered && missing.code != QA_ERROR_NOT_FOUND) { if (error) *error = missing; return false; }
    if (operation == K_FILE_READ || operation == K_FILE_WRITE) {
        if (integer(args + 4)) return guest_fail(error, QA_ERROR_UNSUPPORTED, address, "Windows overlapped I/O is not implemented");
        size_t requested;
        if (!native_count(integer(args + 2), &requested, error)) return false;
        if (operation == K_FILE_READ && handle->stream > 0)
            return invalid_result(owner,out,QA_NATIVE_I32,5,0,error);
        const guest_windows_stream_capability *stream = handle->stream == 0 ? &owner->capabilities.standard_input :
            handle->stream == 1 ? &owner->capabilities.standard_output : &owner->capabilities.standard_error;
        if (operation == K_FILE_READ && handle->stream >= 0 && !stream->id) {
            if (!integer(args + 1)) return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows file read destination is null");
            if (integer(args + 3) && !windows_write(owner, integer(args + 3), 4, 0, error)) return false;
            result(out, QA_NATIVE_I32, 1); return true;
        }
        uint8_t *bytes = requested ? malloc(requested) : NULL;
        if (requested && !bytes) return guest_fail(error, QA_ERROR_MEMORY, 0, "staging Windows file bytes");
        size_t completed = 0; bool okay;
        if (operation == K_FILE_WRITE) {
            if (!integer(args + 1)) { free(bytes); return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows file write source is null"); }
            okay = qa_native_guest_read(owner->guest, integer(args + 1), bytes, requested, error);
            if (okay && handle->stream == 0) { free(bytes); return invalid_result(owner,out,QA_NATIVE_I32,5,0,error); }
            if (okay && handle->stream >= 0 && !stream->id) { free(bytes); return invalid_result(owner,out,QA_NATIVE_I32,6,0,error); }
            if (okay && handle->stream >= 0) {
                okay = stream->write(stream->context,(qa_bytes){bytes,requested},error);
                if (okay) completed = requested;
            } else if (okay) okay = guest_runtime_resources_write(owner->resources, address, (qa_bytes){bytes, requested}, &completed, error);
        } else {
            okay = handle->stream >= 0 ? stream->read(stream->context,bytes,requested,&completed,error) :
                guest_runtime_resources_read(owner->resources, address, bytes, requested, &completed, error);
            if (completed > requested) { free(bytes); return guest_fail(error,QA_ERROR_FORMAT,address,"Windows stream exceeded requested read length"); }
            /* Preserve actual completed host bytes even when its callback fails. */
            qa_error failure = error ? *error : (qa_error){0};
            bool written = integer(args + 1) ? !completed ||
                qa_native_guest_write(owner->guest,integer(args + 1),(qa_bytes){bytes,completed},error) :
                guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows file read destination is null");
            if (!written) {
                qa_error destination_failure = error ? *error : (qa_error){0};
                bool retained = handle->stream >= 0 ||
                    guest_runtime_resources_seek(owner->resources,address,view.offset,error);
                free(bytes);
                if (retained && error) *error = destination_failure;
                return false;
            }
            if (!okay && error) *error = failure;
        }
        free(bytes);
        if (completed) {
            /* Windows' source cursor is a Number. Keep its actual nearest
             * binary64 addition, including positions beyond MAX_SAFE_INTEGER. */
            fenv_t saved; bool held = feholdexcept(&saved) == 0;
            if (!held || fesetround(FE_TONEAREST) != 0) {
                if (held) fesetenv(&saved);
                return guest_fail(error,QA_ERROR_UNSUPPORTED,address,"host cannot provide Windows Number cursor rounding");
            }
            volatile double start = handle->stream >= 0 ? handle->stream_offset : (double)view.offset;
            volatile double amount = (double)completed;
            double next = start + amount;
            if (fesetenv(&saved) != 0) return guest_fail(error,QA_ERROR_UNSUPPORTED,address,"Windows cursor environment restore failed");
            if (handle->stream >= 0) handle->stream_offset = next;
            else {
                if (!isfinite(next) || next < 0 || next >= 0x1p64)
                    return guest_fail(error,QA_ERROR_UNSUPPORTED,address,"actual Windows file cursor exceeds its uint64 external capability domain");
                qa_error failure = error ? *error : (qa_error){0};
                if (!guest_runtime_resources_seek(owner->resources,address,(uint64_t)next,error)) return false;
                if (!okay && error) *error = failure;
            }
        }
        if (integer(args + 3)) {
            qa_error failure = error ? *error : (qa_error){0};
            if (!windows_write(owner, integer(args + 3), 4, completed, error)) return false;
            if (!okay && error) *error = failure;
        }
        result(out, QA_NATIVE_I32, okay); return okay;
    }
    if (operation == K_FILE_CLOSE) {
        if (handle->stream < 0 && registered && !guest_runtime_resources_close(owner->resources, address, error)) return false;
        size_t index = (size_t)(handle - kernel->handles);
        memmove(kernel->handles + index, kernel->handles + index + 1,
            (kernel->handle_count - index - 1) * sizeof(*kernel->handles)); --kernel->handle_count;
        result(out, QA_NATIVE_I32, 1); return true;
    }
    if (operation == K_FILE_FLUSH) {
        bool okay = handle->stream >= 0 || (registered && guest_runtime_resources_flush(owner->resources, address, error));
        result(out, QA_NATIVE_I32, okay); return okay;
    }
    if (operation == K_FILE_END) {
        if (handle->stream >= 0) { result(out, QA_NATIVE_I32, 0); return true; }
        if (!registered || !guest_runtime_resources_truncate(owner->resources, address, view.offset, error)) return false;
        result(out, QA_NATIVE_I32, 1); return true;
    }
    if (operation == K_FILE_SEEK) {
        if (handle->stream >= 0 || !registered) return invalid_result(owner, out, QA_NATIVE_U32, 6, UINT32_MAX, error);
        uint64_t high = integer(args + 2), distance_bits; int64_t distance;
        if (high) {
            uint64_t bits;
            if (!windows_read(owner, high, 4, &bits, error)) return false;
            distance_bits = (bits << 32) | (uint32_t)args[1].as.i32; memcpy(&distance, &distance_bits, 8);
        } else distance = args[1].as.i32;
        uint32_t origin = (uint32_t)integer(args + 3); uint64_t start = origin == 0 ? 0 : view.offset;
        if (origin == 2) {
            if (!guest_runtime_resources_size(owner->resources, address, &start, error)) return false;
            /* Source size() is a Number before BigInt seek arithmetic. Round
             * its physical integer to 53 significant bits, nearest/even,
             * independently of the guest's current floating-point mode. */
            unsigned shift = 0; uint64_t significand = start;
            while (significand > UINT64_C(9007199254740991)) { significand >>= 1; ++shift; }
            if (shift) {
                uint64_t remainder = start & ((UINT64_C(1) << shift) - 1);
                uint64_t half = UINT64_C(1) << (shift - 1);
                if (remainder > half || (remainder == half && (significand & 1u))) ++significand;
                /* A size rounded to 2^64 cannot reach a safe Source position
                 * with this API's signed 64-bit displacement. */
                if (significand > (UINT64_MAX >> shift))
                    return invalid_result(owner, out, QA_NATIVE_U32, 87, UINT32_MAX, error);
                start = significand << shift;
            }
        }
        uint64_t magnitude = distance < 0 ? UINT64_C(0) - (uint64_t)distance : (uint64_t)distance;
        if (origin > 2 || (distance < 0 ? magnitude > start : magnitude > UINT64_C(9007199254740991) || start > UINT64_C(9007199254740991) - magnitude))
            return invalid_result(owner, out, QA_NATIVE_U32, 87, UINT32_MAX, error);
        uint64_t position = distance < 0 ? start - magnitude : start + magnitude;
        if (position > UINT64_C(9007199254740991)) return invalid_result(owner, out, QA_NATIVE_U32, 87, UINT32_MAX, error);
        if (!guest_runtime_resources_seek(owner->resources, address, position, error) ||
            (high && !windows_write(owner, high, 4, position >> 32, error))) return false;
        result(out, QA_NATIVE_U32, position); return true;
    }
    return guest_fail(error, QA_ERROR_ARGUMENT, operation, "invalid Windows file operation");
}

static bool input_text(guest_windows *owner, uint64_t address, int32_t length,
    bool wide, uint16_t **out, size_t *count, qa_error *error)
{
    if (length == -1) {
        if (!windows_string(owner, address, wide, out, count, error)) return false;
        ++*count; return true;
    }
    if (length < 0 || length > 1048576) return guest_fail(error, QA_ERROR_ARGUMENT, address, "invalid Windows string length");
    uint16_t *text = calloc((size_t)length + 1, 2);
    if (!text) return guest_fail(error, QA_ERROR_MEMORY, 0, "reading Windows conversion text");
    for (int32_t i = 0; i < length; ++i) {
        uint64_t value;
        if (!windows_read(owner, address + (uint64_t)i * (wide ? 2 : 1), wide ? 2 : 1, &value, error)) { free(text); return false; }
        text[i] = (uint16_t)value;
    }
    *out = text; *count = (size_t)length; return true;
}

static const uint16_t cp1252[32] = {
    0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,
    0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178
};

static bool locale_information(windows_service *service, const qa_native_value *args,
    qa_native_value *out, qa_error *error)
{
    guest_windows *owner = service->owner;
    uint32_t locale = (uint32_t)integer(args), request = (uint32_t)integer(args + 1);
    uint64_t output = integer(args + 2); int32_t capacity = args[3].as.i32;
    bool wide = service->operation == K_LOCALE_INFO_W;
    if ((locale & UINT32_C(0xfff00000)) || capacity < 0 || !(request & 0xffffu))
        return invalid_result(owner, out, QA_NATIVE_I32, 87, 0, error);
    if (request & UINT32_C(0x1fff0000))
        return invalid_result(owner, out, QA_NATIVE_I32, 1004, 0, error);
    const qa_native_windows_locale_profile *profile = &owner->capabilities.locale;
    const qa_native_windows_locale *record = NULL;
    qa_native_windows_locale canonical = {.lcid = locale, .language_id = locale, .ansi_code_page = 1252, .oem_code_page = 437,
        .decimal = {'.'}, .thousands = {','}, .grouping = {'3',';','0'},
        .default_decimal = {'.'}, .default_thousands = {','}, .default_grouping = {'3',';','0'}};
    if (locale == 0 || locale == 0x0400 || locale == 0x0800) {
        record = locale == 0x0800 ? &profile->system : &profile->user;
        if (!record->lcid)
            return guest_fail(error,QA_ERROR_UNSUPPORTED,locale,"Windows default locale has no acquired supported profile");
    } else if (locale == profile->user.lcid) record = &profile->user;
    else if (locale == profile->system.lcid) record = &profile->system;
    else if (locale == 0x007f || locale == 0x0409) record = &canonical;
    else return guest_fail(error,QA_ERROR_UNSUPPORTED,locale,"Windows locale information requires an implemented explicit locale");
    bool number = (request & UINT32_C(0x20000000)) != 0;
    bool defaults = (request & UINT32_C(0x80000000)) != 0;
    uint32_t kind = request & 0xffffu, value = 0; uint16_t numeric[16] = {0}; const uint16_t *text;
    switch (kind) {
    case 0x0001: value = record->language_id; text = numeric; break;
    case 0x000b: value = record->oem_code_page; text = numeric; break;
    case 0x1004: value = record->ansi_code_page; text = numeric; break;
    case 0x000e: text = defaults ? record->default_decimal : record->decimal; break;
    case 0x000f: text = defaults ? record->default_thousands : record->thousands; break;
    case 0x0010: text = defaults ? record->default_grouping : record->grouping; break;
    default: return guest_fail(error, QA_ERROR_UNSUPPORTED, kind, "Windows locale information field is not implemented");
    }
    if (number && text != numeric) return invalid_result(owner,out,QA_NATIVE_I32,1004,0,error);
    if (text == numeric && !number) {
        char digits[16];
        int length = kind == 1 ? snprintf(digits,sizeof(digits),"%04x",value) : snprintf(digits,sizeof(digits),"%u",value);
        if (length < 0 || (size_t)length >= sizeof(digits))
            return guest_fail(error,QA_ERROR_FORMAT,value,"Windows locale numeric text exceeds its actual field");
        for (int i = 0; i < length; ++i) numeric[i] = (uint8_t)digits[i];
    }
    size_t needed = number ? (wide ? 2u : 4u) : 0;
    uint8_t bytes[QA_NATIVE_WINDOWS_LOCALE_UNITS];
    if (!number) {
        while (text[needed]) ++needed;
        ++needed;
        if (!wide) {
            uint32_t cp = request & UINT32_C(0x40000000) ? profile->ansi_code_page : record->ansi_code_page;
            if (!cp) cp = profile->source ? profile->ansi_code_page : 1252;
            for (size_t i = 0; i < needed; ++i) {
                uint16_t c = text[i]; int encoded = c < 128 ? c : -1;
                if (cp == 1252) {
                    if (c >= 160 && c <= 255) encoded = c;
                    for (size_t j = 0; j < 32; ++j) if (cp1252[j] == c) encoded = (int)j + 128;
                    if (encoded < 0) encoded = '?';
                }
                if (encoded < 0) return guest_fail(error,QA_ERROR_UNSUPPORTED,cp,"Windows locale text code page is not implemented");
                bytes[i] = (uint8_t)encoded;
            }
        }
    }
    if (!capacity) { result(out,QA_NATIVE_I32,needed); return true; }
    if (!output || (size_t)capacity < needed) return invalid_result(owner,out,QA_NATIVE_I32,122,0,error);
    if (number) {
        if (!windows_write(owner,output,4,value,error)) return false;
    } else if (wide) {
        for (size_t i = 0; i < needed; ++i)
            if (!windows_write(owner,output + i * 2,2,text[i],error)) return false;
    } else if (!qa_native_guest_write(owner->guest,output,(qa_bytes){bytes,needed},error)) return false;
    result(out,QA_NATIVE_I32,needed); return true;
}

static bool locale_operation(windows_service *service, const qa_native_value *args,
    qa_native_value *out, qa_error *error)
{
    guest_windows *owner = service->owner; uint32_t operation = service->operation;
    bool wide = operation != K_MULTI_WIDE && operation != K_STRING_TYPE_A && operation != K_MAP_A;
    size_t shift = operation == K_STRING_TYPE_A ? 1 : 0;
    bool string_type = operation == K_STRING_TYPE_A || operation == K_STRING_TYPE_W;
    uint32_t cp = (uint32_t)integer(args);
    if ((operation == K_MULTI_WIDE || operation == K_WIDE_MULTI) && !cp)
        cp = owner->capabilities.locale.source ? owner->capabilities.locale.ansi_code_page : 1252;
    if ((operation == K_MULTI_WIDE || operation == K_WIDE_MULTI) && cp != 0 && cp != 1252 && cp != 65001)
        return guest_fail(error, QA_ERROR_UNSUPPORTED, cp, "unsupported Windows text code page");
    if (string_type && integer(args + shift) != 1) return guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "only Windows CT_CTYPE1 is implemented");
    uint32_t flags = (uint32_t)integer(args + 1);
    if ((operation == K_MAP_A || operation == K_MAP_W) && flags != 0x100 && flags != 0x200)
        return guest_fail(error, QA_ERROR_UNSUPPORTED, flags, "unsupported Windows LCMapString flags");
    size_t source_index = string_type ? shift + 1 : 2;
    uint16_t *source = NULL; size_t source_count;
    if (!input_text(owner, integer(args + source_index), args[source_index + 1].as.i32,
        wide, &source, &source_count, error)) return false;
    if (string_type) {
        uint64_t output = integer(args + shift + 3); bool okay = true;
        for (size_t i = 0; okay && i < source_count; ++i) {
            uint16_t c = source[i]; bool upper = c >= 'A' && c <= 'Z', lower = c >= 'a' && c <= 'z', digit = c >= '0' && c <= '9';
            uint32_t type = (upper ? 1 : 0) | (lower ? 2 : 0) | (digit ? 4 : 0) | (qa_unicode_whitespace(c) ? 8 : 0) |
                ((c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~') ? 16 : 0) |
                (c < 32 || c == 127 ? 32 : 0) | (c == ' ' || c == '\t' ? 64 : 0) |
                (digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ? 128 : 0) | (upper || lower ? 256 : 0);
            okay = windows_write(owner, output + i * 2, 2, type, error);
        }
        free(source); result(out, QA_NATIVE_I32, 1); return okay;
    }
    uint16_t *units = NULL; uint8_t *bytes = NULL; size_t length = 0; bool output_wide = operation == K_MULTI_WIDE || operation == K_MAP_W;
    UErrorCode status = U_ZERO_ERROR; bool used_default = false;
    if (operation == K_MULTI_WIDE && cp == 65001) {
        bytes = malloc(source_count ? source_count : 1);
        if (!bytes) { free(source); return guest_fail(error, QA_ERROR_MEMORY, 0, "staging Windows UTF-8 input"); }
        for (size_t i = 0; i < source_count; ++i) bytes[i] = (uint8_t)source[i];
        int32_t needed = 0;
        u_strFromUTF8WithSub(NULL, 0, &needed, (const char *)bytes, (int32_t)source_count, 0xfffd, NULL, &status);
        if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) { free(source); free(bytes); return guest_fail(error, QA_ERROR_FORMAT, 0, "Windows UTF-8 conversion failed"); }
        units = calloc((size_t)needed + 1, 2); status = U_ZERO_ERROR;
        if (!units) { free(source); free(bytes); return guest_fail(error, QA_ERROR_MEMORY, 0, "encoding Windows UTF-16 output"); }
        u_strFromUTF8WithSub(units, needed + 1, &needed, (const char *)bytes, (int32_t)source_count, 0xfffd, NULL, &status);
        length = (size_t)needed; free(bytes); bytes = NULL;
        /* TextDecoder consumes an initial UTF-8 BOM. */
        if (length && units[0] == 0xfeff) { memmove(units, units + 1, --length * 2); }
    } else if (operation == K_MULTI_WIDE) {
        units = calloc(source_count + 1, 2);
        if (!units) { free(source); return guest_fail(error, QA_ERROR_MEMORY, 0, "encoding Windows code page output"); }
        length = source_count;
        for (size_t i = 0; i < length; ++i) units[i] = source[i] >= 128 && source[i] < 160 ? cp1252[source[i] - 128] : source[i];
    } else if (operation == K_WIDE_MULTI && cp == 65001) {
        int32_t needed = 0;
        u_strToUTF8WithSub(NULL, 0, &needed, source, (int32_t)source_count, 0xfffd, NULL, &status);
        if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) { free(source); return guest_fail(error, QA_ERROR_FORMAT, 0, "Windows UTF-16 conversion failed"); }
        bytes = malloc((size_t)needed + 1); status = U_ZERO_ERROR;
        if (!bytes) { free(source); return guest_fail(error, QA_ERROR_MEMORY, 0, "encoding Windows UTF-8 output"); }
        u_strToUTF8WithSub((char *)bytes, needed + 1, &needed, source, (int32_t)source_count, 0xfffd, NULL, &status); length = (size_t)needed;
    } else if (operation == K_WIDE_MULTI) {
        bytes = malloc(source_count ? source_count : 1);
        if (!bytes) { free(source); return guest_fail(error, QA_ERROR_MEMORY, 0, "encoding Windows narrow output"); }
        for (size_t i = 0; i < source_count; ++i) {
            uint16_t c = source[i]; int converted = c < 128 || (c >= 160 && c <= 255) ? c : -1;
            for (int j = 0; j < 32; ++j) if (cp1252[j] == c) converted = j + 128;
            if (converted < 0) { converted = '?'; used_default = true; }
            bytes[length++] = (uint8_t)converted;
            /* Uint8Array.from(string, ...) iterates Unicode scalar values. */
            if (c >= 0xd800 && c <= 0xdbff && i + 1 < source_count && source[i + 1] >= 0xdc00 && source[i + 1] <= 0xdfff) ++i;
        }
    } else {
        int32_t needed = flags == 0x100 ? u_strToLower(NULL, 0, source, (int32_t)source_count, "", &status) :
            u_strToUpper(NULL, 0, source, (int32_t)source_count, "", &status);
        if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) { free(source); return guest_fail(error, QA_ERROR_FORMAT, 0, "Windows Unicode case conversion failed"); }
        units = calloc((size_t)needed + 1, 2); status = U_ZERO_ERROR;
        if (!units) { free(source); return guest_fail(error, QA_ERROR_MEMORY, 0, "encoding Windows mapped text"); }
        if (flags == 0x100) u_strToLower(units, needed + 1, source, (int32_t)source_count, "", &status);
        else u_strToUpper(units, needed + 1, source, (int32_t)source_count, "", &status);
        length = (size_t)needed;
        if (!output_wide) {
            bytes = malloc(length ? length : 1);
            if (!bytes) { free(source); free(units); return guest_fail(error, QA_ERROR_MEMORY, 0, "encoding Windows mapped bytes"); }
            for (size_t i = 0; i < length; ++i) bytes[i] = (uint8_t)units[i];
        }
    }
    free(source);
    if (U_FAILURE(status)) { free(units); free(bytes); return guest_fail(error, QA_ERROR_FORMAT, 0, "Windows text conversion failed"); }
    if (operation == K_WIDE_MULTI && integer(args + 7) && !windows_write(owner, integer(args + 7), 4, used_default, error)) { free(units); free(bytes); return false; }
    int32_t capacity = args[5].as.i32; uint64_t output = integer(args + 4);
    if (capacity != 0 && (!output || capacity < 0 || (size_t)capacity < length)) {
        free(units); free(bytes); return invalid_result(owner, out, QA_NATIVE_I32, 122, 0, error);
    }
    bool okay = true;
    if (capacity != 0) {
        if (output_wide) for (size_t i = 0; okay && i < length; ++i) okay = windows_write(owner, output + i * 2, 2, units[i], error);
        else okay = qa_native_guest_write(owner->guest, output, (qa_bytes){bytes, length}, error);
    }
    free(units); free(bytes); result(out, QA_NATIVE_I32, length); return okay;
}

bool windows_kernel_invoke(windows_service *service, const qa_native_value *args,
    size_t count, qa_native_value *out, qa_error *error)
{
    (void)count;
    guest_windows *owner = service->owner; guest_windows_kernel *kernel = owner->kernel;
    size_t width = owner->target.pointer_bytes; uint32_t operation = service->operation;
    uint64_t a = count ? integer(args) : 0, b = count > 1 ? integer(args + 1) : 0, value = 0;
    qa_native_value_type type = service->function.signature.result.kind;
    result(out, type, 0);
    if (operation >= K_STD_GET && operation <= K_FILE_END) return file_operation(service, args, out, error);
    if (operation == K_LOCALE_INFO_A || operation == K_LOCALE_INFO_W) return locale_information(service, args, out, error);
    if (operation >= K_MULTI_WIDE) return locale_operation(service, args, out, error);
    switch (operation) {
    case K_ENCODE: result(out, type, a ^ kernel->pointer_secret); return true;
    case K_HEAP: result(out, type, kernel->process_heap); return true;
    case K_LAST_ERROR:
        if (!windows_read(owner, owner->teb + (width == 4 ? 0x34 : 0x68), 4, &value, error)) return false;
        result(out, type, value); return true;
    case K_SET_ERROR: return windows_last_error(owner, (uint32_t)a, error);
    case K_THREAD: result(out, type, owner->thread_id); return true;
    case K_PROCESS: result(out, type, owner->process_id); return true;
    case K_CURRENT_PROCESS: result(out, type, width == 4 ? UINT32_MAX : UINT64_MAX); return true;
    case K_VERSION: result(out, type, 0x05650004); return true;
    case K_COMMAND_A: result(out, type, kernel->command_line_a); return true;
    case K_COMMAND_W: result(out, type, kernel->command_line_w); return true;
    case K_ACP: result(out, type, owner->capabilities.locale.source ? owner->capabilities.locale.ansi_code_page : 1252); return true;
    case K_OEM: result(out, type, owner->capabilities.locale.source ? owner->capabilities.locale.oem_code_page : 437); return true;
    case K_VALID_CP: result(out, type, a == 1252 || a == 437 || a == 65001 ||
        (a && owner->capabilities.locale.source &&
         (a == owner->capabilities.locale.ansi_code_page || a == owner->capabilities.locale.oem_code_page ||
          a == owner->capabilities.locale.user.ansi_code_page || a == owner->capabilities.locale.user.oem_code_page ||
          a == owner->capabilities.locale.system.ansi_code_page || a == owner->capabilities.locale.system.oem_code_page))); return true;
    case K_CP_INFO:
        if (a == 0) a = owner->capabilities.locale.source ? owner->capabilities.locale.ansi_code_page : 1252;
        else if (a == 1) a = owner->capabilities.locale.source ? owner->capabilities.locale.oem_code_page : 437;
        if (a != 1252 && a != 437 && a != 65001)
            return guest_fail(error,QA_ERROR_UNSUPPORTED,a,"Windows code page metadata is not implemented");
        if (!windows_zero(owner, b, 20, error) || !windows_write(owner, b, 4, a == 65001 ? 4 : 1, error) || !windows_write(owner, b + 4, 2, 63, error)) return false;
        result(out, type, 1); return true;
    case K_ENV_A: result(out, type, kernel->environment_a); return true;
    case K_ENV_W: result(out, type, kernel->environment_w); return true;
    case K_FREE_ENV: result(out, type, 1); return true;
    case K_MODULE_A: case K_MODULE_W: case K_LOAD_A: case K_LOAD_W: case K_LOAD_EX_A: case K_LOAD_EX_W: {
        bool wide = operation == K_MODULE_W || operation == K_LOAD_W || operation == K_LOAD_EX_W;
        bool module = operation == K_MODULE_A || operation == K_MODULE_W;
        bool extended = operation == K_LOAD_EX_A || operation == K_LOAD_EX_W;
        uint32_t flags = extended ? (uint32_t)integer(args + 2) : 0;
        if (extended && (!a || b || (flags & ~0x3fffu))) return invalid_result(owner, out, type, 87, 0, error);
        if (extended && (flags & ~0x1f00u)) return invalid_result(owner, out, type, 50, 0, error);
        if (module && !a) {
            const windows_image_record *primary = windows_image_at(owner, owner->primary_image);
            value = primary ? primary->base : 0;
        }
        else {
            char *name = NULL;
            if (!narrow_text(owner, a, wide, &name, error)) return false;
            bool absolute = (strlen(name) >= 3 && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z')) && name[1] == ':' && (name[2] == '/' || name[2] == '\\')) ||
                ((name[0] == '/' || name[0] == '\\') && (name[1] == '/' || name[1] == '\\'));
            if (extended && (!*name || ((flags & 0x100) && !absolute))) { free(name); return invalid_result(owner, out, type, 87, 0, error); }
            bool okay = module ? windows_library_handle(owner, name, &value, error) : windows_load_library(owner, name, &value, error);
            free(name); if (!okay) return false;
        }
        if (!value && module && !windows_last_error(owner, 126, error)) return false;
        result(out, type, value); return true;
    }
    case K_FREE_LIBRARY: {
        bool okay;
        if (!a) return invalid_result(owner, out, type, 6, 0, error);
        if (!windows_free_library(owner, a, &okay, error)) return false;
        result(out, type, okay); return true;
    }
    case K_PROC_ADDRESS: {
        const char *library = windows_library_name(owner, a);
        if (!a || !b || !library) return invalid_result(owner, out, type, 127, 0, error);
        char ordinal[24], *name = NULL;
        if (b <= 65535) { snprintf(ordinal, sizeof(ordinal), "#%llu", (unsigned long long)b); }
        else if (!narrow_text(owner, b, false, &name, error)) return false;
        bool okay = guest_windows_resolve(owner, library, name ? name : ordinal, &value, error); free(name);
        if (!okay) return false;
        if (!value && !windows_last_error(owner, 127, error)) return false;
        result(out, type, value); return true;
    }
    case K_DISABLE_THREAD:
        for (size_t i = 0; i < owner->image_count; ++i) if (owner->images[i].base == a && !guest_pe_describe(owner->images[i].image)->tls.present) { result(out, type, 1); return true; }
        return invalid_result(owner, out, type, 87, 0, error);
    case K_FILENAME_A: case K_FILENAME_W: {
        const windows_image_record *primary = windows_image_at(owner, owner->primary_image);
        const char *name = a ? windows_library_name(owner, a) : primary ? primary->path : NULL;
        if (!name) return invalid_result(owner, out, type, 126, 0, error);
        size_t capacity;
        if (!native_count(integer(args + 2),&capacity,error)) return false;
        if (!capacity) return invalid_result(owner, out, type, 122, 0, error);
        UErrorCode status = U_ZERO_ERROR; int32_t needed = 0;
        u_strFromUTF8(NULL, 0, &needed, name, -1, &status);
        uint16_t *text = calloc((size_t)needed + 1, 2);
        if (!text) return guest_fail(error, QA_ERROR_MEMORY, 0, "encoding Windows module filename");
        status = U_ZERO_ERROR; u_strFromUTF8(text, needed + 1, NULL, name, -1, &status);
        if (U_FAILURE(status)) { free(text); return guest_fail(error, QA_ERROR_FORMAT, 0, "Windows module filename is invalid UTF-8"); }
        size_t copied = (size_t)needed < capacity ? (size_t)needed : capacity - 1, unit = operation == K_FILENAME_W ? 2 : 1;
        bool okay = true;
        for (size_t i = 0; okay && i < copied; ++i) okay = windows_write(owner, b + i * unit, unit, text[i], error);
        free(text); if (!okay || !windows_write(owner, b + copied * unit, unit, 0, error)) return false;
        if ((size_t)needed >= capacity) return invalid_result(owner, out, type, 122, capacity, error);
        result(out, type, (size_t)needed); return true;
    }
    case K_HEAP_CREATE:
        if (!windows_storage(owner, 16, &value, error) || !add(&kernel->heaps, &kernel->heap_count, &kernel->heap_capacity, value, error)) return false;
        result(out, type, value); return true;
    case K_HEAP_ALLOC: {
        if (!a || !contains(kernel->heaps, kernel->heap_count, a)) return invalid_result(owner, out, type, 6, 0, error);
        size_t bytes;
        if (!native_count(integer(args + 2), &bytes, error) || !windows_allocate(owner, bytes, a, &value, error)) return false;
        result(out, type, value); return true;
    }
    case K_HEAP_FREE: {
        qa_error failure = {0}; bool okay = windows_free(owner, integer(args + 2), a, &failure);
        if (!okay && failure.code != QA_ERROR_NOT_FOUND) { if (error) *error = failure; return false; }
        result(out, type, okay); return true;
    }
    case K_HEAP_REALLOC: {
        uint64_t old = integer(args + 2), previous; size_t bytes;
        if (!native_count(integer(args + 3), &bytes, error)) return false;
        if (!a || !old || !windows_allocation_size(owner, old, a, &previous)) return invalid_result(owner, out, type, 87, 0, error);
        if (bytes <= previous) { result(out, type, old); return true; }
        if (b & 16) return true;
        if (!windows_allocate(owner, bytes, a, &value, error)) return false;
        if (value) {
            uint8_t *data = malloc((size_t)previous);
            if (!data) return guest_fail(error, QA_ERROR_MEMORY, 0, "copying Windows heap reallocation");
            bool okay = qa_native_guest_read(owner->guest, old, data, (size_t)previous, error) &&
                qa_native_guest_write(owner->guest, value, (qa_bytes){data, (size_t)previous}, error) && windows_free(owner, old, a, error);
            free(data); if (!okay) return false;
        }
        result(out, type, value); return true;
    }
    case K_HEAP_DESTROY:
        if (!a || a == kernel->process_heap || !contains(kernel->heaps, kernel->heap_count, a)) return true;
        if (!windows_destroy_heap(owner, a, error) || !qa_native_guest_free(owner->guest, a, error)) return false;
        erase(kernel->heaps, &kernel->heap_count, a); result(out, type, 1); return true;
    case K_VIRTUAL_ALLOC: return virtual_allocate(owner, args, out, error);
    case K_VIRTUAL_FREE: return virtual_free(owner, args, out, error);
    case K_TLS_ALLOC:
        for (uint32_t i = 0; i < 1088; ++i) if (!kernel->tls[i]) {
            if (!windows_write(owner, tls_slot(owner, i), width, 0, error)) return false;
            kernel->tls[i] = true; result(out, type, i); return true;
        }
        result(out, type, UINT32_MAX); return true;
    case K_TLS_FREE: case K_TLS_GET: case K_TLS_SET:
        if (a >= 1088 || !kernel->tls[a]) return invalid_result(owner, out, type, 87, 0, error);
        if (operation == K_TLS_FREE) { kernel->tls[a] = false; result(out, type, 1); return true; }
        if (operation == K_TLS_SET) { if (!windows_write(owner, tls_slot(owner, (uint32_t)a), width, b, error)) return false; result(out, type, 1); return true; }
        if (!windows_last_error(owner, 0, error) || !windows_read(owner, tls_slot(owner, (uint32_t)a), width, &value, error)) return false;
        result(out, type, value); return true;
    case K_FLS_ALLOC:
        for (uint32_t i = 0; i < 128; ++i) if (!kernel->fls[i].allocated) {
            kernel->fls[i] = (windows_fls){a, 0, true}; result(out, type, i); return true;
        }
        return invalid_result(owner, out, type, 8, UINT32_MAX, error);
    case K_FLS_GET: case K_FLS_SET: case K_FLS_FREE:
        if (a >= 128 || !kernel->fls[a].allocated) return invalid_result(owner, out, type, 87, 0, error);
        if (operation == K_FLS_GET) { result(out, type, kernel->fls[a].value); return windows_last_error(owner, 0, error); }
        if (operation == K_FLS_SET) { kernel->fls[a].value = b; result(out, type, 1); return true; }
        { windows_fls slot = kernel->fls[a]; kernel->fls[a] = (windows_fls){0};
          if (slot.callback && slot.value) {
              qa_native_value arg = {.type = QA_NATIVE_ADDRESS, .as.address = slot.value}, ignored;
              qa_native_value_type parameter = QA_NATIVE_ADDRESS;
              if (!windows_invoke(owner, slot.callback, &parameter, 1, QA_NATIVE_VOID, &arg, true, &ignored, error)) return false;
          }
          result(out, type, 1); return true; }
    case K_CRITICAL_INIT: case K_CRITICAL_SPIN_INIT: case K_CRITICAL_EX_INIT:
        if (operation == K_CRITICAL_EX_INIT && (integer(args + 2) & ~UINT64_C(0x01000000))) return invalid_result(owner, out, type, 87, 0, error);
        if (!windows_zero(owner, a, width == 4 ? 24 : 40, error) || !windows_write(owner, a + width, 4, UINT32_MAX, error) ||
            !windows_write(owner, a + (width == 4 ? 20 : 32), width, b & 0x7fffffff, error) || !add(&kernel->locks, &kernel->lock_count, &kernel->lock_capacity, a, error)) return false;
        result(out, type, 1); return true;
    case K_CRITICAL_SPIN:
        if (!contains(kernel->locks, kernel->lock_count, a)) return guest_fail(error, QA_ERROR_ARGUMENT, a, "uninitialized Windows critical section");
        if (!windows_read(owner, a + (width == 4 ? 20 : 32), width, &value, error) || !windows_write(owner, a + (width == 4 ? 20 : 32), width, b & 0x7fffffff, error)) return false;
        result(out, type, value); return true;
    case K_CRITICAL_DELETE:
        if (!a) return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows critical section address is null");
        erase(kernel->locks, &kernel->lock_count, a); return true;
    case K_CRITICAL_ENTER: case K_CRITICAL_LEAVE: {
        uint64_t depth, thread;
        if (!contains(kernel->locks, kernel->lock_count, a) || !windows_read(owner, a + width + 4, 4, &depth, error) || !windows_read(owner, a + width + 8, width, &thread, error))
            return guest_fail(error, QA_ERROR_ARGUMENT, a, "uninitialized Windows critical section");
        if (operation == K_CRITICAL_ENTER) {
            if (depth && thread != owner->thread_id) return guest_fail(error, QA_ERROR_UNSUPPORTED, a, "contended Windows critical section requires a thread scheduler");
            return windows_write(owner, a + width, 4, depth, error) && windows_write(owner, a + width + 4, 4, depth + 1, error) && windows_write(owner, a + width + 8, width, owner->thread_id, error);
        }
        if (!depth || thread != owner->thread_id) return guest_fail(error, QA_ERROR_ARGUMENT, a, "unowned Windows critical section");
        return windows_write(owner, a + width, 4, depth - 2, error) && windows_write(owner, a + width + 4, 4, depth - 1, error) &&
            (depth != 1 || windows_write(owner, a + width + 8, width, 0, error));
    }
    case K_INCREMENT: case K_DECREMENT:
        if (!windows_read(owner, a, 4, &value, error)) return false;
        value += operation == K_INCREMENT ? 1 : UINT64_MAX;
        if (!windows_write(owner, a, 4, value, error)) return false;
        result(out, type, value); return true;
    case K_SRW_ACQUIRE: case K_SRW_RELEASE:
        if (!windows_read(owner, a, width, &value, error)) return false;
        if (operation == K_SRW_ACQUIRE && value) return guest_fail(error, QA_ERROR_UNSUPPORTED, a, "contended Windows SRW lock requires a thread scheduler");
        if (operation == K_SRW_RELEASE && !value) return guest_fail(error, QA_ERROR_ARGUMENT, a, "unowned Windows SRW lock");
        return windows_write(owner, a, width, operation == K_SRW_ACQUIRE, error);
    case K_SLIST_INIT: return windows_zero(owner, a, width == 4 ? 8 : 16, error);
    case K_SLIST_FLUSH:
        if (a % (width == 4 ? 8 : 16)) return guest_fail(error, QA_ERROR_ARGUMENT, a, "unaligned Windows SLIST_HEADER");
        if (width == 4) {
            if (!windows_read(owner, a, 4, &value, error)) return false;
            if (value) {
                uint64_t sequence;
                if (!windows_read(owner, a + 6, 2, &sequence, error) || !windows_zero(owner, a, 8, error) || !windows_write(owner, a + 6, 2, sequence + 1, error)) return false;
            }
        } else {
            uint64_t lower, upper;
            if (!windows_read(owner, a, 8, &lower, error) || !windows_read(owner, a + 8, 8, &upper, error)) return false;
            value = upper & ~UINT64_C(15);
            if (value && (!windows_write(owner, a, 8, (lower & ~UINT64_C(65535)) + 65536, error) || !windows_write(owner, a + 8, 8, upper & 15, error))) return false;
        }
        result(out, type, value); return true;
    case K_WAKE: case K_DEBUGGER: return true;
    case K_FEATURE: result(out, type, a == 6 || a == 10); return true;
    case K_FILE_TIME: case K_COUNTER: case K_FREQUENCY: {
        int64_t time;
        if (operation == K_FREQUENCY) time = owner->capabilities.performance_frequency;
        else if (operation == K_COUNTER) { if (!owner->capabilities.performance(owner->capabilities.context, &time, error)) return false; }
        else { if (!owner->capabilities.milliseconds(owner->capabilities.context, &time, error)) return false; }
        uint64_t bits = operation == K_FILE_TIME ? (uint64_t)time * 10000 + UINT64_C(116444736000000000) : (uint64_t)time;
        if (!windows_write(owner, a, 8, bits, error)) return false;
        result(out, type, 1); return true;
    }
    case K_SYSTEM_TIME: case K_LOCAL_TIME: case K_TIMEZONE: {
        int64_t time; guest_windows_calendar date;
        if (!owner->capabilities.milliseconds(owner->capabilities.context, &time, error) ||
            !owner->capabilities.calendar(owner->capabilities.context, time, operation != K_SYSTEM_TIME, &date, error)) return false;
        if (operation == K_TIMEZONE) return windows_zero(owner, a, 172, error) && windows_write(owner, a, 4, (uint32_t)date.timezone_minutes, error);
        int32_t fields[] = {date.year,date.month,date.weekday,date.day,date.hour,date.minute,date.second,date.millisecond};
        for (size_t i = 0; i < 8; ++i) if (!windows_write(owner, a + i * 2, 2, (uint32_t)fields[i], error)) return false;
        return true;
    }
    case K_EXCEPTION: value = kernel->exception_filter; kernel->exception_filter = a; result(out, type, value); return true;
    case K_UNWIND:
        for (size_t i = 0; i < owner->image_count; ++i) {
            const guest_pe_view *image = guest_pe_describe(owner->images[i].image);
            if (a < image->base || a - image->base >= image->image.image_bytes) continue;
            for (size_t j = 0; j < image->function_count; ++j) {
                const guest_pe_unwind *entry = image->unwind + image->functions[j];
                if (a - image->base >= entry->begin && a - image->base < entry->end) {
                    if (!windows_write(owner, b, 8, image->base, error)) return false;
                    result(out, type, image->base + image->directories[3].rva + j * 12); return true;
                }
            }
        }
        return true;
    default: return guest_fail(error, QA_ERROR_ARGUMENT, operation, "invalid Windows kernel descriptor");
    }
}

static bool storage_address(guest_windows *owner, uint64_t address, qa_error *error)
{
    qa_native_allocation_info allocation;
    return (qa_native_guest_allocation(owner->guest,address,&allocation,error) && allocation.base == address &&
        allocation.tag == 0x57494e && allocation.bytes != 0) || guest_fail(error,QA_ERROR_FORMAT,address,"Windows kernel address has no retained source allocation");
}

bool windows_kernel_validate(guest_windows *owner, qa_error *error)
{
    guest_windows_kernel *kernel = owner->kernel; size_t width = owner->target.pointer_bytes;
    if (!storage_address(owner,kernel->command_line_a,error) || !storage_address(owner,kernel->command_line_w,error) ||
        !storage_address(owner,kernel->environment_a,error) || !storage_address(owner,kernel->environment_w,error) ||
        !windows_validate_storage(owner,kernel->process_heap,16,0x57494e,error) ||
        !windows_validate_storage(owner,kernel->dynamic_tls,width*1024,0x57494e,error)) return false;
    for (size_t i = 0; i < kernel->heap_count; ++i)
        if (!windows_validate_storage(owner,kernel->heaps[i],16,0x57494e,error)) return false;
    for (size_t i = 0; i < kernel->lock_count; ++i)
        if (!guest_range(owner->guest,kernel->locks[i],width == 4 ? 24 : 40,QA_NATIVE_GUEST_READ|QA_NATIVE_GUEST_WRITE,error)) return false;
    for (size_t i = 0; i < kernel->reservation_count; ++i) {
        windows_reservation *record = kernel->reservations + i;
        if (record->bytes > SIZE_MAX || !guest_range(owner->guest,record->base,(size_t)record->bytes,0,error) ||
            (record->allocated && !windows_validate_storage(owner,record->base,(size_t)record->bytes,0x575652,error))) return false;
    }
    for (size_t i = 0; i < kernel->handle_count; ++i) {
        windows_file_handle *handle = kernel->handles + i;
        if (handle->stream >= 0 && !windows_validate_storage(owner,handle->handle,
            handle->stream == 0 ? 6 : 7,0x57494e,error)) return false;
        if (handle->stream < 0) {
            guest_runtime_file_view file;
            if (!windows_validate_storage(owner,handle->handle,16,0x57494e,error) ||
                !guest_runtime_resources_find(owner->resources,handle->handle,&file,error) || file.closed) return false;
        } else if (!storage_address(owner,handle->handle,error)) return false;
    }
    for (size_t i = 0; i < guest_runtime_resources_count(owner->resources); ++i) {
        guest_runtime_file_view file;
        if (!guest_runtime_resources_at(owner->resources,i,&file,error)) return false;
        windows_file_handle *handle = handle_at(kernel,file.id);
        if (file.closed ? handle != NULL : handle == NULL)
            return guest_fail(error,QA_ERROR_FORMAT,file.id,"Windows file registry receipt differs from actual open-handle owner");
        if (handle && handle->stream >= 0) return guest_fail(error,QA_ERROR_FORMAT,file.id,"Windows regular file aliases a borrowed process stream");
    }
    return true;
}
