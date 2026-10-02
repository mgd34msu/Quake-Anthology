#include "internal.h"
#include "unicorn_state.h"
#include "native_cpu_codec.h"

static const int registers32[8] = {UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_EBX, UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI};
static const int registers64[16] = {UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX,
    UC_X86_REG_RBX, UC_X86_REG_RSP, UC_X86_REG_RBP, UC_X86_REG_RSI, UC_X86_REG_RDI,
    UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_R10, UC_X86_REG_R11,
    UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15};

static bool transfer(qa_native_guest *guest, int reg, void *value, size_t bytes,
    bool writing, qa_error *error)
{
    size_t transferred = bytes;
    uc_err code = writing ? uc_reg_write2(guest->cpu, reg, value, &transferred) :
        uc_reg_read2(guest->cpu, reg, value, &transferred);
    if (!guest_uc(guest, code, error)) return false;
    if (transferred != bytes) {
        guest->failed = true;
        return guest_fail(error, QA_ERROR_FORMAT, (uint64_t)(unsigned)reg, "native guest CPU register extent differs from its contract");
    }
    return true;
}

static bool word(qa_native_guest *guest, int reg, uint64_t *value, bool writing, qa_error *error)
{
    if (guest->options.image.target.pointer_bytes == 8)
        return transfer(guest, reg, value, sizeof(*value), writing, error);
    uint32_t narrow = (uint32_t)*value;
    if (!transfer(guest, reg, &narrow, sizeof(narrow), writing, error)) return false;
    if (!writing) *value = narrow;
    return true;
}

bool guest_cpu_transfer(qa_native_guest *guest, qa_native_guest_cpu *state,
    bool writing, qa_error *error)
{
    if (guest->options.backend == QA_NATIVE_GUEST_HOST_X86_64)
        return guest_native_transfer(guest, state, writing, error);
    bool wide = guest->options.image.target.pointer_bytes == 8;
    if (!writing) memset(state, 0, sizeof(*state));
    for (size_t i = 0; i < (wide ? 16u : 8u); ++i)
        if (!word(guest, wide ? registers64[i] : registers32[i], &state->registers[i], writing, error)) return false;
    if (!word(guest, wide ? UC_X86_REG_RIP : UC_X86_REG_EIP, &state->instruction, writing, error) ||
        !word(guest, wide ? UC_X86_REG_RFLAGS : UC_X86_REG_EFLAGS, &state->flags, writing, error)) return false;
    for (size_t i = 0; i < 8; ++i) {
        uint64_t aligned[2] = {state->fp_mantissa[i], 0};
        memcpy((uint8_t *)aligned + 8, &state->fp_exponent[i], sizeof(uint16_t));
        if (!transfer(guest, UC_X86_REG_FP0 + (int)i, aligned, 10, writing, error)) return false;
        if (!writing) {
            state->fp_mantissa[i] = aligned[0];
            memcpy(&state->fp_exponent[i], (uint8_t *)aligned + 8, sizeof(uint16_t));
        }
    }
    if (!transfer(guest, UC_X86_REG_FPCW, &state->fp_control, sizeof(uint16_t), writing, error) ||
        !transfer(guest, UC_X86_REG_FPSW, &state->fp_status, sizeof(uint16_t), writing, error) ||
        !transfer(guest, UC_X86_REG_FPTAG, &state->fp_tags, sizeof(uint16_t), writing, error) ||
        !transfer(guest, UC_X86_REG_FIP, &state->fp_instruction, sizeof(uint64_t), writing, error) ||
        !transfer(guest, UC_X86_REG_FDP, &state->fp_operand, sizeof(uint64_t), writing, error) ||
        !transfer(guest, UC_X86_REG_FCS, &state->fp_code_selector, sizeof(uint16_t), writing, error) ||
        !transfer(guest, UC_X86_REG_FDS, &state->fp_data_selector, sizeof(uint16_t), writing, error) ||
        !transfer(guest, UC_X86_REG_FOP, &state->fp_opcode, sizeof(uint16_t), writing, error) ||
        !transfer(guest, UC_X86_REG_MXCSR, &state->mxcsr, sizeof(uint32_t), writing, error)) return false;
    for (size_t i = 0; i < (wide ? 16u : 8u); ++i)
        if (!transfer(guest, UC_X86_REG_XMM0 + (int)i, state->xmm[i], sizeof(state->xmm[i]), writing, error)) return false;
    return guest_uc(guest, qa_unicorn_x86_state(guest->cpu, state, writing), error);
}

