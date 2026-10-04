#include "guest_q3_gear_private.h"

bool q3gear_target(application_q3_gear *gear, qa_actor_id actor,
    application_q3_gear_target *out, qa_error *error)
{
    if (!q3gear_current(gear, error) ||
        !qa_actors_get(qa_session_actors(gear->options.host.session), actor))
        return q3gear_fail(error, QA_ERROR_NOT_FOUND, "Separate QVM gear refers to a retired shared actor");
    application_q3_gear_target target; bool found = false;
    if (!gear->options.target(gear->options.context, actor, &target, &found, error)) return false;
    if (!found)
        return q3gear_fail(error, QA_ERROR_NOT_FOUND, "Separate QVM gear has no admitted shared target");
    if (!qa_actor_id_equal(target.actor, actor) ||
        !isfinite(target.health) || target.health < (float)INT32_MIN ||
        (double)target.health > INT32_MAX || !isfinite(target.view_height) ||
        target.view_height < (float)INT32_MIN || (double)target.view_height > INT32_MAX ||
        (target.player && (!target.userinfo || target.team < 0 || target.team > 3)))
        return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear target lost its source health/team contract");
    *out = target; return q3gear_current(gear, error);
}

static bool same_team(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_gear *gear = context;
    int32_t words[2];
    if (!qa_qvm_call_argument(call, 0, &words[0], error) ||
        !qa_qvm_call_argument(call, 1, &words[1], error)) return false;
    qa_actor_id first = q3gear_actor(gear, words[0]), second = q3gear_actor(gear, words[1]);
    if (!first.registry || !second.registry) return qa_qvm_proceed(call, result, error);
    application_q3_gear_target a, b;
    if (!qa_actors_get(qa_session_actors(gear->options.host.session), first) ||
        !qa_actors_get(qa_session_actors(gear->options.host.session), second)) { *result = 0; return true; }
    if (!q3gear_target(gear, first, &a, error) || !q3gear_target(gear, second, &b, error)) return false;
    *result = a.player && b.player && (a.team == 1 || a.team == 2) && a.team == b.team; return true;
}

static bool damage(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_gear *gear = context; int32_t words[8];
    for (size_t i = 0; i < 8; ++i) if (!qa_qvm_call_argument(call, i, &words[i], error)) return false;
    qa_actor_id actor = q3gear_actor(gear, words[0]);
    if (!actor.registry || actor.slot >= gear->capacity ||
        !qa_actor_id_equal(gear->bindings[actor.slot].actor, actor)) return qa_qvm_proceed(call, result, error);
    application_q3_gear_damage request = {.target = actor, .inflictor = q3gear_actor(gear, words[1]),
        .attacker = q3gear_actor(gear, words[2]), .amount = words[5], .flags = words[6], .method = words[7]};
    if ((words[3] && !q3gear_vector(gear, (uint32_t)words[3], &request.direction, error)) ||
        (words[4] && !q3gear_vector(gear, (uint32_t)words[4], &request.point, error)) ||
        !gear->options.damage(gear->options.context, &request, error)) return false;
    application_q3_gear_target target; int32_t health = 0;
    if (qa_actors_get(qa_session_actors(gear->options.host.session), actor)) {
        if (!q3gear_target(gear, actor, &target, error)) return false;
        health = (int32_t)target.health;
    }
    if (!q3gear_store(gear, (uint32_t)words[0] + gear->definition->fields.health, health, error)) return false;
    *result = 0; return true;
}

typedef struct pull_scope {
    application_q3_gear *gear;
    uint32_t scratch;
    size_t word;
    int32_t ground, result;
} pull_scope;

static bool pull_word(void *context, const qa_qvm_call *call, qa_error *error)
{
    pull_scope *scope = context; application_q3_gear *gear = scope->gear;
    if (scope->word == 5) return qa_qvm_proceed(call, &scope->result, error);
    uint32_t address; int32_t value;
    if (scope->word == 0) { address = gear->definition->globals.movement; value = (int32_t)scope->scratch; }
    else if (scope->word == 4) { address = gear->definition->globals.ground_plane; value = scope->ground; }
    else {
        float components[] = {gear->forward.x, gear->forward.y, gear->forward.z};
        address = gear->definition->globals.forward + (uint32_t)(scope->word - 1)*4;
        memcpy(&value, &components[scope->word - 1], sizeof(value));
    }
    ++scope->word;
    bool okay = qa_qvm_source_global_word(call, gear->image, address, value, pull_word, scope, error);
    --scope->word; return okay;
}

