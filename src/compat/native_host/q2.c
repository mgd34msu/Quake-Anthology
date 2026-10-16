#include "internal.h"
#include "qa/text.h"

#include <errno.h>
#include <math.h>

static bool call_string(qa_native_host *host, const qa_native_import_call *call, size_t index,
    bool copy, qa_bytes *out, qa_error *error)
{
    if (index >= call->argument_count)
        return native_host_fail(error, QA_ERROR_ARGUMENT, index,
                                "native import string argument is missing");
    return native_host_text_read(host, native_argument_address(call, index), copy, out, error);
}

static bool call_text(qa_native_host *host, const qa_native_import_call *call, size_t index,
    qa_bytes *out, qa_error *error)
{
    return call_string(host, call, index, true, out, error);
}

bool qa_native_host_q2_call_string(const qa_native_host_q2_application_call *call,
    size_t index, const char **out, qa_error *error)
{
    qa_bytes text;
    if (!call_string(call->host, call->import, index, false, &text, error)) return false;
    *out = (const char *)text.data;
    return true;
}

static bool emit_print(qa_native_host *host, qa_native_host_print_kind kind,
                       qa_native_address entity, int32_t level, const char *text,
                       qa_error *error)
{
    if (!host->engine.print)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, 0,
                                "native Q2 print service is unbound");
    qa_native_host_print print = {.kind = kind, .level = level, .text = text};
    if (entity &&
        !native_host_actor_for_address(host, entity, false, &print.client, NULL, error))
        return false;
    host->engine.print(host->engine.context, &print);
    return true;
}

static bool print_import(qa_native_host *host, const qa_native_import_call *call,
                         qa_error *error)
{
    size_t text_index = 0;
    qa_native_host_print_kind kind = QA_NATIVE_HOST_PRINT_DEBUG;
    qa_native_address entity = 0;
    int32_t level = 0;
    qa_native_q2_import_kind dispatch=qa_native_q2_import_dispatch(call->profile,call->slot);
    if ((dispatch == QA_NATIVE_Q2_IMPORT_bprintf) || (dispatch == QA_NATIVE_Q2_IMPORT_Broadcast_Print)) {
        kind = QA_NATIVE_HOST_PRINT_BROADCAST;
        level = native_argument_i32(call, 0);
        text_index = 1;
    } else if ((dispatch == QA_NATIVE_Q2_IMPORT_cprintf) || (dispatch == QA_NATIVE_Q2_IMPORT_Client_Print)) {
        kind = QA_NATIVE_HOST_PRINT_CLIENT;
        entity = native_argument_address(call, 0);
        level = native_argument_i32(call, 1);
        text_index = 2;
    } else if ((dispatch == QA_NATIVE_Q2_IMPORT_centerprintf) || (dispatch == QA_NATIVE_Q2_IMPORT_Center_Print)) {
        kind = QA_NATIVE_HOST_PRINT_CENTER;
        entity = native_argument_address(call, 0);
        text_index = 1;
    }
    qa_bytes text = {0};
    if (!call_text(host, call, text_index, &text, error))
        return false;
    bool fatal = (dispatch == QA_NATIVE_Q2_IMPORT_error) || (dispatch == QA_NATIVE_Q2_IMPORT_Com_Error);
    bool ok = fatal ? native_host_fail(error, QA_ERROR_FORMAT, call->slot,
                                       (const char *)text.data)
                    : emit_print(host, kind, entity, level, (const char *)text.data, error);
    return ok;
}

static bool localized_print_import(qa_native_host *host, const qa_native_import_call *call,
                                    qa_error *error)
{
    if (!host->engine.localized_print)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, call->slot,
                                "native Q2 localized print service is unbound");
    /* API2023 game.h declares MAX_LOCALIZATION_ARGS as eight. */
    enum { maximum_arguments = 8 };
    uint64_t count = call->arguments[4].as.u64;
    if (count > maximum_arguments)
        return native_host_fail(error, QA_ERROR_ARGUMENT, call->slot,
                                "native Q2 localization exceeds its original argument count");
    qa_bytes base;
    if (!call_text(host, call, 2, &base, error)) return false;
    const char *arguments[maximum_arguments];
    qa_native_address array = native_argument_address(call, 3);
    for (size_t i = 0; i < (size_t)count; ++i) {
        size_t offset = i * host->pointer_bytes;
        if (array > UINT64_MAX - offset)
            return native_host_fail(error, QA_ERROR_ARGUMENT, call->slot,
                                    "native Q2 localization argument address overflows");
        uint8_t pointer[8]; qa_bytes argument;
        if (!native_host_read(host, array + offset, pointer, host->pointer_bytes, error) ||
            !native_host_string_read(host, host->pointer_bytes == 4 ?
                qa_load_u32le(pointer) : qa_load_u64le(pointer), &argument, error)) return false;
        arguments[i] = (const char *)argument.data;
    }
    qa_native_host_localized_print print = {.level = native_argument_i32(call, 1),
        .base = (const char *)base.data, .arguments = arguments,
        .argument_count = (size_t)count};
    qa_actor_id client = {0};
    qa_native_address entity = native_argument_address(call, 0);
    if (entity && !(print.level & 8) &&
        !native_host_actor_for_address(host, entity, false, &client, NULL, error)) return false;
    return host->engine.localized_print(host->engine.context, client, &print, error);
}

