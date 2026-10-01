#include "internal.h"
#include "control_frame.h"
#include "native_q2_combat_policy.h"
#include "native_q1_wire.h"
#include "supplies.h"
#include "qa/application_players.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static const qa_launch_choices *active_choices(const qa_application *application)
{
    const qa_launch_snapshot *snapshot = application->routing_snapshot;
    if (snapshot == NULL && application->configuration != NULL)
        snapshot = qa_configuration_current(application->configuration);
    return qa_launch_snapshot_choices(snapshot);
}

static bool same_scope(qa_launch_scope left, qa_launch_scope right)
{
    if (left.kind != right.kind)
        return false;
    if (left.kind == QA_SCOPE_ACTOR)
        return qa_actor_id_equal(left.actor, right.actor);
    if (left.kind == QA_SCOPE_SEAT)
        return left.seat == right.seat;
    return true;
}

static const qa_launch_binding *exact_binding(const qa_launch_choices *choices,
                                              qa_launch_scope scope,
                                              qa_launch_role role,
                                              const char *selector)
{
    if (choices == NULL)
        return NULL;
    if (selector == NULL)
        selector = "";
    for (size_t index = 0; index < choices->binding_count; ++index) {
        const qa_launch_binding *binding = &choices->bindings[index];
        if (binding->role == role && same_scope(binding->scope, scope) &&
            strcmp(binding->selector, selector) == 0)
            return binding;
    }
    return NULL;
}

static application_provider *provider_named(qa_application *application,
                                            const char *instance)
{
    if (instance == NULL)
        return NULL;
    application_provider **providers = application->routing_providers != NULL
                                           ? application->routing_providers
                                           : application->providers;
    size_t count = application->routing_providers != NULL
                       ? application->routing_provider_count
                       : application->provider_count;
    for (size_t index = 0; index < count; ++index) {
        application_provider *provider = providers[index];
        if (provider != NULL && provider->attached && provider->launch != NULL &&
            strcmp(provider->launch->selection.instance, instance) == 0)
            return provider;
    }
    return NULL;
}

static application_provider *provider_owned(qa_application *application,
                                            qa_actor_owner owner)
{
    if (owner == 0)
        return NULL;
    application_provider **providers = application->routing_providers != NULL
                                           ? application->routing_providers
                                           : application->providers;
    size_t count = application->routing_providers != NULL
                       ? application->routing_provider_count
                       : application->provider_count;
    for (size_t index = 0; index < count; ++index) {
        application_provider *provider = providers[index];
        if (provider != NULL && provider->attached && provider->owner == owner)
            return provider;
    }
    return NULL;
}

static bool provider_traits(application_provider *provider, qa_actor_id actor,
                            qa_builtin_actor_traits *out)
{
    if (provider == NULL || !provider->constructed)
        return false;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1:
        return qa_q1_game_actor_traits(provider->state.q1, actor, out);
    case APPLICATION_PROVIDER_Q2:
        return qa_q2_actor_traits(provider->state.q2, actor, out);
    case APPLICATION_PROVIDER_Q3:
        return qa_q3_actor_traits(provider->state.q3, actor, out);
    case APPLICATION_PROVIDER_QC:
        return application_qc_actor_traits(provider, actor, out);
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        return false;
    }
    return false;
}

static bool actor_is_player(qa_application *application, qa_actor_id actor,
                            const qa_launch_choices *choices)
{
    uint32_t seat;
    if (qa_application_player_seat(application, actor, &seat))
        return true;
    if (choices != NULL)
        for (size_t index = 0; index < choices->seat_count; ++index)
        {
            qa_actor_id live;
            if (qa_actor_id_equal(choices->seats[index].actor, actor) ||
                (qa_application_player_actor(application, choices->seats[index].id, &live) &&
                 qa_actor_id_equal(live, actor)))
                return true;
        }
    application_provider **providers = application->routing_providers != NULL
                                           ? application->routing_providers
                                           : application->providers;
    size_t count = application->routing_providers != NULL
                       ? application->routing_provider_count
                       : application->provider_count;
    for (size_t index = 0; index < count; ++index) {
        qa_builtin_actor_traits traits = {0};
        if (provider_traits(providers[index], actor, &traits) &&
            traits.player)
            return true;
    }
    return false;
}

application_provider *application_world_provider(qa_application *application,
                                                  qa_launch_role role,
                                                  const char *selector)
{
    if (application == NULL)
        return NULL;
    const qa_launch_binding *binding = exact_binding(active_choices(application),
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, role, selector);
    return binding == NULL ? NULL : provider_named(application, binding->instance);
}

application_provider *application_provider_for(qa_application *application,
                                               qa_actor_id actor,
                                               qa_launch_role role,
                                               const char *selector)
{
    if (application == NULL || (unsigned)role >= QA_ROLE_COUNT)
        return NULL;
    const qa_launch_choices *choices = active_choices(application);
    const qa_launch_binding *binding =
        exact_binding(choices, (qa_launch_scope){.kind = QA_SCOPE_ACTOR,
                                                 .actor = actor},
                      role, selector);
    if (binding != NULL)
        return provider_named(application, binding->instance);

    qa_actor_id configured;
    if (application_player_source_actor(application, actor, &configured)) {
        binding = exact_binding(choices,
                                (qa_launch_scope){.kind = QA_SCOPE_ACTOR,
                                                  .actor = configured},
                                role, selector);
        if (binding != NULL)
            return provider_named(application, binding->instance);
    }

    uint32_t live_seat;
    if (qa_application_player_seat(application, actor, &live_seat)) {
        binding = exact_binding(choices,
            (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = live_seat}, role, selector);
        if (binding != NULL)
            return provider_named(application, binding->instance);
    }

    if (choices != NULL)
        for (size_t index = 0; index < choices->seat_count; ++index) {
            qa_actor_id live;
            if (!qa_actor_id_equal(choices->seats[index].actor, actor) &&
                !(qa_application_player_actor(application, choices->seats[index].id, &live) &&
                  qa_actor_id_equal(live, actor)))
                continue;
            binding = exact_binding(
                choices,
                (qa_launch_scope){.kind = QA_SCOPE_SEAT,
                                  .seat = choices->seats[index].id},
                role, selector);
            if (binding != NULL)
                return provider_named(application, binding->instance);
        }

    if (actor_is_player(application, actor, choices)) {
        binding = exact_binding(
            choices, (qa_launch_scope){.kind = QA_SCOPE_DEFAULT_PLAYER}, role,
            selector);
        if (binding != NULL)
            return provider_named(application, binding->instance);
    }

    const qa_actor_record *record =
        qa_actors_get(qa_session_actors(application->session), actor);
    return record == NULL ? NULL : provider_owned(application, record->owner);
}

