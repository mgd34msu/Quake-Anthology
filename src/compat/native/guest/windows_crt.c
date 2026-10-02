#include "internal.h"
#include "windows_crt.h"
#include "qa/text.h"
#include <math.h>
#include <fenv.h>
#include <stdio.h>
#include <ctype.h>

enum crt_operation {
    C_TICKS = 1, C_MALLOC, C_CALLOC, C_FREE, C_NEW_HANDLER, C_COPY, C_FILL, C_COMPARE,
    C_FIND, C_LENGTH, C_STRCMP, C_STRNCMP, C_STRCHR, C_STRSTR, C_ARGV, C_ENVIRONMENT,
    C_ONEXIT_INIT, C_ONEXIT_REGISTER, C_ATEXIT, C_ONEXIT_EXECUTE, C_CEXIT,
    C_INITTERM, C_INITTERM_E, C_TYPEINFO, C_QSORT, C_ACOS, C_SIN, C_CEIL, C_COS,
    C_TRUNC, C_LOG2, C_FLOOR, C_SQRT, C_TAN, C_ATAN2, C_FMOD, C_POW, C_NEXTAFTER,
    C_MODF, C_DCLASS, C_FCLASS, C_SIGN, C_ERRNO, C_STRTOUL, C_ATOI, C_ATOLL, C_ATOF,
    C_TIME, C_LOCALTIME, C_LOCALE, C_PRINTF, C_SCANF
};
typedef struct crt_descriptor {
    const char *family, *name; uint32_t operation; qa_native_value_type result;
    size_t count; qa_native_value_type parameters[6];
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
    D("stdio","_fileno",WST_FILENO,I,1,P)
};
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
        const crt_descriptor *entry = descriptors + i; qa_native_value_type types[6];
        for (size_t j = 0; j < entry->count; ++j) types[j] = entry->parameters[j] == QA_NATIVE_BYTES ?
            owner->target.pointer_bytes == 4 ? QA_NATIVE_U32 : QA_NATIVE_U64 : entry->parameters[j];
        qa_native_value_type return_type = entry->result == QA_NATIVE_BYTES ? owner->target.pointer_bytes == 4 ? QA_NATIVE_U32 : QA_NATIVE_U64 : entry->result;
        char library[96];
        if (!strcmp(entry->family, "msvcp")) memcpy(library, "msvcp140.dll", 13);
        else if (!strcmp(entry->family, "vcruntime")) memcpy(library, "vcruntime140.dll", 16);
        else snprintf(library, sizeof(library), "api-ms-win-crt-%s-l1-1-0.dll", entry->family);
        uint64_t id = UINT64_C(0x57494e0200000000) + i * 3 + 1;
        if (!windows_service_add(owner, id, 2, entry->operation, library, entry->name, types,
            entry->count, return_type, GUEST_ABI_DEFAULT, bind, error)) return false;
        if (strcmp(entry->family, "msvcp") &&
            (!windows_service_add(owner, id + 1, 2, entry->operation, "ucrtbase.dll", entry->name, types,
                entry->count, return_type, GUEST_ABI_DEFAULT, bind, error) ||
             !windows_service_add(owner, id + 2, 2, entry->operation, "msvcrt.dll", entry->name, types,
                entry->count, return_type, GUEST_ABI_DEFAULT, bind, error))) return false;
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

static bool text(guest_windows *owner, uint64_t address, char **out, qa_error *error)
{
    uint16_t *units = NULL; size_t count;
    if (!windows_string(owner, address, false, &units, &count, error)) return false;
    char *value = malloc(count + 1);
    if (!value) { free(units); return guest_fail(error, QA_ERROR_MEMORY, 0, "reading CRT string"); }
    for (size_t i = 0; i <= count; ++i) value[i] = (char)units[i];
    free(units); *out = value; return true;
}

static bool extent(uint64_t value, size_t *out, qa_error *error)
{
    if (value > 0x10000000) return guest_fail(error, QA_ERROR_ARGUMENT, value, "CRT byte count exceeds source limit");
    *out = (size_t)value; return true;
}

