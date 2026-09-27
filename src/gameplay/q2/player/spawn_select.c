#include "internal.h"

bool q2_player_trace(qa_q2_game *g, qa_actor_id pass, qa_vec3 start, qa_vec3 end,
                     const qa_bounds *bounds, uint32_t mask, qa_trace_result *out, qa_error *e) {
    qa_trace_query q = {.start = start,
                        .end = end,
                        .pass_actor = pass,
                        .shape = {.kind = bounds ? QA_SHAPE_BOX : QA_SHAPE_POINT},
                        .policy = {.family = QA_COLLISION_Q2,
                                   .contents_mask = mask,
                                   .q2_merged_contents = g->options.edition == QA_Q2_RERELEASE}};
    if (bounds)
        q.shape.bounds = *bounds;
    return qa_world_trace(g->services.world, &q, out, e);
}
static float component(qa_vec3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
static qa_vec3 set_component(qa_vec3 v, int axis, float value) {
    if (axis == 0)
        v.x = value;
    else if (axis == 1)
        v.y = value;
    else
        v.z = value;
    return v;
}
bool q2_player_fix_stuck(qa_q2_game *g, qa_actor_id id, qa_vec3 origin, qa_bounds bounds,
                         qa_vec3 *out, bool *found, qa_error *e) {
    qa_trace_result hit;
    if (!q2_player_trace(g, id, origin, origin, &bounds, 0x2010003, &hit, e))
        return false;
    *found = false;
    if (!hit.start_solid) {
        *out = origin;
        *found = true;
        return true;
    }
    static const int axes[] = {2, 2, 0, 0, 1, 1}, signs[] = {1, -1, 1, -1, 1, -1};
    struct candidate {
        qa_vec3 origin;
        float distance;
    } good[6];
    size_t count = 0;
    for (size_t i = 0; i < 6; i++) {
        int axis = axes[i], sign = signs[i], epsilon_axis = -1;
        float epsilon = 0;
        float edge = component(sign < 0 ? bounds.mins : bounds.maxs, axis);
        qa_vec3 start = set_component(origin, axis, component(origin, axis) + edge);
        qa_bounds face = {set_component(bounds.mins, axis, 0), set_component(bounds.maxs, axis, 0)};
        if (!q2_player_trace(g, id, start, start, &face, 0x2010003, &hit, e))
            return false;
        if (hit.start_solid)
            for (int cross = 0; cross < 3 && epsilon_axis < 0; cross++)
                if (cross != axis) {
                    for (int direction = 1; direction >= -1; direction -= 2) {
                        qa_vec3 probe =
                            set_component(start, cross, component(start, cross) + (float)direction);
                        if (!q2_player_trace(g, id, probe, probe, &face, 0x2010003, &hit, e))
                            return false;
                        if (!hit.start_solid) {
                            start = probe;
                            epsilon_axis = cross;
                            epsilon = (float)direction;
                            break;
                        }
                    }
                }
        if (hit.start_solid)
            continue;
        qa_vec3 opposite = set_component(origin, axis,
                                         component(origin, axis) +
                                             component(sign < 0 ? bounds.maxs : bounds.mins, axis));
        if (epsilon_axis >= 0)
            opposite =
                set_component(opposite, epsilon_axis, component(opposite, epsilon_axis) + epsilon);
        if (!q2_player_trace(g, id, start, opposite, &face, 0x2010003, &hit, e))
            return false;
        if (hit.start_solid)
            continue;
        qa_vec3 delta = qa_vec_sub(
            set_component(hit.end, axis, component(hit.end, axis) + (float)sign * .125f), opposite);
        qa_vec3 position = qa_vec_add(origin, delta);
        if (epsilon_axis >= 0)
            position =
                set_component(position, epsilon_axis, component(position, epsilon_axis) + epsilon);
        if (!q2_player_trace(g, id, position, position, &bounds, 0x2010003, &hit, e))
            return false;
        if (!hit.start_solid)
            good[count++] = (struct candidate){position, qa_vec_dot(delta, delta)};
    }
    if (count) {
        size_t selected = 0;
        for (size_t i = 1; i + 1 < count; i++)
            if (good[i].distance < good[selected].distance)
                selected = i;
        *out = good[selected].origin;
        *found = true;
    }
    return true;
}
static qa_vec3 rotate_landmark(qa_vec3 v, qa_vec3 angles) {
    const float radians = .01745329251994329577f;
    float p = angles.x * radians, r = angles.z * radians, y = angles.y * radians;
    qa_vec3 x = qa_v3(v.x, v.y * cosf(p) - v.z * sinf(p), v.y * sinf(p) + v.z * cosf(p));
    qa_vec3 z = qa_v3(x.x * cosf(r) + x.z * sinf(r), x.y, -x.x * sinf(r) + x.z * cosf(r));
    return qa_v3(z.x * cosf(y) - z.y * sinf(y), z.x * sinf(y) + z.y * cosf(y), z.z);
}
static bool range(qa_q2_game *g, qa_actor_id spot, float *out, qa_error *e) {
    qa_body_state spawn;
    if (!qa_world_body_read(g->services.world, spot, &spawn, e))
        return false;
    float closest = 9999999;
    uint32_t cursor = 0;
    const qa_actor_record *r;
    while (qa_actors_next(qa_session_actors(g->services.session), &cursor, &r)) {
        qa_builtin_actor_traits traits = {0};
        if (!g->services.actor_traits ||
            !g->services.actor_traits(g->services.context, r->id, &traits) || !traits.player)
            continue;
        qa_combat_state combat;
        qa_body_state body;
        if (!qa_combat_read(g->services.combat, r->id, &combat, e) ||
            !qa_world_body_read(g->services.world, r->id, &body, e))
            return false;
        if (combat.health > 0)
            closest = fminf(closest, qa_vec_length(qa_vec_sub(body.origin, spawn.origin)));
    }
    *out = closest;
    return true;
}
static bool clear_spawn(qa_q2_game *g, qa_actor_id spot, qa_bounds bounds, bool *clear,
                        qa_error *e) {
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, spot, &b, e))
        return false;
    b.origin.z += 9;
    qa_trace_result hit;
    if (!q2_player_trace(g, spot, b.origin, b.origin, &bounds, 0x42000000, &hit, e))
        return false;
    *clear = !hit.start_solid;
    return true;
}
static bool matching_start(qa_q2_game *g, const char *classname, qa_string_id target, size_t index,
                           qa_actor_id *out) {
    return q2_map_find(g, classname, target, index, out);
}
static bool single_spawn(qa_q2_game *g, qa_string_id target, bool rr, qa_actor_id *out) {
    if (matching_start(g, "info_player_start", target, 0, out))
        return true;
    if (rr && matching_start(g, "info_player_start", 0, 0, out))
        return true;
    return (rr || target == 0) && q2_map_find(g, "info_player_start", UINT32_MAX, 0, out);
}
static bool select_deathmatch(qa_q2_game *g, q2_actor *a, qa_bounds bounds, qa_actor_id *out,
                              qa_error *e) {
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    size_t count = 0;
    qa_actor_id id;
    while (q2_map_find(g, "info_player_deathmatch", UINT32_MAX, count, &id))
        count++;
    const char *names[] = {"info_player_deathmatch", "info_player_team1", "info_player_team2",
                           "info_player_start"};
    size_t first = 0, last = 1;
    if (!count && rr) {
        first = 1;
        last = 3;
        for (size_t name = first; name < last; name++) {
            size_t n = 0;
            while (q2_map_find(g, names[name], UINT32_MAX, n++, &id))
                count++;
        }
        if (!count && q2_map_find(g, names[3], UINT32_MAX, 0, &id)) {
            first = 3;
            last = 4;
            count = 1;
        }
    }
    if (!count) {
        *out = (qa_actor_id){0};
        return true;
    }
    struct spawn {
        qa_actor_id id;
        float distance;
    } *spots = malloc(count * sizeof(*spots));
    if (!spots) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Selecting Q2 spawn");
        return false;
    }
    size_t n = 0;
    bool ok = false;
    for (size_t name = first; name < last; name++)
        for (size_t i = 0; n < count && q2_map_find(g, names[name], UINT32_MAX, i, &id); i++) {
            spots[n].id = id;
            if (!range(g, id, &spots[n].distance, e))
                goto done;
            n++;
        }
    count = n;
    *out = (qa_actor_id){0};
    if (!rr) {
        if (g->options.deathmatch_flags & 512) {
            float best = 0;
            *out = spots[0].id;
            for (size_t i = 0; i < count; i++)
                if (spots[i].distance > best) {
                    best = spots[i].distance;
                    *out = spots[i].id;
                }
        } else {
            size_t closest = SIZE_MAX, second = SIZE_MAX;
            float first_range = 99999, second_range = 99999;
            for (size_t i = 0; i < count; i++)
                if (spots[i].distance < first_range) {
                    first_range = spots[i].distance;
                    closest = i;
                } else if (spots[i].distance < second_range) {
                    second_range = spots[i].distance;
                    second = i;
                }
            size_t available = count > 2 ? count - 2 : count,
                   selection = (size_t)(q2_random(g) * (float)available);
            for (size_t i = 0; i < count; i++) {
                if (count > 2 && (i == closest || i == second))
                    selection++;
                if (selection-- == 0) {
                    *out = spots[i].id;
                    break;
                }
            }
        }
    } else {
        for (size_t i = 1; i < count; i++) {
            struct spawn value = spots[i];
            size_t j = i;
            while (j && spots[j - 1].distance > value.distance) {
                spots[j] = spots[j - 1];
                j--;
            }
            spots[j] = value;
        }
        if (g->player_runtime->rules.spawn_farthest) {
            for (size_t i = count; i > 0; i--) {
                bool clear;
                if (!clear_spawn(g, spots[i - 1].id, bounds, &clear, e))
                    goto done;
                if (clear) {
                    *out = spots[i - 1].id;
                    break;
                }
            }
        } else {
            for (size_t i = count; i > 3; i--) {
                size_t from = i - 1, to = 2 + (size_t)(q2_random(g) * (float)(from - 1));
                struct spawn temp = spots[from];
                spots[from] = spots[to];
                spots[to] = temp;
            }
            for (size_t i = 0; i < count; i++) {
                size_t index = i + 2 < count ? i + 2 : count - 1 - i;
                bool clear;
                if (!clear_spawn(g, spots[index].id, bounds, &clear, e))
                    goto done;
                if (clear) {
                    *out = spots[index].id;
                    break;
                }
            }
        }
        if (!out->registry && a->client->awaiting_respawn &&
            g->now_ns > a->client->respawn_timeout_ns)
            *out = spots[(size_t)(q2_random(g) * (float)count) % count].id;
    }
    ok = true;
