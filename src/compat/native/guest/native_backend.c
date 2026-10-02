#include "internal.h"

qa_native_guest_backend qa_native_guest_execution(const qa_native_guest *guest)
{ return guest ? guest->options.backend : QA_NATIVE_GUEST_EMULATED; }

bool guest_native_result(qa_native_guest *guest, bool okay, qa_error *error)
{
    (void)error;
    if (!okay) guest->failed = true;
    return okay;
}

bool guest_native_map(qa_native_guest *guest,
    const qa_native_guest_mapping *mapping, qa_error *error)
{
    guest_backing *backing = guest_backing_at(guest, mapping->backing);
    if (!backing->child_owned) {
        guest_host_backing_view input = {.id = backing->id,
            .bytes = {backing->data, backing->bytes}, .file = backing->file,
            .source = backing->source};
        if (!guest_native_result(guest,
            guest_host_child_backing(guest->child, &input, error), error)) return false;
        guest_host_backing_view actual = {0}; bool found = false;
        for (size_t i = 0; i < guest_host_child_backing_count(guest->child); ++i) {
            if (!guest_host_child_backing_at(guest->child, i, &actual, error)) {
                guest->failed = true; return false;
            }
            if (actual.id == backing->id) { found = true; break; }
        }
        if (!found || !actual.bytes.data || actual.bytes.size != backing->bytes ||
            actual.file != backing->file || (backing->file &&
            (actual.source.bytes != backing->source.bytes ||
             actual.source.offset != backing->source.offset ||
             actual.source.accessible_bytes != backing->source.accessible_bytes ||
             !qa_sha256_equal(&actual.source.digest, &backing->source.digest)))) {
            guest->failed = true;
            return guest_fail(error, QA_ERROR_FORMAT, backing->id,
                "native child backing differs from its actual owner");
        }
        free(backing->data);
        backing->data = (uint8_t *)actual.bytes.data;
        backing->child_owned = true;
    }
    return guest_native_result(guest,
        guest_host_child_map(guest->child, mapping, error), error);
}

bool guest_backing_retire(qa_native_guest *guest, guest_backing *backing,
    qa_error *error)
{
    if (backing->child_owned) {
        if (!guest_native_result(guest,
            guest_host_child_backing_remove(guest->child, backing->id, error), error)) return false;
    } else free(backing->data);
    backing->data = NULL;
    return true;
}

bool guest_native_transfer(qa_native_guest *guest, qa_native_guest_cpu *cpu,
    bool writing, qa_error *error)
{
    guest_host_x86_64_state retained = {0}, next = {0};
    const guest_host_x86_64_capabilities *capability =
        guest_host_child_capability(guest->child);
    bool okay = capability && guest_host_child_cpu_read(guest->child, &retained, error);
    if (!capability)
        guest_fail(error, QA_ERROR_ARGUMENT, 0, "native CPU lost its physical capability owner");
    if (okay && writing) {
        /* The scalar ABI projection cannot replace privileged/emulated state
         * or extended hardware components. Keep the real full state owner. */
        qa_native_guest_cpu projected;
        okay = guest_host_x86_64_to_cpu(&retained, &projected, error);
        if (okay) projected.xcr0 = capability->xfeatures;
        if (okay && (memcmp(cpu->control, projected.control, sizeof(cpu->control)) ||
            memcmp(cpu->debug, projected.debug, sizeof(cpu->debug)) ||
            cpu->efer != projected.efer || cpu->xcr0 != projected.xcr0 ||
            cpu->xstate_bv != projected.xstate_bv ||
            cpu->execution_flags != projected.execution_flags ||
            cpu->execution_flags2 != projected.execution_flags2 || cpu->a20_mask != projected.a20_mask))
            okay = guest_fail(error, QA_ERROR_UNSUPPORTED, 0,
                "native ABI projection cannot install emulated processor state");
        for (size_t i = 0; okay && i < 4; ++i)
            if (cpu->tables[i].selector || cpu->tables[i].base ||
                cpu->tables[i].limit || cpu->tables[i].flags)
                okay = guest_fail(error, QA_ERROR_UNSUPPORTED, i,
                    "native ABI projection cannot install emulated descriptor tables");
        for (size_t i = 0; okay && i < 6; ++i)
            if (cpu->segments[i].limit || cpu->segments[i].flags ||
                (i < 4 && cpu->segments[i].base))
                okay = guest_fail(error, QA_ERROR_UNSUPPORTED, i,
                    "native ABI projection requires actual flat segment state");
        if (okay) okay = guest_host_x86_64_from_cpu(cpu, &retained, &next, error);
        if (okay) okay = guest_host_child_cpu_write(guest->child, &next, error);
    } else if (okay) {
        okay = guest_host_x86_64_to_cpu(&retained, cpu, error);
        /* Kernel signal transfers may omit unused physical components. XCR0
         * describes the actual CPU, independently of that transfer mask. */
        if (okay) cpu->xcr0 = capability->xfeatures;
    }
    guest_host_x86_64_state_free(&next);
    guest_host_x86_64_state_free(&retained);
    return okay;
}

