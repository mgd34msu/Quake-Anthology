#if defined(QA_NATIVE_PROFILE_DYNAMORIO_CLIENT)
#include "dr_api.h"
#include "drmgr.h"
#include "drutil.h"
#if !defined(X64) || !defined(LINUX)
#error The source instruction monitor requires the actual Linux x64 DynamoRIO SDK.
#endif
#include "guard.h"
#include <cpuid.h>
#include <signal.h>
#include <string.h>

typedef enum profile_phase { PROFILE_ARMED, PROFILE_ACTIVE, PROFILE_STOPPED } profile_phase;
typedef struct profile_store { uint64_t instruction, address, bytes; } profile_store;
typedef struct profile_scope {
    struct profile_scope *parent;
    uint64_t id, entry, stop, fs_base, gs_base;
    bool syscalls;
    profile_phase phase;
    guest_profile_guard_mapping *mappings;
    size_t mapping_count;
    guest_profile_guard_callback *callbacks;
    size_t callback_count;
    guest_profile_interest *interests;
    size_t interest_count;
    profile_store *pending;
    size_t pending_count, pending_at, pending_capacity;
    uint64_t bypass, boundary_bypass;
    uint64_t pkru;
    bool pkru_active;
    guest_profile_guard_fault *fault;
} profile_scope;
typedef struct profile_instruction {
    struct profile_instruction *next;
    instr_t *operands;
    app_pc pc;
    byte bytes[15];
    size_t size;
    byte opcode, modrm;
} profile_instruction;
static profile_scope *scope;
static profile_instruction *instructions;
static app_pc control_entry, fault_entry, import_entry;
static thread_id_t owner_thread;
static size_t pkru_offset;