static bool move(guest_windows *owner, uint64_t destination, uint64_t source, size_t bytes, qa_error *error)
{
    if (!bytes) return true;
    uint8_t *data = malloc(bytes);
    if (!data) return guest_fail(error, QA_ERROR_MEMORY, 0, "staging CRT memory move");
    bool okay = qa_native_guest_read(owner->guest, source, data, bytes, error) &&
        qa_native_guest_write(owner->guest, destination, (qa_bytes){data, bytes}, error);
    free(data); return okay;
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

typedef struct sort_context { guest_windows *owner; uint64_t base, comparator; size_t bytes; } sort_context;

static bool sort_compare(sort_context *sort, size_t left, size_t right, int32_t *out, qa_error *error)
{
    const qa_native_value_type types[] = {QA_NATIVE_ADDRESS, QA_NATIVE_ADDRESS};
    const qa_native_value args[] = {{.type = QA_NATIVE_ADDRESS, .as.address = sort->base + left * sort->bytes},
        {.type = QA_NATIVE_ADDRESS, .as.address = sort->base + right * sort->bytes}};
    qa_native_value result;
    if (!windows_invoke(sort->owner, sort->comparator, types, 2, QA_NATIVE_I32, args, false, &result, error)) return false;
    *out = result.as.i32; return true;
}

static bool sort_swap(sort_context *sort, size_t left, size_t right, qa_error *error)
{
    uint8_t *bytes = malloc(sort->bytes);
    if (!bytes) return guest_fail(error, QA_ERROR_MEMORY, 0, "staging CRT qsort swap");
    uint64_t a = sort->base + left * sort->bytes, b = sort->base + right * sort->bytes;
    bool okay = qa_native_guest_read(sort->owner->guest, a, bytes, sort->bytes, error) &&
        move(sort->owner, a, b, sort->bytes, error) &&
        qa_native_guest_write(sort->owner->guest, b, (qa_bytes){bytes, sort->bytes}, error);
    free(bytes); return okay;
}

static bool sort_sift(sort_context *sort, size_t root, size_t end, qa_error *error)
{
    while (root < end / 2) {
        size_t child = root * 2 + 1; int32_t comparison;
        if (child + 1 < end) {
            if (!sort_compare(sort, child, child + 1, &comparison, error)) return false;
            if (comparison < 0) ++child;
        }
        if (!sort_compare(sort, root, child, &comparison, error)) return false;
        if (comparison >= 0) return true;
        if (!sort_swap(sort, root, child, error)) return false;
        root = child;
    }
    return true;
}

static bool printf_service(guest_windows *, const qa_native_value *, qa_native_value *, qa_error *);
static bool scanf_service(guest_windows *, const qa_native_value *, qa_native_value *, qa_error *);

bool windows_crt_invoke(windows_service *service, const qa_native_value *args,
    size_t count, qa_native_value *out, qa_error *error)
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
            if (bytes && !guest_range(owner->guest,a,bytes,QA_NATIVE_GUEST_WRITE,error)) return false;
            uint8_t data[4096]; memset(data, (uint8_t)b, sizeof(data)); size_t left = bytes; uint64_t at = a;
            while (left) { size_t part = left < sizeof(data) ? left : sizeof(data); if (!qa_native_guest_write(owner->guest, at, (qa_bytes){data, part}, error)) return false; left -= part; at += part; }
            result(out, type, a); return true;
        }
        if (bytes && (!guest_range(owner->guest,a,bytes,QA_NATIVE_GUEST_READ,error) ||
            (operation == C_COMPARE && !guest_range(owner->guest,b,bytes,QA_NATIVE_GUEST_READ,error)))) return false;
        for (size_t i = 0; i < bytes; ++i) {
            uint64_t left, right;
            if (!windows_read(owner, a + i, 1, &left, error)) return false;
            if (operation == C_FIND) { if (left == (b & 255)) { result(out, type, a + i); return true; } }
            else { if (!windows_read(owner, b + i, 1, &right, error)) return false; if (left != right) { result(out, type, left - right); return true; } }
        }
        return true;
    }
    case C_LENGTH: case C_STRCHR: case C_STRSTR: {
        char *source = NULL, *search = NULL;
        if (!text(owner, a, &source, error)) return false;
        if (operation == C_LENGTH) value = strlen(source);
        else if (operation == C_STRCHR) { char *found = strchr(source, (uint8_t)b); value = found ? a + (size_t)(found - source) : 0; }
        else {
            if (!text(owner, b, &search, error)) { free(source); return false; }
            char *found = strstr(source, search); value = found ? a + (size_t)(found - source) : 0;
        }
        free(source); free(search); result(out, type, value); return true;
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
        sort_context sort = {owner, a, integer(args + 3), bytes};
        for (size_t start = length / 2; start; ) { --start; if (!sort_sift(&sort, start, length, error)) return false; }
        for (size_t end = length - 1; end; --end)
            if (!sort_swap(&sort, 0, end, error) || !sort_sift(&sort, 0, end, error)) return false;
        return true;
    }
    case C_ERRNO: result(out, type, crt->error_number); return true;
    case C_STRTOUL: {
        char *source = NULL;
        if (!text(owner, a, &source, error)) return false;
        int base = args[2].as.i32; size_t at = 0, stop = 0; uint32_t number = 0; bool overflowed = false;
        if (base != 0 && (base < 2 || base > 36)) { if (!windows_write(owner, crt->error_number, 4, 22, error)) { free(source); return false; } }
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
            if (overflowed && !windows_write(owner, crt->error_number, 4, 34, error)) { free(source); return false; }
        }
        free(source);
        if (b && !windows_write(owner, b, width, a + stop, error)) return false;
        result(out, type, number); return true;
    }
    case C_ATOI: case C_ATOLL: case C_ATOF: {
        char *source = NULL;
        if (!text(owner, a, &source, error)) return false;
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
                        free(source); return guest_fail(error,QA_ERROR_UNSUPPORTED,0,"host cannot provide nearest decimal conversion");
                    }
                    bool okay = qa_parse_atof(start,&out->as.f64,error);
                    if (fesetenv(&saved) != 0) okay = guest_fail(error,QA_ERROR_UNSUPPORTED,0,"host decimal conversion environment restore failed");
                    if (!okay) { free(source); return false; }
                }
            }
        } else {
            size_t at = 0; while (source[at] && qa_unicode_whitespace((uint8_t)source[at])) ++at;
            bool negative = source[at] == '-'; if (negative || source[at] == '+') ++at;
            while (source[at] >= '0' && source[at] <= '9') { value = value * 10 + (uint32_t)(source[at++] - '0'); }
            if (negative) value = UINT64_C(0) - value;
            result(out, type, value);
        }
        free(source); return true;
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
    case C_PRINTF: return printf_service(owner, args, out, error);
    case C_SCANF: return scanf_service(owner, args, out, error);
    default: return guest_fail(error, QA_ERROR_ARGUMENT, operation, "invalid Windows CRT descriptor");
    }
}

