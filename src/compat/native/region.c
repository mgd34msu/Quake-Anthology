#include "internal.h"
#include "guest/internal.h"

bool native_regions_copy(qa_native_instance *instance, const qa_native_declaration *declaration,
                         qa_error *error) {
    if (!instance)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native instance is required for region setup");
    if (!declaration)
        return true;
    if (declaration != instance->declaration_ref)
        return native_fail(error, QA_ERROR_FORMAT, 0,
                           "native declaration identity changed during creation");
    if (!declaration->region_count)
        return true;
    native_region_slot *regions = calloc(declaration->region_count, sizeof(*regions));
    if (!regions)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native instance region index");
    for (size_t index = 0; index < declaration->region_count; ++index) {
        regions[index].path = native_strdup(declaration->regions[index].path, error);
        if (!regions[index].path) {
            for (size_t prior = 0; prior < index; ++prior)
                free(regions[prior].path);
            free(regions);
            return false;
        }
        regions[index].definition = declaration->regions[index].definition;
        regions[index].definition.path = regions[index].path;
    }
    instance->regions = regions;
    instance->region_count = declaration->region_count;
    return true;
}

void native_regions_destroy(qa_native_instance *instance) {
    if (!instance)
        return;
    for (size_t index = 0; index < instance->region_count; ++index) {
        qa_native_region_binding *binding = instance->regions[index].first;
        while (binding) {
            qa_native_region_binding *next = binding->next;
            free(binding);
            binding = next;
        }
        free(instance->regions[index].path);
    }
    free(instance->regions);
    instance->regions = NULL;
    instance->region_count = 0;
    qa_native_declaration_destroy((qa_native_declaration *)instance->declaration_ref);
    instance->declaration_ref = NULL;
}

size_t qa_native_region_count(const qa_native_instance *instance) {
    return instance ? instance->region_count : 0;
}

bool qa_native_region(const qa_native_instance *instance, size_t index,
                      qa_native_declared_region *out, qa_error *error) {
    if (!instance || !out || index >= instance->region_count)
        return native_fail(error, QA_ERROR_ARGUMENT, index,
                           "native instance region index is invalid");
    *out = instance->regions[index].definition;
    return true;
}

bool qa_native_bind_region(qa_native_instance *instance, uint32_t region_id,
                           qa_native_region_fn callback, void *context,
                           qa_native_region_binding **out, qa_error *error) {
    if (!instance || !callback || !out || region_id >= instance->region_count)
        return native_fail(error, QA_ERROR_ARGUMENT, region_id,
                           "native region, callback and output are required");
    if (!instance->guest)
        return native_fail(error, QA_ERROR_UNSUPPORTED, region_id,
                           "inline native regions require their owned guest process");
    if (instance->active_depth || instance->callback_depth || instance->checkpointing ||
        instance->destroying)
        return native_fail(error, QA_ERROR_ARGUMENT, region_id,
                           "active native region bindings cannot be changed");
    qa_native_region_binding *binding = calloc(1, sizeof(*binding));
    if (!binding)
        return native_fail(error, QA_ERROR_MEMORY, region_id, "allocating native region binding");
    native_region_slot *region = &instance->regions[region_id];
    binding->instance = instance;
    binding->region = region;
    binding->callback = callback;
    binding->context = context;
    binding->previous = region->last;
    if (region->last)
        region->last->next = binding;
    else
        region->first = binding;
    region->last = binding;
    *out = binding;
    return true;
}

bool native_process_region_instruction(void *context, qa_native_guest *guest,
    uint64_t instruction, qa_error *error)
{
    qa_native_instance *instance = context;
    if (!instance || guest != instance->guest)
        return native_fail(error, QA_ERROR_ARGUMENT, instruction, "native region instruction lost its actual owner");
    bool redirected = false;
    if (!native_region_scopes_instruction(instance, instruction, &redirected, error)) return false;
    if (redirected) return true;
    if (instruction < instance->image_base) return true;
    uint64_t rva = instruction - instance->image_base;
    for (size_t i = 0; i < instance->region_count; ++i) {
        native_region_slot *slot = instance->regions + i;
        if (!slot->first || (rva != slot->definition.entry_rva && rva != slot->definition.join_rva)) continue;
        qa_native_guest_cpu actual;
        if (!qa_native_guest_cpu_read(guest, &actual, error)) return false;
        qa_native_region_event event = {.region = slot->definition,
            .phase = rva == slot->definition.entry_rva ? QA_NATIVE_REGION_ENTER : QA_NATIVE_REGION_JOIN};
        memcpy(event.state.registers, actual.registers, sizeof(event.state.registers));
        memcpy(event.state.simd, actual.xmm, sizeof(event.state.simd));
        event.state.flags = actual.flags; event.state.instruction = instruction;
        qa_native_region_decision decision;
        ++instance->region_depth;
        bool okay = native_region_event(instance, &event, &decision, error);
        --instance->region_depth;
        if (!okay) return false;
        if (decision.action == QA_NATIVE_REGION_FAIL_INSTANCE)
            return native_fail(error, QA_ERROR_ARGUMENT, instruction, "native region callback rejected the source continuation");
        if (decision.replace_state) {
            memcpy(actual.registers, decision.state.registers, sizeof(actual.registers));
            memcpy(actual.xmm, decision.state.simd, sizeof(actual.xmm));
            actual.flags = decision.state.flags;
        }
        if (decision.action == QA_NATIVE_REGION_SKIP_TO_JOIN)
            actual.instruction = instance->image_base + slot->definition.join_rva;
        else if (decision.action == QA_NATIVE_REGION_RETURN_TO_FRAME_EXIT)
            actual.instruction = instance->image_base + slot->definition.frame_exit_rva;
        if ((decision.replace_state || actual.instruction != instruction) &&
            !qa_native_guest_cpu_write(guest, &actual, error)) return false;
        if (actual.instruction != instruction) return true;
    }
    return true;
}