static bool copy_from_app(const void *address, void *out, size_t bytes)
{
    size_t copied = 0;
    return dr_safe_read(address, bytes, out, &copied) && copied == bytes;
}
static bool copy_to_app(void *address, const void *data, size_t bytes)
{
    size_t copied = 0;
    return dr_safe_write(address, bytes, data, &copied) && copied == bytes;
}
static void scope_free(profile_scope *row)
{
    if (row->mappings) dr_global_free(row->mappings, row->mapping_count * sizeof(*row->mappings));
    if (row->callbacks) dr_global_free(row->callbacks, row->callback_count * sizeof(*row->callbacks));
    if (row->interests) dr_global_free(row->interests, row->interest_count * sizeof(*row->interests));
    if (row->pending) dr_global_free(row->pending, row->pending_capacity * sizeof(*row->pending));
    dr_global_free(row, sizeof(*row));
}
static bool canonical(uint64_t address)
{ return address <= UINT64_C(0x7fffffffffff) || address >= UINT64_C(0xffff800000000000); }
static bool scope_range(const profile_scope *owner, uint64_t address, uint64_t bytes,
    uint32_t rights, uint64_t *bad)
{
    if (bad) *bad = address;
    if (!owner || !address || !bytes || bytes - 1 > UINT64_MAX - address ||
        !canonical(address) || !canonical(address + bytes - 1)) return false;
    uint64_t offset = 0;
    while (offset < bytes) {
        if (bad) *bad = address + offset;
        const guest_profile_guard_mapping *found = NULL;
        for (size_t i = 0; i < owner->mapping_count; ++i) {
            const guest_profile_guard_mapping *row = owner->mappings + i;
            if (address + offset >= row->mapping.base && address + offset - row->mapping.base < row->mapping.bytes) { found = row; break; }
        }
        if (!found || (found->mapping.permissions & rights) != rights) return false;
        uint64_t within = address + offset - found->mapping.base;
        uint64_t available = found->mapping.bytes - within;
        uint64_t amount = available < bytes - offset ? available : bytes - offset;
        if (found->file && (found->mapping.backing_offset > found->accessible_bytes ||
            within > found->accessible_bytes - found->mapping.backing_offset ||
            amount > found->accessible_bytes - found->mapping.backing_offset - within)) {
            if (bad && found->mapping.backing_offset <= found->accessible_bytes &&
                within <= found->accessible_bytes - found->mapping.backing_offset)
                *bad += found->accessible_bytes - found->mapping.backing_offset - within;
            return false;
        }
        offset += amount;
    }
    return true;
}
static bool range(uint64_t address, uint64_t bytes, uint32_t rights, uint64_t *bad)
{ return scope_range(scope, address, bytes, rights, bad); }
static bool fp_original(void *context, byte *fp)
{
    (void)context;
    /* FXSAVE64 records the physical last x87 instruction. Translate that exact
     * cache address; an original address restored by FLDENV remains untouched.
     * Never substitute the current/last instrumented instruction for FIP. */
    uint64_t saved;
    memcpy(&saved, fp + 8, sizeof(saved));
    if (!saved) return true;
    app_pc physical = (app_pc)(ptr_uint_t)saved;
    /* where_am_i tracks the executing context; a clean callee is not FCACHE
     * even when this saved FIP belongs to it. The actual DR address inventory
     * includes its code cache. Require its real translation for any such FIP;
     * an untranslatable internal/stale address cannot be source state. */
    if (dr_memory_is_in_client(physical)) return false;
    if (!dr_memory_is_dr_internal(physical)) return true;
    app_pc original = dr_app_pc_from_cache_pc(physical);
    if (!original) return false;
    saved = (uint64_t)(ptr_uint_t)original;
    memcpy(fp + 8, &saved, sizeof(saved));
    return true;
}
static profile_scope *scope_read(const guest_profile_guard_control *control)
{
    if (!control->scope || !control->entry || (!control->stop && !control->syscalls) || !control->fault ||
        !control->mapping_count || control->mapping_count > SIZE_MAX / sizeof(guest_profile_guard_mapping) ||
        control->callback_count > SIZE_MAX / sizeof(guest_profile_guard_callback) ||
        control->interest_count > SIZE_MAX / sizeof(guest_profile_interest)) return NULL;
    profile_scope *row = dr_global_alloc(sizeof(*row));
    if (!row) return NULL;
    memset(row, 0, sizeof(*row));
    row->id = control->scope; row->entry = control->entry; row->stop = control->stop;
    row->fs_base = control->fs_base; row->gs_base = control->gs_base;
    row->syscalls = control->syscalls;
    row->fault = control->fault; row->phase = PROFILE_ARMED;
    row->mapping_count = control->mapping_count; row->callback_count = control->callback_count;
    row->interest_count = control->interest_count; row->bypass = control->bypass;
    if (pkru_offset) {
        uint64_t active;
        if (!control->xsave || control->xsave_bytes < 576 ||
            pkru_offset > control->xsave_bytes || sizeof(row->pkru) > control->xsave_bytes - pkru_offset ||
            !copy_from_app(control->xsave + 512, &active, sizeof(active)) ||
            !copy_from_app(control->xsave + pkru_offset, &row->pkru, sizeof(row->pkru))) {
            scope_free(row); return NULL;
        }
        row->pkru_active = (active & (UINT64_C(1) << 9)) != 0;
    }
    row->mappings = dr_global_alloc(row->mapping_count * sizeof(*row->mappings));
    row->callbacks = row->callback_count ? dr_global_alloc(row->callback_count * sizeof(*row->callbacks)) : NULL;
    row->interests = row->interest_count ? dr_global_alloc(row->interest_count * sizeof(*row->interests)) : NULL;
    bool okay = (!row->interest_count || (row->interests && copy_from_app(control->interests,
        row->interests, row->interest_count * sizeof(*row->interests)))) && row->mappings && (!row->callback_count || row->callbacks) &&
        copy_from_app(control->mappings, row->mappings, row->mapping_count * sizeof(*row->mappings)) &&
        (!row->callback_count || copy_from_app(control->callbacks, row->callbacks, row->callback_count * sizeof(*row->callbacks)));
    for (size_t i = 0; okay && i < row->mapping_count; ++i) {
        const qa_native_guest_mapping *m = &row->mappings[i].mapping;
        okay = m->id && m->backing && m->base && m->bytes && !(m->base & 4095) && !(m->bytes & 4095) &&
            m->bytes <= UINT64_MAX - m->base && m->bytes <= UINT64_MAX - m->backing_offset && !(m->permissions & ~7u);
        for (size_t j = 0; okay && j < i; ++j) {
            const qa_native_guest_mapping *old = &row->mappings[j].mapping;
            okay = old->id != m->id && !(old->base < m->base + m->bytes && m->base < old->base + old->bytes);
        }
    }
    for (size_t i = 0; okay && i < row->callback_count; ++i) {
        okay = row->callbacks[i].id && row->callbacks[i].address &&
            scope_range(row, row->callbacks[i].address, 1, QA_NATIVE_GUEST_EXECUTE, NULL);
        for (size_t j = 0; okay && j < i; ++j) okay = row->callbacks[j].id != row->callbacks[i].id && row->callbacks[j].address != row->callbacks[i].address;
    }
    for (size_t i = 0; okay && i < row->interest_count; ++i) {
        const guest_profile_interest *interest = row->interests + i;
        okay = interest->id && interest->address &&
            (interest->kind == GUEST_PROFILE_INTEREST_STORE ? interest->bytes &&
                scope_range(row, interest->address, interest->bytes, QA_NATIVE_GUEST_READ, NULL) :
             interest->kind == GUEST_PROFILE_INTEREST_INSTRUCTION && !interest->bytes &&
                scope_range(row, interest->address, 1, QA_NATIVE_GUEST_EXECUTE, NULL));
        for (size_t j = 0; okay && j < i; ++j)
            okay = row->interests[j].kind != interest->kind || row->interests[j].id != interest->id;
    }
    byte trap;
    okay = okay && scope_range(row, row->entry, 1, QA_NATIVE_GUEST_EXECUTE, NULL) &&
        (!row->stop || (scope_range(row, row->stop, 1, QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_EXECUTE, NULL) &&
        copy_from_app((void *)(ptr_uint_t)row->stop, &trap, 1) && trap == 0xcc));
    if (!okay) { scope_free(row); return NULL; }
    return row;
}
static bool control_apply(guest_profile_guard_control *control)
{
    if (control->magic != GUEST_PROFILE_GUARD_MAGIC || control->reserved ||
        control->operation > GUEST_PROFILE_GUARD_CAPTURE) return false;
    /* FISTTP is part of the source x87 opcode domain and requires real SSE3
     * hardware. Qualify it before any source instruction or initializer. */
    if (control->operation == GUEST_PROFILE_GUARD_PROBE)
        return control_entry && fault_entry && import_entry && !scope && proc_has_feature(FEATURE_SSE3);
    if (control->operation == GUEST_PROFILE_GUARD_CAPTURE) {
        if ((scope && (scope->phase != PROFILE_STOPPED || scope->id != control->scope)) ||
            (!scope && control->scope) || !control->xsave || control->xsave_bytes < 576) return false;
        byte legacy[512];
        bool okay = copy_from_app(control->xsave, legacy, sizeof(legacy)) &&
            fp_original(dr_get_current_drcontext(), legacy) &&
            copy_to_app(control->xsave + 8, legacy + 8, sizeof(uint64_t));
        /* DR rebuilds terminal-trap frames with x87/SIMD only. Protection-key
         * writes are outside the admitted instruction domain; retain their
         * actual ENTER component through that signal transfer. */
        if (okay && scope && pkru_offset) {
            uint64_t active = 0;
            okay = pkru_offset <= control->xsave_bytes &&
                sizeof(scope->pkru) <= control->xsave_bytes - pkru_offset &&
                copy_from_app(control->xsave + 512, &active, sizeof(active));
            active = (active & ~(UINT64_C(1) << 9)) |
                (scope->pkru_active ? UINT64_C(1) << 9 : 0);
            okay = okay && copy_to_app(control->xsave + pkru_offset, &scope->pkru, sizeof(scope->pkru)) &&
                copy_to_app(control->xsave + 512, &active, sizeof(active));
        }
        return okay;
    }
    if (control->operation == GUEST_PROFILE_GUARD_LEAVE) {
        if (!scope || scope->id != control->scope || scope->phase != PROFILE_STOPPED) return false;
        profile_scope *retired = scope; scope = scope->parent; scope_free(retired); return true;
    }
    if (scope && scope->phase != PROFILE_STOPPED) return false;
    if (control->operation == GUEST_PROFILE_GUARD_RESUME && (!scope || scope->id != control->scope)) return false;
    profile_scope *next = scope_read(control);
    if (!next) return false;
    if (control->operation == GUEST_PROFILE_GUARD_RESUME) {
        next->parent = scope->parent;
        next->pending = scope->pending; next->pending_count = scope->pending_count;
        next->pending_at = scope->pending_at; next->pending_capacity = scope->pending_capacity;
        next->bypass = scope->bypass;
        if (control->entry == scope->boundary_bypass)
            next->boundary_bypass = scope->boundary_bypass;
        scope->pending = NULL; scope_free(scope);
    } else next->parent = scope;
    scope = next;
    return true;
}
static void redirect(dr_mcontext_t *context, byte *fp, app_pc target)
{
    context->xip = target;
    proc_restore_fpstate(fp);
    if (!dr_redirect_execution(context)) dr_abort();
}
static void reject(dr_mcontext_t *context, byte *fp, app_pc pc,
    guest_profile_guard_fault_kind kind, uint32_t rights, uint32_t vector,
    uint64_t address, uint64_t bytes)
{
    guest_profile_guard_fault fault = {kind, rights, vector, scope->id,
        (uint64_t)(ptr_uint_t)pc, address, bytes, (uint64_t)(ptr_uint_t)pc};
    if (!copy_to_app(scope->fault, &fault, sizeof(fault))) dr_abort();
    redirect(context, fp, fault_entry);
}
static int64_t bit_displacement(instr_t *instruction, dr_mcontext_t *cpu, size_t bytes)
{
    int opcode = instr_get_opcode(instruction);
    if (opcode != OP_bt && opcode != OP_bts && opcode != OP_btr && opcode != OP_btc) return 0;
    /* BT's register index is source 1; modifying bit operations list it first.
     * Immediate bit indexes select within one word and never move the address. */
    opnd_t index = instr_get_src(instruction, opcode == OP_bt ? 1 : 0);
    if (!opnd_is_reg(index)) return 0;
    uint64_t raw = (uint64_t)reg_get_value(opnd_get_reg(index), cpu);
    int64_t value = bytes == 2 ? (int16_t)raw : bytes == 4 ? (int32_t)raw : (int64_t)raw;
    int64_t bits = (int64_t)bytes * 8, words = value / bits;
    if (value % bits < 0) --words;
    return words * (int64_t)bytes;
}
static uint64_t operand_address(profile_instruction *description, opnd_t operand,
    dr_mcontext_t *cpu, size_t bytes)
{
    int64_t displacement = bit_displacement(description->operands, cpu, bytes);
    uint64_t segment = opnd_get_segment(operand) == DR_SEG_FS ? scope->fs_base :
        opnd_get_segment(operand) == DR_SEG_GS ? scope->gs_base : 0;
    uint64_t offset;
    if (opnd_is_base_disp(operand)) {
        reg_id_t base = opnd_get_base(operand), index = opnd_get_index(operand);
        offset = (base == DR_REG_NULL ? 0 : (uint64_t)reg_get_value(base, cpu)) +
            (index == DR_REG_NULL ? 0 : (uint64_t)reg_get_value(index, cpu) * opnd_get_scale(operand)) +
            (uint64_t)(int64_t)opnd_get_disp(operand) + (uint64_t)displacement;
        /* Wrap the actual decoded address domain before its genuine segment
         * base. An address-size prefix does not narrow implicit stack refs. */
        if (opnd_is_disp_short_addr(operand) || reg_is_32bit(base) || reg_is_32bit(index))
            offset = (uint32_t)offset;
    } else if (opnd_is_abs_addr(operand) || opnd_is_rel_addr(operand))
        offset = (uint64_t)(ptr_uint_t)opnd_get_addr(operand) + (uint64_t)displacement;
    else { dr_abort(); return 0; }
    return offset + segment;
}
static void store_interest(uint64_t instruction, uint64_t address, size_t bytes)
{
    bool watched = false;
    for (size_t i = 0; i < scope->interest_count; ++i) {
        const guest_profile_interest *interest = scope->interests + i;
        if (interest->kind == GUEST_PROFILE_INTEREST_STORE &&
            address < interest->address + interest->bytes && interest->address < address + bytes) {
            watched = true; break;
        }
    }
    if (!watched) return;
    for (size_t i = 0; i < scope->pending_count; ++i)
        if (scope->pending[i].address == address && scope->pending[i].bytes == bytes) return;
    if (scope->pending_count == scope->pending_capacity) {
        size_t capacity = scope->pending_capacity ? scope->pending_capacity * 2 : 2;
        if (capacity < scope->pending_capacity || capacity > SIZE_MAX / sizeof(profile_store)) dr_abort();
        profile_store *next = dr_global_alloc(capacity * sizeof(*next));
        if (!next) dr_abort();
        if (scope->pending) {
            memcpy(next, scope->pending, scope->pending_count * sizeof(*next));
            dr_global_free(scope->pending, scope->pending_capacity * sizeof(*next));
        }
        scope->pending = next; scope->pending_capacity = capacity;
    }
    scope->pending[scope->pending_count++] = (profile_store){instruction, address, bytes};
}
static void environment_store(profile_instruction *description, dr_mcontext_t *cpu,
    byte *fp, byte opcode)
{
    opnd_t destination = instr_get_dst(description->operands, 0);
    size_t bytes = drutil_opnd_mem_size_in_bytes(destination, description->operands);
    bool short_environment = bytes == (opcode == 0xd9 ? 14u : 94u);
    if (bytes != (short_environment ? (opcode == 0xd9 ? 14u : 94u) :
        (opcode == 0xd9 ? 28u : 108u))) dr_abort();
    uint64_t address = operand_address(description, destination, cpu, bytes);
    struct { byte data[108]; } saved;
    /* DR's float-PC store mangler can substitute a preceding block instruction
     * for the actual restored FIP. Execute this admitted operation in the
     * private client against the real normalized FP bank instead. Capture its
     * actual masking/reset result before any other client work. */
    proc_restore_fpstate(fp);
    if (opcode == 0xd9) {
        if (short_environment) __asm__ volatile("data16 fnstenv %0" : "=m"(saved));
        else __asm__ volatile("fnstenv %0" : "=m"(saved));
    } else {
        if (short_environment) __asm__ volatile("data16 fnsave %0" : "=m"(saved));
        else __asm__ volatile("fnsave %0" : "=m"(saved));
    }
    if (!proc_save_fpstate(fp)) dr_abort();
    /* The source environment format defines reserved words as zero. Named
     * environment/register fields still come from the actual hardware store. */
    if (!short_environment) {
        memset(saved.data + 2, 0, 2); memset(saved.data + 6, 0, 2);
        memset(saved.data + 10, 0, 2); memset(saved.data + 26, 0, 2);
        saved.data[19] &= 7;
    }
    if (!copy_to_app((void *)(ptr_uint_t)address, saved.data, bytes)) dr_abort();
    redirect(cpu, fp, description->pc + description->size);
}
static void double_shift(profile_instruction *description, dr_mcontext_t *cpu, byte *fp)
{
    /* The real decoder lists Gv, count, Ev as sources and Ev as destination.
     * Source count-zero only reads Ev: skip the hardware RMW altogether. */
    opnd_t destination = instr_get_dst(description->operands, 0);
    opnd_t count_operand = instr_get_src(description->operands, 1);
    size_t bytes = opnd_size_in_bytes(opnd_get_size(destination));
    if (bytes != 2 && bytes != 4 && bytes != 8) dr_abort();
    uint64_t raw;
    if (opnd_is_immed_int(count_operand)) raw = (uint64_t)opnd_get_immed_int(count_operand);
    else if (opnd_is_reg(count_operand)) raw = (uint64_t)reg_get_value(opnd_get_reg(count_operand), cpu);
    else { dr_abort(); return; }
    uint64_t count = raw & (bytes == 8 ? 63u : 31u);
    if (count > bytes * 8)
        reject(cpu, fp, description->pc, GUEST_PROFILE_GUARD_INSTRUCTION, 0, 0, 0, description->size);
    uint64_t value = 0, address = 0;
    if (opnd_is_memory_reference(destination)) {
        address = operand_address(description, destination, cpu, bytes);
        uint64_t bad = address;
        if (!canonical(address))
            reject(cpu, fp, description->pc, GUEST_PROFILE_GUARD_PROCESSOR, 0, 13, address, bytes);
        if (count && !range(address, bytes, QA_NATIVE_GUEST_WRITE, &bad))
            reject(cpu, fp, description->pc, GUEST_PROFILE_GUARD_OPERAND, QA_NATIVE_GUEST_WRITE, 0, bad, bytes);
        if (!range(address, bytes, QA_NATIVE_GUEST_READ, &bad))
            reject(cpu, fp, description->pc, GUEST_PROFILE_GUARD_OPERAND, QA_NATIVE_GUEST_READ, 0, bad, bytes);
        if (!copy_from_app((void *)(ptr_uint_t)address, &value, bytes)) dr_abort();
    } else if (opnd_is_reg(destination))
        value = (uint64_t)reg_get_value(opnd_get_reg(destination), cpu);
    else dr_abort();
    if (count) {
        opnd_t source = instr_get_src(description->operands, 0);
        if (!opnd_is_reg(source)) dr_abort();
        uint64_t other = (uint64_t)reg_get_value(opnd_get_reg(source), cpu);
        uint64_t mask = bytes == 8 ? UINT64_MAX : (UINT64_C(1) << (bytes * 8)) - 1;
        bool right = instr_get_opcode(description->operands) == OP_shrd;
        uint64_t result = (right ? (value >> count) | (other << (bytes * 8 - count)) :
            (value << count) | (other >> (bytes * 8 - count))) & mask;
        uint64_t carry = (value >> (right ? count - 1 : bytes * 8 - count)) & 1;
        unsigned low = (unsigned)result & 255u;
        low ^= low >> 4;
        uint64_t flags = carry | ((UINT64_C(0x9669) >> (low & 15u)) & 1u) << 2 |
            (!result ? 0x40u : 0) | ((result >> (bytes * 8 - 1)) & 1u) << 7;
        uint64_t changed = 0xc5;
        /* Source retains AF, and OF when count != 1. Hardware leaves these
         * undefined, so compute only the actual source-owned flag fields. */
        if (count == 1) { changed |= 0x800; flags |= ((value ^ result) >> (bytes * 8 - 1) & 1u) << 11; }
        cpu->xflags = (cpu->xflags & ~(reg_t)changed) | (reg_t)flags;
        if (opnd_is_memory_reference(destination)) {
            if (!copy_to_app((void *)(ptr_uint_t)address, &result, bytes)) dr_abort();
        } else {
            reg_id_t full = reg_resize_to_opsz(opnd_get_reg(destination), OPSZ_8);
            uint64_t original = (uint64_t)reg_get_value(full, cpu);
            reg_set_value(full, cpu, (reg_t)(bytes == 2 ? (original & ~mask) | result : result));
        }
    }
    redirect(cpu, fp, description->pc + description->size);
}
static void clean(ptr_uint_t encoded)
{
    profile_instruction *description = (profile_instruction *)encoded;
    app_pc pc = description->pc;
    if (pc != control_entry && (!scope || scope->phase == PROFILE_STOPPED ||
        (scope->phase == PROFILE_ARMED && pc != (app_pc)(ptr_uint_t)scope->entry))) return;
    void *context = dr_get_current_drcontext();
    if (dr_get_thread_id(context) != owner_thread) dr_abort();
    byte fp_raw[DR_FPSTATE_BUF_SIZE + DR_FPSTATE_ALIGN];
    byte *fp = (byte *)(((ptr_uint_t)fp_raw + DR_FPSTATE_ALIGN - 1) & ~(ptr_uint_t)(DR_FPSTATE_ALIGN - 1));
    if (!proc_save_fpstate(fp)) dr_abort();
    /* Normalize while the actual fragment still exists, before a code-change
     * flush can retire its translation. The restored hardware FIP consequently
     * remains an original address across the following non-x87 instruction. */
    if (!fp_original(context, fp)) dr_abort();
    dr_mcontext_t cpu = {.size = sizeof(cpu), .flags = DR_MC_ALL};
    if (!dr_get_mcontext(context, &cpu)) dr_abort();
    if (pc == control_entry) {
        /* Controller control is never an allowed destination from guest code. */
        if (scope && scope->phase == PROFILE_ACTIVE) reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_FETCH, QA_NATIVE_GUEST_EXECUTE, 0, (uint64_t)(ptr_uint_t)pc, description->size);
        guest_profile_guard_control control;
        guest_profile_guard_control *destination = (guest_profile_guard_control *)cpu.xdi;
        if (!copy_from_app(destination, &control, sizeof(control))) dr_abort();
        control.status = control_apply(&control) ? 1 : 0;
        control.installed = control.status ? (guest_profile_guard_receipt){GUEST_PROFILE_GUARD_SOURCE_X64} :
            (guest_profile_guard_receipt){0};
        if (!copy_to_app(destination, &control, sizeof(control))) dr_abort();
        proc_restore_fpstate(fp); return;
    }
    scope->phase = PROFILE_ACTIVE;
    if (pc == fault_entry || pc == import_entry) {
        guest_profile_guard_fault receipt;
        if (!copy_from_app(scope->fault, &receipt, sizeof(receipt)) || receipt.scope != scope->id ||
            (pc == import_entry ? (receipt.kind != GUEST_PROFILE_GUARD_ENTRY && receipt.kind != GUEST_PROFILE_GUARD_SYSCALL &&
                receipt.kind != GUEST_PROFILE_GUARD_STORE && receipt.kind != GUEST_PROFILE_GUARD_BOUNDARY &&
                receipt.kind != GUEST_PROFILE_GUARD_RETURN) :
                receipt.kind == GUEST_PROFILE_GUARD_NO_FAULT || receipt.kind == GUEST_PROFILE_GUARD_ENTRY ||
                    receipt.kind == GUEST_PROFILE_GUARD_SYSCALL || receipt.kind == GUEST_PROFILE_GUARD_STORE ||
                    receipt.kind == GUEST_PROFILE_GUARD_BOUNDARY || receipt.kind == GUEST_PROFILE_GUARD_RETURN)) dr_abort();
        proc_restore_fpstate(fp); return;
    }
    if (scope->pending_at < scope->pending_count) {
        profile_store store = scope->pending[scope->pending_at++];
        guest_profile_guard_fault committed = {GUEST_PROFILE_GUARD_STORE, QA_NATIVE_GUEST_WRITE,
            scope->pending_at == scope->pending_count, scope->id, store.instruction,
            store.address, store.bytes, (uint64_t)(ptr_uint_t)pc};
        if (!copy_to_app(scope->fault, &committed, sizeof(committed))) dr_abort();
        redirect(&cpu, fp, import_entry);
    }
    scope->pending_count = 0; scope->pending_at = 0;
    uint64_t bad;
    for (size_t i = 0; i < scope->callback_count; ++i) if (scope->callbacks[i].address == (uint64_t)(ptr_uint_t)pc) {
        if (!range((uint64_t)(ptr_uint_t)pc, 1, QA_NATIVE_GUEST_EXECUTE, &bad))
            reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_FETCH, QA_NATIVE_GUEST_EXECUTE, 0, bad, 1);
        if (scope->callbacks[i].id == scope->bypass) { scope->bypass = 0; break; }
        guest_profile_guard_fault entry = {GUEST_PROFILE_GUARD_ENTRY, 0, 0,
            scope->id, (uint64_t)(ptr_uint_t)pc, (uint64_t)(ptr_uint_t)pc, 1, (uint64_t)(ptr_uint_t)pc};
        if (!copy_to_app(scope->fault, &entry, sizeof(entry))) dr_abort();
        redirect(&cpu, fp, import_entry);
    }
    bool bypass_boundary = scope->boundary_bypass == (uint64_t)(ptr_uint_t)pc;
    scope->boundary_bypass = 0;
    if (!bypass_boundary) for (size_t i = 0; i < scope->interest_count; ++i) {
        const guest_profile_interest *interest = scope->interests + i;
        if (interest->kind != GUEST_PROFILE_INTEREST_INSTRUCTION || interest->address != (uint64_t)(ptr_uint_t)pc) continue;
        guest_profile_guard_fault boundary = {GUEST_PROFILE_GUARD_BOUNDARY, 0, 0, scope->id,
            interest->address, interest->address, 1, interest->address};
        if (!copy_to_app(scope->fault, &boundary, sizeof(boundary))) dr_abort();
        scope->boundary_bypass = interest->address;
        redirect(&cpu, fp, import_entry);
    }
    if (!range((uint64_t)(ptr_uint_t)pc, description->size, QA_NATIVE_GUEST_EXECUTE, &bad)) reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_FETCH, QA_NATIVE_GUEST_EXECUTE, 0, bad, description->size);
    byte actual[15];
    if (!copy_from_app(pc, actual, description->size)) reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_FETCH, QA_NATIVE_GUEST_EXECUTE, 0, (uint64_t)(ptr_uint_t)pc, description->size);
    if (memcmp(actual, description->bytes, description->size)) {
        if (!dr_unlink_flush_region(pc, description->size)) dr_abort();
        redirect(&cpu, fp, pc);
    }
    if ((uint64_t)(ptr_uint_t)pc == scope->stop && description->size == 1 && actual[0] == 0xcc) {
        guest_profile_guard_fault returned = {GUEST_PROFILE_GUARD_RETURN, 0, 0, scope->id,
            scope->stop, scope->stop, 1, scope->stop};
        if (!copy_to_app(scope->fault, &returned, sizeof(returned))) dr_abort();
        redirect(&cpu, fp, import_entry);
    }
    guest_profile_instruction policy = guest_profile_x64_instruction((qa_bytes){actual, description->size});
    if (policy.kind == GUEST_PROFILE_UNSUPPORTED) reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_INSTRUCTION, 0, 0, 0, description->size);
    if (policy.kind == GUEST_PROFILE_PROCESSOR_FAULT) reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_PROCESSOR, 0, policy.vector, 0, description->size);
    if (policy.kind == GUEST_PROFILE_SYSCALL) {
        if (!scope->syscalls || description->size != 2)
            reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_INSTRUCTION, 0, 0, 0, description->size);
        guest_profile_guard_fault syscall = {GUEST_PROFILE_GUARD_SYSCALL, 0, 0,
            scope->id, (uint64_t)(ptr_uint_t)pc, (uint64_t)(ptr_uint_t)pc + 2, 2, (uint64_t)(ptr_uint_t)pc};
        if (!copy_to_app(scope->fault, &syscall, sizeof(syscall))) dr_abort();
        redirect(&cpu, fp, import_entry);
    }
    if (policy.kind == GUEST_PROFILE_CPUID) {
        uint32_t output[4]; guest_profile_x64_cpuid((uint32_t)cpu.xax, (uint32_t)cpu.xcx, output);
        cpu.xax = output[0]; cpu.xbx = output[1]; cpu.xcx = output[2]; cpu.xdx = output[3];
        redirect(&cpu, fp, pc + description->size);
    }
    byte op = description->opcode, modrm = description->modrm;
    unsigned group = (modrm >> 3) & 7;
    bool numeric = op == 0x9b || (op >= 0xd8 && op <= 0xdf);
    bool no_wait = (op == 0xdb && (modrm == 0xe2 || modrm == 0xe3)) ||
        (op == 0xdf && modrm == 0xe0) ||
        ((op == 0xd9 || op == 0xdd) && modrm < 0xc0 && (group == 6 || group == 7));
    uint16_t control, status;
    memcpy(&control, fp, 2); memcpy(&status, fp + 2, 2);
    if (numeric && !no_wait && (status & ~control & 63))
        reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_PROCESSOR, 0, 16, 0, description->size);
    size_t popped = 0;
    int opcode = description->operands ? instr_get_opcode(description->operands) : OP_INVALID;
    if (opcode == OP_pop && opnd_is_memory_reference(instr_get_dst(description->operands, 0))) {
        popped = drutil_opnd_mem_size_in_bytes(instr_get_dst(description->operands, 0), description->operands);
        bad = (uint64_t)cpu.xsp;
        if (!canonical(bad)) reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_PROCESSOR, 0, 13, bad, popped);
        if (!popped || !range((uint64_t)cpu.xsp, popped, QA_NATIVE_GUEST_READ, &bad))
            reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_OPERAND, QA_NATIVE_GUEST_READ, 0, bad, popped);
    }
    /* Source PUSH/CALL and MOVS read their actual source before checking the
     * destination. DR's address iterator lists destinations first. */
    bool source_first = opcode == OP_push || opcode == OP_call || opcode == OP_call_ind ||
        op == 0xa4 || op == 0xa5;
    if (description->operands) for (unsigned pass = 0; pass < (source_first ? 2u : 1u); ++pass) {
        if (pass && (opcode == OP_call || opcode == OP_call_ind)) {
            /* The real source reads and validates its target before its stack
             * write. Use DR's actual decoded target operand, never a guessed ABI. */
            opnd_t operand = instr_get_target(description->operands);
            uint64_t target;
            if (opnd_is_pc(operand)) target = (uint64_t)(ptr_uint_t)opnd_get_pc(operand);
            else if (opnd_is_reg(operand)) target = (uint64_t)reg_get_value(opnd_get_reg(operand), &cpu);
            else if (opnd_is_memory_reference(operand)) {
                uint64_t address = operand_address(description, operand, &cpu, sizeof(target));
                if (!copy_from_app((void *)(ptr_uint_t)address, &target, sizeof(target))) dr_abort();
            } else dr_abort();
            if (!canonical(target)) reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_PROCESSOR, 0, 13, target, description->size);
        }
        for (uint index = 0;; ++index) {
            app_pc address; bool write; uint position;
            if (!instr_compute_address_ex_pos(description->operands, &cpu, index, &address, &write, &position)) break;
            if (source_first && write != (pass != 0)) continue;
            opnd_t operand = write ? instr_get_dst(description->operands, (int)position) : instr_get_src(description->operands, (int)position);
            size_t bytes = drutil_opnd_mem_size_in_bytes(operand, description->operands);
            dr_mcontext_t operand_cpu = cpu;
            if (write && popped) operand_cpu.xsp += popped;
            uint64_t actual_address = operand_address(description, operand, &operand_cpu, bytes);
            uint32_t rights = write ? QA_NATIVE_GUEST_WRITE : QA_NATIVE_GUEST_READ;
            bad = actual_address;
            if (!canonical(actual_address))
                reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_PROCESSOR, 0, 13, bad, bytes);
            if (!bytes || !range(actual_address, bytes, rights, &bad)) {
                /* Source execution restores the pre-instruction GPR bank on an
                 * operand fault, including POP/LEAVE's intermediate SP changes. */
                reject(&cpu, fp, pc, GUEST_PROFILE_GUARD_OPERAND, rights, 0, bad, bytes);
            }
            if (write) store_interest((uint64_t)(ptr_uint_t)pc, actual_address, bytes);
        }
    }
    if (opcode == OP_shld || opcode == OP_shrd) double_shift(description, &cpu, fp);
    if ((op == 0xd9 || op == 0xdd) && modrm < 0xc0 && group == 6)
        environment_store(description, &cpu, fp, op);
    proc_restore_fpstate(fp);
}
static dr_emit_flags_t app2app(void *context, void *tag, instrlist_t *list, bool trace, bool translating)
{
    (void)tag; (void)trace; (void)translating;
    if (!drutil_expand_rep_string(context, list)) dr_abort();
    return DR_EMIT_DEFAULT;
}
static dr_emit_flags_t instrument(void *context, void *tag, instrlist_t *list, instr_t *where,
    bool trace, bool translating, void *user)
{
    (void)tag; (void)trace; (void)translating; (void)user;
    instr_t *fetch = drmgr_orig_app_instr_for_fetch(context);
    instr_t *operands = drmgr_orig_app_instr_for_operands(context);
    if (!fetch && !operands) return DR_EMIT_DEFAULT;
    /* Expanded REP operands have synthetic translations inside the original
     * instruction. Policy/fetch always use the actual original emulation row. */
    const emulated_instr_t *emulated = NULL;
    instr_t *original = fetch ? fetch : operands;
    app_pc pc = instr_get_app_pc(original);
    if (drmgr_in_emulation_region(context, &emulated)) {
        original = emulated->instr; pc = emulated->pc;
    }
    if (!pc) return DR_EMIT_DEFAULT;
    profile_instruction *row = dr_global_alloc(sizeof(*row));
    if (!row) dr_abort();
    memset(row, 0, sizeof(*row)); row->pc = pc;
    size_t bytes = instr_length(context, original);
    if (!bytes || bytes > 15) dr_abort();
    row->size = bytes;
    if (!copy_from_app(pc, row->bytes, bytes)) dr_abort();
    size_t opcode_at = 0;
    for (; opcode_at < bytes; ++opcode_at) {
        size_t i = opcode_at;
        byte p = row->bytes[i];
        if (!((p >= 0x40 && p <= 0x4f) || p == 0x66 || p == 0x67 || p == 0xf2 || p == 0xf3 ||
            p == 0xf0 || p == 0x64 || p == 0x65 || p == 0x2e || p == 0x36 || p == 0x3e || p == 0x26)) break;
    }
    if (opcode_at < bytes) {
        row->opcode = row->bytes[opcode_at];
        if (opcode_at + 1 < bytes) row->modrm = row->bytes[opcode_at + 1];
    }
    row->operands = operands ? instr_clone(context, operands) : NULL;
    if (operands && !row->operands) dr_abort();
    if (row->operands) instr_make_persistent(context, row->operands);
    row->next = instructions; instructions = row;
    dr_insert_clean_call_ex(context, list, where, (void *)clean,
        DR_CLEANCALL_READS_APP_CONTEXT | DR_CLEANCALL_WRITES_APP_CONTEXT, 1, OPND_CREATE_INTPTR(row));
    return DR_EMIT_DEFAULT;
}
static dr_emit_flags_t meta_branches(void *context, void *tag, instrlist_t *list,
    bool trace, bool translating)
{
    (void)tag; (void)trace; (void)translating;
    /* REP expansion adds an eight-bit JECXZ. The actual guard clean calls can
     * exceed that reach; use DR's long meta sequence after insertion. */
    for (instr_t *row = instrlist_first(list), *next; row; row = next) {
        next = instr_get_next(row);
        /* drmgr marks intra-block application branches meta only after this
         * phase. Apply that same rule before asking DR to widen their reach. */
        if (instr_is_cti_short(row) && opnd_is_instr(instr_get_target(row))) {
            instr_set_meta(row);
            instr_set_translation(row, NULL);
            instr_convert_short_meta_jmp_to_long(context, list, row);
        }
    }
    return DR_EMIT_DEFAULT;
}
static void module_load(void *context, const module_data_t *module, bool loaded)
{
    (void)loaded;
    if (control_entry) return;
    app_pc entry = (app_pc)dr_get_proc_address(module->handle, "qa_guest_profile_control");
    if (!entry) return;
    app_pc fault = (app_pc)dr_get_proc_address(module->handle, "qa_guest_profile_fault");
    app_pc import = (app_pc)dr_get_proc_address(module->handle, "qa_guest_profile_import");
    if (!fault || !import) dr_abort();
    control_entry = entry; fault_entry = fault; import_entry = import;
    owner_thread = dr_get_thread_id(context);
}
static dr_signal_action_t signal_event(void *context, dr_siginfo_t *information)
{
    /* A terminal INT3 has no following app instruction for DR's post-trap PC
     * translation. Recover the actual trap from the stopped cache instruction,
     * only for this scope's admitted import marker or return trap. */
    if (scope && information->sig == SIGTRAP && information->mcontext &&
        !information->mcontext->xip && information->raw_mcontext &&
        information->raw_mcontext->xip) {
        app_pc original = dr_app_pc_from_cache_pc(information->raw_mcontext->xip - 1);
        byte opcode;
        if (original && (original == import_entry ||
            original == (app_pc)(ptr_uint_t)scope->stop) &&
            copy_from_app(original, &opcode, 1) && opcode == 0xcc)
            information->mcontext->xip = original + 1;
    }
    /* DR's decoder can forge SIGILL before presenting an invalid first
     * instruction to a bb callback (core/arch/interp.c bb_process_invalid_instr).
     * The source callback table dispatches that actual entry before decoding.
     * Recover only the genuine published entry and execute permission receipt;
     * unrelated processor/memory faults continue to the normal child bridge. */
    if (scope && information->mcontext && (scope->phase == PROFILE_ACTIVE ||
        (scope->phase == PROFILE_ARMED &&
            information->mcontext->xip == (app_pc)(ptr_uint_t)scope->entry)) &&
        (information->sig == SIGILL || information->sig == SIGSEGV || information->sig == SIGBUS)) {
        uint64_t pc = (uint64_t)(ptr_uint_t)information->mcontext->xip;
        byte trap;
        if (pc == scope->stop && scope_range(scope, pc, 1,
            QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_EXECUTE, NULL) &&
            copy_from_app((void *)(ptr_uint_t)pc, &trap, 1) && trap == 0xcc) {
            guest_profile_guard_fault returned = {GUEST_PROFILE_GUARD_RETURN, 0, 0,
                scope->id, pc, pc, 1, pc};
            if (!copy_to_app(scope->fault, &returned, sizeof(returned))) dr_abort();
            scope->phase = PROFILE_ACTIVE;
            information->mcontext->xip = import_entry;
            return DR_SIGNAL_REDIRECT;
        }
        for (size_t i = 0; i < scope->callback_count; ++i) if (scope->callbacks[i].address == pc &&
            range(pc, 1, QA_NATIVE_GUEST_EXECUTE, NULL)) {
            if (scope->callbacks[i].id == scope->bypass) { scope->bypass = 0; break; }
            if (dr_get_thread_id(context) != owner_thread) dr_abort();
            guest_profile_guard_fault entry = {GUEST_PROFILE_GUARD_ENTRY, 0, 0,
                scope->id, pc, pc, 1, pc};
            if (!copy_to_app(scope->fault, &entry, sizeof(entry))) dr_abort();
            scope->phase = PROFILE_ACTIVE;
            information->mcontext->xip = import_entry;
            return DR_SIGNAL_REDIRECT;
        }
    }
    /* An unrelated ignored asynchronous signal must not turn off the active
     * source guard. These are exactly the installed child bridge's stops. */
    if (scope && (scope->phase == PROFILE_ACTIVE ||
        (scope->phase == PROFILE_ARMED && information->mcontext &&
            information->mcontext->xip == (app_pc)(ptr_uint_t)scope->entry)) &&
        (information->sig == SIGTRAP ||
        information->sig == SIGILL || information->sig == SIGFPE ||
        information->sig == SIGSEGV || information->sig == SIGBUS)) {
        if (dr_get_thread_id(context) != owner_thread) dr_abort();
        if (information->mcontext && information->mcontext->xip != import_entry + 1)
            scope->pending_count = scope->pending_at = 0;
        scope->phase = PROFILE_STOPPED;
    }
    (void)information;
    return DR_SIGNAL_DELIVER;
}
static bool syscall_filter(void *context, int number)
{
    (void)context; (void)number;
    /* The filter is evaluated for translated sites, rather than for a single
     * current invocation. Keep delivery installed for every actual site. */
    return true;
}
static bool syscall_pre(void *context, int number)
{
    (void)context; (void)number;
    /* Guest SYSCALL is redirected by its qualified instruction clean call.
     * Never let an active source instruction fall through to the host OS.
     * ARMED entry plumbing and STOPPED controller services are separate. */
    if (scope && scope->phase == PROFILE_ACTIVE) dr_abort();
    return true;
}
static void process_exit(void)
{
    while (scope) { profile_scope *next = scope->parent; scope_free(scope); scope = next; }
    void *context = dr_get_current_drcontext();
    while (instructions) {
        profile_instruction *next = instructions->next;
        if (instructions->operands) instr_destroy(context, instructions->operands);
        dr_global_free(instructions, sizeof(*instructions)); instructions = next;
    }
    drmgr_unregister_signal_event(signal_event); drmgr_unregister_module_load_event(module_load);
    drmgr_unregister_pre_syscall_event(syscall_pre); drmgr_unregister_filter_syscall_event(syscall_filter);
    drmgr_unregister_bb_app2app_event(app2app); drmgr_unregister_bb_insertion_event(instrument);
    drmgr_unregister_bb_instru2instru_event(meta_branches);
    drutil_exit(); drmgr_exit();
}
DR_EXPORT void dr_client_main(client_id_t id, int argc, const char *argv[])
{
    (void)id; (void)argc; (void)argv;
    dr_set_client_name("Quake Anthology source instruction guard", "");
    unsigned a, b, c, d;
    if (__get_cpuid_count(0x0d, 9, &a, &b, &c, &d) && a) {
        if (a != sizeof(uint64_t) || b < 576 || (c & 1)) dr_abort();
        pkru_offset = b;
    }
    uint64 setting;
    /* A direct branch elided before client instrumentation would omit its
     * source fetch/canonical-target admission. Qualify the real engine values,
     * including its actual virtualized app segments, before writing PROBE. */
    if (!dr_get_integer_option("max_elide_jmp", &setting) || setting ||
        !dr_get_integer_option("max_elide_call", &setting) || setting ||
        !dr_get_integer_option("disable_traces", &setting) || !setting ||
        !dr_get_integer_option("mangle_app_seg", &setting) || !setting ||
        !dr_get_integer_option("translate_fpu_pc", &setting) || !setting) dr_abort();
    dr_track_where_am_i();
    if (!dr_using_all_private_caches() || !drmgr_init() || !drutil_init() ||
        !drmgr_register_module_load_event(module_load) || !drmgr_register_signal_event(signal_event) ||
        !drmgr_register_filter_syscall_event(syscall_filter) || !drmgr_register_pre_syscall_event(syscall_pre) ||
        !drmgr_register_bb_app2app_event(app2app, NULL) || !drmgr_register_bb_instrumentation_event(NULL, instrument, NULL) ||
        !drmgr_register_bb_instru2instru_event(meta_branches, NULL) ||
        !drmgr_register_exit_event(process_exit)) dr_abort();
}
#else
int qa_native_profile_client_requires_linux_x64_dynamorio;
#endif
