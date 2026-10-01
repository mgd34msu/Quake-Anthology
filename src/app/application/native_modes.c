#include "internal.h"
#include "native_maps.h"
#include "native_q3_console.h"
#include "native_q3_clients.h"
#include "native_q3_settings.h"
#include "native_q3_wire_state.h"
#include "native_q3_votes.h"
#include "native_q3_objectives.h"
#include "native_q3_team_status.h"
#include "native_q3_match.h"
#include "native_q3_session.h"
#include "q3_restart.h"
#include "guest_q3_restart.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q3_shader_remap.h"
#include "qa/game_q3_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool q1_finish(qa_q1_game_operation *operation, bool okay, qa_error *error) {
    if (okay && !qa_q1_game_operation_live(operation))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "mode source retired during callback");
    qa_q1_game_operation_end(operation);
    return okay;
}

typedef struct mode_q2_select { qa_q2_game *game; qa_q2_weapon weapon; } mode_q2_select;
static bool q2_select(void *opaque, qa_actor_id actor, qa_error *error) {
    mode_q2_select *call = opaque;
    qa_q2_selection selection;
    return qa_q2_weapon_select(call->game, actor, call->weapon, false, &selection, error);
}
static bool q2_armor(void *opaque, qa_actor_id actor, qa_error *error) {
    bool accepted;
    return qa_q2_item_give(opaque, actor, "item_armor_body", 0, &accepted, error);
}

application_provider *application_mode_provider(qa_application *app, qa_mode_id mode) {
    qa_mode_view view;
    if (!app->modes || !qa_modes_read(app->modes, mode, &view, NULL)) return NULL;
    const qa_launch_snapshot *snapshot = app->routing_snapshot;
    if (!snapshot && app->configuration) snapshot = qa_configuration_current(app->configuration);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    size_t index = 0;
    while (index < app->mode_count &&
           (app->mode_ids[index].slot != mode.slot ||
            app->mode_ids[index].generation != mode.generation)) ++index;
    if (!choices || index >= app->mode_count || index >= choices->mode_count) return NULL;
    application_provider **providers = app->routing_providers ? app->routing_providers : app->providers;
    size_t count = app->routing_providers ? app->routing_provider_count : app->provider_count;
    for (size_t i = 0; i < count; ++i) {
        application_provider *p = providers[i];
        if (p && p->constructed && p->attached && !p->close_pending && p->launch &&
            !strcmp(p->launch->selection.instance, choices->modes[index].instance)) return p;
    }
    return NULL;
}

static bool native_q3_mode(application_provider *p, qa_mode_id id) {
    qa_mode_view view;
    return application_native_q3_mode_source_provider(p->application, id) == p &&
        qa_modes_read(p->application->modes, id, &view, NULL) &&
        view.rules.source >= QA_MODE_Q3 && view.rules.kind >= QA_MODE_FFA &&
        view.rules.kind <= QA_MODE_HARVESTER;
}

bool application_native_q3_mode_frame_owned(qa_application *app, qa_mode_id id) {
    application_provider *p = app ? application_native_q3_mode_source_provider(app, id) : NULL;
    return p && p->kind == APPLICATION_PROVIDER_Q3 && p->constructed && p->attached &&
        !p->close_pending && p->state.q3 && application_native_q3_console_settings_bound(p) &&
        native_q3_mode(p, id);
}

bool application_native_mode_q3_team_status_bound(void *opaque, qa_mode_id id) {
    qa_application *app = opaque;
    application_provider *p = app ? application_native_q3_mode_source_provider(app, id) : NULL;
    return p && p == application_world_provider(app, QA_ROLE_ENTITIES, "") &&
        native_q3_mode(p, id) && application_native_q3_team_status_bound(p);
}

