#include "sysv_libc_private.h"
#include "qa/text.h"
#include <fenv.h>

enum libc_operation {
    LC_ERRNO = 1, LC_TLS, LC_MALLOC, LC_CALLOC, LC_FREE, LC_REALLOC,
    LC_COPY, LC_SET, LC_COMPARE, LC_LENGTH, LC_STRING_COMPARE, LC_STRING_COPY,
    LC_STRING_NCOPY, LC_CHARACTER, LC_SUBSTRING, LC_TOKEN, LC_STRTOL, LC_TIME,
    LC_SORT, LC_MATH, LC_ATAN2, LC_SINCOS, LC_ISNAN, LC_STACK, LC_RAND, LC_SRAND, LC_STRTOD
};
static uint32_t random_step(uint8_t state[128])
{
    uint32_t rear = qa_load_u32le(state), front = (rear + 3) % 31;
    uint32_t sum = qa_load_u32le(state + 4 + front * 4) +
        qa_load_u32le(state + 4 + rear * 4);
    qa_store_u32le(state + 4 + front * 4, sum);
    qa_store_u32le(state, (rear + 1) % 31);
    return sum >> 1;
}
static bool random_seed(guest_sysv_runtime *r, uint64_t address, uint32_t seed, qa_error *error)
{
    uint8_t state[128] = {0};
    if (!seed) seed = 1;
    qa_store_u32le(state + 4, seed);
    int64_t word = seed <= INT32_MAX ? (int64_t)seed : (int64_t)seed - INT64_C(0x100000000);
    for (size_t i = 1; i < 31; ++i) {
        word = 16807 * (word % 127773) - 2836 * (word / 127773);
        if (word < 0) word += INT32_MAX;
        qa_store_u32le(state + 4 + i * 4, (uint32_t)word);
    }
    for (size_t i = 0; i < 310; ++i) (void)random_step(state);
    return sysv_write(r, address, state, sizeof(state), error);
}
static bool add(guest_sysv_runtime *r, uint32_t operation, uint64_t flags,
    const char *library, const char *name, const char *const *versions,
    size_t version_count, const qa_native_value_type *types, size_t count,
    qa_native_value_type result, qa_error *error)
{
    return sysv_service_add(r, SYSV_LIBC, operation, flags, 0, 0, library,
        name, versions, version_count, types, count, result, NULL, NULL, error);
}
bool sysv_libc_install(guest_sysv_runtime *r, qa_error *error)
{
    const char *base = r->target.pointer_bytes == 4 ? "GLIBC_2.0" : "GLIBC_2.2.5";
    const char *versions[] = {base, NULL}, *checked[] = {"GLIBC_2.3.4", NULL};
    const char *tls[] = {"GLIBC_2.3", NULL}, *memcpy_versions[] = {base, NULL, "GLIBC_2.14"};
    const char *stack[] = {"GLIBC_2.4", NULL}, *atan[] = {"GLIBC_2.15", NULL};
    const char *sincos_versions[] = {r->target.pointer_bytes == 4 ? "GLIBC_2.1" : base, NULL};
    qa_native_value_type p = QA_NATIVE_ADDRESS, z = sysv_size_type(r), s = sysv_signed_type(r);
    const qa_native_value_type zp[] = {z}, zz[] = {z,z}, pp[] = {p,p}, pz[] = {p,z};
    const qa_native_value_type ppz[] = {p,p,z}, ppzz[] = {p,p,z,z}, piz[] = {p,QA_NATIVE_I32,z};
    const qa_native_value_type pi[] = {p,QA_NATIVE_I32}, ppi[] = {p,p,QA_NATIVE_I32}, pzzp[] = {p,z,z,p};
    uint64_t random_state;
    qa_native_value_type seed_type = QA_NATIVE_U32;
    if (!sysv_allocate(r, 128, false, &random_state, error) ||
        !random_seed(r, random_state, 1, error) ||
        !add(r, LC_RAND, random_state, "libc.so.6", "rand", versions, 2,
            NULL, 0, QA_NATIVE_I32, error) ||
        !add(r, LC_SRAND, random_state, "libc.so.6", "srand", versions, 2,
            &seed_type, 1, QA_NATIVE_VOID, error)) return false;
    if (!sysv_format_install(r, error) ||
        !add(r, LC_ERRNO, 0, "libc.so.6", "__errno_location", versions, 2, NULL, 0, p, error) ||
        (r->target.pointer_bytes == 8 && !add(r, LC_TLS, 0, "ld-linux-x86-64.so.2", "__tls_get_addr", tls, 2, &p, 1, p, error)) ||
        !add(r, LC_MALLOC, 0, "libc.so.6", "malloc", versions, 2, zp, 1, p, error) ||
        !add(r, LC_CALLOC, 0, "libc.so.6", "calloc", versions, 2, zz, 2, p, error) ||
        !add(r, LC_FREE, 0, "libc.so.6", "free", versions, 2, &p, 1, QA_NATIVE_VOID, error) ||
        !add(r, LC_REALLOC, 0, "libc.so.6", "realloc", versions, 2, pz, 2, p, error)) return false;
    const char *copy_names[] = {"memcpy", "memmove", "__memcpy_chk", "__memmove_chk"};
    for (size_t i = 0; i < 4; ++i)
        if (!add(r, LC_COPY, i >= 2, "libc.so.6", copy_names[i], i >= 2 ? checked :
            i == 0 && r->target.pointer_bytes == 8 ? memcpy_versions : versions,
            i == 0 && r->target.pointer_bytes == 8 ? 3 : 2, i >= 2 ? ppzz : ppz,
            i >= 2 ? 4 : 3, p, error)) return false;
    if (!add(r, LC_SET, 0, "libc.so.6", "memset", versions, 2, piz, 3, p, error) ||
        !add(r, LC_COMPARE, 0, "libc.so.6", "memcmp", versions, 2, ppz, 3, QA_NATIVE_I32, error) ||
        !add(r, LC_LENGTH, 0, "libc.so.6", "strlen", versions, 2, &p, 1, z, error) ||
        !add(r, LC_STRING_COMPARE, 0, "libc.so.6", "strcmp", versions, 2, pp, 2, QA_NATIVE_I32, error)) return false;
    const char *string_names[] = {"strcpy", "stpcpy", "strcat", "__strcpy_chk", "__stpcpy_chk", "__strcat_chk"};
    for (size_t i = 0; i < 6; ++i) {
        uint64_t flags = (i >= 3 ? 1 : 0) | (i % 3 == 2 ? 2 : 0) | (i % 3 == 1 ? 4 : 0);
        if (!add(r, LC_STRING_COPY, flags, "libc.so.6", string_names[i], i >= 3 ? checked : versions,
            2, i >= 3 ? ppz : pp, i >= 3 ? 3 : 2, p, error)) return false;
    }
    if (!add(r, LC_STRING_NCOPY, 0, "libc.so.6", "strncpy", versions, 2, ppz, 3, p, error) ||
        !add(r, LC_CHARACTER, 0, "libc.so.6", "strchr", versions, 2, pi, 2, p, error) ||
        !add(r, LC_CHARACTER, 1, "libc.so.6", "strrchr", versions, 2, pi, 2, p, error) ||
        !add(r, LC_SUBSTRING, 0, "libc.so.6", "strstr", versions, 2, pp, 2, p, error) ||
        !add(r, LC_SUBSTRING, 1, "libc.so.6", "strpbrk", versions, 2, pp, 2, p, error) ||
        !sysv_allocate(r, r->target.pointer_bytes, false, &r->strtok_slot, error) ||
        !add(r, LC_TOKEN, 0, "libc.so.6", "strtok", versions, 2, pp, 2, p, error) ||
        !add(r, LC_STRTOL, 0, "libc.so.6", "strtol", versions, 2, ppi, 3, s, error) ||
        !add(r, LC_STRTOD, 0, "libc.so.6", "strtod", versions, 2, pp, 2, QA_NATIVE_F64, error) ||
        !add(r, LC_TIME, 0, "libc.so.6", "time", versions, 2, &p, 1, s, error) ||
        !add(r, LC_SORT, 0, "libc.so.6", "qsort", versions, 2, pzzp, 4, QA_NATIVE_VOID, error)) return false;
    const char *math_names[] = {"sin", "cos", "ceil", "floor", "sqrt", "fabs"};
    for (size_t i = 0; i < 6; ++i) for (size_t f = 0; f < 2; ++f) {
        char name[8]; size_t bytes = strlen(math_names[i]);
        memcpy(name, math_names[i], bytes); if (!f) name[bytes++] = 'f'; name[bytes] = 0;
        qa_native_value_type type = f ? QA_NATIVE_F64 : QA_NATIVE_F32;
        if (!add(r, LC_MATH, i, "libm.so.6", name, versions, 2, &type, 1, type, error)) return false;
    }
    const qa_native_value_type dd[] = {QA_NATIVE_F64, QA_NATIVE_F64};
    if (!add(r, LC_ATAN2, 0, "libm.so.6", "__atan2_finite", atan, 2, dd, 2, QA_NATIVE_F64, error)) return false;
    for (size_t f = 0; f < 2; ++f) {
        qa_native_value_type types[] = {f ? QA_NATIVE_F64 : QA_NATIVE_F32,p,p};
        if (!add(r, LC_SINCOS, 0, "libm.so.6", f ? "sincos" : "sincosf", sincos_versions,
            2, types, 3, QA_NATIVE_VOID, error)) return false;
    }
    qa_native_value_type float_type = QA_NATIVE_F32;
    return add(r, LC_ISNAN, 0, "libc.so.6", "__isnanf", versions, 2, &float_type, 1, QA_NATIVE_I32, error) &&
        add(r, LC_STACK, 0, "libc.so.6", "__stack_chk_fail", stack, 2, NULL, 0, QA_NATIVE_VOID, error);
}
static bool extent(uint64_t value, size_t *out, qa_error *error)
{
    if (value > SYSV_MAX_ALLOCATION || value > SIZE_MAX)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V memory count exceeds its supported actual extent");
    *out = (size_t)value; return true;
}
static bool memory_copy(guest_sysv_runtime *r, uint64_t destination,
    uint64_t source, size_t count, qa_error *error)
{
    if (!count) return true;
    if (!destination || !source)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V memory copy requires actual nonnull pointers");
    uint8_t *bytes = malloc(count);
    if (!bytes) return sysv_fail(error, QA_ERROR_MEMORY, "copying System V guest bytes");
    bool ok = sysv_read(r, source, bytes, count, error) && sysv_write(r, destination, bytes, count, error);
    free(bytes); return ok;
}
static bool full_string(guest_sysv_runtime *r, uint64_t address, qa_buffer *out, qa_error *error)
{
    bool terminated;
    if (!address) return sysv_fail(error, QA_ERROR_ARGUMENT, "System V string requires a nonnull pointer");
    if (!sysv_string(r, address, SYSV_MAX_STRING, out, &terminated, error)) return false;
    if (!terminated) { qa_buffer_free(out); return sysv_fail(error, QA_ERROR_ARGUMENT, "System V string exceeds supported terminated extent"); }
    return true;
}
static bool set_integer(qa_native_value *out, qa_native_value_type type, uint64_t value)
{
    out->type = type;
    if (type == QA_NATIVE_ADDRESS) out->as.address = value;
    else if (type == QA_NATIVE_U32) out->as.u32 = (uint32_t)value;
    else if (type == QA_NATIVE_U64) out->as.u64 = value;
    else if (type == QA_NATIVE_I32) {
        uint32_t bits = (uint32_t)value;
        out->as.i32 = bits <= INT32_MAX ? (int32_t)bits : -(int32_t)(UINT32_MAX - bits) - 1;
    } else if (type == QA_NATIVE_I64)
        out->as.i64 = value <= INT64_MAX ? (int64_t)value : -(int64_t)(UINT64_MAX - value) - 1;
    return true;
}
static int digit(uint8_t byte)
{
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'A' && byte <= 'Z') return byte - 'A' + 10;
    if (byte >= 'a' && byte <= 'z') return byte - 'a' + 10;
    return -1;
}
static bool strtol_call(guest_sysv_runtime *r, const qa_native_value *args,
    qa_native_value *out, qa_error *error)
{
    uint64_t source = args[0].as.address, end = args[1].as.address;
    qa_buffer text = {0};
    if (!full_string(r, source, &text, error)) return false;
    int radix = args[2].as.i32;
    if (radix && (radix < 2 || radix > 36)) {
        qa_buffer_free(&text);
        if (!sysv_errno(r, 22, error) || (end && !sysv_put_pointer(r, end, source, error))) return false;
        return set_integer(out, sysv_signed_type(r), 0);
    }
    size_t at = 0;
    while (at < text.size && (text.data[at] == ' ' || (text.data[at] >= 9 && text.data[at] <= 13))) ++at;
    bool negative = false;
    if (at < text.size && (text.data[at] == '-' || text.data[at] == '+')) negative = text.data[at++] == '-';
    if ((!radix || radix == 16) && at + 2 < text.size && text.data[at] == '0' &&
        (text.data[at + 1] == 'x' || text.data[at + 1] == 'X') && digit(text.data[at + 2]) >= 0 && digit(text.data[at + 2]) < 16) {
        radix = 16; at += 2;
    }
    if (!radix) radix = at < text.size && text.data[at] == '0' ? 8 : 10;
    size_t first = at;
    uint64_t limit = UINT64_C(1) << (r->target.pointer_bytes * 8 - 1), value = 0;
    bool overflow = false;
    for (; at < text.size; ++at) {
        int d = digit(text.data[at]);
        if (d < 0 || d >= radix) break;
        if (!overflow) {
            if (value > (limit - (uint64_t)d) / (unsigned)radix) overflow = true;
            else value = value * (unsigned)radix + (unsigned)d;
        }
    }
    qa_buffer_free(&text);
    if (end && !sysv_put_pointer(r, end, source + (at == first ? 0 : at), error)) return false;
    if (overflow || (!negative && value >= limit)) {
        if (!sysv_errno(r, 34, error)) return false;
        value = negative ? limit : limit - 1;
    }
    return set_integer(out, sysv_signed_type(r), negative ? UINT64_C(0) - value : value);
}
typedef struct sort_state { guest_sysv_runtime *runtime; uint64_t base, comparator; size_t size; } sort_state;
static bool sort_less(sort_state *state, size_t a, size_t b, bool *out, qa_error *error)
{
    const qa_native_value_type types[] = {QA_NATIVE_ADDRESS,QA_NATIVE_ADDRESS};
    qa_native_value arguments[] = {{.type = QA_NATIVE_ADDRESS, .as.address = state->base + a * state->size},
        {.type = QA_NATIVE_ADDRESS, .as.address = state->base + b * state->size}}, result = {.type = QA_NATIVE_I32};
    if (!sysv_invoke(state->runtime, state->comparator, types, 2, QA_NATIVE_I32, arguments, &result, error)) return false;
    *out = result.as.i32 < 0; return true;
}
static bool sort_swap(sort_state *state, size_t a, size_t b, qa_error *error)
{
    uint8_t *bytes = malloc(state->size);
    if (!bytes) return sysv_fail(error, QA_ERROR_MEMORY, "holding qsort reached swap bytes");
    uint64_t first = state->base + a * state->size, second = state->base + b * state->size;
    bool ok = sysv_read(state->runtime, first, bytes, state->size, error) &&
        memory_copy(state->runtime, first, second, state->size, error) &&
        sysv_write(state->runtime, second, bytes, state->size, error);
    free(bytes); return ok;
}
static bool sort_sift(sort_state *state, size_t root, size_t end, qa_error *error)
{
    for (size_t child = root * 2 + 1; child < end; child = root * 2 + 1) {
        bool less;
        if (child + 1 < end) {
            if (!sort_less(state, child, child + 1, &less, error)) return false;
            if (less) ++child;
        }
        if (!sort_less(state, root, child, &less, error)) return false;
        if (!less) break;
        if (!sort_swap(state, root, child, error)) return false;
        root = child;
    }
    return true;
}
bool sysv_libc_call(sysv_service *service, const qa_native_value *args,
    qa_native_value *out, qa_error *error)
{
    guest_sysv_runtime *r = service->runtime;
    out->type = service->result.kind;
    uint64_t a = service->parameter_count ? sysv_integer(args) : 0;
    uint64_t b = service->parameter_count > 1 ? sysv_integer(args + 1) : 0;
    uint64_t c = service->parameter_count > 2 ? sysv_integer(args + 2) : 0;
    size_t count;
    switch (service->operation) {
    case LC_RAND: {
        uint8_t state[128];
        if (!sysv_read(r, service->a, state, sizeof(state), error)) return false;
        if (qa_load_u32le(state) >= 31)
            return sysv_fail(error, QA_ERROR_FORMAT, "System V random state has an invalid ring cursor");
        out->as.i32 = (int32_t)random_step(state);
        return sysv_write(r, service->a, state, sizeof(state), error);
    }
    case LC_SRAND: return random_seed(r, service->a, (uint32_t)a, error);
    case LC_ERRNO: out->as.address = r->errno_address; return true;
    case LC_TLS: {
        uint64_t module, offset;
        if (!a || a > UINT64_MAX - 8) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V TLS index pointer is null or overflowing");
        return sysv_unsigned(r, a, 8, &module, error) && sysv_unsigned(r, a + 8, 8, &offset, error) &&
            guest_sysv_tls_address(r, module, offset, &out->as.address, error);
    }
    case LC_MALLOC: return sysv_allocate(r, a, true, &out->as.address, error);
    case LC_CALLOC:
        if (a && b > SYSV_MAX_ALLOCATION / a) { out->as.address = 0; return sysv_errno(r, 12, error); }
        return sysv_allocate(r, a * b, true, &out->as.address, error);
    case LC_FREE: return sysv_free(r, a, error);
    case LC_REALLOC: {
        uint64_t old_size = 0;
        if (!b && a) { out->as.address = 0; return sysv_free(r, a, error); }
        if (a && !sysv_allocation_size(r, a, &old_size, error)) return false;
        uint64_t next;
        if (!sysv_allocate(r, b, true, &next, error)) return false;
        if (a && (!memory_copy(r, next, a, (size_t)(b < old_size ? b : old_size), error) || !sysv_free(r, a, error))) return false;
        out->as.address = next; return true;
    }
    case LC_COPY:
        if (!extent(c, &count, error) || (service->a && c > sysv_integer(args + 3)))
            return sysv_fail(error, QA_ERROR_ARGUMENT, "fortified System V memory copy exceeds its actual destination");
        out->as.address = a; return memory_copy(r, a, b, count, error);
    case LC_SET: {
        if (!extent(c, &count, error)) return false;
        out->as.address = a; if (!count) return true;
        if (!a) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V memset requires an actual nonnull destination");
        uint8_t *bytes = malloc(count);
        if (!bytes) return sysv_fail(error, QA_ERROR_MEMORY, "holding memset guest bytes");
        memset(bytes, (uint8_t)b, count);
        bool ok = sysv_write(r, a, bytes, count, error); free(bytes); return ok;
    }
    case LC_COMPARE: {
        if (!extent(c, &count, error)) return false;
        out->as.i32 = 0; if (!count) return true;
        if (!a || !b) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V memcmp requires actual nonnull pointers");
        uint8_t *first = malloc(count), *second = malloc(count);
        if (!first || !second) { free(first); free(second); return sysv_fail(error, QA_ERROR_MEMORY, "holding memcmp guest bytes"); }
        bool ok = sysv_read(r, a, first, count, error) && sysv_read(r, b, second, count, error);
        if (ok) for (size_t i = 0; i < count; ++i)
            if (first[i] != second[i]) { out->as.i32 = (int)first[i] - (int)second[i]; break; }
        free(first); free(second); return ok;
    }
    case LC_LENGTH: {
        qa_buffer text = {0}; if (!full_string(r, a, &text, error)) return false;
        set_integer(out, sysv_size_type(r), text.size); qa_buffer_free(&text); return true;
    }
    case LC_STRING_COMPARE: {
        qa_buffer first = {0}, second = {0};
        bool ok = full_string(r, a, &first, error) && full_string(r, b, &second, error);
        out->as.i32 = 0;
        if (ok) {
            size_t n = first.size < second.size ? first.size : second.size, i = 0;
            for (; i < n; ++i) if (first.data[i] != second.data[i]) { out->as.i32 = first.data[i] - second.data[i]; break; }
            if (i == n && first.size != second.size) out->as.i32 = first.size < second.size ? -(int)second.data[n] : first.data[n];
        }
        qa_buffer_free(&first); qa_buffer_free(&second); return ok;
    }
    case LC_STRING_COPY: {
        qa_buffer text = {0}, prefix = {0};
        if (!a) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V string copy requires an actual nonnull destination");
        bool ok = full_string(r, b, &text, error);
        if (ok && (service->a & 2)) ok = full_string(r, a, &prefix, error);
        uint64_t total = prefix.size + text.size + 1;
        if (ok && (service->a & 1) && total > c) ok = sysv_fail(error, QA_ERROR_ARGUMENT, "fortified System V string copy exceeds its actual destination");
        if (ok) {
            uint8_t *bytes = realloc(text.data, text.size + 1);
            if (!bytes) ok = sysv_fail(error, QA_ERROR_MEMORY, "terminating copied System V string");
            else { text.data = bytes; bytes[text.size] = 0;
                ok = a <= UINT64_MAX-prefix.size && sysv_write(r, a + prefix.size, bytes, text.size + 1, error); }
        }
        out->as.address = a + ((service->a & 4) ? text.size : 0);
        qa_buffer_free(&text); qa_buffer_free(&prefix); return ok;
    }
    case LC_STRING_NCOPY: {
        if (!extent(c, &count, error)) return false;
        out->as.address = a; if (!count) return true;
        if (!a || !b) return sysv_fail(error, QA_ERROR_ARGUMENT, "strncpy requires actual nonnull source and destination");
        uint8_t *bytes = calloc(count, 1);
        if (!bytes) return sysv_fail(error, QA_ERROR_MEMORY, "holding zero-padded strncpy bytes");
        bool ok = true;
        for (size_t i = 0; i < count; ++i) {
            if (b > UINT64_MAX-i || !sysv_read(r, b + i, bytes + i, 1, error)) { ok = false; break; }
            if (!bytes[i]) break;
        }
        if (ok) ok = sysv_write(r, a, bytes, count, error);
        free(bytes); return ok;
    }
    case LC_CHARACTER: {
        qa_buffer text = {0}; if (!full_string(r, a, &text, error)) return false;
        size_t found = SIZE_MAX;
        for (size_t i = 0; i <= text.size; ++i) if ((i == text.size ? 0 : text.data[i]) == (uint8_t)b) {
            found = i; if (!service->a) break;
        }
        out->as.address = found == SIZE_MAX ? 0 : a + found; qa_buffer_free(&text); return true;
    }
    case LC_SUBSTRING: {
        qa_buffer text = {0}, needle = {0};
        bool ok = full_string(r, a, &text, error) && full_string(r, b, &needle, error);
        size_t found = SIZE_MAX;
        if (ok) for (size_t i = 0; i <= text.size; ++i) {
            if (service->a) { if (i < text.size && needle.size && memchr(needle.data, text.data[i], needle.size)) { found = i; break; } }
            else if (needle.size <= text.size - i && (!needle.size || !memcmp(text.data + i, needle.data, needle.size))) { found = i; break; }
        }
        out->as.address = found == SIZE_MAX ? 0 : a + found;
        qa_buffer_free(&text); qa_buffer_free(&needle); return ok;
    }
    case LC_TOKEN: {
        qa_buffer text = {0}, delimiters = {0};
        if (!a && !sysv_pointer(r, r->strtok_slot, &a, error)) return false;
        if (!full_string(r, b, &delimiters, error)) return false;
        if (!a) { qa_buffer_free(&delimiters); out->as.address = 0; return true; }
        if (!full_string(r, a, &text, error)) { qa_buffer_free(&delimiters); return false; }
        size_t first = 0;
        while (first < text.size && delimiters.size && memchr(delimiters.data, text.data[first], delimiters.size)) ++first;
        size_t last = first;
        while (last < text.size && (!delimiters.size || !memchr(delimiters.data, text.data[last], delimiters.size))) ++last;
        bool ok = true;
        if (last < text.size) ok = sysv_store(r, a + last, 1, 0, error);
        if (ok) ok = sysv_put_pointer(r, r->strtok_slot, last < text.size ? a + last + 1 : 0, error);
        out->as.address = first == text.size ? 0 : a + first;
        qa_buffer_free(&text); qa_buffer_free(&delimiters); return ok;
    }
    case LC_STRTOL: return strtol_call(r, args, out, error);
    case LC_STRTOD: {
        qa_buffer text = {0};
        qa_native_guest_cpu cpu;
        if ((b && !guest_range(r->guest,b,r->target.pointer_bytes,QA_NATIVE_GUEST_WRITE,error)) ||
            !full_string(r,a,&text,error) || !qa_native_guest_cpu_read(r->guest,&cpu,error)) {
            qa_buffer_free(&text); return false;
        }
        uint8_t *terminated = realloc(text.data,text.size+1);
        if (!terminated) { qa_buffer_free(&text); return sysv_fail(error,QA_ERROR_MEMORY,"terminating System V strtod input"); }
        text.data = terminated; text.data[text.size] = 0;
        const int modes[] = {FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
        unsigned rounding = r->target.pointer_bytes == 8 ? (cpu.mxcsr>>13)&3 : (cpu.fp_control>>10)&3;
        fenv_t saved;
        if (feholdexcept(&saved) != 0) {
            qa_buffer_free(&text); return sysv_fail(error,QA_ERROR_UNSUPPORTED,"host cannot retain the System V strtod floating environment");
        }
        bool ok = fesetround(modes[rounding]) == 0, range_error = false;
        size_t consumed = 0;
        if (ok) ok = qa_parse_strtod((const char *)text.data,&out->as.f64,&consumed,&range_error,error);
        else sysv_fail(error,QA_ERROR_UNSUPPORTED,"host cannot select System V strtod rounding");
        int exceptions = fetestexcept(FE_ALL_EXCEPT);
        if (fesetenv(&saved) != 0) ok = sysv_fail(error,QA_ERROR_UNSUPPORTED,"host cannot restore the System V strtod floating environment");
        qa_buffer_free(&text);
        if (!ok) return false;
        unsigned sticky = (exceptions & FE_INVALID ? 1u : 0u) |
            (exceptions & FE_DIVBYZERO ? 4u : 0u) | (exceptions & FE_OVERFLOW ? 8u : 0u) |
            (exceptions & FE_UNDERFLOW ? 16u : 0u) | (exceptions & FE_INEXACT ? 32u : 0u);
        if (sticky) {
            if (r->target.pointer_bytes == 8) cpu.mxcsr |= sticky;
            else cpu.fp_status |= (uint16_t)sticky;
            if (!qa_native_guest_cpu_write(r->guest,&cpu,error)) return false;
        }
        return (!range_error || sysv_errno(r,34,error)) &&
            (!b || sysv_put_pointer(r,b,a+consumed,error));
    }
    case LC_TIME: {
        int64_t seconds;
        if (!r->options.bindings.clock_id || !r->options.bindings.time)
            return sysv_fail(error, QA_ERROR_UNSUPPORTED, "System V time has no deterministic clock capability");
        if (!r->options.bindings.time(r->options.bindings.context, &seconds, error) || !sysv_current(r, error) ||
            (a && !sysv_store(r, a, r->target.pointer_bytes, (uint64_t)seconds, error))) return false;
        return set_integer(out, sysv_signed_type(r), (uint64_t)seconds);
    }
    case LC_SORT: {
        uint64_t comparator = args[3].as.address;
        if (!comparator) return sysv_fail(error, QA_ERROR_ARGUMENT, "qsort requires an actual comparator even for an empty range");
        if (!extent(b, &count, error) || c > SYSV_MAX_ALLOCATION) return false;
        if (count < 2 || !c) return true;
        if (!a || c > SYSV_MAX_ALLOCATION / count || !guest_range(r->guest, a, count * (size_t)c, QA_NATIVE_GUEST_WRITE, error)) return false;
        sort_state state = {r,a,comparator,(size_t)c};
        for (size_t i = count / 2; i; --i) if (!sort_sift(&state, i - 1, count, error)) return false;
        for (size_t end = count - 1; end; --end)
            if (!sort_swap(&state, 0, end, error) || !sort_sift(&state, 0, end, error)) return false;
        return true;
    }
    case LC_MATH: {
        double value = args[0].type == QA_NATIVE_F32 ? args[0].as.f32 : args[0].as.f64;
        double result = service->a == 0 ? sin(value) : service->a == 1 ? cos(value) :
            service->a == 2 ? ceil(value) : service->a == 3 ? floor(value) : service->a == 4 ? sqrt(value) : fabs(value);
        if (out->type == QA_NATIVE_F32) out->as.f32 = (float)result; else out->as.f64 = result;
        return true;
    }
    case LC_ATAN2: out->as.f64 = atan2(args[0].as.f64, args[1].as.f64); return true;
    case LC_SINCOS: {
        if (!args[1].as.address || !args[2].as.address)
            return sysv_fail(error,QA_ERROR_ARGUMENT,"sincos requires both actual nonnull output pointers");
        double value = args[0].type == QA_NATIVE_F32 ? args[0].as.f32 : args[0].as.f64;
        uint8_t bytes[8]; size_t width = args[0].type == QA_NATIVE_F32 ? 4 : 8;
        double sine = sin(value), cosine = cos(value);
        if (width == 4) { float f = (float)sine; uint32_t bits; memcpy(&bits, &f, 4); qa_store_u32le(bytes, bits); }
        else { uint64_t bits; memcpy(&bits, &sine, 8); qa_store_u64le(bytes, bits); }
        if (!sysv_write(r, args[1].as.address, bytes, width, error)) return false;
        if (width == 4) { float f = (float)cosine; uint32_t bits; memcpy(&bits, &f, 4); qa_store_u32le(bytes, bits); }
        else { uint64_t bits; memcpy(&bits, &cosine, 8); qa_store_u64le(bytes, bits); }
        return sysv_write(r, args[2].as.address, bytes, width, error);
    }
    case LC_ISNAN: out->as.i32 = isnan(args[0].as.f32); return true;
    case LC_STACK: return sysv_fail(error, QA_ERROR_UNSUPPORTED, "System V guest stack protection failure");
    default: return sysv_fail(error, QA_ERROR_FORMAT, "unknown retained System V libc operation");
    }
}
