#include "sysv_scan.h"
#include "scan.h"

typedef struct scan_entry {
    qa_native_guest *guest;
    guest_abi_plan *plan;
    size_t next;
} scan_entry;
static bool destination(void *context, uint64_t *out, qa_error *error)
{
    scan_entry *entry = context;
    qa_native_value value = {0}; qa_buffer storage = {0};
    bool okay = guest_abi_decode_argument(entry->plan, entry->guest,
        entry->next++, &value, &storage, error);
    if (okay) *out = value.as.address;
    qa_buffer_free(&storage);
    return okay;
}
bool sysv_scan_install(guest_sysv_runtime *runtime, qa_error *error)
{
    const qa_native_value_type parameters[] = {QA_NATIVE_ADDRESS, QA_NATIVE_ADDRESS};
    const char *base = runtime->target.pointer_bytes == 4 ? "GLIBC_2.0" : "GLIBC_2.2.5";
    const char *const names[] = {"sscanf", "__isoc99_sscanf", "__isoc23_sscanf"};
    const char *const versions[] = {base, "GLIBC_2.7", "GLIBC_2.38"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        const char *available[] = {versions[i], NULL};
        if (!sysv_service_add(runtime, SYSV_FORMAT, i == 2 ? 5 : 4, 0, 0, 0,
            "libc.so.6", names[i], available, 2, parameters, 2, QA_NATIVE_I32,
            NULL, NULL, error)) return false;
    }
    return true;
}
bool sysv_scan_call(sysv_service *service, const qa_native_value *arguments,
    qa_native_value *out, qa_error *error)
{
    guest_sysv_runtime *runtime = service->runtime;
    guest_scan_program *program = NULL;
    if (!guest_scan_prepare(runtime->guest, arguments[1].as.address,
        service->operation == 5, &program, error)) return false;
    size_t count = guest_scan_argument_count(program), width = runtime->target.pointer_bytes;
    guest_abi_layout *extra = count ? calloc(count, sizeof(*extra)) : NULL;
    if (count && !extra) {
        guest_scan_destroy(program);
        return sysv_fail(error, QA_ERROR_MEMORY, "Preparing scanner destination ABI layouts");
    }
    for (size_t i = 0; i < count; ++i)
        extra[i] = (guest_abi_layout){.kind = QA_NATIVE_ADDRESS, .bytes = width, .alignment = width};
    const qa_native_type fixed[] = {{.kind = QA_NATIVE_ADDRESS, .count = 1},
        {.kind = QA_NATIVE_ADDRESS, .count = 1}};
    qa_native_signature signature = {.abi = runtime->target.abi, .parameters = fixed,
        .parameter_count = 2, .result = {.kind = QA_NATIVE_I32, .count = 1}, .variadic = true};
    scan_entry entry = {.guest = runtime->guest, .next = 2};
    bool okay = guest_abi_plan_native(&signature, extra, count, &entry.plan, error);
    free(extra);
    int32_t assigned = 0;
    if (okay) okay = guest_scan_execute(program, runtime->guest, arguments[0].as.address,
        UINT64_MAX, destination, &entry, &assigned, error);
    if (okay) *out = (qa_native_value){.type = QA_NATIVE_I32, .as.i32 = assigned};
    guest_abi_plan_destroy(entry.plan);
    guest_scan_destroy(program);
    return okay;
}
