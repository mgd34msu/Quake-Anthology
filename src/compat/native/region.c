#include "internal.h"

bool native_regions_copy(qa_native_instance *instance, const qa_native_declaration *declaration,
                         qa_error *error) {
    if (!instance)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native instance is required for region setup");
    if (!declaration)
        return true;
    if (!qa_sha256_equal(&declaration->digest, &instance->declaration))
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
    if (instance->backend != QA_NATIVE_BACKEND_RUNNER)
        return native_fail(error, QA_ERROR_UNSUPPORTED, region_id,
                           "inline native regions require the instrumented runner backend");
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

void qa_native_unbind_region(qa_native_region_binding *binding) {
    if (!binding || !binding->region)
        return;
    qa_native_instance *instance = binding->instance;
    if (instance && (instance->active_depth || instance->callback_depth ||
                     instance->checkpointing || instance->destroying))
        return;
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
}

bool native_runner_region_event(qa_native_instance *instance, const qa_native_region_event *event,
                                qa_native_region_decision *decision, qa_error *error) {
    if (!instance || !event || !decision || event->region.id >= instance->region_count ||
        event->phase > QA_NATIVE_REGION_JOIN)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "valid native region event is required");
    native_region_slot *slot = &instance->regions[event->region.id];
    if (event->region.entry_rva != slot->definition.entry_rva ||
        event->region.join_rva != slot->definition.join_rva)
        return native_fail(error, QA_ERROR_FORMAT, event->region.id,
                           "native runner region identity differs from declaration");
    qa_native_region_event current = *event;
    current.region = slot->definition;
    qa_native_region_decision combined = {
        .action = QA_NATIVE_REGION_CONTINUE, .replace_state = false, .state = event->state};
    bool terminal = false;
    for (qa_native_region_binding *binding = slot->first; binding; binding = binding->next) {
        qa_native_region_decision next = {
            .action = QA_NATIVE_REGION_CONTINUE, .replace_state = false, .state = current.state};
        ++instance->callback_depth;
        bool ok = binding->callback(binding->context, instance, &current, &next, error);
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
    size_t size = 64u;
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
    memcpy(bytes + 16u, instance->module->info.image.digest.bytes, 32u);
    qa_store_u32le(bytes + 48u, (uint32_t)instance->region_count);
    qa_store_u32le(bytes + 52u, instance->module->info.image.target.pointer_bytes);
    qa_store_u64le(bytes + 56u, instance->module->info.image.image_bytes);
    for (size_t index = 0; index < instance->region_count; ++index) {
        uint8_t *record = bytes + 64u + index * 24u;
        const qa_native_declared_region *region = &instance->regions[index].definition;
        qa_store_u32le(record, region->id);
        qa_store_u32le(record + 4u, region->entry_rva);
        qa_store_u32le(record + 8u, region->join_rva);
        qa_store_u32le(record + 12u, region->has_frame ? 1u : 0u);
        qa_store_u32le(record + 16u, region->frame_entry_rva);
        qa_store_u32le(record + 20u, region->frame_exit_rva);
    }
    memcpy(bytes + 64u + records, name, name_size);
    *out = (qa_buffer){bytes, size};
    return true;
}