typedef struct printf_token {
    size_t start, length;
    char code, modifier[4];
    uint32_t flags;
    int32_t width, precision;
    bool conversion, width_argument, precision_argument;
} printf_token;
enum { PRINT_LEFT=1, PRINT_PLUS=2, PRINT_BLANK=4, PRINT_ALT=8, PRINT_ZERO=16 };
typedef struct printf_output {
    guest_windows *owner; uint64_t address, capacity, used, total, options;
    uint32_t format_error;
    bool full;
} printf_output;

static bool format_number(const char *format, size_t *at, int32_t *out, uint32_t *failure)
{
    uint64_t number = 0;
    while (format[*at] >= '0' && format[*at] <= '9') {
        uint32_t digit = (uint32_t)(format[(*at)++] - '0');
        if (number > (INT32_MAX - digit) / 10) { *failure = 132; return false; }
        number = number * 10 + digit;
    }
    *out = (int32_t)number; return true;
}

static bool printf_parse(const char *format, printf_token **out, size_t *count,
    uint32_t *failure, qa_error *error)
{
    size_t at = 0, capacity = 0; printf_token *tokens = NULL;
    while (format[at]) {
        printf_token token = {.start = at, .precision = -1};
        while (format[at] && format[at] != '%') ++at;
        if (at != token.start) { token.length = at - token.start; }
        else {
            ++at;
            if (format[at] == '%') { token.start = at++; token.length = 1; }
            else {
                token.conversion = true;
                size_t position = at; int32_t index;
                if (!format_number(format, &at, &index, failure)) goto failed;
                if (format[at] == '$') { *failure = 22; goto failed; }
                at = position;
                while (format[at] && strchr("-+ #0'", format[at])) {
                    char c = format[at++];
                    token.flags |= c == '-' ? PRINT_LEFT : c == '+' ? PRINT_PLUS : c == ' ' ? PRINT_BLANK : c == '#' ? PRINT_ALT : c == '0' ? PRINT_ZERO : 0;
                }
                if (format[at] == '*') { token.width_argument = true; ++at; }
                else if (!format_number(format, &at, &token.width, failure)) goto failed;
                if (format[at] == '.') {
                    ++at; token.precision = 0;
                    if (format[at] == '*') { token.precision_argument = true; ++at; }
                    else if (!format_number(format, &at, &token.precision, failure)) goto failed;
                }
                static const char *modifiers[] = {"I64","I32","hh","ll","h","l","L","j","z","t","I","w","q"};
                for (size_t i = 0; i < sizeof(modifiers)/sizeof(*modifiers); ++i) {
                    size_t bytes = strlen(modifiers[i]);
                    if (!strncmp(format + at, modifiers[i], bytes)) { memcpy(token.modifier, modifiers[i], bytes); at += bytes; break; }
                }
                token.code = format[at];
                if (!token.code || !strchr("diouxXfFeEgGaAcCsSpn", token.code)) { *failure = 22; goto failed; }
                ++at;
            }
        }
        if (!guest_grow((void **)&tokens, &capacity, *count + 1, sizeof(*tokens), error)) { free(tokens); return false; }
        tokens[(*count)++] = token;
    }
    *out = tokens; return true;
failed:
    free(tokens); *count = 0; return true;
}

static bool printf_append(printf_output *output, const char *bytes, size_t count, qa_error *error)
{
    if (!count || output->full || output->format_error) return true;
    uint64_t available = output->address && output->capacity > output->used ? output->capacity - output->used : 0;
    size_t copied = available < count ? (size_t)available : count;
    if (copied && !qa_native_guest_write(output->owner->guest, output->address + output->used, (qa_bytes){(const uint8_t *)bytes, copied}, error)) return false;
    output->used += copied;
    if (output->address && !(output->options & 2) && copied < count) { output->full = true; return true; }
    output->total += count;
    if (output->total > INT32_MAX) output->format_error = 132;
    return true;
}

static bool printf_repeat(printf_output *output, char c, uint64_t count, qa_error *error)
{
    char bytes[4096]; memset(bytes, c, sizeof(bytes));
    if (!output->address || (output->options & 2)) {
        uint64_t available = output->address && output->capacity > output->used ? output->capacity - output->used : 0;
        uint64_t copied = available < count ? available : count;
        for (uint64_t at = 0; at < copied; ) {
            size_t part = copied - at < sizeof(bytes) ? (size_t)(copied - at) : sizeof(bytes);
            if (!printf_append(output, bytes, part, error)) return false;
            at += part;
        }
        if (!output->format_error) {
            output->total += count - copied;
            if (output->total > INT32_MAX) output->format_error = 132;
        }
    } else {
        while (count && !output->full && !output->format_error) {
            size_t part = count < sizeof(bytes) ? (size_t)count : sizeof(bytes);
            if (!printf_append(output, bytes, part, error)) return false;
            count -= part;
        }
    }
    return true;
}

static bool va_next(guest_windows *owner, uint64_t *cursor, size_t bytes, uint64_t *value, qa_error *error)
{
    size_t width = owner->target.pointer_bytes;
    if (!*cursor || *cursor > UINT64_MAX - (width - 1)) return guest_fail(error, QA_ERROR_ARGUMENT, *cursor, "Windows va_list has no actual argument storage");
    uint64_t at = (*cursor + width - 1) & ~(uint64_t)(width - 1);
    if (!windows_read(owner, at, bytes, value, error)) return false;
    *cursor = at + (bytes > width ? bytes : width); return true;
}

