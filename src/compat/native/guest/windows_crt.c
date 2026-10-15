#include "internal.h"
#include "windows_crt.h"
#include "memory_string.h"
#include "scan.h"
#include "sysv_libc_format.h"
#include "qa/text.h"
#include <math.h>
#include <fenv.h>
#include <stdio.h>

enum crt_operation {
    C_TICKS = 1, C_MALLOC, C_CALLOC, C_FREE, C_NEW_HANDLER, C_COPY, C_FILL, C_COMPARE,
    C_FIND, C_LENGTH, C_STRCMP, C_STRNCMP, C_STRCHR, C_STRSTR, C_ARGV, C_ENVIRONMENT,
    C_ONEXIT_INIT, C_ONEXIT_REGISTER, C_ATEXIT, C_ONEXIT_EXECUTE, C_CEXIT,
    C_INITTERM, C_INITTERM_E, C_TYPEINFO, C_QSORT, C_ACOS, C_SIN, C_CEIL, C_COS,
    C_TRUNC, C_LOG2, C_FLOOR, C_SQRT, C_TAN, C_ATAN2, C_FMOD, C_POW, C_NEXTAFTER,
    C_MODF, C_DCLASS, C_FCLASS, C_SIGN, C_ERRNO, C_STRTOUL, C_ATOI, C_ATOLL, C_ATOF,
    C_TIME, C_LOCALTIME, C_LOCALE, C_PRINTF, C_SCANF, C_PRINTF_S
};
typedef struct crt_descriptor {
    const char *family, *name; uint32_t operation; qa_native_value_type result;
    size_t count; qa_native_value_type parameters[7];
} crt_descriptor;
#define P QA_NATIVE_ADDRESS
#define I QA_NATIVE_I32
#define U QA_NATIVE_U32
#define Z QA_NATIVE_BYTES
#define V QA_NATIVE_VOID
#define F QA_NATIVE_F32
#define R QA_NATIVE_F64
#define D(f,n,o,r,c,...) {f,n,o,r,c,{__VA_ARGS__}}
static const crt_descriptor descriptors[] = {
    D("msvcp","_Xtime_get_ticks",C_TICKS,QA_NATIVE_I64,0,0),
    D("heap","malloc",C_MALLOC,P,1,Z), D("heap","calloc",C_CALLOC,P,2,Z,Z), D("heap","free",C_FREE,V,1,P), D("heap","_callnewh",C_NEW_HANDLER,I,1,Z),
    D("vcruntime","memcpy",C_COPY,P,3,P,P,Z), D("vcruntime","memmove",C_COPY,P,3,P,P,Z), D("vcruntime","memset",C_FILL,P,3,P,I,Z),
    D("vcruntime","memcmp",C_COMPARE,I,3,P,P,Z), D("vcruntime","memchr",C_FIND,P,3,P,I,Z),
    D("string","strlen",C_LENGTH,Z,1,P), D("string","strcmp",C_STRCMP,I,2,P,P), D("string","strncmp",C_STRNCMP,I,3,P,P,Z),
    D("vcruntime","strchr",C_STRCHR,P,2,P,I), D("vcruntime","strstr",C_STRSTR,P,2,P,P),
    D("runtime","_configure_narrow_argv",C_ARGV,I,1,I), D("runtime","_initialize_narrow_environment",C_ENVIRONMENT,I,0,0),
    D("runtime","_initialize_onexit_table",C_ONEXIT_INIT,I,1,P), D("runtime","_register_onexit_function",C_ONEXIT_REGISTER,I,2,P,P),
    D("runtime","_crt_atexit",C_ATEXIT,I,1,P), D("runtime","_execute_onexit_table",C_ONEXIT_EXECUTE,I,1,P), D("runtime","_cexit",C_CEXIT,V,0,0),
    D("runtime","_initterm",C_INITTERM,V,2,P,P), D("runtime","_initterm_e",C_INITTERM_E,I,2,P,P),
    D("vcruntime","__std_type_info_destroy_list",C_TYPEINFO,V,1,P), D("utility","qsort",C_QSORT,V,4,P,Z,Z,P),
    D("math","acosf",C_ACOS,F,1,F), D("math","sinf",C_SIN,F,1,F), D("math","ceilf",C_CEIL,F,1,F),
    D("math","cosf",C_COS,F,1,F), D("math","truncf",C_TRUNC,F,1,F), D("math","log2f",C_LOG2,F,1,F),
    D("math","floorf",C_FLOOR,F,1,F), D("math","sqrtf",C_SQRT,F,1,F), D("math","tanf",C_TAN,F,1,F),
    D("math","atan2f",C_ATAN2,F,2,F,F), D("math","fmodf",C_FMOD,F,2,F,F), D("math","pow",C_POW,R,2,R,R),
    D("math","nextafterf",C_NEXTAFTER,F,2,F,F), D("math","modf",C_MODF,R,2,R,P),
    D("math","_dclass",C_DCLASS,QA_NATIVE_I16,1,R), D("math","_fdclass",C_FCLASS,QA_NATIVE_I16,1,F), D("math","_dsign",C_SIGN,QA_NATIVE_I16,1,R),
    D("runtime","_errno",C_ERRNO,P,0,0), D("convert","strtoul",C_STRTOUL,U,3,P,P,I),
    D("convert","atoi",C_ATOI,I,1,P), D("convert","atoll",C_ATOLL,QA_NATIVE_I64,1,P), D("convert","atof",C_ATOF,R,1,P),
    D("time","_time64",C_TIME,QA_NATIVE_I64,1,P), D("time","_localtime64",C_LOCALTIME,P,1,P), D("locale","localeconv",C_LOCALE,P,0,0),
    D("stdio","__stdio_common_vsprintf",C_PRINTF,I,6,QA_NATIVE_U64,P,Z,P,P,P),
    D("stdio","__stdio_common_vsscanf",C_SCANF,I,6,QA_NATIVE_U64,P,Z,P,P,P),
    D("stdio","fopen",WST_OPEN,P,2,P,P),
    D("stdio","fclose",WST_CLOSE,I,1,P),
    D("stdio","fread",WST_READ,Z,4,P,Z,Z,P),
    D("stdio","fwrite",WST_WRITE,Z,4,P,Z,Z,P),
    D("stdio","fseek",WST_SEEK,I,3,P,I,I),
    D("stdio","ftell",WST_TELL,I,1,P),
    D("stdio","_fseeki64",WST_SEEK64,I,3,P,QA_NATIVE_I64,I),
    D("stdio","_ftelli64",WST_TELL64,QA_NATIVE_I64,1,P),
    D("stdio","fflush",WST_FLUSH,I,1,P),
    D("stdio","feof",WST_EOF,I,1,P),
    D("stdio","ferror",WST_ERROR,I,1,P),
    D("stdio","clearerr",WST_CLEAR,V,1,P),
    D("stdio","rewind",WST_REWIND,V,1,P),
    D("stdio","fgetc",WST_GET,I,1,P),
    D("stdio","getc",WST_GET,I,1,P),
    D("stdio","_filbuf",WST_GET,I,1,P),
    D("stdio","fputc",WST_PUT,I,2,I,P),
    D("stdio","putc",WST_PUT,I,2,I,P),
    D("stdio","_flsbuf",WST_PUT,I,2,I,P),
    D("stdio","ungetc",WST_UNGET,I,2,I,P),
    D("stdio","_fileno",WST_FILENO,I,1,P),
    D("stdio","__stdio_common_vsnprintf_s",C_PRINTF_S,I,7,QA_NATIVE_U64,P,Z,Z,P,P,P)
};

