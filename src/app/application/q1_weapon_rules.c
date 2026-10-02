#include "internal.h"
#include "q1_weapon_rules.h"
#include "native_q1_composition.h"
#include "native_q1_composition_flags.h"
#include "native_q1_composition_rogue.h"
#include "qa/game_q1_bots.h"

static bool physical_composition(qa_application *app, application_provider **out,
    application_provider **rogue, qa_error *error) {
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    *out = NULL;
    *rogue = NULL;
    if (!source || !source->constructed || !source->attached || source->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 weapon rules lost their actual GAME");
    if (source->kind != APPLICATION_PROVIDER_Q1) return true;
    qa_q1_options options;
    double seconds;
    if (!qa_q1_source_respawn_options_read(source->state.q1, &options, &seconds, error)) return false;
    if (options.program == QA_Q1_ROGUE) {
        if (!application_native_q1_composition_current(app, source->owner, QA_MODE_ROGUE, error)) return false;
        *rogue = source;
        return true;
    }
    if (options.program != QA_Q1_CTF) return true;
    if (!application_native_q1_composition_current(app, source->owner, QA_MODE_THREEWAVE, error)) return false;
    *out = source;
    return true;
}

static bool source_live(application_provider *source, qa_actor_id actor) {
    qa_application *app = source->application;
    return source->constructed && source->attached && !source->close_pending &&
           qa_actors_get(qa_session_actors(app->session), actor) &&
           application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == source;
}

static bool begin(application_provider *source, qa_actor_id actor, qa_q1_weapon weapon,
                  qa_q1_game_operation *operation, qa_error *error) {
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 ||
        (unsigned)weapon >= QA_Q1_WEAPON_COUNT || !source_live(source, actor))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q1 weapon rules require the live selected arsenal");
    return qa_q1_game_operation_begin(source->state.q1, operation, error);
}

static bool finish(application_provider *source, qa_actor_id actor,
                   qa_q1_game_operation *operation, bool okay, qa_error *error) {
    if (okay && (!qa_q1_game_operation_live(operation) || !source_live(source, actor)))
        okay = application_fail(error, QA_ERROR_ARGUMENT,
                                 "Q1 weapon rule retired its source or player");
    qa_q1_game_operation_end(operation);
    return okay;
}

static bool rule_read(qa_application *app, size_t index, qa_mode_id *id,
                      qa_mode_source *source, bool native_ctf, bool native_rogue,
                      bool *selected, qa_error *error) {
    *id = app->mode_ids[index];
    qa_mode_view view;
    if (!qa_modes_read(app->modes, *id, &view, error)) return false;
    *source = view.rules.source;
    *selected = view.rules.enabled &&
                ((*source == QA_MODE_ROGUE && !native_rogue) || (*source == QA_MODE_THREEWAVE && !native_ctf));
    return true;
}

static bool quad_active(qa_application *app, qa_actor_id actor, bool *active, qa_error *error) {
    application_provider *effects = application_provider_for(app, actor, QA_ROLE_EFFECTS, "");
    if (!effects || !effects->constructed || effects->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                 "ThreeWave strength has no live selected effects owner");
    qa_clock_state clock;
    if (!qa_session_clock(app->session, effects->owner, &clock))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                 "ThreeWave strength effects have no source clock");
    switch (effects->kind) {
    case APPLICATION_PROVIDER_Q1:
        *active = qa_q1_game_power_expires(effects->state.q1, actor, QA_Q1_QUAD) >
                  (double)clock.frame.time_ns / 1e9;
        return true;
    case APPLICATION_PROVIDER_Q2: {
        qa_q2_powerups powers;
        if (!qa_q2_powerups_read(effects->state.q2, actor, &powers, error)) return false;
        *active = powers.quad_until_ns > clock.frame.time_ns;
        return true;
    }
    case APPLICATION_PROVIDER_Q3: {
        qa_q3_entity_view view;
        if (!qa_q3_entity_read(effects->state.q3, actor, &view, error)) return false;
        *active = (view.powerups & (UINT64_C(1) << QA_Q3_P_QUAD)) != 0;
        return true;
    }
    default:
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                 "Selected guest effects have no Quad timer reader");
    }
}

bool application_q1_weapon_parameters(void *opaque, qa_actor_id actor, qa_q1_weapon weapon,
                                      qa_q1_weapon_parameters *parameters, qa_error *error) {
    application_provider *source = opaque;
    qa_q1_game_operation operation = {0};
    if (!parameters)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 weapon parameters are missing");
    if (!begin(source, actor, weapon, &operation, error)) return false;
    qa_application *app = source->application;
    qa_item_id item = qa_q1_weapon_item(source->state.q1, weapon);
    application_provider *ctf, *rogue;
    bool okay = physical_composition(app, &ctf, &rogue, error);
    for (size_t i = 0; okay && app->modes && i < app->mode_count; ++i) {
        qa_mode_id id;
        qa_mode_source mode;
        bool selected;
        okay = rule_read(app, i, &id, &mode, ctf != NULL, rogue != NULL, &selected, error);
        if (okay && selected && mode == QA_MODE_THREEWAVE)
            okay = qa_modes_haste_weapon(app->modes, id, actor, item, parameters->interval,
                                         &parameters->interval, &parameters->nail_speed, error);
    }
    if (okay && ctf) okay = application_native_q1_ctf_weapon_parameters(ctf, actor, weapon, parameters, error);
    return finish(source, actor, &operation, okay, error);
}