bool qa_native_remove_region(qa_native_region_binding *binding, qa_error *error) {
    if (!binding || !binding->region)
        return true;
    qa_native_instance *instance = binding->instance;
    if (instance && (instance->active_depth || instance->callback_depth ||
                     instance->checkpointing || instance->destroying))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "active native region removal retains its callback lifetime");
    native_region_slot *region = binding->region;
    if (binding->previous)
        binding->previous->next = binding->next;
    else
        region->first = binding->next;
    if (binding->next)
        binding->next->previous = binding->previous;
    else
        region->last = binding->previous;
    memset(binding, 0, sizeof(*binding));
    free(binding);
    return true;
}
void qa_native_unbind_region(qa_native_region_binding *binding) {
    qa_native_remove_region(binding, NULL);
}

bool native_region_event(qa_native_instance *instance, const qa_native_region_event *event,
                                qa_native_region_decision *decision, qa_error *error) {
    if (!instance || !event || !decision || event->region.id >= instance->region_count ||
        event->phase > QA_NATIVE_REGION_JOIN)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "valid native region event is required");
    native_region_slot *slot = &instance->regions[event->region.id];
    if (event->region.entry_rva != slot->definition.entry_rva ||
        event->region.join_rva != slot->definition.join_rva)
        return native_fail(error, QA_ERROR_FORMAT, event->region.id,
                           "native region identity differs from declaration");
    qa_native_region_event current = *event;
    current.region = slot->definition;
    qa_native_region_decision combined = {
        .action = QA_NATIVE_REGION_CONTINUE, .replace_state = false, .state = event->state};
    bool terminal = false;
    for (qa_native_region_binding *binding = slot->first; binding; binding = binding->next) {
        qa_native_region_decision next = {
            .action = QA_NATIVE_REGION_CONTINUE, .replace_state = false, .state = current.state};
        ++instance->callback_depth;
        const qa_native_region_event *previous_event = instance->active_region_event;
        uint32_t previous_callback = instance->region_callback_depth;
        uint32_t previous_call = instance->region_call_depth;
        instance->active_region_event = &current;
        instance->region_callback_depth = instance->callback_depth;
        instance->region_call_depth = instance->active_depth;
        bool ok = binding->callback(binding->context, instance, &current, &next, error);
        instance->active_region_event = previous_event;
        instance->region_callback_depth = previous_callback;
        instance->region_call_depth = previous_call;
        --instance->callback_depth;
        if (!ok)
            return false;
        if (next.action > QA_NATIVE_REGION_FAIL_INSTANCE)
            return native_fail(error, QA_ERROR_ARGUMENT, next.action,
                               "native region callback returned an invalid action");
        if ((next.action == QA_NATIVE_REGION_SKIP_TO_JOIN &&
             event->phase != QA_NATIVE_REGION_ENTER) ||
            (next.action == QA_NATIVE_REGION_RETURN_TO_FRAME_EXIT && !slot->definition.has_frame))
            return native_fail(error, QA_ERROR_ARGUMENT, next.action,
                               "native region action is invalid at this boundary");
        if (next.action != QA_NATIVE_REGION_CONTINUE) {
            if (terminal && next.action != combined.action)
                return native_fail(error, QA_ERROR_ARGUMENT, next.action,
                                   "native region callbacks selected conflicting actions");
            combined.action = next.action;
            terminal = true;
        }
        if (next.replace_state) {
            combined.replace_state = true;
            combined.state = next.state;
            current.state = next.state;
        }
    }
    *decision = combined;
    return true;
}