guest_readonly_kind windows_crt_readonly_kind(uint32_t operation)
{
    return operation == C_COMPARE ? GUEST_READONLY_COMPARE :
        operation == C_LENGTH ? GUEST_READONLY_LENGTH : GUEST_READONLY_NONE;
}

#undef D
#undef P
#undef I
#undef U
#undef Z
#undef V
#undef F
#undef R

static uint64_t integer(const qa_native_value *value)
{
    switch (value->type) {
    case QA_NATIVE_ADDRESS: return value->as.address; case QA_NATIVE_U32: return value->as.u32;
    case QA_NATIVE_I32: return (uint64_t)(int64_t)value->as.i32; case QA_NATIVE_I64: return (uint64_t)value->as.i64;
    default: return value->as.u64;
    }
}

static void result(qa_native_value *out, qa_native_value_type type, uint64_t value)
{
    *out = (qa_native_value){.type = type};
    switch (type) {
    case QA_NATIVE_ADDRESS: out->as.address = value; break;
    case QA_NATIVE_U32: out->as.u32 = (uint32_t)value; break;
    case QA_NATIVE_I16: { uint16_t bits = (uint16_t)value; memcpy(&out->as.i16, &bits, 2); break; }
    case QA_NATIVE_I32: { uint32_t bits = (uint32_t)value; memcpy(&out->as.i32, &bits, 4); break; }
    case QA_NATIVE_U64: out->as.u64 = value; break;
    case QA_NATIVE_I64: memcpy(&out->as.i64, &value, 8); break;
    default: break;
    }
}

