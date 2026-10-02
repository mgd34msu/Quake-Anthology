#include "guest_q3_gear_private.h"
#include "qa/vfs.h"

bool q3gear_replace_text(char **out, const char *text, qa_error *error)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) return q3gear_fail(error, QA_ERROR_MEMORY, "Copying separate QVM gear text");
    memcpy(copy, text, length + 1); free(*out); *out = copy; return true;
}

static void print(void *context, const char *text)
{
    application_q3_gear *gear = context;
    if (gear->options.host.common.print)
        gear->options.host.common.print(gear->options.host.common.context, text);
}

static void console_print(void *context, const qa_command_context *command, const char *text)
{
    (void)command; print(context, text);
}

static bool cheats(void *context)
{
    application_q3_gear *gear = context;
    const qa_cvar_view *value = qa_cvars_find(gear->options.host.engine_cvars, "sv_cheats");
    return value && value->integer != 0;
}

static bool command_active(void *context, const qa_command_context *command)
{
    application_q3_gear *gear = context;
    return command && command->owner == gear->options.host.command_context.owner &&
        command->dialect == QA_CONSOLE_Q3 && gear->options.current(gear->options.context);
}

static bool command_capture(void *context, const qa_command_context *command,
    qa_command_context *out, qa_error *error)
{
    if (!command_active(context, command))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear console received another lifetime");
    *out = *command; return true;
}

static qa_cvars *cvar_owner(void *context, const qa_command_context *command, const char *name)
{
    application_q3_gear *gear = context;
    if (!command_active(context, command)) return NULL;
    return !strcmp(name, "sv_cheats") && gear->options.host.engine_cvars ?
        gear->options.host.engine_cvars : gear->cvars;
}