static bool configstring_get(qa_native_host *host, int32_t index,
                             qa_native_address *out, qa_error *error)
{
    if (!host->engine.configstring_get)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, (size_t)(uint32_t)index,
                                "native configstring reader is unbound");
    qa_string_id value = QA_STRING_NONE;
    if (!host->engine.configstring_get(host->engine.context, index, &value, error))
        return false;
    return qa_native_host_retain_string(host, value, out, error);
}

static bool configstring_set(qa_native_host *host, const qa_native_import_call *call,
                             qa_error *error)
{
    if (!host->engine.configstring_set)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, call->slot,
                                "native configstring writer is unbound");
    qa_bytes value = {0};
    if (!call_text(host, call, 1, &value, error))
        return false;
    bool ok = host->engine.configstring_set(host->engine.context,
                                            native_argument_i32(call, 0),
                                            (const char *)value.data, error);
    return ok;
}

static qa_native_host_resource_kind resource_kind(qa_native_q2_import_kind dispatch)
{
    return (dispatch == QA_NATIVE_Q2_IMPORT_modelindex) ? QA_NATIVE_HOST_MODEL
           : (dispatch == QA_NATIVE_Q2_IMPORT_soundindex) ? QA_NATIVE_HOST_SOUND
                                          : QA_NATIVE_HOST_IMAGE;
}

static bool index_resource(qa_native_host *host, qa_native_host_resource_kind kind,
                           const char *name, int32_t *out, qa_error *error)
{
    if (!host->engine.resource_index)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, kind,
                                "native resource-index service is unbound");
    return host->engine.resource_index(host->engine.context, kind, name, out, error);
}

static bool remember_inline_model(qa_native_host *, int32_t, const char *,
                                  uint32_t *, qa_error *);

static bool resource_import(qa_native_host *host, const qa_native_import_call *call,
                            qa_native_value *result, qa_error *error)
{
    qa_bytes name = {0};
    if (!call_text(host, call, 0, &name, error))
        return false;
    int32_t index;
    bool ok = index_resource(host, resource_kind(qa_native_q2_import_dispatch(call->profile,call->slot)), (const char *)name.data,
                             &index, error);
    if (ok && resource_kind(qa_native_q2_import_dispatch(call->profile,call->slot)) == QA_NATIVE_HOST_MODEL) {
        uint32_t model;
        ok = remember_inline_model(host, index, (const char *)name.data, &model, error);
    }
    if (ok)
        result->as.i32 = index;
    return ok;
}

static bool remember_inline_model(qa_native_host *host, int32_t resource, const char *name,
                                  uint32_t *model, qa_error *error)
{
    if (name[0] != '*') {
        if (resource >= 0 && (uint32_t)resource < host->models.count)
            host->model_entries[resource] = (qa_entity_model_field){0};
        *model = 0;
        return true;
    }
    errno = 0;
    char *end = NULL;
    unsigned long parsed = strtoul(name + 1, &end, 10);
    if (errno || end == name + 1 || *end || parsed == 0 || parsed > UINT32_MAX)
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native inline model name is invalid");
    if (resource < 0)
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native inline model has an invalid resource index");
    if ((uint32_t)resource >= host->models.count) {
        uint32_t count = host->models.count ? host->models.count : 16u;
        while (count <= (uint32_t)resource) count *= 2u;
        qa_entity_model_field *entries = calloc(count, sizeof(*entries));
        if (!entries)
            return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                    "allocating native inline-model mapping");
        if (host->models.count)
            memcpy(entries, host->model_entries,
                   (size_t)host->models.count * sizeof(*entries));
        free(host->model_entries);
        host->model_entries = entries;
        host->models = (qa_entity_model_fields){entries, count};
    }
    host->model_entries[resource] = (qa_entity_model_field){
        .model = (uint32_t)parsed, .geometry = qa_world_geometry(host->world.world),
        .present = true};
    *model = (uint32_t)parsed;
    return true;
}