bool windows_crt_descriptors(guest_windows *owner, bool bind, qa_error *error)
{
    for (size_t i = 0; i < sizeof(descriptors) / sizeof(*descriptors); ++i) {
        const crt_descriptor *entry = descriptors + i; qa_native_value_type types[7];
        for (size_t j = 0; j < entry->count; ++j) types[j] = entry->parameters[j] == QA_NATIVE_BYTES ?
            owner->target.pointer_bytes == 4 ? QA_NATIVE_U32 : QA_NATIVE_U64 : entry->parameters[j];
        qa_native_value_type return_type = entry->result == QA_NATIVE_BYTES ? owner->target.pointer_bytes == 4 ? QA_NATIVE_U32 : QA_NATIVE_U64 : entry->result;
        char library[96];
        if (!strcmp(entry->family, "msvcp")) memcpy(library, "msvcp140.dll", sizeof("msvcp140.dll"));
        else if (!strcmp(entry->family, "vcruntime")) memcpy(library, "vcruntime140.dll", sizeof("vcruntime140.dll"));
        else snprintf(library, sizeof(library), "api-ms-win-crt-%s-l1-1-0.dll", entry->family);
        uint64_t id = UINT64_C(0x57494e0200000000) + i * 3 + 1;
        if (!windows_service_add(owner, id, 2, entry->operation, library, entry->name, types,
            entry->count, return_type, QA_NATIVE_CALL_DEFAULT, bind, error)) return false;
        if (strcmp(entry->family, "msvcp") &&
            (!windows_service_add(owner, id + 1, 2, entry->operation, "ucrtbase.dll", entry->name, types,
                entry->count, return_type, QA_NATIVE_CALL_DEFAULT, bind, error) ||
             !windows_service_add(owner, id + 2, 2, entry->operation, "msvcrt.dll", entry->name, types,
                entry->count, return_type, QA_NATIVE_CALL_DEFAULT, bind, error))) return false;
    }
    return true;
}

bool windows_crt_initialize(guest_windows *owner, qa_error *error)
{
    guest_windows_crt *crt = owner->crt; size_t width = owner->target.pointer_bytes;
    crt->next_file = 3;
    crt->has_file_opener = owner->capabilities.open_file != NULL;
    const uint16_t decimal[] = {'.'};
    if (!windows_storage(owner, width * 3, &crt->onexit, error) ||
        !windows_storage(owner, 4, &crt->error_number, error) || !windows_storage(owner, 36, &crt->time_buffer, error) ||
        !windows_storage(owner, width * 10 + 16, &crt->locale, error) ||
        !windows_store_string(owner, NULL, 0, false, &crt->empty, error) ||
        !windows_store_string(owner, decimal, 1, false, &crt->decimal, error)) return false;
    for (size_t i = 0; i < 10; ++i)
        if (!windows_write(owner, crt->locale + i * width, width, i ? crt->empty : crt->decimal, error)) return false;
    uint8_t fields[14]; memset(fields, 127, sizeof(fields));
    return qa_native_guest_write(owner->guest, crt->locale + width * 10, (qa_bytes){fields, sizeof(fields)}, error);
}

static bool text(guest_windows *owner, uint64_t address, qa_arena *scratch,
    char **out, qa_error *error)
{
    qa_buffer value = {0}; bool terminated;
    if (!guest_string_read(owner->guest, address, 1, 1048576, scratch,
        &value, &terminated, error)) return false;
    if (!terminated)
        return guest_fail(error, QA_ERROR_FORMAT, address, "Windows string exceeds source read limit");
    *out = (char *)value.data; return true;
}

static bool extent(uint64_t value, size_t *out, qa_error *error)
{
    if (value > 0x10000000) {
        guest_fail(error, QA_ERROR_ARGUMENT, value, "CRT byte count exceeds source limit");
        return false;
    }
    *out = (size_t)value; return true;
}

static bool move(guest_windows *owner, uint64_t destination, uint64_t source, size_t bytes, qa_error *error)
{
    if (!bytes) return true;
    qa_arena scratch; qa_arena_init_pool(&scratch, &owner->guest->scratch_pages);
    uint8_t *data = qa_arena_alloc(&scratch, bytes, 1, error);
    if (!data) { qa_arena_destroy(&scratch); return false; }
    bool okay = qa_native_guest_read(owner->guest, source, data, bytes, error) &&
        qa_native_guest_write(owner->guest, destination, (qa_bytes){data, bytes}, error);
    qa_arena_destroy(&scratch); return okay;
}

static bool onexit_register(guest_windows *owner, uint64_t table, uint64_t callback,
    qa_native_value *out, qa_error *error)
{
    size_t width = owner->target.pointer_bytes; uint64_t begin, end, capacity;
    if (!table || !callback || !windows_read(owner, table, width, &begin, error) ||
        !windows_read(owner, table + width, width, &end, error) || !windows_read(owner, table + width * 2, width, &capacity, error)) return false;
    if (!begin || !end || !capacity || end == capacity) {
        uint64_t existing = !begin || !end ? 0 : end - begin;
        if ((begin && end < begin) || existing > 0x8000000) return guest_fail(error, QA_ERROR_ARGUMENT, table, "invalid CRT onexit growth extent");
        size_t bytes = (size_t)existing * 2; if (bytes < width * 32) bytes = width * 32;
        uint64_t next;
        if (!windows_allocate(owner, bytes, 0, &next, error)) return false;
        if (!next) { result(out, QA_NATIVE_I32, UINT32_MAX); return true; }
        if (begin && (!move(owner, next, begin, (size_t)existing, error) || !windows_free(owner, begin, 0, error))) return false;
        begin = next; end = next + existing; capacity = next + bytes;
        if (!windows_write(owner, table, width, begin, error) || !windows_write(owner, table + width * 2, width, capacity, error)) return false;
    }
    if (!windows_write(owner, end, width, callback, error) || !windows_write(owner, table + width, width, end + width, error)) return false;
    result(out, QA_NATIVE_I32, 0); return true;
}

