#include "internal.h"

#include <errno.h>
#include <math.h>

static bool call_text(qa_native_host *host, const qa_native_import_call *call, size_t index,
                      qa_buffer *out, qa_error *error)
{
    if (index >= call->argument_count)
        return native_host_fail(error, QA_ERROR_ARGUMENT, index,
                                "native import string argument is missing");
    qa_native_address address = native_argument_address(call, index);
    if (!address) {
        out->data = calloc(1, 1);
        if (!out->data)
            return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                    "allocating empty native import string");
        return true;
    }
    return native_host_string_read(host, address, out, error);
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
    if (!strcmp(call->name, "bprintf") || !strcmp(call->name, "Broadcast_Print")) {
        kind = QA_NATIVE_HOST_PRINT_BROADCAST;
        level = native_argument_i32(call, 0);
        text_index = 1;
    } else if (!strcmp(call->name, "cprintf") || !strcmp(call->name, "Client_Print")) {
        kind = QA_NATIVE_HOST_PRINT_CLIENT;
        entity = native_argument_address(call, 0);
        level = native_argument_i32(call, 1);
        text_index = 2;
    } else if (!strcmp(call->name, "centerprintf") || !strcmp(call->name, "Center_Print")) {
        kind = QA_NATIVE_HOST_PRINT_CENTER;
        entity = native_argument_address(call, 0);
        text_index = 1;
    }
    qa_buffer text = {0};
    if (!call_text(host, call, text_index, &text, error))
        return false;
    bool fatal = !strcmp(call->name, "error") || !strcmp(call->name, "Com_Error");
    bool ok = fatal ? native_host_fail(error, QA_ERROR_FORMAT, call->slot,
                                       (const char *)text.data)
                    : emit_print(host, kind, entity, level, (const char *)text.data, error);
    qa_buffer_free(&text);
    return ok;
}

static bool configstring_get(qa_native_host *host, int32_t index,
                             qa_native_address *out, qa_error *error)
{
    if (!host->engine.configstring_get)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, (size_t)(uint32_t)index,
                                "native configstring reader is unbound");
    const char *value = NULL;
    if (!host->engine.configstring_get(host->engine.context, index, &value, error))
        return false;
    return native_host_string_address(host, value ? value : "", out, error);
}

static bool configstring_set(qa_native_host *host, const qa_native_import_call *call,
                             qa_error *error)
{
    if (!host->engine.configstring_set)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, call->slot,
                                "native configstring writer is unbound");
    qa_buffer value = {0};
    if (!call_text(host, call, 1, &value, error))
        return false;
    bool ok = host->engine.configstring_set(host->engine.context,
                                            native_argument_i32(call, 0),
                                            (const char *)value.data, error);
    qa_buffer_free(&value);
    return ok;
}

static qa_native_host_resource_kind resource_kind(const char *name)
{
    return !strcmp(name, "modelindex") ? QA_NATIVE_HOST_MODEL
           : !strcmp(name, "soundindex") ? QA_NATIVE_HOST_SOUND
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

static bool resource_import(qa_native_host *host, const qa_native_import_call *call,
                            qa_native_value *result, qa_error *error)
{
    qa_buffer name = {0};
    if (!call_text(host, call, 0, &name, error))
        return false;
    int32_t index;
    bool ok = index_resource(host, resource_kind(call->name), (const char *)name.data,
                             &index, error);
    qa_buffer_free(&name);
    if (ok)
        result->as.i32 = index;
    return ok;
}

static bool remember_inline_model(qa_native_host *host, int32_t resource, const char *name,
                                  uint32_t *model, qa_error *error)
{
    if (name[0] != '*') {
        *model = 0;
        return true;
    }
    errno = 0;
    char *end = NULL;
    unsigned long parsed = strtoul(name + 1, &end, 10);
    if (errno || end == name + 1 || *end || parsed == 0 || parsed > UINT32_MAX)
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native inline model name is invalid");
    native_host_model *record = host->models;
    while (record && record->resource != resource)
        record = record->next;
    if (!record) {
        record = calloc(1, sizeof(*record));
        if (!record)
            return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                    "allocating native inline-model mapping");
        record->resource = resource;
        record->next = host->models;
        host->models = record;
    }
    record->inline_model = (uint32_t)parsed;
    *model = record->inline_model;
    return true;
}

static bool set_model(qa_native_host *host, const qa_native_import_call *call,
                      qa_error *error)
{
    qa_native_address entity = native_argument_address(call, 0);
    qa_buffer name = {0};
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
    qa_buffer_free(&name);
    return ok;
}

