#include "internal.h"
#include "native_q3_console.h"
#include "engine_shutdown.h"

#include <stdlib.h>
#include <string.h>

static application_provider *selected_arsenal(void *opaque, qa_actor_id actor)
{
    application_provider *source = opaque;
    return application_provider_for(source->application, actor, QA_ROLE_ARSENAL, "");
}

bool application_native_cheats_enabled(void *opaque)
{
    application_provider *source = opaque;
    const qa_cvar_view *cheats = qa_cvars_find(application_engine_shutdown_cvars(source), "sv_cheats");
    return cheats != NULL && cheats->integer != 0;
}

bool application_native_q3_console_print(void *opaque, const char *text,
                                          qa_error *error)
{
    application_provider *source = opaque;
    if (!source || source->kind != APPLICATION_PROVIDER_Q3 || !text ||
        !source->application || !source->state.q3 ||
        !source->constructed || !source->attached || source->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 print source has retired");
    qa_application *application = source->application;
    qa_q3_game *game = source->state.q3;
    struct application_native_q3_console *console = source->native_q3_console;
    qa_actor_owner owner = source->owner;
    qa_command_context context = {.owner = source->owner, .dialect = QA_CONSOLE_Q3,
                                  .origin = QA_COMMAND_SERVER};
    if (!qa_application_capture_command_context(application, &context, &context, error))
        return false;
    size_t length = strlen(text);
    if (length == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 print exceeds addressable source storage");
    char *retained = malloc(length + 1);
    if (!retained)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q3 print text");
    memcpy(retained, text, length + 1);
    if (!application_native_q3_console_borrow(source, error)) {
        free(retained);
        return false;
    }
    application_console_print(application, &context, retained);
    bool current = source->kind == APPLICATION_PROVIDER_Q3 &&
        source->application == application && source->state.q3 == game &&
        source->native_q3_console == console && source->owner == owner &&
        source->constructed && source->attached && !source->close_pending &&
        qa_application_command_context_active(application, &context);
    application_native_q3_console_release(source);
    free(retained);
    return current || application_fail(error, QA_ERROR_ARGUMENT,
        "Q3 print callback retired or replaced its source publication");
}

bool application_native_console_motion(void *opaque, qa_actor_id actor,
                                       bool noclip, qa_error *error)
{
    application_provider *source = opaque;
    qa_application *application = source->application;
    bool spectator = false;
    if (application->modes != NULL && application->primary_mode_ready) {
        qa_mode_player_view player;
        if (!qa_modes_player_read(application->modes, application->primary_mode,
                                   actor, &player, error))
            return false;
        spectator = player.state.spectator;
    }
    return application_control_player_mode(application, actor,
        noclip ? QA_MOVEMENT_MODE_NOCLIP : QA_MOVEMENT_MODE_NORMAL, spectator, error);
}

bool application_native_grant_arsenal(void *opaque, qa_actor_id actor,
                                      bool ammo, bool *handled, qa_error *error)
{
    if (opaque == NULL || handled == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "arsenal grant requires a source and result");
    *handled = false;
    application_provider *selected = selected_arsenal(opaque, actor);
    if (selected == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND, "actor has no selected arsenal owner");
    switch (selected->kind) {
    case APPLICATION_PROVIDER_Q1:
        return qa_q1_game_grant_arsenal(selected->state.q1, actor, ammo, handled, error);
    case APPLICATION_PROVIDER_Q2:
        *handled = true;
        return qa_q2_game_grant_arsenal(selected->state.q2, actor, ammo, error);
    case APPLICATION_PROVIDER_Q3:
        *handled = true;
        return qa_q3_game_grant_arsenal(selected->state.q3, actor, ammo, error);
    case APPLICATION_PROVIDER_QC:
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "selected arsenal has no direct grant adapter");
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "unknown selected arsenal provider");
}

bool application_native_give_item(void *opaque, qa_actor_id actor,
                                  size_t count, const char *const *arguments,
                                  bool *handled, qa_error *error)
{
    if (opaque == NULL || handled == NULL || (count != 0 && arguments == NULL))
        return application_fail(error, QA_ERROR_ARGUMENT, "item grant requires a source and arguments");
    *handled = false;
    application_provider *selected = selected_arsenal(opaque, actor);
    if (selected == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND, "actor has no selected arsenal owner");
    switch (selected->kind) {
    case APPLICATION_PROVIDER_Q1:
        return qa_q1_game_give_item(selected->state.q1, actor, count, arguments, handled, error);
    case APPLICATION_PROVIDER_Q2:
        return qa_q2_game_give_item(selected->state.q2, actor, count, arguments, handled, error);
    case APPLICATION_PROVIDER_Q3:
        return qa_q3_game_give_item(selected->state.q3, actor, count, arguments, handled, error);
    case APPLICATION_PROVIDER_QC:
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "selected arsenal has no direct item grant adapter");
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "unknown selected arsenal provider");
}

bool application_native_q3_award(void *opaque, qa_actor_id actor,
                                 qa_q3_source_award award, qa_error *error)
{
    application_provider *source = opaque;
    qa_application *application = source->application;
    if (application->modes == NULL || !application->primary_mode_ready)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 award requires an active mode");
    return qa_modes_source_award(application->modes, application->primary_mode,
                                 actor, award, error);
}