static bool read_script(void *context, const qa_command_context *command,
    const char *path, qa_bytes *out, void **lease, qa_error *error)
{
    application_q3_gear *gear = context; qa_resource *resource = NULL;
    if (!command_active(context, command) || !gear->options.host.mounts)
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear script content retired");
    if (!qa_vfs_acquire(gear->options.host.mounts, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource); *lease = resource; return true;
}

static void release_script(void *context, void *lease)
{
    (void)context; qa_resource_release(lease);
}

static qa_command_result source_command(void *context, const qa_command_invocation *command,
    qa_error *error)
{
    application_q3_gear *gear = context;
    if (!gear->initialized) return QA_COMMAND_UNHANDLED;
    const qa_command_invocation *previous = gear->arguments;
    bool was_busy = gear->busy;
    gear->busy = true;
    gear->arguments = command;
    int32_t words[] = {9}, result;
    bool okay = qa_qvm_invoke(gear->vm, 0, words, 1, &result, error);
    gear->arguments = previous;
    if (!was_busy) okay = q3gear_leave(gear, okay, error);
    return !okay ? QA_COMMAND_FAILED : result ? QA_COMMAND_HANDLED : QA_COMMAND_UNHANDLED;
}

static uint32_t milliseconds(void *context)
{
    return (uint32_t)((application_q3_gear *)context)->milliseconds;
}

static int32_t calendar(void *context, qa_q3_host_calendar *out)
{
    application_q3_gear *gear = context;
    return gear->options.host.common.calendar(gear->options.host.common.context, out);
}

static bool arguments(void *context, qa_native_host_command_view *out, qa_error *error)
{
    (void)error; application_q3_gear *gear = context;
    const qa_command_invocation *command = gear->arguments;
    *out = (qa_native_host_command_view){.count = command ? command->argc : 0,
        .arguments = command ? command->argv : NULL, .tail = command ? command->args_text : ""};
    return true;
}

static bool configstring(void *context, uint32_t index, const char **out, qa_error *error)
{
    application_q3_gear *gear = context;
    if (index >= 1024) return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear configstring index is outside its table");
    *out = gear->configstrings[index] ? gear->configstrings[index] : ""; return true;
}

static bool set_configstring(void *context, uint32_t index, const char *value, qa_error *error)
{
    application_q3_gear *gear = context;
    if (index >= 1024)
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear configstring index is outside its table");
    return q3gear_replace_text(&gear->configstrings[index], value, error) &&
        gear->options.configstring(gear->options.context, index, gear->configstrings[index], error) &&
        q3gear_current(gear, error);
}

static bool userinfo(void *context, uint32_t index, const char **out, qa_error *error)
{
    application_q3_gear *gear = context;
    if (index >= 64) return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear client is outside its table");
    *out = gear->userinfo[index] ? gear->userinfo[index] : ""; return true;
}

static bool set_userinfo(void *context, uint32_t index, const char *value, qa_error *error)
{
    application_q3_gear *gear = context;
    return index < 64 ? q3gear_replace_text(&gear->userinfo[index], value, error) :
        q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear client is outside its table");
}

static bool user_command(void *context, uint32_t index, qa_q3_usercmd *out, qa_error *error)
{
    application_q3_gear *gear = context;
    if (index >= 64) return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear client is outside its table");
    *out = (qa_q3_usercmd){.serverTime = gear->milliseconds}; return true;
}

static bool drop_client(void *context, uint32_t index, const char *reason, qa_error *error)
{
    (void)context; qa_error_set(error, QA_ERROR_ARGUMENT, index,
        "Separate QVM gear rejected borrowed client: %s", reason); return false;
}

static bool send_command(void *context, int32_t client, const char *text, qa_error *error)
{
    application_q3_gear *gear = context;
    return gear->options.host.server.send_command ? gear->options.host.server.send_command(
        gear->options.host.server.context, client, text, error) :
        q3gear_fail(error, QA_ERROR_UNSUPPORTED, "Separate QVM gear server-command event owner is absent");
}

bool q3gear_services(application_q3_gear *gear, bool restoring, qa_error *error)
{
    qa_cvar_options cvars = {.dialect = QA_CONSOLE_Q3, .user = gear, .print = print, .cheats_allowed = cheats};
    gear->cvars = qa_cvars_create(&cvars, error);
    if (!gear->cvars) return false;
    qa_console_options console = {.context = gear->options.host.command_context,
        .cvars = gear->cvars, .user = gear, .print = console_print, .cvar_owner = cvar_owner,
        .read_script = read_script, .release_script = release_script, .source_command = source_command,
        .capture_context = command_capture, .context_active = command_active};
    gear->console = qa_console_create(&console, error);
    if (!gear->console) return false;
    if (!restoring) {
        static const char *names[] = {"g_log", "cm_noCurves", "cm_playerCurveClip", "bot_enable", "sv_maxclients", "dedicated", "g_gametype"};
        static const char *values[] = {"", "0", "1", "0", "64", "1", "0"};
        for (size_t i = 0; i < sizeof(names)/sizeof(*names); ++i)
            if (!qa_cvars_register(gear->cvars, names[i], values[i], 0,
                gear->options.host.service_owner, NULL, error)) return false;
        for (size_t i = 0; i < gear->definition->initial_cvar_count; ++i) {
            application_q3_grapple_cvar value = gear->definition->initial_cvars[i];
            if (!qa_cvars_register(gear->cvars, value.name, value.value, 0, gear->options.host.service_owner, NULL, error) ||
                !qa_cvars_set(gear->cvars, value.name, value.value, true, error)) return false;
        }
    }
    qa_q3_host_options host = gear->options.host;
    host.cvars = gear->cvars; host.console = gear->console;
    host.common = (qa_q3_host_common_services){.context = gear, .print = print,
        .milliseconds = milliseconds, .calendar = host.common.calendar ? calendar : NULL, .arguments = arguments};
    host.server = (qa_q3_host_server_services){.context = gear, .maximum_clients = 64,
        .configstring = configstring, .set_configstring = set_configstring, .userinfo = userinfo,
        .set_userinfo = set_userinfo, .user_command = user_command, .drop_client = drop_client, .send_command = send_command};
    host.entity_text = (qa_bytes){gear->entities.data, gear->entities.size};
    host.writable_mount = 0;
    host.write_view = (qa_q3_host_write_view){0};
    return qa_q3_host_create(&host, &gear->host, error);
}

static bool argument(const qa_qvm_call *call, size_t index, int32_t *out, qa_error *error)
{
    return qa_qvm_call_argument(call, index, out, error);
}

static bool spatial_trace(application_q3_gear *gear, const qa_qvm_call *call,
    bool capsule, qa_error *error)
{
    int32_t words[7];
    for (size_t i = 0; i < 7; ++i) if (!argument(call, i, &words[i], error)) return false;
    qa_trace_query query = {.shape.kind = capsule ? QA_SHAPE_CAPSULE : QA_SHAPE_BOX,
        .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    query.policy.contents_mask = (uint32_t)words[6];
    const qa_cvar_view *curves = qa_cvars_find(gear->cvars, "cm_noCurves");
    const qa_cvar_view *clip = qa_cvars_find(gear->cvars, "cm_playerCurveClip");
    query.policy.curves = !curves || curves->number == 0;
    query.policy.player_curve_clip = !clip || clip->number != 0;
    if (!q3gear_vector(gear, qa_qvm_mask_address(gear->vm, words[1]), &query.start, error) ||
        !q3gear_vector(gear, qa_qvm_mask_address(gear->vm, words[4]), &query.end, error) ||
        (words[2] && !q3gear_vector(gear, qa_qvm_mask_address(gear->vm, words[2]), &query.shape.bounds.mins, error)) ||
        (words[3] && !q3gear_vector(gear, qa_qvm_mask_address(gear->vm, words[3]), &query.shape.bounds.maxs, error))) return false;
    qa_q3_host_game_data layout;
    if (!q3gear_layout(gear, &layout, error)) return false;
    if (words[5] >= 0 && words[5] < 1022 && (uint32_t)words[5] < layout.entity_count)
        query.pass_actor = q3gear_actor(gear, (int32_t)(layout.entities_address + (uint32_t)words[5]*layout.entity_stride));
    qa_actor_id *excluded = gear->capacity ? malloc(gear->capacity * sizeof(*excluded)) : NULL;
    if (gear->capacity && !excluded) return q3gear_fail(error, QA_ERROR_MEMORY, "Collecting separate QVM gear collision exclusions");
    size_t count = 0;
    for (uint32_t i = 0; i < gear->capacity; ++i)
        if (gear->tethers[i].actor.registry) excluded[count++] = gear->tethers[i].actor;
    qa_trace_result hit;
    bool okay = qa_world_trace_excluding(gear->options.host.world, &query, excluded, count, &hit, error);
    free(excluded); if (!okay) return false;
    int32_t number = hit.fraction == 1 ? 1023 : 1022;
    if (hit.hit == QA_TRACE_HIT_ACTOR) {
        application_q3_gear_target target;
        uint32_t slot;
        if (!q3gear_target(gear, hit.actor, &target, error) || !q3gear_mirror(gear, &target, error) ||
            !q3gear_slot(gear, gear->bindings[hit.actor.slot].pointer, &slot, error)) return false;
        number = (int32_t)slot;
    }
    return qa_qvm_write_trace(gear->vm, words[0], &hit, number, error);
}

static bool area_entities(application_q3_gear *gear, const qa_qvm_call *call,
    int32_t *result, qa_error *error)
{
    int32_t words[4];
    for (size_t i = 0; i < 4; ++i) if (!argument(call, i, &words[i], error)) return false;
    qa_bounds bounds;
    if (!q3gear_vector(gear, qa_qvm_mask_address(gear->vm, words[0]), &bounds.mins, error) ||
        !q3gear_vector(gear, qa_qvm_mask_address(gear->vm, words[1]), &bounds.maxs, error)) return false;
    size_t capacity = qa_actors_count(qa_session_actors(gear->options.host.session)), count = 0;
    qa_actor_id *actors = capacity ? malloc(capacity*sizeof(*actors)) : NULL;
    if (capacity && !actors) return q3gear_fail(error, QA_ERROR_MEMORY, "Collecting separate QVM gear area actors");
    bool overflow, okay = qa_world_query(gear->options.host.world, bounds, QA_COLLISION_BOTH,
        actors, capacity, &count, &overflow, error);
    if (okay && overflow) okay = q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear area changed during query");
    size_t written = 0;
    for (size_t i = 0; i < count && okay; ++i) {
        const qa_actor_record *record = qa_actors_get(qa_session_actors(gear->options.host.session), actors[i]);
        if (record && record->owner == gear->options.host.owner) continue;
        if (words[3] >= 0 && written == (size_t)words[3]) break;
        application_q3_gear_target target; uint32_t slot;
        okay = q3gear_target(gear, actors[i], &target, error) && q3gear_mirror(gear, &target, error) &&
            q3gear_slot(gear, gear->bindings[actors[i].slot].pointer, &slot, error);
        uint64_t address = (uint64_t)qa_qvm_mask_address(gear->vm, words[2]) + written*4;
        if (okay && address > UINT32_MAX) okay = q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear area output overflow");
        if (okay) okay = q3gear_store(gear, (uint32_t)address, (int32_t)slot, error);
        if (okay) ++written;
    }
    free(actors); if (okay) *result = (int32_t)written; return okay;
}

bool q3gear_syscall(void *context, const qa_qvm_call *call, int32_t trap, int32_t *result, qa_error *error)
{
    application_q3_gear *gear = context;
    if (!q3gear_current(gear, error)) return false;
    int32_t code; bool engine;
    if (!qa_qvm_classify_syscall(QA_QVM_GAME, QA_QVM_Q3_MODERN, trap, &code, &engine, error)) return false;
    if (engine && (code == 200 || code == 304)) { *result = 0; return true; }
    if (engine && (code == 23 || code == 28 || code == 33 || code == 44))
        return q3gear_fail(error, QA_ERROR_UNSUPPORTED, "Separate QVM gear requested undeclared brush or portal ownership");
    if (engine && (code == 24 || code == 43)) {
        *result = 0; return spatial_trace(gear, call, code == 43, error);
    }
    if (engine && code == 32) return area_entities(gear, call, result, error);
    if (engine && (code == 30 || code == 31)) {
        int32_t pointer; uint32_t slot; qa_qvm_entity_shared shared;
        if (!argument(call, 0, &pointer, error)) return false;
        if (!pointer) return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear LinkEntity received a null source pointer");
        pointer = (int32_t)qa_qvm_mask_address(gear->vm, pointer);
        if (!q3gear_slot(gear, (uint32_t)pointer, &slot, error) ||
            !qa_qvm_read_shared_entity(gear->vm, pointer, &shared, error)) return false;
        shared.linked = code == 30;
        if (shared.linked) {
            shared.absolute_bounds.mins = (qa_vec3){shared.origin.x+shared.local_bounds.mins.x-1,
                shared.origin.y+shared.local_bounds.mins.y-1, shared.origin.z+shared.local_bounds.mins.z-1};
            shared.absolute_bounds.maxs = (qa_vec3){shared.origin.x+shared.local_bounds.maxs.x+1,
                shared.origin.y+shared.local_bounds.maxs.y+1, shared.origin.z+shared.local_bounds.maxs.z+1};
        }
        *result = 0; return qa_qvm_write_shared_entity(gear->vm, pointer, &shared, error);
    }
    return gear->lower.syscall(gear->lower.context, call, trap, result, error);
}

bool q3gear_host_checkpoint(void *context, qa_buffer *state, qa_error *error)
{
    application_q3_gear *gear = context;
    return gear->lower.checkpoint(gear->lower.context, state, error);
}

bool q3gear_host_restore(void *context, qa_bytes state, qa_error *error)
{
    application_q3_gear *gear = context;
    return gear->lower.restore(gear->lower.context, state, error);
}
