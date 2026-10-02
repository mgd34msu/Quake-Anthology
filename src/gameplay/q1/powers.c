#include "boss_internal.h"
#include "maps/internal.h"

bool q1_message(qa_q1_game *g, qa_actor_id actor, const char *text, qa_error *error) {
    return q1_message_args(g, actor, text, NULL, 0, error);
}
bool q1_message_args(qa_q1_game *g, qa_actor_id actor, const char *text,
                     const qa_builtin_message_arg *arguments, size_t count, qa_error *error) {
    if (g->destroy_pending)
        return true;
    if (!text[0])
        return true;
    qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = actor,
                              .time_ns = g->time_ns,
                              .arguments = arguments,
                              .argument_count = count};
    return qa_builtin_resource(&g->services, text, &event.text, error) &&
           qa_builtin_emit(&g->services, &event, error);
}

bool qa_q1_player_auto_switch(qa_q1_game *g, qa_actor_id actor, qa_q1_auto_switch setting,
                              qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || setting < QA_Q1_SWITCH_ALWAYS || setting > QA_Q1_SWITCH_NEVER) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "invalid Q1 pickup selection preference");
        return false;
    }
    player->auto_switch = setting;
    return true;
}

static const struct {
    qa_q1_weapon base, powered;
    qa_q1_ammo ammo;
    const char *message;
} combos[] = {
    {QA_Q1_NAILGUN, QA_Q1_LAVA_NAILGUN, QA_Q1_LAVA_NAILS, "$qc_lava_enabled"},
    {QA_Q1_SUPER_NAILGUN, QA_Q1_LAVA_SUPER_NAILGUN, QA_Q1_LAVA_NAILS, "$qc_super_lava_enabled"},
    {QA_Q1_GRENADE, QA_Q1_MULTI_GRENADE, QA_Q1_MULTI_ROCKETS, "$qc_multi_gl_enabled"},
    {QA_Q1_ROCKET, QA_Q1_MULTI_ROCKET, QA_Q1_MULTI_ROCKETS, "$qc_multi_rl_enabled"},
    {QA_Q1_LIGHTNING, QA_Q1_PLASMA, QA_Q1_PLASMA_CELLS, "$qc_plasma_enabled"}};
bool q1_enable_combos(qa_q1_game *g, q1_player *player, qa_error *error) {
    if (g->options.program != QA_Q1_ROGUE || !player->arsenal)
        return true;
    for (size_t i = 0; i < sizeof(combos) / sizeof(*combos); ++i) {
        qa_inventory_entry base, powered;
        if (!qa_inventory_entry_read(g->services.inventory, player->id, g->weapons[combos[i].base],
                                     &base, NULL) ||
            base.count <= 0 || q1_ammo_count(g, player->id, combos[i].ammo) <= 0)
            continue;
        if (!qa_inventory_entry_read(g->services.inventory, player->id,
                                     g->weapons[combos[i].powered], &powered, error))
            return false;
        if (powered.count > 0)
            continue;
        double given;
        if (!q1_alive(g, player->id))
            return true;
        if (!qa_inventory_give(g->services.inventory, player->id, powered.item, 1, &given, error))
            return false;
        if (!q1_alive(g, player->id))
            return true;
        if (given && !q1_message(g, player->id, combos[i].message, error))
            return false;
        if (!q1_alive(g, player->id))
            return true;
    }
    return true;
}
static bool combo_player_current(qa_q1_game *g, qa_actor_id actor,
    const q1_player *player, qa_error *error) {
    if (g && !g->continuation_pending && player && q1_player_get(g, actor) == player &&
        player->arsenal) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Selected Q1 combo lost its actual arsenal player");
    return false;
}
bool q1_enable_combos_read(qa_q1_game *g, qa_actor_id actor, q1_player *player,
    qa_error *error) {
    if (!combo_player_current(g, actor, player, error)) return false;
    if (g->options.program != QA_Q1_ROGUE) return true;
    for (size_t i = 0; i < sizeof(combos) / sizeof(*combos); ++i) {
        qa_inventory_entry powered, base, ammo;
        if (!qa_inventory_entry_read(g->services.inventory, actor, g->weapons[combos[i].powered],
                &powered, error) || !combo_player_current(g, actor, player, error)) return false;
        if (powered.count != 0) continue;
        if (!qa_inventory_entry_read(g->services.inventory, actor, g->weapons[combos[i].base],
                &base, error) || !combo_player_current(g, actor, player, error)) return false;
        if (base.count <= 0) continue;
        if (!qa_inventory_entry_read(g->services.inventory, actor, g->ammo[combos[i].ammo],
                &ammo, error) || !combo_player_current(g, actor, player, error)) return false;
        if (ammo.count <= 0) continue;
        double given;
        if (!qa_inventory_give(g->services.inventory, actor, powered.item, 1, &given, error) ||
            !combo_player_current(g, actor, player, error) ||
            !q1_message(g, actor, combos[i].message, error) ||
            !combo_player_current(g, actor, player, error)) return false;
    }
    return true;
}
qa_q1_weapon q1_combo_weapon(qa_q1_game *g, q1_player *player, qa_q1_weapon weapon) {
    if (g->options.program == QA_Q1_ROGUE)
        for (size_t i = 0; i < sizeof(combos) / sizeof(*combos); ++i)
            if (weapon == combos[i].base && q1_ammo_count(g, player->id, combos[i].ammo) >=
                                                (weapon == QA_Q1_SUPER_NAILGUN ? 2 : 1))
                return combos[i].powered;
    return weapon;
}