static bool point_contents(qa_native_host *host, qa_native_address address,
                           uint32_t *out, qa_error *error)
{
    qa_vec3 point;
    if (!native_host_read_vec3(host, address, &point, error))
        return false;
    qa_point_query query = {.point = point,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.q2_merged_contents = host->profile != QA_NATIVE_Q2_GAME_API3;
    qa_point_contents contents;
    if (!qa_world_point_contents(host->world.world, &query, &contents, error))
        return false;
    *out = (uint32_t)contents.contents;
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
    qa_collision_leaf from, to;
    if (!qa_collision_point_leaf(geometry, first, &from, error) ||
        !qa_collision_point_leaf(geometry, second, &to, error) ||
        !qa_collision_cluster_visible(geometry, (int32_t)from.cluster,
                                      (int32_t)to.cluster, phs, out, error))
        return false;
    bool ignore_portals = host->profile == QA_NATIVE_Q2_GAME_API2023 &&
                          call->argument_count > 2 && call->arguments[2].as.u8 != 0;
    if (*out && !ignore_portals)
        return qa_collision_areas_connected(geometry, (int32_t)from.area,
                                            (int32_t)to.area, out, error);
    return true;
}

static bool sound_import(qa_native_host *host, const qa_native_import_call *call,
                         qa_error *error)
{
    if (!host->engine.sound)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, call->slot,
                                "native sound service is unbound");
    bool positioned = !strcmp(call->name, "positioned_sound");
    bool local = !strcmp(call->name, "local_sound");
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
    if (!strcmp(call->name, "argc")) {
        if (command.count > INT32_MAX)
            return native_host_fail(error, QA_ERROR_FORMAT, command.count,
                                    "native command has too many arguments");
        result->as.i32 = (int32_t)command.count;
        return true;
    }
    const char *text;
    if (!strcmp(call->name, "argv")) {
        int32_t index = native_argument_i32(call, 0);
        text = index >= 0 && (size_t)index < command.count ? command.arguments[index] : "";
    } else {
        text = command.tail ? command.tail : "";
    }
    return native_host_string_address(host, text, &result->as.address, error);
}

static bool add_command(qa_native_host *host, const qa_native_import_call *call,
                        qa_error *error)
{
    if (!host->console)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, call->slot,
                                "native command buffer is unbound");
    qa_buffer text = {0};
    if (!call_text(host, call, 0, &text, error))
        return false;
    bool ok = qa_console_append(host->console, &host->command_context,
                                (const char *)text.data, error);
    qa_buffer_free(&text);
    return ok;
}

static bool extension_import(qa_native_host *host, const qa_native_import_call *call,
                             qa_native_value *result, qa_error *error)
{
    qa_buffer name = {0};
    if (!call_text(host, call, 0, &name, error))
        return false;
    qa_native_address address = 0;
    bool ok = !host->engine.extension ||
              host->engine.extension(host->engine.context, host->profile,
                                     (const char *)name.data, &address, error);
    qa_buffer_free(&name);
    if (ok)
        result->as.address = address;
    return ok;
}