bool qa_native_region_invoke(qa_native_instance *instance,
    const qa_native_region_event *event, qa_native_address entry,
    const qa_native_signature *signature, const qa_native_value *arguments,
    size_t argument_count, qa_native_value *result, qa_error *error) {
    if (!instance || !event || event != instance->active_region_event ||
        native_active_instance != instance || !instance->region_depth ||
        instance->callback_depth != instance->region_callback_depth ||
        instance->active_depth != instance->region_call_depth || !instance->active_depth ||
        instance->write_depth || instance->checkpointing || instance->destroying ||
        instance->unloading || instance->failed || instance->lifecycle != QA_NATIVE_INITIALIZED ||
        !signature || signature->variadic ||
        signature->abi != instance->module->info.image.target.abi ||
        argument_count != signature->parameter_count || (argument_count && !arguments) ||
        entry < instance->image_base || entry - instance->image_base >= instance->image_bytes)
        return native_fail(error, QA_ERROR_ARGUMENT, entry,
            "nested source invocation requires its exact current region event and same-image fixed ABI");
    bool declared = false;
    uint64_t rva = entry - instance->image_base;
    for (size_t i = 0; i < instance->region_count; ++i)
        if (instance->regions[i].definition.entry_rva == rva) { declared = true; break; }
    if (!declared)
        return native_fail(error, QA_ERROR_ARGUMENT, entry,
            "nested source entry lacks its actual whole-function declaration");
    if (instance->guest && qa_native_guest_execution(instance->guest) == QA_NATIVE_GUEST_EMULATED) {
        bool executable = false;
        for (size_t i = 0; i < qa_native_guest_mapping_count(instance->guest); ++i) {
            qa_native_guest_mapping mapping;
            if (!qa_native_guest_mapping_at(instance->guest, i, &mapping, error)) return false;
            if (entry >= mapping.base && entry - mapping.base < mapping.bytes &&
                (mapping.permissions & QA_NATIVE_GUEST_EXECUTE)) { executable = true; break; }
        }
        if (!executable)
            return native_fail(error, QA_ERROR_ARGUMENT, entry,
                "nested source entry is outside actual executable guest storage");
    } else return native_fail(error, QA_ERROR_UNSUPPORTED, entry,
        "nested source invocation has no actual suspended execution owner");
    if (instance->active_depth == UINT32_MAX)
        return native_fail(error, QA_ERROR_ARGUMENT, entry, "nested source invocation depth exhausted");
    qa_native_address previous_entry = instance->region_invocation_entry;
    uint32_t previous_depth = instance->region_invocation_depth;
    instance->region_invocation_entry = entry;
    instance->region_invocation_depth = instance->active_depth + 1;
    bool okay = native_invoke_entry(instance, entry, signature, arguments, argument_count, result, error);
    instance->region_invocation_entry = previous_entry;
    instance->region_invocation_depth = previous_depth;
    return okay;
}

bool native_regions_descriptor(const qa_native_instance *instance, qa_buffer *out,
                               qa_error *error) {
    if (!instance || !out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native instance and region descriptor output are required");
    const char *name = instance->module->source;
    for (const char *cursor = name; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\')
            name = cursor + 1;
    bool safe_name = name[0] && strcmp(name, ".") && strcmp(name, "..");
    for (const char *cursor = name; safe_name && *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\' || *cursor == ':')
            safe_name = false;
    if (!safe_name)
        name = instance->module->info.image.format == QA_NATIVE_IMAGE_PE32 ||
                       instance->module->info.image.format == QA_NATIVE_IMAGE_PE32_PLUS
                   ? "module.dll"
                   : "module.so";
    size_t name_size = strlen(name);
    if (name_size > UINT32_MAX)
        return native_fail(error, QA_ERROR_MEMORY, name_size,
                           "native region module name is too long");
    size_t size = 32u;
    size_t records;
    if (!native_size_multiply(instance->region_count, 24u, &records) ||
        !native_size_add(size, records, &size) || !native_size_add(size, name_size, &size))
        return native_fail(error, QA_ERROR_MEMORY, 0, "native region descriptor size overflows");
    uint8_t *bytes = calloc(1, size);
    if (!bytes)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native region descriptor");
    memcpy(bytes, "QANHOOK\0", 8);
    qa_store_u32le(bytes + 8u, 1u);
    qa_store_u32le(bytes + 12u, (uint32_t)instance->module->info.image.target.arch);
    qa_store_u32le(bytes + 16u, (uint32_t)instance->region_count);
    qa_store_u32le(bytes + 20u, instance->module->info.image.target.pointer_bytes);
    qa_store_u64le(bytes + 24u, instance->module->info.image.image_bytes);
    for (size_t index = 0; index < instance->region_count; ++index) {
        uint8_t *record = bytes + 32u + index * 24u;
        const qa_native_declared_region *region = &instance->regions[index].definition;
        qa_store_u32le(record, region->id);
        qa_store_u32le(record + 4u, region->entry_rva);
        qa_store_u32le(record + 8u, region->join_rva);
        qa_store_u32le(record + 12u, region->has_frame ? 1u : 0u);
        qa_store_u32le(record + 16u, region->frame_entry_rva);
        qa_store_u32le(record + 20u, region->frame_exit_rva);
    }
    memcpy(bytes + 32u + records, name, name_size);
    *out = (qa_buffer){bytes, size};
    return true;
}