/* A binary64 significand produces at most 1075 exact decimal places. This
 * bounded decimal integer avoids host printf rounding and locale entirely. */
typedef struct decimal_integer { uint8_t digits[1200]; size_t count; } decimal_integer;

static void decimal_start(decimal_integer *number, uint64_t value)
{
    number->count = 0;
    do { number->digits[number->count++] = (uint8_t)(value % 10); value /= 10; } while (value);
}

static void decimal_multiply(decimal_integer *number, unsigned factor)
{
    unsigned carry = 0;
    for (size_t i = 0; i < number->count; ++i) { unsigned value = number->digits[i] * factor + carry; number->digits[i] = (uint8_t)(value % 10); carry = value / 10; }
    while (carry) { number->digits[number->count++] = (uint8_t)(carry % 10); carry /= 10; }
}

static bool round_up(unsigned mode, bool sign, unsigned first, bool rest, unsigned last)
{
    bool inexact = first != 0 || rest;
    if (mode == 0) return first > 5 || (first == 5 && (rest || (last & 1)));
    if (mode == 4) return first >= 5;
    return mode == 1 ? sign && inexact : mode == 2 ? !sign && inexact : false;
}

static char *decimal_scaled(const decimal_integer *number, int scale, int places,
    unsigned mode, bool sign, qa_error *error)
{
    if (number->count == 1 && number->digits[0] == 0) {
        char *zero = malloc(2);
        if (!zero) { guest_fail(error, QA_ERROR_MEMORY, 0, "formatting Windows decimal zero"); return NULL; }
        zero[0] = '0'; zero[1] = 0; return zero;
    }
    int drop = scale - places; size_t length;
    if (drop <= 0) length = number->count + (size_t)(-drop);
    else length = (size_t)drop < number->count ? number->count - (size_t)drop : 1;
    char *out = malloc(length + 2);
    if (!out) { guest_fail(error, QA_ERROR_MEMORY, 0, "formatting exact Windows decimal"); return NULL; }
    if (drop <= 0) {
        for (size_t i = 0; i < number->count; ++i) out[i] = (char)('0' + number->digits[number->count - i - 1]);
        memset(out + number->count, '0', (size_t)(-drop));
    } else {
        for (size_t i = 0; i < length; ++i) {
            size_t position = (size_t)drop + length - i - 1;
            out[i] = position < number->count ? (char)('0' + number->digits[position]) : '0';
        }
        unsigned first = (size_t)(drop - 1) < number->count ? number->digits[drop - 1] : 0; bool rest = false;
        for (size_t i = 0; i < number->count && i + 1 < (size_t)drop; ++i) if (number->digits[i]) rest = true;
        if (round_up(mode, sign, first, rest, (unsigned)(out[length - 1] - '0'))) {
            size_t i = length;
            while (i && out[i - 1] == '9') out[--i] = '0';
            if (i) ++out[i - 1];
            else { memmove(out + 1, out, length); out[0] = '1'; ++length; }
        }
    }
    out[length] = 0; return out;
}

static char *fixed_decimal(const char *digits, size_t places, bool point, qa_error *error)
{
    size_t count = strlen(digits), whole = count > places ? count - places : 1;
    size_t bytes = whole + places + (places || point ? 1 : 0);
    char *out = malloc(bytes + 1);
    if (!out) { guest_fail(error, QA_ERROR_MEMORY, 0, "formatting Windows decimal point"); return NULL; }
    size_t at = 0;
    if (count > places) { memcpy(out, digits, whole); at = whole; } else out[at++] = '0';
    if (places || point) out[at++] = '.';
    if (places) {
        size_t zeros = places > count ? places - count : 0;
        memset(out + at, '0', zeros); at += zeros;
        const char *fraction = count > places ? digits + count - places : digits;
        memcpy(out + at, fraction, places - zeros); at += places - zeros;
    }
    out[at] = 0; return out;
}