bool native_host_q2_models_restore(qa_native_host *host, qa_error *error)
{
    qa_q2_codec codec;
    qa_q2_config_layout layout;
    if (!qa_q2_codec_init(&codec, qa_q2_default_protocol(host->profile != QA_NATIVE_Q2_GAME_API3), error) ||
        !qa_q2_config_layout_read(&codec, &layout, error)) return false;
    if (host->models.count)
        memset(host->model_entries, 0, (size_t)host->models.count * sizeof(*host->model_entries));
    for (uint32_t index = 1; index < layout.max_models; ++index) {
        qa_string_id name = 0;
        if (!host->engine.configstring_get(host->engine.context,
                (int32_t)(layout.models + index), &name, error)) return false;
        const char *text = qa_strings_cstr(host->engine.strings, name);
        uint32_t model;
        if (text && text[0] == '*' &&
            !remember_inline_model(host, (int32_t)index, text, &model, error)) return false;
    }
    return true;
}

static bool set_model(qa_native_host *host, const qa_native_import_call *call,
                      qa_error *error)
{
    qa_native_address entity = native_argument_address(call, 0);
    qa_bytes name = {0};
    if (!entity || !call_text(host, call, 1, &name, error))
        return false;
    int32_t resource;
    uint32_t model;
    bool ok = index_resource(host, QA_NATIVE_HOST_MODEL, (const char *)name.data,
                             &resource, error) &&
              remember_inline_model(host, resource, (const char *)name.data, &model, error) &&
              native_host_write_i32(host, entity + 40, resource, error);
    if (ok && model) {
        qa_bounds bounds;
        ok = qa_collision_model_bounds(qa_world_geometry(host->world.world), model, &bounds,
                                       error);
        if (ok)
            ok = native_host_write_vec3(host, entity + host->edict->mins, bounds.mins, error) &&
                 native_host_write_vec3(host, entity + host->edict->maxs, bounds.maxs, error) &&
                 native_host_link(host, entity, error);
    }
    return ok;
}

static bool point_contents(qa_native_host *host, qa_native_address address,
                           uint32_t *out, qa_error *error)
{
    qa_vec3 point;
    if (!native_host_read_vec3(host, address, &point, error))
        return false;
    qa_point_query query = {.point = point,
                            .policy = qa_collision_default_policy(QA_GAME_Q2)};
    query.policy.behavior = &qa_trace_behaviors[host->profile != QA_NATIVE_Q2_GAME_API3 ?
        QA_RULESET_Q2_RERELEASE : QA_RULESET_Q2_CLASSIC];
    qa_point_contents contents;
    if (!qa_world_point_contents(host->world.world, &query, &contents, error))
        return false;
    *out = (uint32_t)qa_collision_point_contents_export(contents.contents,QA_GAME_Q2,contents.q1_opaque_token);
    return true;
}

static bool visibility(qa_native_host *host, const qa_native_import_call *call,
                       bool phs, bool *out, qa_error *error)
{
    qa_vec3 first, second;
    if (!native_host_read_vec3(host, native_argument_address(call, 0), &first, error) ||
        !native_host_read_vec3(host, native_argument_address(call, 1), &second, error))
        return false;
    qa_collision_geometry *geometry = qa_world_geometry(host->world.world);
    bool ignore_portals = host->profile == QA_NATIVE_Q2_GAME_API2023 &&
                          call->argument_count > 2 && call->arguments[2].as.u8 != 0;
    qa_collision_point_visibility_scratch scratch = {0};
    const qa_collision_point_visibility_rules rules = {.leaf_rule = QA_LEAF_COLLISION,
        .phs = phs, .ignore_areas = ignore_portals, .minimum_cluster = INT32_MIN,
        .minimum_area = INT32_MIN};
    return qa_collision_point_visible(geometry, first, second, &rules, &scratch, out, error);
}

static bool sound_import(qa_native_host *host, const qa_native_import_call *call,
                         qa_error *error)
{
    if (!host->engine.sound)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, call->slot,
                                "native sound service is unbound");
    qa_native_q2_import_kind dispatch=qa_native_q2_import_dispatch(call->profile,call->slot);
    bool positioned = (dispatch == QA_NATIVE_Q2_IMPORT_positioned_sound);
    bool local = (dispatch == QA_NATIVE_Q2_IMPORT_local_sound);
    size_t cursor = 0;
    qa_native_host_sound sound = {.positioned = positioned, .local = local};
    if (local) {
        qa_native_address client = native_argument_address(call, cursor++);
        if (client &&
            !native_host_actor_for_address(host, client, false, &sound.client, NULL, error))
            return false;
    }
    qa_native_address position = (positioned || local)
                                     ? native_argument_address(call, cursor++)
                                     : 0;
    sound.positioned = position != 0;
    if (position && !native_host_read_vec3(host, position, &sound.origin, error))
        return false;
    qa_native_address entity = native_argument_address(call, cursor++);
    if (entity &&
        !native_host_actor_for_address(host, entity, false, &sound.actor, NULL, error))
        return false;
    sound.channel = host->profile == QA_NATIVE_Q2_GAME_API3
                        ? (uint8_t)native_argument_i32(call, cursor++)
                        : call->arguments[cursor++].as.u8;
    sound.index = native_argument_i32(call, cursor++);
    sound.volume = native_argument_f32(call, cursor++);
    sound.attenuation = native_argument_f32(call, cursor++);
    sound.time_offset = native_argument_f32(call, cursor++);
    if (local)
        sound.flags = native_argument_u32(call, cursor++);
    return host->engine.sound(host->engine.context, &sound, error);
}

