#include "internal.h"
#include "native_maps.h"
#include "match_intents.h"
#include "native_q3_clients.h"
#include "native_q3_objectives.h"
#include "map_travel_private.h"
#include "equipment_runtime.h"
#include "equipment_requests.h"
#include "guest_input_private.h"
#include "native_q1_composition.h"
#include "native_q1_composition_player.h"
#include "native_q1_composition_death.h"
#include "guest_q3_private.h"
#include "guest_q3_weapons_services.h"
#include "guest_qc_items.h"
#include "guest_qc_item_weapons.h"
#include "qa/application_qc_presentation.h"
#include "qa/game_q2_bots.h"
#include "qa/game_q2_combat.h"

#include <stdlib.h>
#include <string.h>

static qa_game_family mode_family(qa_application *application,
                                  qa_mode_id mode)
{
    qa_mode_view view;
    if (application->modes != NULL &&
        qa_modes_read(application->modes, mode, &view, NULL)) {
        if (view.rules.source <= QA_MODE_Q1_HORDE)
            return QA_GAME_Q1;
        if (view.rules.source < QA_MODE_Q3)
            return QA_GAME_Q2;
    }
    return QA_GAME_Q3;
}

static bool mode_event(void *opaque, const qa_mode_event *event,
                       qa_error *error)
{
    qa_application *application = opaque;
    application_provider *source = application_mode_provider(application, event->mode);
    if (source == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "mode event lost its selected source content owner");
    return application_emit(
        application,
        &(qa_builtin_event){.kind = event->kind == QA_MODE_MESSAGE
                                        ? QA_BUILTIN_MESSAGE
                                        : QA_BUILTIN_EFFECT,
                            .family = mode_family(application, event->mode),
                            .provider = source->owner,
                            .actor = event->actor,
                            .other = event->other,
                            .time_ns = event->time_ns,
                            .text = event->text,
                            .value = (float)event->value,
                            .code = (int32_t)event->kind,
                            .channel = (int32_t)event->team,
                            .frame = event->detail},
        error);
}

static bool mode_intent(void *opaque, const qa_match_intent *intent,
                        qa_error *error)
{
    qa_application *application = opaque;
    if (intent->kind == QA_MATCH_NEXT_MAP || intent->kind == QA_MATCH_SELECTED_MAP ||
        intent->kind == QA_MATCH_RESTART_MAP ||
        intent->kind == QA_MATCH_WARMUP || intent->kind == QA_MATCH_TIME_LIMIT ||
        intent->kind == QA_MATCH_FRAG_LIMIT || intent->kind == QA_MATCH_GAME_TYPE) {
        if (application->match_intents == NULL)
            application->match_intents = application_match_intents_create(error);
        return application->match_intents != NULL &&
               application_match_intents_enqueue(application->match_intents,
                                                   application, intent, error);
    }
    return application_emit(
        application,
        &(qa_builtin_event){.kind = QA_BUILTIN_TARGET,
                            .family = mode_family(application, intent->mode),
                            .actor = intent->actor,
                            .time_ns = qa_session_elapsed(application->session),
                            .text = intent->map,
                            .value = intent->value,
                            .code = (int32_t)intent->kind,
                            .channel = (int32_t)intent->team,
                            .frame = (int32_t)intent->game_type},
        error);
}