static bool notify_motion(qa_q1_game *g, qa_actor_id actor, const qa_body_state *body,
                           qa_error *error) {
    if (!q1_alive(g, actor))
        return true;
    if (!g->services.motion_changed) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 timed movement effect requires selected movement continuation");
        return false;
    }
    qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_LAUNCH, .body = *body};
    return g->services.motion_changed(g->services.context, actor, &change, error);
}
static bool timer_current(qa_q1_game *game, qa_actor_id actor, q1_player *player,
    qa_error *error) {
    if (!game->destroy_pending && !game->continuation_pending &&
        q1_player_get(game, actor) == player) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 mission timer lost its actual player");
    return false;
}
bool q1_power_frame(qa_q1_game *g, q1_player *player, double seconds,
    uint64_t frame_ns, qa_error *error) {
    static const struct {
        qa_q1_power power;
        const char *warning, *lost, *sound;
    } timers[] = {{QA_Q1_WETSUIT, "$qc_wetsuit_fade", "", "items/suit2.wav"},
                  {QA_Q1_EMPATHY, "$qc_empathy_fade", "", "items/suit2.wav"},
                  {QA_Q1_SHIELD, "$qc_shield_failing", "$qc_shield_lost", "shield/fadeout.wav"},
                  {QA_Q1_ANTIGRAV, "$qc_antigrav_failing", "$qc_antigrav_lost", "belt/fadeout.wav"},
                  {QA_Q1_LAVA_SUIT, "$mg3_qc_lavasuit_wearing_out", "", "items/suit2.wav"}};
    qa_actor_id actor = player->id;
    if (!q1_enable_combos(g, player, error))
        return false;
    if (!q1_alive(g, player->id))
        return true;
    if (!timer_current(g, actor, player, error)) return false;
    for (size_t i = 0; i < sizeof(timers) / sizeof(*timers); ++i) {
        qa_q1_power power = timers[i].power;
        double expires = player->power_expires[power];
        if (!player->power_order[power])
            continue;
        if (expires < seconds + 3 && !(player->power_warned & (1u << power))) {
            if (!q1_message(g, actor, timers[i].warning, error) ||
                !timer_current(g, actor, player, error) ||
                !q1_sound(g, actor, timers[i].sound, 0, 1, error) ||
                !timer_current(g, actor, player, error))
                return false;
            player->power_warned |= (uint16_t)(1u << power);
        }
        if (!q1_alive(g, player->id))
            return true;
        if (expires < seconds + 3 && player->power_flash[power] < seconds) {
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, actor, &body, error) ||
                !timer_current(g, actor, player, error) ||
                !q1_effect(g, QA_BUILTIN_ITEM, actor, body.origin, 0, 0, error) ||
                !timer_current(g, actor, player, error))
                return false;
            player->power_flash[power] = seconds + 1;
        }
        if (!q1_alive(g, player->id))
            return true;
        if (expires <= seconds && !(player->power_lost & (1u << power))) {
            if (!q1_message(g, actor, timers[i].lost, error) ||
                !timer_current(g, actor, player, error)) return false;
            player->power_lost |= (uint16_t)(1u << power);
            if (power == QA_Q1_ANTIGRAV) {
                if (!g->host.set_gravity) {
                    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                        "Q1 anti-gravity expiry requires selected gravity owner");
                    return false;
                }
                if (!g->host.set_gravity(g->host.context, actor, 1, error) ||
                    !timer_current(g, actor, player, error)) return false;
            }
        }
    }
    if (!q1_alive(g, player->id))
        return true;
    uint8_t water =
        player->source_client || player->arsenal ? player->input.water_level
                                                : player->character_state.input.water_level;
    if (g->services.physics && g->services.physics->services.read) {
        qa_physics_properties physics;
        if (g->services.physics->services.read(g->services.physics->services.context, player->id,
                                               &physics))
            water = (uint8_t)physics.water_level;
    }
    if (!q1_alive(g, player->id))
        return true;
    if (player->power_expires[QA_Q1_WETSUIT] > seconds) {
        player->air_finished = player->character_state.air_until = seconds + 12;
        if (water >= 2) {
            if (player->scuba_at < seconds) {
                player->scuba_at = seconds + 7;
                if (!q1_sound(g, player->id, "misc/wetsuit.wav", 4, 1, error))
                    return false;
                if (!q1_alive(g, player->id))
                    return true;
            }
            if (!player->wetsuit_scaled_level || player->wetsuit_scaled_frame != frame_ns) {
                qa_actor_id receiver = player->id;
                qa_body_state body;
                if (!qa_world_body_read(g->services.world, receiver, &body, error))
                    return !q1_player_get(g, receiver);
                player = q1_player_get(g, receiver);
                if (!player)
                    return true;
                body.velocity = qa_vec_scale(body.velocity, water == 2 ? 1.25f : 1.5f);
                uint64_t scaled_frame = frame_ns;
                if (!qa_world_body_write(g->services.world, receiver, &body, error))
                    return false;
                player = q1_player_get(g, receiver);
                if (!player)
                    return true;
                player->wetsuit_scaled_frame = scaled_frame;
                player->wetsuit_scaled_level = water;
                if (!notify_motion(g, receiver, &body, error))
                    return false;
                player = q1_player_get(g, receiver);
                if (!player)
                    return true;
            }
        }
    }
    q1_actor *entity = q1_entity(g, player->id);
    if (entity)
        entity->effects = player->power_expires[QA_Q1_EMPATHY] > seconds ? entity->effects | 8
                                                                         : entity->effects & ~8u;
    return true;
}
static bool player_after_physics(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->wetsuit_scaled_level || player->wetsuit_scaled_frame != g->time_ns)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return !q1_player_get(g, actor);
    player = q1_player_get(g, actor);
    if (!player)
        return true;
    body.velocity = qa_vec_scale(body.velocity, player->wetsuit_scaled_level == 2 ? 0.8f : 0.66f);
    if (!qa_world_body_write(g->services.world, actor, &body, error))
        return false;
    player = q1_player_get(g, actor);
    if (player)
        player->wetsuit_scaled_level = 0;
    return notify_motion(g, actor, &body, error);
}
bool qa_q1_player_after_physics(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = player_after_physics(g, actor, error);
    if (!qa_q1_game_operation_live(&operation)) {
        if (ok || (error && error->code == QA_OK))
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                         "Q1 teardown requested during player after physics");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}

