#include "sysv_libc_private.h"
#include "qa/binary.h"

enum platform_operation {
    PL_CLOCK = 1, PL_CLOCK_GETTIME, PL_RANDOM, PL_LOCALTIME, PL_GMTIME
};
enum { PL_ZONE = 40 };
typedef struct platform_export {
    const char *name, *version32, *version64, *older32, *older64;
    uint32_t operation;
    bool time64;
} platform_export;
/* glibc's i386/x86_64 libc.abilist and time/bits/types/struct_timespec.h. */
static const platform_export exports[] = {
    {"clock", "GLIBC_2.0", "GLIBC_2.2.5", NULL, NULL, PL_CLOCK, false},
    {"clock_gettime", "GLIBC_2.17", "GLIBC_2.17", "GLIBC_2.2", "GLIBC_2.2.5", PL_CLOCK_GETTIME, false},
    {"__clock_gettime64", "GLIBC_2.34", NULL, NULL, NULL, PL_CLOCK_GETTIME, true},
    {"getrandom", "GLIBC_2.25", "GLIBC_2.25", NULL, NULL, PL_RANDOM, false},
    {"localtime_r", "GLIBC_2.0", "GLIBC_2.2.5", NULL, NULL, PL_LOCALTIME, false},
    {"gmtime_r", "GLIBC_2.0", "GLIBC_2.2.5", NULL, NULL, PL_GMTIME, false},
    {"__localtime64_r", "GLIBC_2.34", NULL, NULL, NULL, PL_LOCALTIME, true},
    {"__gmtime64_r", "GLIBC_2.34", NULL, NULL, NULL, PL_GMTIME, true}
};
static size_t signature(const guest_sysv_runtime *r, uint32_t operation,
    qa_native_value_type types[3], qa_native_value_type *result)
{
    qa_native_value_type pointer = QA_NATIVE_ADDRESS;
    *result = QA_NATIVE_I32;
    switch (operation) {
    case PL_CLOCK: *result = sysv_signed_type(r); return 0;
    case PL_CLOCK_GETTIME: types[0] = QA_NATIVE_I32; types[1] = pointer; return 2;
    case PL_RANDOM:
        types[0] = pointer; types[1] = sysv_size_type(r); types[2] = QA_NATIVE_U32;
        *result = sysv_signed_type(r); return 3;
    case PL_LOCALTIME: case PL_GMTIME:
        types[0] = types[1] = pointer; *result = pointer; return 2;
    default: return 0;
    }
}
bool sysv_platform_install(guest_sysv_runtime *r, qa_error *error)
{
    bool wide = r->target.pointer_bytes == 8;
    for (size_t i = 0; i < sizeof(exports)/sizeof(exports[0]); ++i) {
        const platform_export *entry = exports + i;
        const char *current = wide ? entry->version64 : entry->version32;
        const char *older = wide ? entry->older64 : entry->older32;
        if (!current) continue;
        const char *versions[] = {current, NULL, older};
        qa_native_value_type types[3], result;
        size_t count = signature(r, entry->operation, types, &result);
        if (!sysv_service_add(r, SYSV_PLATFORM, entry->operation, entry->time64,
            0, 0, "libc.so.6", entry->name, versions, older ? 3 : 2,
            types, count, result, NULL, NULL, error)) return false;
    }
    return true;
}
bool sysv_platform_valid(const sysv_service *service, qa_error *error)
{
    bool wide = service->runtime->target.pointer_bytes == 8;
    bool valid = service->group == SYSV_PLATFORM && !service->b && !service->c &&
        !service->detail && !strcmp(service->library, "libc.so.6");
    const platform_export *entry = NULL;
    for (size_t i = 0; i < sizeof(exports)/sizeof(exports[0]) && valid; ++i) {
        const platform_export *candidate = exports + i;
        const char *version = wide ? candidate->version64 : candidate->version32;
        const char *older = wide ? candidate->older64 : candidate->older32;
        if (version && candidate->operation == service->operation &&
            service->a == (uint64_t)candidate->time64 && !strcmp(candidate->name, service->name) &&
            (!service->version || !strcmp(version, service->version) ||
             (older && !strcmp(older, service->version)))) { entry = candidate; break; }
    }
    qa_native_value_type types[3], result;
    size_t count = signature(service->runtime, service->operation, types, &result);
    valid = valid && entry && service->parameter_count == count && service->result.kind == result;
    for (size_t i = 0; i < count && valid; ++i) valid = service->parameters[i].kind == types[i];
    return valid || sysv_fail(error, QA_ERROR_FORMAT, "Saved System V platform service has a different ABI");
}
static bool result_integer(qa_native_value *out, uint64_t value)
{
    if (out->type == QA_NATIVE_I32) out->as.i32 = (int32_t)(uint32_t)value;
    else out->as.i64 = (int64_t)value;
    return true;
}
static bool clock_call(sysv_service *service, const qa_native_value *arguments,
    qa_native_value *out, qa_error *error)
{
    guest_sysv_runtime *r = service->runtime;
    if (!r->options.bindings.clock)
        return sysv_fail(error, QA_ERROR_UNSUPPORTED, "System V clock has no prepared platform clock capability");
    int64_t seconds;
    int32_t nanoseconds;
    int32_t id = service->operation == PL_CLOCK ? 2 : arguments[0].as.i32;
    if (!r->options.bindings.clock(r->options.bindings.context, id, &seconds, &nanoseconds, error) ||
        !sysv_current(r, error)) return false;
    if (service->operation == PL_CLOCK)
        return result_integer(out, (uint64_t)seconds * UINT64_C(1000000) + (uint32_t)nanoseconds / 1000);
    size_t width = service->a ? 8 : r->target.pointer_bytes;
    if (width == 4 && (seconds < INT32_MIN || seconds > INT32_MAX))
        return sysv_errno(r, 75, error) && result_integer(out, UINT64_MAX);
    uint8_t data[16] = {0};
    if (width == 8) qa_store_u64le(data, (uint64_t)seconds);
    else qa_store_u32le(data, (uint32_t)seconds);
    if (r->target.pointer_bytes == 8) qa_store_u64le(data + width, (uint32_t)nanoseconds);
    else qa_store_u32le(data + width, (uint32_t)nanoseconds);
    size_t bytes = service->a ? 16 : width * 2;
    return sysv_write(r, arguments[1].as.address, data, bytes, error) && result_integer(out, 0);
}
static bool random_call(sysv_service *service, const qa_native_value *arguments,
    qa_native_value *out, qa_error *error)
{
    guest_sysv_runtime *r = service->runtime;
    uint64_t count = sysv_integer(arguments + 1);
    uint32_t flags = arguments[2].as.u32;
    if (!r->options.bindings.random)
        return sysv_fail(error, QA_ERROR_UNSUPPORTED, "System V getrandom has no prepared platform entropy capability");
    qa_error access = {0};
    if (count > SIZE_MAX || (count && !guest_range(r->guest, arguments[0].as.address,
        (size_t)count, QA_NATIVE_GUEST_WRITE, &access))) {
        if (count > SIZE_MAX || access.code == QA_ERROR_ARGUMENT)
            return sysv_errno(r, 14, error) && result_integer(out, UINT64_MAX);
        if (error) *error = access;
        return false;
    }
    uint8_t local[256];
    uint8_t *data = count > sizeof(local) ? malloc((size_t)count) : local;
    if (!data) return sysv_errno(r, 12, error) && result_integer(out, UINT64_MAX);
    size_t completed = 0;
    int32_t native_error = 0;
    bool okay = r->options.bindings.random(r->options.bindings.context, data,
        (size_t)count, flags, &completed, &native_error, error) && sysv_current(r, error);
    if (okay && completed) okay = sysv_write(r, arguments[0].as.address, data, completed, error);
    if (data != local) free(data);
    if (!okay) return false;
    return (!native_error || sysv_errno(r, native_error, error)) &&
        result_integer(out, native_error ? UINT64_MAX : completed);
}
static bool calendar_call(sysv_service *service, const qa_native_value *arguments,
    qa_native_value *out, qa_error *error)
{
    guest_sysv_runtime *r = service->runtime;
    if (!r->options.bindings.calendar)
        return sysv_fail(error, QA_ERROR_UNSUPPORTED, "System V calendar has no prepared platform calendar capability");
    size_t width = service->a ? 8 : r->target.pointer_bytes;
    uint64_t raw;
    if (!sysv_unsigned(r, arguments[0].as.address, width, &raw, error)) return false;
    int64_t seconds = width == 4 ? (int64_t)(int32_t)(uint32_t)raw : (int64_t)raw;
    if (seconds < INT64_MIN/1000 || seconds > INT64_MAX/1000)
        return sysv_errno(r, 75, error) && (out->as.address = 0, true);
    qa_platform_calendar_fields date;
    if (!r->options.bindings.calendar(r->options.bindings.context, seconds * 1000,
        service->operation == PL_LOCALTIME, &date, error) || !sysv_current(r, error)) return false;
    uint8_t data[56] = {0};
    const int32_t fields[] = {date.second, date.minute, date.hour, date.day,
        date.month - 1, date.year - 1900, date.weekday, date.yearday, date.daylight};
    for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i)
        qa_store_u32le(data + i * 4, (uint32_t)fields[i]);
    int64_t offset = -date.timezone_seconds;
    size_t offset_at = r->target.pointer_bytes == 8 ? 40 : 36;
    if (r->target.pointer_bytes == 8) qa_store_u64le(data + offset_at, (uint64_t)offset);
    else qa_store_u32le(data + offset_at, (uint32_t)offset);
    sysv_object *cached = sysv_object_find(r, PL_ZONE, date.timezone);
    uint64_t zone = cached ? cached->address : 0;
    if (!cached && (!sysv_string_new(r, date.timezone, &zone, error) ||
        !sysv_object_add(r, PL_ZONE, date.timezone, zone, strlen(date.timezone) + 1,
            NULL, 0, error))) return false;
    if (r->target.pointer_bytes == 8) qa_store_u64le(data + offset_at + 8, zone);
    else qa_store_u32le(data + offset_at + 4, (uint32_t)zone);
    if (!sysv_write(r, arguments[1].as.address, data, r->target.pointer_bytes == 8 ? 56 : 44, error)) return false;
    out->as.address = arguments[1].as.address;
    return true;
}
bool sysv_platform_call(sysv_service *service, const qa_native_value *arguments,
    qa_native_value *out, qa_error *error)
{
    out->type = service->result.kind;
    switch (service->operation) {
    case PL_CLOCK: case PL_CLOCK_GETTIME: return clock_call(service, arguments, out, error);
    case PL_RANDOM: return random_call(service, arguments, out, error);
    case PL_LOCALTIME: case PL_GMTIME: return calendar_call(service, arguments, out, error);
    default: return sysv_fail(error, QA_ERROR_FORMAT, "Unknown System V platform operation");
    }
}