static bool command_view(qa_native_host *host, qa_native_host_command_view *out,
                         qa_error *error)
{
    if (!host->engine.command)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, 0,
                                "native command-argument service is unbound");
    return host->engine.command(host->engine.context, out, error);
}

static bool command_import(qa_native_host *host, const qa_native_import_call *call,
                           qa_native_value *result, qa_error *error)
{
    qa_native_host_command_view command = {0};
    if (!command_view(host, &command, error))
        return false;
    if (command.count > NATIVE_HOST_COMMAND_TOKENS) command.count = NATIVE_HOST_COMMAND_TOKENS;
    qa_native_q2_import_kind dispatch=qa_native_q2_import_dispatch(call->profile,call->slot);
    if ((dispatch == QA_NATIVE_Q2_IMPORT_argc)) {
        result->as.i32 = (int32_t)command.count;
        return true;
    }
    const char *text;
    size_t slot = command.count;
    if ((dispatch == QA_NATIVE_Q2_IMPORT_argv)) {
        int32_t index = native_argument_i32(call, 0);
        if (index < 0 || (size_t)index >= command.count)
            return qa_native_host_retain_string(host, host->empty_string, &result->as.address, error);
        slot = (size_t)index;
        text = command.arguments[slot];
    } else {
        text = command.tail ? command.tail : "";
    }
    return native_host_command_string(host, slot, command.count, text, &result->as.address, error);
}

static bool add_command(qa_native_host *host, const qa_native_import_call *call,
                        qa_error *error)
{
    if (!host->console)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, call->slot,
                                "native command buffer is unbound");
    qa_bytes text = {0};
    if (!call_text(host, call, 0, &text, error))
        return false;
    bool ok = qa_console_append(host->console, &host->command_context,
                                (const char *)text.data, error);
    return ok;
}

static bool extension_import(qa_native_host *host, const qa_native_import_call *call,
                             qa_native_value *result, qa_error *error)
{
    qa_bytes name = {0};
    if (!call_text(host, call, 0, &name, error))
        return false;
    qa_native_address address = 0;
    bool ok = !host->engine.extension ||
              host->engine.extension(host->engine.context, host->profile,
                                     (const char *)name.data, &address, error);
    if (ok)
        result->as.address = address;
    return ok;
}

static bool tagged_memory(qa_native_host *host, const qa_native_import_call *call,
                          qa_native_value *result, qa_error *error)
{
    qa_native_q2_import_kind dispatch=qa_native_q2_import_dispatch(call->profile,call->slot);
    if ((dispatch == QA_NATIVE_Q2_IMPORT_TagMalloc)) {
        uint64_t amount = host->profile == QA_NATIVE_Q2_GAME_API3
                              ? (uint32_t)native_argument_i32(call, 0)
                              : call->arguments[0].as.u64;
        if (amount > SIZE_MAX)
            return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                    "native tagged allocation exceeds the host");
        return qa_native_allocate(host->instance, (size_t)amount,
                                  native_argument_i32(call, 1),
                                  &result->as.address, error);
    }
    if ((dispatch == QA_NATIVE_Q2_IMPORT_TagFree))
        return qa_native_free(host->instance, native_argument_address(call, 0), error);
    qa_native_free_tag(host->instance, native_argument_i32(call, 0));
    return true;
}