static bool pull_scratch(void *context, const qa_qvm_call *call, uint32_t scratch, qa_error *error)
{
    pull_scope *scope = context; application_q3_gear *gear = scope->gear;
    scope->scratch = scratch;
    if (!qa_qvm_fill(gear->vm, scratch, gear->definition->movement_bytes, 0, error)) return false;
    for (size_t i = 0; i < gear->definition->movement_word_count; ++i) {
        application_q3_grapple_word word = gear->definition->movement_words[i];
        if (!q3gear_store(gear, scratch + word.offset, word.value, error)) return false;
    }
    return q3gear_store(gear, scratch, (int32_t)gear->pull_client, error) &&
        q3gear_word(gear, gear->definition->globals.ground_plane, &scope->ground, error) &&
        pull_word(scope, call, error);
}

static bool pull(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_gear *gear = context;
    if (!gear->pull_pending) return qa_qvm_proceed(call, result, error);
    pull_scope scope = {.gear = gear};
    bool okay = qa_qvm_source_scratch(call, gear->image, gear->definition->movement_bytes,
        pull_scratch, &scope, error);
    if (okay) *result = scope.result;
    return okay;
}

typedef struct mover_scope { application_q3_gear *gear; int32_t result; } mover_scope;
static bool mover_scratch(void *context, const qa_qvm_call *call, uint32_t scratch, qa_error *error)
{
    mover_scope *scope = context;
    return q3gear_vector_store(scope->gear, scratch, scope->gear->translation, error) &&
        qa_qvm_call_set_argument(call, 1, (int32_t)scratch, error) &&
        qa_qvm_proceed(call, &scope->result, error);
}

static bool mover(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_gear *gear = context;
    if (!gear->mover_pending) return qa_qvm_proceed(call, result, error);
    mover_scope scope = {.gear = gear};
    bool okay = qa_qvm_source_scratch(call, gear->image, 12, mover_scratch, &scope, error);
    if (okay) *result = scope.result;
    return okay;
}

bool q3gear_bind_hooks(application_q3_gear *gear, qa_error *error)
{
    return qa_qvm_bind_function(gear->vm, gear->definition->callbacks.same_team, true,
        same_team, gear, &gear->same_team, error) &&
        qa_qvm_bind_function(gear->vm, gear->definition->callbacks.damage, true, damage, gear, &gear->damage, error) &&
        qa_qvm_bind_function(gear->vm, gear->definition->callbacks.pull, true, pull, gear, &gear->pull, error) &&
        (!gear->definition->callbacks.move_mover_hooks || qa_qvm_bind_function(gear->vm,
            gear->definition->callbacks.move_mover_hooks, true, mover, gear, &gear->mover, error));
}

size_t q3gear_descriptors(application_q3_gear *gear, qa_qvm_saved_function out[4])
{
    out[0] = (qa_qvm_saved_function){gear->same_team, gear->definition->callbacks.same_team, true, same_team, gear};
    out[1] = (qa_qvm_saved_function){gear->damage, gear->definition->callbacks.damage, true, damage, gear};
    out[2] = (qa_qvm_saved_function){gear->pull, gear->definition->callbacks.pull, true, pull, gear};
    if (gear->definition->callbacks.move_mover_hooks)
        out[3] = (qa_qvm_saved_function){gear->mover, gear->definition->callbacks.move_mover_hooks, true, mover, gear};
    return gear->definition->callbacks.move_mover_hooks ? 4 : 3;
}

bool q3gear_call(application_q3_gear *gear, uint32_t instruction, const int32_t *words,
    size_t count, int32_t *result, qa_error *error)
{
    return q3gear_current(gear, error) && qa_qvm_invoke(gear->vm, instruction, words, count, result, error) &&
        q3gear_current(gear, error);
}