static void remember_failure(bool result, const qa_error *current,
                             const char *fallback, bool *ok, qa_error *first)
{
    if (result || !*ok)
        return;
    *ok = false;
    if (current != NULL && current->code != QA_OK)
        *first = *current;
    else
        qa_error_set(first, QA_ERROR_ARGUMENT, 0, "%s", fallback);
}

bool application_provider_actor_released(application_provider *provider,
                                         qa_actor_record released,
                                         qa_error *error)
{
    if (provider == NULL || !provider->constructed)
        return true;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1:
        qa_q1_game_actor_released(provider->state.q1, released);
        return true;
    case APPLICATION_PROVIDER_Q2:
        return qa_q2_actor_released(provider->state.q2, released, error);
    case APPLICATION_PROVIDER_Q3:
        qa_q3_actor_released(provider->state.q3, released);
        return true;
    case APPLICATION_PROVIDER_QC:
        return application_qc_actor_released(provider, released, error);
    case APPLICATION_PROVIDER_NATIVE:
        if (provider->state.native.engine != NULL)
            return application_q3_guest_actor_released(provider, released, error);
        if (provider->state.native.host != NULL &&
            !qa_native_host_actor_released(provider->state.native.host,
                                           released, error))
            return false;
        return provider->state.native.q3_host == NULL ||
               qa_q3_host_actor_released(provider->state.native.q3_host,
                                         released, error);
    case APPLICATION_PROVIDER_QVM:
        return application_q3_guest_actor_released(provider, released, error);
    }
    return true;
}

bool application_actor_released(void *opaque, qa_session *session,
                                qa_actor_record released, qa_error *error)
{
    qa_application *application = opaque;
    if (application == NULL || session != application->session)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "actor release reached the wrong application");

    application_supplies_actor_released(application->supplies, released);
    application_native_q1_wire_actor_released(application, released.id);

    bool ok = true;
    qa_error first = {0};
    qa_error current = {0};
    remember_failure(application_bots_actor_released(application, released, &current),
                     &current, "bot actor retirement failed", &ok, &first);
    current = (qa_error){0};
    if (application->world != NULL)
        remember_failure(qa_world_actor_released(application->world, released,
                                                 &current),
                         &current, "world actor retirement failed", &ok, &first);
    qa_combat_actor_released(application->combat, released);
    qa_inventory_actor_released(application->inventory, released);
    qa_pickups_actor_released(application->pickups, released);
    if (application->targets != NULL)
        qa_targets_unbind(application->targets, released.id);
    if (application->equipment != NULL)
        qa_equipment_actor_released(application->equipment, released);
    if (released.id.slot < application->motion_capacity) {
        application_motion_record *motion =
            &application->motion[released.id.slot];
        if (motion->active && qa_actor_id_equal(motion->actor, released.id))
            *motion = (application_motion_record){0};
    }
    if (released.id.slot < application->control_capacity) {
        application_control_record *control =
            &application->controls[released.id.slot];
        if (control->active &&
            qa_actor_id_equal(control->actor, released.id)) {
            if (control->moving) {
                control->active = false;
                control->retired = true;
            } else {
                qa_movement_result_free(&control->result);
                *control = (application_control_record){0};
            }
        }
    }
    application_control_frames_release(application, released.id);
    if (released.id.slot < application->q2_visual_capacity) {
        application_q2_visual_record *visual =
            &application->q2_visuals[released.id.slot];
        if (visual->active && qa_actor_id_equal(visual->actor, released.id))
            *visual = (application_q2_visual_record){0};
    }
    current = (qa_error){0};
    if (application->modes != NULL)
        remember_failure(
            qa_modes_actor_released(application->modes, released, &current),
            &current, "mode actor retirement failed", &ok, &first);

    for (size_t index = 0; index < application->provider_count; ++index) {
        application_provider *provider = application->providers[index];
        if (provider != NULL && provider->component_attached &&
            provider->owner == released.owner &&
            provider->component.actor_released != NULL)
            continue;
        current = (qa_error){0};
        remember_failure(application_provider_actor_released(
                             provider, released, &current),
                         &current, "provider actor retirement failed", &ok,
                         &first);
    }
    if (!ok && error != NULL)
        *error = first;
    return ok;
}

static qa_team_id combat_team(void *opaque, qa_actor_id actor,
                              qa_team_id fallback)
{
    qa_application *application = opaque;
    return application->modes != NULL && application->primary_mode_ready
               ? qa_modes_combat_team(application->modes,
                                      application->primary_mode, actor, fallback)
               : fallback;
}

static bool q3_damage_allowed(void *opaque, const qa_damage_request *request)
{
    qa_application *application = opaque;
    application_provider *effects = application_provider_for(
        application, request->target, QA_ROLE_EFFECTS, "");
    application_provider *character = application_provider_for(
        application, request->target, QA_ROLE_CHARACTER, "");
    if (effects != NULL && effects->kind == APPLICATION_PROVIDER_Q3 &&
        !qa_q3_damage_allowed(effects->state.q3, request))
        return false;
    return character == NULL || character == effects ||
           character->kind != APPLICATION_PROVIDER_Q3 ||
           qa_q3_damage_allowed(character->state.q3, request);
}

