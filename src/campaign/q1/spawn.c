#include "qa/campaign_q1.h"
#include <math.h>
#include <stdlib.h>

struct qa_q1_spawn_selector {
    qa_q1_spawn_options options;
    qa_q1_spawn_rule *rules;
    qa_builtin_actor_snapshot players, candidates;
    qa_actor_id last;
    bool active;
};
static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static bool live(const qa_q1_spawn_selector *selector, qa_actor_id actor) {
    return qa_actors_get(qa_session_actors(selector->options.services.session), actor) != NULL;
}
qa_q1_spawn_selector *qa_q1_spawn_selector_create(const qa_q1_spawn_options *options,
                                                  qa_error *error) {
    if (!options || !options->services.session || !options->services.world ||
        !options->services.combat || !options->server_flags || !options->random ||
        (options->rule_count && !options->rules) ||
        options->rule_count > SIZE_MAX / sizeof(*options->rules)) {
        fail(error, "Invalid Q1 spawn selector services");
        return NULL;
    }
    qa_strings *strings = qa_session_strings(options->services.session);
    for (size_t i = 0; i < options->rule_count; ++i) {
        const char *name = qa_strings_cstr(strings, options->rules[i].id);
        if (!name || !*name || !options->rules[i].select) {
            fail(error, "Invalid Q1 source spawn rule");
            return NULL;
        }
        for (size_t j = 0; j < i; ++j)
            if (options->rules[i].id == options->rules[j].id) {
                fail(error, "Duplicate Q1 source spawn rule");
                return NULL;
            }
    }
    qa_q1_spawn_selector *selector = calloc(1, sizeof(*selector));
    if (!selector) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 spawn selector");
        return NULL;
    }
    selector->options = *options;
    if (options->rule_count) {
        selector->rules = malloc(options->rule_count * sizeof(*selector->rules));
        if (!selector->rules) {
            free(selector);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "copying Q1 spawn rules");
            return NULL;
        }
        for (size_t i = 0; i < options->rule_count; ++i)
            selector->rules[i] = options->rules[i];
    }
    selector->options.rules = selector->rules;
    return selector;
}
void qa_q1_spawn_selector_destroy(qa_q1_spawn_selector *selector) {
    if (!selector)
        return;
    qa_builtin_snapshot_free(&selector->players);
    qa_builtin_snapshot_free(&selector->candidates);
    free(selector->rules);
    free(selector);
}
qa_actor_id qa_q1_spawn_last(const qa_q1_spawn_selector *selector) { return selector->last; }
bool qa_q1_spawn_restore_last(qa_q1_spawn_selector *selector, qa_actor_id last, qa_error *error) {
    if (!selector || selector->active || (last.registry ? !live(selector, last) : last.slot || last.generation))
        return fail(error, "Invalid Q1 saved spawn point");
    selector->last = last;
    return true;
}
bool qa_q1_spawn_selector_checkpoint_capture(const qa_q1_spawn_selector *selector,
                                              qa_q1_spawn_selector_checkpoint *out,
                                              qa_error *error) {
    if (!selector || !out || selector->active ||
        (!selector->last.registry && (selector->last.slot || selector->last.generation)))
        return fail(error, "Invalid or active Q1 spawn selector checkpoint");
    if (selector->last.registry) {
        qa_saved_actor_id saved;
        if (!qa_actors_save_reference(qa_session_actors(selector->options.services.session),
                                       selector->last, &saved, error))
            return false;
    }
    *out = (qa_q1_spawn_selector_checkpoint){.last = selector->last};
    return true;
}
bool qa_q1_spawn_selector_checkpoint_restore(qa_q1_spawn_selector *selector,
                                              const qa_q1_spawn_selector_checkpoint *checkpoint,
                                              qa_error *error) {
    if (!selector || !checkpoint || selector->active ||
        (!checkpoint->last.registry && (checkpoint->last.slot || checkpoint->last.generation)))
        return fail(error, "Invalid or active Q1 spawn selector restoration");
    if (checkpoint->last.registry) {
        qa_saved_actor_id saved;
        if (!qa_actors_save_reference(qa_session_actors(selector->options.services.session),
                                       checkpoint->last, &saved, error))
            return false;
    }
    selector->last = checkpoint->last;
    return true;
}
static bool player_body(qa_q1_spawn_selector *selector, qa_actor_id actor, bool living,
                        qa_body_state *body, bool *available, qa_error *error) {
    *available = false;
    if (!live(selector, actor))
        return true;
    if (living) {
        qa_combat_state combat;
        qa_error local = {0};
        if (!qa_combat_read(selector->options.services.combat, actor, &combat, &local)) {
            if (local.code == QA_ERROR_NOT_FOUND)
                return true;
            if (error)
                *error = local;
            return false;
        }
        if (combat.health <= 0)
            return true;
    }
    qa_error local = {0};
    if (!qa_world_body_read(selector->options.services.world, actor, body, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND)
            return true;
        if (error)
            *error = local;
        return false;
    }
    *available = true;
    return true;
}
static bool nearby(qa_q1_spawn_selector *selector, qa_actor_id point, float radius, bool living,
                   bool *out, qa_error *error) {
    qa_body_state spawn;
    if (!qa_world_body_read(selector->options.services.world, point, &spawn, error))
        return false;
    *out = false;
    for (size_t i = 0; i < selector->players.count; ++i) {
        qa_body_state body;
        bool available;
        if (!player_body(selector, selector->players.ids[i], living, &body, &available, error))
            return false;
        if (!available)
            continue;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        if (qa_vec_length(qa_vec_sub(center, spawn.origin)) <= radius) {
            *out = true;
            break;
        }
    }
    return true;
}
static bool visible(qa_q1_spawn_selector *selector, qa_actor_id point, bool *out, qa_error *error) {
    qa_trace_policy policy = {.family = QA_COLLISION_Q1,
                              .contents_mask = qa_collision_contents_mask(0, QA_COLLISION_Q1),
                              .q1_move = QA_Q1_MOVE_NO_MONSTERS, .q1_hull = -1};
    qa_body_state spawn;
    if (!qa_world_body_read(selector->options.services.world, point, &spawn, error))
        return false;
    *out = false;
    for (size_t i = 0; i < selector->players.count; ++i) {
        qa_body_state body;
        bool available;
        if (!player_body(selector, selector->players.ids[i], true, &body, &available, error))
            return false;
        if (!available)
            continue;
        qa_trace_query query = {.start = qa_vec_add(spawn.origin, qa_v3(0, 0, 22)),
                                .end = qa_vec_add(body.origin, qa_v3(0, 0, 22)),
                                .shape.kind = QA_SHAPE_POINT,
                                .pass_actor = point,
                                .policy = policy};
        qa_trace_result trace;
        if (!qa_world_trace(selector->options.services.world, &query, &trace, error))
            return false;
        if (trace.fraction >= 1) {
            *out = true;
            break;
        }
    }
    return true;
}
static qa_actor_id first(const qa_q1_spawn_selector *selector, const qa_q1_spawn_point *points,
                         size_t count, qa_q1_spawn_kind kind, size_t start) {
    for (size_t i = start; i < count; ++i)
        if (points[i].kind == kind && live(selector, points[i].actor) &&
            (!selector->options.point_eligible ||
             selector->options.point_eligible(selector->options.point_context, points[i].actor)))
            return points[i].actor;
    return (qa_actor_id){0};
}
static bool random_choice(qa_q1_spawn_selector *selector, qa_actor_id *out, qa_error *error) {
    double random = selector->options.random(selector->options.context);
    if (!isfinite(random) || random < 0 || random > 1)
        return fail(error, "Invalid Q1 spawn random fraction");
    size_t count = selector->candidates.count;
    /* The original rounds over count-1, giving endpoints half the interior weight. */
    size_t index = (size_t)floor(random * (double)(count - 1) + 0.5);
    *out = selector->candidates.ids[index];
    return true;
}
static bool select_point(qa_q1_spawn_selector *selector, const qa_q1_spawn_point *points,
                         size_t count, bool force, qa_actor_id *out, qa_error *error) {
    *out = (qa_actor_id){0};
    for (size_t i = 0; i < selector->options.rule_count; ++i) {
        const qa_q1_spawn_rule *rule = &selector->rules[i];
        qa_q1_spawn_decision decision = QA_Q1_SPAWN_DELEGATE;
        qa_actor_id actor = {0};
        if (!rule->select(rule->context, force, &decision, &actor, error))
            return false;
        if (decision == QA_Q1_SPAWN_DEFERRED)
            return true;
        if (decision == QA_Q1_SPAWN_SELECTED) {
            if (!live(selector, actor))
                return fail(error, "Source spawn rule selected stale actor");
            *out = actor;
            return true;
        }
        if (decision != QA_Q1_SPAWN_DELEGATE)
            return fail(error, "Invalid source spawn decision");
    }
    qa_actor_id point = first(selector, points, count, QA_Q1_SPAWN_TEST, 0);
    if (point.registry) {
        *out = point;
        return true;
    }
    if (selector->options.coop) {
        size_t after = 0;
        for (size_t i = 0; i < count; ++i)
            if (qa_actor_id_equal(points[i].actor, selector->last)) {
                after = i + 1;
                break;
            }
        point = first(selector, points, count, QA_Q1_SPAWN_COOP, after);
        if (!point.registry && selector->options.cycle_coop && after)
            point = first(selector, points, after, QA_Q1_SPAWN_COOP, 0);
        if (!point.registry)
            point = first(selector, points, count, QA_Q1_SPAWN_START, 0);
        if (point.registry) {
            selector->last = *out = point;
            return true;
        }
    } else if (selector->options.deathmatch) {
        qa_builtin_actor_snapshot *candidates = &selector->candidates;
        if (!qa_builtin_snapshot_reserve(candidates, count, error) ||
            !qa_builtin_players(&selector->options.services, &selector->players, error))
            return false;
        candidates->count = 0;
        for (size_t i = 0; i < count; ++i)
            if (points[i].kind == QA_Q1_SPAWN_DEATHMATCH && live(selector, points[i].actor))
                candidates->ids[candidates->count++] = points[i].actor;
        if (!candidates->count)
            return fail(error, "No info_player_deathmatch on level");
        if (!selector->options.rerelease) {
            size_t start = candidates->count - 1;
            for (size_t i = 0; i < candidates->count; ++i)
                if (qa_actor_id_equal(candidates->ids[i], selector->last)) {
                    start = i;
                    break;
                }
            for (size_t i = 0; i < candidates->count; ++i) {
                if (++start == candidates->count)
                    start = 0;
                point = candidates->ids[start];
                bool occupied = false;
                if (!qa_actor_id_equal(point, selector->last) &&
                    !nearby(selector, point, 32, false, &occupied, error))
                    return false;
                if (qa_actor_id_equal(point, selector->last) || !occupied) {
                    selector->last = *out = point;
                    return true;
                }
            }
            if (force)
                *out = candidates->ids[0];
            return true;
        }
        size_t all = candidates->count;
        for (unsigned pass = 0; pass < 2; ++pass) {
            candidates->count = 0;
            for (size_t i = count; i > 0; --i) {
                point = points[i - 1].actor;
                if (points[i - 1].kind != QA_Q1_SPAWN_DEATHMATCH || !live(selector, point))
                    continue;
                bool occupied, seen = false;
                if (!nearby(selector, point, pass == 0 ? 384 : 84, true, &occupied, error))
                    return false;
                if (occupied)
                    continue;
                if (pass == 0 && !visible(selector, point, &seen, error))
                    return false;
                if (!seen)
                    candidates->ids[candidates->count++] = point;
            }
            if (candidates->count)
                return random_choice(selector, out, error);
        }
        if (!force)
            return true;
        candidates->count = 0;
        for (size_t i = 0; i < count; ++i)
            if (points[i].kind == QA_Q1_SPAWN_DEATHMATCH && live(selector, points[i].actor))
                candidates->ids[candidates->count++] = points[i].actor;
        if (candidates->count != all)
            return fail(error, "Spawn candidates mutated during selection");
        return random_choice(selector, out, error);
    }
    if (*selector->options.server_flags)
        point = first(selector, points, count, QA_Q1_SPAWN_RETURN, 0);
    if (!point.registry)
        point = first(selector, points, count, QA_Q1_SPAWN_START, 0);
    if (!point.registry)
        return fail(error, "PutClientInServer: no info_player_start on level");
    *out = point;
    return true;
}
bool qa_q1_spawn_select(qa_q1_spawn_selector *selector, const qa_q1_spawn_point *points,
                        size_t count, bool force, qa_actor_id *out, qa_error *error) {
    if (!out || (count && !points) || selector->active)
        return fail(error, "Invalid or recursive Q1 spawn selection");
    for (size_t i = 0; i < count; ++i)
        if (points[i].kind < QA_Q1_SPAWN_START || points[i].kind > QA_Q1_SPAWN_TEST)
            return fail(error, "Invalid Q1 spawn point kind");
    selector->active = true;
    qa_actor_id selected;
    bool ok = select_point(selector, points, count, force, &selected, error);
    selector->active = false;
    if (ok)
        *out = selected;
    return ok;
}