bool application_native_mode_q3_source_match_exit(void *opaque, qa_mode_id mode,
    qa_string_id reason, qa_error *error) {
    qa_application *app = opaque;
    application_provider *p = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    if (!p || p->kind != APPLICATION_PROVIDER_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 match exit has no actual native GAME source");
    return application_native_q3_match_log_exit(p, mode, reason, error);
}

bool application_native_q3_source_world_initialize(void *opaque, qa_error *error) {
    application_provider *p = opaque;
    return application_native_q3_match_init(p, error) &&
        application_native_q3_session_initialize(p, error);
}

static bool q3_settings_values(application_provider *p, qa_mode_q3_settings *out, qa_error *e) {
    const application_native_q3_cvar_snapshot *warmup;
    *out = (qa_mode_q3_settings){0};
    if (!application_native_q3_settings_integer(p, "g_doWarmup", &out->do_warmup, e) ||
        !application_native_q3_settings_integer(p, "g_warmup", &out->warmup_seconds, e) ||
        !application_native_q3_settings_integer(p, "timelimit", &out->time_limit_minutes, e) ||
        !application_native_q3_settings_integer(p, "fraglimit", &out->frag_limit, e) ||
        !application_native_q3_settings_integer(p, "capturelimit", &out->capture_limit, e) ||
        !application_native_q3_settings_snapshot(p, "g_warmup", &warmup, e)) return false;
    out->warmup_modification_count = warmup->modification_count;
    return true;
}

bool application_native_mode_q3_clock(void *opaque, qa_mode_id mode, int32_t *out, qa_error *e) {
    qa_application *app = opaque;
    application_provider *p = application_native_q3_mode_source_provider(app, mode);
    if (!p || !out ||
        !native_q3_mode(p, mode))
        return application_fail(e, QA_ERROR_NOT_FOUND, "Q3 mode has no actual source clock");
    if (p->kind == APPLICATION_PROVIDER_Q3 && p->state.q3)
        return qa_q3_source_clock(p->state.q3, out, e);
    if ((p->kind == APPLICATION_PROVIDER_QVM || p->kind == APPLICATION_PROVIDER_NATIVE) &&
        p->component.clock.kind == QA_CLOCK_Q3)
        return application_q3_guest_round_clock(p, out, e);
    return application_fail(e, QA_ERROR_NOT_FOUND, "Q3 mode has no running GAME clock owner");
}

bool application_native_mode_q3_warmup_restart(void *opaque, qa_mode_id mode, qa_error *e) {
    qa_application *app = opaque;
    application_provider *p = application_native_q3_mode_source_provider(app, mode);
    qa_cvars *cvars = application_native_q3_console_registry(p);
    const qa_cvar_view *flag = qa_cvars_find(cvars, "g_restarted");
    if (!p || !native_q3_mode(p, mode) || !flag || flag->owner != p->owner)
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 warmup restart lacks its actual source flag");
    if (!application_native_q3_console_borrow(p, e)) return false;
    bool okay = qa_cvars_set(cvars, "g_restarted", "1", true, e);
    application_native_q3_console_release(p);
    return okay;
}

bool application_native_q3_restart_configstring(application_provider *provider,
    uint32_t index, const char *text, qa_error *error) {
    qa_application *app = provider ? provider->application : NULL;
    if (!app || !text || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        app->destroy_requested ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_CONFIGURING) ||
        !qa_q3_destroy_ready(provider->state.q3))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 configstring has no idle source owner");
    if (!application_native_q3_console_borrow(provider, error)) return false;
    bool okay = qa_q3_configstring_write(provider->state.q3, index, text, error);
    application_native_q3_console_release(provider);
    return okay;
}

static bool q3_settings_ready(application_provider *p, bool admitted, qa_error *e) {
    qa_application *app = p ? p->application : NULL;
    if (!app || p->kind != APPLICATION_PROVIDER_Q3 || !p->state.q3 || !p->constructed ||
        !p->attached || p->close_pending || app->destroy_requested || !app->modes ||
        !application_native_q3_console_registry(p))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 settings source is not admitted");
    /* Candidate cvar restore validates source text before private owners finish;
     * it sends no changed notifications. Late reconnect checks the values. */
    if (app->operation == APPLICATION_PERSISTING) return true;
    if (!qa_modes_idle(app->modes))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 settings source is busy");
    if (!qa_q3_destroy_ready(p->state.q3))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 settings source is busy");
    if (admitted)
        for (size_t i = 0; i < app->mode_count; ++i) {
            qa_mode_id id = app->mode_ids[i];
            qa_mode_q3_settings settings;
            bool present;
            if (native_q3_mode(p, id) &&
                (!qa_modes_q3_settings_read(app->modes, id, &settings, &present, e) || !present))
                return application_fail(e, QA_ERROR_ARGUMENT, "Q3 settings mode is not admitted");
        }
    return true;
}

static bool q3_initial_rules(application_provider *p, qa_mode_rules *out, bool *found, qa_error *e) {
    qa_application *app = p->application;
    const qa_launch_snapshot *snapshot = app->routing_snapshot;
    if (!snapshot && app->configuration) snapshot = qa_configuration_current(app->configuration);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    *found = false;
    bool primary = false;
    if (!choices) return application_fail(e, QA_ERROR_ARGUMENT, "Q3 settings have no actual configuration");
    for (size_t i = 0; i < app->mode_count && i < choices->mode_count; ++i) {
        if (!native_q3_mode(p, app->mode_ids[i])) continue;
        const qa_launch_mode *choice = &choices->modes[i];
        if (choice->primary_score) { *out = choice->rules; *found = true; primary = true; break; }
    }
    if (primary) return true;
    for (size_t i = 0; i < app->mode_count && i < choices->mode_count; ++i) {
        if (!native_q3_mode(p, app->mode_ids[i])) continue;
        const qa_mode_rules *rules = &choices->modes[i].rules;
        if (*found && (out->warmup_seconds != rules->warmup_seconds || out->frag_limit != rules->frag_limit ||
            out->capture_limit != rules->capture_limit || out->time_limit_minutes != rules->time_limit_minutes))
            return application_fail(e, QA_ERROR_ARGUMENT, "Q3 source modes have conflicting initial settings");
        *out = *rules;
        *found = true;
    }
    return true;
}

