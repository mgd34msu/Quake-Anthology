#include "internal.h"

bool qa_q2_player_chase(qa_q2_game *g, qa_actor_id id, int direction, bool toggle, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a || (direction != 1 && direction != -1)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 chase direction");
        return false;
    }
    q2_client_state *s = a->client;
    if (toggle && s->info.chase_target.registry)
        s->info.chase_target = (qa_actor_id){0};
    else {
        q2_actor *current = q2_actor_get(g, s->info.chase_target, false, NULL), *best = NULL,
                 *wrap = NULL;
        uint32_t slot = current && current->client ? current->client->info.slot
                        : direction > 0            ? 0
                                                   : UINT32_MAX;
        for (size_t i = 0; i < g->capacity; i++) {
            q2_actor *other = g->actors[i];
            if (!other || !other->client || !other->client->info.connected ||
                other->client->info.spectator)
                continue;
            uint32_t candidate = other->client->info.slot;
            if (!wrap || (direction > 0 ? candidate < wrap->client->info.slot
                                        : candidate > wrap->client->info.slot))
                wrap = other;
            if ((!current || (direction > 0 ? candidate > slot : candidate < slot)) &&
                (!best || (direction > 0 ? candidate < best->client->info.slot
                                         : candidate > best->client->info.slot)))
                best = other;
        }
        if (!best)
            best = wrap;
        s->info.chase_target = best ? best->id : (qa_actor_id){0};
    }
    if (!q2_player_emit(g,
                        &(qa_q2_player_event){.kind = QA_Q2_PLAYER_CHASE,
                                              .actor = id,
                                              .target = s->info.chase_target},
                        e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    return s->info.chase_target.registry
               ? q2_player_update_chase(g, a, e)
               : q2_player_move(
                     g, a, &(qa_q2_player_motion){.kind = QA_Q2_PLAYER_NOCLIP, .enabled = true}, e);
}
bool q2_player_update_chase(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_client_state *s = a->client;
    q2_actor *target = q2_actor_get(g, s->info.chase_target, false, NULL);
    if (!target || !target->client || !target->client->info.connected ||
        target->client->info.spectator) {
        if (s->info.chase_target.registry) {
            s->info.chase_target = (qa_actor_id){0};
            return qa_q2_player_chase(g, a->id, 1, false, e);
        }
        return true;
    }
    qa_body_state body;
    qa_q2_player_movement m;
    if (!qa_world_body_read(g->services.world, target->id, &body, e) ||
        !q2_player_observe(g, target, &m, e))
        return false;
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, target->id))
        return true;
    qa_vec3 eye = qa_vec_add(body.origin, qa_v3(0, 0, target->client->info.view_height)), forward,
            angles = m.view_angles;
    angles.x = fminf(angles.x, 56);
    qa_builtin_angle_vectors(angles, &forward, NULL, NULL);
    qa_vec3 desired = qa_vec_add(eye, qa_vec_scale(forward, -30));
    desired.z = fmaxf(desired.z, body.origin.z + 20) + (m.grounded ? 0 : 16);
    qa_trace_result hit;
    if (!q2_player_trace(g, target->id, eye, desired, NULL, 3, &hit, e))
        return false;
    qa_vec3 goal = qa_vec_add(hit.end, qa_vec_scale(forward, 2));
    if (!q2_player_trace(g, target->id, goal, qa_vec_add(goal, qa_v3(0, 0, 6)), NULL, 3, &hit, e))
        return false;
    if (hit.fraction < 1)
        goal = qa_vec_add(hit.end, qa_v3(0, 0, -6));
    if (!q2_player_trace(g, target->id, goal, qa_vec_add(goal, qa_v3(0, 0, -6)), NULL, 3, &hit, e))
        return false;
    if (hit.fraction < 1)
        goal = qa_vec_add(hit.end, qa_v3(0, 0, 6));
    angles = target->client->info.dead ? qa_v3(-15, target->client->killer_yaw, 40) : m.view_angles;
    s->info.view_height = 0;
    qa_body_state own;
    if (!qa_world_body_read(g->services.world, a->id, &own, e))
        return false;
    own.origin = goal;
    own.velocity = qa_v3(0, 0, 0);
    if (!qa_world_body_write(g->services.world, a->id, &own, e) ||
        !qa_world_link(g->services.world, a->id, NULL, e))
        return false;
    return !q2_actor_live(g, a->id) ||
           q2_player_move(g, a,
                          &(qa_q2_player_motion){
                              .kind = QA_Q2_PLAYER_FREEZE, .origin = goal, .angles = angles},
                          e);
}
static bool hazard(qa_q2_game *g, qa_actor_id id, qa_vec3 point, uint32_t mask, bool *value,
                   qa_error *e) {
    qa_point_contents contents;
    if (!qa_world_point_contents(
            g->services.world,
            &(qa_point_query){.point = point,
                              .pass_actor = id,
                              .policy = {.family = QA_COLLISION_Q2, .q2_merged_contents = true}},
            &contents, e))
        return false;
    *value = (contents.merged & (int32_t)mask) != 0;
    return true;
}
static bool squad_spot(qa_q2_game *g, q2_actor *a, const qa_q2_player_movement *m, qa_vec3 *out,
                       bool *found, qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_trace_result hit;
    uint32_t mask = 0x201001b;
    *found = false;
    if (!q2_player_trace(g, a->id, body.origin, body.origin, &m->standing_bounds, mask, &hit, e))
        return false;
    if (hit.start_solid || hit.all_solid)
        return true;
    static const float yaws[] = {0, 90, 45, -45, -90};
    for (size_t i = 0; i < 5; i++) {
        if (!q2_player_trace(g, a->id, body.origin, qa_vec_add(body.origin, qa_v3(0, 0, 128)),
                             &m->standing_bounds, mask, &hit, e))
            return false;
        bool danger;
        if (!hazard(g, a->id, hit.end, 24, &danger, e))
            return false;
        if (hit.start_solid || hit.all_solid || danger)
            continue;
        qa_vec3 forward;
        qa_builtin_angle_vectors(qa_v3(0, body.angles.y + 180 + yaws[i], 0), &forward, NULL, NULL);
        if (!q2_player_trace(g, a->id, hit.end, qa_vec_add(hit.end, qa_vec_scale(forward, 128)),
                             &m->standing_bounds, mask, &hit, e))
            return false;
        if (!hazard(g, a->id, hit.end, 24, &danger, e))
            return false;
        if (hit.start_solid || hit.all_solid || danger)
            continue;
        if (!q2_player_trace(g, a->id, hit.end, qa_vec_add(hit.end, qa_v3(0, 0, -512)),
                             &m->standing_bounds, mask, &hit, e))
            return false;
        if (!hazard(g, a->id, hit.end, 24, &danger, e))
            return false;
        if (hit.start_solid || hit.all_solid || hit.fraction == 1 ||
            hit.hit != QA_TRACE_HIT_WORLD || danger || !hit.contact ||
            hit.contact_plane.normal.z < .7f)
            continue;
        qa_vec3 goal = hit.end;
        if (!hazard(g, a->id, qa_vec_add(goal, qa_v3(0, 0, 22)), 56, &danger, e))
            return false;
        if (danger)
            continue;
        float height = fabsf(body.origin.z - goal.z);
        if (height > 72)
            continue;
        if (height > 18) {
            if (!q2_player_trace(g, a->id, body.origin, goal, NULL, mask, &hit, e))
                return false;
            if (hit.fraction != 1)
                continue;
            if (!q2_player_trace(g, a->id, qa_vec_add(body.origin, qa_v3(0, 0, 22)),
                                 qa_vec_add(goal, qa_v3(0, 0, 22)), NULL, mask, &hit, e))
                return false;
            if (hit.fraction != 1)
                continue;
        }
        *out = goal;
        *found = true;
        return true;
    }
    return true;
}
bool q2_player_coop_respawn(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_client_state *s = a->client;
    q2_players *p = g->player_runtime;
    qa_q2_respawn_status status = QA_Q2_RESPAWN_READY;
    bool allowed = true;
    if (p->rules.coop_lives && s->info.lives == 0) {
        status = QA_Q2_RESPAWN_NO_LIVES;
        allowed = false;
    } else if (p->rules.coop_squad_respawn) {
        bool living = false, found = false, searching = q2_map_searching(g, (qa_actor_id){0});
        for (size_t i = 0; i < g->capacity; i++) {
            q2_actor *other = g->actors[i];
            if (!other || !other->client || !other->client->info.connected ||
                other->client->info.dead)
                continue;
            qa_combat_state combat;
            if (!qa_combat_read(g->services.combat, other->id, &combat, e))
                return false;
            if (combat.health <= 0)
                continue;
            living = true;
            q2_client_state *candidate = other->client;
            uint64_t firing =
                other->weapon_bound ? other->weapon.last_firing_ns : candidate->last_firing_ns;
            if (candidate->last_damage_ns >= g->now_ns || q2_map_searching(g, other->id) ||
                (searching && firing >= g->now_ns)) {
                status = QA_Q2_RESPAWN_COMBAT;
                continue;
            }
            qa_q2_player_movement m;
            if (!q2_player_observe(g, other, &m, e))
                return false;
            if (!m.grounded_on_world || m.water_level >= 3) {
                status = QA_Q2_RESPAWN_BAD_AREA;
                continue;
            }
            qa_vec3 origin;
            bool clear;
            if (!squad_spot(g, other, &m, &origin, &clear, e))
                return false;
            if (!clear) {
                status = QA_Q2_RESPAWN_BLOCKED;
                continue;
            }
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, other->id, &body, e))
                return false;
            s->squad_spawn = true;
            s->squad_origin = origin;
            s->squad_angles = qa_v3(body.angles.x, body.angles.y, 0);
            found = true;
            break;
        }
        if (living && !found)
            allowed = false;
    }
    if (allowed) {
        status = QA_Q2_RESPAWN_READY;
        s->info.spectator = s->requested_spectator = false;
        s->latched_buttons = 0;
        if (!qa_q2_player_respawn(g, a->id, e))
            return false;
    } else {
        if (status == QA_Q2_RESPAWN_READY)
            status = QA_Q2_RESPAWN_WAITING;
        if (!s->info.spectator) {
            if (!q2_player_copy_corpse(g, a, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            s->info.spectator = s->info.noclip = true;
            s->visual.visible = false;
            s->damage_alpha = s->bonus_alpha = 0;
            qa_combat_state combat;
            if (!qa_combat_read_traits(g->services.combat, a->id, &combat, e))
                return false;
            combat.can_take_damage = false;
            if (!qa_combat_set_traits(g->services.combat, a->id, &combat, e) ||
                !q2_player_collision(g, a, false, e) || !q2_publish_visual(g, a->id, &s->visual, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            if (!q2_player_move(
                    g, a, &(qa_q2_player_motion){.kind = QA_Q2_PLAYER_NOCLIP, .enabled = true},
                    e) ||
                !qa_q2_player_chase(g, a->id, 1, false, e))
                return false;
        }
    }
    if (!q2_actor_live(g, a->id))
        return true;
    return q2_player_emit(g,
                          &(qa_q2_player_event){.kind = QA_Q2_PLAYER_RESPAWN_STATUS,
                                                .actor = a->id,
                                                .respawn_status = status,
                                                .lives = s->info.lives},
                          e);
}
