#include "source_trajectory.h"
#include "source_projection.h"
#include "internal.h"
#include "native_q3_settings.h"
#include "qa/binary.h"
#include "qa/console_cvars_prepare.h"
#include "qa/json.h"
#include "qa/q3_host.h"
#include "qa/source_save.h"
#include "qa/stamp.h"
#include "qa/text.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    QVM_TRAJECTORY_CLIENT = 1, QVM_TRAJECTORY_PROJECTILE = 2,
    QVM_TRAJECTORY_ACTIVATED = 4, QVM_TRAJECTORY_RETIRED = 8,
    QVM_TRAJECTORY_TARGET = 16,
    QVM_TRAJECTORY_ENTITIES = QA_Q3_ENTITIES, QVM_TRAJECTORY_CLIENTS = 64,
    QVM_TRAJECTORY_USERINFO_BYTES = 1024
};

typedef struct trajectory_qvm {
    qa_application *application;
    qa_qvm_image *image;
    qa_qvm *vm;
    qa_q3_host *host;
    qa_qvm_options original;
    qa_qvm_abi abi;
    qa_cvars *cvars;
    qa_cvars_value_scope *cvar_scopes[2];
    qa_q3_gamestate *configstrings;
    qa_buffer entities;
    qa_builtin_services services;
    qa_actor_owner owner;
    qa_actor_id pending_projectile;
    uint32_t fire, activate, allocate, free_entity, stride, level_time;
    uint32_t inuse, nextthink, think, health, maximum_clients;
    uint64_t time_ns, refreshed_time;
    qa_qvm_binding free_binding;
    qa_stamp_set refreshed;
    uint32_t *marks;
    uint8_t flags[QVM_TRAJECTORY_ENTITIES];
    char userinfo[QVM_TRAJECTORY_CLIENTS][QVM_TRAJECTORY_USERINFO_BYTES];
    qa_string_id userinfo_names[QVM_TRAJECTORY_CLIENTS], userinfo_models[QVM_TRAJECTORY_CLIENTS];
    bool userinfo_ready[QVM_TRAJECTORY_CLIENTS];
    const char *const *arguments;
    size_t argument_count;
    bool initialized, releasing, refreshed_once, cvars_active;
} trajectory_qvm;

static application_source_projection qvm_projection(trajectory_qvm *);

static bool qvm_word_read(trajectory_qvm *source, uint32_t address, int32_t *value, qa_error *error)
{
    uint8_t bytes[4];
    if (!qa_qvm_read(source->vm, address, bytes, sizeof(bytes), error)) return false;
    *value = qa_load_i32le(bytes);
    return true;
}

static bool qvm_word_write(trajectory_qvm *source, uint32_t address, int32_t value, qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)value);
    return qa_qvm_source_bytes_write(source->vm, source->image, address,
        (qa_bytes){bytes, sizeof(bytes)}, error);
}

static bool qvm_data(trajectory_qvm *source, qa_q3_host_game_data *data, qa_error *error)
{
    if (!qa_q3_host_game_data_read(source->host, data) || !data->entities_address ||
        data->entity_stride != source->stride || !data->clients_address)
        return application_fail(error, QA_ERROR_FORMAT, "Trajectory QVM layout differs from its located game data");
    return true;
}