static bool mode_damage_prepare(void *opaque, qa_mode_id mode,
                                qa_damage_request *request, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *source = app ? application_mode_provider(app, mode) : NULL;
    qa_clock_state clock;
    if (!request || !app || !app->session || app->destroy_requested || app->finalizing ||
        !source || source->application != app || !source->constructed || !source->attached ||
        source->close_pending || !source->launch ||
        !qa_session_clock(app->session, source->owner, &clock) ||
        clock.frame.provider != source->owner ||
        clock.frame.kind != source->launch->selection.clock.kind)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Mode damage lost its actual content provider or Source clock");
    if (request->attack.weapon)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Mode damage requires its actual weaponless Source operation");
    if (request->attack.cause.kind == QA_CAUSE_Q2) {
        qa_q2_combat_rules rules;
        uint64_t now, started;
        bool intermission;
        if (source->kind != APPLICATION_PROVIDER_Q2 || !source->state.q2)
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "Mode damage has no actual Q2 cause profile producer");
        if (!qa_q2_combat_rules_read(source->state.q2, &rules) ||
            rules.owner != source->owner ||
            clock.frame.kind != (rules.edition == QA_Q2_CLASSIC
                ? QA_CLOCK_Q2_CLASSIC : QA_CLOCK_Q2_RERELEASE))
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Mode damage lost its actual Q2 GAME rules");
        if (!qa_q2_bot_clock_read(source->state.q2, &now, &intermission, &started, error))
            return false;
        if (now != clock.frame.time_ns)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Mode damage lost its actual Q2 GAME clock");
        request->attack.cause = qa_q2_damage_cause(rules.edition, rules.product,
            request->attack.cause.source.q2.means_of_death,
            request->attack.cause.source.q2.flags);
    }
    request->attack.weapon_provider = source->owner;
    request->attack.time_ns = clock.frame.time_ns;
    return true;
}

static qa_actor_owner mode_combat_provider(void *opaque, qa_actor_id actor,
                                           qa_game_family family)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_COMBAT, "");
    if (provider != NULL)
        return provider->owner;
    for (size_t index = 0; index < application->routing_provider_count; ++index) {
        provider = application->routing_providers[index];
        if ((family == QA_GAME_Q1 &&
             (provider->kind == APPLICATION_PROVIDER_Q1 ||
              provider->kind == APPLICATION_PROVIDER_QC)) ||
            (family == QA_GAME_Q2 &&
             provider->kind == APPLICATION_PROVIDER_Q2) ||
            (family == QA_GAME_Q3 &&
             (provider->kind == APPLICATION_PROVIDER_Q3 ||
              provider->kind == APPLICATION_PROVIDER_QVM ||
              provider->kind == APPLICATION_PROVIDER_NATIVE)))
            return provider->owner;
    }
    return 0;
}

static bool mode_player_view(void *opaque, qa_actor_id actor, qa_vec3 *angles)
{
    qa_application *application = opaque;
    if (angles != NULL && actor.slot < application->control_capacity) {
        const application_control_record *control =
            &application->controls[actor.slot];
        if (control->active && qa_actor_id_equal(control->actor, actor)) {
            *angles = control->view_angles;
            return true;
        }
    }
    qa_body_state body;
    if (angles == NULL || application->world == NULL ||
        !qa_world_body_read(application->world, actor, &body, NULL))
        return false;
    *angles = body.angles;
    return true;
}

static bool mode_grapple_pulling(void *opaque, qa_actor_id actor)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_EQUIPMENT, "");
    return provider != NULL && provider->kind == APPLICATION_PROVIDER_Q1 &&
           qa_q1_grapple_pulling(provider->state.q1, actor);
}

static bool restore_player_binding(void *opaque, qa_mode_id mode, qa_actor_id actor,
    qa_actor_owner owner, qa_match_binding *out, qa_error *error)
{
    qa_application *application = opaque;
    if (!application || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Restored match player requires its actual application owner");
    application_provider *provider = application_world_provider(application, QA_ROLE_ENTITIES, "");
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!provider || provider->application != application ||
        !provider->constructed || provider->close_pending || !engine || !engine->game ||
        !engine->game->weapon_services)
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "Restored match player has no selected original Q3 Source binding");
    return application_q3_weapons_services_match_binding(engine->game->weapon_services,
        mode, actor, owner, out, error);
}

