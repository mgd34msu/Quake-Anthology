#include "internal.h"

static bool post_respawn(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (g->options.edition == QA_Q2_RERELEASE &&
        (a->client->awaiting_respawn || !a->client->visual.visible))
        return true;
    qa_body_state body;
    qa_q2_player_movement movement;
    if (!qa_world_body_read(g->services.world, a->id, &body, e) ||
        !q2_player_observe(g, a, &movement, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!q2_player_move(g, a,
                        &(qa_q2_player_motion){.kind = QA_Q2_PLAYER_SPAWN,
                                               .origin = body.origin,
                                               .velocity = body.velocity,
                                               .angles = movement.view_angles,
                                               .command_angles = movement.command_angles,
                                               .preserve_view_angles = true,
                                               .hold_ns = 112 * Q2_MS,
                                               .spectator = a->client->info.spectator},
                        e))
        return false;
    if (q2_actor_live(g, a->id)) {
        a->client->respawn_ns = g->now_ns;
        a->client->event = 6;
    }
    return true;
}
static bool fresh_inventory(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_client_state *s = a->client;
    if (!q2_player_inventory_set(g, a->id, s->use_inventory ? NULL : s->spawn_inventory,
                                 s->use_inventory ? 0 : s->spawn_count, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (s->use_inventory && !qa_q2_items_admit_player(g, a->id, true, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!qa_combat_set_health(g->services.combat, a->id, 100, e) ||
        !qa_combat_set_armor(g->services.combat, a->id, &(qa_armor){0}, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    q2_power_state *powers = q2_powers(g, a->id, e);
    if (!powers)
        return false;
    powers->maximum_health = 100;
    s->info.selected_item = s->use_inventory ? g->items[QA_Q2_BLASTER] : 0;
    if (s->use_inventory)
        s->pending_start_items = true;
    qa_q2_player_services *services = &g->player_runtime->services;
    if (s->use_inventory && services->persistent_inventory &&
        !services->persistent_inventory(services->context, a->id, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    return true;
}
bool q2_player_start_items(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    q2_client_state *s = a->client;
    if (!s->pending_start_items)
        return true;
    const char *expression = g->player_runtime->rules.start_items;
    if (*expression && !qa_q2_items_start(g, id, expression, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    s->pending_start_items = false;
    qa_inventory_entry *entries = NULL;
    size_t count = 0;
    if (!q2_player_inventory_copy(g, id, &entries, &count, e))
        return false;
    if (!q2_actor_live(g, id)) {
        free(entries);
        return true;
    }
    free(s->spawn_inventory);
    s->spawn_inventory = entries;
    s->spawn_count = count;
    if (g->options.cooperative) {
        qa_q2_player_carry carry = {0};
        if (!qa_q2_player_carry_capture(g, id, &carry, e))
            return false;
        if (!q2_actor_live(g, id)) {
            qa_q2_player_carry_free(&carry);
            return true;
        }
        qa_q2_player_carry_free(&s->coop);
        s->coop = carry;
        s->has_coop = true;
    }
    return true;
}
static bool player_start_items(void *context, qa_actor_id id, qa_error *e) {
    return q2_player_start_items(context, id, e);
}
bool qa_q2_player_start_items(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    return qa_q2_run_actor(g, id, player_start_items, g, e);
}
static bool spawn_completed(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_actor_live(g, id) ? q2_actor_get(g, id, false, NULL) : NULL;
    if (!a || !a->client || !a->client->info.connected || !a->client->spawned ||
        a->client->awaiting_respawn)
        return true;
    qa_q2_player_services *services = &g->player_runtime->services;
    return !services->spawn_completed || services->spawn_completed(services->context, id, e);
}
static bool player_spawn(qa_q2_game *g, qa_actor_id id, bool restore,
    const qa_q2_landmark *landmark, bool complete, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    q2_client_state *s = a->client;
    q2_players *p = g->player_runtime;
    qa_q2_player_movement movement;
    if (!q2_player_observe(g, a, &movement, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    if (landmark) {
        s->pending_landmark = *landmark;
        s->has_pending_landmark = true;
    }
    qa_body_state body;
    qa_vec3 command_view;
    bool found;
    if (!q2_player_spawn_select(g, a, &movement,
                                s->has_pending_landmark ? &s->pending_landmark : NULL, &body,
                                &command_view, &found, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!found) {
        if (!s->awaiting_respawn)
            s->respawn_timeout_ns = q2_deadline(g->now_ns, 3 * Q2_NS);
        s->awaiting_respawn = true;
        s->spawned = false;
        qa_actor_id camera = {0};
        size_t cameras = 0;
        while (q2_map_find(g, "info_player_intermission", UINT32_MAX, cameras, &camera))
            cameras++;
        if (cameras)
            q2_map_find(g, "info_player_intermission", UINT32_MAX,
                        (size_t)(q2_random(g) * 4) % cameras, &camera);
        if (!camera.registry)
            q2_map_find(g, "info_player_start", UINT32_MAX, 0, &camera);
        if (!camera.registry)
            q2_map_find(g, "info_player_deathmatch", UINT32_MAX, 0, &camera);
        qa_body_state view = {0};
        if (!qa_world_body_read(g->services.world, id, &body, e) ||
            (camera.registry && !qa_world_body_read(g->services.world, camera, &view, e)))
            return false;
        body.origin = view.origin;
        body.velocity = qa_v3(0, 0, 0);
        s->info.dead = false;
        s->info.noclip = true;
        s->visual.visible = false;
        if (!q2_player_collision(g, a, false, e) ||
            !qa_world_body_write(g->services.world, id, &body, e) ||
            !q2_publish_visual(g, id, &s->visual, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
        if (!q2_player_move(g, a,
                            &(qa_q2_player_motion){.kind = QA_Q2_PLAYER_FREEZE,
                                                   .origin = body.origin,
                                                   .angles = view.angles},
                            e))
            return false;
        return !q2_actor_live(g, id) || qa_world_link(g->services.world, id, NULL, e);
    }
    bool was_waiting = s->awaiting_respawn;
    if (a->character_birth_epoch == UINT64_MAX) {
        qa_error_set(e, QA_ERROR_ARGUMENT, id.slot, "Q2 character birth epoch exhausted");
        return false;
    }
    s->awaiting_respawn = false;
    s->respawn_timeout_ns = 0;
    s->has_pending_landmark = false;
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, id, &combat, e))
        return false;
    if (restore) {
        if (g->options.cooperative && s->has_coop) {
            int32_t score;
            if (!q2_player_score_read(g, id, &score, e))
                return false;
            if (!q2_actor_live(g, id))
                return true;
            s->coop.score = score > s->coop.score ? score : s->coop.score;
            if (!qa_q2_player_carry_restore(g, id, &s->coop, e))
                return false;
            if (rr && s->coop.health <= 0) {
                if (!fresh_inventory(g, a, e))
                    return false;
                s->info.god = s->info.notarget = false;
                s->auto_shield_enabled = false;
                if (a->powers)
                    a->powers->power_cubes = 0;
            }
        } else if (g->options.deathmatch || combat.health <= 0) {
            if (!fresh_inventory(g, a, e))
                return false;
        }
    }
    if (!q2_actor_live(g, id))
        return true;
    ++a->character_birth_epoch;
    s->info.dead = s->gibbed = false;
    s->old_water = 0;
    s->air_ns = q2_deadline(g->now_ns, 12 * Q2_NS);
    s->drown_damage = 2;
    s->damage_alpha = s->bonus_alpha = s->damage_blood = s->damage_armor = s->damage_power =
        s->damage_knockback = 0;
    s->fall_ns = 0;
    s->bob_time = s->bob_move = 0;
    s->animation_priority = 0;
    s->animation_end = 39;
    s->info.spectator = s->requested_spectator;
    s->info.noclip = s->info.spectator;
    s->info.chase_target = (qa_actor_id){0};
    s->player_collision = !rr || !g->options.cooperative || p->rules.coop_player_collision;
    s->old_velocity = qa_v3(0, 0, 0);
    s->info.view_height = 22;
    s->visual.frame = 0;
    s->visual.old_frame = -1;
    s->visual.effects = 0;
    s->visual.render_flags = rr ? 32768 : 0;
    s->visual.visible = !s->info.spectator;
    s->visual.alpha = 1;
    if (!q2_player_clear_powerups(g, a, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!qa_combat_read_traits(g->services.combat, id, &combat, e))
        return false;
    combat.can_take_damage = !s->info.spectator;
    combat.no_knockback = false;
    a->character_no_damage_effects = false;
    combat.mass = 200;
    combat.invulnerable = s->info.god;
    if (!qa_combat_set_traits(g->services.combat, id, &combat, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (movement.animate_q2) {
        char model[320];
        size_t length = strcspn(s->info.skin, "/");
        if (!length)
            snprintf(model, sizeof(model), "players/male/tris.md2");
        else
            snprintf(model, sizeof(model), "players/%.*s/tris.md2", (int)length, s->info.skin);
        if (!qa_builtin_resource(&g->services, model, &s->visual.models[0], e))
            return false;
        a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
        a->physics_bound = true;
        a->physics.q2_rerelease = rr;
        a->physics.motion = QA_PHYSICS_STATIONARY;
        a->physics.flags = QA_PHYSICS_PLAYER;
        a->physics.solid = s->info.spectator ? QA_PHYSICS_NOT_SOLID : QA_PHYSICS_BOX;
        a->physics.clip_mask = rr && g->options.cooperative && !p->rules.coop_player_collision
                                   ? 0x2010003
                                   : 0x42010003;
    }
    if (!qa_world_body_write(g->services.world, id, &body, e) ||
        !q2_player_collision(g, a, !s->info.spectator, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    qa_q2_player_motion change = {.kind = QA_Q2_PLAYER_SPAWN,
                                  .origin = body.origin,
                                  .velocity = body.velocity,
                                  .angles = body.angles,
                                  .command_angles = movement.command_angles,
                                  .command_view_angles = command_view,
                                  .has_command_view_angles = true,
                                  .spectator = s->info.spectator};
    if (!q2_player_move(g, a, &change, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!s->info.spectator) {
        bool clear;
        if (!q2_killbox(g, id, id, true, false, &clear, e))
            return false;
    }
    if (!q2_actor_live(g, id))
        return true;
    if (s->use_weapons && a->weapon_bound) {
        qa_q2_weapon weapon = g->options.deathmatch           ? QA_Q2_BLASTER
                              : s->has_coop && s->coop.weapon ? s->coop.weapon
                              : a->weapon.weapon              ? a->weapon.weapon
                                                              : QA_Q2_BLASTER;
        qa_q2_weapon_state reset = {
            .weapon = weapon, .phase = QA_Q2_ACTIVATING, .gun_rate = 10, .kick_seconds = .2f};
        if (!qa_q2_weapon_restore(g, id, &reset, e) || !qa_q2_weapon_silencer(g, id, 0, e))
            return false;
    }
    if (movement.animate_q2 && !q2_publish_visual(g, id, &s->visual, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!qa_world_link(g->services.world, id, NULL, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (p->services.spawned && !p->services.spawned(p->services.context, id, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (restore && !q2_player_start_items(g, id, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (rr && p->services.player_collision &&
        !p->services.player_collision(p->services.context, id, s->player_collision, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    s->spawned = true;
    if (rr) {
        s->slime_ns = s->animation_ns = s->invisibility_fade_ns = 0;
        s->slow_view_angles = qa_v3(0, 0, 0);
        if (!qa_q2_entities_player_reset(g, id, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
        if (!q2_player_emit(
                g,
                &(qa_q2_player_event){.kind = QA_Q2_PLAYER_FLASHLIGHT,
                                      .actor = id,
                                      .hand = s->hand,
                                      .visible = s->info.flashlight && !p->intermission},
                e))
            return false;
    }
    if (!q2_actor_live(g, id))
        return true;
    if (rr && !g->options.deathmatch && q2_player_map_is(p->rules.map_name, "rboss") &&
        s->use_inventory) {
        const qa_q2_item_definition *key = qa_q2_item_lookup(g, "key_nuke");
        if (key &&
            !qa_inventory_configure(g->services.inventory, id,
                                    &(qa_inventory_entry){key->item, 1, 1, QA_COUNT_SOURCE_INT32},
                                    NULL, NULL, e))
            return false;
    }
    if (!q2_actor_live(g, id)) return true;
    if (was_waiting && !post_respawn(g, a, e)) return false;
    return !complete || spawn_completed(g, id, e);
}
bool qa_q2_player_spawn(qa_q2_game *g, qa_actor_id id, bool restore,
    const qa_q2_landmark *landmark, qa_error *e) {
    return player_spawn(g, id, restore, landmark, true, e);
}
bool qa_q2_player_map_spawn_pose(qa_q2_game *g, qa_actor_id id, const qa_bounds *bounds,
    const qa_q2_landmark *landmark, qa_body_state *out, bool *found, qa_error *e) {
    if (!bounds || !out || !found) {
        qa_error_set(e, QA_ERROR_ARGUMENT, id.slot, "Q2 map spawn requires selected standing bounds");
        return false;
    }
    q2_actor *a = q2_client(g, id, e);
    if (!a) return false;
    q2_client_state *s = a->client;
    if (landmark) {
        s->pending_landmark = *landmark;
        s->has_pending_landmark = true;
    }
    qa_q2_player_movement movement = {.standing_bounds = *bounds};
    if (!q2_player_spawn_select(g, a, &movement,
            s->has_pending_landmark ? &s->pending_landmark : NULL, out, NULL, found, e)) return false;
    if (!q2_actor_live(g, id)) return true;
    if (!*found) {
        if (!s->awaiting_respawn) s->respawn_timeout_ns = q2_deadline(g->now_ns, 3 * Q2_NS);
        s->awaiting_respawn = true;
        s->spawned = false;
    } else {
        s->awaiting_respawn = false;
        s->respawn_timeout_ns = 0;
        s->has_pending_landmark = false;
    }
    return true;
}
bool qa_q2_player_map_spawn_complete(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a) return false;
    if (!a->client->info.spectator) {
        bool clear;
        if (!q2_killbox(g, id, id, true, false, &clear, e)) return false;
    }
    if (!q2_actor_live(g, id)) return true;
    a = q2_client(g, id, e);
    if (!a) return false;
    a->client->spawned = true;
    return true;
}
bool qa_q2_player_respawn(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    if (!g->options.deathmatch && !g->options.cooperative)
        return q2_player_emit(g, &(qa_q2_player_event){.kind = QA_Q2_PLAYER_LOAD_MENU, .actor = id},
                              e);
    if ((g->options.edition == QA_Q2_RERELEASE ? !a->client->info.spectator
                                               : !a->client->info.noclip) &&
        !q2_player_copy_corpse(g, a, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!player_spawn(g, id, true, NULL, false, e))
        return false;
    if (!q2_actor_live(g, id) || a->client->awaiting_respawn)
        return true;
    return post_respawn(g, a, e) && spawn_completed(g, id, e);
}
bool qa_q2_player_teleport(qa_q2_game *g, qa_actor_id id, qa_vec3 origin, qa_vec3 angles,
                           qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a || !qa_vec_finite(origin) || !qa_vec_finite(angles))
        return false;
    qa_q2_player_movement m;
    if (!q2_player_observe(g, a, &m, e))
        return false;
    a->client->event = 6;
    return q2_player_move(g, a,
                          &(qa_q2_player_motion){.kind = QA_Q2_PLAYER_TELEPORT,
                                                 .origin = origin,
                                                 .angles = angles,
                                                 .command_angles = m.command_angles,
                                                 .hold_ns = 160 * Q2_MS,
                                                 .spectator = a->client->info.spectator},
                          e);
}