static bool qvm_pointer(trajectory_qvm *source, uint32_t slot, int32_t *pointer, qa_error *error)
{
    application_source_projection projection = qvm_projection(source);
    uint64_t address;
    if (!application_source_projection_pointer(&projection, slot, &address, error)) return false;
    if (address > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Trajectory QVM entity exceeds its pointer word");
    *pointer = (int32_t)address;
    return true;
}

static bool qvm_slot(trajectory_qvm *source, int32_t pointer, uint32_t *slot, qa_error *error)
{
    application_source_projection projection = qvm_projection(source);
    return application_source_projection_slot(&projection, (uint32_t)pointer, slot, error);
}

typedef struct trajectory_qvm_call {
    trajectory_qvm *source;
    const qa_qvm_call *call;
    uint32_t entry;
    const int32_t *arguments;
    size_t count;
    int32_t *result;
} trajectory_qvm_call;

static bool qvm_execute(void *context, qa_error *error)
{
    trajectory_qvm_call *invocation = context;
    trajectory_qvm *source = invocation->source;
    bool previous = source->cvars_active;
    source->cvars_active = true;
    bool okay = invocation->call ? qa_qvm_invoke_source_callback(invocation->call, source->image,
        (int32_t)invocation->entry, invocation->arguments, invocation->count, invocation->result, error) :
        qa_qvm_invoke(source->vm, invocation->entry, invocation->arguments, invocation->count,
            invocation->result, error);
    source->cvars_active = previous;
    return okay;
}

static bool qvm_function(trajectory_qvm *source, const qa_qvm_call *call, uint32_t entry,
    const int32_t *arguments, size_t count, int32_t *result, qa_error *error)
{
    trajectory_qvm_call invocation = {source, call, entry, arguments, count, result};
    if (source->cvars_active) return qvm_execute(&invocation, error);
    bool teams = false;
    if (source->application->primary_mode_ready) {
        qa_mode_view mode;
        if (!qa_modes_read(source->application->modes, source->application->primary_mode, &mode, error)) return false;
        teams = qa_game_type_is_team(application_q3_game_type(mode.rules.kind));
    }
    return qa_cvars_value_scope_run(source->cvar_scopes[teams], qvm_execute, &invocation, error);
}

static bool projection_span(void *context, uint64_t address, size_t size,
    qa_bytes *out, qa_error *error)
{
    trajectory_qvm *source = context;
    if (address > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Trajectory projection exceeds its QVM pointer word");
    return qa_qvm_span(source->vm, (int32_t)address, 0, size, out, error);
}

static bool projection_write(void *context, uint64_t address, qa_bytes bytes,
    bool private_fields, qa_error *error)
{
    trajectory_qvm *source = context;
    if (address > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Trajectory projection exceeds its QVM address word");
    return private_fields ? qa_qvm_source_bytes_write(source->vm, source->image,
        (uint32_t)address, bytes, error) : qa_qvm_write(source->vm, (uint32_t)address, bytes, error);
}

static bool projection_invoke(void *context, const qa_qvm_call *call, uint32_t entry,
    const int32_t *arguments, size_t count, int32_t *result, qa_error *error)
{ return qvm_function(context, call, entry, arguments, count, result, error); }

static application_source_projection qvm_projection(trajectory_qvm *source)
{
    return (application_source_projection){.host = source->host, .abi = source->abi,
        .entity_stride = source->stride, .inuse = source->inuse, .health = source->health,
        .allocate = source->allocate, .free = source->free_entity, .context = source,
        .span = projection_span, .write = projection_write, .invoke = projection_invoke};
}

static bool qvm_call(trajectory_qvm *source, const qa_qvm_call *call, int32_t command,
    const int32_t *arguments, size_t count, int32_t *result, qa_error *error)
{
    int32_t words[10] = {command};
    if (count) memcpy(words + 1, arguments, count * sizeof(*arguments));
    return qvm_function(source, call, 0, words, count + 1, result, error);
}

static bool qvm_time(trajectory_qvm *source, uint64_t time_ns, qa_error *error)
{
    source->time_ns = time_ns;
    return qvm_word_write(source, source->level_time,
        (int32_t)(uint32_t)(time_ns / UINT64_C(1000000)), error);
}

static bool qvm_trajectory(trajectory_qvm *source, uint32_t slot,
    qa_builtin_trajectory_update *update, qa_error *error)
{
    int32_t pointer; qa_q3_entity entity; qa_qvm_entity_shared shared;
    if (!qvm_pointer(source, slot, &pointer, error) ||
        !qa_qvm_read_entity(source->vm, pointer, true, &entity, error) ||
        !qa_qvm_read_shared_entity(source->vm, pointer, &shared, error)) return false;
    *update = (qa_builtin_trajectory_update){.origin = shared.origin, .angles = shared.angles,
        .velocity = {entity.pos.delta[0], entity.pos.delta[1], entity.pos.delta[2]}};
    return true;
}

static bool qvm_configstring(void *context, uint32_t index, const char **text, qa_error *error)
{
    trajectory_qvm *source = context;
    if (index >= QA_Q3_CONFIGSTRINGS)
        return application_fail(error, QA_ERROR_ARGUMENT, "Trajectory configstring index exceeds the original table");
    *text = qa_q3_configstring(source->configstrings, index);
    return true;
}

static bool qvm_set_configstring(void *context, uint32_t index, const char *text, qa_error *error)
{
    return qa_q3_configstring_set(((trajectory_qvm *)context)->configstrings, index, text, error);
}

static bool qvm_userinfo(void *context, uint32_t slot, const char **text, qa_error *error)
{
    trajectory_qvm *source = context;
    if (slot >= source->maximum_clients)
        return application_fail(error, QA_ERROR_ARGUMENT, "Trajectory userinfo index exceeds source clients");
    *text = source->userinfo[slot]; return true;
}

static bool qvm_set_userinfo(void *context, uint32_t slot, const char *text, qa_error *error)
{
    trajectory_qvm *source = context;
    if (slot >= source->maximum_clients || strlen(text) >= sizeof(source->userinfo[slot]))
        return application_fail(error, QA_ERROR_CAPACITY, "Trajectory userinfo exceeds the original record");
    memmove(source->userinfo[slot], text, strlen(text) + 1);
    source->userinfo_ready[slot] = false;
    return true;
}

static bool qvm_user_command(void *context, uint32_t slot, qa_q3_usercmd *out, qa_error *error)
{
    trajectory_qvm *source = context; (void)slot; (void)error;
    *out = (qa_q3_usercmd){.serverTime = (int32_t)(source->time_ns / UINT64_C(1000000))};
    return true;
}

static bool qvm_drop_client(void *context, uint32_t slot, const char *reason, qa_error *error)
{
    (void)context; (void)slot; (void)reason;
    return application_fail(error, QA_ERROR_UNSUPPORTED, "Trajectory source cannot drop a primary client");
}

static bool qvm_send_command(void *context, int32_t slot, const char *text,
    const struct qa_unified_q3_event *event, qa_event_lease *lease, uint32_t config,
    uint32_t fragment, qa_error *error)
{
    (void)context; (void)slot; (void)text; (void)event; (void)lease;
    (void)config; (void)fragment; (void)error;
    return true;
}

static void qvm_print(void *context, const char *text) { (void)context; (void)text; }
static uint32_t qvm_milliseconds(void *context)
{ return (uint32_t)(((trajectory_qvm *)context)->time_ns / UINT64_C(1000000)); }
static int32_t qvm_calendar(void *context, qa_q3_host_calendar *out)
{ return qa_application_q3_calendar(((trajectory_qvm *)context)->application, out); }
static bool qvm_arguments(void *context, qa_native_host_command_view *out, qa_error *error)
{
    trajectory_qvm *source = context; (void)error;
    *out = (qa_native_host_command_view){.arguments = source->arguments,
        .count = source->argument_count, .tail = source->argument_count > 1 ? source->arguments[1] : ""};
    return true;
}

static bool qvm_syscall(void *context, const qa_qvm_call *call, int32_t trap,
    int32_t *result, qa_error *error)
{
    trajectory_qvm *source = context; int32_t code; bool engine;
    if (!qa_qvm_classify_syscall(QA_QVM_GAME, source->abi, trap, &code, &engine, error)) return false;
    if (engine && (code == 14 || code == 23 || code == 28))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Trajectory QVM cannot change the primary world");
    if (engine && code == 10) {
        int32_t mode, destination;
        if (!qa_qvm_call_argument(call, 2, &mode, error) ||
            !qa_qvm_call_argument(call, 1, &destination, error)) return false;
        if (mode != 0) {
            uint8_t absent[4] = {0};
            if (destination && !qa_qvm_write(source->vm, qa_qvm_mask_address(source->vm, destination),
                (qa_bytes){absent, sizeof(absent)}, error)) return false;
            *result = -1; return true;
        }
    }
    /* BotAISetup tests BLERR_NOERROR. An unavailable donor botlib must fail
     * that setup normally; the cvar scope preserves canonical bot_enable. */
    if (engine && code >= 200 && code <= 207) {
        if (code == 203) {
            int32_t destination, length;
            if (!qa_qvm_call_argument(call, 1, &destination, error) ||
                !qa_qvm_call_argument(call, 2, &length, error)) return false;
            if (length > 0 && !qa_qvm_write_string(source->vm, destination,
                (qa_bytes){(const uint8_t *)"", 0}, (size_t)length, error)) return false;
        }
        *result = code == 204 ? 0 : 1; return true;
    }
    return source->original.syscall(source->original.context, call, trap, result, error);
}

static bool qvm_info_pair(trajectory_qvm *source, char *text, size_t *length,
    const char *key, qa_string_id value, const char *fallback, qa_error *error)
{
    const char *string = value ? qa_strings_cstr(qa_session_strings(source->application->session), value) : fallback;
    if (!string) string = fallback;
    return qa_info_append(text, QVM_TRAJECTORY_USERINFO_BYTES, length,
        (qa_bytes){(const uint8_t *)key, strlen(key)},
        (qa_bytes){(const uint8_t *)string, strlen(string)}, 0, error);
}

static bool qvm_team(trajectory_qvm *source, qa_actor_id actor,
    const qa_actor_player *player, int32_t *out, qa_error *error)
{
    *out = player->spectator ? 3 : 0;
    if (*out || !source->application->primary_mode_ready) return true;
    qa_mode_view mode; qa_team_id team;
    if (!qa_modes_read(source->application->modes, source->application->primary_mode, &mode, error)) return false;
    if (!qa_game_type_is_team(application_q3_game_type(mode.rules.kind))) return true;
    if (!qa_modes_team(source->application->modes, source->application->primary_mode, actor, &team, error)) return false;
    if (mode.rules.teams[0] && team == mode.rules.teams[0]) *out = 1;
    else if (mode.rules.teams[1] && team == mode.rules.teams[1]) *out = 2;
    return true;
}

static bool qvm_player(trajectory_qvm *source, const qa_qvm_call *call, qa_actor_id actor,
    const qa_actor_player *player, const qa_body_state *body, int32_t health,
    uint32_t slot, bool created, qa_error *error)
{
    qa_string_id model = player->model ? player->model : player->skin;
    bool changed = false;
    if (!source->userinfo_ready[slot] || source->userinfo_names[slot] != player->name ||
        source->userinfo_models[slot] != model) {
        char text[QVM_TRAJECTORY_USERINFO_BYTES] = {0}; size_t length = 0;
        if (!qvm_info_pair(source, text, &length, "name", player->name, "player", error) ||
            !qvm_info_pair(source, text, &length, "model", model, "sarge/default", error) ||
            !qvm_info_pair(source, text, &length, "ip", 0, "localhost", error)) return false;
        changed = strcmp(source->userinfo[slot], text) != 0;
        if (changed) memcpy(source->userinfo[slot], text, length + 1);
        source->userinfo_names[slot] = player->name;
        source->userinfo_models[slot] = model;
        source->userinfo_ready[slot] = true;
    }
    int32_t result, argument = (int32_t)slot;
    if (created) {
        int32_t connect[3] = {(int32_t)slot, 1, 0};
        if (!qvm_call(source, call, 2, connect, 3, &result, error)) return false;
        if (result) return application_fail(error, QA_ERROR_UNSUPPORTED, "Trajectory QVM rejected its projected source client");
        if (!qvm_call(source, call, 3, &argument, 1, &result, error)) return false;
    } else if (changed && !qvm_call(source, call, 4, &argument, 1, &result, error)) return false;
    qa_q3_host_game_data data; qa_q3_player state; int32_t team;
    if (!qvm_data(source, &data, error)) return false;
    uint64_t address = data.clients_address + (uint64_t)slot * data.client_stride;
    if (address > INT32_MAX || !qa_qvm_read_player(source->vm, (int32_t)address, true, &state, error) ||
        !qvm_team(source, actor, player, &team, error)) return false;
    if (state.persistant[3] != team) {
        static const char *const teams[] = {"free", "red", "blue", "spectator"};
        const char *arguments[] = {"team", teams[team]};
        const char *const *previous = source->arguments; size_t previous_count = source->argument_count;
        source->arguments = arguments; source->argument_count = 2;
        bool okay = qvm_call(source, call, 6, &argument, 1, &result, error);
        source->arguments = previous; source->argument_count = previous_count;
        if (!okay || !qa_qvm_read_player(source->vm, (int32_t)address, true, &state, error)) return false;
        if (state.persistant[3] != team)
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Trajectory QVM did not admit the projected player's team");
    }
    qa_vec3 view = body->angles;
    qa_builtin_player_control control = {0}; qa_error local = {0};
    if (source->services.player_control && source->services.player_control(
        source->services.context, source->owner, actor, &control, &local)) {
        if (control.view_angles) view = *control.view_angles;
        if (control.view_height) state.viewheight = qa_source_to_i32(*control.view_height);
    } else if (local.code != QA_OK && local.code != QA_ERROR_NOT_FOUND) {
        if (error) *error = local;
        return false;
    }
    state.origin[0] = body->origin.x; state.origin[1] = body->origin.y; state.origin[2] = body->origin.z;
    state.velocity[0] = body->velocity.x; state.velocity[1] = body->velocity.y; state.velocity[2] = body->velocity.z;
    state.viewangles[0] = view.x; state.viewangles[1] = view.y; state.viewangles[2] = view.z;
    state.stats[0] = health;
    return qa_qvm_write_player(source->vm, (int32_t)address, true, true, &state, error);
}

static bool qvm_project(trajectory_qvm *source, const qa_qvm_call *call, qa_actor_id actor,
    const qa_body_state *observed, uint32_t *out, qa_error *error)
{
    uint32_t slot; bool created = !qa_q3_host_actor_slot(source->host, actor, &slot, NULL);
    if (!created && (source->flags[slot] & QVM_TRAJECTORY_PROJECTILE)) { *out = slot; return true; }
    if (!created && !call && qa_stamp_set_test(&source->refreshed, actor.slot)) { *out = slot; return true; }
    qa_body_state body;
    if (observed) body = *observed;
    else if (!qa_world_body_read(source->application->world, actor, &body, error)) return false;
    qa_actor_player *player = qa_actors_player(qa_session_actors(source->application->session), actor);
    bool client = player && player->present;
    application_source_projection projection = qvm_projection(source);
    if (created) {
        if (client) {
            for (slot = 0; slot < source->maximum_clients && source->flags[slot]; ++slot) {}
            if (slot == source->maximum_clients)
                return application_fail(error, QA_ERROR_CAPACITY, "Trajectory QVM has no free source client");
            if (!qa_q3_host_bind_actor(source->host, slot, actor, true, error)) return false;
        } else {
            if (!application_source_projection_borrow(&projection, call, actor, &slot, &created, error)) return false;
        }
        source->flags[slot] = client ? QVM_TRAJECTORY_CLIENT : QVM_TRAJECTORY_TARGET;
    }
    qa_stamp_set_mark(&source->refreshed, actor.slot);
    qa_combat_state combat = {0};
    if (qa_combat_has(source->application->combat, actor) &&
        !qa_combat_read_traits(source->application->combat, actor, &combat, error)) return false;
    int32_t health = qa_source_to_i32(combat.health);
    if (client && !qvm_player(source, call, actor, player, &body, health, slot, created, error)) return false;
    if (!application_source_projection_publish(&projection, slot, &body, &health,
        source->time_ns, error)) return false;
    *out = slot; return true;
}

static bool qvm_project_actor(void *context, const qa_qvm_call *call, qa_actor_id actor,
    uint32_t *out, qa_error *error)
{ return qvm_project(context, call, actor, NULL, out, error); }

static bool qvm_refresh(trajectory_qvm *source, qa_actor_id excluded, qa_error *error)
{
    if (source->refreshed_once && source->refreshed_time == source->time_ns) return true;
    qa_stamp_set_begin(&source->refreshed);
    uint32_t cursor = 0; const qa_actor_record *record;
    while (qa_actors_next(qa_session_actors(source->application->session), &cursor, &record)) {
        if (qa_actor_id_equal(record->id, excluded)) continue;
        qa_body_state body; qa_error local = {0}; uint32_t slot;
        if (!qa_world_body_read(source->application->world, record->id, &body, &local)) {
            if (local.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = local;
            return false;
        }
        if (!qvm_project(source, NULL, record->id, &body, &slot, error)) return false;
    }
    source->refreshed_time = source->time_ns;
    source->refreshed_once = true;
    return true;
}

static bool qvm_free_hook(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    trajectory_qvm *source = context;
    if (source->releasing) return qa_qvm_proceed(call, result, error);
    int32_t pointer; uint32_t slot; qa_q3_host_game_data data;
    if (!qa_qvm_call_argument(call, 0, &pointer, error)) return false;
    if (!qa_q3_host_game_data_read(source->host, &data) || !data.entities_address)
        return qa_qvm_proceed(call, result, error);
    if (!qvm_slot(source, pointer, &slot, error)) return false;
    if (source->flags[slot] & QVM_TRAJECTORY_PROJECTILE) {
        source->flags[slot] |= QVM_TRAJECTORY_RETIRED;
        *result = 0; return true;
    }
    if (!source->flags[slot]) return qa_qvm_proceed(call, result, error);
    /* A donor can retire its private target state, but its allocator cannot
     * reuse a row still borrowed by a live canonical actor. */
    application_source_projection projection = qvm_projection(source);
    return qa_qvm_proceed(call, result, error) &&
        application_source_projection_keep(&projection, slot, error);
}

static bool qvm_initialize(trajectory_qvm *source, qa_error *error)
{
    if (source->initialized) return true;
    int32_t result, arguments[] = {0, (int32_t)source->owner, 0};
    if (!qvm_call(source, NULL, 0, arguments, 3, &result, error)) return false;
    qa_q3_host_game_data data;
    if (!qvm_data(source, &data, error)) return false;
    source->initialized = true; return true;
}

static bool qvm_launch_inner(void *context, const qa_builtin_weapon_launch *launch,
    application_trajectory_attachment *attachment, qa_builtin_trajectory_update *update,
    bool *changed, qa_error *error)
{
    trajectory_qvm *source = context; *changed = false;
    if (!qvm_initialize(source, error) || !qvm_time(source, launch->time_ns, error) ||
        !qvm_refresh(source, launch->projectile, error)) return false;
    uint32_t shooter; int32_t pointer, result;
    qa_stamp_set_restore_mark(&source->refreshed, launch->shooter.slot, 0);
    if (!qvm_project(source, NULL, launch->shooter, NULL, &shooter, error) ||
        !qvm_pointer(source, shooter, &pointer, error)) return false;
    if (source->activate && !(source->flags[shooter] & QVM_TRAJECTORY_ACTIVATED)) {
        if (!qvm_function(source, NULL, source->activate, &pointer, 1, &result, error)) return false;
        source->flags[shooter] |= QVM_TRAJECTORY_ACTIVATED;
    }
    qa_q3_entity entity; qa_qvm_entity_shared shared;
    float length = qa_vec_length(launch->body.velocity);
    if (!(length > 0)) return application_fail(error, QA_ERROR_ARGUMENT, "Trajectory QVM firing requires a projectile direction");
    qa_vec3 direction = qa_vec_scale(launch->body.velocity, 1 / length);
    if (!qa_qvm_read_entity(source->vm, pointer, true, &entity, error) ||
        !qa_qvm_read_shared_entity(source->vm, pointer, &shared, error)) return false;
    shared.origin = launch->body.origin;
    entity.pos.delta[0] = direction.x; entity.pos.delta[1] = direction.y; entity.pos.delta[2] = direction.z;
    if (!qa_qvm_write_entity(source->vm, pointer, true, &entity, error) ||
        !qa_qvm_write_shared_entity(source->vm, pointer, &shared, error)) return false;
    int32_t arguments[] = {pointer, pointer + (source->abi == QA_QVM_Q3_MODERN ? 488 : 476), pointer + 36};
    if (!qvm_function(source, NULL, source->fire, arguments, 3, &result, error)) return false;
    uint32_t slot; int32_t active;
    if (!qvm_slot(source, result, &slot, error) ||
        !qvm_word_read(source, (uint32_t)result + source->inuse, &active, error) ||
        !qa_qvm_read_entity(source->vm, result, true, &entity, error)) return false;
    qa_actor_id previous = {0};
    if (!qa_q3_host_actor(source->host, slot, false, &previous, error)) return false;
    bool occupied = previous.registry != 0;
    if (!active || entity.eType != 3 || occupied)
        return application_fail(error, QA_ERROR_FORMAT, "Trajectory QVM fire did not return a new source missile");
    if (!qa_q3_host_bind_actor(source->host, slot, launch->projectile, true, error)) return false;
    source->flags[slot] = QVM_TRAJECTORY_PROJECTILE;
    *attachment = (application_trajectory_attachment){.projectile = launch->projectile, .handle = slot};
    if (!qvm_trajectory(source, slot, update, error)) return false;
    *changed = true; return true;
}

static bool qvm_launch(void *context, const qa_builtin_weapon_launch *launch,
    application_trajectory_attachment *attachment, qa_builtin_trajectory_update *update,
    bool *changed, qa_error *error)
{
    trajectory_qvm *source = context;
    qa_actor_id previous = source->pending_projectile;
    source->pending_projectile = launch->projectile;
    bool okay = qvm_launch_inner(context, launch, attachment, update, changed, error);
    source->pending_projectile = previous;
    return okay;
}

static bool qvm_step(void *context, application_trajectory_attachment *attachment,
    const qa_body_state *body, uint64_t time_ns, qa_builtin_trajectory_update *update,
    bool *changed, qa_error *error)
{
    trajectory_qvm *source = context; *changed = false;
    uint32_t slot;
    if (!qa_q3_host_actor_slot(source->host, attachment->projectile, &slot, error)) return false;
    if (source->flags[slot] & QVM_TRAJECTORY_RETIRED) { attachment->retired = true; return true; }
    if (!qvm_time(source, time_ns, error) || !qvm_refresh(source, attachment->projectile, error)) return false;
    int32_t pointer, active, next, entry, result;
    if (!qvm_pointer(source, slot, &pointer, error) ||
        !qvm_word_read(source, (uint32_t)pointer + source->inuse, &active, error)) return false;
    if (!active) { source->flags[slot] |= QVM_TRAJECTORY_RETIRED; attachment->retired = true; return true; }
    application_source_projection projection = qvm_projection(source);
    if (!application_source_projection_publish(&projection, slot, body, NULL, source->time_ns, error) ||
        !qvm_word_read(source, (uint32_t)pointer + source->nextthink, &next, error)) return false;
    if (next <= 0 || next > (int32_t)(time_ns / UINT64_C(1000000))) return true;
    if (!qvm_word_read(source, (uint32_t)pointer + source->think, &entry, error)) return false;
    size_t count; const qa_qvm_instruction *instructions = qa_qvm_image_instructions(source->image, &count);
    if (entry <= 0 || (uint32_t)entry >= count || instructions[entry].opcode != QA_QVM_ENTER)
        return application_fail(error, QA_ERROR_FORMAT, "Trajectory QVM projectile has an invalid think callback");
    if (!qvm_word_write(source, (uint32_t)pointer + source->nextthink, 0, error) ||
        !qvm_function(source, NULL, (uint32_t)entry, &pointer, 1, &result, error) ||
        !qvm_word_read(source, (uint32_t)pointer + source->inuse, &active, error)) return false;
    qa_q3_entity entity;
    if (!qa_qvm_read_entity(source->vm, pointer, true, &entity, error)) return false;
    if (!active || entity.eType != 3 || (source->flags[slot] & QVM_TRAJECTORY_RETIRED)) {
        source->flags[slot] |= QVM_TRAJECTORY_RETIRED; attachment->retired = true; return true;
    }
    if (!qvm_trajectory(source, slot, update, error)) return false;
    *changed = true; return true;
}

static bool qvm_private_release(trajectory_qvm *source, uint32_t slot, qa_error *error)
{
    int32_t pointer, result;
    if (!qvm_pointer(source, slot, &pointer, error)) return false;
    bool prior = source->releasing; source->releasing = true;
    bool okay;
    if (source->flags[slot] & QVM_TRAJECTORY_CLIENT) {
        int32_t argument = (int32_t)slot;
        okay = qvm_call(source, NULL, 5, &argument, 1, &result, error);
        source->userinfo[slot][0] = 0;
        source->userinfo_ready[slot] = false;
    } else {
        application_source_projection projection = qvm_projection(source);
        okay = application_source_projection_release(&projection, NULL, slot, error);
    }
    source->releasing = prior;
    if (okay) source->flags[slot] = 0;
    return okay;
}

static bool qvm_release(void *context, application_trajectory_attachment *attachment, qa_error *error)
{
    trajectory_qvm *source = context; uint32_t slot;
    if (!qa_q3_host_actor_slot(source->host, attachment->projectile, &slot, NULL)) return true;
    /* The actual release notification supplies the invalidated actor record.
     * Let that notification clear its host binding before private Unlink. */
    if (!qa_actors_get(qa_session_actors(source->application->session), attachment->projectile)) return true;
    return qa_q3_host_detach_actor(source->host, slot, attachment->projectile, error) &&
        qvm_private_release(source, slot, error);
}

static bool qvm_actor_released(void *context, qa_actor_record actor, qa_error *error)
{
    trajectory_qvm *source = context; uint32_t slot;
    bool present = qa_q3_host_actor_slot(source->host, actor.id, &slot, NULL);
    return qa_q3_host_actor_released(source->host, actor, error) &&
        (!present || qvm_private_release(source, slot, error));
}

static bool qvm_host_capture(void *context, qa_buffer *out, qa_error *error)
{
    trajectory_qvm *source = context; qa_buffer host = {0}; qa_source_save_io io = {0};
    if (!source->original.checkpoint(source->original.context, &host, error)) return false;
    bool okay = qa_source_save_writer(&io, source->application->session, error);
    size_t bytes = host.size;
    okay = okay && qa_source_save_count(&io, &bytes, SIZE_MAX) &&
        qa_source_save_bytes(&io, host.data, bytes) &&
        qa_source_save_u64(&io, &source->time_ns) && qa_source_save_bool(&io, &source->initialized);
    size_t count = 0;
    for (size_t slot = 0; slot < QVM_TRAJECTORY_ENTITIES; ++slot) if (source->flags[slot]) ++count;
    okay = okay && qa_source_save_count(&io, &count, QVM_TRAJECTORY_ENTITIES);
    for (uint32_t slot = 0; okay && slot < QVM_TRAJECTORY_ENTITIES; ++slot) if (source->flags[slot]) {
        uint8_t flags = source->flags[slot];
        okay = qa_source_save_u32(&io, &slot) && qa_source_save_u8(&io, &flags);
        if (okay && (flags & QVM_TRAJECTORY_CLIENT)) {
            const char *text = source->userinfo[slot]; okay = qa_source_save_text(&io, &text);
        }
    }
    count = 0;
    for (uint32_t index = 0; index < QA_Q3_CONFIGSTRINGS; ++index)
        if (*qa_q3_configstring(source->configstrings, index)) ++count;
    okay = okay && qa_source_save_count(&io, &count, QA_Q3_CONFIGSTRINGS);
    for (uint32_t index = 0; okay && index < QA_Q3_CONFIGSTRINGS; ++index) {
        const char *text = qa_q3_configstring(source->configstrings, index);
        if (*text) okay = qa_source_save_u32(&io, &index) && qa_source_save_text(&io, &text);
    }
    if (okay) okay = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(&host); return okay;
}

static bool qvm_host_restore(void *context, qa_bytes state, qa_error *error)
{
    trajectory_qvm *source = context; qa_source_save_io io = {0}; qa_bytes host; size_t bytes, count;
    bool okay = qa_source_save_reader(&io, source->application->session, state, error) &&
        qa_source_save_count(&io, &bytes, state.size) && qa_source_save_span(&io, bytes, &host) &&
        source->original.restore(source->original.context, host, error) &&
        qa_source_save_u64(&io, &source->time_ns) && qa_source_save_bool(&io, &source->initialized) &&
        qa_source_save_count(&io, &count, QVM_TRAJECTORY_ENTITIES);
    memset(source->flags, 0, sizeof(source->flags)); memset(source->userinfo, 0, sizeof(source->userinfo));
    memset(source->userinfo_ready, 0, sizeof(source->userinfo_ready));
    for (size_t row = 0; okay && row < count; ++row) {
        uint32_t slot; uint8_t flags;
        okay = qa_source_save_u32(&io, &slot) && qa_source_save_u8(&io, &flags);
        if (okay && (slot >= 1022 || source->flags[slot] || !flags ||
            (flags & ~(QVM_TRAJECTORY_CLIENT | QVM_TRAJECTORY_PROJECTILE | QVM_TRAJECTORY_ACTIVATED | QVM_TRAJECTORY_RETIRED | QVM_TRAJECTORY_TARGET)) ||
            ((flags & QVM_TRAJECTORY_CLIENT) && slot >= source->maximum_clients)))
            okay = application_fail(error, QA_ERROR_FORMAT, "Invalid saved trajectory QVM row state");
        if (!okay) break;
        source->flags[slot] = flags;
        if (flags & QVM_TRAJECTORY_CLIENT) {
            const char *text = NULL;
            okay = qa_source_save_text(&io, &text) && text && qvm_set_userinfo(source, slot, text, error);
        }
    }
    qa_q3_gamestate_init(source->configstrings);
    okay = okay && qa_source_save_count(&io, &count, QA_Q3_CONFIGSTRINGS);
    for (size_t row = 0; okay && row < count; ++row) {
        uint32_t index; const char *text = NULL;
        okay = qa_source_save_u32(&io, &index) && qa_source_save_text(&io, &text);
        if (okay && (index >= QA_Q3_CONFIGSTRINGS || !text || !*text ||
            *qa_q3_configstring(source->configstrings, index)))
            okay = application_fail(error, QA_ERROR_FORMAT, "Invalid saved trajectory QVM configstring");
        if (okay) okay = qa_q3_configstring_set(source->configstrings, index, text, error);
    }
    if (okay) okay = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io); return okay;
}

static bool qvm_capture(void *context, qa_buffer *out, qa_error *error)
{ return qa_qvm_checkpoint(((trajectory_qvm *)context)->vm, out, error); }

static bool qvm_restore(void *context, qa_bytes state, qa_error *error)
{
    trajectory_qvm *source = context;
    if (!qa_qvm_restore(source->vm, state, error) || !qa_q3_host_finish_restore(source->host, error)) return false;
    qa_stamp_set_begin(&source->refreshed);
    source->refreshed_once = false;
    if (source->initialized) { qa_q3_host_game_data data; return qvm_data(source, &data, error); }
    return true;
}

static bool qvm_destroy(void *context, qa_error *error)
{
    trajectory_qvm *source = context;
    if (source->vm) {
        if (!qa_qvm_can_destroy(source->vm) || (source->host && !qa_q3_host_destroy_ready(source->host)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Trajectory QVM is still executing");
        if (!qa_qvm_destroy(source->vm, error)) return false;
        source->vm = NULL;
        if (source->host) qa_q3_host_qvm_consumed(source->host);
    }
    if (source->host && !qa_q3_host_destroy(source->host, error)) return false;
    for (size_t i = 0; i < sizeof(source->cvar_scopes) / sizeof(source->cvar_scopes[0]); ++i)
        qa_cvars_value_scope_destroy(source->cvar_scopes[i]);
    qa_cvars_destroy(source->cvars); qa_qvm_image_release(source->image);
    qa_buffer_free(&source->entities); free(source->configstrings); free(source->marks); free(source);
    return true;
}

static bool qvm_entry(trajectory_qvm *source, const qa_json_document *document, qa_json_id object,
    const char *name, bool optional, uint32_t *out, qa_error *error)
{
    qa_json_id id = qa_json_get(document, object, name); uint64_t value;
    if (optional && (id == QA_JSON_NONE || qa_json_type(document, id) == QA_JSON_NULL)) { *out = 0; return true; }
    if (!qa_json_u64(document, id, &value, error)) return false;
    size_t count; const qa_qvm_instruction *instructions = qa_qvm_image_instructions(source->image, &count);
    if (!value || value >= count || instructions[value].opcode != QA_QVM_ENTER)
        return application_fail(error, QA_ERROR_FORMAT, "Trajectory declaration callback is not a QVM function entry");
    *out = (uint32_t)value; return true;
}

static bool qvm_field(const qa_json_document *document, qa_json_id object, const char *name,
    uint32_t minimum, uint32_t maximum, uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(document, qa_json_get(document, object, name), &value, error)) return false;
    if (value < minimum || value > maximum || (value & 3))
        return application_fail(error, QA_ERROR_FORMAT, "Trajectory declaration field leaves its source record");
    *out = (uint32_t)value; return true;
}

static bool qvm_qualify(trajectory_qvm *source, const qa_catalog_weapon_behavior *behavior, qa_error *error)
{
    qa_json_document *document = NULL;
    if (!qa_json_parse(QA_JSON_STRICT, behavior->entry, &document, error)) return false;
    qa_json_id root = qa_json_root(document);
    bool okay = qa_json_string_equal(document, qa_json_get(document, root, "fireAbi"), "entity-pointer-start-direction");
    if (okay) {
        if (qa_json_string_equal(document, qa_json_get(document, root, "abiProfile"), "q3-1.16n-base")) source->abi = QA_QVM_Q3_116N;
        else okay = qa_json_string_equal(document, qa_json_get(document, root, "abiProfile"), "q3-modern");
    }
    if (!okay) application_fail(error, QA_ERROR_FORMAT, "Unsupported trajectory QVM declaration ABI");
    if (okay) okay = qvm_entry(source, document, root, "fireFunction", false, &source->fire, error) &&
        qvm_entry(source, document, root, "activationFunction", true, &source->activate, error) &&
        qvm_entry(source, document, root, "allocateFunction", false, &source->allocate, error) &&
        qvm_entry(source, document, root, "freeFunction", false, &source->free_entity, error) &&
        qvm_field(document, root, "entityStride", (uint32_t)qa_qvm_shared_entity_bytes(source->abi),
            (uint32_t)qa_qvm_image_memory_size(source->image), &source->stride, error) &&
        qvm_field(document, root, "levelTime", 4, UINT32_MAX, &source->level_time, error) &&
        qa_qvm_qualify_global_word(source->image, source->level_time, error);
    qa_json_id fields = qa_json_get(document, root, "fields");
    uint32_t minimum = (uint32_t)qa_qvm_shared_entity_bytes(source->abi);
    if (okay) okay = qvm_field(document, fields, "inuse", minimum, source->stride - 4, &source->inuse, error) &&
        qvm_field(document, fields, "nextthink", minimum, source->stride - 4, &source->nextthink, error) &&
        qvm_field(document, fields, "think", minimum, source->stride - 4, &source->think, error) &&
        qvm_field(document, fields, "health", minimum, source->stride - 4, &source->health, error);
    if (okay && (source->inuse == source->nextthink || source->inuse == source->think ||
        source->inuse == source->health || source->nextthink == source->think ||
        source->nextthink == source->health || source->think == source->health))
        okay = application_fail(error, QA_ERROR_FORMAT, "Trajectory QVM declaration fields overlap");
    qa_json_destroy(document); return okay;
}

bool application_trajectory_qvm_check(qa_qvm_image *image,
    const qa_catalog_weapon_behavior *behavior, qa_error *error)
{
    trajectory_qvm source = {.image = image};
    return qvm_qualify(&source, behavior, error);
}

bool application_trajectory_qvm_create(qa_application *application, const qa_launch_instance *launch,
    qa_qvm_image *image, const qa_catalog_weapon_behavior *behavior,
    application_trajectory_source *out, qa_error *error)
{
    trajectory_qvm *source = calloc(1, sizeof(*source));
    if (!source) return application_fail(error, QA_ERROR_MEMORY, "Creating QVM trajectory source");
    source->application = application; source->image = image;
    qa_qvm_image_retain(image);
    source->services = application_builtin_services(application, application->world, application->physics);
    uint32_t capacity = qa_actors_capacity(qa_session_actors(application->session)) + 1;
    source->marks = calloc(capacity, sizeof(*source->marks));
    qa_stamp_set_init(&source->refreshed, source->marks, source->marks ? capacity : 0);
    source->configstrings = calloc(1, sizeof(*source->configstrings));
    if (source->configstrings) qa_q3_gamestate_init(source->configstrings);
    bool okay = source->marks && source->configstrings &&
        qa_strings_intern_cstr(qa_session_strings(application->session), behavior->id, &source->owner, error) &&
        qvm_qualify(source, behavior, error);
    static const char *const classes[] = {"worldspawn", "info_player_start", "info_player_deathmatch",
        "info_player_intermission", "team_CTF_redplayer", "team_CTF_blueplayer", "team_CTF_redspawn", "team_CTF_bluespawn"};
    if (okay) okay = application_trajectory_entities_prepare(application, classes,
        sizeof(classes) / sizeof(*classes), &source->entities, error);
    qa_cvar_options cvars = {.dialect = QA_RULESET_Q3, .side = QA_CVAR_SIDE_SERVER,
        .role = QA_CVAR_ROLE_GAME, .declaration_save_policy = application_native_q3_cvar_save_policy};
    if (okay) okay = (source->cvars = qa_cvars_create_view(application->cvars, &cvars, error)) != NULL;
    static const qa_cvar_assignment assignments[][2] = {
        {{"g_gametype", "0"}, {"bot_enable", "0"}},
        {{"g_gametype", "3"}, {"bot_enable", "0"}}
    };
    for (size_t i = 0; okay && i < sizeof(assignments) / sizeof(assignments[0]); ++i)
        okay = qa_cvars_value_scope_create(source->cvars, assignments[i],
            sizeof(assignments[i]) / sizeof(assignments[i][0]), &source->cvar_scopes[i], error);
    if (okay) {
        const qa_cvar_view *maximum = qa_cvars_find(source->cvars, "sv_maxclients");
        source->maximum_clients = maximum && maximum->integer > 0 ? (uint32_t)maximum->integer : 64;
        if (source->maximum_clients > 64) source->maximum_clients = 64;
        qa_q3_host_options options = {.role = QA_QVM_GAME, .abi = source->abi,
            .session = application->session, .world = application->world, .owner = source->owner,
            .cvars = source->cvars, .console = application->console, .mounts = launch->content,
            .command_context = {.owner = source->owner, .dialect = QA_RULESET_Q3, .origin = QA_COMMAND_SERVER},
            .common = {.context = source, .print = qvm_print, .milliseconds = qvm_milliseconds,
                .calendar = qvm_calendar, .arguments = qvm_arguments},
            .server = {.context = source, .maximum_clients = source->maximum_clients,
                .projected_entities = true, .project_actor = qvm_project_actor,
                .pending_spawn = &source->pending_projectile,
                .configstring = qvm_configstring, .set_configstring = qvm_set_configstring,
                .userinfo = qvm_userinfo, .set_userinfo = qvm_set_userinfo, .user_command = qvm_user_command,
                .drop_client = qvm_drop_client, .send_command = qvm_send_command},
            .entity_text = {source->entities.data, source->entities.size}};
        okay = qa_q3_host_create(&options, &source->host, error);
    }
    if (okay) {
        source->original = qa_q3_host_qvm_options(source->host, QA_QVM_COMPILED_SEMANTICS);
        qa_qvm_options options = source->original;
        options.context = source; options.syscall = qvm_syscall;
        options.checkpoint = qvm_host_capture; options.restore = qvm_host_restore;
        okay = qa_qvm_create(image, &options, &source->vm, error) &&
            qa_q3_host_attach_qvm(source->host, source->vm, error) &&
            qa_qvm_bind_function(source->vm, source->free_entity, true,
                qvm_free_hook, source, &source->free_binding, error);
    }
    if (!okay) {
        if (error && error->code == QA_OK) application_fail(error, QA_ERROR_MEMORY, "Preparing QVM trajectory source");
        (void)qvm_destroy(source, NULL); return false;
    }
    *out = (application_trajectory_source){.context = source, .launch = qvm_launch, .step = qvm_step,
        .release = qvm_release, .actor_released = qvm_actor_released, .capture = qvm_capture,
        .restore = qvm_restore, .destroy = qvm_destroy};
    return true;
}