static bool tagged_memory(qa_native_host *host, const qa_native_import_call *call,
                          qa_native_value *result, qa_error *error)
{
    if (!strcmp(call->name, "TagMalloc")) {
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
    if (!strcmp(call->name, "TagFree"))
        return qa_native_free(host->instance, native_argument_address(call, 0), error);
    qa_native_free_tag(host->instance, native_argument_i32(call, 0));
    return true;
}

static bool cvar_import(qa_native_host *host, const qa_native_import_call *call,
                        qa_native_value *result, qa_error *error)
{
    qa_buffer name = {0}, value = {0};
    if (!call_text(host, call, 0, &name, error) ||
        !call_text(host, call, 1, &value, error)) {
        qa_buffer_free(&name);
        qa_buffer_free(&value);
        return false;
    }
    bool registration = !strcmp(call->name, "cvar");
    uint32_t flags = registration
                         ? (host->profile == QA_NATIVE_Q2_GAME_API3
                                ? (uint32_t)native_argument_i32(call, 2)
                                : native_argument_u32(call, 2))
                         : !strcmp(call->name, "cvar_forceset") ? 1u : 0u;
    bool ok = native_host_cvar(host, (const char *)name.data, (const char *)value.data,
                               flags, !registration, &result->as.address, error);
    qa_buffer_free(&name);
    qa_buffer_free(&value);
    return ok;
}

static bool info_find(const char *info, const char *key, const char **begin, size_t *length)
{
    const char *cursor = info;
    if (*cursor == '\\')
        ++cursor;
    while (*cursor) {
        const char *separator = strchr(cursor, '\\');
        if (!separator)
            break;
        const char *end = strchr(separator + 1, '\\');
        if (!end)
            end = separator + 1 + strlen(separator + 1);
        if ((size_t)(separator - cursor) == strlen(key) &&
            !memcmp(cursor, key, (size_t)(separator - cursor))) {
            *begin = separator + 1;
            *length = (size_t)(end - separator - 1);
            return true;
        }
        cursor = *end ? end + 1 : end;
    }
    return false;
}

static bool info_value(qa_native_host *host, const qa_native_import_call *call,
                       qa_native_value *result, qa_error *error)
{
    qa_buffer info = {0}, key = {0};
    if (!call_text(host, call, 0, &info, error) || !call_text(host, call, 1, &key, error)) {
        qa_buffer_free(&info);
        qa_buffer_free(&key);
        return false;
    }
    const char *begin = "";
    size_t length = 0;
    info_find((const char *)info.data, (const char *)key.data, &begin, &length);
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
    qa_buffer_free(&info);
    qa_buffer_free(&key);
    return ok;
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

bool native_host_q2_import(qa_native_host *host, const qa_native_import_call *call,
                           qa_native_value *result, qa_error *error)
{
    const char *name = call->name;
    if (!strcmp(name, "bprintf") || !strcmp(name, "dprintf") ||
        !strcmp(name, "cprintf") || !strcmp(name, "centerprintf") ||
        !strcmp(name, "error") || !strcmp(name, "Broadcast_Print") ||
        !strcmp(name, "Com_Print") || !strcmp(name, "Client_Print") ||
        !strcmp(name, "Center_Print") || !strcmp(name, "Com_Error"))
        return print_import(host, call, error);
    if (!strcmp(name, "TagMalloc") || !strcmp(name, "TagFree") ||
        !strcmp(name, "FreeTags"))
        return tagged_memory(host, call, result, error);
    if (!strcmp(name, "cvar") || !strcmp(name, "cvar_set") ||
        !strcmp(name, "cvar_forceset"))
        return cvar_import(host, call, result, error);
    if (!strcmp(name, "configstring"))
        return configstring_set(host, call, error);
    if (!strcmp(name, "get_configstring"))
        return configstring_get(host, native_argument_i32(call, 0),
                                &result->as.address, error);
    if (!strcmp(name, "modelindex") || !strcmp(name, "soundindex") ||
        !strcmp(name, "imageindex"))
        return resource_import(host, call, result, error);
    if (!strcmp(name, "setmodel"))
        return set_model(host, call, error);
    if (!strcmp(name, "sound") || !strcmp(name, "positioned_sound") ||
        !strcmp(name, "local_sound"))
        return sound_import(host, call, error);
    if (!strcmp(name, "trace") || !strcmp(name, "clip"))
        return native_host_trace(host, call, result, !strcmp(name, "clip"), error);
    if (!strcmp(name, "pointcontents")) {
        uint32_t contents;
        if (!point_contents(host, native_argument_address(call, 0), &contents, error))
            return false;
        if (host->profile == QA_NATIVE_Q2_GAME_API3)
            result->as.i32 = (int32_t)contents;
        else
            result->as.u32 = contents;
        return true;
    }
    if (!strcmp(name, "inPVS") || !strcmp(name, "inPHS")) {
        bool visible;
        if (!visibility(host, call, !strcmp(name, "inPHS"), &visible, error))
            return false;
        if (host->profile == QA_NATIVE_Q2_GAME_API3)
            result->as.i32 = visible ? 1 : 0;
        else
            result->as.u8 = visible ? 1u : 0u;
        return true;
    }
    if (!strcmp(name, "SetAreaPortalState")) {
        bool open = host->profile == QA_NATIVE_Q2_GAME_API3
                        ? native_argument_i32(call, 1) != 0
                        : call->arguments[1].as.u8 != 0;
        return qa_collision_set_portal(qa_world_geometry(host->world.world),
                                       (uint32_t)native_argument_i32(call, 0), open, error);
    }
    if (!strcmp(name, "AreasConnected")) {
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
    if (!strcmp(name, "linkentity"))
        return native_host_link(host, native_argument_address(call, 0), error);
    if (!strcmp(name, "unlinkentity"))
        return native_host_unlink(host, native_argument_address(call, 0), error);
    if (!strcmp(name, "BoxEdicts"))
        return native_host_box_edicts(host, call, result, error);
    if (!strcmp(name, "Pmove"))
        return native_host_pmove(host, native_argument_address(call, 0), error);
    if (!strcmp(name, "multicast") || !strcmp(name, "unicast"))
        return native_host_message_send(host, call, error);
    if (!strncmp(name, "Write", 5))
        return native_host_message_write(host, call, error);
    if (!strcmp(name, "argc") || !strcmp(name, "argv") || !strcmp(name, "args"))
        return command_import(host, call, result, error);
    if (!strcmp(name, "AddCommandString"))
        return add_command(host, call, error);
    if (!strcmp(name, "GetExtension"))
        return extension_import(host, call, result, error);
    if (host->profile == QA_NATIVE_Q2_GAME_API2023 &&
        (!strcmp(name, "Bot_RegisterEdict") || !strcmp(name, "Bot_UnRegisterEdict")))
        return native_host_q2_bot_register(host, native_argument_address(call, 0),
            !strcmp(name, "Bot_RegisterEdict"), error);
    if (!strcmp(name, "ServerFrame")) {
        result->as.u32 = host->engine.server_frame
                             ? host->engine.server_frame(host->engine.context)
                             : 0;
        return true;
    }
    if (!strcmp(name, "Info_ValueForKey"))
        return info_value(host, call, result, error);
    return q2_application(host, call, result, error);
}