static bool onexit_execute(guest_windows *owner, uint64_t table, qa_error *error)
{
    size_t width = owner->target.pointer_bytes; uint64_t begin, end;
    if (!windows_read(owner, table, width, &begin, error) || !windows_read(owner, table + width, width, &end, error)) return false;
    if (begin && end) {
        if (end < begin || (end - begin) % width) return guest_fail(error, QA_ERROR_ARGUMENT, table, "invalid CRT onexit range");
        for (uint64_t at = end; at > begin; ) {
            at -= width; uint64_t callback; qa_native_value ignored;
            if (!windows_read(owner, at, width, &callback, error) || !windows_write(owner, at, width, 0, error)) return false;
            if (callback && !windows_invoke(owner, callback, NULL, 0, QA_NATIVE_VOID, NULL, false, &ignored, error)) return false;
        }
        if (!windows_free(owner, begin, 0, error)) return false;
    }
    return windows_zero(owner, table, width * 3, error);
}

bool windows_crt_finalize_image(guest_windows *owner, uint64_t base, uint64_t bytes, qa_error *error)
{
    if (!bytes || base > UINT64_MAX - bytes)
        return guest_fail(error, QA_ERROR_ARGUMENT, base, "CRT module retirement has no actual image range");
    size_t width = owner->target.pointer_bytes;
    uint64_t begin, end, table = owner->crt->onexit;
    if (!windows_read(owner, table, width, &begin, error) ||
        !windows_read(owner, table + width, width, &end, error)) return false;
    if (!begin || !end) return true;
    if (end < begin || (end - begin) % width)
        return guest_fail(error, QA_ERROR_ARGUMENT, table, "CRT module retirement has an invalid onexit table");
    for (uint64_t at = end; at > begin;) {
        at -= width; uint64_t callback; qa_native_value result;
        if (!windows_read(owner, at, width, &callback, error)) return false;
        if (callback < base || callback >= base + bytes) continue;
        if (!windows_write(owner, at, width, 0, error) ||
            !windows_invoke(owner, callback, NULL, 0, QA_NATIVE_VOID, NULL, false, &result, error)) return false;
    }
    return true;
}

typedef struct sort_callback {
    guest_windows *owner;
    uint64_t comparator;
} sort_callback;

static bool sort_compare(void *context, uint64_t left, uint64_t right, int32_t *out, qa_error *error)
{
    sort_callback *sort = context;
    const qa_native_value_type types[] = {QA_NATIVE_ADDRESS, QA_NATIVE_ADDRESS};
    const qa_native_value args[] = {{.type = QA_NATIVE_ADDRESS, .as.address = left},
        {.type = QA_NATIVE_ADDRESS, .as.address = right}};
    qa_native_value result;
    if (!windows_invoke(sort->owner, sort->comparator, types, 2, QA_NATIVE_I32, args, false, &result, error)) return false;
    *out = result.as.i32; return true;
}

static bool sort_transfer(void *context, uint64_t address, void *bytes, size_t width,
    bool write, qa_error *error)
{
    sort_callback *sort = context;
    return write ? qa_native_guest_write(sort->owner->guest, address, (qa_bytes){bytes, width}, error) :
        qa_native_guest_read(sort->owner->guest, address, bytes, width, error);
}

static bool printf_service(guest_windows *, const qa_native_value *, bool, qa_arena *, qa_native_value *, qa_error *);
static bool scanf_service(guest_windows *, const qa_native_value *, qa_native_value *, qa_error *);

