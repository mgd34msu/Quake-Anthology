#include "internal.h"
#include "qa/game_q1_composition.h"

bool qa_q1_source_captures_read(const qa_q1_game *game, double *red, double *blue,
    qa_error *error) {
    if (!game || !red || !blue || game->destroy_pending || game->continuation_pending ||
        game->options.program != QA_Q1_CTF) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "ThreeWave captures require the live source-client owner");
        return false;
    }
    *red = game->source_captures[0];
    *blue = game->source_captures[1];
    return true;
}
bool qa_q1_source_capture_add(qa_q1_game *game, bool blue, double *total, qa_error *error) {
    if (!total || !game || game->options.program != QA_Q1_CTF) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "ThreeWave capture requires its actual source and result");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    *total = ++game->source_captures[blue ? 1 : 0];
    qa_q1_game_operation_end(&operation);
    return true;
}
bool qa_q1_source_random(qa_q1_game *game, double *out, qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source random draw requires an output");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    *out = q1_random(game);
    qa_q1_game_operation_end(&operation);
    return true;
}
bool qa_q1_source_damage(qa_q1_game *game, qa_actor_id target, qa_actor_id inflictor,
    qa_actor_id attacker, float amount, const char *cause, qa_error *error) {
    if (!game || !cause || !isfinite(amount)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source damage requires its real GAME and cause");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    qa_damage_request request = {.target = target, .amount = amount, .knockback = amount};
    /* Reflection is a new source request, not a replay of the inflictor's
     * projectile attack. The donor submits no weapon for this direct damage. */
    request.attack = q1_attack(game, attacker, (qa_actor_id){0}, QA_Q1_WEAPON_COUNT);
    request.attack.inflictor = inflictor;
    bool okay = qa_strings_intern_cstr(qa_session_strings(game->services.session), cause,
        &request.attack.cause.source.q1.death_type, error);
    if (okay && q1_damageable(game, target)) {
        okay = qa_attack_next(&game->attack_sequence, &request.attack, error);
        if (okay && game->host.combat_provider)
            request.attack.combat_provider = game->host.combat_provider(game->host.context, target);
        qa_body_state target_body, source_body;
        if (okay) {
            request.point = qa_world_body_read(game->services.world, target, &target_body, NULL)
                ? target_body.origin : qa_v3(0, 0, 0);
            qa_vec3 source_origin = qa_world_body_read(game->services.world, inflictor, &source_body, NULL)
                ? source_body.origin : request.point;
            request.direction = qa_vec_normalize(qa_vec_sub(request.point, source_origin));
            okay = qa_q1_game_operation_live(&operation);
            if (!okay) qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                "Source damage lost its GAME while preparing the request");
        }
        if (okay) {
            qa_damage_outcome outcome = {0};
            okay = qa_combat_apply(game->services.combat, &request, &outcome, error);
            qa_damage_outcome_free(&outcome);
        }
    }
    if (okay && !qa_q1_game_operation_live(&operation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source damage retired its retained GAME");
        okay = false;
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}