static bool combat_impulse(void *opaque, qa_actor_id actor, qa_vec3 impulse,
                           qa_actor_owner movement, qa_error *error)
{
    qa_application *application = opaque;
    if (application->world == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "damage impulse has no published world");
    application_provider *selected = application_provider_for(
        application, actor, QA_ROLE_MOVEMENT, "");
    if (movement != 0 &&
        (selected == NULL || selected->owner != movement))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "damage impulse names an unselected movement owner");
    qa_body_state body;
    if (!qa_world_body_read(application->world, actor, &body, error))
        return false;
    body.velocity = qa_vec_add(body.velocity, impulse);
    return qa_world_body_write(application->world, actor, &body, error);
}

static bool before_reaction(void *opaque, const qa_damage_outcome *outcome,
                            qa_error *error)
{
    qa_application *application = opaque;
    if (!application_native_q1_wire_damage(application, outcome, error))
        return false;
    if (!application_native_q2_combat_before_reaction(application, outcome, error))
        return false;
    application_provider *effects = application_provider_for(
        application, outcome->request.target, QA_ROLE_EFFECTS, "");
    application_provider *character = application_provider_for(
        application, outcome->request.target, QA_ROLE_CHARACTER, "");
    if (outcome->result.reaction == QA_REACTION_NONE && outcome->result.has_feedback &&
        character != NULL &&
        character->kind == APPLICATION_PROVIDER_Q2 && character->constructed &&
        character->attached && !character->close_pending &&
        !qa_q2_damage_reaction(character->state.q2, outcome, error))
        return false;
    if (effects != NULL && effects->kind == APPLICATION_PROVIDER_Q3 &&
        !qa_q3_before_reaction(effects->state.q3, outcome, error))
        return false;
    if (character != NULL && character != effects &&
        character->kind == APPLICATION_PROVIDER_Q3 &&
        !qa_q3_before_reaction(character->state.q3, outcome, error))
        return false;
    application_provider *source = application_world_provider(
        application, QA_ROLE_ENTITIES, "");
    uint32_t slot;
    return source == NULL || source == effects || source == character ||
           source->kind != APPLICATION_PROVIDER_Q3 || !source->constructed ||
           !source->attached || source->close_pending ||
           !qa_q3_native_client_slot(source->state.q3, outcome->request.target,
                                      &slot, NULL) ||
           qa_q3_before_reaction(source->state.q3, outcome, error);
}

static bool selected_source_reaction(void *opaque, const qa_damage_outcome *outcome,
                                     qa_actor_owner source_owner,
                                     qa_source_reaction_body original,
                                     void *original_context, qa_error *error)
{
    qa_application *application = opaque;
    application_provider *source = application_world_provider(application, QA_ROLE_ENTITIES, "");
    const qa_actor_record *record = qa_actors_get(qa_session_actors(application->session),
                                                outcome->request.target);
    if (source && source->kind == APPLICATION_PROVIDER_Q3 && source->constructed &&
        source->attached && !source->close_pending && record && record->owner == source->owner) {
        bool handled;
        if (!qa_q3_source_obelisk_reaction(source->state.q3, outcome, &handled, error)) return false;
        if (handled) return true;
    }
    if (application->modes != NULL &&
        !qa_modes_object_reaction(application->modes, outcome, error))
        return false;
    application_provider *provider = application_provider_for(
        application, outcome->request.target, QA_ROLE_CHARACTER, "");
    if (provider == NULL && original == NULL)
        return true;
    bool death = outcome->result.reaction == QA_REACTION_DEATH;
    if (death && application->modes != NULL)
        for (size_t i = 0; i < application->mode_count; ++i)
            if (!qa_modes_horde_before_death(application->modes,
                    application->mode_ids[i], outcome, error))
                return false;
    bool ok = true;
    if (qa_actors_get(qa_session_actors(application->session),
                      outcome->request.target) != NULL) {
      if (original != NULL && (provider == NULL || provider->owner == source_owner)) {
        ok = original(original_context, error);
      } else switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1:
        ok = qa_q1_game_reaction(provider->state.q1, outcome, error);
        break;
    case APPLICATION_PROVIDER_Q2:
        ok = qa_q2_damage_reaction(provider->state.q2, outcome, error);
        break;
    case APPLICATION_PROVIDER_Q3:
        ok = qa_q3_damage_reaction(provider->state.q3, outcome, error);
        break;
    case APPLICATION_PROVIDER_QC:
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        ok = application_fail(error, QA_ERROR_UNSUPPORTED,
                              "selected foreign character has no source reaction adapter");
        break;
      }
    }
    if (ok && death && application->modes != NULL)
        for (size_t i = 0; i < application->mode_count; ++i)
            if (!qa_modes_horde_after_death(application->modes,
                    application->mode_ids[i], outcome->request.target, error))
                return false;
    return ok;
}

static bool selected_reaction(void *opaque, const qa_damage_outcome *outcome,
                              qa_error *error)
{
    return selected_source_reaction(opaque, outcome, 0, NULL, NULL, error);
}

static bool death_cleanup(qa_application *application,
                          const qa_damage_outcome *outcome, qa_error *error)
{
    if (outcome->stale)
        return true;
    if (outcome->result.reaction == QA_REACTION_DEATH)
        for (size_t index = 0; index < application->provider_count; ++index) {
            application_provider *provider = application->providers[index];
            if (provider != NULL && provider->attached && provider->constructed &&
                provider->kind == APPLICATION_PROVIDER_Q3 &&
                !qa_q3_player_death_cleanup(provider->state.q3,
                                            outcome->request.target, error))
                return false;
        }
    return true;
}

static bool confirmed_death(void *opaque, const qa_damage_outcome *outcome,
                            qa_error *error)
{
    qa_application *application = opaque;
    if (outcome->stale)
        return true;
    if (!death_cleanup(application, outcome, error))
        return false;
    if (application->modes == NULL || !application->primary_mode_ready)
        return true;
    return outcome->result.reaction != QA_REACTION_DEATH ||
           qa_modes_player_death(application->modes,
                                 application->primary_mode, outcome, error);
}

