#include "internal.h"

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
    return application_emit(
        application,
        &(qa_builtin_event){.kind = event->kind == QA_MODE_MESSAGE
                                        ? QA_BUILTIN_MESSAGE
                                        : QA_BUILTIN_EFFECT,
                            .family = mode_family(application, event->mode),
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

static qa_modes_hooks mode_hooks(qa_application *application)
{
    return (qa_modes_hooks){.context = application,
                            .event = mode_event,
                            .emit = application_native_mode_emit,
                            .intent = mode_intent,
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
                            .player_view = mode_player_view};
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

static bool assign_equipment_source(application_provider *provider,
                                    qa_equipment_options *options,
                                    qa_error *error)
{
    if (provider == NULL)
        return true;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1:
        if (options->q1 != NULL && options->q1 != provider->state.q1)
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                                    "one equipment scope selects multiple Q1 instances");
        options->q1 = provider->state.q1;
        return true;
    case APPLICATION_PROVIDER_Q2:
        if (options->q2 != NULL && options->q2 != provider->state.q2)
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                                    "one equipment scope selects multiple Q2 instances");
        options->q2 = provider->state.q2;
        return true;
    case APPLICATION_PROVIDER_Q3:
        if (options->q3 != NULL && options->q3 != provider->state.q3)
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                                    "one equipment scope selects multiple Q3 instances");
        options->q3 = provider->state.q3;
        options->q3_owner = provider->owner;
        return true;
    case APPLICATION_PROVIDER_QC:
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "external equipment projection is not installed");
    }
    return false;
}

static application_provider *arsenal_provider(qa_application *application,
                                              qa_actor_id actor)
{
    return application_provider_for(application, actor, QA_ROLE_ARSENAL, "");
}

static bool equipment_holster(void *opaque, qa_actor_id actor,
                              qa_error *error)
{
    application_provider *provider = arsenal_provider(opaque, actor);
    if (provider == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "actor has no selected arsenal owner");
    if (provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_grapple_weapon_holster(provider->state.q1, actor, error);
    if (provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_weapon_holster(provider->state.q2, actor, error);
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        return qa_q3_set_weapon_slot(provider->state.q3, actor, true, error);
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
    return false;
}

static bool equipment_resume(void *opaque, qa_actor_id actor, qa_error *error)
{
    application_provider *provider = arsenal_provider(opaque, actor);
    if (provider == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "actor has no selected arsenal owner");
    if (provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_grapple_weapon_resume(provider->state.q1, actor, error);
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
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "selected arsenal has no equipment resume adapter");
}

bool application_match_prepare(qa_application *application,
                               application_publication *publication,
                               qa_error *error)
{
    const qa_launch_choices *choices =
        qa_launch_snapshot_choices(publication->candidate);
    if (choices->mode_count > UINT32_MAX)
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
        .mode_capacity = (uint32_t)(choices->mode_count == 0
                                        ? 1
                                        : choices->mode_count),
        .objective_capacity = 256,
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

    qa_equipment_options equipment = {
        .services = modes.services,
        .context = application,
        .select_weapon = application_native_mode_select_weapon,
        .primary_holster = equipment_holster,
        .primary_holstered = equipment_holstered,
        .primary_resume = equipment_resume,
    };
    for (size_t index = 0; index < choices->equipment_count; ++index) {
        const qa_launch_equipment *selection = &choices->equipment[index];
        if (!assign_equipment_source(named(publication, selection->instance),
                                     &equipment, error) ||
            !assign_equipment_source(named(publication,
                                           selection->grapple_source),
                                     &equipment, error) ||
            !assign_equipment_source(named(publication,
                                           selection->grenade_source),
                                     &equipment, error))
            return false;
    }
    if (equipment.q3 != NULL) {
        application_provider *provider = NULL;
        for (size_t index = 0; index < publication->next_count; ++index)
            if (publication->next[index]->kind == APPLICATION_PROVIDER_Q3 &&
                publication->next[index]->state.q3 == equipment.q3) {
                provider = publication->next[index];
                break;
            }
        const qa_product *product =
            provider == NULL
                ? NULL
                : qa_catalog_product(qa_launch_snapshot_catalog(
                                         publication->candidate),
                                     provider->launch->selection.product);
        equipment.q3_product =
            product != NULL && strcmp(product->campaign, "missionpack") == 0
                ? QA_Q3_TEAM_ARENA
                : QA_Q3_ARENA;
    }
    return qa_equipment_create(&equipment, &publication->equipment, error);
}