static char *float_body(uint64_t bits, char code, int precision, bool alternate,
    unsigned mode, unsigned exponent_digits, qa_error *error)
{
    bool sign = (bits >> 63) != 0; unsigned encoded = (unsigned)((bits >> 52) & 2047);
    uint64_t coefficient = bits & UINT64_C(0xfffffffffffff); char lower = (char)tolower((unsigned char)code);
    char *out = NULL;
    if (encoded == 2047) {
        const char *word = !coefficient ? "inf" : !(coefficient & UINT64_C(0x8000000000000)) ? "nan(snan)" :
            sign && coefficient == UINT64_C(0x8000000000000) ? "nan(ind)" : "nan";
        out = malloc(strlen(word) + 1); if (out) strcpy(out, word);
    } else {
        int exponent = encoded ? (int)encoded - 1023 - 52 : -1074;
        if (encoded) coefficient |= UINT64_C(0x10000000000000);
        if (lower == 'a') {
            size_t places = precision < 0 ? 13 : (size_t)precision;
            int exp = !coefficient ? 0 : !encoded ? -1022 : exponent + 52;
            unsigned keep = places < 13 ? (unsigned)places * 4 : 52;
            uint64_t rounded = coefficient;
            if (keep < 52) {
                unsigned shift = 52 - keep; uint64_t denominator = UINT64_C(1) << shift;
                uint64_t remainder = coefficient & (denominator - 1); rounded = coefficient >> shift;
                bool up = mode == 0 ? remainder > denominator / 2 || (remainder == denominator / 2 && (rounded & 1)) :
                    mode == 4 ? remainder >= denominator / 2 : mode == 1 ? sign && remainder != 0 : mode == 2 ? !sign && remainder != 0 : false;
                if (up) ++rounded;
            }
            size_t natural = places < 13 ? places : 13;
            char suffix[32]; snprintf(suffix, sizeof(suffix), "p%c%d", exp < 0 ? '-' : '+', exp < 0 ? -exp : exp);
            out = malloc(places + strlen(suffix) + 6);
            if (out) {
                size_t at = 0; out[at++] = '0'; out[at++] = 'x';
                out[at++] = "0123456789abcdef"[(rounded >> (natural * 4)) & 15];
                if (places || alternate) out[at++] = '.';
                for (size_t i = 0; i < natural; ++i) out[at++] = "0123456789abcdef"[(rounded >> ((natural - i - 1) * 4)) & 15];
                for (size_t i = natural; i < places; ++i) out[at++] = '0';
                strcpy(out + at, suffix);
            }
        } else {
            decimal_integer exact; decimal_start(&exact, coefficient); int scale = 0;
            if (exponent >= 0) for (int i = 0; i < exponent; ++i) decimal_multiply(&exact, 2);
            else { scale = -exponent; for (int i = 0; i < scale; ++i) decimal_multiply(&exact, 5); }
            int places = precision < 0 ? 6 : precision;
            if (lower == 'f') {
                char *digits = decimal_scaled(&exact, scale, places, mode, sign, error);
                if (!digits) return NULL;
                out = fixed_decimal(digits, (size_t)places, alternate, error); free(digits);
            } else {
                int count = lower == 'e' ? places + 1 : places < 1 ? 1 : places;
                int exp = coefficient ? (int)exact.count - 1 - scale : 0;
                char *digits = decimal_scaled(&exact, scale, count - 1 - exp, mode, sign, error);
                if (!digits) return NULL;
                size_t length = strlen(digits);
                if (length > (size_t)count) { ++exp; digits[count] = 0; }
                else if (length < (size_t)count) {
                    char *padded = malloc((size_t)count + 1);
                    if (!padded) { free(digits); guest_fail(error, QA_ERROR_MEMORY, 0, "padding Windows significant digits"); return NULL; }
                    memset(padded, '0', (size_t)count - length);
                    memcpy(padded + count - length, digits, length + 1); free(digits); digits = padded;
                }
                if (lower == 'e' || exp < -4 || exp >= count) {
                    size_t tail = strlen(digits) - 1;
                    if (lower == 'g' && !alternate) while (tail && digits[tail] == '0') --tail;
                    char suffix[32]; snprintf(suffix, sizeof(suffix), "e%c%0*u", exp < 0 ? '-' : '+', (int)exponent_digits, (unsigned)(exp < 0 ? -exp : exp));
                    out = malloc(tail + strlen(suffix) + 3);
                    if (out) {
                        size_t at = 0; out[at++] = digits[0];
                        if (tail || alternate) out[at++] = '.';
                        memcpy(out + at, digits + 1, tail); at += tail; strcpy(out + at, suffix);
                    }
                } else {
                    out = fixed_decimal(digits, (size_t)(count - 1 - exp), alternate, error);
                    if (out && !alternate && strchr(out, '.')) {
                        size_t end = strlen(out); while (end && out[end - 1] == '0') --end; if (end && out[end - 1] == '.') --end; out[end] = 0;
                    }
                }
                free(digits);
            }
        }
    }
    if (!out) { guest_fail(error, QA_ERROR_MEMORY, 0, "formatting Windows floating value"); return NULL; }
    if (code != lower) for (size_t i = 0; out[i]; ++i) out[i] = (char)toupper((unsigned char)out[i]);
    return out;
}

static unsigned integer_bits(const printf_token *token, size_t width)
{
    const char *length = token->modifier;
    if (!strcmp(length,"hh")) return 8;
    if (!strcmp(length,"h")) return 16;
    if (!strcmp(length,"ll") || !strcmp(length,"j") || !strcmp(length,"I64") || !strcmp(length,"q")) return 64;
    if (!strcmp(length,"z") || !strcmp(length,"t") || !strcmp(length,"I")) return (unsigned)width * 8;
    return 32;
}