static bool confirmed_damage(void *opaque, const qa_damage_outcome *outcome,
                             qa_error *error)
{
    qa_application *application = opaque;
    if (outcome->stale)
        return true;
    /* Preserve ordinary cleanup before after-damage, then score a death. The
     * deferred original source path confirms only the later death boundary. */
    if (!death_cleanup(application, outcome, error))
        return false;
    if (application->modes == NULL || !application->primary_mode_ready)
        return true;
    return qa_modes_after_damage(application->modes, application->primary_mode,
                                  outcome, error) &&
           (outcome->result.reaction != QA_REACTION_DEATH ||
            qa_modes_player_death(application->modes,
                                  application->primary_mode, outcome, error));
}

static bool provider_invulnerable(application_provider *provider,
                                  qa_actor_id actor)
{
    if (provider == NULL || !provider->constructed)
        return false;
    if (provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_game_invulnerable(provider->state.q1, actor);
    if (provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_timed_invulnerability(provider->state.q2, actor);
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        return qa_q3_timed_invulnerability(provider->state.q3, actor);
    return false;
}

static bool force_death(qa_application *application,
                        const qa_damage_request *request, int32_t final_health,
                        bool source_death, application_provider *q2_source, qa_error *error)
{
    if (application == NULL || application->combat == NULL ||
        application->destroy_requested ||
        final_health > 0 ||
        !qa_damage_request_validate(request, error))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "direct death requires an active combat owner");
    qa_actor_registry *actors = qa_session_actors(application->session);
    if (qa_actors_get(actors, request->target) == NULL)
        return true;
    application_provider *character = application_provider_for(
        application, request->target, QA_ROLE_CHARACTER, "");
    if (character == NULL || !character->constructed ||
        character->kind > APPLICATION_PROVIDER_Q3)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "selected character has no direct death adapter");
    qa_combat_state traits;
    if (!qa_combat_read_traits(application->combat, request->target,
                               &traits, error))
        return false;
    if (traits.health <= 0 && !source_death)
        return true;
    bool react = true;
    if (q2_source) {
        qa_q2_player_info player;
        if (q2_source->application != application || q2_source->kind != APPLICATION_PROVIDER_Q2 ||
            !q2_source->constructed || !q2_source->attached || q2_source->close_pending ||
            request->attack.combat_provider != q2_source->owner ||
            !qa_actor_id_equal(request->target, request->attack.attacker) ||
            !qa_actor_id_equal(request->target, request->attack.inflictor) ||
            request->attack.cause.kind != QA_CAUSE_Q2 ||
            ((uint32_t)request->attack.cause.source.q2.means_of_death &
                ~UINT32_C(0x08000000)) != 23 ||
            !qa_q2_player_read(q2_source->state.q2, request->target, &player) || !player.connected)
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "source suicide requires its actual Q2 client");
        if (traits.health != 0)
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "Q2 source suicide has not committed its health-zero boundary");
    } else if (source_death) {
        application_provider *source = application_world_provider(
            application, QA_ROLE_ENTITIES, "");
        qa_q3_player_state player;
        qa_q3_wire_policy policy;
        qa_q3_source_match_state match;
        uint32_t slot;
        if (!source || source->kind != APPLICATION_PROVIDER_Q3 ||
            !source->constructed || !source->attached || source->close_pending ||
            !qa_q3_native_client_slot(source->state.q3, request->target, &slot, error) ||
            !qa_q3_player_read(source->state.q3, request->target, &player) ||
            !qa_q3_wire_player_policy_read(source->state.q3, request->target,
                                            &policy, error) ||
            !qa_q3_source_match_state_read(source->state.q3, &match, error))
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "source death requires its actual native Q3 client state");
        react = !player.dead && policy.pm_type != 3 && !match.intermission_time_ms;
    }
    application_operation previous = application->operation;
    application->operation = APPLICATION_ADVANCING;
    qa_damage_mutation mutation = {
        .kind = QA_MUTATION_HEALTH,
        .value.health = {.before = traits.health, .after = final_health},
    };
    qa_damage_outcome outcome = {
        .request = *request,
        .result = {.applied_damage = request->amount,
                   .reaction = QA_REACTION_DEATH},
        .mutations = &mutation,
        .mutation_count = q2_source ? 0 : 1,
    };
    traits.invulnerable = false;
    bool ok = q2_source || (qa_combat_set_traits(application->combat, request->target,
                                    &traits, error) &&
              qa_combat_set_health(application->combat, request->target,
                                    final_health, error));
    if (ok && react)
        ok = qa_combat_source_reaction(application->combat, request,
                                         &outcome.result, error);
    if (ok && react && qa_actors_get(actors, request->target) != NULL)
        ok = selected_reaction(application, &outcome, error);
    if (ok && react)
        ok = confirmed_damage(application, &outcome, error);
    application->operation = previous;
    return ok;
}

bool application_force_death(void *opaque, const qa_damage_request *request,
                              qa_error *error)
{
    return force_death(opaque, request, -999, false, NULL, error);
}

bool application_source_force_death(qa_application *application,
    const qa_damage_request *request, int32_t final_health, qa_error *error)
{
    return force_death(application, request, final_health, true, NULL, error);
}

bool application_native_q2_source_suicide(void *opaque, const qa_damage_request *request,
                                          qa_error *error)
{
    application_provider *source = opaque;
    if (!source || !request)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 source suicide lost its owner");
    return force_death(source->application, request, 0, true, source, error);
}

static bool combat_invulnerable(void *opaque, qa_actor_id actor)
{
    qa_application *application = opaque;
    application_provider *effects = application_provider_for(
        application, actor, QA_ROLE_EFFECTS, "");
    application_provider *character = application_provider_for(
        application, actor, QA_ROLE_CHARACTER, "");
    return provider_invulnerable(effects, actor) ||
           (character != effects && provider_invulnerable(character, actor));
}

