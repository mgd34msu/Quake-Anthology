#include "internal.h"
#include "qa/game_q2_source.h"

static bool client_head(qa_q2_game *g, q2_actor *a, float damage, qa_error *e) {
    q2_client_state *s = a->client;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    bool head = q2_random(g) < .5f;
    if (!qa_builtin_resource(&g->services,
                             head ? "models/objects/gibs/head2/tris.md2"
                                  : "models/objects/gibs/skull/tris.md2",
                             &s->visual.models[0], e))
        return false;
    s->visual.skin = head ? 1 : 0;
    s->visual.frame = 0;
    s->visual.effects = 2;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    float scale = damage < 50 ? .7f : 1.2f;
    body.origin.z += 32;
    float vx = q2_crandom(g) * 100 * scale;
    float vy = q2_crandom(g) * 100 * scale;
    float vz = (200 + q2_random(g) * 100) * scale;
    body.velocity = qa_vec_add(body.velocity, qa_v3(vx, vy, vz));
    body.bounds = (qa_bounds){qa_v3(-16, -16, 0), qa_v3(16, 16, 16)};
    a->physics_bound = true;
    a->physics.motion = QA_PHYSICS_BOUNCE;
    a->physics.angular_velocity = qa_v3(0, 0, 0);
    a->physics.solid = rr ? QA_PHYSICS_TRIGGER : QA_PHYSICS_NOT_SOLID;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, a->id, &combat, e))
        return false;
    combat.can_take_damage = rr;
    combat.no_knockback = true;
    a->character_no_damage_effects = rr;
    s->gibbed = true;
    if (!qa_combat_set_traits(g->services.combat, a->id, &combat, e)) return false;
    if (!q2_actor_live(g, a->id)) return true;
    if (!qa_world_body_write(g->services.world, a->id, &body, e)) return false;
    if (!q2_actor_live(g, a->id)) return true;
    qa_actor_collision collision = {.family = QA_COLLISION_Q2, .shape = QA_SHAPE_BOX,
        .contents = qa_collision_q2_source_contents(1, 2u | 8u, rr),
        .role = QA_COLLISION_TRIGGER, .dead_monster = true};
    return qa_world_set_collision(g->services.world, a->id, rr ? &collision : NULL, e);
}
static bool throw_gibs(qa_q2_game *g, q2_actor *a, float damage, qa_error *e) {
    if (!q2_player_sound(g, a->id, "misc/udeath.wav", 4, e))
        return false;
    for (int i = 0; i < 4 && q2_actor_live(g, a->id); i++)
        if (!q2_spawn_gib(g, a->id, "models/objects/gibs/sm_meat/tris.md2", damage, 0, 0, 1, e))
            return false;
    return true;
}
static bool drop_death(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (!g->options.deathmatch || !a->client->use_weapons || !a->weapon_bound)
        return true;
    qa_q2_weapon weapon = a->weapon.weapon;
    const qa_q2_weapon_definition *definition = qa_q2_weapon_definition_at(g, weapon);
    int ammo = 0;
    if (definition && g->ammo[weapon] && !q2_count(g, a->id, g->ammo[weapon], &ammo, e))
        return false;
    qa_item_id item =
        definition && weapon != QA_Q2_BLASTER && (!g->ammo[weapon] || ammo) ? g->items[weapon] : 0;
    qa_q2_powerups powers;
    if (!qa_q2_powerups_read(g, a->id, &powers, e))
        return false;
    bool quad = (g->options.deathmatch_flags & 16384) &&
                powers.quad_until_ns > q2_deadline(g->now_ns, Q2_NS);
    bool quadfire = powers.quad_fire_until_ns > q2_deadline(g->now_ns, Q2_NS);
    if (g->options.edition == QA_Q2_RERELEASE) {
        qa_actor_id id = a->id;
        float no_drop;
        if (!qa_q2_source_value(g, "g_dm_no_quadfire_drop", 0, &no_drop, e)) return false;
        if (!q2_actor_live(g, id)) return true;
        if (no_drop != 0) quadfire = false;
    }
    float spread = item ? (quad ? 22.5f : quadfire ? 12.5f : 0) : 0;
    qa_actor_id dropped;
    bool accepted;
    if (item && !qa_q2_item_drop(g, a->id, item,
                                 &(qa_q2_drop_options){.player_death = true, .yaw_offset = -spread},
                                 &dropped, &accepted, e))
        return false;
    const char *names[] = {"item_quad", "item_quadfire"};
    bool enabled[] = {quad, quadfire};
    uint64_t expires[] = {powers.quad_until_ns, powers.quad_fire_until_ns};
    for (size_t i = 0; i < 2 && q2_actor_live(g, a->id); i++)
        if (enabled[i]) {
            const qa_q2_item_definition *d = qa_q2_item_lookup(g, names[i]);
            if (!d)
                continue;
            if (!q2_item_drop_definition(
                    g, a->id, d,
                    &(qa_q2_drop_options){.player_death = true,
                                          .yaw_offset = spread,
                                          .expires_ns = expires[i],
                                          .immediate_touch =
                                              i == 1 || g->options.edition == QA_Q2_RERELEASE},
                    0, &dropped, e))
                return false;
        }
    return true;
}
bool q2_player_reserve_corpses(qa_q2_game *g, qa_error *e) {
    q2_players *p = g->player_runtime;
    for (size_t i = 0; i < 8; i++)
        if (!q2_actor_live(g, p->corpses[i])) {
            qa_string_id name;
            if (!qa_builtin_resource(&g->services, "bodyque", &name, e))
                return false;
            qa_builtin_spawn spawn = {.owner = g->options.owner,
                                      .definition = name,
                                      .combat = &(qa_combat_state){0}};
            qa_actor_id id;
            if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
                return false;
            q2_actor *corpse = q2_actor_get(g, id, true, e);
            if (!corpse)
                return false;
            corpse->client = calloc(1, sizeof(*corpse->client));
            if (!corpse->client) {
                qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 corpse");
                qa_session_release(g->services.session, id, NULL);
                return false;
            }
            corpse->client->corpse = true;
            p->corpses[i] = id;
        }
    return true;
}
bool q2_player_copy_corpse(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_players *p = g->player_runtime;
    if (!q2_player_reserve_corpses(g, e))
        return false;
    q2_actor *corpse = q2_actor_get(g, p->corpses[p->corpse_index], false, e);
    if (!corpse)
        return false;
    p->corpse_index = (p->corpse_index + 1) % 8;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    qa_body_state body;
    qa_combat_state combat, player;
    if (!qa_world_body_read(g->services.world, a->id, &body, e) ||
        !qa_combat_read_traits(g->services.combat, corpse->id, &combat, e))
        return false;
    if (rr && !qa_combat_read_traits(g->services.combat, a->id, &player, e))
        return false;
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, corpse->id))
        return true;
    if (!qa_world_unlink(g->services.world, a->id, e) ||
        !qa_world_unlink(g->services.world, corpse->id, e))
        return false;
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, corpse->id))
        return true;
    corpse->client->visual = a->client->visual;
    corpse->client->info.slot = a->client->info.slot;
    if (rr) {
        corpse->client->visual.skin &= 255;
        corpse->client->visual.effects = 0;
        corpse->client->visual.render_flags = 0;
        if (!player.can_take_damage)
            body.bounds = (qa_bounds){0};
    }
    corpse->client->info.dead = true;
    corpse->client->gibbed = rr && a->client->gibbed;
    corpse->physics = a->physics;
    corpse->physics_bound = a->physics_bound;
    combat.can_take_damage = true;
    combat.invulnerable = false;
    if (!qa_combat_set_traits(g->services.combat, corpse->id, &combat, e))
        return false;
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, corpse->id))
        return true;
    if (rr && !qa_combat_set_health(g->services.combat, corpse->id, player.health, e))
        return false;
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, corpse->id))
        return true;
    if (!qa_world_body_write(g->services.world, corpse->id, &body, e))
        return false;
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, corpse->id))
        return true;
    qa_actor_collision collision;
    qa_error observed = {0};
    bool has = qa_world_get_collision(g->services.world, a->id, &collision, &observed);
    if (observed.code) {
        if (e)
            *e = observed;
        return false;
    }
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, corpse->id))
        return true;
    if (!qa_world_set_collision(g->services.world, corpse->id, has ? &collision : NULL, e))
        return false;
    if (!q2_actor_live(g, corpse->id))
        return true;
    if (!qa_world_link(g->services.world, corpse->id, NULL, e))
        return false;
    return !q2_actor_live(g, corpse->id) ||
           q2_publish_visual(g, corpse->id, &corpse->client->visual, e);
}
static bool coop_death(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_players *runtime = g->player_runtime;
    qa_builtin_snapshot_frame *players = q2_player_roster(g, e);
    if (!players)
        return false;
    bool all_dead = true, okay = true;
    for (size_t i = 0; i < players->snapshot.count; ++i) {
        qa_actor_id id = players->snapshot.ids[i];
        if (!q2_actor_live(g, id))
            continue;
        qa_combat_state health = {0};
        qa_error missing = {0};
        if (!qa_combat_read(g->services.combat, id, &health, &missing) &&
            missing.code != QA_ERROR_NOT_FOUND) {
            if (e)
                *e = missing;
            okay = false;
            break;
        }
        if (!q2_actor_live(g, id))
            continue;
        q2_actor *native = q2_actor_get(g, id, false, NULL);
        int lives = native && native->client ? native->client->info.lives : 0;
        if (health.health > 0 ||
            (!runtime->deadly_killbox && runtime->rules.coop_lives && lives > 0)) {
            all_dead = false;
            break;
        }
    }
    qa_builtin_snapshot_release(players);
    if (!okay || !q2_actor_live(g, a->id))
        return okay;
    if (!all_dead) {
        a->client->respawn_ns = q2_deadline(g->now_ns, 3 * Q2_NS);
        return true;
    }
    runtime->restart_ns = q2_deadline(g->now_ns, 5 * Q2_NS);
    qa_string_id message;
    if (!qa_builtin_resource(&g->services, "$g_coop_lose", &message, e))
        return false;
    players = q2_player_roster(g, e);
    if (!players)
        return false;
    for (size_t i = 0; i < players->snapshot.count; ++i) {
        qa_actor_id id = players->snapshot.ids[i];
        if (q2_actor_live(g, id) &&
            !qa_builtin_emit(&g->services,
                             &(qa_builtin_event){.kind = QA_BUILTIN_CENTERPRINT,
                                                 .family = QA_GAME_Q2,
                                                 .provider = g->options.owner,
                                                 .actor = id,
                                                 .time_ns = g->now_ns,
                                                 .text = message}, e)) {
            okay = false;
            break;
        }
    }
    qa_builtin_snapshot_release(players);
    return okay;
}
bool q2_player_death(qa_q2_game *g, q2_actor *a, const qa_damage_outcome *outcome, qa_error *e) {
    q2_client_state *s = a->client;
    qa_combat_state combat;
    qa_body_state body;
    if (!qa_combat_read(g->services.combat, a->id, &combat, e) ||
        !qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    float damage = outcome->request.amount;
    bool rr = g->options.edition == QA_Q2_RERELEASE, first = !s->info.dead;
    int means = outcome->request.attack.cause.kind == QA_CAUSE_Q2
                    ? outcome->request.attack.cause.source.q2.means_of_death & ~0x8000000
                    : 0;
    if (s->corpse) {
        bool changed = false;
        /* The compiled rerelease player edict retains zero gib_health from
         * SpawnEntities; its body queue copies that field, unlike player_die's -40. */
        if (combat.health < (rr ? 0 : -40) && !s->gibbed) {
            if (!throw_gibs(g, a, damage, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            body.origin.z -= 48;
            if (!qa_world_body_write(g->services.world, a->id, &body, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            if (!client_head(g, a, damage, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            changed = true;
        }
        if (rr && means == 20) {
            changed = true;
            s->visual.visible = false;
            a->physics_bound = true;
            a->physics.solid = QA_PHYSICS_NOT_SOLID;
            a->physics.motion = QA_PHYSICS_NOCLIP;
            if (!qa_combat_read_traits(g->services.combat, a->id, &combat, e))
                return false;
            combat.can_take_damage = false;
            if (!qa_combat_set_traits(g->services.combat, a->id, &combat, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            if (!qa_world_set_collision(g->services.world, a->id, NULL, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            if (!qa_world_link(g->services.world, a->id, NULL, e))
                return false;
        }
        return !changed || !q2_actor_live(g, a->id) ||
               q2_publish_visual(g, a->id, &s->visual, e);
    }
    qa_q2_player_movement movement;
    if (!q2_player_observe(g, a, &movement, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_q2_player_carry carry = {0};
    if (rr && first && g->options.cooperative &&
        (g->player_runtime->rules.coop_instanced_items ||
         g->player_runtime->rules.coop_squad_respawn) &&
        !qa_q2_player_carry_capture(g, a->id, &carry, e))
        return false;
    if (rr) {
        s->visual.models[1] = s->visual.models[2] = 0;
        if (!q2_player_loop(g, a, 0, e))
            goto fail;
        if (!q2_actor_live(g, a->id))
            goto finish;
        if (means == 51) {
            combat.health = -100;
            damage = 400;
            if (!qa_combat_set_health(g->services.combat, a->id, -100, e))
                goto fail;
        }
    }
    if (first) {
        s->info.dead = true;
        s->respawn_ns = q2_deadline(g->now_ns, Q2_NS);
        qa_actor_id killer = outcome->request.attack.attacker;
        if (!killer.registry || qa_actor_id_equal(killer, a->id))
            killer = outcome->request.attack.inflictor;
        qa_body_state target;
        if (killer.registry && !qa_actor_id_equal(killer, a->id) && q2_actor_live(g, killer)) {
            if (!qa_world_body_read(g->services.world, killer, &target, e))
                goto fail;
            s->killer_yaw = qa_builtin_angle_mod(
                atan2f(target.origin.y - body.origin.y, target.origin.x - body.origin.x) *
                57.29577951308232f);
        } else
            s->killer_yaw = body.angles.y;
        if (!q2_player_obituary(g, a, outcome, e))
            goto fail;
        if (!q2_actor_live(g, a->id))
            goto finish;
        if (!drop_death(g, a, e))
            goto fail;
        if (!q2_actor_live(g, a->id))
            goto finish;
        qa_q2_player_services *services = &g->player_runtime->services;
        if (!s->use_weapons && services->drop_inventory &&
            !services->drop_inventory(services->context, a->id, &outcome->request.attack, e))
            goto fail;
        if (!q2_actor_live(g, a->id))
            goto finish;
        if (services->before_death_inventory &&
            !services->before_death_inventory(services->context, a->id, &outcome->request.attack,
                                              e))
            goto fail;
        if (!q2_actor_live(g, a->id))
            goto finish;
        if (g->options.cooperative && !qa_q2_player_consumed_key(g, a->id, e))
            goto fail;
        bool clear =
            !rr || (g->options.cooperative && !g->player_runtime->rules.coop_instanced_items &&
                    !g->player_runtime->rules.coop_squad_respawn);
        if (clear && !q2_player_inventory_set(g, a->id, NULL, 0, e))
            goto fail;
        s->show_scores = g->options.deathmatch;
    }
    if (!q2_actor_live(g, a->id))
        goto finish;
    if (!q2_player_clear_powerups(g, a, e))
        goto fail;
    if (!q2_actor_live(g, a->id))
        goto finish;
    if (!qa_q2_clear_trackers(g, a->id, e))
        goto fail;
    if (!q2_actor_live(g, a->id))
        goto finish;
    if (g->player_runtime->services.death &&
        !g->player_runtime->services.death(g->player_runtime->services.context, a->id,
                                           &outcome->request.attack, e))
        goto fail;
    if (!q2_actor_live(g, a->id))
        goto finish;
    if (rr && first) {
        s->animation_ns = 0;
        qa_q2_player_rules *rules = &g->player_runtime->rules;
        if (g->options.deathmatch && rules->force_respawn_seconds != 0)
            s->respawn_ns = q2_deadline(g->now_ns, q2_item_seconds(rules->force_respawn_seconds));
        if (carry.inventory || (carry.count == 0 && g->options.cooperative &&
                                   (rules->coop_instanced_items || rules->coop_squad_respawn))) {
            carry.health = carry.maximum_health;
            qa_q2_player_carry_free(&s->coop);
            s->coop = carry;
            carry = (qa_q2_player_carry){0};
            s->has_coop = true;
        }
        if (g->options.cooperative && (rules->coop_squad_respawn || rules->coop_lives)) {
            if (rules->coop_lives && s->info.lives > 0)
                s->info.lives--;
            if (!coop_death(g, a, e))
                goto fail;
        }
    }
    if (!q2_actor_live(g, a->id))
        goto finish;
    if (movement.animate_q2) {
        body.angles = qa_v3(0, body.angles.y, 0);
        body.bounds.maxs.z = -8;
        a->physics_bound = true;
        a->physics.motion = QA_PHYSICS_TOSS;
        a->physics.angular_velocity = qa_v3(0, 0, 0);
        a->physics.flags |= QA_PHYSICS_DEAD;
        if (!qa_world_body_write(g->services.world, a->id, &body, e))
            goto fail;
        qa_combat_state traits;
        if (!qa_combat_read_traits(g->services.combat, a->id, &traits, e))
            goto fail;
        traits.can_take_damage = true;
        if (!qa_combat_set_traits(g->services.combat, a->id, &traits, e))
            goto fail;
        if (combat.health < -40 && !s->gibbed) {
            if (!(rr && means == 47 && combat.health < -80) && !throw_gibs(g, a, damage, e))
                goto fail;
            if (q2_actor_live(g, a->id) && !client_head(g, a, damage, e))
                goto fail;
            if (rr && q2_actor_live(g, a->id)) {
                if (!qa_combat_read_traits(g->services.combat, a->id, &traits, e))
                    goto fail;
                traits.can_take_damage = false;
                if (!qa_combat_set_traits(g->services.combat, a->id, &traits, e))
                    goto fail;
            }
        } else if (first) {
            g->player_runtime->death_animation = (g->player_runtime->death_animation + 1) % 3;
            unsigned index = g->player_runtime->death_animation;
            s->animation_priority = 5;
            s->visual.frame = movement.ducked ? 172 : index == 0 ? 177 : index == 1 ? 183 : 189;
            s->animation_end = movement.ducked ? 177 : index == 0 ? 183 : index == 1 ? 189 : 197;
            char sound[32];
            snprintf(sound, sizeof(sound), "*death%d.wav", (int)(q2_random(g) * 4) + 1);
            if (!q2_player_sound(g, a->id, sound, 2, e))
                goto fail;
        }
        if (q2_actor_live(g, a->id) && (!qa_world_link(g->services.world, a->id, NULL, e) ||
                                        !q2_publish_visual(g, a->id, &s->visual, e)))
            goto fail;
    }
finish:
    qa_q2_player_carry_free(&carry);
    return true;
fail:
    qa_q2_player_carry_free(&carry);
    return false;
}