static bool shield_hit(qa_q1_game *g, q1_player *player, qa_error *error) {
    if (player->shield_until <= g->time) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, player->id, &body, error))
            return false;
        q1_actor *shield;
        if (!q1_create(g, "power_shield", Q1_TIMER, player->id, &shield, error) ||
            !q1_model(g, shield, "progs/p_shield.mdl", error))
            return false;
        shield->delay = (float)(g->time + 0.3);
        player->shield_until = shield->delay;
        body.bounds = (qa_bounds){0};
        body.velocity = qa_v3(0, 0, 0);
        if (!qa_world_body_write(g->services.world, shield->id, &body, error) ||
            !q1_link(g, shield, error) || !q1_schedule(g, shield, 0.1, Q1_THINK_SHIELD, error))
            return false;
    }
    if (player->shield_sound_at < g->time) {
        player->shield_sound_at = g->time + 0.5;
        return q1_sound(g, player->id, "shield/hit.wav", 3, 1, error);
    }
    return true;
}
bool qa_q1_game_damage_effect(qa_q1_game *g, qa_damage_effect_stage stage,
                              const qa_damage_request *request, qa_damage_effect *effect,
                              qa_error *error) {
    if (!g || !request || !effect) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 damage effect call");
        return false;
    }
    if (stage == QA_DAMAGE_BEFORE_QUAD && !request->radius &&
        q1_map_radius_only(g, request->target)) {
        effect->allowed = false;
        return true;
    }
    if (stage == QA_DAMAGE_LETHAL_HEALTH && effect->allowed && effect->amount <= 0 &&
        q1_map_mg3_buddha(g, request->target)) {
        effect->amount = 1;
        effect->reaction = QA_REACTION_NONE;
        return true;
    }
    if (!q1_major_boss_effect(g, stage, request, effect, error))
        return false;
    q1_player *player = q1_player_get(g, request->target);
    if (!player || !effect->allowed)
        return true;
    if (stage == QA_DAMAGE_BEFORE_QUAD) {
        if (player->power_expires[QA_Q1_WETSUIT] != 0 &&
            request->attack.cause.kind == QA_CAUSE_Q1) {
            qa_bytes cause = qa_strings_text(qa_session_strings(g->services.session),
                                             request->attack.cause.source.q1.death_type);
            if (cause.size == 9 && !memcmp(cause.data, "discharge", 9)) {
                effect->allowed = false;
                return true;
            }
        }
        if (player->power_expires[QA_Q1_SHIELD] != 0 && q1_alive(g, request->attack.inflictor)) {
            qa_body_state body, incoming;
            if (!qa_world_body_read(g->services.world, request->target, &body, error) ||
                !qa_world_body_read(g->services.world, request->attack.inflictor, &incoming, error))
                return false;
            qa_vec3 delta = qa_vec_sub(incoming.origin, body.origin), forward;
            qa_builtin_angle_vectors(body.angles, &forward, NULL, NULL);
            float angle = qa_builtin_angle_mod(atan2f(delta.y, delta.x) * 57.29577951308232f) -
                          qa_builtin_angle_mod(atan2f(forward.y, forward.x) * 57.29577951308232f);
            if (!(angle > 90 && angle < 270) && !(angle < -90 && angle > -270)) {
                effect->amount *=
                    q1_classnamed(g, request->attack.inflictor, "lava_spike") ? 0.7f : 0.3f;
                return shield_hit(g, player, error);
            }
        }
    } else if (stage == QA_DAMAGE_AFTER_QUAD) {
        qa_actor_id attacker = request->attack.attacker;
        if (attacker.registry)
            player->killer = attacker;
        q1_player *inflictor = q1_player_get(g, request->attack.inflictor);
        if (attacker.registry && !qa_actor_id_equal(attacker, request->target) &&
            player->power_expires[QA_Q1_EMPATHY] != 0 &&
            (!inflictor || inflictor->power_expires[QA_Q1_EMPATHY] == 0)) {
            qa_string_id cause;
            effect->amount *= 0.5f;
            if (!qa_builtin_resource(&g->services, "hipnotic:empathy", &cause, error))
                return false;
            return !q1_alive(g, attacker) ||
                   q1_damage_typed(g, attacker, request->target, request->target, effect->amount,
                                   QA_Q1_WEAPON_COUNT, QA_Q1_ARMOR_NORMAL, cause, error);
        }
    }
    return true;
}