static bool printf_service(guest_windows *owner, const qa_native_value *args,
    qa_native_value *out, qa_error *error)
{
    uint64_t options = integer(args), buffer = integer(args + 1), capacity = integer(args + 2), format = integer(args + 3), cursor = integer(args + 5);
    if (integer(args + 4) || (options & ~UINT64_C(0x3f)) || (options & 8))
        return guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "unsupported UCRT printf locale/options");
    if (!format || (!buffer && capacity)) {
        if (!windows_write(owner, owner->crt->error_number, 4, 22, error)) return false;
        result(out, QA_NATIVE_I32, UINT32_MAX); return true;
    }
    char *source = NULL; printf_token *tokens = NULL; size_t token_count = 0;
    printf_output output = {.owner = owner, .address = buffer, .capacity = capacity, .options = options};
    if (!text(owner, format, &source, error) || !printf_parse(source, &tokens, &token_count, &output.format_error, error)) { free(source); return false; }
    unsigned mode = 4;
    if (options & 32) { qa_native_guest_cpu cpu; if (!qa_native_guest_cpu_read(owner->guest, &cpu, error)) { free(source); free(tokens); return false; } mode = (cpu.mxcsr >> 13) & 3; }
    bool okay = true; size_t width = owner->target.pointer_bytes;
    for (size_t i = 0; okay && i < token_count && !output.full && !output.format_error; ++i) {
        printf_token token = tokens[i];
        if (!token.conversion) { okay = printf_append(&output, source + token.start, token.length, error); continue; }
        uint64_t argument; int64_t field_width = token.width; int precision = token.precision;
        if (token.width_argument) { if (!va_next(owner, &cursor, 4, &argument, error)) { okay = false; break; } uint32_t bits = (uint32_t)argument; int32_t signed_width; memcpy(&signed_width, &bits, 4); field_width = signed_width; }
        if (token.precision_argument) { if (!va_next(owner, &cursor, 4, &argument, error)) { okay = false; break; } uint32_t bits = (uint32_t)argument; memcpy(&precision, &bits, 4); }
        if (precision < 0) precision = -1;
        if (precision > 1048576) { okay = guest_fail(error, QA_ERROR_ARGUMENT, format, "Windows printf precision exceeds source limit"); break; }
        bool left = (token.flags & PRINT_LEFT) || field_width < 0;
        if (field_width < 0) field_width = -field_width;
        bool floating = strchr("fFeEgGaA", token.code) != NULL;
        unsigned bits = integer_bits(&token, width); size_t bytes = floating || (!strchr("sSpncC", token.code) && bits == 64) ? 8 : strchr("sSpn", token.code) ? width : 4;
        if (!va_next(owner, &cursor, bytes, &argument, error)) { okay = false; break; }
        char prefix[4] = {0}, small[80] = {0}, *body = small, *allocated = NULL; size_t body_count = 0;
        bool zero = (token.flags & PRINT_ZERO) && !left; uint64_t leading_zeros = 0;
        if (strchr("diouxX", token.code)) {
            bool signed_value = token.code == 'd' || token.code == 'i';
            uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1; argument &= mask;
            bool negative = signed_value && (argument & (UINT64_C(1) << (bits - 1))); uint64_t magnitude = negative ? (UINT64_C(0) - argument) & mask : argument;
            unsigned radix = token.code == 'o' ? 8 : token.code == 'x' || token.code == 'X' ? 16 : 10;
            char reversed[65]; size_t digits = 0; uint64_t remaining = magnitude;
            if (precision != 0 || magnitude) do { reversed[digits++] = (token.code == 'X' ? "0123456789ABCDEF" : "0123456789abcdef")[remaining % radix]; remaining /= radix; } while (remaining);
            for (size_t j = 0; j < digits; ++j) small[j] = reversed[digits - j - 1];
            body_count = digits;
            if (precision >= 0 && (size_t)precision > digits) leading_zeros = (size_t)precision - digits;
            if (signed_value) prefix[0] = negative ? '-' : (token.flags & PRINT_PLUS) ? '+' : (token.flags & PRINT_BLANK) ? ' ' : 0;
            else if ((token.flags & PRINT_ALT) && radix == 16 && magnitude) { prefix[0] = '0'; prefix[1] = token.code == 'X' ? 'X' : 'x'; }
            else if ((token.flags & PRINT_ALT) && radix == 8 && !leading_zeros && (!digits || small[0] != '0')) prefix[0] = '0';
            if (precision >= 0) zero = false;
        } else if (floating) {
            allocated = float_body(argument, token.code, precision, (token.flags & PRINT_ALT) != 0, mode, options & 16 ? 3 : 2, error);
            if (!allocated) { okay = false; break; }
            body = allocated; prefix[0] = (argument >> 63) ? '-' : (token.flags & PRINT_PLUS) ? '+' : (token.flags & PRINT_BLANK) ? ' ' : 0;
            if (((argument >> 52) & 2047) == 2047) zero = false;
            if (body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) {
                size_t at = strlen(prefix); prefix[at] = '0'; prefix[at + 1] = body[1]; body += 2;
            }
            body_count = strlen(body);
        } else if (token.code == 'p') {
            for (size_t j = 0; j < width * 2; ++j) small[j] = "0123456789ABCDEF"[(argument >> ((width * 2 - j - 1) * 4)) & 15];
            body_count = width * 2;
        } else if (token.code == 's' || token.code == 'S') {
            bool wide = !strcmp(token.modifier,"l") || !strcmp(token.modifier,"w") || (token.code == 'S' && strcmp(token.modifier,"h")); zero = false;
            if (!argument) { body = "(null)"; body_count = precision >= 0 && precision < 6 ? (size_t)precision : 6; }
            else {
                size_t maximum = precision < 0 ? 1048576 : (size_t)precision;
                allocated = malloc(maximum + 1); if (!allocated) { okay = guest_fail(error, QA_ERROR_MEMORY, 0, "formatting Windows printf string"); break; }
                body = allocated;
                for (; body_count < maximum; ++body_count) {
                    uint64_t c;
                    if (!windows_read(owner, argument + body_count * (wide ? 2 : 1), wide ? 2 : 1, &c, error)) { okay = false; break; }
                    if (!c) break;
                    if (wide && c > 255) { output.format_error = 42; break; }
                    body[body_count] = (char)c;
                }
                if (okay && precision < 0 && body_count == maximum) okay = guest_fail(error, QA_ERROR_ARGUMENT, argument, "Windows printf string exceeds source limit");
            }
        } else if (token.code == 'c' || token.code == 'C') {
            bool wide = !strcmp(token.modifier,"l") || !strcmp(token.modifier,"w") || (token.code == 'C' && strcmp(token.modifier,"h"));
            uint32_t c = (uint32_t)argument & (wide ? 65535u : 255u); zero = false;
            if (wide && c > 255) output.format_error = 42;
            else { small[0] = (char)c; body_count = 1; }
        } else output.format_error = 22;
        if (okay && !output.format_error) {
            uint64_t prefix_count = strlen(prefix), total = prefix_count + body_count + leading_zeros;
            uint64_t padding = (uint64_t)field_width > total ? (uint64_t)field_width - total : 0;
            if (!left && !zero) okay = printf_repeat(&output, ' ', padding, error);
            if (okay) okay = printf_append(&output, prefix, (size_t)prefix_count, error);
            if (okay && zero) okay = printf_repeat(&output, '0', padding, error);
            if (okay) okay = printf_repeat(&output, '0', leading_zeros, error);
            if (okay) okay = printf_append(&output, body, body_count, error);
            if (okay && left) okay = printf_repeat(&output, ' ', padding, error);
        }
        free(allocated);
    }
    free(source); free(tokens);
    if (!okay) return false;
    int32_t returned = output.format_error ? -1 : (int32_t)output.total;
    if (buffer) {
        if (options & 1) {
            if (output.used < capacity && !windows_write(owner, buffer + output.used, 1, 0, error)) return false;
            if (output.full || output.total > capacity) returned = -1;
        } else {
            if (capacity) {
                uint64_t at = output.format_error && (options & 2) ? 0 : output.used >= capacity ? capacity - 1 : output.used;
                if (!windows_write(owner, buffer + at, 1, 0, error)) return false;
            }
            if (!(options & 2) && (!capacity || output.used == capacity)) returned = !capacity ? -1 : -2;
        }
    }
    if (output.format_error && !windows_write(owner, owner->crt->error_number, 4, output.format_error, error)) return false;
    result(out, QA_NATIVE_I32, (uint32_t)returned); return true;
}