static bool cvar_import(qa_native_host *host, const qa_native_import_call *call,
                        qa_native_value *result, qa_error *error)
{
    qa_bytes name_text, value_text;
    if (!call_string(host, call, 0, false, &name_text, error)) return false;
    qa_native_q2_import_kind dispatch=qa_native_q2_import_dispatch(call->profile,call->slot);
    bool registration = (dispatch == QA_NATIVE_Q2_IMPORT_cvar);
    const qa_cvar_view *existing = host->cvars ? qa_cvars_find(host->cvars, (const char *)name_text.data) : NULL;
    bool copy = !registration || !existing || !existing->declared;
    /* Declaration and set callbacks may reenter the guest before retaining text.
     * Existing Cvar_Get ignores the supplied default. */
    if ((copy && !call_string(host, call, 0, true, &name_text, error)) ||
        !call_string(host, call, 1, copy, &value_text, error)) return false;
    const char *name = (const char *)name_text.data, *value = (const char *)value_text.data;
    uint32_t flags = registration
                         ? (host->profile == QA_NATIVE_Q2_GAME_API3
                                ? (uint32_t)native_argument_i32(call, 2)
                                : native_argument_u32(call, 2))
                         : (dispatch == QA_NATIVE_Q2_IMPORT_cvar_forceset) ? 1u : 0u;
    bool ok = native_host_cvar(host, name, value, flags, !registration,
                               &result->as.address, error);
    return ok;
}

static bool info_value(qa_native_host *host, const qa_native_import_call *call,
                       qa_native_value *result, qa_error *error)
{
    qa_bytes info = {0}, key = {0};
    if (!call_text(host, call, 0, &info, error) || !call_text(host, call, 1, &key, error)) {
        return false;
    }
    qa_bytes value;
    qa_info_find((const char *)info.data, key, 0, &value);
    const char *begin = (const char *)value.data;
    size_t length = value.size;
    uint64_t capacity = call->arguments[3].as.u64;
    bool ok = true;
    if (capacity) {
        size_t copied = length;
        if (copied >= capacity)
            copied = (size_t)capacity - 1u;
        qa_native_address output = native_argument_address(call, 2);
        uint8_t zero = 0;
        ok = output && native_host_write(host, output, begin, copied, error) &&
             native_host_write(host, output + copied, &zero, 1, error);
    }
    if (ok)
        result->as.u64 = length;
    return ok;
}

enum { NATIVE_Q2_INFO_KEY = 64, NATIVE_Q2_INFO_VALUE = 256,
       NATIVE_Q2_INFO_STRING = 2048 };

static bool info_part_valid(qa_bytes part, size_t limit)
{
    return part.size < limit && !memchr(part.data, '\\', part.size) &&
           !memchr(part.data, '"', part.size) && !memchr(part.data, ';', part.size);
}

static bool info_change(qa_native_host *host, const qa_native_import_call *call,
                        qa_native_value *result, bool set, qa_error *error)
{
    qa_bytes info, key, value = {0};
    if (!call_text(host, call, 0, &info, error) ||
        !call_text(host, call, 1, &key, error) ||
        (set && !call_text(host, call, 2, &value, error))) return false;
    result->as.u8 = 0;
    if (set && (!info_part_valid(key, NATIVE_Q2_INFO_KEY) ||
                !info_part_valid(value, NATIVE_Q2_INFO_VALUE))) return true;

    size_t capacity = info.size + 1 + (set ? key.size + value.size + 2 : 0);
    char *output = qa_arena_alloc(&host->text_storage, capacity, 1, error);
    if (!output) return false;
    memcpy(output, info.data, info.size + 1);
    bool removed = false;
    const char *cursor = output;
    qa_bytes pair_key, pair_value;
    while (*cursor) {
        const char *start = cursor;
        if (!qa_info_next(&cursor, &pair_key, &pair_value)) break;
        if (qa_info_key_equal(pair_key, key, 0)) {
            memmove((char *)start, cursor, strlen(cursor) + 1);
            cursor = start;
            removed = true;
        }
    }

    bool accepted = removed, appended = false;
    size_t length = strlen(output);
    if (set) {
        accepted = !value.size ||
            (length < NATIVE_Q2_INFO_STRING &&
             key.size + value.size + 2 < NATIVE_Q2_INFO_STRING - length);
        if (accepted && value.size) {
            const qa_bytes parts[] = {{(const uint8_t *)"\\", 1}, key,
                                     {(const uint8_t *)"\\", 1}, value};
            for (size_t part = 0; part < sizeof(parts) / sizeof(*parts); ++part)
                for (size_t index = 0; index < parts[part].size; ++index) {
                    uint8_t byte = parts[part].data[index] & 127u;
                    if (byte >= 32 && byte < 127) output[length++] = (char)byte;
                }
            output[length] = 0;
            appended = true;
        }
    }
    if ((removed || appended) &&
        !native_host_write(host, native_argument_address(call, 0), output,
                           length + 1, error)) return false;
    result->as.u8 = accepted ? 1u : 0u;
    return true;
}

static bool q2_application(qa_native_host *host, const qa_native_import_call *call,
                           qa_native_value *result, qa_error *error)
{
    if (!host->q2_application)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, call->slot,
                                "native Q2 application service is unbound");
    qa_native_host_q2_application_call application = {
        .host = host, .instance = host->instance, .import = call,
        .seat = host->q2_seat, .seat_bound = host->q2_seat_bound};
    return host->q2_application(host->q2_application_context, &application, result, error);
}