static bool crt_invoke(windows_service *service, const qa_native_value *args,
    size_t count, qa_arena *scratch, qa_native_value *out, qa_error *error)
{
    guest_windows *owner = service->owner; guest_windows_crt *crt = owner->crt;
    uint32_t operation = service->operation; size_t width = owner->target.pointer_bytes;
    uint64_t a = count ? integer(args) : 0, b = count > 1 ? integer(args + 1) : 0, value = 0;
    qa_native_value_type type = service->function.signature.result.kind; result(out, type, 0);
    if (operation >= WST_OPEN && operation <= WST_FILENO)
        return windows_stdio_invoke(service, args, count, out, error);
    if (operation >= C_ACOS && operation <= C_SIGN) {
        double left = args[0].type == QA_NATIVE_F32 ? args[0].as.f32 : args[0].as.f64;
        double right = count > 1 && args[1].type == QA_NATIVE_F32 ? args[1].as.f32 : count > 1 && args[1].type == QA_NATIVE_F64 ? args[1].as.f64 : 0;
        if (operation >= C_DCLASS) {
            value = operation == C_SIGN ? (left < 0 || (left == 0 && signbit(left))) ? 0x8000 : 0 : isnan(left) ? 2 : !isfinite(left) ? 1 : left == 0 ? 0 :
                fabs(left) < (operation == C_FCLASS ? 0x1p-126 : 0x1p-1022) ? (uint64_t)-2 : (uint64_t)-1;
            result(out, type, value); return true;
        }
        fenv_t saved; bool held = feholdexcept(&saved) == 0;
        if (!held || fesetround(FE_TONEAREST) != 0) {
            if (held) fesetenv(&saved);
            return guest_fail(error,QA_ERROR_UNSUPPORTED,operation,"host cannot provide Windows Math rounding");
        }
        double number = 0; bool okay = true;
        switch (operation) {
        case C_ACOS: number = acos(left); break; case C_SIN: number = sin(left); break;
        case C_CEIL: number = ceil(left); break; case C_COS: number = cos(left); break;
        case C_TRUNC: number = trunc(left); break; case C_LOG2: number = log2(left); break;
        case C_FLOOR: number = floor(left); break; case C_SQRT: number = sqrt(left); break;
        case C_TAN: number = tan(left); break; case C_ATAN2: number = atan2(left, right); break;
        case C_FMOD: number = fmod(left, right); break;
        case C_POW: number = fabs(left) == 1 && isinf(right) ? NAN : pow(left, right); break;
        case C_NEXTAFTER: {
            float x = (float)left, y = (float)right;
            if (isnan(x) || isnan(y)) number = x + y;
            else if (x == y) number = y;
            else {
                uint32_t bits; memcpy(&bits, &x, 4);
                if (x == 0) bits = y < 0 ? 0x80000001u : 1;
                else bits += (y > x) == (x > 0) ? 1u : UINT32_MAX;
                memcpy(&x, &bits, 4); number = x;
            }
            break;
        }
        case C_MODF: {
            double whole = trunc(left); uint64_t bits; memcpy(&bits, &whole, 8);
            if (!windows_write(owner, b, 8, bits, error)) { okay = false; break; }
            number = isfinite(left) ? left - whole : isnan(left) ? NAN : 0;
            if (number == 0 && signbit(left)) number = -0.0;
            break;
        }
        default: okay = guest_fail(error,QA_ERROR_ARGUMENT,operation,"invalid Windows Math operation"); break;
        }
        if (okay) { if (type == QA_NATIVE_F32) out->as.f32 = (float)number; else out->as.f64 = number; }
        if (fesetenv(&saved) != 0 && okay)
            return guest_fail(error,QA_ERROR_UNSUPPORTED,operation,"Windows Math environment restore failed");
        return okay;
    }
    switch (operation) {
    case C_TICKS: case C_TIME: {
        int64_t milliseconds;
        if (!owner->capabilities.milliseconds(owner->capabilities.context, &milliseconds, error)) return false;
        int64_t seconds = milliseconds / 1000; if (milliseconds < 0 && milliseconds % 1000) --seconds;
        value = operation == C_TICKS ? (uint64_t)milliseconds * 10000 : (uint64_t)seconds;
        if (operation == C_TIME && a && !windows_write(owner, a, 8, value, error)) return false;
        result(out, type, value); return true;
    }
    case C_MALLOC: case C_CALLOC: {
        uint64_t bytes = a;
        size_t checked;
        if (operation == C_MALLOC && !extent(a,&checked,error)) return false;
        if (operation == C_CALLOC) {
            if (b && a > UINT64_MAX / b) return true;
            bytes *= b; if (bytes > 0x10000000) return true;
        }
        if (bytes > SIZE_MAX || !windows_allocate(owner, (size_t)bytes, 0, &value, error)) return false;
        result(out, type, value); return true;
    }
    case C_FREE: return windows_free(owner, a, 0, error);
    case C_NEW_HANDLER: case C_ENVIRONMENT: return true;
    case C_COPY: case C_FILL: case C_COMPARE: case C_FIND: {
        size_t bytes;
        if (!extent(integer(args + 2), &bytes, error)) return false;
        if (operation == C_COPY) { if (!move(owner, a, b, bytes, error)) return false; result(out, type, a); return true; }
        if (operation == C_FILL) {
            if (bytes) {
                if (!guest_range(owner->guest,a,bytes,QA_NATIVE_GUEST_WRITE,error)) return false;
                uint8_t *data = qa_arena_alloc(scratch, bytes, 1, error);
                if (!data) return false;
                memset(data, (uint8_t)b, bytes);
                if (!qa_native_guest_write(owner->guest, a, (qa_bytes){data, bytes}, error)) return false;
            }
            result(out, type, a); return true;
        }
        if (bytes && (!guest_range(owner->guest,a,bytes,QA_NATIVE_GUEST_READ,error) ||
            (operation == C_COMPARE && !guest_range(owner->guest,b,bytes,QA_NATIVE_GUEST_READ,error)))) return false;
        if (!bytes) return true;
        uint8_t *first = qa_arena_alloc(scratch, bytes, 1, error);
        if (!first || !qa_native_guest_read(owner->guest, a, first, bytes, error)) return false;
        uint8_t *second = operation == C_COMPARE ? qa_arena_alloc(scratch, bytes, 1, error) : NULL;
        if (operation == C_COMPARE && (!second || !qa_native_guest_read(owner->guest, b, second, bytes, error))) return false;
        if (operation == C_COMPARE) {
            (void)guest_readonly_apply(GUEST_READONLY_COMPARE, (qa_bytes){first, bytes},
                (qa_bytes){second, bytes}, bytes, &value);
            result(out, type, value); return true;
        }
        for (size_t i = 0; i < bytes; ++i) {
            if (first[i] == (b & 255)) { result(out, type, a + i); return true; }
        }
        return true;
    }
    case C_LENGTH: {
        qa_buffer source = {0}; bool terminated;
        if (!guest_string_read(owner->guest, a, 1, 1048576, scratch, &source, &terminated, error)) return false;
        if (!terminated) return guest_fail(error, QA_ERROR_FORMAT, a, "Windows string exceeds source read limit");
        (void)guest_readonly_apply(GUEST_READONLY_LENGTH, (qa_bytes){source.data, source.size + 1},
            (qa_bytes){0}, 0, &value);
        result(out, type, value); return true;
    }
    case C_STRCHR: case C_STRSTR: {
        char *source = NULL, *search = NULL;
        if (!text(owner, a, scratch, &source, error)) return false;
        if (operation == C_STRCHR) { char *found = strchr(source, (uint8_t)b); value = found ? a + (size_t)(found - source) : 0; }
        else {
            if (!text(owner, b, scratch, &search, error)) return false;
            char *found = strstr(source, search); value = found ? a + (size_t)(found - source) : 0;
        }
        result(out, type, value); return true;
    }
    case C_STRCMP: case C_STRNCMP: {
        size_t maximum = 1048576;
        if (!a || !b) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "CRT string comparison requires actual pointers");
        if (operation == C_STRNCMP && !extent(integer(args + 2), &maximum, error)) return false;
        for (size_t i = 0; i < maximum; ++i) {
            uint64_t left, right;
            if (!windows_read(owner, a + i, 1, &left, error) || !windows_read(owner, b + i, 1, &right, error)) return false;
            if (left != right) { result(out, type, left - right); return true; }
            if (!left) return true;
        }
        return true;
    }
    case C_ARGV:
        return (args[0].as.i32 >= 0 && args[0].as.i32 <= 2) || guest_fail(error, QA_ERROR_UNSUPPORTED, a, "invalid CRT narrow argument mode");
    case C_ONEXIT_INIT: return windows_zero(owner, a, width * 3, error);
    case C_ONEXIT_REGISTER: return onexit_register(owner, a, b, out, error);
    case C_ATEXIT: return onexit_register(owner, crt->onexit, a, out, error);
    case C_ONEXIT_EXECUTE: return onexit_execute(owner, a, error);
    case C_CEXIT: return onexit_execute(owner, crt->onexit, error);
    case C_INITTERM: case C_INITTERM_E:
        if (!a || !b || b < a || (b - a) % width || b - a > 1048576) return guest_fail(error, QA_ERROR_ARGUMENT, a, "invalid CRT initializer range");
        for (uint64_t at = a; at < b; at += width) {
            uint64_t callback;
            if (!windows_read(owner, at, width, &callback, error)) return false;
            if (callback) {
                qa_native_value returned;
                if (!windows_invoke(owner, callback, NULL, 0, type, NULL, false, &returned, error)) return false;
                if (type == QA_NATIVE_I32 && returned.as.i32) { *out = returned; return true; }
            }
        }
        return true;
    case C_TYPEINFO:
        if (!guest_range(owner->guest,a,width == 4 ? 8 : 16,QA_NATIVE_GUEST_READ,error)) return false;
        for (size_t i = 0; i < (width == 4 ? 8 : 16); ++i) {
            if (!windows_read(owner, a + i, 1, &value, error)) return false;
            if (value) return guest_fail(error, QA_ERROR_UNSUPPORTED, a, "populated CRT type-name SLIST destruction is not implemented");
        }
        return true;
    case C_QSORT: {
        size_t length, bytes;
        if (!extent(b, &length, error) || !extent(integer(args + 2), &bytes, error)) return false;
        if (length < 2) return true;
        if (!bytes || length > 0x10000000 / bytes) return guest_fail(error, QA_ERROR_ARGUMENT, a, "invalid CRT qsort extent");
        if (!guest_range(owner->guest, a, length * bytes, QA_NATIVE_GUEST_WRITE, error)) return false;
        sort_callback sort = {owner, integer(args + 3)};
        return guest_sort(a, length, bytes, scratch, sort_compare, sort_transfer, &sort, error);
    }
    case C_ERRNO: result(out, type, crt->error_number); return true;
    case C_STRTOUL: {
        char *source = NULL;
        if (!text(owner, a, scratch, &source, error)) return false;
        int base = args[2].as.i32; size_t at = 0, stop = 0; uint32_t number = 0; bool overflowed = false;
        if (base != 0 && (base < 2 || base > 36)) { if (!windows_write(owner, crt->error_number, 4, 22, error)) return false; }
        else {
            while (source[at] && strchr(" \t\n\r\f\v", source[at])) ++at;
            bool negative = source[at] == '-'; if (negative || source[at] == '+') ++at;
            unsigned char c = (unsigned char)source[at + (source[at] ? 1 : 0)];
            int following = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : c >= 'a' && c <= 'z' ? c - 'a' + 10 : 99;
            if ((base == 0 || base == 16) && source[at] == '0' && (source[at + 1] == 'x' || source[at + 1] == 'X')) {
                c = (unsigned char)source[at + 2]; following = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : c >= 'a' && c <= 'z' ? c - 'a' + 10 : 99;
                if (following < 16) { base = 16; at += 2; }
            }
            if (!base) base = source[at] == '0' ? 8 : 10;
            size_t first = at;
            while (source[at]) {
                c = (unsigned char)source[at]; int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : c >= 'a' && c <= 'z' ? c - 'a' + 10 : 99;
                if (digit >= base) break;
                if (number > (UINT32_MAX - (uint32_t)digit) / (uint32_t)base) { overflowed = true; number = UINT32_MAX; }
                else number = number * (uint32_t)base + (uint32_t)digit;
                ++at;
            }
            if (at != first) { stop = at; if (negative && !overflowed) number = 0u - number; }
            if (overflowed && !windows_write(owner, crt->error_number, 4, 34, error)) return false;
        }
        if (b && !windows_write(owner, b, width, a + stop, error)) return false;
        result(out, type, number); return true;
    }
    case C_ATOI: case C_ATOLL: case C_ATOF: {
        char *source = NULL;
        if (!text(owner, a, scratch, &source, error)) return false;
        if (operation == C_ATOF) {
            char *start = source; while (*start && qa_unicode_whitespace((uint8_t)*start)) ++start;
            char *at = start; if (*at == '+' || *at == '-') ++at;
            if (!strncmp(at,"Infinity",8)) out->as.f64 = *start == '-' ? -INFINITY : INFINITY;
            else {
                size_t digits = 0;
                while (*at >= '0' && *at <= '9') { ++at; ++digits; }
                if (*at == '.') { ++at; while (*at >= '0' && *at <= '9') { ++at; ++digits; } }
                if (digits && (*at == 'e' || *at == 'E')) {
                    char *exponent = at++, *first;
                    if (*at == '+' || *at == '-') ++at;
                    first = at; while (*at >= '0' && *at <= '9') ++at;
                    if (at == first) at = exponent;
                }
                out->as.f64 = 0;
                if (digits) {
                    *at = 0;
                    fenv_t saved; bool held = feholdexcept(&saved) == 0;
                    if (!held || fesetround(FE_TONEAREST) != 0) {
                        if (held) fesetenv(&saved);
                        return guest_fail(error,QA_ERROR_UNSUPPORTED,0,"host cannot provide nearest decimal conversion");
                    }
                    bool okay = qa_parse_atof(start,&out->as.f64,error);
                    if (fesetenv(&saved) != 0) okay = guest_fail(error,QA_ERROR_UNSUPPORTED,0,"host decimal conversion environment restore failed");
                    if (!okay) return false;
                }
            }
        } else {
            size_t at = 0; while (source[at] && qa_unicode_whitespace((uint8_t)source[at])) ++at;
            bool negative = source[at] == '-'; if (negative || source[at] == '+') ++at;
            while (source[at] >= '0' && source[at] <= '9') { value = value * 10 + (uint32_t)(source[at++] - '0'); }
            if (negative) value = UINT64_C(0) - value;
            result(out, type, value);
        }
        return true;
    }
    case C_LOCALTIME: {
        if (!windows_read(owner, a, 8, &value, error)) return false;
        if (value > UINT64_C(32535215999)) return true;
        guest_windows_calendar date;
        if (!owner->capabilities.calendar(owner->capabilities.context, (int64_t)value * 1000, true, &date, error)) return false;
        int32_t fields[] = {date.second,date.minute,date.hour,date.day,date.month - 1,date.year - 1900,date.weekday,date.year_day,date.daylight};
        for (size_t i = 0; i < 9; ++i) if (!windows_write(owner, crt->time_buffer + i * 4, 4, (uint32_t)fields[i], error)) return false;
        result(out, type, crt->time_buffer); return true;
    }
    case C_LOCALE: result(out, type, crt->locale); return true;
    case C_PRINTF: case C_PRINTF_S:
        return printf_service(owner, args, operation == C_PRINTF_S, scratch, out, error);
    case C_SCANF: return scanf_service(owner, args, out, error);
    default: return guest_fail(error, QA_ERROR_ARGUMENT, operation, "invalid Windows CRT descriptor");
    }
}