bool application_native_suicide(void *opaque, qa_actor_id actor, qa_error *error)
{
    application_provider *source = opaque;
    qa_application *application = source->application;
    qa_damage_request request = {
        .target = actor,
        .amount = 100000,
        .attack = {
            .attacker = actor,
            .inflictor = actor,
            .weapon_provider = source->owner,
            .time_ns = qa_session_elapsed(application->session),
            .cause = {.kind = QA_CAUSE_Q3,
                      .source.q3 = {.means_of_death = 20}},
        },
    };
    return application_force_death(application, &request, error);
}

static bool native_horde_finish(qa_q1_game_operation *operation, bool okay, qa_error *error)
{
    if (okay && !qa_q1_game_operation_live(operation))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Horde source retired during callback");
    qa_q1_game_operation_end(operation);
    return okay;
}

bool application_native_horde_spawn_monster(void *opaque, qa_mode_id mode,
    qa_string_id classname, qa_vec3 origin, qa_vec3 angles, qa_actor_id enemy,
    qa_actor_id *out, qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_mode_provider(application, mode);
    qa_actor_id manager;
    const char *name = qa_strings_cstr(qa_session_strings(application->session), classname);
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1 || !name)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "selected Horde source has no native monster adapter");
    if (!qa_modes_horde_manager_actor(application->modes, mode, &manager, error)) return false;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(provider->state.q1, &operation, error)) return false;
    bool okay = qa_q1_horde_spawn(provider->state.q1, name, origin, angles, manager, enemy, out, error);
    return native_horde_finish(&operation, okay, error);
}

bool application_native_horde_spawn_loot(void *opaque, qa_mode_id mode,
    const qa_mode_loot_spawn *spawn, qa_actor_id *out, qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_mode_provider(application, mode);
    const char *name = spawn ? qa_strings_cstr(qa_session_strings(application->session),
                                               spawn->classname) : NULL;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1 || !name)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "selected Horde source has no native loot adapter");
    qa_q1_spawn native = {.classname = name, .origin = spawn->body.origin,
                          .angles = spawn->body.angles, .spawnflags = spawn->spawnflags};
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(provider->state.q1, &operation, error)) return false;
    bool okay = qa_q1_pickup_spawn_external(provider->state.q1, &native, &spawn->body,
                                           spawn->bounce, out, error);
    return native_horde_finish(&operation, okay, error);
}

bool application_native_horde_grant_loot(void *opaque, qa_actor_id item,
    qa_actor_id player, bool *accepted, qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(application, item, QA_ROLE_ENTITIES, "");
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "Horde loot has no native pickup owner");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(provider->state.q1, &operation, error)) return false;
    bool okay = qa_q1_pickup_grant_external(provider->state.q1, item, player, accepted, error);
    return native_horde_finish(&operation, okay, error);
}

bool application_native_horde_head(void *opaque, qa_actor_id actor,
    bool enabled, qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(application, actor, QA_ROLE_ENTITIES, "");
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "Horde head has no native death continuation");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(provider->state.q1, &operation, error)) return false;
    bool okay = qa_q1_horde_after_death(provider->state.q1, actor, enabled, error);
    return native_horde_finish(&operation, okay, error);
}

bool application_native_horde_alpha(void *opaque, qa_actor_id actor,
    float alpha, qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(application, actor, QA_ROLE_ENTITIES, "");
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "Horde loot has no native alpha owner");
    return qa_q1_game_alpha(provider->state.q1, actor, alpha, error);
}

float application_native_horde_random(void *opaque, qa_mode_id mode)
{
    application_provider *provider = application_mode_provider(opaque, mode);
    return provider && provider->kind == APPLICATION_PROVIDER_Q1
        ? qa_q1_game_random(provider->state.q1) : 0;
}

bool application_native_horde_point(void *opaque, qa_mode_id mode, qa_actor_id actor,
    qa_vec3 *origin, qa_vec3 *angles, qa_string_id *target, uint32_t *flags, qa_error *error)
{
    (void)mode;
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(application, actor, QA_ROLE_ENTITIES, "");
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1)
        return false;
    qa_horde_point point;
    bool found;
    if (!qa_q1_game_map_horde_point_read(provider->state.q1, actor, &point, &found, error) || !found)
        return false;
    *origin = point.origin; *angles = point.angles; *target = point.target; *flags = point.flags;
    return true;
}

bool application_native_horde_manager(void *opaque, qa_mode_id mode, qa_actor_id actor,
    qa_string_id *target, qa_actor_id *activator, qa_error *error)
{
    (void)mode;
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(application, actor, QA_ROLE_ENTITIES, "");
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1)
        return false;
    bool found;
    return qa_q1_game_map_horde_manager_read(provider->state.q1, actor, target, activator,
                                            &found, error) && found;
}

bool application_native_horde_restart(void *opaque, qa_mode_id mode,
    uint32_t starting_flags, qa_error *error)
{
    qa_application *application = opaque;
    qa_actor_id manager;
    if (!qa_modes_horde_manager_actor(application->modes, mode, &manager, error)) return false;
    application_provider *provider = application_provider_for(application, manager, QA_ROLE_ENTITIES, "");
    const char *map = qa_strings_cstr(qa_session_strings(application->session), application->current_map);
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1 || !map || !*map)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "Horde restart has no authored campaign owner");
    if (!qa_application_queue_travel(application, &(qa_application_travel_request){
        .provider = provider->owner, .cause = manager, .geometry = application->map_geometry,
        .expression = map, .carry_players = false}, error))
        return false;
    provider->q1_server_flags = starting_flags;
    return true;
}