bool application_native_q3_settings_register(application_provider *p, qa_error *e) {
    if (!q3_settings_ready(p, false, e)) return false;
    if (application_native_q3_console_settings_bound(p)) return true;
    if (p->application->operation == APPLICATION_PERSISTING) {
        if (!application_native_q3_settings_initialized(p))
            return application_fail(e, QA_ERROR_ARGUMENT, "restored Q3 mode has no imported source settings");
        application_native_q3_console_settings_commit(p);
        return true;
    }
    qa_q3_rules native;
    qa_mode_rules rules = {0};
    bool found;
    if (!qa_q3_rules_read(p->state.q3, &native, e) || !q3_initial_rules(p, &rules, &found, e)) return false;
    char values[6][64];
    snprintf(values[0], sizeof(values[0]), "%d", native.game_type);
    snprintf(values[1], sizeof(values[1]), "%d", found ? rules.frag_limit : 20);
    snprintf(values[2], sizeof(values[2]), "%.9g", found ? (double)rules.time_limit_minutes : 0.0);
    snprintf(values[3], sizeof(values[3]), "%d", found ? rules.capture_limit : 8);
    snprintf(values[4], sizeof(values[4]), "%d", found && rules.warmup_seconds ? rules.warmup_seconds : 20);
    snprintf(values[5], sizeof(values[5]), "%d", found && rules.warmup_seconds != 0);
    static const char *names[] = {"g_gametype", "fraglimit", "timelimit", "capturelimit",
        "g_warmup", "g_doWarmup"};
    static const char *defaults[] = {"0", "20", "0", "8", "20", "0"};
    static const uint32_t flags[] = {QA_CVAR_SERVERINFO | QA_CVAR_USERINFO | QA_CVAR_LATCH,
        QA_CVAR_SERVERINFO | QA_CVAR_ARCHIVE | QA_CVAR_NO_RESTART,
        QA_CVAR_SERVERINFO | QA_CVAR_ARCHIVE | QA_CVAR_NO_RESTART,
        QA_CVAR_SERVERINFO | QA_CVAR_ARCHIVE | QA_CVAR_NO_RESTART,
        QA_CVAR_ARCHIVE, 0};
    qa_cvars *cvars = application_native_q3_console_registry(p);
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        bool existed = qa_cvars_find(cvars, names[i]) != NULL;
        if (!qa_cvars_register(cvars, names[i], defaults[i], flags[i], p->owner, NULL, e) ||
            (!existed && !qa_cvars_set(cvars, names[i], values[i], true, e))) return false;
    }
    bool friendly_existed = qa_cvars_find(cvars, "g_friendlyFire") != NULL;
    if (!qa_cvars_register(cvars, "g_friendlyFire", "0", QA_CVAR_ARCHIVE, p->owner, NULL, e) ||
        (!friendly_existed && native.friendly_fire &&
         !qa_cvars_set(cvars, "g_friendlyFire", "1", true, e))) return false;
    if (!application_native_q3_settings_register_cache(p, __DATE__, e)) return false;
    application_native_q3_console_settings_commit(p);
    return true;
}

bool application_native_q3_settings_install(application_provider *p, qa_error *e) {
    if (!q3_settings_ready(p, false, e)) return false;
    if (!application_native_q3_console_settings_bound(p))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 settings require registered source bindings");
    qa_application *app = p->application;
    qa_mode_q3_settings settings;
    if (!q3_settings_values(p, &settings, e)) return false;
    int32_t restarted;
    if (!application_native_q3_settings_integer(p, "g_restarted", &restarted, e)) return false;
    for (size_t i = 0; i < app->mode_count; ++i) {
        qa_mode_id id = app->mode_ids[i];
        if (!native_q3_mode(p, id)) continue;
        qa_mode_q3_settings current;
        bool present;
        int32_t now;
        if (!qa_modes_q3_settings_read(app->modes, id, &current, &present, e)) return false;
        if (!present && (!application_native_mode_q3_clock(app, id, &now, e) ||
            !qa_modes_q3_settings_admit(app->modes, id, &settings, now, restarted, e))) return false;
    }
    return true;
}

bool application_native_q3_settings_reconnect(application_provider *p, qa_error *e) {
    if (!p || !application_native_q3_console_settings_bound(p))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 settings lost their actual source bindings");
    qa_mode_q3_settings current;
    if (!q3_settings_values(p, &current, e)) return false;
    qa_application *app = p->application;
    for (size_t i = 0; i < app->mode_count; ++i) {
        qa_mode_id id = app->mode_ids[i];
        qa_mode_q3_settings saved;
        bool present;
        if (!native_q3_mode(p, id)) continue;
        if (!qa_modes_q3_settings_read(app->modes, id, &saved, &present, e)) return false;
        if (!present || saved.do_warmup != current.do_warmup || saved.warmup_seconds != current.warmup_seconds ||
            saved.time_limit_minutes != current.time_limit_minutes || saved.frag_limit != current.frag_limit ||
            saved.capture_limit != current.capture_limit ||
            saved.warmup_modification_count != current.warmup_modification_count)
            return application_fail(e, QA_ERROR_ARGUMENT, "Q3 settings differ from their restored source cvars");
    }
    return true;
}

bool application_native_q3_settings_source_remap(void *opaque, qa_error *error) {
    application_provider *p = opaque;
    const char *red, *blue;
    int32_t time;
    return application_native_q3_settings_string(p, "g_redteam", &red, error) &&
        application_native_q3_settings_string(p, "g_blueteam", &blue, error) &&
        qa_q3_source_clock(p->state.q3, &time, error) &&
        qa_q3_shader_remap_teams(p->state.q3, red, blue, time, error);
}

