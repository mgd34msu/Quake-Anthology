#include "../entities/internal.h"

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
static bool range(qa_q2_game *g, qa_actor_id spot, const qa_builtin_actor_snapshot *players,
                  float *out, qa_error *e) {
    qa_body_state spawn;
    if (!qa_world_body_read(g->services.world, spot, &spawn, e))
        return false;
    float closest = 9999999;
    for (size_t i = 0; i < players->count; i++) {
        qa_actor_id player = players->ids[i];
        if (!q2_actor_live(g, player))
            continue;
        qa_combat_state combat;
        qa_body_state body;
        if (!qa_combat_read(g->services.combat, player, &combat, e))
            return false;
        if (combat.health <= 0)
            continue;
        if (!qa_world_body_read(g->services.world, player, &body, e))
            return false;
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
                           qa_actor_id *out, qa_error *e) {
    *out = (qa_actor_id){0};
    qa_target_cursor cursor = {0};
    qa_actor_id id;
    qa_targets *targets = g->entity_runtime->services.targets;
    while (qa_targets_next_authored(targets, classname, &cursor, &id)) {
        qa_authored_target fields;
        if (!qa_targets_read(targets, id, &fields))
            continue;
        bool same;
        if (!q2_player_same_target(g, target, fields.targetname, &same, e))
            return false;
        if (same && index-- == 0) {
            *out = id;
            return true;
        }
    }
    return true;
}
static bool single_spawn(qa_q2_game *g, qa_string_id target, bool rr, qa_actor_id *out,
                         qa_error *e) {
    if (!matching_start(g, "info_player_start", target, 0, out, e))
        return false;
    if (out->registry)
        return true;
    if (rr && !matching_start(g, "info_player_start", 0, 0, out, e))
        return false;
    if (!out->registry && (rr || target == 0))
        q2_map_find(g, "info_player_start", UINT32_MAX, 0, out);
    return true;
}
static bool select_deathmatch(qa_q2_game *g, q2_actor *a, qa_bounds bounds, qa_actor_id *out,
                              qa_error *e) {
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    size_t count = 0;
    qa_actor_id id = {0};
    qa_targets *targets = g->entity_runtime->services.targets;
    qa_target_cursor cursor = {0};
    while (qa_targets_next_authored(targets, "info_player_deathmatch", &cursor, &id))
        count++;
    const char *names[] = {"info_player_deathmatch", "info_player_team1", "info_player_team2",
                           "info_player_start"};
    size_t first = 0, last = 1;
    if (!count && rr) {
        first = 1;
        last = 3;
        for (size_t name = first; name < last; name++) {
            cursor = (qa_target_cursor){0};
            while (qa_targets_next_authored(targets, names[name], &cursor, &id))
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
        if (rr) {
            qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 rerelease has no valid spawn points");
            return false;
        }
        return true;
    }
    bool force = a->client->awaiting_respawn && g->now_ns > a->client->respawn_timeout_ns;
    if (rr && count == 1) {
        bool clear = force;
        if (!force && !clear_spawn(g, id, bounds, &clear, e))
            return false;
        *out = clear ? id : (qa_actor_id){0};
        return true;
    }
    struct spawn {
        qa_actor_id id;
        float distance;
        size_t source_order;
    } *spots = malloc(count * sizeof(*spots));
    if (!spots) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Selecting Q2 spawn");
        return false;
    }
    size_t n = 0;
    bool ok = false;
    qa_builtin_snapshot_frame *players = q2_player_roster(g, e);
    if (!players)
        goto done;
    for (size_t name = first; name < last; name++) {
        cursor = (qa_target_cursor){0};
        while (n < count && qa_targets_next_authored(targets, names[name], &cursor, &id)) {
            spots[n].id = id;
            spots[n].source_order = n;
            if (!range(g, id, &players->snapshot, &spots[n].distance, e))
                goto done;
            n++;
        }
    }
    count = n;
    *out = (qa_actor_id){0};
    if (!count) {
        if (rr)
            qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 rerelease has no live spawn points");
        else
            ok = true;
        goto done;
    }
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
        if (!out->registry && force) {
            size_t selected = (size_t)(q2_random(g) * (float)count);
            if (selected >= count)
                selected = count - 1;
            for (size_t i = 0; i < count; i++)
                if (spots[i].source_order == selected) {
                    *out = spots[i].id;
                    break;
                }
        }
    }
    ok = true;
done:
    if (players)
        qa_builtin_snapshot_release(players);
    free(spots);
    return ok;
}
static bool trace_player(qa_q2_game *g, const qa_trace_result *hit) {
    qa_builtin_actor_traits traits = {0};
    return hit->hit == QA_TRACE_HIT_ACTOR && g->services.actor_traits &&
           g->services.actor_traits(g->services.context, hit->actor, &traits) && traits.player;
}
static bool coop_trace(qa_q2_game *g, qa_actor_id player, qa_vec3 origin, qa_bounds bounds,
                       const qa_builtin_actor_snapshot *excluded, qa_trace_result *hit,
                       qa_error *e) {
    qa_trace_query query = {.start = origin,
                            .end = origin,
                            .pass_actor = player,
                            .shape = {.kind = QA_SHAPE_BOX, .bounds = bounds},
                            .policy = {.family = QA_COLLISION_Q2,
                                       .q2_merged_contents = true,
                                       .contents_mask = excluded ? 0x2010003 : 0x42010003}};
    return excluded ? qa_world_trace_excluding(g->services.world, &query, excluded->ids,
                                               excluded->count, hit, e)
                    : qa_world_trace(g->services.world, &query, hit, e);
}
static bool lava_spawn(qa_q2_game *g, qa_actor_id *out, qa_error *e) {
    float top = -99999, height = 999999;
    *out = (qa_actor_id){0};
    qa_targets *targets = g->entity_runtime->services.targets;
    qa_target_cursor cursor = {0};
    qa_actor_id id;
    while (qa_targets_next_authored(targets, "func_water", &cursor, &id)) {
        if (!(q2_map_flags(g, id) & 2))
            continue;
        qa_body_state body;
        qa_point_contents contents;
        if (!qa_world_body_read(g->services.world, id, &body, e))
            return false;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f));
        if (!qa_world_point_contents(g->services.world,
                                     &(qa_point_query){.point = center,
                                                       .policy = {.family = QA_COLLISION_Q2,
                                                                  .contents_mask = UINT32_MAX,
                                                                  .q2_merged_contents = true}},
                                     &contents, e))
            return false;
        if (contents.merged & 56)
            top = fmaxf(top, body.origin.z + body.bounds.maxs.z);
    }
    if (top == -99999)
        return true;
    qa_builtin_snapshot_frame *players = q2_player_roster(g, e);
    if (!players)
        return false;
    bool okay = false;
    cursor = (qa_target_cursor){0};
    for (size_t i = 0;
         i < 64 && qa_targets_next_authored(targets, "info_player_coop_lava", &cursor, &id); i++) {
        qa_body_state body;
        float distance;
        if (!qa_world_body_read(g->services.world, id, &body, e))
            goto done;
        if (body.origin.z < top + 64 || body.origin.z >= height)
            continue;
        if (!range(g, id, &players->snapshot, &distance, e))
            goto done;
        if (distance > 32) {
            *out = id;
            height = body.origin.z;
        }
    }
    okay = true;