bool q1_sphere_pickup(qa_q1_game *g, q1_actor *item, qa_actor_id actor, bool *taken,
                      qa_error *error) {
    *taken = false;
    q1_player *player = q1_player_allocate(g, actor, error);
    if (!player)
        return false;
    qa_inventory_entry entry;
    if (!qa_inventory_entry_read(g->services.inventory, actor, g->vengeance_item, &entry, NULL)) {
        entry = (qa_inventory_entry){
            .item = g->vengeance_item, .capacity = 1, .policy = QA_COUNT_SOURCE_FLOAT};
        if (!qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, error))
            return false;
    }
    double given;
    if (!qa_inventory_give(g->services.inventory, actor, g->vengeance_item, 1, &given, error))
        return false;
    if (!given)
        return true;
    q1_actor *sphere;
    if (!q1_create(g, "Vengeance", Q1_PROJECTILE, actor, &sphere, error) ||
        !q1_model(g, sphere, "progs/sphere.mdl", error))
        return false;
    sphere->state.projectile.kind = Q1_VENGEANCE;
    sphere->state.projectile.expires = g->time + 30;
    sphere->state.projectile.activator = sphere->id;
    sphere->state.projectile.attack = q1_attack(g, sphere->id, sphere->id, QA_Q1_WEAPON_COUNT);
    if (!qa_builtin_resource(&g->services, "rogue:vengeance",
                             &sphere->state.projectile.attack.cause.source.q1.death_type, error))
        return false;
    sphere->physics.motion = QA_PHYSICS_FLY_MISSILE;
    sphere->physics.solid = QA_PHYSICS_NOT_SOLID;
    sphere->physics.angular_velocity = qa_v3(40, 40, 40);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, item->id, &body, error))
        return false;
    body.bounds = (qa_bounds){0};
    body.velocity = qa_v3(0, 0, 0);
    if (!qa_world_body_write(g->services.world, sphere->id, &body, error) ||
        !q1_schedule(g, sphere, 0.1, Q1_THINK_SPHERE_ORBIT, error))
        return false;
    *taken = true;
    return true;
}
static bool sphere_attack(qa_q1_game *g, q1_actor *sphere, qa_error *error) {
    sphere->physics.solid = QA_PHYSICS_TRIGGER;
    if (!q1_link(g, sphere, error))
        return false;
    if (!q1_alive(g, sphere->id))
        return true;
    qa_actor_id enemy = sphere->state.projectile.enemy;
    if (!q1_alive(g, enemy) || q1_health(g, enemy) < 1) {
        if (!q1_message(g, sphere->owner, "$qc_you_are_denied_vengeance", error))
            return false;
        return q1_remove(g, sphere, error);
    }
    qa_body_state target, body;
    if (!qa_world_body_read(g->services.world, enemy, &target, error) ||
        !qa_world_body_read(g->services.world, sphere->id, &body, error))
        return false;
    qa_vec3 delta = qa_vec_sub(qa_vec_add(target.origin, qa_v3(0, 0, 22)), body.origin);
    return q1_missile_velocity(g, sphere, qa_vec_scale(qa_vec_normalize(delta), 650), error) &&
           q1_schedule(g, sphere, 0.1, Q1_THINK_SPHERE_ATTACK, error);
}
bool q1_power_think(qa_q1_game *g, q1_actor *entity, q1_think_kind kind, qa_error *error) {
    if (kind == Q1_THINK_SPHERE_ATTACK)
        return sphere_attack(g, entity, error);
    if (!q1_alive(g, entity->owner))
        return q1_remove(g, entity, error);
    qa_body_state owner, body;
    if (!qa_world_body_read(g->services.world, entity->owner, &owner, error) ||
        !qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    if (kind == Q1_THINK_SHIELD) {
        if (entity->delay < g->time)
            return q1_remove(g, entity, error);
        if (entity->delay - 0.25 <= g->time)
            entity->model = 0;
        else {
            body.origin = owner.origin;
            body.angles = owner.angles;
            if (!qa_world_body_write(g->services.world, entity->id, &body, error))
                return false;
        }
        return q1_schedule(g, entity, 0.05, kind, error);
    }
    q1_player *player = q1_player_get(g, entity->owner);
    if (!player)
        return q1_remove(g, entity, error);
    if (entity->wait < g->time) {
        entity->wait = (float)(g->time + 4);
        if (!q1_sound(g, entity->id, "sphere/sphere.wav", 2, 1, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
    }
    if (g->time > entity->state.projectile.expires || q1_health(g, player->id) < 1) {
        bool consumed;
        if (!qa_inventory_consume(g->services.inventory, player->id, g->vengeance_item, 1,
                                  &consumed, error))
            return false;
        if (g->time > entity->state.projectile.expires) {
            if (!q1_message(g, player->id, "$qc_vengeance_lost", error))
                return false;
            return q1_remove(g, entity, error);
        }
        qa_actor_id killer = player->killer;
        qa_q1_target target;
        if (q1_alive(g, killer) && (!q1_target(g, killer, &target) || !target.player)) {
            qa_builtin_actor_traits traits;
            q1_actor *native = q1_entity(g, killer);
            killer = native ? native->owner
                     : g->services.actor_traits &&
                             g->services.actor_traits(g->services.context, killer, &traits)
                         ? traits.owner
                         : (qa_actor_id){0};
        }
        if (!q1_alive(g, killer) || !q1_target(g, killer, &target) || !target.player)
            return q1_remove(g, entity, error);
        entity->state.projectile.enemy = killer;
        return sphere_attack(g, entity, error);
    }
    qa_vec3 center = qa_vec_add(owner.origin, qa_v3(0, 0, 48));
    qa_trace_result trace;
    if (!q1_trace(g, body.origin, center, (qa_actor_id){0}, false, &trace, error))
        return false;
    if (entity->state.projectile.count > 3)
        entity->state.projectile.count = 0;
    if (trace.fraction < 1) {
        body.origin = center;
        ++entity->state.projectile.count;
        if (!qa_world_body_write(g->services.world, entity->id, &body, error))
            return false;
    } else {
        static const qa_vec3 offsets[] = {{16, 0, 0}, {0, 16, 0}, {-16, 0, 0}, {0, -16, 0}};
        qa_vec3 delta =
            qa_vec_sub(qa_vec_add(center, offsets[entity->state.projectile.count]), body.origin);
        float distance = qa_vec_length(delta);
        if (distance < 8)
            ++entity->state.projectile.count;
        else if (!q1_missile_velocity(
                     g, entity, qa_vec_scale(qa_vec_normalize(delta), distance < 50 ? 150 : 500),
                     error))
            return false;
    }
    return q1_schedule(g, entity, 0.1, Q1_THINK_SPHERE_ORBIT, error);
}
bool q1_sphere_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    if (entity->physics.solid != QA_PHYSICS_TRIGGER)
        return true;
    if (q1_health(g, other) != 0 &&
        !q1_damage(g, other, entity->id, entity->id, 1000, QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!q1_radius(g, entity->id, entity->id, 300, other, QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_vec3 origin = qa_vec_sub(body.origin, qa_vec_scale(qa_vec_normalize(body.velocity), 8));
    return q1_effect(g, QA_BUILTIN_EXPLOSION, entity->id, origin, 0, 0, error) &&
           q1_remove(g, entity, error);
}
qa_actor_id qa_q1_horn_charmer(const qa_q1_game *g) {
    return g ? g->horn_charmer : (qa_actor_id){0};
}