done:
    free(spots);
    return ok;
}
bool q2_player_spawn_select(qa_q2_game *g, q2_actor *a, const qa_q2_player_movement *m,
                            const qa_q2_landmark *landmark, qa_body_state *out, bool *found,
                            qa_error *e) {
    q2_players *p = g->player_runtime;
    q2_client_state *s = a->client;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    *found = false;
    if (s->squad_spawn) {
        s->squad_spawn = false;
        *out = (qa_body_state){
            .origin = s->squad_origin, .angles = s->squad_angles, .bounds = m->standing_bounds};
        *found = true;
        return true;
    }
    if (p->services.select_spawn) {
        qa_vec3 origin, angles;
        bool selected = false;
        if (!p->services.select_spawn(p->services.context, a->id, &origin, &angles, &selected, e))
            return false;
        if (selected) {
            *out =
                (qa_body_state){.origin = origin, .angles = angles, .bounds = m->standing_bounds};
            *found = true;
            return true;
        }
    }
    qa_string_id target = 0;
    if (*p->rules.spawn_point &&
        !qa_builtin_resource(&g->services, p->rules.spawn_point, &target, e))
        return false;
    qa_actor_id spot = {0};
    if (g->options.deathmatch) {
        if (!select_deathmatch(g, a, m->standing_bounds, &spot, e))
            return false;
    } else if (g->options.cooperative) {
        if (rr) {
            qa_actor_id first = {0};
            single_spawn(g, target, true, &first);
            for (int pass = 0; pass < 2 && !spot.registry; pass++)
                for (size_t i = 0;; i++) {
                    qa_actor_id candidate = {0};
                    if (i == 0)
                        candidate = first;
                    else if (!matching_start(g, "info_player_coop", target, i - 1, &candidate))
                        break;
                    if (!candidate.registry)
                        continue;
                    qa_body_state b;
                    qa_trace_result hit;
                    if (!qa_world_body_read(g->services.world, candidate, &b, e) ||
                        !q2_player_trace(g, a->id, b.origin, b.origin, &m->standing_bounds,
                                         pass ? 0x2010003 : 0x42010003, &hit, e))
                        return false;
                    if (!hit.start_solid) {
                        spot = candidate;
                        break;
                    }
                    qa_builtin_actor_traits traits = {0};
                    bool player =
                        hit.hit == QA_TRACE_HIT_ACTOR && g->services.actor_traits &&
                        g->services.actor_traits(g->services.context, hit.actor, &traits) &&
                        traits.player;
                    if (pass && player) {
                        spot = candidate;
                        break;
                    }
                    if (!player) {
                        bool clear;
                        qa_vec3 fixed;
                        b.origin.z += 1;
                        if (!q2_player_fix_stuck(g, a->id, b.origin, m->standing_bounds, &fixed,
                                                 &clear, e))
                            return false;
                        if (clear) {
                            spot = candidate;
                            break;
                        }
                    }
                }
            if (!spot.registry && (!p->rules.coop_player_collision ||
                                   (s->awaiting_respawn && g->now_ns > s->respawn_timeout_ns)))
                spot = first;
        } else if (s->info.slot)
            matching_start(g, "info_player_coop", target, s->info.slot - 1, &spot);
    }
    if (!spot.registry && (!rr || (!g->options.deathmatch && !g->options.cooperative)))
        single_spawn(g, target, rr, &spot);
    if (!spot.registry) {
        if (rr && (g->options.deathmatch || g->options.cooperative))
            return true;
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "No Q2 player spawn for configured start point");
        return false;
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, spot, &body, e))
        return false;
    body.velocity = qa_v3(0, 0, 0);
    body.ground = (qa_actor_id){0};
    body.bounds = m->standing_bounds;
    bool from_landmark = false;
    if (landmark && landmark->name) {
        qa_actor_id reference;
        if (q2_map_find(g, NULL, landmark->name, 0, &reference)) {
            qa_body_state b;
            if (!qa_world_body_read(g->services.world, reference, &b, e))
                return false;
            qa_vec3 position =
                qa_vec_add(rotate_landmark(landmark->relative_origin, b.angles), b.origin);
            if (q2_map_flags(g, reference) & 1)
                position.z = body.origin.z;
            bool clear;
            qa_vec3 fixed;
            if (!q2_player_fix_stuck(g, a->id, position, m->standing_bounds, &fixed, &clear, e))
                return false;
            if (clear) {
                body.origin = fixed;
                body.angles = qa_vec_add(landmark->relative_view_angles, b.angles);
                body.velocity = rotate_landmark(landmark->relative_velocity, b.angles);
                from_landmark = true;
            }
        }
    }
    body.origin.z += rr ? (g->options.deathmatch ? 10 : 1) : from_landmark ? 1 : 10;
    body.angles = rr || from_landmark ? qa_v3(body.angles.x / 3, body.angles.y, body.angles.z)
                                      : qa_v3(0, body.angles.y, 0);
    s->landmark_free_fall = from_landmark;
    *out = body;
    *found = true;
    return true;
}