static qa_modes_hooks mode_hooks(qa_application *application)
{
    return (qa_modes_hooks){.context = application,
                            .q1_composition_expected = application_native_q1_composition_expected,
                            .q1_composition_current = application_native_q1_composition_current,
                            .q1_composition_player_current = application_native_q1_composition_player_current,
                            .q1_rogue_state = application_native_q1_rogue_state,
                            .q1_rogue_state_current = application_native_q1_rogue_state_current,
                            .q1_rogue_number_read = application_native_q1_rogue_number_read,
                            .q1_rogue_number_write = application_native_q1_rogue_number_write,
                            .event = mode_event,
                            .emit = application_native_mode_emit,
                            .q3_clock = application_native_mode_q3_clock,
                            .q3_client_slot = application_native_q3_mode_client_slot,
                            .q3_native_source = application_native_q3_mode_source,
                            .q3_source_score_bound = application_native_q3_source_score_bound,
                            .q3_source_match_exit = application_native_mode_q3_source_match_exit,
                            .q3_team_status_bound = application_native_mode_q3_team_status_bound,
                            .q3_source_object = application_native_q3_objective_bound,
                            .q3_source_object_view = application_native_q3_objective_view,
                            .q3_rank_client = application_native_q3_mode_rank_client,
                            .q3_rank_counts = application_native_q3_mode_rank_counts,
                            .q3_choose_team = application_native_q3_mode_choose_team,
                            .q3_vote_calls = application_native_q3_mode_vote_calls,
                            .q3_team_request = application_native_q3_mode_team_request,
                            .q3_stop_following = application_native_q3_mode_stop_following,
                            .q3_intermission_client = application_native_q3_mode_intermission_client,
                            .q3_intermission_ready_publish = application_native_q3_mode_ready_publish,
                            .q3_warmup_restart = application_native_mode_q3_warmup_restart,
                            .map_allowed = application_native_mode_map_allowed,
                            .next_map_allowed = application_native_mode_next_map_allowed,
                            .selected_map_command = application_native_mode_selected_map_command,
                            .rogue_runes_claim = application_native_mode_rogue_runes_claim,
                            .rogue_runes_read = application_native_mode_rogue_runes_read,
                            .intent = mode_intent,
                            .respawn = application_native_mode_respawn,
                            .q1_source_score = application_native_q1_mode_score,
                            .q1_source_death_bound = application_native_q1_source_death_bound,
                            .q1_source_set_score = application_native_q1_mode_set_score,
                            .q1_source_add_score = application_native_q1_mode_add_score,
                            .q1_ctf_suicide_notice = application_native_q1_ctf_suicide_notice,
                            .release_grapple = application_native_mode_release_grapple,
                            .damage_prepare = mode_damage_prepare,
                            .combat_provider = mode_combat_provider,
                            .force_death = application_force_death,
                            .visible = application_native_mode_visible,
                            .character_frame = application_native_mode_character_frame,
                            .select_weapon = application_native_mode_select_weapon,
                            .use_item = application_native_mode_use_item,
                            .give_body_armor = application_native_mode_body_armor,
                            .give_quad = application_native_mode_quad,
                            .team_equipment = application_native_mode_team_equipment,
                            .select_grapple = application_native_mode_select_grapple,
                            .drop_arsenal = application_native_mode_drop_arsenal,
                            .spawn_monster = application_native_horde_spawn_monster,
                            .spawn_loot = application_native_horde_spawn_loot,
                            .grant_loot = application_native_horde_grant_loot,
                            .horde_head = application_native_horde_head,
                            .loot_alpha = application_native_horde_alpha,
                            .source_random = application_native_horde_random,
                            .horde_point = application_native_horde_point,
                            .horde_manager = application_native_horde_manager,
                            .campaign_restart = application_native_horde_restart,
                            .grapple_pulling = mode_grapple_pulling,
                            .player_view = mode_player_view,
                            .restore_player_binding = restore_player_binding};
}

