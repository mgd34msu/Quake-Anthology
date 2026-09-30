#include "internal.h"
#include <string.h>

bool qa_application_weapon_read(qa_application *application, qa_actor_id actor,
                                  qa_item_id *out, qa_error *error)
{
    if (application == NULL || out == NULL || application->session == NULL ||
        qa_actors_get(qa_session_actors(application->session), actor) == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "weapon view requires a live actor");
    application_provider *provider = application_provider_for(application, actor, QA_ROLE_ARSENAL, "");
    if (provider == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND, "actor has no selected arsenal");
    qa_item_id weapon = 0;
    if (provider->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view state;
        if (!qa_q1_player_read(provider->state.q1, actor, &state))
            return application_fail(error, QA_ERROR_NOT_FOUND, "selected Q1 arsenal has no player state");
        weapon = qa_q1_weapon_item(provider->state.q1, state.weapon);
    } else if (provider->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state state;
        if (!qa_q2_weapon_read(provider->state.q2, actor, &state, error))
            return false;
        const qa_q2_weapon_definition *definition = qa_q2_weapon_definition_at(provider->state.q2, state.weapon);
        const qa_q2_item_definition *item = definition == NULL ? NULL
            : qa_q2_item_lookup(provider->state.q2, definition->item);
        if (item != NULL)
            weapon = item->item;
    } else if (provider->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state state;
        if (!qa_q3_player_read(provider->state.q3, actor, &state))
            return application_fail(error, QA_ERROR_NOT_FOUND, "selected Q3 arsenal has no player state");
        weapon = qa_q3_weapon_item(provider->state.q3, state.weapon, false);
    } else {
        return application_guest_weapon_read(provider, actor, out, error);
    }
    *out = weapon;
    return true;
}

bool application_q2_character_weapon(void *context, qa_actor_id actor,
                                       qa_q2_character_weapon *out, qa_error *error)
{
    application_provider *character = context;
    if (character == NULL || out == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 character weapon view is missing");
    *out = (qa_q2_character_weapon){0};
    application_provider *arsenal = application_provider_for(character->application, actor,
                                                               QA_ROLE_ARSENAL, "");
    if (arsenal == NULL || arsenal->kind != APPLICATION_PROVIDER_Q2)
        return true;
    qa_q2_weapon_state state;
    if (!qa_q2_weapon_read(arsenal->state.q2, actor, &state, error)) return false;
    const qa_q2_weapon_definition *definition =
        qa_q2_weapon_definition_at(arsenal->state.q2, state.weapon);
    qa_item_id ammo = 0;
    if (definition && definition->ammo)
        ammo = qa_strings_find(qa_session_strings(character->application->session),
            (qa_bytes){(const uint8_t *)definition->ammo, strlen(definition->ammo)});
    *out = (qa_q2_character_weapon){.q2_weapon = state.weapon, .ammo = ammo,
        .kick_angles = state.kick_angles, .kick_origin = state.kick_origin,
        .loop_sound = state.loop_sound};
    return true;
}

bool application_q2_weapon_selected(void *context, qa_actor_id actor)
{
    application_provider *provider = context;
    return provider != NULL && provider->kind == APPLICATION_PROVIDER_Q2 &&
           application_provider_for(provider->application, actor, QA_ROLE_ARSENAL, "") == provider;
}

bool application_q2_weapon_input(void *context, qa_actor_id actor,
                                  qa_q2_weapon_input *out, qa_error *error)
{
    application_provider *provider = context;
    if (provider == NULL || provider->kind != APPLICATION_PROVIDER_Q2 || out == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 input needs a native arsenal");
    qa_application *application = provider->application;
    qa_application_control_view control;
    if (!qa_application_control_read(application, actor, &control))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q2 arsenal actor has no controls");
    if (!qa_q2_weapon_controls_read(provider->state.q2, actor, out, error))
        return false;
    qa_builtin_services services = application_builtin_services(application, application->world,
                                                                  application->physics);
    qa_builtin_actor_traits traits = {0};
    if (services.actor_traits != NULL)
        (void)services.actor_traits(services.context, actor, &traits);
    if (!qa_actors_get(qa_session_actors(application->session), actor))
        return true;
    out->angles = control.view_angles;
    out->view_height = control.view_height;
    out->gravity = application->physics->gravity * control.gravity_multiplier;
    out->attack = !control.cutscene && (control.buttons & 1u) != 0;
    out->latched_attack = false;
    out->ducked = control.bounds.maxs.z <
                  application->controls[actor.slot].standing_bounds.maxs.z;
    out->spectator = traits.spectator;
    out->notarget = traits.no_target;
    out->animate_player = application_provider_for(application, actor, QA_ROLE_CHARACTER, "") == provider;
    return true;
}

bool application_arsenal_source_actor(void *context, qa_session *session, qa_actor_id actor,
                                       const qa_source_frame *frame, qa_error *error)
{
    qa_application *application = context;
    if (application == NULL || session != application->session || frame == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "Arsenal turn belongs to another session");
    application_provider *arsenal = application_provider_for(application, actor, QA_ROLE_ARSENAL, "");
    if (arsenal == NULL || arsenal->owner != frame->provider)
        return true;
    if (arsenal->kind == APPLICATION_PROVIDER_Q1) {
        application_provider *movement = application_provider_for(application, actor, QA_ROLE_MOVEMENT, "");
        if (movement == NULL || movement->component.clock.kind == QA_CLOCK_NETQUAKE ||
            movement->component.clock.kind == QA_CLOCK_QUAKEWORLD)
            return true;
        qa_q1_game_operation operation = {0};
        if (!qa_q1_game_operation_begin(arsenal->state.q1, &operation, error)) return false;
        bool ok = qa_q1_player_prethink(arsenal->state.q1, actor, error);
        qa_q1_game_operation_end(&operation);
        return ok;
    }
    if (arsenal->kind != APPLICATION_PROVIDER_Q2)
        return true;
    qa_application_control_view control;
    if (!qa_application_control_read(application, actor, &control))
        return true;
    qa_actor_owner execution = 0;
    qa_q2_player_info character;
    if (qa_session_execution(session, actor, &execution) && execution == arsenal->owner &&
        qa_q2_player_read(arsenal->state.q2, actor, &character) && character.connected)
        return true;
    qa_q2_weapon_input input;
    if (!application_q2_weapon_input(arsenal, actor, &input, error))
        return false;
    if (!qa_actors_get(qa_session_actors(session), actor))
        return true;
    return qa_q2_weapon_tick(arsenal->state.q2, actor, &input, frame->time_ns,
                              frame->elapsed_ns, error);
}
