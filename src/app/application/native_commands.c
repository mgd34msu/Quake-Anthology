#include "internal.h"
#include "../../console/internal.h"
#include "native_q3_console.h"
#include "engine_shutdown.h"
#include "unified_q3_events.h"
#include "map_players_private.h"
#include "native_q1_console.h"
#include "native_q1_wire.h"
#include "native_q1_powers.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q3_clients.h"

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
    qa_console *physical_console=NULL;
    qa_command_context context;
    if (!application_native_q3_console_at(source,&physical_console,NULL,&context) ||
        physical_console!=application->console)
        return application_fail(error,QA_ERROR_NOT_FOUND,"Q3 print source has no live GAME console");
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
    bool emitted = application_unified_q3_text(source, APPLICATION_Q3_SOURCE_PRINT, 0, retained, error);
    if (emitted) application_console_print(application, &context, retained);
    bool current = source->kind == APPLICATION_PROVIDER_Q3 &&
        source->application == application && source->state.q3 == game &&
        source->native_q3_console == console && source->owner == owner &&
        source->constructed && source->attached && !source->close_pending &&
        qa_application_command_context_active(application, &context);
    application_native_q3_console_release(source);
    free(retained);
    return emitted && (current || application_fail(error, QA_ERROR_ARGUMENT,
        "Q3 print callback retired or replaced its source publication"));
}

static bool selected_spectator(qa_application *application, qa_actor_id actor,
    bool *spectator, qa_error *error)
{
    if (application->modes != NULL && application->primary_mode_ready) {
        qa_mode_player_view player;
        if (!qa_modes_player_read(application->modes, application->primary_mode,
                                   actor, &player, error))
            return false;
        *spectator = *spectator || player.state.spectator;
    }
    return true;
}

bool application_native_console_motion(void *opaque, qa_actor_id actor,
                                       bool noclip, qa_error *error)
{
    application_provider *source = opaque;
    qa_application *application = source->application;
    bool spectator = false;
    if (!selected_spectator(application, actor, &spectator, error)) return false;
    return application_control_player_mode(application, actor,
        noclip ? QA_MOVEMENT_MODE_NOCLIP : QA_MOVEMENT_MODE_NORMAL, spectator, error);
}

typedef struct engine_fly_call {
    qa_application *application;
    application_provider *source;
    const qa_command_invocation *invocation;
    const qa_command_context *context;
    void *game;
    application_provider_kind kind;
} engine_fly_call;

static bool engine_fly_client(const engine_fly_call *call, qa_actor_id actor,
    bool *spectator, bool *noclip, qa_error *error)
{
    qa_application *app = call->application;
    application_provider *source = call->source;
    if (app->destroy_requested || app->finalizing || !app->players ||
        !qa_actor_id_equal(actor, call->context->actor) ||
        !qa_console_invocation_current(app->console, call->invocation) ||
        !qa_application_command_context_active(app, call->context) ||
        source->application != app || source->kind != call->kind ||
        !source->constructed || !source->attached || source->close_pending ||
        application_provider_for(app, actor, QA_ROLE_CHARACTER, "") != source)
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE fly lost its selected Source client");
    uint32_t slot;
    if (call->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_player_info client;
        if (source->state.q2 != call->game ||
            !qa_actor_id_equal(qa_q2_current_actor(source->state.q2), actor) ||
            !qa_q2_player_read(source->state.q2, actor, &client) || !client.connected)
            return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE fly requires its actual Q2 client operation");
        slot = client.slot;
        *spectator = client.spectator;
        *noclip = client.noclip;
    } else {
        qa_q3_native_client client;
        qa_q3_player_state player;
        if (source->state.q3 != call->game ||
            !qa_q3_native_client_slot(source->state.q3, actor, &slot, error) ||
            !qa_q3_client_read(source->state.q3, actor, &client, error) ||
            client.connected != QA_Q3_CLIENT_CONNECTED ||
            !qa_q3_player_read(source->state.q3, actor, &player))
            return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE fly requires its actual Q3 client");
        *spectator = player.spectator;
        *noclip = player.noclip;
    }
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *row = app->players->records + i;
        if (!row->retiring && !row->source_begin_pending && row->character == source &&
            qa_actor_id_equal(row->actor, actor) && row->client_slot == slot) return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE fly differs from its full physical Source roster");
}

static bool engine_fly(void *opaque, qa_actor_id actor, qa_error *error)
{
    const engine_fly_call *call = opaque;
    application_provider *source = call->source;
    bool spectator, noclip;
    if (!engine_fly_client(call, actor, &spectator, &noclip, error)) return false;
    const char *text = NULL;
    if (call->kind == APPLICATION_PROVIDER_Q2) {
        if (!qa_q2_player_cheats_allowed(source->state.q2))
            text = "You must run the server with '+set cheats 1' to enable this command.\n";
    } else {
        if (!application_native_cheats_enabled(source))
            text = "Cheats are not enabled on this server.\n";
        else {
            qa_combat_state combat;
            if (!qa_combat_read(call->application->combat, actor, &combat, error)) return false;
            if (combat.health <= 0) text = "You must be alive to use this command.\n";
        }
    }
    if (!text) {
        if (noclip) {
            if (call->kind == APPLICATION_PROVIDER_Q2) {
                if (!qa_q2_player_command(source->state.q2, actor, "noclip", 0, NULL, error)) return false;
            } else {
                bool enabled;
                if (!qa_q3_player_noclip(source->state.q3, actor, &enabled, error)) return false;
            }
        }
        if (!engine_fly_client(call, actor, &spectator, &noclip, error) ||
            !selected_spectator(call->application, actor, &spectator, error)) return false;
        bool enabled;
        if (!application_control_toggle_motion(call->application, actor, QA_PHYSICS_FLY,
                spectator, &enabled, error)) return false;
        text = enabled ? "fly ON\n" : "fly OFF\n";
    }
    if (!engine_fly_client(call, actor, &spectator, &noclip, error)) return false;
    application_console_print(call->application, call->context, text);
    return engine_fly_client(call, actor, &spectator, &noclip, error);
}