static application_provider *named(application_publication *publication,
                                   const char *name)
{
    if (name == NULL || name[0] == '\0')
        return NULL;
    for (size_t index = 0; index < publication->next_count; ++index) {
        application_provider *provider = publication->next[index];
        if (provider != NULL && provider->launch != NULL &&
            strcmp(provider->launch->selection.instance, name) == 0)
            return provider;
    }
    return NULL;
}

static application_provider *arsenal_provider(qa_application *application,
                                              qa_actor_id actor)
{
    return application_provider_for(application, actor, QA_ROLE_ARSENAL, "");
}

static bool equipment_primary_owner(void *opaque,qa_actor_id actor,qa_actor_owner *out,qa_error *error)
{
    qa_application *app=opaque;
    application_provider *provider=arsenal_provider(app,actor);
    if(!out||!provider||!provider->constructed||!provider->attached||provider->close_pending||
        !qa_actors_get(qa_session_actors(app->session),actor))
        return application_fail(error,QA_ERROR_NOT_FOUND,"Equipment primary binding lost its genuine selected source");
    *out=provider->owner; return true;
}
static bool equipment_holster(void *opaque, qa_actor_id actor,
                              qa_error *error)
{
    application_provider *provider = arsenal_provider(opaque, actor);
    if (provider == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "actor has no selected arsenal owner");
    if (provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_primary_weapon_holster(provider->state.q1, actor, error);
    if (provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_weapon_holster(provider->state.q2, actor, error);
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        return qa_q3_set_weapon_slot(provider->state.q3, actor, true, error);
    if (provider->kind == APPLICATION_PROVIDER_QVM)
        return application_arsenal_guest_equipment_handoff_ready(provider, actor, error);
    if(provider->kind==APPLICATION_PROVIDER_QC)
        return qa_actors_get(qa_session_actors(provider->application->session),actor)!=NULL;
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "selected arsenal has no equipment holster adapter");
}

static bool equipment_holstered(void *opaque, qa_actor_id actor)
{
    application_provider *provider = arsenal_provider(opaque, actor);
    if (provider == NULL)
        return false;
    if (provider->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view view;
        return qa_q1_player_read(provider->state.q1, actor, &view) &&
               view.holstered;
    }
    if (provider->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state state;
        return qa_q2_weapon_read(provider->state.q2, actor, &state, NULL) &&
               state.handoff == QA_Q2_PRIMARY_HOLSTERED;
    }
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state state;
        return qa_q3_player_read(provider->state.q3, actor, &state) &&
               state.external_slot == QA_Q3_SLOT_HOLSTERED;
    }
    if (provider->kind == APPLICATION_PROVIDER_QVM) {
        qa_equipment_state state;
        qa_application *application = opaque;
        return application_arsenal_guest_equipment_handoff_ready(provider, actor, NULL) &&
            qa_equipment_read(application->equipment, actor, &state) && state.slot_holstering;
    }
    return false;
}