static bool bot_order_import(qa_native_host *host,const qa_native_import_call *call,
    qa_native_value *result,bool follow,qa_error *error)
{
    result->as.i32=0;
    qa_native_address address=native_argument_address(call,0);
    if(!address) return true;
    qa_actor_id actor;
    if(!native_host_actor_for_address(host,address,false,&actor,NULL,error)) return false;
    if(!actor.registry) return true;
    if(follow) {
        if(!host->engine.bot_follow)
            return native_host_fail(error,QA_ERROR_UNSUPPORTED,call->slot,"Native Q2 bot follow service is unbound");
        address=native_argument_address(call,1);
        if(!address) return true;
        qa_actor_id target;
        if(!native_host_actor_for_address(host,address,false,&target,NULL,error)) return false;
        if(!target.registry) return true;
        return host->engine.bot_follow(host->engine.context,actor,target,&result->as.i32,error);
    }
    if(!host->engine.bot_move_to)
        return native_host_fail(error,QA_ERROR_UNSUPPORTED,call->slot,"Native Q2 bot point service is unbound");
    qa_vec3 point;
    if(!native_host_read_vec3(host,native_argument_address(call,1),&point,error)) return false;
    return host->engine.bot_move_to(host->engine.context,actor,point,
        call->arguments[2].as.f32,&result->as.i32,error);
}

typedef struct native_path_sink {
    qa_native_host *host;
    qa_native_address address;
    uint64_t written,capacity;
} native_path_sink;

static bool path_point(void *opaque,qa_vec3 point,qa_error *error)
{
    native_path_sink *sink=opaque;
    if(sink->written>=sink->capacity || sink->written>UINT64_MAX/12 ||
        sink->address>UINT64_MAX-sink->written*12)
        return native_host_fail(error,QA_ERROR_ARGUMENT,0,"Native Q2 path point address exceeds its buffer");
    if(!native_host_write_vec3(sink->host,sink->address+sink->written*12,point,error)) return false;
    ++sink->written;return true;
}

static bool path_import(qa_native_host *host,const qa_native_import_call *call,
    qa_native_value *result,qa_error *error)
{
    if(!host->engine.path_to_goal)
        return native_host_fail(error,QA_ERROR_UNSUPPORTED,call->slot,"Native Q2 path service is unbound");
    qa_native_target target=qa_native_module_describe(qa_native_get_module(host->instance)).image.target;
    bool sysv32=target.abi==QA_NATIVE_ABI_SYSTEM_V_I386;
    size_t pointer_offset=sysv32?60:64,count_offset=sysv32?64:72;
    uint8_t bytes[80];
    if(!native_host_read(host,native_argument_address(call,0),bytes,sysv32?72:80,error)) return false;
    qa_native_address output=native_argument_address(call,1);
    if(output && !qa_native_range_check(host->instance,output,40,QA_NATIVE_MEMORY_WRITE,error)) return false;
    qa_native_host_q2_path_request request={
        .start={qa_load_f32le(bytes),qa_load_f32le(bytes+4),qa_load_f32le(bytes+8)},
        .goal={qa_load_f32le(bytes+12),qa_load_f32le(bytes+16),qa_load_f32le(bytes+20)},
        .flags=qa_load_u32le(bytes+24),.move_distance=qa_load_f32le(bytes+28),
        .draw_time=qa_load_f32le(bytes+32),.ignore_node_flags=bytes[36]!=0,
        .min_height=qa_load_f32le(bytes+40),.max_height=qa_load_f32le(bytes+44),
        .radius=qa_load_f32le(bytes+48),.drop_height=qa_load_f32le(bytes+52),
        .jump_height=qa_load_f32le(bytes+56)};
    uint64_t count=qa_load_u64le(bytes+count_offset);
    memcpy(&request.point_capacity,&count,sizeof(count));
    native_path_sink sink={.host=host,.capacity=count,.address=target.pointer_bytes==4?
        qa_load_u32le(bytes+pointer_offset):qa_load_u64le(bytes+pointer_offset)};
    if(request.point_capacity<0 || (request.point_capacity && !sink.address) ||
        !qa_vec_finite(request.start) || !qa_vec_finite(request.goal) ||
        !isfinite(request.move_distance) || !isfinite(request.draw_time) ||
        !isfinite(request.min_height) || !isfinite(request.max_height) ||
        !isfinite(request.radius) || !isfinite(request.drop_height) || !isfinite(request.jump_height))
        return native_host_fail(error,QA_ERROR_ARGUMENT,call->slot,"Invalid native Q2 path request");
    request.point=path_point;request.point_context=&sink;
    qa_native_host_q2_path_info info;
    if(!host->engine.path_to_goal(host->engine.context,&request,&info,error)) return false;
    if(output) {
        qa_store_u32le(bytes,(uint32_t)info.point_count);qa_store_f32le(bytes+4,info.distance_squared);
        qa_store_f32le(bytes+8,info.first.x);qa_store_f32le(bytes+12,info.first.y);qa_store_f32le(bytes+16,info.first.z);
        qa_store_f32le(bytes+20,info.second.x);qa_store_f32le(bytes+24,info.second.y);qa_store_f32le(bytes+28,info.second.z);
        qa_store_u32le(bytes+32,(uint32_t)info.link_type);qa_store_u32le(bytes+36,(uint32_t)info.return_code);
        if(!native_host_write(host,output,bytes,40,error)) return false;
    }
    result->as.u8=info.return_code<5?1:0;return true;
}