static bool combat_effect(void *opaque, qa_combat *combat,
                          qa_damage_effect_stage stage,
                          const qa_damage_request *request,
                          qa_damage_effect *effect, qa_error *error)
{
    qa_application *application = opaque;
    if (combat != application->combat)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "damage effect reached the wrong combat owner");
    const qa_launch_binding *world_binding = exact_binding(active_choices(application),
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
    application_provider *selected[] = {
        application_provider_for(application, request->target, QA_ROLE_EFFECTS, ""),
        application_provider_for(application, request->target, QA_ROLE_CHARACTER, ""),
        world_binding == NULL ? NULL : provider_named(application, world_binding->instance),
    };
    for (size_t i = 0; i < sizeof(selected) / sizeof(*selected); ++i) {
        application_provider *provider = selected[i];
        if (provider == NULL || !provider->constructed ||
            provider->kind != APPLICATION_PROVIDER_Q1)
            continue;
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j)
            duplicate |= selected[j] == provider;
        if (!duplicate &&
            !qa_q1_game_damage_effect(provider->state.q1, stage, request, effect, error))
            return false;
        if (qa_actors_get(qa_session_actors(application->session), request->target) == NULL)
            return true;
    }
    return application->modes == NULL || !application->primary_mode_ready ||
           qa_modes_damage_effect(application->modes,
                                  application->primary_mode, stage, request,
                                  effect, error);
}

static bool combat_inflictor_center(void *opaque, const qa_damage_request *request,
                                    double center[3], bool *found, qa_error *error)
{
    return application_native_q1_wire_inflictor_center(opaque, request, center, found, error);
}

qa_combat_hooks application_combat_hooks(qa_application *application)
{
    return (qa_combat_hooks){.context = application,
                             .inflictor_center = combat_inflictor_center,
                             .team = combat_team,
                             .damage_allowed = q3_damage_allowed,
                             .impulse = combat_impulse,
                             .before_reaction = before_reaction,
                             .reaction = selected_reaction,
                             .source_reaction = selected_source_reaction,
                             .source_reaction_confirmed = confirmed_death,
                             .confirmed = confirmed_damage,
                             .invulnerable = combat_invulnerable,
                             .effect = combat_effect,
                             .armor_context = application_native_q2_armor_context};
}

static bool builtin_players(void *opaque, qa_actor_id *actors, size_t capacity,
                            size_t *count, qa_error *error)
{
    qa_application *application = opaque;
    const qa_launch_choices *choices = active_choices(application);
    if (actors == NULL || count == NULL || choices == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "player roster has no active launch selection");
    size_t written = 0;
    for (size_t index = 0; index < choices->seat_count; ++index) {
        qa_actor_id actor = choices->seats[index].actor;
        (void)qa_application_player_actor(application, choices->seats[index].id, &actor);
        if (qa_actors_get(qa_session_actors(application->session), actor) == NULL)
            continue;
        if (written == capacity)
            return application_fail(error, QA_ERROR_MEMORY,
                                    "player roster output is too small");
        actors[written++] = actor;
    }
    *count = written;
    return true;
}

static bool builtin_player_info(void *opaque, qa_actor_id actor,
                                qa_builtin_player_info *out)
{
    qa_application *application = opaque;
    if (out == NULL ||
        qa_actors_get(qa_session_actors(application->session), actor) == NULL)
        return false;
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_CHARACTER, "");
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q2 &&
        qa_q2_player_projection(provider->state.q2, actor, out))
        return true;

    const qa_launch_choices *choices = active_choices(application);
    if (choices == NULL)
        return false;
    for (size_t index = 0; index < choices->seat_count; ++index) {
        const qa_launch_seat *seat = &choices->seats[index];
        qa_actor_id live = seat->actor;
        (void)qa_application_player_actor(application, seat->id, &live);
        if (!qa_actor_id_equal(live, actor))
            continue;
        *out = (qa_builtin_player_info){.name = seat->name,
                                        .slot = (uint32_t)index,
                                        .connected = true,
                                        .spectator = seat->spectator};
        qa_builtin_actor_traits traits = {0};
        if (provider_traits(provider, actor, &traits))
            out->view_height = traits.view_height;
        qa_combat_state combat;
        if (qa_combat_read(application->combat, actor, &combat, NULL))
            out->dead = combat.health <= 0;
        return true;
    }
    return false;
}

static bool builtin_traits(void *opaque, qa_actor_id actor,
                           qa_builtin_actor_traits *out)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_CHARACTER, "");
    if (provider_traits(provider, actor, out))
        return true;
    application_provider **providers = application->routing_providers != NULL
                                           ? application->routing_providers
                                           : application->providers;
    size_t count = application->routing_providers != NULL
                       ? application->routing_provider_count
                       : application->provider_count;
    for (size_t index = 0; index < count; ++index)
        if (providers[index] != provider &&
            provider_traits(providers[index], actor, out)) {
            out->has_life = false;
            out->birth_epoch = 0;
            out->dead = false;
            return true;
        }
    return false;
}

static bool builtin_use_targets(void *opaque, qa_actor_id source,
                                qa_actor_id activator, qa_string_id target,
                                qa_string_id killtarget, float delay,
                                qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(
        application, source, QA_ROLE_ENTITIES, "");
    qa_clock_kind dialect = provider != NULL
                                ? provider->component.clock.kind
                                : QA_CLOCK_NETQUAKE;
    uint64_t time_ns = qa_session_elapsed(application->session);
    qa_clock_state clock;
    if (provider != NULL &&
        qa_session_clock(application->session, provider->owner, &clock))
        time_ns = clock.frame.time_ns;
    qa_target_use request = {
        .source = source,
        .activator = activator,
        .dialect = dialect,
        .fields = {.target = target,
                   .killtarget = killtarget,
                   .delay_seconds = delay},
        .time_ns = time_ns,
    };
    return qa_targets_use_request(application->targets, &request, error);
}

static bool target_defer(void *opaque, const qa_target_use *request,
                         qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(
        application, request->source, QA_ROLE_ENTITIES, "");
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_game_map_defer_targets(provider->state.q1, request, error);
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_entity_defer_targets(provider->state.q2, request, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "selected source has no delayed-target adapter");
}