static bool q3_source_rules(application_provider *p, qa_error *error) {
    qa_q3_rules rules;
    int32_t friendly_fire, blood;
    if (!qa_q3_rules_read(p->state.q3, &rules, error) ||
        !application_native_q3_settings_integer(p, "g_gametype", &rules.game_type, error) ||
        !application_native_q3_settings_integer(p, "dmflags", &rules.dmflags, error) ||
        !application_native_q3_settings_integer(p, "g_friendlyFire", &friendly_fire, error) ||
        !application_native_q3_settings_integer(p, "com_blood", &blood, error) ||
        !application_native_q3_settings_integer(p, "g_forcerespawn", &rules.force_respawn_seconds, error) ||
        !application_native_q3_settings_number(p, "g_gravity", &rules.gravity, error) ||
        !application_native_q3_settings_number(p, "g_quadfactor", &rules.quad_factor, error) ||
        !application_native_q3_settings_number(p, "g_knockback", &rules.knockback, error) ||
        !application_native_q3_settings_number(p, "g_weaponrespawn", &rules.weapon_respawn_seconds, error) ||
        !application_native_q3_settings_number(p, "g_weaponTeamRespawn", &rules.team_weapon_respawn_seconds, error))
        return false;
    if (p->product && !strcmp(p->product->campaign, "missionpack") &&
        !application_native_q3_settings_integer(p, "g_proxMineTimeout", &rules.proximity_timeout_ms, error))
        return false;
    rules.friendly_fire = friendly_fire != 0;
    rules.blood = blood != 0;
    return qa_q3_set_source_rules(p->state.q3, &rules, error);
}

bool application_native_q3_settings_source_init(application_provider *p, qa_error *error) {
    return q3_source_rules(p, error) &&
        (application_world_provider(p->application, QA_ROLE_ENTITIES, "") != p ||
         (application_native_q3_votes_reset(p, error) &&
          application_native_q3_team_status_initialize(p, error)));
}

bool application_native_q3_source_team_items(void *opaque, qa_error *error) {
    return application_native_q3_objectives_initialize(opaque, error);
}

bool application_native_q3_settings_source_modes(application_provider *p, qa_error *error) {
    qa_mode_q3_settings settings;
    qa_application *app = p->application;
    if (!q3_settings_values(p, &settings, error)) return false;
    for (size_t i = 0; i < app->mode_count; ++i) {
        if (!native_q3_mode(p, app->mode_ids[i])) continue;
        qa_mode_q3_settings current;
        bool present;
        if (!qa_modes_q3_settings_read(app->modes, app->mode_ids[i], &current, &present, error) ||
            (present && !qa_modes_q3_settings_update(app->modes, app->mode_ids[i], &settings, error))) return false;
    }
    return true;
}

static bool q3_source_info(application_provider *p, qa_error *error) {
    if (!application_native_q3_console_borrow(p, error)) return false;
    qa_cvars *cvars = application_native_q3_console_registry(p);
    qa_buffer system = {0}, server = {0};
    bool okay = qa_cvars_info(cvars, QA_CVAR_SYSTEMINFO, 8192, &system, error) &&
        qa_cvars_info(cvars, QA_CVAR_SERVERINFO, 1024, &server, error) &&
        qa_q3_configstring_write(p->state.q3, 1, (const char *)system.data, error) &&
        qa_q3_configstring_write(p->state.q3, 0, (const char *)server.data, error);
    qa_buffer_free(&system);
    qa_buffer_free(&server);
    application_native_q3_console_release(p);
    return okay;
}

bool application_native_q3_settings_source_loaded(application_provider *p, qa_error *error) {
    qa_application *app = p->application;
    const char *map = qa_strings_cstr(qa_session_strings(app->session), app->current_map);
    if (!map || !application_native_q3_settings_force_set(p, "mapname", map, error) ||
        !application_native_q3_settings_force_set(p, "sv_mapname", map, error)) return false;
    return application_native_q3_settings_remap_teams(p, error) &&
        application_native_q3_settings_update(p, error) && q3_source_rules(p, error) &&
        application_native_q3_settings_source_modes(p, error) && q3_source_info(p, error);
}

bool application_native_q3_settings_source_frame(void *opaque,
    const qa_source_frame *frame, qa_error *error) {
    application_provider *p = opaque;
    qa_application *app = p ? p->application : NULL;
    if (!app || !frame || frame->provider != p->owner || frame->phase != QA_FRAME_ENTRY ||
        app->operation != APPLICATION_ADVANCING || !p->constructed || !p->attached ||
        p->close_pending || app->destroy_requested || !app->modes ||
        !application_native_q3_console_settings_bound(p))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 settings update has no genuine GAME frame");
    if (!application_native_q3_settings_update(p, error) || !q3_source_rules(p, error)) return false;
    return application_native_q3_settings_source_modes(p, error);
}

