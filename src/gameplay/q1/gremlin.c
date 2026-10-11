#include "internal.h"
#include <stdio.h>

static bool gib(qa_q1_game *g, q1_actor *entity, float damage, qa_error *error) {
    qa_actor_id id = entity->id;
    if (!q1_sound(g, id, "player/udeath.wav", 2, 1, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity)
        return true;
    if (!q1_gib_head(g, entity, "h_grem", damage, error))
        return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (!q1_entity(g, id))
            return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!q1_entity(g, id))
            return true;
        if (!q1_gib_at(g, id, body.origin, damage, "gib1", error))
            return false;
    }
    return true;
}
static bool resume(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (q1_ref_present(m->old_enemy) && q1_health(g, q1_ref_actor(g, m->old_enemy)) > 0) {
        m->enemy = m->old_enemy;
        entity->physics.goal = m->enemy;
        m->next_frame = q1_frame_index(m->species->run);
        m->attack_finished = g->time + 1;
        return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
    }
    return q1_monster_play(
        g, entity,
        qa_strings_text(qa_session_strings(g->services.session), entity->target).size
            ? "gremlin_walk1"
            : "gremlin_stand1",
        error);
}
static q1_actor *melee_source(qa_q1_game *g, qa_actor_id source) {
    q1_actor *entity = q1_entity(g, source);
    return entity && entity->kind == Q1_MONSTER &&
                   entity->state.monster.species->species == QA_Q1_GREMLIN
               ? entity
               : NULL;
}
static bool melee(qa_q1_game *g, q1_actor *entity, float side, qa_error *error) {
    qa_actor_id source = entity->id;
    if (!q1_monster_face(g, entity, error))
        return false;
    entity = melee_source(g, source);
    if (!entity)
        return true;
    qa_body_state body, target;
    qa_actor_id enemy = q1_ref_actor(g, entity->state.monster.enemy);
    if (!enemy.registry)
        return true;
    if (!qa_world_body_read(g->services.world, enemy, &target, NULL))
        return true;
    if (!melee_source(g, source))
        return true;
    if (!qa_world_body_read(g->services.world, source, &body, error))
        return !melee_source(g, source);
    entity = melee_source(g, source);
    if (!entity)
        return true;
    if (qa_vec_length(qa_vec_sub(target.origin, body.origin)) > 100)
        return true;
    bool visible;
    if (!q1_can_damage(g, q1_ref_actor(g, entity->state.monster.enemy), source, &visible, error))
        return false;
    entity = melee_source(g, source);
    if (!entity || !visible)
        return true;
    if (!q1_sound(g, source, "grem/attack.wav", 1, 1, error))
        return false;
    entity = melee_source(g, source);
    if (!entity)
        return true;
    if (!q1_damage(g, q1_ref_actor(g, entity->state.monster.enemy), source, source, 10 + 5 * q1_random(g),
                   QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!melee_source(g, source))
        return true;
    if (!qa_world_body_read(g->services.world, source, &body, error))
        return !melee_source(g, source);
    if (!melee_source(g, source))
        return true;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    qa_vec3 forward = g->forward, right = g->right;
    if (!qa_world_body_read(g->services.world, source, &body, error))
        return !melee_source(g, source);
    entity = melee_source(g, source);
    if (!entity)
        return true;
    return q1_meat_spray(g, entity, qa_vec_add(body.origin, qa_vec_scale(forward, 16)),
                         qa_vec_scale(right, side), error);
}
static bool split(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (g->spawned_gremlins >= g->authored_gremlins * 2u)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_vec3 angles = body.angles, position = body.origin;
    bool found = false;
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
        position = qa_vec_add(body.origin, qa_vec_scale(g->forward, 80));
        qa_builtin_snapshot_frame *snapshot;
        if (!q1_radius_snapshot(g, position, 35, &snapshot, error))
            return false;
        bool proceed = true;
        for (size_t i = snapshot->snapshot.count; i > 0; --i) {
            qa_actor_id actor = snapshot->snapshot.ids[i - 1];
            qa_q1_target traits;
            q1_actor *other = q1_entity(g, actor);
            qa_builtin_actor_traits foreign = {0};
            bool monster =
                other ? (other->physics.flags & QA_PHYSICS_MONSTER) != 0
                      : g->services.actor_traits &&
                            g->services.actor_traits(g->services.context, actor, &foreign) &&
                            foreign.monster;
            if (q1_health(g, actor) > 0 &&
                (monster || (q1_target(g, actor, &traits) && traits.player)))
                proceed = false;
        }
        qa_builtin_snapshot_release(snapshot);
        const qa_vec3 offsets[] = {{0, 0, 0}, {-40, -40, 0}, {40, 40, 0}, {0, 0, 64}, {0, 0, -64}};
        bool clear = true;
        for (unsigned i = 0; i < 5; ++i) {
            qa_trace_result trace;
            if (!q1_trace(g, body.origin, qa_vec_add(position, offsets[i]), entity->id, true,
                          &trace, error))
                return false;
            if (i == 0 && (!proceed || trace.fraction != 1)) {
                clear = false;
                break;
            }
            if (i < 4 ? trace.fraction != 1 : trace.fraction == 1) {
                clear = false;
                break;
            }
        }
        if (clear) {
            found = true;
            break;
        }
        angles.y += 36;
    }
    if (!found)
        return true;
    ++g->spawned_gremlins;
    qa_actor_id id;
    if (!qa_q1_game_clone(g, entity->id, &id, error))
        return false;
    q1_actor *child = q1_entity(g, id);
    if (!child) {
        qa_error_set(error, QA_ERROR_FORMAT, id.slot, "Gremlin clone lost its native continuation");
        return false;
    }
    child->physics.solid = QA_PHYSICS_BOX;
    child->physics.motion = QA_PHYSICS_STEP;
    if (!q1_model(g, child, "progs/grem.mdl", error) ||
        !qa_world_body_read(g->services.world, id, &body, error))
        return false;
    body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
    body.origin = position;
    float health = fmaxf(100, q1_health(g, entity->id)) * 0.5f;
    if (!qa_combat_set_health(g->services.combat, entity->id, health, error) ||
        !qa_combat_set_health(g->services.combat, id, health, error))
        return false;
    child->state.monster.source.gremlin.stolen = false;
    for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT; ++i) {
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(g->services.inventory, id, g->weapons[i], &entry, NULL))
            continue;
        entry.count = 0;
        if (!qa_inventory_configure(g->services.inventory, id, &entry, NULL, NULL, error))
            return false;
    }
    ++g->total_monsters;
    if (!q1_effect(g, QA_BUILTIN_TARGET, id, position, (float)g->total_monsters, 1, error) ||
        !qa_world_body_write(g->services.world, id, &body, error) ||
        !q1_monster_play(g, child, "gremlin_spawn1", error))
        return false;
    child->state.monster.enemy = (q1_ref){0};
    child->state.monster.source.gremlin.gorging = false;
    return true;
}
static bool gorge_damage(qa_q1_game *g, q1_actor *entity, qa_actor_id target, float damage,
                         qa_error *error) {
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, target, &combat, NULL) || combat.invulnerable)
        return true;
    qa_combat_state effective;
    if (qa_combat_read(g->services.combat, target, &effective, NULL) && effective.invulnerable) {
        if (entity->state.monster.source.gremlin.protection_sound < g->time) {
            entity->state.monster.source.gremlin.protection_sound = g->time + 2;
            return q1_sound(g, target, "items/protect3.wav", 3, 1, error);
        }
        return true;
    }
    qa_combat_state self;
    if (g->options.teamplay == 1 && combat.team &&
        qa_combat_read_traits(g->services.combat, entity->id, &self, NULL) &&
        self.team == combat.team)
        return true;
    return qa_combat_set_health(g->services.combat, target, combat.health - damage, error);
}
static bool gorge(qa_q1_game *g, q1_actor *entity, float side, qa_error *error) {
    qa_actor_id target = q1_ref_actor(g, entity->state.monster.enemy);
    if (!q1_alive(g, target))
        return true;
    if (!q1_sound(g, entity->id, "demon/dhit2.wav", 1, 1, error) ||
        !gorge_damage(g, entity, target, 7 + 5 * q1_random(g), error))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    if (!q1_meat_spray(g, entity, qa_vec_add(body.origin, qa_vec_scale(g->forward, 16)),
                       qa_vec_scale(g->right, side), error))
        return false;
    if (q1_health(g, target) >= -200)
        return true;
    q1_actor *victim = q1_entity(g, target);
    if (victim && !victim->consumed_corpse) {
        victim->consumed_corpse = true;
        const char *head = victim->kind == Q1_MONSTER ? victim->state.monster.species->head : NULL;
        if (q1_classnamed(g, target, g->runtime_names[Q1_NAME_MONSTER_FISH]))
            head = "gib1";
        if (!head)
            head = "h_player";
        if (!q1_sound(g, entity->id, "player/udeath.wav", 2, 1, error) ||
            !q1_gib_head(g, victim, head, -15, error))
            return false;
        float amount = 150 + 100 * q1_random(g), health = q1_health(g, entity->id);
        if (health > 0 && health < entity->max_health &&
            !qa_combat_set_health(g->services.combat, entity->id,
                                  fminf(entity->max_health, health + ceilf(amount)), error))
            return false;
        if (!split(g, entity, error))
            return false;
    }
    entity->state.monster.enemy = (q1_ref){0};
    entity->state.monster.source.gremlin.gorging = false;
    return q1_monster_play(g, entity, "gremlin_look1", error);
}
bool q1_gremlin_melee(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (entity->state.monster.source.gremlin.gorging)
        return q1_monster_play(g, entity, "gremlin_gorge1", error);
    if (entity->state.monster.source.gremlin.stolen) {
        qa_error_set(error, QA_ERROR_FORMAT, entity->id.slot,
                     "Gremlin attempted melee with stolen weapon");
        return false;
    }
    qa_q1_target target;
    if (q1_target(g, q1_ref_actor(g, entity->state.monster.enemy), &target) && target.player &&
        q1_random(g) < 0.4f) {
        bool stolen;
        if (!q1_gremlin_steal(g, entity, &stolen, error))
            return false;
        if (stolen)
            return true;
    }
    float random = q1_random(g);
    return q1_monster_play(g, entity,
                           random < 0.3f   ? "gremlin_claw1"
                           : random < 0.6f ? "gremlin_lunge1"
                                           : "gremlin_claw1",
                           error);
}
static bool missile_attack(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (entity->state.monster.source.gremlin.stolen) {
        bool fired;
        if (!q1_gremlin_weapon_attack(g, entity, &fired, error))
            return false;
        if (fired)
            return true;
        if (q1_random(g) < 0.1f && (entity->physics.flags & QA_PHYSICS_ONGROUND))
            return q1_monster_play(g, entity, "gremlin_jump1", error);
    }
    return !(entity->physics.flags & QA_PHYSICS_ONGROUND) ||
           q1_monster_play(g, entity, "gremlin_jump1", error);
}
bool q1_gremlin_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    ++g->authored_gremlins;
    entity->physics.yaw_speed = 40;
    entity->max_health = 101;
    entity->state.monster.source.gremlin.weapon = QA_Q1_WEAPON_COUNT;
    if (qa_inventory_has(g->services.inventory, entity->id))
        return true;
    qa_inventory_entry entries[4];
    for (unsigned i = 0; i < 4; ++i)
        entries[i] = (qa_inventory_entry){
            .item = g->ammo[i], .capacity = 1000000, .policy = QA_COUNT_SOURCE_FLOAT};
    return qa_inventory_create_actor(g->services.inventory, entity->id, entries, 4, error);
}
bool q1_gremlin_pain(qa_q1_game *g, q1_actor *entity, qa_actor_id attacker, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (m->source.gremlin.pain_disabled)
        return true;
    if (q1_random(g) < 0.8f) {
        m->source.gremlin.gorging = false;
        m->enemy = q1_ref_from(g, attacker);
        if (attacker.registry && !q1_monster_found(g, entity, attacker, error))
            return false;
    }
    if (m->source.gremlin.touch == 1 || m->pain_finished > g->time)
        return true;
    m->pain_finished = g->time + 1;
    float random = q1_random(g);
    char path[24];
    snprintf(path, sizeof(path), "grem/pain%u.wav", random < 0.33f ? 1 : random < 0.66f ? 2 : 3);
    return q1_sound(g, entity->id, path, 2, 1, error) &&
           q1_monster_play(g, entity,
                           m->source.gremlin.stolen ? "gremlin_gunpain1" : "gremlin_pain1", error);
}
bool q1_gremlin_die(qa_q1_game *g, q1_actor *entity, qa_actor_id attacker, qa_error *error) {
    qa_actor_id id = entity->id;
    bool has_weapon = false;
    for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT; ++i) {
        if (i == QA_Q1_AXE || i == QA_Q1_SHOTGUN || i == QA_Q1_MJOLNIR)
            continue;
        qa_inventory_entry entry;
        bool present = qa_inventory_entry_read(g->services.inventory, id, g->weapons[i],
                                                &entry, NULL);
        entity = q1_entity(g, id);
        if (!entity)
            return true;
        if (present && entry.count > 0) {
            has_weapon = true;
            break;
        }
    }
    if (has_weapon) {
        if (!q1_gremlin_backpack(g, entity, error))
            return false;
        entity = q1_entity(g, id);
        if (!entity)
            return true;
        entity->state.monster.source.gremlin.stolen = false;
    }
    qa_body_state body, target = {0};
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!q1_entity(g, id))
        return true;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    qa_vec3 forward = g->forward;
    if (attacker.registry)
        (void)qa_world_body_read(g->services.world, attacker, &target, NULL);
    if (!q1_entity(g, id))
        return true;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!q1_entity(g, id))
        return true;
    float facing = qa_vec_dot(qa_vec_normalize(qa_vec_sub(target.origin, body.origin)), forward),
          health = q1_health(g, id);
    entity = q1_entity(g, id);
    if (!entity)
        return true;
    if (health < -35)
        return gib(g, entity, health, error);
    if (facing > 0.7f && q1_random(g) < 0.5f && (entity->physics.flags & QA_PHYSICS_ONGROUND))
        return q1_monster_play(g, entity, "gremlin_flip1", error);
    return q1_monster_play(g, entity, "gremlin_die1", error);
}
bool q1_gremlin_touch(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    uint8_t touch = entity->state.monster.source.gremlin.touch;
    if (!touch || (touch == 1 && q1_health(g, entity->id) <= 0))
        return true;
    qa_body_state body;
    bool grounded;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_physics_check_bottom(g->services.physics, entity->id, body.origin, &grounded, error))
        return false;
    if (!grounded && !(entity->physics.flags & QA_PHYSICS_ONGROUND))
        return true;
    entity->state.monster.source.gremlin.touch = 0;
    entity->state.monster.next_frame =
        q1_frame_index(touch == 1 ? (grounded ? "gremlin_jump12" : "gremlin_jump1")
                                  : (grounded ? "gremlin_flip8" : "gremlin_flip1"));
    return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
}
bool q1_gremlin_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    switch (action) {
    case Q1_ACTION_GREMLIN_MELEEATTACK:
        return q1_gremlin_melee(g, entity, error);
    case Q1_ACTION_GREMLIN_MISSILEATTACK:
        return missile_attack(g, entity, error);
    case Q1_ACTION_GREMLIN_FIRELIGHTNINGGUN:
        return q1_gremlin_lightning(g, entity, error);
    case Q1_ACTION_GREMLIN_MELEE_0:
    case Q1_ACTION_GREMLIN_MELEE_200:
        return melee(g, entity, action == Q1_ACTION_GREMLIN_MELEE_0 ? 0 : 200, error);
    case Q1_ACTION_GREMLIN_GORGE_NEG_200:
    case Q1_ACTION_GREMLIN_GORGE_200:
        return gorge(g, entity, action == Q1_ACTION_GREMLIN_GORGE_NEG_200 ? -200 : 200, error);
    case Q1_ACTION_GREMLIN_GIB:
        return gib(g, entity, -35, error);
    case Q1_ACTION_HIPGREM_GREMLIN_STAND1: {
        bool found;
        if (!q1_monster_find_target(g, entity, &found, error))
            return false;
        if (!found && g->time > m->pause_until &&
            !q1_monster_play(g, entity, "gremlin_walk1", error))
            return false;
        return !q1_alive(g, entity->id) ||
               q1_schedule(g, entity, 0.2, Q1_THINK_MONSTER_FRAME, error);
    }
    case Q1_ACTION_HIPGREM_GREMLIN_JUMP5:
    case Q1_ACTION_HIPGREM_GREMLIN_FLIP1: {
        bool jump = action == Q1_ACTION_HIPGREM_GREMLIN_JUMP5;
        if (!q1_monster_face(g, entity, error))
            return false;
        if (jump && !(entity->physics.flags & QA_PHYSICS_ONGROUND))
            return q1_monster_play(g, entity, "gremlin_run1", error);
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
        body.origin.z += 1;
        body.velocity =
            qa_vec_add(qa_vec_scale(g->forward, jump ? 300 : -200), qa_v3(0, 0, jump ? 300 : 350));
        entity->physics.flags &= ~(uint32_t)QA_PHYSICS_ONGROUND;
        if (jump)
            m->source.gremlin.touch = 1;
        return qa_world_body_write(g->services.world, entity->id, &body, error) &&
               (jump || q1_sound(g, entity->id, "grem/death.wav", 2, 1, error));
    }
    case Q1_ACTION_HIPGREM_GREMLIN_JUMP11:
        return q1_schedule(g, entity, 3, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_HIPGREM_GREMLIN_SHOT1:
        entity->effects |= 2;
        return true;
    case Q1_ACTION_HIPGREM_GREMLIN_NAIL1:
        entity->effects |= 2;
        return q1_gremlin_fire_nail(g, entity, false, error);
    case Q1_ACTION_HIPGREM_GREMLIN_LASER1:
        entity->effects |= 2;
        return q1_gremlin_fire_nail(g, entity, true, error);
    case Q1_ACTION_HIPGREM_GREMLIN_LOOK1:
        return q1_schedule(g, entity, 0.2, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_HIPGREM_GREMLIN_LOOK9:
        return resume(g, entity, error);
    case Q1_ACTION_HIPGREM_GREMLIN_GLOOK20: {
        qa_actor_id source = entity->id;
        if (!q1_gremlin_backpack(g, entity, error))
            return false;
        entity = q1_entity(g, source);
        if (!entity)
            return true;
        entity->state.monster.source.gremlin.stolen = false;
        return resume(g, entity, error);
    }
    case Q1_ACTION_HIPGREM_GREMLIN_SPAWN1:
        m->source.gremlin.pain_disabled = true;
        return q1_schedule(g, entity, 0.3, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_HIPGREM_GREMLIN_SPAWN2:
        return q1_schedule(g, entity, 0.3, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_HIPGREM_GREMLIN_SPAWN6:
        m->source.gremlin.pain_disabled = false;
        return true;
    case Q1_ACTION_HIPGREM_GREMLIN_FLIP6:
        m->source.gremlin.touch = 2;
        return true;
    case Q1_ACTION_GREMLIN_RUN_0:
        return q1_gremlin_run(g, entity, 0, error);
    case Q1_ACTION_GREMLIN_RUN_8:
        return q1_gremlin_run(g, entity, 8, error);
    case Q1_ACTION_GREMLIN_RUN_12:
        return q1_gremlin_run(g, entity, 12, error);
    case Q1_ACTION_GREMLIN_RUN_16:
        return q1_gremlin_run(g, entity, 16, error);
    case Q1_ACTION_GREMLIN_WALK_8:
        return q1_gremlin_walk(g, entity, 8, error);
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown Gremlin frame action");
        return false;
    }
}