static bool target_message(void *opaque, const qa_target_use *request,
                           qa_error *error)
{
    qa_game_family family =
        request->dialect == QA_CLOCK_Q3
            ? QA_GAME_Q3
            : request->dialect == QA_CLOCK_Q2_CLASSIC ||
                      request->dialect == QA_CLOCK_Q2_RERELEASE
                  ? QA_GAME_Q2
                  : QA_GAME_Q1;
    return application_emit(
        opaque,
        &(qa_builtin_event){.kind = QA_BUILTIN_CENTERPRINT,
                            .family = family,
                            .actor = request->activator,
                            .other = request->source,
                            .time_ns = request->time_ns,
                            .text = request->fields.message},
        error);
}

static bool target_remap_shader(void *opaque, qa_string_id original,
                                qa_string_id replacement, uint64_t time_ns,
                                qa_error *error)
{
    qa_application *application = opaque;
    qa_strings *strings = qa_session_strings(application->session);
    if (original == QA_STRING_NONE || replacement == QA_STRING_NONE ||
        qa_strings_text(strings, original).data == NULL ||
        qa_strings_text(strings, replacement).data == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "shader remap needs interned source names");
    for (size_t index = 0; index < application->shader_remap_count; ++index) {
        application_shader_remap *remap =
            &application->shader_remaps[index];
        if (remap->original != original)
            continue;
        remap->replacement = replacement;
        remap->time_ns = time_ns;
        return true;
    }
    if (application->shader_remap_count ==
        application->shader_remap_capacity) {
        size_t capacity = application->shader_remap_capacity == 0
                              ? 16
                              : application->shader_remap_capacity * 2;
        if (capacity < application->shader_remap_capacity ||
            capacity > SIZE_MAX / sizeof(*application->shader_remaps))
            return application_fail(error, QA_ERROR_MEMORY,
                                    "shader remap capacity is exhausted");
        application_shader_remap *remaps =
            realloc(application->shader_remaps,
                    capacity * sizeof(*application->shader_remaps));
        if (remaps == NULL)
            return application_fail(error, QA_ERROR_MEMORY,
                                    "cannot retain shader remap");
        application->shader_remaps = remaps;
        application->shader_remap_capacity = capacity;
    }
    application->shader_remaps[application->shader_remap_count++] =
        (application_shader_remap){original, replacement, time_ns};
    return true;
}

qa_target_options application_target_options(qa_application *application)
{
    return (qa_target_options){.session = application->session,
                               .context = application,
                               .defer = target_defer,
                               .message = target_message,
                               .remap_shader = target_remap_shader};
}

static bool physics_read(void *opaque, qa_actor_id actor,
                         qa_physics_properties *out)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_MOVEMENT, "");
    if (provider != NULL) {
        if (provider->kind == APPLICATION_PROVIDER_Q1 &&
            qa_q1_game_physics_read(provider->state.q1, actor, out))
            return true;
        if (provider->kind == APPLICATION_PROVIDER_Q2 &&
            qa_q2_physics_read(provider->state.q2, actor, out))
            return true;
        if (provider->kind == APPLICATION_PROVIDER_QC &&
            application_qc_physics_read(provider, actor, out))
            return true;
    }
    if (application_control_physics_read(application, actor, out))
        return true;
    return application->modes != NULL &&
           qa_modes_physics(application->modes, actor, out);
}

static bool physics_write(void *opaque, qa_actor_id actor,
                          const qa_physics_properties *value, qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_MOVEMENT, "");
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_game_physics_write(provider->state.q1, actor, value,
                                        error);
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_physics_write(provider->state.q2, actor, value, error);
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_QC)
        return application_qc_physics_write(provider, actor, value, error);
    if (actor.slot < application->control_capacity) {
        const application_control_record *control =
            &application->controls[actor.slot];
        if (control->active && qa_actor_id_equal(control->actor, actor))
            return application_control_physics_write(application, actor, value,
                                                     error);
    }
    if (application->modes != NULL &&
        qa_modes_physics(application->modes, actor,
                         &(qa_physics_properties){0}))
        return qa_modes_physics_write(application->modes, actor, value, error);
    return application_fail(error, QA_ERROR_NOT_FOUND,
                            "actor has no selected physics owner");
}

static bool physics_touch(void *opaque, const qa_touch_contact *contact,
                          qa_error *error)
{
    qa_application *application = opaque;
    bool accepted = false;
    if (application->modes != NULL) {
        bool handled = false;
        if (!qa_modes_horde_loot_touch(application->modes, contact->self,
                contact->other, &handled, &accepted, error))
            return false;
        if (handled)
            return true;
    }
    if (application->modes != NULL &&
        !qa_modes_touch(application->modes, contact->self, contact->other,
                        &accepted, error))
        return false;
    application_provider *provider = application_provider_for(
        application, contact->self, QA_ROLE_ENTITIES, "");
    if (provider == NULL)
        return true;
    if (provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_game_touch(provider->state.q1, contact, error);
    if (provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_touch(provider->state.q2, contact, error);
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        return qa_q3_touch(provider->state.q3, contact, error);
    if (provider->kind == APPLICATION_PROVIDER_QC)
        return application_qc_touch(provider, contact, error);
    return true;
}

static bool physics_blocked(void *opaque, qa_actor_id actor,
                            qa_actor_id obstacle, qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_ENTITIES, "");
    if (provider == NULL)
        return true;
    if (provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_game_blocked(provider->state.q1, actor, obstacle, error);
    if (provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_entity_blocked(provider->state.q2, actor, obstacle,
                                    error);
    if (provider->kind == APPLICATION_PROVIDER_QC)
        return application_qc_blocked(provider, actor, obstacle, error);
    return true;
}

static qa_game_family provider_family(const application_provider *provider)
{
    if (provider == NULL)
        return QA_GAME_Q2;
    if (provider->kind == APPLICATION_PROVIDER_Q1 ||
        provider->kind == APPLICATION_PROVIDER_QC)
        return QA_GAME_Q1;
    if (provider->kind == APPLICATION_PROVIDER_Q3 ||
        provider->kind == APPLICATION_PROVIDER_QVM ||
        (provider->kind == APPLICATION_PROVIDER_NATIVE &&
         provider->state.native.q3_host != NULL))
        return QA_GAME_Q3;
    return QA_GAME_Q2;
}

static bool physics_event(void *opaque, const qa_physics_event *value,
                          qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(
        application, value->actor, QA_ROLE_EFFECTS, "");
    qa_game_family family = provider_family(provider);
    const char *path = value->kind == QA_PHYSICS_LAND
                           ? (family == QA_GAME_Q1 ? "demon/dland2.wav"
                                                  : "world/land.wav")
                           : "misc/h2ohit1.wav";
    qa_string_id resource;
    qa_builtin_services services =
        application_builtin_services(application, application->world,
                                     application->physics);
    if (!qa_builtin_resource(&services, path, &resource, error))
        return false;
    return application_emit(
        application,
        &(qa_builtin_event){.kind = QA_BUILTIN_SOUND,
                            .family = family,
                            .provider = provider == NULL ? 0 : provider->owner,
                            .actor = value->actor,
                            .time_ns = qa_session_elapsed(application->session),
                            .resource = resource,
                            .origin = value->origin,
                            .volume = 1.0f,
                            .attenuation = 1.0f},
        error);
}

static bool physics_q1_water_transition(void *opaque, qa_actor_id actor,
                                        qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_MOVEMENT, "");
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_game_water_transition(provider->state.q1, actor, error);
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_QC)
        return application_qc_water_transition(provider, actor, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "selected movement source has no Q1 water adapter");
}

static bool physics_accept_ground(void *opaque, qa_actor_id actor,
                                  qa_vec3 destination, bool *accepted,
                                  qa_error *error)
{
    qa_application *application = opaque;
    if (accepted == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "ground policy needs a decision output");
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_MONSTERS, "");
    if (provider == NULL)
        provider = application_provider_for(application, actor,
                                            QA_ROLE_MOVEMENT, "");
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_accept_ground(provider->state.q2, actor, destination,
                                   accepted, error);
    *accepted = true;
    return true;
}

