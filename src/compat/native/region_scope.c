#include "internal.h"

struct qa_native_region_snapshot {
    struct qa_native_region_snapshot *next;
    qa_native_region_scope *scope;
    qa_native_guest_cpu cpu;
    uint32_t depth;
};
struct qa_native_region_scope {
    struct qa_native_region_scope *next;
    qa_native_instance *instance;
    qa_native_guest *guest;
    qa_native_module *module;
    qa_native_address image, target;
    qa_native_declared_region region;
    qa_native_region_scope_fn callback;
    void *context;
    qa_native_region_snapshot *snapshots;
    const qa_native_region_scope_event *event;
    uint32_t parent_calls, parent_callbacks, parent_regions, parent_services;
    uint32_t entered_depth, calls;
    bool invoking, invoked, reached;
};
static bool fail(qa_error *error, const char *message) {
    return native_fail(error, QA_ERROR_ARGUMENT, 0, "%s", message);
}
bool qa_native_region_scope_argument_bytes(const qa_native_signature *signature,
    size_t *out, qa_error *error) {
    if (!signature || signature->variadic || !out)
        return fail(error, "Region argument extent requires its actual fixed ABI signature");
    guest_abi_plan *plan = NULL;
    if (!guest_abi_plan_native(signature, NULL, 0, &plan, error)) return false;
    *out = guest_abi_argument_bytes(plan);
    guest_abi_plan_destroy(plan);
    return true;
}
static bool physical(const qa_native_region_scope *scope) {
    return scope && scope->instance && scope->instance->module == scope->module &&
        scope->instance->guest == scope->guest && scope->instance->image_base == scope->image;
}
bool qa_native_region_scope_current(const qa_native_region_scope *scope) {
    if (!physical(scope)) return false;
    const qa_native_instance *instance = scope->instance;
    return !instance->failed && !instance->checkpointing && !instance->destroying &&
        !instance->unloading && instance->lifecycle == QA_NATIVE_INITIALIZED &&
        !instance->process_host_pending && !qa_native_terminal(instance);
}
static bool parent_returned(const qa_native_region_scope *scope) {
    const qa_native_instance *instance = scope->instance;
    return physical(scope) && !scope->invoking && !scope->calls && !scope->event &&
        instance->active_depth == scope->parent_calls &&
        instance->callback_depth == scope->parent_callbacks &&
        instance->region_depth == scope->parent_regions &&
        instance->region_service_depth == scope->parent_services &&
        (!scope->parent_calls || native_active_instance == instance);
}
bool qa_native_region_scope_open(qa_native_instance *instance,
    const qa_native_declaration *declaration, uint32_t id, qa_native_address target,
    qa_native_region_scope_fn callback, void *context, qa_native_region_scope **out, qa_error *error) {
    if (!instance || !declaration || !callback || !out || *out ||
        !instance->has_declaration || !qa_sha256_equal(&instance->declaration, &declaration->digest) ||
        id >= declaration->region_count || id >= instance->region_count ||
        instance->failed || instance->checkpointing || instance->destroying || instance->unloading ||
        instance->write_depth || instance->lifecycle != QA_NATIVE_INITIALIZED || instance->process_host_pending ||
        (instance->active_depth && (native_active_instance != instance || !instance->callback_depth)))
        return fail(error, "Region scope requires its actual acquired declaration and returned or entered source owner");
    if (instance->backend != QA_NATIVE_BACKEND_OWNED_PROCESS || !instance->guest ||
        qa_native_guest_execution(instance->guest) != QA_NATIVE_GUEST_EMULATED)
        return native_fail(error, QA_ERROR_UNSUPPORTED, 0,
            "Dynamic region scope requires its actual full stopped processor owner");
    const qa_native_declared_region *declared = &declaration->regions[id].definition;
    const qa_native_declared_region *actual = &instance->regions[id].definition;
    if (!declared->has_frame || !actual->has_frame || declared->entry_rva != actual->entry_rva ||
        declared->join_rva != actual->join_rva || declared->frame_entry_rva != actual->frame_entry_rva ||
        declared->frame_exit_rva != actual->frame_exit_rva || strcmp(declared->path, actual->path))
        return fail(error, "Region scope differs from its admitted physical frame");
    uint32_t bounds[] = {actual->frame_entry_rva, actual->entry_rva, actual->join_rva, actual->frame_exit_rva};
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < i; ++j) if (bounds[i] == bounds[j])
            return fail(error, "Region scope has repeated frame boundaries");
        qa_native_address address;
        if (!qa_native_rva(instance, bounds[i], 1, &address, error) ||
            !qa_native_range_check(instance, address, 1, QA_NATIVE_MEMORY_EXECUTE, error)) return false;
    }
    if (target < instance->image_base || target - instance->image_base >= instance->image_bytes ||
        !qa_native_range_check(instance, target, 1, QA_NATIVE_MEMORY_EXECUTE, error))
        return fail(error, "Region target is outside its actual executable source image");
    qa_native_region_scope *scope = calloc(1, sizeof(*scope));
    if (!scope) return native_fail(error, QA_ERROR_MEMORY, 0, "Retaining dynamic source region scope");
    scope->instance = instance; scope->module = instance->module; scope->guest = instance->guest;
    scope->image = instance->image_base; scope->target = target; scope->region = *actual;
    scope->callback = callback; scope->context = context;
    scope->parent_calls = instance->active_depth; scope->parent_callbacks = instance->callback_depth;
    scope->parent_regions = instance->region_depth; scope->parent_services = instance->region_service_depth;
    scope->next = instance->region_scopes; instance->region_scopes = scope;
    *out = scope; return true;
}
bool qa_native_region_scope_invoke(qa_native_region_scope *scope, const qa_native_signature *signature,
    const qa_native_value *arguments, size_t count, qa_native_value *result, bool *entered, qa_error *error) {
    if (!entered) return fail(error, "Region invocation requires its actual entered receipt");
    *entered = false;
    if (!qa_native_region_scope_current(scope) || !parent_returned(scope) || scope->invoked ||
        !signature || signature->variadic || signature->abi != scope->module->info.image.target.abi ||
        count != signature->parameter_count || (count && !arguments) || scope->parent_calls == UINT32_MAX)
        return fail(error, "Region invocation differs from its retained target, fixed ABI or parent continuation");
    qa_native_instance *instance = scope->instance;
    scope->invoking = true; scope->invoked = true;
    bool *previous = instance->invoke_entered;
    instance->invoke_entered = entered;
    bool okay = native_invoke_entry(instance, scope->target, signature, arguments, count, result, error);
    instance->invoke_entered = previous;
    scope->invoking = false;
    return okay;
}
static bool event_current(const qa_native_region_scope *scope, const qa_native_region_scope_event *event) {
    return qa_native_region_scope_current(scope) && event && event == scope->event && event->scope == scope &&
        scope->invoking && scope->calls && native_active_instance == scope->instance &&
        scope->instance->active_depth == scope->entered_depth;
}
bool qa_native_region_scope_capture(qa_native_region_scope *scope,
    const qa_native_region_scope_event *event, qa_native_region_snapshot **out, qa_error *error) {
    if (!out || *out || !event_current(scope, event))
        return fail(error, "Processor capture requires the exact borrowed stopped region event");
    qa_native_region_snapshot *snapshot = calloc(1, sizeof(*snapshot));
    if (!snapshot) return native_fail(error, QA_ERROR_MEMORY, 0, "Retaining full stopped processor snapshot");
    if (!qa_native_guest_cpu_read(scope->guest, &snapshot->cpu, error)) { free(snapshot); return false; }
    snapshot->scope = scope; snapshot->depth = scope->entered_depth;
    snapshot->next = scope->snapshots; scope->snapshots = snapshot;
    *out = snapshot; return true;
}
bool qa_native_region_scope_write(qa_native_region_scope *scope,
    const qa_native_region_scope_event *event, const qa_native_processor_state *state, qa_error *error) {
    if (!state || !event_current(scope, event))
        return fail(error, "Processor write requires the exact stopped source region event");
    qa_native_guest_cpu cpu;
    if (!qa_native_guest_cpu_read(scope->guest, &cpu, error)) return false;
    memcpy(cpu.registers, state->registers, sizeof(cpu.registers));
    memcpy(cpu.xmm, state->simd, sizeof(cpu.xmm)); cpu.flags = state->flags;
    return qa_native_guest_cpu_write(scope->guest, &cpu, error);
}
bool qa_native_region_scope_restore(const qa_native_region_snapshot *snapshot,
    const qa_native_region_scope_event *event, qa_native_region_scope_decision *decision, qa_error *error) {
    if (!snapshot || !decision || !event_current(snapshot->scope, event) ||
        snapshot->depth != snapshot->scope->entered_depth)
        return fail(error, "Processor restore requires its actual scope and current stopped continuation");
    decision->restore = snapshot; decision->replace_state = true;
    memcpy(decision->state.registers, snapshot->cpu.registers, sizeof(decision->state.registers));
    memcpy(decision->state.simd, snapshot->cpu.xmm, sizeof(decision->state.simd));
    decision->state.flags = snapshot->cpu.flags; decision->state.instruction = event->state.instruction;
    return true;
}
static bool boundary(qa_native_region_scope *scope, qa_native_region_scope_phase phase,
    uint64_t instruction, bool *redirected, qa_error *error) {
    qa_native_instance *instance = scope->instance;
    qa_native_guest_cpu cpu;
    if (!qa_native_region_scope_current(scope) || native_active_instance != instance ||
        scope->calls == UINT32_MAX || instance->callback_depth == UINT32_MAX || instance->region_depth == UINT32_MAX)
        return fail(error, "Region boundary lost its actual stopped owner or callback capacity");
    if (!qa_native_guest_cpu_read(scope->guest, &cpu, error)) return false;
    qa_native_region_scope_event event = {.scope = scope, .region = scope->region, .phase = phase};
    memcpy(event.state.registers, cpu.registers, sizeof(event.state.registers));
    memcpy(event.state.simd, cpu.xmm, sizeof(event.state.simd));
    event.state.flags = cpu.flags; event.state.instruction = instruction;
    qa_native_region_scope_decision decision = {.state = event.state};
    ++scope->calls; ++instance->callback_depth; ++instance->region_depth;
    scope->event = &event;
    bool okay = scope->callback(scope->context, instance, &event, &decision, error);
    scope->event = NULL;
    --instance->region_depth; --instance->callback_depth; --scope->calls;
    if (!okay) return false;
    if (!qa_native_region_scope_current(scope) || (unsigned)decision.action > QA_NATIVE_SCOPE_SKIP_TO_FRAME_EXIT ||
        (decision.action == QA_NATIVE_SCOPE_SKIP_TO_ENTRY && phase != QA_NATIVE_SCOPE_FRAME_ENTRY) ||
        (decision.action == QA_NATIVE_SCOPE_SKIP_TO_JOIN && phase != QA_NATIVE_SCOPE_REGION_ENTER) ||
        (decision.action == QA_NATIVE_SCOPE_SKIP_TO_FRAME_EXIT && phase != QA_NATIVE_SCOPE_REGION_JOIN))
        return fail(error, "Region callback returned a stale processor or invalid source slice redirect");
    if (decision.restore) {
        bool found = false;
        for (const qa_native_region_snapshot *row = scope->snapshots; row; row = row->next)
            if (row == decision.restore && row->depth == scope->entered_depth) { found = true; break; }
        if (!found) return fail(error, "Region processor snapshot belongs to another retained frame");
        cpu = decision.restore->cpu;
    } else if (!qa_native_guest_cpu_read(scope->guest, &cpu, error)) return false;
    if (decision.replace_state) {
        memcpy(cpu.registers, decision.state.registers, sizeof(cpu.registers));
        memcpy(cpu.xmm, decision.state.simd, sizeof(cpu.xmm)); cpu.flags = decision.state.flags;
    }
    cpu.instruction = instruction;
    if (decision.action == QA_NATIVE_SCOPE_SKIP_TO_ENTRY) cpu.instruction = scope->image + scope->region.entry_rva;
    else if (decision.action == QA_NATIVE_SCOPE_SKIP_TO_JOIN) cpu.instruction = scope->image + scope->region.join_rva;
    else if (decision.action == QA_NATIVE_SCOPE_SKIP_TO_FRAME_EXIT) cpu.instruction = scope->image + scope->region.frame_exit_rva;
    *redirected = cpu.instruction != instruction;
    return (!decision.restore && !decision.replace_state && !*redirected) ||
        qa_native_guest_cpu_write(scope->guest, &cpu, error);
}
bool native_region_scopes_instruction(qa_native_instance *instance, uint64_t instruction,
    bool *redirected, qa_error *error) {
    *redirected = false;
    for (qa_native_region_scope *scope = instance->region_scopes; scope; scope = scope->next) {
        if (!scope->invoking || instance->active_depth != scope->parent_calls + 1) continue;
        qa_native_region_scope_phase phase;
        if (!scope->reached && instruction == scope->target) {
            scope->reached = true; scope->entered_depth = instance->active_depth;
            if (!boundary(scope, QA_NATIVE_SCOPE_TARGET_ENTRY, instruction, redirected, error)) return false;
        }
        if (!scope->reached) continue;
        if (instruction == scope->image + scope->region.frame_entry_rva) phase = QA_NATIVE_SCOPE_FRAME_ENTRY;
        else if (instruction == scope->image + scope->region.entry_rva) phase = QA_NATIVE_SCOPE_REGION_ENTER;
        else if (instruction == scope->image + scope->region.join_rva) phase = QA_NATIVE_SCOPE_REGION_JOIN;
        else continue;
        if (!boundary(scope, phase, instruction, redirected, error)) return false;
        if (*redirected) return true;
    }
    return true;
}
bool qa_native_region_scope_close(qa_native_region_scope **owner, qa_error *error) {
    if (!owner || !*owner) return true;
    qa_native_region_scope *scope = *owner;
    qa_native_instance *instance = scope->instance;
    bool drained = physical(scope) && !scope->invoking && !scope->calls && !scope->event &&
        !instance->active_depth && !instance->callback_depth && !instance->region_depth && !instance->region_service_depth;
    if ((!parent_returned(scope) && !drained) || instance->checkpointing || instance->destroying || instance->write_depth)
        return fail(error, "Region scope close retains its actual entered processor and callback owner");
    qa_native_region_scope **at = &scope->instance->region_scopes;
    while (*at && *at != scope) at = &(*at)->next;
    if (!*at) return fail(error, "Region scope lost its actual instance association");
    *at = scope->next;
    while (scope->snapshots) {
        qa_native_region_snapshot *snapshot = scope->snapshots; scope->snapshots = snapshot->next; free(snapshot);
    }
    free(scope); *owner = NULL; return true;
}