bool qa_native_guest_cpu_read(const qa_native_guest *guest, qa_native_guest_cpu *out, qa_error *error)
{
    if (!guest_ready(guest, error)) return false;
    if (!out) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest CPU output is required");
    qa_native_guest_cpu state;
    if (!guest_cpu_transfer((qa_native_guest *)guest, &state, false, error)) return false;
    *out = state;
    return true;
}

bool qa_native_guest_cpu_write(qa_native_guest *guest, const qa_native_guest_cpu *state, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (!state) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest CPU state is required");
    if (guest->options.image.target.pointer_bytes == 4) {
        if (state->instruction > UINT32_MAX || state->flags > UINT32_MAX)
            return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest CPU values exceed its i386 ABI");
        for (size_t i = 0; i < 16; ++i)
            if (state->registers[i] > (i < 8 ? UINT32_MAX : 0))
                return guest_fail(error, QA_ERROR_ARGUMENT, i, "native guest CPU register exceeds its i386 ABI");
        for (size_t i = 8; i < 16; ++i)
            if (state->xmm[i][0] || state->xmm[i][1])
                return guest_fail(error, QA_ERROR_ARGUMENT, i, "native guest i386 has eight SSE registers");
        for (size_t i = 0; i < 10; ++i)
            if ((i < 6 ? state->segments[i].base : state->tables[i - 6].base) > UINT32_MAX)
                return guest_fail(error, QA_ERROR_ARGUMENT, i, "native guest segment exceeds its i386 ABI");
        for (size_t i = 0; i < 9; ++i)
            if (state->control[i] > UINT32_MAX)
                return guest_fail(error, QA_ERROR_ARGUMENT, i, "native guest control register exceeds its i386 ABI");
        for (size_t i = 0; i < 8; ++i)
            if (state->debug[i] > UINT32_MAX)
                return guest_fail(error, QA_ERROR_ARGUMENT, i, "native guest debug register exceeds its i386 ABI");
    }
    qa_native_guest_cpu copy = *state;
    return guest_cpu_transfer(guest, &copy, true, error);
}

static void record_store(uc_engine *cpu, uc_mem_type type, uint64_t address, int size,
    int64_t value, void *context)
{
    (void)value;
    qa_native_guest *guest = context;
    guest_run *run = guest->run;
    /* Ordinary Unicorn write hooks run before protection and watchpoint checks. */
    if (type == UC_MEM_WRITE) return;
    if (!run || run->recording_failed) return;
    if ((int)type == QA_UNICORN_MEM_WRITE_COMMITTED) {
        if (!run->prepared || run->prepared_address != address || run->prepared_size != size) {
            guest_fail(&run->failure, QA_ERROR_FORMAT, address, "native guest committed store lacks its prepared journal");
            run->recording_failed = true; uc_emu_stop(cpu); return;
        }
        run->count += run->prepared;
        run->prepared = 0;
        return;
    }
    if ((int)type != QA_UNICORN_MEM_WRITE_PREPARE || run->prepared) {
        guest_fail(&run->failure, QA_ERROR_FORMAT, address, "native guest store journal sequence is invalid");
        run->recording_failed = true; uc_emu_stop(cpu); return;
    }
    if (size <= 0 || !guest_range(guest, address, (size_t)size, QA_NATIVE_GUEST_WRITE, &run->failure)) {
        run->recording_failed = true; uc_emu_stop(cpu); return;
    }
    size_t offset = 0;
    while (offset < (size_t)size) {
        if (!guest_grow((void **)&run->writes, &run->capacity, run->count + run->prepared + 1,
            sizeof(*run->writes), &run->failure)) {
            run->recording_failed = true; uc_emu_stop(cpu); return;
        }
        qa_native_guest_mapping *mapping = guest_mapping(guest, address + offset);
        uint64_t displacement = address + offset - mapping->base;
        size_t amount = (size_t)size - offset;
        if (amount > mapping->bytes - displacement) amount = (size_t)(mapping->bytes - displacement);
        run->writes[run->count + run->prepared++] = (qa_native_guest_commit){address + offset, mapping->backing,
            mapping->backing_offset + displacement, amount, run->instruction, false};
        offset += amount;
    }
    run->prepared_address = address;
    run->prepared_size = size;
}