qa_command_result application_native_engine_fly(qa_application *application,
    const qa_command_invocation *invocation, const qa_command_context *context, qa_error *error)
{
    if (invocation->console != application->console || context->owner ||
        !qac_equal(invocation->argv[0], "fly") || !context->actor.registry ||
        !qa_console_invocation_current(application->console, invocation)) return QA_COMMAND_UNHANDLED;
    application_provider *source = application_provider_for(application, context->actor,
        QA_ROLE_CHARACTER, "");
    if (!source || (source->kind != APPLICATION_PROVIDER_Q2 &&
                   source->kind != APPLICATION_PROVIDER_Q3)) return QA_COMMAND_UNHANDLED;
    engine_fly_call call = {.application = application, .source = source,
        .invocation = invocation, .context = context, .kind = source->kind,
        .game = source->kind == APPLICATION_PROVIDER_Q2 ? (void *)source->state.q2
                                                       : (void *)source->state.q3};
    bool okay;
    if (source->kind == APPLICATION_PROVIDER_Q2)
        okay = qa_q2_run_actor(source->state.q2, context->actor, engine_fly, &call, error);
    else {
        if (!application_native_q3_console_borrow(source, error)) return QA_COMMAND_FAILED;
        okay = engine_fly(&call, context->actor, error);
        application_native_q3_console_release(source);
    }
    return okay ? QA_COMMAND_HANDLED : QA_COMMAND_FAILED;
}

static bool q1_cheat_current(application_provider *source, qa_application *application,
    qa_q1_game_operation *operation, qa_actor_id actor, qa_q1_source_client_view *client,
    qa_error *error)
{
    if (!source || !application || application->destroy_requested || application->finalizing ||
        !application->session || !application->players || source->application != application ||
        source->kind != APPLICATION_PROVIDER_Q1 || !source->constructed || !source->attached ||
        source->close_pending || source->state.q1 != operation->game ||
        !qa_q1_game_operation_live(operation) ||
        (application_world_provider(application, QA_ROLE_ENTITIES, "") != source &&
         application_provider_for(application, actor, QA_ROLE_CHARACTER, "") != source) ||
        !qa_q1_source_client_read(operation->game, actor, client))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 cheat lost its actual published Source client");
    for (size_t i = 0; i < application->players->count; ++i) {
        const application_player_record *record = application->players->records + i;
        if (!record->retiring && qa_actor_id_equal(record->actor, actor) &&
            record->client_slot == client->slot) return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Q1 cheat actor differs from its physical Source roster");
}

bool application_native_q1_console_cheat(void *opaque, qa_actor_id actor,
    const char *name, bool *enabled, qa_error *error)
{
    application_provider *source = opaque;
    qa_application *application = source ? source->application : NULL;
    if (!source || !application || source->kind != APPLICATION_PROVIDER_Q1 ||
        !source->state.q1 || !name || !enabled ||
        (strcmp(name, "god") && strcmp(name, "noclip") && strcmp(name, "fly")))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 cheat requires its actual Source and toggle");
    if (application->operation == APPLICATION_IDLE && qa_console_idle(application->console) &&
        application_native_q1_console_idle(source) && application_native_q1_wire_idle(source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 cheat requires an entered Source command");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(source->state.q1, &operation, error)) return false;
    qa_q1_source_client_view client;
    bool okay = q1_cheat_current(source, application, &operation, actor, &client, error);
    if (okay && !strcmp(name, "god")) {
        okay = qa_q1_source_client_toggle_god(operation.game, actor, enabled, error) &&
            q1_cheat_current(source, application, &operation, actor, &client, error) &&
            application_native_q1_powerup(source, actor, QA_Q1_INVULNERABILITY,
                qa_q1_game_power_expires(operation.game, actor, QA_Q1_INVULNERABILITY), error);
    } else if (okay) {
        bool spectator = client.observer;
        okay = selected_spectator(application, actor, &spectator, error) &&
            q1_cheat_current(source, application, &operation, actor, &client, error);
        if (okay) okay = application_control_toggle_motion(application, actor,
            !strcmp(name, "fly") ? QA_PHYSICS_FLY : QA_PHYSICS_NOCLIP, spectator, enabled, error);
    }
    if (okay) okay = q1_cheat_current(source, application, &operation, actor, &client, error);
    qa_q1_game_operation_end(&operation);
    return okay;
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
                                 actor, (int32_t)award, error);
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
                          .angles = spawn->body.angles, .spawnflags = spawn->spawnflags,
                          .source_movement_flags = UINT32_C(256)};
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