bool windows_crt_invoke(windows_service *service, const qa_native_value *args,
    size_t count, qa_native_value *out, qa_error *error)
{
    qa_arena scratch; qa_arena_init_pool(&scratch, &service->owner->guest->scratch_pages);
    bool okay = crt_invoke(service, args, count, &scratch, out, error);
    qa_arena_destroy(&scratch);
    return okay;
}

static bool va_next(guest_windows *owner, uint64_t *cursor, size_t bytes, uint64_t *value, qa_error *error)
{
    size_t width = owner->target.pointer_bytes;
    if (!*cursor || *cursor > UINT64_MAX - (width - 1)) return guest_fail(error, QA_ERROR_ARGUMENT, *cursor, "Windows va_list has no actual argument storage");
    uint64_t at = (*cursor + width - 1) & ~(uint64_t)(width - 1);
    if (!windows_read(owner, at, bytes, value, error)) return false;
    *cursor = at + (bytes > width ? bytes : width); return true;
}

static bool printf_service(guest_windows *owner, const qa_native_value *args, bool secure,
    qa_arena *scratch, qa_native_value *out, qa_error *error)
{
    uint64_t options = integer(args), buffer = integer(args + 1), capacity = integer(args + 2),
        maximum = secure ? integer(args + 3) : 0,
        format = integer(args + (secure ? 4 : 3)), cursor = integer(args + (secure ? 6 : 5));
    if (integer(args + (secure ? 5 : 4)) || (options & ~UINT64_C(0x3f)) || (options & 8))
        return guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "unsupported UCRT printf locale/options");
    if (!format || (!secure && !buffer && capacity)) {
        if (!windows_write(owner, owner->crt->error_number, 4, 22, error)) return false;
        result(out, QA_NATIVE_I32, UINT32_MAX); return true;
    }
    if (secure && !maximum && !buffer && !capacity) {
        result(out, QA_NATIVE_I32, 0); return true;
    }
    if (secure && (!buffer || !capacity)) {
        if (!windows_write(owner, owner->crt->error_number, 4, 22, error)) return false;
        result(out, QA_NATIVE_I32, UINT32_MAX); return true;
    }
    bool count_limited = secure && capacity > maximum;
    uint64_t render_capacity = count_limited ? maximum + 1 : capacity;
    int32_t returned; uint32_t format_errno;
    if (!guest_format_render_va(owner->guest,format,cursor,buffer,render_capacity,options,scratch,
        &returned,&format_errno,error)) return false;
    if (secure) {
        if (!count_limited && !windows_write(owner, buffer + capacity - 1, 1, 0, error)) return false;
        uint64_t truncate = owner->target.pointer_bytes == 4 ? UINT32_MAX : UINT64_MAX;
        if (returned == -2 && (count_limited || maximum == truncate)) {
            if (format_errno == 34) format_errno = 0;
            returned = -1;
        } else if (returned < 0) {
            if (!windows_write(owner, buffer, 1, 0, error)) return false;
            if (returned == -2) format_errno = 34;
            returned = -1;
        }
    }
    if (format_errno && !windows_write(owner,owner->crt->error_number,4,format_errno,error)) return false;
    result(out,QA_NATIVE_I32,(uint32_t)returned); return true;
}