static bool record_fault(uc_engine *cpu, uc_mem_type type, uint64_t address,
    int size, int64_t value, void *context)
{
    (void)cpu; (void)size; (void)value;
    qa_native_guest *guest = context;
    uint32_t access = type == UC_MEM_READ_PROT || type == UC_MEM_READ_UNMAPPED ?
        QA_NATIVE_GUEST_READ : type == UC_MEM_WRITE_PROT || type == UC_MEM_WRITE_UNMAPPED ?
        QA_NATIVE_GUEST_WRITE : QA_NATIVE_GUEST_EXECUTE;
    qa_native_guest_mapping *mapping = guest_mapping(guest, address);
    qa_native_guest_fault fault = {.kind = QA_NATIVE_GUEST_FAULT_UNMAPPED,
        .access = access, .address = address};
    if (mapping) {
        guest_backing *backing = guest_backing_at(guest, mapping->backing);
        fault.backing = mapping->backing;
        fault.backing_offset = mapping->backing_offset + address - mapping->base;
        fault.kind = backing->file && fault.backing_offset >= backing->source.accessible_bytes &&
            (mapping->permissions & access) == access ? QA_NATIVE_GUEST_FAULT_FILE_EOF :
            QA_NATIVE_GUEST_FAULT_PROTECTION;
    }
    guest->memory_fault = fault; guest->has_memory_fault = true;
    /* Let the real dependency restore the faulting instruction and return its
     * actual access failure. No page, result or signal continuation is supplied. */
    return false;
}

bool guest_cpu_open(qa_native_guest *guest, bool fresh, qa_error *error)
{
    if (guest->options.backend == QA_NATIVE_GUEST_HOST_X86_64) {
        guest_host_child_options options = {.executable = guest->options.host_executable,
            .target = guest->options.image.target, .profile_guard = guest->options.profile_guard};
        if (!guest_host_child_create(&options, &guest->child, error)) return false;
        /* Fresh source construction initializes absent architectural components
         * once, before the first backing or mapping transaction. Cold adoption
         * keeps the saved components and never enters this initializer. */
        if (fresh && !guest_host_child_source_initialize(guest->child, error)) return false;
        if (!guest_host_child_profile_read(guest->child, &guest->profile, error) ||
            !guest_host_child_source_domain(guest->child, &guest->source_domain, error)) return false;
        guest_host_x86_64_state state = {0}; qa_buffer qualified = {0};
        bool okay = guest_host_child_cpu_read(guest->child, &state, error) &&
            (!fresh || guest_profile_cpu_current(&guest->source_domain,
                guest_host_child_capability(guest->child), &state, error)) &&
            guest_native_cpu_checkpoint(&state, guest_host_child_capability(guest->child),
                &qualified, error);
        guest_host_x86_64_state_free(&state); qa_buffer_free(&qualified);
        return okay;
    }
    if (uc_version(NULL, NULL) != UINT32_C(0x020104ff) ||
        qa_unicorn_state_revision() != QA_UNICORN_STATE_REVISION ||
        qa_unicorn_store_revision() != QA_UNICORN_STORE_REVISION ||
        qa_unicorn_map_revision() != QA_UNICORN_MAP_REVISION)
        return guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "native guest CPU requires pinned Unicorn 2.1.4 and its state, store and memory extensions");
    bool wide = guest->options.image.target.pointer_bytes == 8;
    if (!guest_uc(guest, uc_open(UC_ARCH_X86, wide ? UC_MODE_64 : UC_MODE_32, &guest->cpu), error)) return false;
    /* This lower unit qualifies x87/SSE state. Later profile admission must
     * explicitly extend the owner if an artifact requires AVX or other state. */
    if (!guest_uc(guest, uc_ctl_set_cpu_model(guest->cpu,
        wide ? UC_CPU_X86_QEMU64 : UC_CPU_X86_QEMU32), error)) return false;
    uc_cb_hookmem_t store_callback = record_store;
    uc_cb_eventmem_t fault_callback = record_fault;
    void *store_pointer = NULL, *fault_pointer = NULL;
    if (sizeof(store_callback) != sizeof(store_pointer) || sizeof(fault_callback) != sizeof(fault_pointer))
        return guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "Unicorn hook API requires the actual host function-pointer representation");
    memcpy(&store_pointer, &store_callback, sizeof(store_pointer));
    memcpy(&fault_pointer, &fault_callback, sizeof(fault_pointer));
    if (!guest_uc(guest, uc_hook_add(guest->cpu, &guest->store_hook, UC_HOOK_MEM_WRITE,
        store_pointer, guest, 1, 0), error)) return false;
    if (!guest_uc(guest, uc_hook_add(guest->cpu, &guest->fault_hook, UC_HOOK_MEM_INVALID,
        fault_pointer, guest, 1, 0), error)) return false;
    /* The real hook API initializes Unicorn after selecting the CPU model. */
    if (!guest_uc(guest, qa_unicorn_memory_bind(guest->cpu), error)) return false;
    return guest_uc(guest, qa_unicorn_store_bind(guest->cpu, guest->store_hook), error);
}