static bool q3_list_entities(application_provider *p, qa_error *error) {
    int32_t list;
    if (!application_native_q3_settings_integer(p, "g_listEntity", &list, error)) return false;
    if (!list) return true;
    for (uint32_t slot = 0; slot < 1024; ++slot) {
        const char *name;
        if (!qa_q3_source_classname_read(p->state.q3, slot, &name, error)) return false;
        if (!name) name = "(null)";
        int length = snprintf(NULL, 0, "%4u: %s\n", slot, name);
        if (length < 0) return application_fail(error, QA_ERROR_FORMAT, "Q3 entity listing cannot format its source row");
        char *text = malloc((size_t)length + 1);
        if (!text) return application_fail(error, QA_ERROR_MEMORY, "Q3 entity listing cannot retain its source row");
        snprintf(text, (size_t)length + 1, "%4u: %s\n", slot, name);
        bool okay = application_native_q3_console_print(p, text, error);
        free(text);
        if (!okay) return false;
    }
    return application_native_q3_settings_force_set(p, "g_listEntity", "0", error);
}

bool application_native_q3_source_end_frame(void *opaque,
    const qa_source_frame *frame, qa_error *error) {
    application_provider *p = opaque;
    qa_application *app = p ? p->application : NULL;
    qa_source_frame active;
    int32_t time;
    if (!app || !frame || p->kind != APPLICATION_PROVIDER_Q3 ||
        frame->provider != p->owner || frame->kind != QA_CLOCK_Q3 ||
        frame->phase != QA_CLIENT_END_FRAME || app->operation != APPLICATION_ADVANCING ||
        !p->constructed || !p->attached || p->close_pending || app->destroy_requested ||
        !application_native_q3_console_settings_bound(p) || !app->modes ||
        !qa_session_active_frame(app->session, p->owner, &active) ||
        active.phase != frame->phase || active.number != frame->number ||
        active.start_ns != frame->start_ns || active.time_ns != frame->time_ns ||
        active.elapsed_ns != frame->elapsed_ns ||
        !qa_q3_source_clock(p->state.q3, &time, error) ||
        (uint32_t)time != (uint32_t)(frame->time_ns / UINT64_C(1000000)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source tail has no genuine END frame");
    for (size_t i = 0; i < app->mode_count; ++i) {
        qa_mode_id mode = app->mode_ids[i];
        if (!native_q3_mode(p, mode)) continue;
        qa_actor_owner source_owner;
        bool source_owned = application_native_q3_mode_source(app, mode, &source_owner) &&
            source_owner == p->owner;
        bool okay = source_owned
            ? qa_modes_q3_source_frame(app->modes, mode, frame->time_ns,
                frame->elapsed_ns, error)
            : qa_modes_frame(app->modes, mode, frame->time_ns, frame->elapsed_ns, error);
        if (!okay) return false;
    }
    if (application_world_provider(app, QA_ROLE_ENTITIES, "") == p &&
        (!application_native_q3_match_frame(p, frame, error) ||
         !application_native_q3_team_status(p, error) ||
         !application_native_q3_votes_frame(p, error))) return false;
    return application_native_q3_settings_check_cvars(p, error) &&
        q3_list_entities(p, error) && q3_source_info(p, error);
}

bool application_native_mode_emit(void *opaque, qa_mode_id mode,
                                    const qa_builtin_event *event, qa_error *error) {
    qa_application *app = opaque;
    application_provider *source = application_mode_provider(app, mode);
    if (!source || !event)
        return application_fail(error, QA_ERROR_NOT_FOUND, "mode event has no live source content");
    qa_builtin_event projected = *event;
    projected.provider = source->owner;
    return application_emit(app, &projected, error);
}

bool application_native_mode_rogue_runes_claim(void *opaque, qa_mode_id mode,
    bool *newly_claimed, qa_error *error) {
    qa_application *app = opaque;
    application_provider *p = application_mode_provider(app, mode);
    qa_mode_view view;
    if (!p || p->kind != APPLICATION_PROVIDER_Q1 || !qa_modes_read(app->modes, mode, &view, error) ||
        view.rules.source != QA_MODE_ROGUE)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Rogue rune startup needs its actual native Q1 source");
    return qa_q1_game_rogue_runes_claim(p->state.q1, newly_claimed, error);
}
bool application_native_mode_rogue_runes_read(void *opaque, qa_mode_id mode,
    qa_actor_id *world, bool *started) {
    qa_application *app = opaque;
    application_provider *p = application_mode_provider(app, mode);
    qa_mode_view view;
    return p && p->kind == APPLICATION_PROVIDER_Q1 && qa_modes_read(app->modes, mode, &view, NULL) &&
        view.rules.source == QA_MODE_ROGUE && qa_q1_game_rogue_runes_read(p->state.q1, world, started);
}

bool application_native_mode_map_allowed(void *opaque, qa_mode_id mode, qa_string_id map) {
    qa_application *app = opaque;
    application_provider *source = application_mode_provider(app, mode);
    char *path = NULL;
    bool allowed = application_source_map_path(source,
        app && app->session ? qa_strings_cstr(qa_session_strings(app->session), map) : NULL,
        &path, NULL);
    free(path);
    return allowed;
}

bool application_native_mode_select_weapon(void *opaque, qa_actor_id actor,
                                             qa_item_id item, qa_error *error) {
    qa_application *app = opaque;
    application_provider *p = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (!item || !p || !p->constructed || p->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "weapon selection has no live arsenal");
    if (p->kind == APPLICATION_PROVIDER_Q1)
        for (int weapon = 0; weapon < QA_Q1_WEAPON_COUNT; ++weapon)
            if (qa_q1_weapon_item(p->state.q1, (qa_q1_weapon)weapon) == item) {
                qa_q1_game_operation operation = {0};
                if (!qa_q1_game_operation_begin(p->state.q1, &operation, error)) return false;
                return q1_finish(&operation,
                    qa_q1_player_select(p->state.q1, actor, (qa_q1_weapon)weapon, error), error);
            }
    if (p->kind == APPLICATION_PROVIDER_Q2)
        for (int weapon = 1; weapon < QA_Q2_WEAPON_COUNT; ++weapon) {
            const qa_q2_weapon_definition *d = qa_q2_weapon_definition_at(p->state.q2, (qa_q2_weapon)weapon);
            const char *identity = qa_strings_cstr(qa_session_strings(app->session), item);
            if (!d || !identity || strcmp(d->item, identity)) continue;
            mode_q2_select call = {.game = p->state.q2, .weapon = (qa_q2_weapon)weapon};
            return qa_q2_run_actor(p->state.q2, actor, q2_select, &call, error);
        }
    if (p->kind == APPLICATION_PROVIDER_Q3)
        for (int weapon = 1; weapon < QA_Q3_WEAPON_COUNT; ++weapon)
            if (qa_q3_weapon_item(p->state.q3, (qa_q3_weapon)weapon, false) == item)
                return qa_q3_player_request_weapon(p->state.q3, actor, (qa_q3_weapon)weapon, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED, "item has no selected arsenal weapon action");
}

bool application_native_mode_use_item(void *opaque, qa_actor_id actor,
                                        qa_item_id item, qa_error *error) {
    qa_application *app = opaque;
    return qa_inventory_item_action(app->inventory, actor, item, QA_ITEM_USE, error);
}

bool application_native_mode_select_grapple(void *opaque, qa_actor_id actor, qa_error *error) {
    qa_application *app = opaque;
    if (!app->equipment)
        return application_fail(error, QA_ERROR_NOT_FOUND, "grapple selection has no equipment coordinator");
    return qa_equipment_select_grapple(app->equipment, actor, true, error);
}

bool application_native_mode_character_frame(void *opaque, qa_actor_id actor, int32_t *frame) {
    qa_application *app = opaque;
    application_provider *p = application_provider_for(app, actor, QA_ROLE_CHARACTER, "");
    if (!frame || !p || !p->constructed || p->close_pending) return false;
    if (p->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_character_view view;
        if (!qa_q1_character_read(p->state.q1, actor, &view)) return false;
        *frame = view.frame; return true;
    }
    if (p->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_visual view;
        if (!qa_q2_presentation_read(p->state.q2, actor, &view)) return false;
        *frame = view.frame; return true;
    }
    return false;
}

bool application_native_mode_body_armor(void *opaque, qa_mode_id mode,
                                          qa_actor_id actor, qa_error *error) {
    qa_application *app = opaque;
    application_provider *p = application_mode_provider(app, mode);
    if (!p || p->kind != APPLICATION_PROVIDER_Q2)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Tag body armor requires its native Q2 source");
    return qa_q2_run_actor(p->state.q2, actor, q2_armor, p->state.q2, error);
}

static bool quad_sound(qa_application *app, application_provider *p,
                         qa_actor_id actor, qa_error *error) {
    qa_clock_state clock;
    qa_body_state body;
    qa_string_id resource;
    if (!qa_session_clock(app->session, p->owner, &clock))
        return application_fail(error, QA_ERROR_ARGUMENT, "quad activation has no native source clock");
    if (!qa_world_body_read(app->world, actor, &body, error) ||
        !qa_strings_intern_cstr(qa_session_strings(app->session), "items/damage.wav", &resource, error))
        return false;
    if (p->close_pending || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "quad actor retired during activation");
    return application_emit(app, &(qa_builtin_event){.kind = QA_BUILTIN_SOUND,
        .family = QA_GAME_Q2, .provider = p->owner, .actor = actor,
        .time_ns = clock.frame.time_ns, .resource = resource, .origin = body.origin,
        .channel = 3, .volume = 1, .attenuation = 1}, error);
}
bool application_native_mode_quad(void *opaque, qa_mode_id mode, qa_actor_id actor,
                                    qa_game_family family, uint64_t duration_ns, qa_error *error) {
    qa_application *app = opaque;
    application_provider *p = application_provider_for(app, actor, QA_ROLE_EFFECTS, "");
    if (family != QA_GAME_Q1 && family != QA_GAME_Q2)
        return application_fail(error, QA_ERROR_ARGUMENT, "quad grant has no source timer policy");
    bool stack = family == QA_GAME_Q2;
    application_provider *source = stack ? application_mode_provider(app, mode) : NULL;
    if (stack && (!source || source->kind != APPLICATION_PROVIDER_Q2))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Tag quad has no native Q2 mode source");
    if (!p || !p->constructed || p->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "quad grant has no selected effects owner");
    if (p->kind == APPLICATION_PROVIDER_Q1) {
        qa_clock_state clock;
        qa_q1_game_operation operation = {0};
        if (!qa_session_clock(app->session, p->owner, &clock))
            return application_fail(error, QA_ERROR_ARGUMENT, "quad grant has no native source clock");
        if (!qa_q1_game_operation_begin(p->state.q1, &operation, error)) return false;
        double base = (double)clock.frame.time_ns / 1e9;
        double old = qa_q1_game_power_expires(p->state.q1, actor, QA_Q1_QUAD);
        if (stack && old > base) base = old;
        double expires = stack || duration_ns ? base + (double)duration_ns / 1e9 : 0;
        bool okay = qa_q1_player_power(p->state.q1, actor, QA_Q1_QUAD, expires, error);
        if (okay && stack) {
            if (!qa_q1_game_operation_live(&operation))
                okay = application_fail(error, QA_ERROR_ARGUMENT, "quad source retired during activation");
            else okay = quad_sound(app, source, actor, error);
        }
        return q1_finish(&operation, okay, error);
    }
    if (p->kind == APPLICATION_PROVIDER_Q2) {
        bool okay = stack ? qa_q2_player_quad_stack(p->state.q2, actor, duration_ns, error)
                          : qa_q2_player_quad(p->state.q2, actor, duration_ns, error);
        return okay && (!stack || quad_sound(app, source, actor, error));
    }
    if (p->kind == APPLICATION_PROVIDER_Q3) {
        bool okay = stack ? qa_q3_player_quad_stack(p->state.q3, actor, duration_ns, error)
                          : qa_q3_player_quad(p->state.q3, actor, duration_ns, error);
        return okay && (!stack || quad_sound(app, source, actor, error));
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED, "selected effects owner has no timed quad adapter");
}

static bool current_weapon(application_provider *p, qa_actor_id actor,
                              qa_item_id *weapon, qa_item_id *ammo, qa_error *error) {
    *weapon = *ammo = 0;
    if (p->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view view;
        qa_q1_weapon_view selected;
        bool found;
        if (!qa_q1_player_read(p->state.q1, actor, &view))
            return application_fail(error, QA_ERROR_NOT_FOUND, "selected Q1 arsenal has no player");
        qa_q1_game_operation operation = {0};
        if (!qa_q1_game_operation_begin(p->state.q1, &operation, error)) return false;
        bool okay = qa_q1_player_weapon_read(p->state.q1, actor, view.weapon, &selected, &found, error);
        if (okay && found) { *weapon = selected.item; *ammo = selected.ammo; }
        return q1_finish(&operation, okay, error);
    }
    if (p->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state view;
        if (!qa_q2_weapon_read(p->state.q2, actor, &view, error)) return false;
        const qa_q2_weapon_definition *d = qa_q2_weapon_definition_at(p->state.q2, view.weapon);
        if (d) {
            qa_strings *strings = qa_session_strings(p->application->session);
            *weapon = qa_strings_find(strings, (qa_bytes){(const uint8_t *)d->item, strlen(d->item)});
            if (d->ammo) *ammo = qa_strings_find(strings, (qa_bytes){(const uint8_t *)d->ammo, strlen(d->ammo)});
        }
        return true;
    }
    if (p->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state view;
        if (!qa_q3_player_read(p->state.q3, actor, &view))
            return application_fail(error, QA_ERROR_NOT_FOUND, "selected Q3 arsenal has no player");
        *weapon = qa_q3_weapon_item(p->state.q3, view.weapon, false);
        *ammo = qa_q3_weapon_item(p->state.q3, view.weapon, true);
        return true;
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED, "selected arsenal has no current weapon query");
}

bool application_native_mode_team_equipment(void *opaque, qa_actor_id actor,
                                              qa_item_id *weapon, uint64_t *powerups, qa_error *error) {
    qa_application *app = opaque;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    application_provider *effects = application_provider_for(app, actor, QA_ROLE_EFFECTS, "");
    qa_item_id ammo;
    if (!weapon || !powerups || !arsenal || !effects || !arsenal->constructed ||
        !effects->constructed || arsenal->close_pending || effects->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "team equipment requires selected live owners");
    if (!current_weapon(arsenal, actor, weapon, &ammo, error)) return false;
    *powerups = 0;
    if (effects->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_entity_view view;
        if (!qa_q3_entity_read(effects->state.q3, actor, &view, error)) return false;
        *powerups = view.powerups; return true;
    }
    qa_clock_state clock;
    if (!qa_session_clock(app->session, effects->owner, &clock))
        return application_fail(error, QA_ERROR_ARGUMENT, "team power query has no source clock");
    if (effects->kind == APPLICATION_PROVIDER_Q1) {
        static const struct { qa_q1_power source; qa_q3_powerup projected; } powers[] = {
            {QA_Q1_QUAD, QA_Q3_P_QUAD}, {QA_Q1_INVULNERABILITY, QA_Q3_P_INVULNERABILITY},
            {QA_Q1_INVISIBILITY, QA_Q3_P_INVIS}, {QA_Q1_SUIT, QA_Q3_P_BATTLESUIT}};
        double now = (double)clock.frame.time_ns / 1e9;
        for (size_t i = 0; i < sizeof(powers) / sizeof(*powers); ++i)
            if (qa_q1_game_power_expires(effects->state.q1, actor, powers[i].source) > now)
                *powerups |= UINT64_C(1) << powers[i].projected;
        return true;
    }
    if (effects->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_powerups powers;
        if (!qa_q2_powerups_read(effects->state.q2, actor, &powers, error)) return false;
        if (powers.quad_until_ns > clock.frame.time_ns) *powerups |= UINT64_C(1) << QA_Q3_P_QUAD;
        if (powers.invulnerability_until_ns > clock.frame.time_ns) *powerups |= UINT64_C(1) << QA_Q3_P_INVULNERABILITY;
        if (powers.invisibility_until_ns > clock.frame.time_ns) *powerups |= UINT64_C(1) << QA_Q3_P_INVIS;
        if (powers.enviro_until_ns > clock.frame.time_ns) *powerups |= UINT64_C(1) << QA_Q3_P_BATTLESUIT;
        if (powers.double_until_ns > clock.frame.time_ns) *powerups |= UINT64_C(1) << QA_Q3_P_DOUBLER;
        return true;
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED, "selected effects owner has no team power query");
}

bool application_native_mode_drop_arsenal(void *opaque, qa_actor_id actor,
                                           bool weapon, qa_error *error) {
    qa_application *app = opaque;
    application_provider *p = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (!p || !p->constructed || p->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND, "drop has no selected live arsenal");
    qa_q1_game_operation operation = {0};
    if (p->kind == APPLICATION_PROVIDER_Q1 &&
        !qa_q1_game_operation_begin(p->state.q1, &operation, error)) return false;
    qa_q1_drop_input input = {0};
    bool okay = current_weapon(p, actor, &input.selected_weapon, &input.selected_ammo, error);
    if (okay && p->kind == APPLICATION_PROVIDER_Q1) {
        const application_control_record *control = actor.slot < app->control_capacity
            ? &app->controls[actor.slot] : NULL;
        if (control && control->active && qa_actor_id_equal(control->actor, actor))
            input.view_angles = control->view_angles;
        else {
            qa_body_state body;
            okay = qa_world_body_read(app->world, actor, &body, error);
            if (okay) input.view_angles = body.angles;
        }
        qa_actor_id dropped;
        if (okay) okay = weapon
            ? qa_q1_ctf_toss_weapon(p->state.q1, actor, &input, &dropped, error)
            : qa_q1_ctf_toss_ammo(p->state.q1, actor, &input, &dropped, error);
    } else if (okay) {
        qa_item_id item = weapon ? input.selected_weapon : input.selected_ammo;
        okay = !item || qa_inventory_item_action(app->inventory, actor, item, QA_ITEM_DROP, error);
    }
    return operation.game ? q1_finish(&operation, okay, error) : okay;
}

bool application_native_mode_visible(void *opaque, qa_actor_id from, qa_actor_id to, bool pvs) {
    qa_application *app = opaque;
    qa_body_state a, b;
    if (!qa_world_body_read(app->world, from, &a, NULL) ||
        !qa_world_body_read(app->world, to, &b, NULL)) return false;
    if (pvs) {
        qa_collision_geometry *geometry = qa_world_geometry(app->world);
        qa_collision_leaf first, second;
        bool visible, connected;
        return geometry && qa_collision_point_leaf(geometry, a.origin, &first, NULL) &&
            qa_collision_point_leaf(geometry, b.origin, &second, NULL) &&
            first.cluster >= INT32_MIN && first.cluster <= INT32_MAX &&
            second.cluster >= INT32_MIN && second.cluster <= INT32_MAX &&
            first.area >= INT32_MIN && first.area <= INT32_MAX &&
            second.area >= INT32_MIN && second.area <= INT32_MAX &&
            qa_collision_cluster_visible(geometry, (int32_t)first.cluster, (int32_t)second.cluster,
                                          false, &visible, NULL) && visible &&
            qa_collision_areas_connected(geometry, (int32_t)first.area, (int32_t)second.area,
                                          &connected, NULL) && connected;
    }
    qa_physics_services physics = application_physics_services(app);
    qa_physics_properties properties;
    if (physics.read && physics.read(physics.context, to, &properties) &&
        properties.motion == QA_PHYSICS_PUSH) return false;
    qa_builtin_services services = application_builtin_services(app, app->world, app->physics);
    qa_builtin_actor_traits traits = {0};
    if (services.actor_traits) services.actor_traits(services.context, from, &traits);
    a.origin.z += traits.view_height;
    qa_vec3 points[8];
    points[0] = qa_vec_add(b.origin, b.bounds.mins);
    points[1] = points[2] = points[3] = points[0];
    points[1].x -= b.bounds.mins.x;
    points[2].y -= b.bounds.mins.y;
    points[3].x -= b.bounds.mins.x; points[3].y -= b.bounds.mins.y;
    points[4] = qa_vec_add(b.origin, b.bounds.maxs);
    points[5] = points[4]; points[5].x -= b.bounds.maxs.x;
    points[6] = points[7] = points[0];
    points[6].y -= b.bounds.maxs.y;
    points[7].x -= b.bounds.maxs.x; points[7].y -= b.bounds.maxs.y;
    for (size_t i = 0; i < 8; ++i) {
        qa_trace_query query = {.start = a.origin, .end = points[i], .pass_actor = from,
            .shape = {.kind = QA_SHAPE_POINT},
            .policy = {.family = QA_COLLISION_Q2, .contents_mask = 3, .q1_hull = -1}};
        qa_trace_result trace;
        if (qa_world_trace(app->world, &query, &trace, NULL) && trace.fraction == 1) return true;
    }
    return false;
}