bool application_q1_weapon_observation(void *opaque, qa_actor_id actor, qa_q1_weapon weapon,
                                       qa_q1_weapon_parameters *parameters, qa_error *error) {
    application_provider *source = opaque;
    qa_q1_game_operation operation = {0};
    if (!parameters)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 weapon observation is missing");
    if (!begin(source, actor, weapon, &operation, error)) return false;
    qa_application *app = source->application;
    qa_item_id item = qa_q1_weapon_item(source->state.q1, weapon);
    float nail_speed = parameters->nail_speed;
    application_provider *ctf, *rogue;
    bool okay = physical_composition(app, &ctf, &rogue, error);
    for (size_t i = 0; okay && app->modes && i < app->mode_count; ++i) {
        qa_mode_id id;
        qa_mode_source mode;
        bool selected;
        okay = rule_read(app, i, &id, &mode, ctf != NULL, rogue != NULL, &selected, error);
        if (okay && selected)
            okay = qa_modes_haste_weapon(app->modes, id, actor, item, parameters->interval,
                                         &parameters->interval, &nail_speed, error);
    }
    if (okay && ctf) okay = application_native_q1_ctf_weapon_parameters(ctf, actor, weapon, parameters, error);
    if (okay && rogue) okay = application_native_q1_rogue_attack_delay(rogue, actor,
        weapon, &parameters->interval, true, error);
    return finish(source, actor, &operation, okay, error);
}

bool application_q1_before_fire(void *opaque, qa_actor_id actor, qa_q1_weapon weapon,
                                qa_error *error) {
    application_provider *source = opaque;
    qa_q1_game_operation operation = {0};
    if (!begin(source, actor, weapon, &operation, error)) return false;
    qa_application *app = source->application;
    application_provider *ctf, *rogue;
    bool okay = physical_composition(app, &ctf, &rogue, error);
    for (size_t i = 0; okay && app->modes && i < app->mode_count; ++i) {
        qa_mode_id id;
        qa_mode_source mode;
        bool selected;
        okay = rule_read(app, i, &id, &mode, ctf != NULL, rogue != NULL, &selected, error);
        if (!okay || !selected ||
            !qa_modes_has_relic(app->modes, id, actor, QA_RELIC_STRENGTH)) continue;
        bool quad = false, handled;
        if (mode == QA_MODE_THREEWAVE) okay = quad_active(app, actor, &quad, error);
        if (okay)
            okay = qa_modes_tech_sound(app->modes, id, actor, QA_RELIC_STRENGTH,
                                       quad, false, &handled, error);
        if (okay && (!qa_q1_game_operation_live(&operation) || !source_live(source, actor)))
            okay = application_fail(error, QA_ERROR_ARGUMENT,
                                     "Q1 strength rule retired its source or player");
    }
    if (okay && ctf) okay = application_native_q1_ctf_before_fire(ctf, actor, error);
    if (okay && rogue) okay = application_native_q1_rogue_before_fire(rogue, actor, error);
    return finish(source, actor, &operation, okay, error);
}

bool application_q1_attack_delay(void *opaque, qa_actor_id actor, qa_q1_weapon weapon,
                                 float *delay, qa_error *error) {
    application_provider *source = opaque;
    qa_q1_game_operation operation = {0};
    if (!delay)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 attack delay is missing");
    if (!begin(source, actor, weapon, &operation, error)) return false;
    qa_application *app = source->application;
    qa_item_id item = qa_q1_weapon_item(source->state.q1, weapon);
    application_provider *ctf, *rogue;
    bool okay = physical_composition(app, &ctf, &rogue, error);
    for (size_t i = 0; okay && app->modes && i < app->mode_count; ++i) {
        qa_mode_id id;
        qa_mode_source mode;
        bool selected;
        okay = rule_read(app, i, &id, &mode, ctf != NULL, rogue != NULL, &selected, error);
        if (okay && selected)
            okay = qa_modes_weapon_attack_delay(app->modes, id, actor, item, delay, error);
        if (okay && (!qa_q1_game_operation_live(&operation) || !source_live(source, actor)))
            okay = application_fail(error, QA_ERROR_ARGUMENT,
                                     "Q1 haste rule retired its source or player");
    }
    if (okay && ctf) okay = application_native_q1_ctf_attack_delay(ctf, actor, weapon, delay, error);
    if (okay && rogue) okay = application_native_q1_rogue_attack_delay(rogue, actor, weapon, delay, false, error);
    return finish(source, actor, &operation, okay, error);
}

bool application_q1_nail_fire(void *opaque, qa_actor_id actor, qa_q1_weapon weapon,
                              qa_error *error) {
    application_provider *source = opaque;
    qa_q1_game_operation operation = {0};
    if (weapon != QA_Q1_NAILGUN && weapon != QA_Q1_SUPER_NAILGUN)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 nail rule requires a nail weapon");
    if (!begin(source, actor, weapon, &operation, error)) return false;
    qa_application *app = source->application;
    application_provider *ctf, *rogue;
    bool okay = physical_composition(app, &ctf, &rogue, error);
    for (size_t i = 0; okay && app->modes && i < app->mode_count; ++i) {
        qa_mode_id id;
        qa_mode_source mode;
        bool selected, handled;
        okay = rule_read(app, i, &id, &mode, ctf != NULL, rogue != NULL, &selected, error);
        if (okay && selected && mode == QA_MODE_THREEWAVE)
            okay = qa_modes_tech_sound(app->modes, id, actor, QA_RELIC_HASTE,
                                       false, false, &handled, error);
        if (okay && (!qa_q1_game_operation_live(&operation) || !source_live(source, actor)))
            okay = application_fail(error, QA_ERROR_ARGUMENT,
                                     "Q1 nail haste rule retired its source or player");
    }
    if (okay && ctf) okay = application_native_q1_ctf_nail_fire(ctf, actor, error);
    return finish(source, actor, &operation, okay, error);
}