typedef struct scan_directive {
    char code, literal;
    uint64_t width;
    bool space, suppressed, long_float;
} scan_directive;

static bool scan_space(unsigned char c)
{ return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

static bool scan_parse(const char *format, scan_directive **out, size_t *count, qa_error *error)
{
    size_t capacity = 0; scan_directive *directives = NULL;
    for (size_t at = 0; format[at]; ) {
        char c = format[at++]; scan_directive directive = {.width = UINT64_C(9007199254740991)};
        if (scan_space((unsigned char)c)) directive.space = true;
        else if (c != '%') directive.literal = c;
        else if (format[at] == '%') { ++at; directive.literal = '%'; }
        else {
            if (format[at] == '*') { ++at; directive.suppressed = true; }
            if (format[at] >= '0' && format[at] <= '9') {
                directive.width = 0;
                do {
                    unsigned digit = (unsigned)(format[at++] - '0');
                    if (directive.width > (UINT64_C(9007199254740991) - digit) / 10) goto unsupported;
                    directive.width = directive.width * 10 + digit;
                } while (format[at] >= '0' && format[at] <= '9');
                if (!directive.width) goto unsupported;
            }
            if (format[at] == 'l') { ++at; directive.long_float = true; }
            c = format[at]; if (!c) goto unsupported; ++at;
            if (strchr("fFeEgG", c)) directive.code = 'f';
            else if ((c == 'd' || c == 'i') && !directive.long_float) directive.code = c;
            else goto unsupported;
        }
        if (!guest_grow((void **)&directives, &capacity, *count + 1, sizeof(*directives), error)) { free(directives); return false; }
        directives[(*count)++] = directive; continue;
unsupported:
        free(directives); return guest_fail(error, QA_ERROR_UNSUPPORTED, at, "unsupported Windows scanf conversion or width");
    }
    *out = directives; return true;
}

typedef struct scan_input { guest_windows *owner; uint64_t address, capacity, cursor; } scan_input;

static bool scan_peek(scan_input *input, unsigned char *out, qa_error *error)
{
    *out = 0;
    if (input->cursor >= input->capacity) return true;
    uint64_t value;
    if (!windows_read(input->owner, input->address + input->cursor, 1, &value, error)) return false;
    *out = (unsigned char)value; return true;
}

static int scan_digit(unsigned char c)
{ return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99; }

static bool scan_take(scan_input *input, char **token, size_t *count, size_t *capacity,
    unsigned char c, qa_error *error)
{
    if (!guest_grow((void **)token, capacity, *count + 2, 1, error)) return false;
    (*token)[(*count)++] = (char)c; (*token)[*count] = 0; ++input->cursor; return true;
}

static bool scanf_service(guest_windows *owner, const qa_native_value *args,
    qa_native_value *out, qa_error *error)
{
    if (integer(args + 4) || (integer(args) & ~UINT64_C(2)))
        return guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "unsupported Windows scanf locale/options");
    if (!integer(args + 1)) return guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows scanf input is null");
    char *format = NULL; scan_directive *directives = NULL; size_t count = 0;
    if (!text(owner, integer(args + 3), &format, error) || !scan_parse(format, &directives, &count, error)) { free(format); return false; }
    free(format); scan_input input = {owner, integer(args + 1), integer(args + 2), 0};
    uint64_t arguments = integer(args + 5); int32_t assigned = 0; bool converted = false, okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        scan_directive directive = directives[i]; unsigned char c;
        if (!scan_peek(&input, &c, error)) { okay = false; break; }
        if (directive.space || directive.code) {
            while (scan_space(c)) { ++input.cursor; if (!scan_peek(&input, &c, error)) { okay = false; break; } }
            if (!okay) break;
        }
        if (directive.space) continue;
        if (directive.literal) {
            if (c != (unsigned char)directive.literal) { if (!converted && !c) assigned = -1; break; }
            ++input.cursor; continue;
        }
        if (!c) { if (!converted) assigned = -1; break; }
        uint64_t start = input.cursor; char *token = NULL; size_t token_count = 0, token_capacity = 0;
        bool negative = c == '-';
        if (c == '+' || c == '-') {
            if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { free(token); okay = false; break; }
        }
        if (input.cursor - start >= directive.width) c = 0;
        bool valid = true; int base = 10; size_t digits = 0;
        if (directive.code == 'f') {
            if (c == 'i' || c == 'I' || c == 'n' || c == 'N') {
                while (((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) && token_count < 9) {
                    if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { okay = false; break; }
                    if (input.cursor - start >= directive.width) c = 0;
                }
                const char *word = token ? token + (negative || token[0] == '+') : "";
                bool nonfinite = strlen(word) >= 3 &&
                    (((word[0] | 32) == 'i' && (word[1] | 32) == 'n' && (word[2] | 32) == 'f') ||
                     ((word[0] | 32) == 'n' && (word[1] | 32) == 'a' && (word[2] | 32) == 'n'));
                if (okay && nonfinite) okay = guest_fail(error, QA_ERROR_UNSUPPORTED, input.address + start, "Windows scanf nonfinite text is not implemented");
                free(token); break;
            }
            while (c >= '0' && c <= '9') {
                ++digits;
                if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { okay = false; break; }
                if (input.cursor - start >= directive.width) c = 0;
            }
            const char *unsigned_token = token ? token + (negative || token[0] == '+') : "";
            if (okay && !strcmp(unsigned_token, "0") && (c == 'x' || c == 'X')) okay = guest_fail(error, QA_ERROR_UNSUPPORTED, input.address + start, "Windows scanf hexadecimal floating input is not implemented");
            if (okay && c == '.') {
                if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) okay = false;
                if (input.cursor - start >= directive.width) c = 0;
                while (okay && c >= '0' && c <= '9') {
                    ++digits;
                    if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { okay = false; break; }
                    if (input.cursor - start >= directive.width) c = 0;
                }
            }
            if (okay && (c == 'e' || c == 'E')) {
                if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) okay = false;
                if (input.cursor - start >= directive.width) c = 0;
                if (okay && (c == '+' || c == '-')) {
                    if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) okay = false;
                    if (input.cursor - start >= directive.width) c = 0;
                }
                size_t exponent_digits = 0;
                while (okay && c >= '0' && c <= '9') {
                    ++exponent_digits;
                    if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { okay = false; break; }
                    if (input.cursor - start >= directive.width) c = 0;
                }
                if (!exponent_digits) valid = false;
            }
            if (!digits) valid = false;
        } else {
            if (directive.code == 'i' && c == '0') {
                base = 8; ++digits;
                if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) okay = false;
                if (input.cursor - start >= directive.width) c = 0;
                if (okay && (c == 'x' || c == 'X')) {
                    base = 16; digits = 0;
                    if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) okay = false;
                    if (input.cursor - start >= directive.width) c = 0;
                }
            }
            while (okay && scan_digit(c) < base) {
                ++digits;
                if (!scan_take(&input, &token, &token_count, &token_capacity, c, error) || !scan_peek(&input, &c, error)) { okay = false; break; }
                if (input.cursor - start >= directive.width) c = 0;
            }
            if (!digits) valid = false;
        }
        if (!okay || !valid) { free(token); break; }
        converted = true;
        if (!directive.suppressed) {
            uint64_t destination;
            if (!va_next(owner, &arguments, owner->target.pointer_bytes, &destination, error)) { free(token); okay = false; break; }
            if (!destination) { free(token); okay = guest_fail(error,QA_ERROR_ARGUMENT,0,"Windows scanf destination is null"); break; }
            if (directive.code == 'f') {
                fenv_t saved; bool held = feholdexcept(&saved) == 0;
                if (!held || fesetround(FE_TONEAREST) != 0) { free(token); okay = guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "host cannot provide nearest decimal scan rounding"); if (held) fesetenv(&saved); break; }
                uint64_t bits = 0;
                if (directive.long_float) { double value; okay = qa_parse_atof(token, &value, error); if (okay) memcpy(&bits, &value, 8); }
                else { float value; uint32_t encoded; okay = qa_parse_atof_float(token, &value, error); if (okay) { memcpy(&encoded, &value, 4); bits = encoded; } }
                if (fesetenv(&saved) != 0) okay = guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "host decimal scan environment restore failed");
                if (okay) okay = windows_write(owner, destination, directive.long_float ? 8 : 4, bits, error);
            } else {
                const char *at = token + (negative || token[0] == '+');
                if (base == 16) at += 2;
                uint32_t value = 0; while (*at) { value = value * (uint32_t)base + (uint32_t)scan_digit((unsigned char)*at++); }
                if (negative) value = 0u - value;
                okay = windows_write(owner, destination, 4, value, error);
            }
            if (okay) ++assigned;
        }
        free(token);
    }
    free(directives); result(out, QA_NATIVE_I32, (uint32_t)assigned); return okay;
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
