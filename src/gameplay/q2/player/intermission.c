#include "internal.h"

bool qa_q2_players_in_intermission(const qa_q2_game *g) {
    return g && g->player_runtime && g->player_runtime->intermission;
}
static bool camera_actor(qa_q2_game *g, q2_actor *a, qa_vec3 origin, qa_vec3 angles, bool entering,
                         qa_error *e) {
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    q2_client_state *s = a->client;
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, a->id, &combat, e))
        return false;
    if (entering && combat.health <= 0) {
        if (!qa_q2_player_respawn(g, a->id, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    s->show_help = false;
    s->show_scores = g->options.deathmatch || (!rr && g->options.cooperative);
    s->damage_alpha = s->bonus_alpha = 0;
    if (!q2_player_loop(g, a, 0, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    s->info.view_height = 0;
    s->visual.visible = false;
    s->visual.effects = 0;
    if (rr) {
        s->visual.models[0] = s->visual.models[1] = s->visual.models[2] = 0;
        if (a->weapon_bound) {
            a->weapon.grenade_blew_up = false;
            a->weapon.grenade_ns = 0;
            a->weapon.view_model = 0;
        }
    }
    if (!q2_player_clear_powerups(g, a, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!q2_player_collision(g, a, false, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    body.origin = origin;
    body.velocity = qa_v3(0, 0, 0);
    if (!qa_world_body_write(g->services.world, a->id, &body, e) ||
        !q2_publish_visual(g, a->id, &s->visual, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!q2_player_move(
            g, a,
            &(qa_q2_player_motion){.kind = QA_Q2_PLAYER_FREEZE, .origin = origin, .angles = angles},
            e))
        return false;
    if (q2_actor_live(g, a->id) && s->show_scores && !q2_player_scoreboard(g, a, true, e))
        return false;
    return true;
}
bool qa_q2_players_camera(qa_q2_game *g, qa_vec3 origin, qa_vec3 angles, bool entering,
                          qa_error *e) {
    if (!g || !qa_vec_finite(origin) || !qa_vec_finite(angles)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 intermission camera");
        return false;
    }
    q2_players *p = g->player_runtime;
    p->camera_origin = origin;
    p->camera_angles = angles;
    p->camera_set = true;
    q2_trace_frame *players = q2_player_roster(g, e);
    if (!players)
        return false;
    bool okay = true;
    for (size_t i = 0; i < players->snapshot.count; i++) {
        qa_actor_id id = players->snapshot.ids[i];
        if (!q2_actor_live(g, id))
            continue;
        q2_actor *a = q2_actor_get(g, id, false, NULL);
        if (a && a->client) {
            if (a->client->info.connected && !camera_actor(g, a, origin, angles, entering, e)) {
                okay = false;
                break;
            }
        } else if (!q2_map_camera_player(g, id, origin, angles, entering, e)) {
            okay = false;
            break;
        }
    }
    players->active = false;
    return okay;
}
bool qa_q2_players_intermission(qa_q2_game *g, const char *map, const qa_q2_landmark *landmark,
                                uint32_t flags, qa_error *e) {
    if (!g || !map) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 intermission");
        return false;
    }
    q2_players *p = g->player_runtime;
    if (p->intermission)
        return true;
    qa_string_id map_id = 0;
    if (*map && !qa_builtin_resource(&g->services, map, &map_id, e))
        return false;
    p->intermission = true;
    p->intermission_flags = flags;
    p->fade_ns = 0;
    p->intermission_ns = g->now_ns;
    p->next_map = map_id;
    p->has_landmark = landmark != NULL;
    if (landmark)
        p->landmark = *landmark;
    p->exit = false;
    bool rr = g->options.edition == QA_Q2_RERELEASE, end_unit = strchr(map, '*') != NULL;
    for (size_t i = 0; i < g->capacity; i++) {
        q2_actor *a = g->actors[i];
        if (!a || !a->client || !a->client->info.connected)
            continue;
        qa_combat_state combat;
        if (!qa_combat_read(g->services.combat, a->id, &combat, e))
            return false;
        if (combat.health <= 0) {
            if (rr && a->client->has_coop &&
                (p->rules.coop_instanced_items || p->rules.coop_squad_respawn))
                a->client->coop.health = a->client->coop.maximum_health;
            if (!qa_q2_player_spawn(g, a->id, true, NULL, e))
                return false;
        }
        if (!q2_actor_live(g, a->id))
            continue;
        if (end_unit && g->options.cooperative)
            for (size_t j = 0; j < qa_q2_item_count(g); j++) {
                const qa_q2_item_definition *d = qa_q2_item_at(g, j);
                if (d->kind != QA_Q2_ITEM_KEY)
                    continue;
                qa_inventory_entry entry;
                qa_error missing = {0};
                if (!qa_inventory_entry_read(g->services.inventory, a->id, d->item, &entry,
                                             &missing)) {
                    if (missing.code == QA_ERROR_NOT_FOUND)
                        continue;
                    if (e)
                        *e = missing;
                    return false;
                }
                entry.count = 0;
                if (!qa_inventory_configure(g->services.inventory, a->id, &entry, NULL, NULL, e))
                    return false;
                if (!q2_actor_live(g, a->id))
                    break;
            }
    }
    if (rr && end_unit && !(flags & 16) &&
        !q2_map_event(
            g, &(qa_q2_map_event){.kind = QA_Q2_MAP_END_UNIT, .resource = map_id, .flags = flags},
            e))
        return false;
    if (!g->options.deathmatch && (!end_unit || (rr && (flags & 16) && (flags & 64)))) {
        p->exit = true;
        return true;
    }
    qa_body_state camera = {.origin = p->camera_origin, .angles = p->camera_angles};
    if (!rr || !p->camera_set) {
        size_t count = 0;
        qa_actor_id spot = {0};
        while (q2_map_find(g, "info_player_intermission", UINT32_MAX, count, &spot))
            count++;
        if (count) {
            size_t choice = rr ? q2_random_bounded(g, 4) : (size_t)(q2_random(g) * 4);
            q2_map_find(g, "info_player_intermission", UINT32_MAX, choice % count, &spot);
        } else if (!q2_map_find(g, "info_player_start", UINT32_MAX, 0, &spot) &&
                   !q2_map_find(g, "info_player_deathmatch", UINT32_MAX, 0, &spot)) {
            qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 intermission has no camera or spawn");
            return false;
        }
        if (!qa_world_body_read(g->services.world, spot, &camera, e))
            return false;
    }
    return qa_q2_players_camera(g, camera.origin, camera.angles, true, e);
}
bool qa_q2_players_finish_camera(qa_q2_game *g, qa_error *e) {
    if (!g) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing Q2 camera provider");
        return false;
    }
    q2_players *p = g->player_runtime;
    p->intermission = true;
    p->intermission_ns = g->now_ns;
    const char *map = qa_strings_cstr(qa_session_strings(g->services.session), p->next_map);
    p->exit = map && *map && !strchr(map, '*');
    return true;
}
bool qa_q2_players_frame(qa_q2_game *g, qa_error *e) {
    if (!g) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing Q2 player provider");
        return false;
    }
    q2_players *p = g->player_runtime;
    if (p->restart_ns && g->now_ns >= p->restart_ns) {
        p->restart_ns = 0;
        return q2_player_emit(
            g, &(qa_q2_player_event){.kind = QA_Q2_PLAYER_RESTART, .text = p->rules.map_name}, e);
    }
    if (!p->intermission || !p->next_map)
        return true;
    if (p->exit && (p->intermission_flags & 32)) {
        if (!p->fade_ns)
            p->fade_ns = q2_deadline(g->now_ns, 1300 * Q2_MS);
        if (g->now_ns < p->fade_ns) {
            float alpha = q2_clamp(1 - (q2_seconds_left(p->fade_ns, g->now_ns) - .3f), 0, 1);
            for (size_t i = 0; i < g->capacity; i++) {
                q2_actor *a = g->actors[i];
                if (a && a->client && a->client->info.connected &&
                    !q2_map_event(g,
                                  &(qa_q2_map_event){.kind = QA_Q2_MAP_SCREEN_BLEND,
                                                     .recipient = a->id,
                                                     .alpha = alpha},
                                  e))
                    return false;
            }
            return true;
        }
        p->intermission_flags &= ~32u;
        p->fade_ns = 0;
    }
    if (!p->exit)
        return true;
    qa_string_id map = p->next_map;
    qa_q2_landmark landmark = p->landmark;
    bool has_landmark = p->has_landmark;
    p->intermission = false;
    p->exit = false;
    for (size_t i = 0; i < g->capacity; i++) {
        q2_actor *a = g->actors[i];
        if (!a || !a->client || !a->client->info.connected)
            continue;
        if (!qa_q2_player_end_frame(g, a->id, e))
            return false;
        if (!q2_actor_live(g, a->id))
            continue;
        if (p->intermission_flags & 8) {
            if (!q2_player_inventory_set(g, a->id, NULL, 0, e) ||
                !qa_combat_set_health(g->services.combat, a->id, 0, e) ||
                !qa_combat_set_armor(g->services.combat, a->id, &(qa_armor){0}, e) ||
                !qa_q2_powerups_clear(g, a->id, e))
                return false;
            a->client->info.god = a->client->info.notarget = a->client->info.flashlight = false;
            a->client->info.selected_item = 0;
            a->client->has_coop = false;
            qa_q2_player_carry_free(&a->client->coop);
            if (a->powers)
                a->powers->power_cubes = 0;
        } else {
            qa_combat_state combat;
            if (!qa_combat_read(g->services.combat, a->id, &combat, e))
                return false;
            float maximum = a->powers ? a->powers->maximum_health : 100;
            if (combat.health > maximum &&
                !qa_combat_set_health(g->services.combat, a->id, maximum, e))
                return false;
        }
    }
    p->intermission_flags &= ~8u;
    const char *destination = qa_strings_cstr(qa_session_strings(g->services.session), map);
    return q2_map_transition(g, (qa_actor_id){0}, has_landmark ? landmark.player : (qa_actor_id){0},
                             map, has_landmark ? &landmark : NULL,
                             destination && strchr(destination, '*'), e);
}