bool qa_native_guest_bind(qa_native_guest *guest, const qa_native_guest_callback *callback, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    /* Source services can create real callback methods lazily at an import
     * entry. The CPU is stopped and run copied its active callback by value.
     * Appending a descriptor cannot change that frame. Store observers and
     * fault/restore publication are not callback registration boundaries. */
    if (!qa_native_guest_idle(guest) &&
        !(guest->run && guest->callback_depth && !guest->publication_depth))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest callback binding requires idle ownership or a stopped import callback");
    if (!callback || !callback->id || !callback->invoke)
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest callback identity and actual dispatch are required");
    if (!guest_range(guest, callback->address, 1, QA_NATIVE_GUEST_EXECUTE, error)) return false;
    for (size_t i = 0; i < guest->callback_count; ++i)
        if (guest->callbacks[i].id == callback->id || guest->callbacks[i].address == callback->address)
            return guest_fail(error, QA_ERROR_ARGUMENT, callback->address, "native guest callback identity or address is already bound");
    if (!guest_grow((void **)&guest->callbacks, &guest->callback_capacity,
        guest->callback_count + 1, sizeof(*guest->callbacks), error)) return false;
    if (guest->options.backend == QA_NATIVE_GUEST_HOST_X86_64 &&
        !guest_native_result(guest, guest_host_child_bind(guest->child,
            callback->id, callback->address, error), error)) return false;
    guest->callbacks[guest->callback_count++] = *callback;
    return true;
}

bool qa_native_guest_unbind(qa_native_guest *guest, uint64_t id, qa_error *error)
{
    if (!qa_native_guest_idle(guest))
        return guest_fail(error, QA_ERROR_ARGUMENT, id, "native guest callback retirement requires idle ownership");
    for (size_t i = 0; i < guest->callback_count; ++i) if (guest->callbacks[i].id == id) {
        if (guest->options.backend == QA_NATIVE_GUEST_HOST_X86_64 &&
            !guest_native_result(guest, guest_host_child_unbind(guest->child, id, error), error)) return false;
        memmove(guest->callbacks + i, guest->callbacks + i + 1,
            (--guest->callback_count - i) * sizeof(*guest->callbacks));
        return true;
    }
    return guest_fail(error, QA_ERROR_NOT_FOUND, id, "native guest callback is absent");
}

bool qa_native_guest_observe(qa_native_guest *guest, qa_native_guest_commit_fn observer,
    void *context, qa_error *error)
{
    if (!qa_native_guest_idle(guest))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native guest observer replacement requires idle ownership");
    if (observer && guest->options.backend == QA_NATIVE_GUEST_HOST_X86_64)
        return guest_fail(error, QA_ERROR_UNSUPPORTED, 0,
            "hardware execution cannot provide exact committed CPU store receipts");
    guest->observe = observer; guest->observe_context = context;
    return true;
}

static bool instruction_pointer(qa_native_guest *guest, uint64_t *value, bool writing, qa_error *error)
{
    return word(guest, guest->options.image.target.pointer_bytes == 8 ? UC_X86_REG_RIP : UC_X86_REG_EIP,
        value, writing, error);
}

bool qa_native_guest_instructions(qa_native_guest *guest, qa_native_guest_instruction_fn observer,
    void *context, qa_error *error)
{
    if (!qa_native_guest_idle(guest))
        return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native instruction observer replacement requires idle ownership");
    if (observer && guest->options.backend != QA_NATIVE_GUEST_EMULATED)
        return guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "exact stopped instruction regions require emulated execution");
    guest->instruction_observer = observer; guest->instruction_context = context;
    return true;
}