static bool physics_before_monster_step(void *opaque, qa_actor_id actor,
                                        qa_vec3 *displacement, bool *handled,
                                        qa_error *error)
{
    qa_application *application = opaque;
    if (displacement == NULL || handled == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "monster step policy needs mutable outputs");
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_MONSTERS, "");
    if (provider == NULL)
        provider = application_provider_for(application, actor,
                                            QA_ROLE_MOVEMENT, "");
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_before_monster_step(provider->state.q2, actor,
                                         displacement, handled, error);
    *handled = false;
    return true;
}

static bool physics_pusher_think(void *opaque, qa_actor_id actor,
                                 const qa_source_frame *frame,
                                 qa_error *error)
{
    qa_application *application = opaque;
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_ENTITIES, "");
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_game_pusher_think(provider->state.q1, actor, frame, error);
    if (provider != NULL && provider->kind == APPLICATION_PROVIDER_QC)
        return application_qc_pusher_think(provider, actor, frame, error);
    return true;
}

static int physics_source_order(void *opaque, qa_actor_id left,
                                qa_actor_id right)
{
    qa_application *application = opaque;
    const qa_actor_registry *actors = qa_session_actors(application->session);
    const qa_actor_record *a = qa_actors_get(actors, left);
    const qa_actor_record *b = qa_actors_get(actors, right);
    uint32_t a_slot = a != NULL && a->has_source ? a->source_slot : left.slot;
    uint32_t b_slot = b != NULL && b->has_source ? b->source_slot : right.slot;
    if (a_slot != b_slot)
        return a_slot < b_slot ? -1 : 1;
    if (a != NULL && b != NULL && a->owner != b->owner)
        return a->owner < b->owner ? -1 : 1;
    return left.slot < right.slot ? -1 : left.slot != right.slot;
}

static uint32_t physics_random(void *opaque)
{
    return qa_builtin_random_integer(&((qa_application *)opaque)->random);
}

bool application_record_motion_change(qa_application *application,
                                      qa_actor_id actor,
                                      const qa_builtin_motion_change *change,
                                      qa_error *error)
{
    if (change == NULL || (unsigned)change->reason > QA_BUILTIN_MOTION_RESET ||
        actor.slot >= application->motion_capacity ||
        qa_actors_get(qa_session_actors(application->session), actor) == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "motion change needs a live shared actor");
    qa_body_state body;
    if (!qa_world_body_read(application->world, actor, &body, error))
        return false;
    if (!qa_vec_finite(change->view_angles) ||
        !qa_vec_finite(change->angular_kick))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "motion change contains a nonfinite view");
    application_motion_record *record = &application->motion[actor.slot];
    uint64_t revision =
        record->active && qa_actor_id_equal(record->actor, actor)
            ? record->revision
            : 0;
    if (revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY,
                                "motion continuation revision is exhausted");
    uint64_t now = qa_session_elapsed(application->session);
    uint64_t hold_until = UINT64_MAX - now < change->hold_ns
                              ? UINT64_MAX
                              : now + change->hold_ns;
    *record = (application_motion_record){
        .actor = actor,
        .body = body,
        .view_angles = change->view_angles,
        .angular_kick = change->angular_kick,
        .hold_until_ns = hold_until,
        .revision = revision + 1,
        .reason = change->reason,
        .active = true,
        .force_view_angles = change->force_view_angles,
        .apply_angular_kick = change->apply_angular_kick,
    };
    return true;
}

static bool builtin_motion_changed(void *opaque, qa_actor_id actor,
                                   const qa_builtin_motion_change *change,
                                   qa_error *error)
{
    qa_application *application = opaque;
    if (!application_record_motion_change(application, actor, change, error))
        return false;
    if (actor.slot >= application->control_capacity)
        return true;
    application_control_record *control =
        &application->controls[actor.slot];
    return !control->active || !qa_actor_id_equal(control->actor, actor) ||
           application_control_motion_changed(application, actor, change,
                                              error);
}