static bool import(void *context, guest_host_child *child, uint64_t id,
    guest_host_x86_64_state *state, qa_error *error)
{
    qa_native_guest *guest = context;
    if (guest->child != child || !guest->run || guest->callback_depth == UINT_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, id, "native import lost its actual stopped owner");
    qa_native_guest_callback callback = {0};
    for (size_t i = 0; i < guest->callback_count; ++i)
        if (guest->callbacks[i].id == id && guest->callbacks[i].address == state->instruction)
            callback = guest->callbacks[i];
    if (!callback.invoke)
        return guest_fail(error, QA_ERROR_NOT_FOUND, id, "native import has no actual saved callback");
    guest_dispatch_started(guest);
    guest->stepping = false;
    ++guest->callback_depth;
    bool okay = guest_mutable(guest, error) &&
        callback.invoke(callback.context, guest, id, error);
    --guest->callback_depth;
    if (okay) okay = guest_mutable(guest, error);
    guest->stepping = true;
    return okay;
}

typedef struct program_call {
    qa_native_guest *guest;
    qa_native_guest_syscall_fn syscall;
    void *context;
} program_call;

static bool program_import(void *context, guest_host_child *child, uint64_t id,
    guest_host_x86_64_state *state, qa_error *error)
{ return import(((program_call *)context)->guest, child, id, state, error); }

static bool program_syscall(void *context, guest_host_child *child,
    const guest_host_syscall *request, guest_host_syscall_result *result, qa_error *error)
{
    program_call *call = context;
    qa_native_guest *guest = call->guest;
    if (guest->child != child || !guest->run || guest->callback_depth == UINT_MAX)
        return guest_fail(error, QA_ERROR_ARGUMENT, request->instruction,
            "native syscall lost its actual stopped program owner");
    qa_native_guest_syscall actual = {.instruction = request->instruction,
        .next_instruction = request->next_instruction, .number = request->number};
    memcpy(actual.arguments, request->arguments, sizeof(actual.arguments));
    qa_native_guest_syscall_result completed = {0};
    guest->stepping = false;
    ++guest->callback_depth;
    bool okay = guest_mutable(guest, error) &&
        call->syscall(call->context, guest, &actual, &completed, error);
    --guest->callback_depth;
    if (okay) okay = guest_mutable(guest, error);
    guest->stepping = true;
    if (okay) *result = (guest_host_syscall_result){completed.value, completed.stop};
    return okay;
}

static bool native_run(qa_native_guest *guest, uint64_t start,
    uint64_t stop, qa_native_guest_syscall_fn syscall, void *context,
    bool *program_stopped, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (guest->options.backend != QA_NATIVE_GUEST_HOST_X86_64 || guest->observe ||
        (!stop && !syscall) || (guest->run && !guest->callback_depth) ||
        !guest_range(guest, start, 1, QA_NATIVE_GUEST_EXECUTE, error) ||
        (stop && !guest_range(guest, stop, 1, QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_EXECUTE, error)))
        return guest_fail(error, QA_ERROR_ARGUMENT, start,
            "native invocation requires its hardware owner and unbudgeted capability");
    guest_run frame = {.parent = guest->run};
    guest->run = &frame; guest->stepping = true;
    program_call call = {guest, syscall, context};
    bool entered = false;
    bool okay = syscall ? guest_host_child_run_with_syscalls(guest->child, start, stop,
        program_import, program_syscall, &call, program_stopped, error) :
        guest_host_child_run_receipt(guest->child, start, stop, import, guest, &entered, error);
    if (entered) guest_dispatch_started(guest);
    guest->stepping = false; guest->run = frame.parent;
    if (!okay) {
        guest_host_stop fault = {0}; qa_error ignored = {0};
        if (guest_host_child_last_fault(guest->child, &fault, &ignored) && fault.access) {
            qa_native_guest_mapping *mapping = guest_mapping(guest, fault.address);
            qa_native_guest_fault receipt = {.kind = QA_NATIVE_GUEST_FAULT_UNMAPPED,
                .address = fault.address, .access = fault.access};
            if (mapping) {
                guest_backing *backing = guest_backing_at(guest, mapping->backing);
                receipt.backing = mapping->backing;
                receipt.backing_offset = mapping->backing_offset + fault.address - mapping->base;
                receipt.kind = backing->file && receipt.backing_offset >= backing->source.accessible_bytes &&
                    (mapping->permissions & fault.access) == fault.access ?
                    QA_NATIVE_GUEST_FAULT_FILE_EOF : QA_NATIVE_GUEST_FAULT_PROTECTION;
            }
            guest->memory_fault = receipt; guest->has_memory_fault = true;
        }
        guest_host_x86_64_state_free(&fault.state);
        guest->failed = true;
    }
    return okay;
}

bool qa_native_guest_run_native(qa_native_guest *guest, uint64_t start,
    uint64_t stop, qa_error *error)
{ return native_run(guest, start, stop, NULL, NULL, NULL, error); }

bool guest_native_run_program(qa_native_guest *guest, uint64_t start,
    uint64_t stop, qa_native_guest_syscall_fn syscall, void *context,
    bool *program_stopped, qa_error *error)
{ return native_run(guest, start, stop, syscall, context, program_stopped, error); }