static bool syscall_opcode(const qa_native_guest *guest, uint64_t instruction)
{
    static const uint8_t opcode[2] = {0x0f, 0x05};
    if (instruction > UINT64_MAX - 2) return false;
    for (size_t i = 0; i < sizeof(opcode); ++i) {
        qa_native_guest_mapping *mapping = guest_mapping(guest, instruction + i);
        if (!mapping || !(mapping->permissions & QA_NATIVE_GUEST_EXECUTE)) return false;
        guest_backing *backing = guest_backing_at(guest, mapping->backing);
        uint64_t offset = mapping->backing_offset + instruction + i - mapping->base;
        if (!backing || offset >= backing->bytes ||
            (backing->file && offset >= backing->source.accessible_bytes) ||
            backing->data[offset] != opcode[i]) return false;
    }
    return true;
}

static bool run(qa_native_guest *guest, uint64_t start, uint64_t stop,
    size_t budget, uint64_t bypass, qa_native_guest_syscall_fn syscall,
    void *syscall_context, bool *program_stopped, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (guest->options.backend != QA_NATIVE_GUEST_EMULATED)
        return guest_fail(error, QA_ERROR_UNSUPPORTED, start,
            "exact instruction budgets require the qualified emulated execution backend");
    if (!budget || !stop || (guest->run && !guest->callback_depth && !guest->publication_depth) ||
        (guest->options.image.target.pointer_bytes == 4 && stop > UINT32_MAX) ||
        !guest_range(guest, start, 1, QA_NATIVE_GUEST_EXECUTE, error))
        return guest_fail(error, QA_ERROR_ARGUMENT, start, "native guest execution requires a genuine executable entry and bounded continuation");
    if (bypass) {
        bool found = false;
        for (size_t i = 0; i < guest->callback_count; ++i)
            if (guest->callbacks[i].id == bypass && guest->callbacks[i].address == start) found = true;
        if (!found) return guest_fail(error, QA_ERROR_ARGUMENT, bypass,
            "native original invocation requires its actual bound entry");
    }
    guest_run frame = {.parent = guest->run, .remaining = budget, .bypass = bypass};
    guest->run = &frame;
    bool okay = instruction_pointer(guest, &start, true, error);
    uint64_t instruction = start;
    while (okay && instruction != stop) {
        if (!guest_mutable(guest, error)) { okay = false; break; }
        for (guest_run *owner = &frame; owner; owner = owner->parent)
            if (!owner->remaining) {
                okay = guest_fail(error, QA_ERROR_ARGUMENT, instruction, "native guest instruction budget exhausted");
                break;
            }
        if (!okay || !guest_range(guest, instruction, 1, QA_NATIVE_GUEST_EXECUTE, error)) { okay = false; break; }
        for (guest_run *owner = &frame; owner; owner = owner->parent) --owner->remaining;
        qa_native_guest_callback callback = {0};
        for (size_t i = 0; i < guest->callback_count; ++i)
            if (guest->callbacks[i].address == instruction) { callback = guest->callbacks[i]; break; }
        if (callback.invoke && callback.id == frame.bypass) {
            frame.bypass = 0;
            callback = (qa_native_guest_callback){0};
        }
        if (callback.invoke) {
            if (guest->callback_depth == UINT_MAX) {
                okay = guest_fail(error, QA_ERROR_ARGUMENT, instruction, "native guest callback depth exhausted");
                break;
            }
            ++guest->callback_depth;
            okay = callback.invoke(callback.context, guest, callback.id, error);
            --guest->callback_depth;
            if (okay) okay = guest_mutable(guest, error);
            uint64_t after = 0;
            if (okay) okay = instruction_pointer(guest, &after, false, error);
            if (okay && after == instruction)
                okay = guest_fail(error, QA_ERROR_ARGUMENT, instruction, "native guest callback did not produce its ABI continuation");
            instruction = after;
            continue;
        }
        if (guest->instruction_observer) {
            if (guest->callback_depth == UINT_MAX) {
                okay = guest_fail(error, QA_ERROR_ARGUMENT, instruction, "native instruction observer depth exhausted");
                break;
            }
            ++guest->callback_depth;
            okay = guest->instruction_observer(guest->instruction_context, guest, instruction, error);
            --guest->callback_depth;
            if (okay) okay = guest_mutable(guest, error);
            uint64_t after = instruction;
            if (okay) okay = instruction_pointer(guest, &after, false, error);
            if (!okay) break;
            if (after != instruction) { instruction = after; continue; }
        }
        if (syscall && syscall_opcode(guest, instruction)) {
            qa_native_guest_cpu state;
            okay = qa_native_guest_cpu_read(guest, &state, error);
            if (!okay) break;
            qa_native_guest_syscall request = {.instruction = instruction,
                .next_instruction = instruction + 2, .number = state.registers[0],
                .arguments = {state.registers[7], state.registers[6], state.registers[2],
                    state.registers[10], state.registers[8], state.registers[9]}};
            qa_native_guest_syscall_result result = {0};
            if (guest->callback_depth == UINT_MAX) {
                okay = guest_fail(error, QA_ERROR_ARGUMENT, instruction, "native syscall callback depth exhausted");
                break;
            }
            ++guest->callback_depth;
            okay = syscall(syscall_context, guest, &request, &result, error);
            --guest->callback_depth;
            if (okay) okay = guest_mutable(guest, error);
            if (!okay) break;
            if (result.stop) { *program_stopped = true; break; }
            qa_native_guest_cpu after;
            okay = qa_native_guest_cpu_read(guest, &after, error);
            if (!okay) break;
            after.registers[0] = (uint64_t)result.value;
            after.registers[1] = request.next_instruction;
            after.registers[11] = state.flags;
            after.instruction = request.next_instruction;
            okay = qa_native_guest_cpu_write(guest, &after, error);
            instruction = request.next_instruction;
            continue;
        }
        frame.count = 0;
        frame.prepared = 0;
        frame.instruction = instruction;
        guest->stepping = true;
        uc_err code = uc_emu_start(guest->cpu, instruction, stop, 0, 1);
        guest->stepping = false;
        guest->faulting = code != UC_ERR_OK || frame.recording_failed;
        /* Actual RAM completions survive a later instruction fault. Publish
         * while the owner is still readable, then make any engine fault terminal. */
        for (size_t i = 0; okay && i < frame.count; ++i) {
            qa_native_guest_commit commit = frame.writes[i];
            commit.instruction_last = i + 1 == frame.count;
            okay = guest_publish(guest, &commit, error);
        }
        if (okay && frame.recording_failed) {
            if (error) *error = frame.failure;
            okay = false;
        } else if (okay && code != UC_ERR_OK && guest->has_memory_fault &&
            guest->memory_fault.kind == QA_NATIVE_GUEST_FAULT_FILE_EOF) {
            okay = guest_fail(error, QA_ERROR_FORMAT, guest->memory_fault.address,
                "native guest CPU file mapping access faults beyond EOF");
        } else if (okay) okay = guest_uc(guest, code, error);
        guest->faulting = false;
        if (okay) okay = instruction_pointer(guest, &instruction, false, error);
    }
    guest->run = frame.parent;
    free(frame.writes);
    if (!okay) guest->failed = true;
    return okay;
}