qa_physics_services application_physics_services(qa_application *application)
{
    return (qa_physics_services){.context = application,
                                 .read = physics_read,
                                 .write = physics_write,
                                 .touch = physics_touch,
                                 .blocked = physics_blocked,
                                 .event = physics_event,
                                 .pusher_think = physics_pusher_think,
                                 .source_order = physics_source_order,
                                 .random_integer = physics_random,
                                 .q1_water_transition =
                                     physics_q1_water_transition,
                                 .accept_ground = physics_accept_ground,
                                 .before_monster_step =
                                     physics_before_monster_step};
}

static uint64_t power_seconds(double seconds)
{
    if (!(seconds > 0.0))
        return 0;
    double nanoseconds = seconds * 1000000000.0;
    if (!isfinite(nanoseconds) || nanoseconds >= (double)UINT64_MAX)
        return UINT64_MAX;
    return (uint64_t)nanoseconds;
}

static uint64_t power_milliseconds(int32_t deadline, uint64_t source_now)
{
    if (!deadline)
        return 0;
    uint32_t now = (uint32_t)(source_now / UINT64_C(1000000));
    uint32_t remaining = (uint32_t)deadline - now;
    if (!remaining || remaining > INT32_MAX)
        return 0;
    uint64_t duration = (uint64_t)remaining * UINT64_C(1000000) -
                        source_now % UINT64_C(1000000);
    return UINT64_MAX - source_now < duration ? UINT64_MAX : source_now + duration;
}

static bool provider_powerups(application_provider *provider, qa_actor_id actor,
                              qa_builtin_powerups *out, bool *present,
                              qa_error *error)
{
    *out = (qa_builtin_powerups){0};
    *present = false;
    if (provider == NULL || !provider->constructed)
        return true;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1: {
        qa_q1_player_view view;
        if (!qa_q1_player_read(provider->state.q1, actor, &view))
            return true;
        out->quad_until_ns = power_seconds(view.power_expires[QA_Q1_QUAD]);
        out->invulnerability_until_ns =
            power_seconds(view.power_expires[QA_Q1_INVULNERABILITY]);
        break;
    }
    case APPLICATION_PROVIDER_Q2: {
        qa_q2_powerups powers;
        if (!qa_q2_powerups_present(provider->state.q2, actor))
            return true;
        qa_error current = {0};
        if (!qa_q2_powerups_read(provider->state.q2, actor, &powers, &current)) {
            if (current.code == QA_ERROR_NOT_FOUND)
                return true;
            if (error != NULL)
                *error = current;
            return false;
        }
        out->quad_until_ns = powers.quad_until_ns;
        out->double_until_ns = powers.double_until_ns;
        out->quad_fire_until_ns = powers.quad_fire_until_ns;
        out->invulnerability_until_ns = powers.invulnerability_until_ns;
        break;
    }
    case APPLICATION_PROVIDER_Q3: {
        qa_q3_player_state view;
        if (!qa_q3_player_read(provider->state.q3, actor, &view))
            return true;
        qa_clock_state clock;
        if (!qa_session_clock(provider->application->session, provider->owner, &clock))
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "Q3 power projection needs its admitted clock");
        out->quad_until_ns = power_milliseconds(view.powerups[QA_Q3_P_QUAD], clock.frame.time_ns);
        if (view.persistent == QA_Q3_P_DOUBLER && !view.dead && !view.spectator)
            out->double_until_ns = UINT64_MAX;
        out->invulnerability_until_ns = power_milliseconds(view.invulnerability_until,
                                                           clock.frame.time_ns);
        break;
    }
    case APPLICATION_PROVIDER_QC:
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        return true;
    }
    *present = true;
    return true;
}

static uint64_t power_translate(uint64_t deadline, uint64_t source_now,
                                uint64_t observer_now)
{
    if (deadline == UINT64_MAX)
        return UINT64_MAX;
    if (deadline <= source_now)
        return 0;
    uint64_t remaining = deadline - source_now;
    return UINT64_MAX - observer_now < remaining ? UINT64_MAX : observer_now + remaining;
}

static bool builtin_powerups(void *opaque, qa_actor_owner observer,
                             qa_actor_id actor, qa_builtin_powerups *out,
                             qa_error *error)
{
    qa_application *application = opaque;
    if (out == NULL ||
        qa_actors_get(qa_session_actors(application->session), actor) == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "power-up projection needs a live actor and output");
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_EFFECTS, "");
    qa_builtin_powerups result;
    bool present;
    if (!provider_powerups(provider, actor, &result, &present, error))
        return false;
    if (!present) {
        provider = application_provider_for(application, actor, QA_ROLE_CHARACTER, "");
        if (!provider_powerups(provider, actor, &result, &present, error))
            return false;
    }
    if (!present) {
        *out = (qa_builtin_powerups){0};
        return true;
    }
    qa_clock_state source_clock, observer_clock;
    if (!qa_session_clock(application->session, provider->owner, &source_clock) ||
        !qa_session_clock(application->session, observer, &observer_clock))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "power-up projection needs admitted source clocks");
    result.quad_until_ns = power_translate(result.quad_until_ns,
        source_clock.frame.time_ns, observer_clock.frame.time_ns);
    result.double_until_ns = power_translate(result.double_until_ns,
        source_clock.frame.time_ns, observer_clock.frame.time_ns);
    result.quad_fire_until_ns = power_translate(result.quad_fire_until_ns,
        source_clock.frame.time_ns, observer_clock.frame.time_ns);
    result.invulnerability_until_ns = power_translate(result.invulnerability_until_ns,
        source_clock.frame.time_ns, observer_clock.frame.time_ns);
    *out = result;
    return true;
}

qa_builtin_services application_builtin_services(qa_application *application,
                                                  qa_world *world,
                                                  qa_physics *physics)
{
    return (qa_builtin_services){.session = application->session,
                                 .world = world,
                                 .combat = application->combat,
                                 .inventory = application->inventory,
                                 .pickups = application->pickups,
                                 .physics = physics,
                                 .context = application,
                                 .emit = application_emit,
                                 .use_targets = builtin_use_targets,
                                 .actor_traits = builtin_traits,
                                 .players = builtin_players,
                                 .player_info = builtin_player_info,
                                 .powerups = builtin_powerups,
                                 .motion_changed = builtin_motion_changed};
}