static bool equipment_holstered_read(void *opaque,qa_actor_id actor,bool *out,qa_error *error)
{
    application_provider *provider=arsenal_provider(opaque,actor);
    if(!provider||!out) return application_fail(error,QA_ERROR_NOT_FOUND,"Equipment holster lost its actual selected arsenal");
    if(provider->kind==APPLICATION_PROVIDER_QC)
        return qa_application_qc_weapon_settled(opaque,actor,out,error);
    *out=equipment_holstered(opaque,actor); return true;
}
static bool equipment_resume(void *opaque, qa_actor_id actor, qa_error *error)
{
    application_provider *provider = arsenal_provider(opaque, actor);
    if (provider == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "actor has no selected arsenal owner");
    if (provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_primary_weapon_resume(provider->state.q1, actor, error);
    if (provider->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state state;
        if (!qa_q2_weapon_read(provider->state.q2, actor, &state, error))
            return false;
        return qa_q2_weapon_resume(provider->state.q2, actor,
                                   &(qa_q2_weapon_input){0}, state.weapon,
                                   error);
    }
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        return qa_q3_set_weapon_slot(provider->state.q3, actor, false, error);
    if (provider->kind == APPLICATION_PROVIDER_QVM)
        return application_arsenal_guest_equipment_handoff_ready(provider, actor, error);
    if(provider->kind==APPLICATION_PROVIDER_QC) {
        if(provider->state.qc.qualified && provider->state.qc.qualified->items &&
            provider->state.qc.qualified->items->weapons)
            return application_qc_item_weapons_resume(provider->state.qc.engine,actor,error);
        return qa_actors_get(qa_session_actors(provider->application->session),actor)!=NULL;
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "selected arsenal has no equipment resume adapter");
}

bool application_match_prepare_modes(qa_application *application,
                               application_publication *publication,
                               qa_error *error)
{
    const qa_launch_choices *choices =
        qa_launch_snapshot_choices(publication->candidate);
    if (choices->mode_count >= UINT32_MAX)
        return application_fail(error, QA_ERROR_MEMORY,
                                "selected mode roster exceeds engine capacity");
    qa_actor_owner owner;
    if (!qa_strings_intern_cstr(qa_session_strings(application->session),
                                "application:modes", &owner, error))
        return false;

    qa_modes_options modes = {
        .owner = owner,
        .services = application_builtin_services(
            application,
            publication->initial_world != NULL ? publication->initial_world
                                               : application->world,
            application->physics),
        .hooks = mode_hooks(application),
        .mode_capacity = (uint32_t)choices->mode_count + 1,
        .objective_capacity = qa_actors_capacity(qa_session_actors(application->session)),
        .random_seed = application->catalog_generation ^
                       UINT64_C(0x9e3779b97f4a7c15),
    };
    if (!qa_modes_create(&modes, &publication->modes, error))
        return false;
    if (choices->mode_count > SIZE_MAX / sizeof(*publication->mode_ids))
        return application_fail(error, QA_ERROR_MEMORY,
                                "selected mode roster is too large");
    publication->mode_ids =
        calloc(choices->mode_count == 0 ? 1 : choices->mode_count,
               sizeof(*publication->mode_ids));
    if (publication->mode_ids == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot retain selected mode identities");
    for (size_t index = 0; !publication->restoring &&
                           index < choices->mode_count; ++index) {
        qa_mode_rules rules = choices->modes[index].rules;
        if (rules.source == QA_MODE_ROGUE) {
            application_provider *source = named(publication, choices->modes[index].instance);
            int32_t deathmatch;
            uint32_t gamecfg;
            if (source == NULL || source->kind != APPLICATION_PROVIDER_Q1 ||
                !source->constructed ||
                !qa_q1_game_rules_read(source->state.q1, &deathmatch, &gamecfg))
                return application_fail(error, QA_ERROR_UNSUPPORTED,
                                        "Rogue runes require actual native Q1 source rules");
            rules.rogue_deathmatch = deathmatch != 0;
            rules.relics = rules.relics && rules.rogue_deathmatch && (gamecfg & 1) != 0;
        }
        for (size_t team = 0; team < 3; ++team)
            if (choices->modes[index].teams[team][0] != '\0' &&
                !qa_strings_intern_cstr(qa_session_strings(application->session),
                                        choices->modes[index].teams[team],
                                        &rules.teams[team], error))
                return false;
        if (choices->modes[index].forced_team[0] != '\0' &&
            !qa_strings_intern_cstr(qa_session_strings(application->session),
                                    choices->modes[index].forced_team,
                                    &rules.forced_team, error))
            return false;
        qa_mode_id id;
        if (!qa_modes_add(publication->modes, &rules, &id, error))
            return false;
        publication->mode_ids[publication->mode_count++] = id;
        if (choices->modes[index].primary_score && rules.enabled)
            publication->primary_mode = id;
    }

    return application_native_q1_composition_prepare(application, publication, error);
}

bool application_match_prepare_equipment(qa_application *application,
                                         application_publication *publication,
                                         qa_error *error)
{
    if (!publication || publication->equipment || publication->equipment_runtime)
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment publication already owns its controller or source roster");
    qa_equipment_options equipment = {
        .services = application_builtin_services(
            application,
            publication->initial_world != NULL ? publication->initial_world
                                               : application->world,
            application->physics),
        .context = application,
        .select_weapon = application_native_mode_select_weapon,
        .primary_holster = equipment_holster,
        .primary_owner = equipment_primary_owner,
        .primary_holstered = equipment_holstered,
        .primary_holstered_read = equipment_holstered_read,
        .primary_resume = equipment_resume,
        .primary_accepts = application_equipment_primary_accepts,
        .primary_select = application_equipment_primary_select,
        .grenade_interval = application_q3_weapons_services_grenade_interval,
    };
    application_equipment_runtime_options runtime = {.application = application,
        .snapshot = publication->candidate, .providers = publication->next,
        .provider_count = publication->next_count, .world_source = publication->map_provider,
        .services = equipment.services, .entity_text = publication->map.lumps[QA_BSP_ENTITIES].bytes};
    if (!application_equipment_runtime_create(&runtime, publication->equipment_runtime_saved,
        &publication->equipment_runtime, error)) return false;
    application_equipment_runtime_bind(publication->equipment_runtime, &equipment);
    return qa_equipment_create(&equipment, &publication->equipment, error);
}

bool application_match_prepare(qa_application *application,
                               application_publication *publication,
                               qa_error *error)
{
    return application_match_prepare_modes(application, publication, error) &&
           application_match_prepare_equipment(application, publication, error);
}

bool qa_application_prepare_match_travel(qa_application *application,
                                         qa_error *error)
{
    if (application == NULL || application->operation != APPLICATION_IDLE ||
        application->destroy_requested || application->finalizing ||
        application->startup_flow ||
        application->state == QA_APPLICATION_FAULTED ||
        application->state == QA_APPLICATION_STOPPING ||
        !qa_session_destroy_ready(application->session) ||
        !qa_world_idle(application->world) || !qa_combat_idle(application->combat))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "match travel requires idle shared owners");
    if (application->match_intents == NULL)
        return true;
    bool consumed = false;
    if (!application_match_intents_reconnect(application->match_intents,
                                              application, error) ||
        !application_match_intents_prepare(application->match_intents,
                                            application, &consumed, error))
        return false;
    if (consumed)
        return true;
    const application_next_map_plan *plan = NULL;
    qa_application_travel_request request;
    if (!application_match_intents_travel_read(application->match_intents,
                                                &plan, &request))
        return true;
    qa_application_travel_view travel;
    return application_source_queue_map_travel(application, &request, error) &&
           qa_application_travel_read(application, &travel) &&
           application_match_intents_queued(application->match_intents,
                                              application, travel.revision, error);
}

bool qa_application_finish_match_travel(qa_application *application,
                                        uint64_t revision, qa_error *error)
{
    if (application == NULL || application->operation != APPLICATION_IDLE ||
        application->destroy_requested || application->finalizing ||
        application->state == QA_APPLICATION_FAULTED ||
        application->state == QA_APPLICATION_STOPPING)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "match completion requires a live idle application");
    uint64_t published_revision = 0;
    if (!qa_application_travel_publication_read(application, &published_revision) || published_revision != revision)
        return application_fail(error, QA_ERROR_ARGUMENT, "match completion requires its actual travel publication");
    if (application->match_intents == NULL)
        return true;
    uint64_t pending_revision = 0;
    if (!application_match_intents_waiting(application->match_intents,
                                           &pending_revision)) {
        bool consumed = false;
        return application_match_intents_prepare(application->match_intents,
                                                    application, &consumed, error);
    }
    if (pending_revision != revision)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "match completion differs from its queued travel");
    return application_match_intents_completed(application->match_intents,
                                                application, revision, error);
}