done:
    qa_builtin_snapshot_release(players);
    return okay;
}
static bool select_coop(qa_q2_game *g, q2_actor *a, qa_bounds bounds, qa_string_id target,
                        qa_actor_id *out, qa_error *e) {
    if (q2_player_map_is(g->player_runtime->rules.map_name, "rmine2"))
        return lava_spawn(g, out, e);
    qa_actor_id first = {0}, match = {0};
    if (!single_spawn(g, target, true, &first, e) ||
        !matching_start(g, "info_player_coop", target, 0, &match, e))
        return false;
    qa_string_id coop_target = match.registry ? target : 0;
    qa_builtin_snapshot_frame *players = q2_player_roster(g, e);
    if (!players)
        return false;
    bool okay = false;
    for (int pass = 0; pass < 2 && !out->registry; pass++)
        for (size_t i = 0;; i++) {
            qa_actor_id candidate = first;
            if (i && !matching_start(g, "info_player_coop", coop_target, i - 1, &candidate, e))
                goto done;
            if (!candidate.registry) {
                if (i)
                    break;
                else
                    continue;
            }
            qa_body_state body;
            qa_trace_result hit;
            if (!qa_world_body_read(g->services.world, candidate, &body, e))
                goto done;
            const qa_builtin_actor_snapshot *excluded = pass ? &players->snapshot : NULL;
            if (!coop_trace(g, a->id, body.origin, bounds, excluded, &hit, e))
                goto done;
            if (hit.start_solid && !trace_player(g, &hit)) {
                body.origin.z += 1;
                if (!coop_trace(g, a->id, body.origin, bounds, excluded, &hit, e))
                    goto done;
            }
            if (hit.start_solid && !trace_player(g, &hit)) {
                bool fixed;
                qa_vec3 origin;
                if (!q2_player_fix_stuck(g, a->id, body.origin, bounds, &origin, &fixed, e))
                    goto done;
                if (!fixed)
                    continue;
                if (!coop_trace(g, a->id, origin, bounds, excluded, &hit, e))
                    goto done;
            }
            if (hit.fraction == 1 || (pass && trace_player(g, &hit))) {
                *out = candidate;
                break;
            }
        }
    if (!out->registry &&
        (!g->player_runtime->rules.coop_player_collision ||
         (a->client->awaiting_respawn && g->now_ns > a->client->respawn_timeout_ns)))
        *out = first;
    okay = true;
done:
    qa_builtin_snapshot_release(players);
    return okay;
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
    if (!g->entity_runtime->services.targets) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 spawn selection requires shared map targets");
        return false;
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
            if (!select_coop(g, a, m->standing_bounds, target, &spot, e))
                return false;
        } else if (s->info.slot &&
                   !matching_start(g, "info_player_coop", target, s->info.slot - 1, &spot, e))
            return false;
    }
    if (!spot.registry && (!rr || (!g->options.deathmatch && !g->options.cooperative)) &&
        !single_spawn(g, target, rr, &spot, e))
        return false;
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
        if (q2_entity_pick(g, landmark->name, &reference)) {
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