bool qa_native_guest_run(qa_native_guest *guest, uint64_t start, uint64_t stop,
    size_t budget, qa_error *error)
{ return run(guest, start, stop, budget, 0, NULL, NULL, NULL, error); }

bool qa_native_guest_run_original(qa_native_guest *guest, uint64_t id,
    uint64_t start, uint64_t stop, size_t budget, qa_error *error)
{
    if (!id) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "native original callback identity is required");
    return run(guest, start, stop, budget, id, NULL, NULL, NULL, error);
}

bool qa_native_guest_run_program(qa_native_guest *guest, uint64_t start,
    uint64_t stop, size_t budget, qa_native_guest_syscall_fn syscall,
    void *context, bool *program_stopped, qa_error *error)
{
    if (!guest_mutable(guest, error)) return false;
    if (!syscall || !program_stopped || guest->options.image.target.os != QA_NATIVE_OS_LINUX ||
        guest->options.image.target.arch != QA_NATIVE_ARCH_X86_64 ||
        guest->options.image.target.pointer_bytes != 8)
        return guest_fail(error, QA_ERROR_ARGUMENT, start, "program execution requires an actual Linux x64 syscall owner");
    *program_stopped = false;
    if (guest->options.backend == QA_NATIVE_GUEST_HOST_X86_64) {
        if (budget) return guest_fail(error, QA_ERROR_ARGUMENT, budget, "hardware program execution has no instruction-budget capability");
        return guest_native_run_program(guest, start, stop, syscall, context, program_stopped, error);
    }
    return run(guest, start, stop, budget, 0, syscall, context, program_stopped, error);
}