typedef struct windows_scan_arguments { guest_windows *owner; uint64_t cursor; } windows_scan_arguments;
static bool scanf_destination(void *context, uint64_t *out, qa_error *error)
{
    windows_scan_arguments *arguments = context;
    return va_next(arguments->owner, &arguments->cursor,
        arguments->owner->target.pointer_bytes, out, error);
}
static bool scanf_service(guest_windows *owner, const qa_native_value *args,
    qa_native_value *out, qa_error *error)
{
    if (integer(args + 4) || (integer(args) & ~UINT64_C(2)))
        return guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "unsupported Windows scanf locale/options");
    if (!integer(args + 1)) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "Windows scanf input is null");
    qa_arena scratch; qa_arena_init_pool(&scratch, &owner->guest->scratch_pages);
    guest_scan_program *program = NULL;
    if (!guest_scan_prepare(owner->guest, integer(args + 3), false, &scratch, &program, error)) {
        qa_arena_destroy(&scratch);
        return false;
    }
    windows_scan_arguments arguments = {owner, integer(args + 5)};
    int32_t assigned;
    bool okay = guest_scan_execute(program, owner->guest, integer(args + 1), integer(args + 2),
        scanf_destination, &arguments, &assigned, error);
    qa_arena_destroy(&scratch);
    if (okay) result(out, QA_NATIVE_I32, (uint32_t)assigned);
    return okay;
}

bool windows_crt_validate(guest_windows *owner, qa_error *error)
{
    guest_windows_crt *crt = owner->crt; size_t width = owner->target.pointer_bytes;
    return windows_validate_storage(owner,crt->onexit,width*3,0x57494e,error) &&
        windows_validate_storage(owner,crt->error_number,4,0x57494e,error) &&
        windows_validate_storage(owner,crt->time_buffer,36,0x57494e,error) &&
        windows_validate_storage(owner,crt->locale,width*10+16,0x57494e,error) &&
        windows_validate_storage(owner,crt->empty,1,0x57494e,error) &&
        windows_validate_storage(owner,crt->decimal,2,0x57494e,error) &&
        windows_stdio_lower_valid(owner,error);
}