bool native_host_q2_import(qa_native_host *host, const qa_native_import_call *call,
                           qa_native_value *result, qa_error *error)
{
    qa_native_q2_import_kind dispatch=qa_native_q2_import_dispatch(call->profile,call->slot);
    switch (dispatch) {
    case QA_NATIVE_Q2_IMPORT_bprintf:
    case QA_NATIVE_Q2_IMPORT_dprintf:
    case QA_NATIVE_Q2_IMPORT_cprintf:
    case QA_NATIVE_Q2_IMPORT_centerprintf:
    case QA_NATIVE_Q2_IMPORT_error:
    case QA_NATIVE_Q2_IMPORT_Broadcast_Print:
    case QA_NATIVE_Q2_IMPORT_Com_Print:
    case QA_NATIVE_Q2_IMPORT_Client_Print:
    case QA_NATIVE_Q2_IMPORT_Center_Print:
    case QA_NATIVE_Q2_IMPORT_Com_Error:
    return print_import(host, call, error);
    case QA_NATIVE_Q2_IMPORT_Loc_Print:
    return localized_print_import(host, call, error);
    case QA_NATIVE_Q2_IMPORT_DebugGraph:
    return !isfinite(call->arguments[0].as.f32) || !host->engine.debug_graph || host->engine.debug_graph(host->engine.context,
        call->arguments[0].as.f32, native_argument_i32(call, 1), error);
    case QA_NATIVE_Q2_IMPORT_TagMalloc:
    case QA_NATIVE_Q2_IMPORT_TagFree:
    case QA_NATIVE_Q2_IMPORT_FreeTags:
    return tagged_memory(host, call, result, error);
    case QA_NATIVE_Q2_IMPORT_cvar:
    case QA_NATIVE_Q2_IMPORT_cvar_set:
    case QA_NATIVE_Q2_IMPORT_cvar_forceset:
    return cvar_import(host, call, result, error);
    case QA_NATIVE_Q2_IMPORT_configstring:
    return configstring_set(host, call, error);
    case QA_NATIVE_Q2_IMPORT_get_configstring:
    return configstring_get(host, native_argument_i32(call, 0),
                                &result->as.address, error);
    case QA_NATIVE_Q2_IMPORT_modelindex:
    case QA_NATIVE_Q2_IMPORT_soundindex:
    case QA_NATIVE_Q2_IMPORT_imageindex:
    return resource_import(host, call, result, error);
    case QA_NATIVE_Q2_IMPORT_setmodel:
    return set_model(host, call, error);
    case QA_NATIVE_Q2_IMPORT_sound:
    case QA_NATIVE_Q2_IMPORT_positioned_sound:
    case QA_NATIVE_Q2_IMPORT_local_sound:
    return sound_import(host, call, error);
    case QA_NATIVE_Q2_IMPORT_trace:
    case QA_NATIVE_Q2_IMPORT_clip:
    return native_host_trace(host, call, result, (dispatch == QA_NATIVE_Q2_IMPORT_clip), error);
    case QA_NATIVE_Q2_IMPORT_pointcontents:
    {
        uint32_t contents;
        if (!point_contents(host, native_argument_address(call, 0), &contents, error))
            return false;
        if (host->profile == QA_NATIVE_Q2_GAME_API3)
            result->as.i32 = (int32_t)contents;
        else
            result->as.u32 = contents;
        return true;
    }
    case QA_NATIVE_Q2_IMPORT_inPVS:
    case QA_NATIVE_Q2_IMPORT_inPHS:
    {
        bool visible;
        if (!visibility(host, call, (dispatch == QA_NATIVE_Q2_IMPORT_inPHS), &visible, error))
            return false;
        if (host->profile == QA_NATIVE_Q2_GAME_API3)
            result->as.i32 = visible ? 1 : 0;
        else
            result->as.u8 = visible ? 1u : 0u;
        return true;
    }
    case QA_NATIVE_Q2_IMPORT_SetAreaPortalState:
    {
        bool open = host->profile == QA_NATIVE_Q2_GAME_API3
                        ? native_argument_i32(call, 1) != 0
                        : call->arguments[1].as.u8 != 0;
        return qa_collision_set_portal(qa_world_geometry(host->world.world),
                                       (uint32_t)native_argument_i32(call, 0), open, error);
    }
    case QA_NATIVE_Q2_IMPORT_AreasConnected:
    {
        bool connected;
        if (!qa_collision_areas_connected(qa_world_geometry(host->world.world),
                                          native_argument_i32(call, 0),
                                          native_argument_i32(call, 1), &connected, error))
            return false;
        if (host->profile == QA_NATIVE_Q2_GAME_API3)
            result->as.i32 = connected ? 1 : 0;
        else
            result->as.u8 = connected ? 1u : 0u;
        return true;
    }
    case QA_NATIVE_Q2_IMPORT_linkentity:
    return native_host_link(host, native_argument_address(call, 0), error);
    case QA_NATIVE_Q2_IMPORT_unlinkentity:
    return native_host_unlink(host, native_argument_address(call, 0), error);
    case QA_NATIVE_Q2_IMPORT_BoxEdicts:
    return native_host_box_edicts(host, call, result, error);
    case QA_NATIVE_Q2_IMPORT_Pmove:
    return native_host_pmove(host, native_argument_address(call, 0), error);
    case QA_NATIVE_Q2_IMPORT_multicast:
    case QA_NATIVE_Q2_IMPORT_unicast:
    return native_host_message_send(host, call, error);
    case QA_NATIVE_Q2_IMPORT_ReportMatchDetails_Multicast:
    /* q2repro src/server/game.c PF_ReportMatchDetails_Multicast consumes
     * the pending telemetry without publishing it when no platform uses it. */
    native_host_message_clear(host);
    return true;
    case QA_NATIVE_Q2_IMPORT_WriteChar:
    case QA_NATIVE_Q2_IMPORT_WriteByte:
    case QA_NATIVE_Q2_IMPORT_WriteShort:
    case QA_NATIVE_Q2_IMPORT_WriteLong:
    case QA_NATIVE_Q2_IMPORT_WriteFloat:
    case QA_NATIVE_Q2_IMPORT_WriteString:
    case QA_NATIVE_Q2_IMPORT_WritePosition:
    case QA_NATIVE_Q2_IMPORT_WriteDir:
    case QA_NATIVE_Q2_IMPORT_WriteAngle:
    case QA_NATIVE_Q2_IMPORT_WriteEntity:
    return native_host_message_write(host, call, error);
    case QA_NATIVE_Q2_IMPORT_argc:
    case QA_NATIVE_Q2_IMPORT_argv:
    case QA_NATIVE_Q2_IMPORT_args:
    return command_import(host, call, result, error);
    case QA_NATIVE_Q2_IMPORT_AddCommandString:
    return add_command(host, call, error);
    case QA_NATIVE_Q2_IMPORT_GetExtension:
    return extension_import(host, call, result, error);
    case QA_NATIVE_Q2_IMPORT_Bot_RegisterEdict:
    case QA_NATIVE_Q2_IMPORT_Bot_UnRegisterEdict:
    return native_host_q2_bot_register(host, native_argument_address(call, 0),
            (dispatch == QA_NATIVE_Q2_IMPORT_Bot_RegisterEdict), error);
    case QA_NATIVE_Q2_IMPORT_Bot_MoveToPoint:
    case QA_NATIVE_Q2_IMPORT_Bot_FollowActor:
    return bot_order_import(host,call,result,dispatch==QA_NATIVE_Q2_IMPORT_Bot_FollowActor,error);
    case QA_NATIVE_Q2_IMPORT_GetPathToGoal:
    return path_import(host,call,result,error);
    case QA_NATIVE_Q2_IMPORT_ServerFrame:
    {
        result->as.u32 = host->engine.server_frame
                             ? host->engine.server_frame(host->engine.context)
                             : 0;
        return true;
    }
    case QA_NATIVE_Q2_IMPORT_Info_ValueForKey:
    return info_value(host, call, result, error);
    case QA_NATIVE_Q2_IMPORT_Info_RemoveKey:
    return info_change(host, call, result, false, error);
    case QA_NATIVE_Q2_IMPORT_Info_SetValueForKey:
    return info_change(host, call, result, true, error);
    default:
    return q2_application(host, call, result, error);
    }
}
