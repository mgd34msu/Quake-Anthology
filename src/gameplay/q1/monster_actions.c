#include "internal.h"

static bool state_body(qa_q1_game *g, q1_actor *entity, qa_body_state *body, qa_error *error) {
    return qa_world_body_read(g->services.world, entity->id, body, error);
}
static void axes(qa_q1_game *g, qa_body_state body) {
    if (g->options.edition == QA_Q1_RERELEASE && g->options.program != QA_Q1_MG3)
        body.angles.x = -body.angles.x;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
}
static bool meat(qa_q1_game *g, q1_actor *entity, float side, qa_error *error) {
    qa_body_state body;
    if (!state_body(g, entity, &body, error))
        return false;
    axes(g, body);
    qa_vec3 velocity = qa_vec_scale(g->right, side == 1 ? (q1_random(g) * 2 - 1) * 100 : side);
    return q1_meat_spray(g, entity, qa_vec_add(body.origin, qa_vec_scale(g->forward, 16)), velocity,
                         error);
}
static bool chainsaw(qa_q1_game *g, q1_actor *entity, float side, qa_error *error) {
    qa_actor_id enemy = q1_ref_actor(g, entity->state.monster.enemy);
    if (!enemy.registry)
        return true;
    bool visible;
    if (!q1_can_damage(g, enemy, entity->id, &visible, error))
        return false;
    if (!visible)
        return true;
    if (!q1_monster_ai(g, entity, Q1_AI_CHARGE, 10, error))
        return false;
    qa_body_state body, other;
    if (!state_body(g, entity, &body, error) ||
        !qa_world_body_read(g->services.world, enemy, &other, error))
        return false;
    if (qa_vec_length(qa_vec_sub(other.origin, body.origin)) > 100)
        return true;
    return q1_monster_melee(g, entity, 100, 4, 3, false, error) &&
           (side == 0 || meat(g, entity, side, error));
}
static bool jump(qa_q1_game *g, q1_actor *entity, bool tar, bool dog, qa_error *error) {
    qa_body_state body;
    if (!state_body(g, entity, &body, error))
        return false;
    axes(g, body);
    if (tar)
        entity->physics.motion = QA_PHYSICS_BOUNCE;
    if (dog)
        entity->physics.motion = QA_PHYSICS_TOSS;
    entity->state.monster.counter = 0;
    entity->state.monster.jump_touch = true;
    entity->physics.flags &= ~(uint32_t)QA_PHYSICS_ONGROUND;
    body.origin.z += 1;
    body.velocity =
        qa_vec_add(qa_vec_scale(g->forward, dog ? 300 : 600), qa_v3(0, 0,
                                                                    tar   ? 200 + q1_random(g) * 150
                                                                    : dog ? 200
                                                                          : 250));
    return qa_world_body_write(g->services.world, entity->id, &body, error) &&
           q1_link(g, entity, error);
}
bool q1_monster_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    if (entity->state.monster.addon.boss != Q1_BOSS_NONE)
        return q1_boss_touch(g, entity, other, error);
    if (entity->state.monster.addon.demodog)
        return q1_demodog_touch(g, entity, other, error);
    if (entity->state.monster.species->species == QA_Q1_GREMLIN)
        return q1_gremlin_touch(g, entity, error);
    if (entity->state.monster.species->species == QA_Q1_DRAGON)
        return q1_dragon_touch(g, entity, other, error);
    q1_monster *m = &entity->state.monster;
    if (!m->jump_touch || q1_health(g, entity->id) <= 0)
        return true;
    bool tar = m->species->species == QA_Q1_TARBABY, dog = m->species->species == QA_Q1_DOG;
    q1_actor *target = q1_entity(g, other);
    qa_body_state body;
    if (!state_body(g, entity, &body, error))
        return false;
    if (q1_damageable(g, other) && (!tar || !target || target->classname != entity->classname)) {
        if (qa_vec_length(body.velocity) > (dog ? 300 : 400)) {
            if (!q1_damage(g, other, entity->id, entity->id,
                           (tar || dog ? 10 : 40) + 10 * q1_random(g), QA_Q1_WEAPON_COUNT, error))
                return false;
            if (tar && !q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_BLOB_HIT1_WAV], 1, 1, 1, error))
                return false;
        }
    } else if (tar && !q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_BLOB_LAND1_WAV], 1, 1, 1, error))
        return false;
    bool grounded;
    if (!qa_physics_check_bottom(g->services.physics, entity->id, body.origin, &grounded, error))
        return false;
    if (grounded || (!dog && (entity->physics.flags & QA_PHYSICS_ONGROUND))) {
        m->jump_touch = false;
        if (!tar || !grounded)
            entity->physics.motion = QA_PHYSICS_STEP;
        const char *next = dog        ? m->species->run
                           : tar      ? grounded ? "tbaby_jump1" : "tbaby_run1"
                           : grounded ? "demon1_jump11"
                                      : "demon1_jump1";
        m->next_frame = q1_frame_index(next);
        return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
    }
    return true;
}
static bool lightning(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state body, other;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, entity->state.monster.enemy), &other, NULL))
        return true;
    if (!q1_monster_face(g, entity, error) || !state_body(g, entity, &body, error))
        return false;
    if (!q1_effect(g, QA_BUILTIN_MUZZLE, entity->id, body.origin, 0, 0, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    ++entity->state.monster.lightning_count;
    qa_vec3 origin = qa_vec_add(body.origin, qa_v3(0, 0, 40));
    qa_vec3 direction =
        qa_vec_normalize(qa_vec_sub(qa_vec_add(other.origin, qa_v3(0, 0, 16)), origin));
    qa_trace_result wall;
    if (!q1_trace(g, origin, qa_vec_add(body.origin, qa_vec_scale(direction, 600)), entity->id,
                  false, &wall, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_BEAM,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = entity->id,
                              .origin = origin,
                              .end = wall.end,
                              .code = 1,
                              .time_ns = g->time_ns};
    if (!qa_builtin_emit(&g->services, &event, error))
        return false;
    return q1_lightning_rays(g, entity->id, entity->id, origin, wall.end, 10, 40, 1, qa_v3(0, 0, 0),
                             Q1_LIGHTNING_DAMAGE_FIRST, QA_Q1_WEAPON_COUNT, NULL, error);
}
static bool knight_shot(qa_q1_game *g, q1_actor *entity, int offset, qa_error *error) {
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, entity->state.monster.enemy), &target, NULL))
        return true;
    if (!state_body(g, entity, &body, error))
        return false;
    qa_vec3 delta = qa_vec_sub(target.origin, body.origin);
    qa_vec3 angles = qa_v3(atan2f(delta.z, hypotf(delta.x, delta.y)) * 57.29577951308232f,
                           atan2f(delta.y, delta.x) * 57.29577951308232f + (float)offset * 6, 0);
    qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
    qa_vec3 origin = qa_vec_add(
        qa_vec_add(body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f)),
        qa_vec_scale(g->forward, 20));
    qa_vec3 direction = qa_vec_normalize(g->forward);
    direction.z = -direction.z + (q1_random(g) - 0.5f) * 0.1f;
    q1_actor *missile;
    return q1_projectile_spawn(g, entity->id, QA_Q1_WEAPON_COUNT, Q1_KNIGHT_SPIKE, origin,
                               qa_vec_scale(direction, 300), &missile, error) &&
           q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_HKNIGHT_ATTACK1_WAV], 1, 1, 1, error);
}
static bool wizard_fast(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id enemy = q1_ref_actor(g, entity->state.monster.enemy);
    if (!enemy.registry)
        return true;
    qa_body_state body;
    if (!state_body(g, entity, &body, error) ||
        !q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_WIZARD_WATTACK_WAV], 1, 1, 1, error))
        return false;
    axes(g, body);
    qa_vec3 forward = g->forward, right = g->right;
    for (unsigned i = 0; i < 2; ++i) {
        float side = i ? -1 : 1;
        q1_actor *timer;
        if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_WIZARD_FASTFIRE], Q1_TIMER, entity->id, &timer, error))
            return false;
        timer->state.projectile.enemy = q1_ref_from(g, enemy);
        timer->state.projectile.right = qa_vec_scale(right, side);
        qa_body_state state = {
            .origin = qa_vec_add(
                body.origin,
                qa_vec_add(qa_v3(0, 0, 30),
                           qa_vec_add(qa_vec_scale(forward, 14), qa_vec_scale(right, 14 * side))))};
        if (!qa_world_body_write(g->services.world, timer->id, &state, error) ||
            !q1_schedule(g, timer, i ? 0.3 : 0.8, Q1_THINK_WIZARD, error))
            return false;
    }
    return true;
}
static bool boss_face(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (!q1_ref_present(m->enemy) || q1_health(g, q1_ref_actor(g, m->enemy)) <= 0 || q1_random(g) < 0.02f) {
        uint32_t start = q1_ref_present(m->enemy) ? q1_ref_actor(g, m->enemy).slot + 1 : 0;
        unsigned considered = 0;
        for (uint32_t offset = 0; offset < g->capacity && considered < 4; ++offset) {
            uint32_t slot = (start + offset) % g->capacity;
            uint32_t cursor = slot;
            const qa_actor_record *record;
            if (!qa_actors_next(qa_session_actors(g->services.session), &cursor, &record) ||
                record->id.slot != slot)
                continue;
            qa_q1_target target;
            if (!q1_target(g, record->id, &target) || !target.player)
                continue;
            ++considered;
            if (q1_health(g, record->id) > 0) {
                m->enemy = q1_ref_from(g, record->id);
                break;
            }
        }
    }
    return q1_monster_face(g, entity, error);
}
static bool boss_missile(qa_q1_game *g, q1_actor *entity, float side, qa_error *error) {
    qa_body_state self, target;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, entity->state.monster.enemy), &target, NULL))
        return true;
    if (!state_body(g, entity, &self, error))
        return false;
    qa_vec3 delta = qa_vec_sub(target.origin, self.origin);
    qa_vec3 angles = qa_v3(atan2f(delta.z, hypotf(delta.x, delta.y)) * 57.29577951308232f,
                           atan2f(delta.y, delta.x) * 57.29577951308232f, 0);
    qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
    qa_vec3 origin = qa_vec_add(
        self.origin, qa_vec_add(qa_vec_scale(g->forward, 100),
                                qa_vec_add(qa_vec_scale(g->right, side), qa_v3(0, 0, 200))));
    qa_vec3 destination = target.origin;
    if (g->options.skill > 1) {
        qa_vec3 velocity = target.velocity;
        velocity.z = 0;
        destination = qa_vec_add(
            destination,
            qa_vec_scale(velocity, qa_vec_length(qa_vec_sub(target.origin, origin)) / 300));
    }
    q1_actor *missile;
    if (!q1_projectile_spawn(g, entity->id, QA_Q1_WEAPON_COUNT, Q1_LAVA_BALL, origin,
                             qa_vec_scale(qa_vec_normalize(qa_vec_sub(destination, origin)), 300),
                             &missile, error) ||
        !q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_BOSS1_THROW_WAV], 1, 1, 1, error))
        return false;
    return q1_health(g, q1_ref_actor(g, entity->state.monster.enemy)) > 0 ||
           q1_monster_play(g, entity, "boss_idle1", error);
}

bool q1_monster_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action, qa_error *error) {
    if (entity->state.monster.addon.boss != Q1_BOSS_NONE)
        return q1_boss_action(g, entity, action, error);
    if (entity->state.monster.addon.heavy != Q1_HEAVY_NONE)
        return q1_heavy_action(g, entity, action, error);
    switch (action) {
    case Q1_ACTION_INFECTED_CORPSE_HOLD:
    case Q1_ACTION_INFECTED_TEST_RISE:
    case Q1_ACTION_INFECTED_RISE_PAIN:
    case Q1_ACTION_INFECTED_RESURRECT:
        return q1_infected_action(g, entity, action, error);
    case Q1_ACTION_DEMODOG_BITE:
    case Q1_ACTION_DEMODOG_JUMP:
        return q1_demodog_action(g, entity, action, error);
    default:
        break;
    }
    q1_monster *m = &entity->state.monster;
    if (action >= Q1_ACTION_BOSS_IDLE1 && action <= Q1_ACTION_BOSS_IDLE9) {
        if (action == Q1_ACTION_BOSS_IDLE1 && q1_ref_present(m->enemy) && q1_health(g, q1_ref_actor(g, m->enemy)) > 0)
            return q1_monster_play(g, entity, "boss_missile1", error);
        return boss_face(g, entity, error);
    }
    if (action >= Q1_ACTION_BOSS_MISSILE1 && action <= Q1_ACTION_BOSS_MISSILE9) {
        if (action == Q1_ACTION_BOSS_MISSILE9)
            return boss_missile(g, entity, 100, error);
        if (action == Q1_ACTION_BOSS_MISSILE20)
            return boss_missile(g, entity, -100, error);
        return boss_face(g, entity, error);
    }
    switch (action) {
    case Q1_ACTION_HKNIGHT_MAGICA7:
    case Q1_ACTION_HKNIGHT_MAGICB7:
    case Q1_ACTION_HKNIGHT_MAGICC6:
        return knight_shot(g, entity, -2, error);
    case Q1_ACTION_HKNIGHT_MAGICA8:
    case Q1_ACTION_HKNIGHT_MAGICB8:
    case Q1_ACTION_HKNIGHT_MAGICC7:
        return knight_shot(g, entity, -1, error);
    case Q1_ACTION_HKNIGHT_MAGICA9:
    case Q1_ACTION_HKNIGHT_MAGICB9:
    case Q1_ACTION_HKNIGHT_MAGICC8:
        return knight_shot(g, entity, 0, error);
    case Q1_ACTION_HKNIGHT_MAGICA10:
    case Q1_ACTION_HKNIGHT_MAGICB10:
    case Q1_ACTION_HKNIGHT_MAGICC9:
        return knight_shot(g, entity, 1, error);
    case Q1_ACTION_HKNIGHT_MAGICA11:
    case Q1_ACTION_HKNIGHT_MAGICB11:
    case Q1_ACTION_HKNIGHT_MAGICC10:
        return knight_shot(g, entity, 2, error);
    case Q1_ACTION_HKNIGHT_MAGICA12:
    case Q1_ACTION_HKNIGHT_MAGICB12:
    case Q1_ACTION_HKNIGHT_MAGICC11:
        return knight_shot(g, entity, 3, error);
    default:
        break;
    }
    qa_body_state body, target;
    if (!state_body(g, entity, &body, error))
        return false;
    bool has_target = qa_world_body_read(g->services.world, q1_ref_actor(g, m->enemy), &target, NULL);
    q1_actor *missile;
    bool visible, moved;
    switch (action) {
    case Q1_ACTION_AI_BACK_2:
    case Q1_ACTION_AI_BACK_13:
    case Q1_ACTION_AI_BACK_3:
    case Q1_ACTION_AI_BACK_4:
    case Q1_ACTION_AI_BACK_5:
        return q1_monster_ai(g, entity, Q1_AI_PAIN,
                             action == Q1_ACTION_AI_BACK_2    ? 2
                             : action == Q1_ACTION_AI_BACK_13 ? 13
                             : action == Q1_ACTION_AI_BACK_3  ? 3
                             : action == Q1_ACTION_AI_BACK_4  ? 4
                                                              : 5,
                             error);
    case Q1_ACTION_KNIGHT_RUNATK1:
        return q1_sound_resource(g, entity->id, q1_random(g) > 0.5f ? g->runtime_names[Q1_NAME_RESOURCE_KNIGHT_SWORD2_WAV] : g->runtime_names[Q1_NAME_RESOURCE_KNIGHT_SWORD1_WAV], 1, 1, 1, error) &&
               q1_monster_ai(g, entity, Q1_AI_CHARGE, 20, error);
    case Q1_ACTION_ENF_ATK6:
    case Q1_ACTION_ENF_ATK10:
        if (!has_target)
            return true;
        axes(g, body);
        return q1_effect(g, QA_BUILTIN_MUZZLE, entity->id, body.origin, 0, 0, error) &&
               q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_ENFORCER_ENFIRE_WAV], 1, 1, 1, error) &&
               q1_projectile_spawn(
                   g, entity->id, QA_Q1_WEAPON_COUNT, Q1_ENFORCER_LASER,
                   qa_vec_add(body.origin, qa_vec_add(qa_vec_scale(g->forward, 30),
                                                      qa_vec_add(qa_vec_scale(g->right, 8.5f),
                                                                 qa_v3(0, 0, 16)))),
                   qa_vec_scale(qa_vec_normalize(qa_vec_sub(target.origin, body.origin)), 600),
                   &missile, error);
    case Q1_ACTION_ENF_ATK14:
    case Q1_ACTION_GRUNT_ARMY_ATK7:
        if (g->options.skill == 3 && m->counter != 1) {
            if (!q1_monster_visible(g, entity, q1_ref_actor(g, m->enemy), &visible, error))
                return false;
            if (visible) {
                m->counter = 1;
                m->next_frame =
                    q1_frame_index(action == Q1_ACTION_ENF_ATK14 ? "enf_atk1" : "army_atk1");
            }
        }
        return true;
    case Q1_ACTION_ENF_DIE3:
    case Q1_ACTION_ENF_FDIE3: {
        float ammo[QA_Q1_AMMO_COUNT] = {[QA_Q1_CELLS] = 5};
        return q1_drop_backpack(g, entity, QA_Q1_WEAPON_COUNT, ammo, error);
    }
    case Q1_ACTION_GRUNT_ARMY_DIE3:
    case Q1_ACTION_GRUNT_ARMY_CDIE3: {
        entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        float ammo[QA_Q1_AMMO_COUNT] = {[QA_Q1_SHELLS] = 5};
        return q1_link(g, entity, error) &&
               q1_drop_backpack(g, entity, QA_Q1_WEAPON_COUNT, ammo, error);
    }
    case Q1_ACTION_GRUNT_ARMY_ATK5:
        if (!has_target)
            return true;
        if (m->addon.enabled) {
            if (!q1_monster_face(g, entity, error) || !state_body(g, entity, &body, error))
                return false;
        }
        return q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_SOLDIER_SATTCK1_WAV], 1, 1, 1, error) &&
               q1_bullets(g, entity->id,
                          qa_vec_normalize(qa_vec_sub(
                              qa_vec_sub(target.origin, qa_vec_scale(target.velocity, 0.2f)),
                              body.origin)),
                          m->addon.enabled ? body.angles : qa_v3(0, 0, 0), 4, 0.1f, 0.1f,
                          QA_Q1_WEAPON_COUNT, error) &&
               q1_effect(g, QA_BUILTIN_MUZZLE, entity->id, body.origin, 0, 0, error);
    case Q1_ACTION_DOG_BITE:
        return q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_DOG_DATTACK1_WAV], 2, 1, 1, error) &&
               q1_monster_melee(g, entity, 100, 8, 3, true, error);
    case Q1_ACTION_ROTTWEILER_DOG_LEAP2:
        return jump(g, entity, false, true, error);
    case Q1_ACTION_DEMON1_JUMP4:
        return jump(g, entity, false, false, error);
    case Q1_ACTION_DEMON1_JUMP10:
        return q1_schedule(g, entity, 3, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_DEMON1_ATTA5:
    case Q1_ACTION_DEMON1_ATTA11:
        if (!q1_monster_face(g, entity, error) ||
            !qa_physics_walk_move(g->services.physics, entity->id, entity->physics.ideal_yaw, 12,
                                  (float)g->elapsed, true, true, &moved, error))
            return false;
        if (!has_target || !state_body(g, entity, &body, error) ||
            qa_vec_length(qa_vec_sub(target.origin, body.origin)) > 100)
            return true;
        if (!q1_can_damage(g, q1_ref_actor(g, m->enemy), entity->id, &visible, error))
            return false;
        return !visible || (q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_DEMON_DHIT2_WAV], 1, 1, 1, error) &&
                            q1_damage(g, q1_ref_actor(g, m->enemy), entity->id, entity->id, 10 + 5 * q1_random(g),
                                      QA_Q1_WEAPON_COUNT, error) &&
                            meat(g, entity, action == Q1_ACTION_DEMON1_ATTA5 ? 200 : -200, error));
    case Q1_ACTION_OGRE_SWING5:
    case Q1_ACTION_OGRE_SWING6:
    case Q1_ACTION_OGRE_SWING7:
    case Q1_ACTION_OGRE_SWING8:
    case Q1_ACTION_OGRE_SWING9:
    case Q1_ACTION_OGRE_SWING10:
    case Q1_ACTION_OGRE_SWING11:
        if (!chainsaw(g, entity,
                      action == Q1_ACTION_OGRE_SWING6    ? 200
                      : action == Q1_ACTION_OGRE_SWING10 ? -200
                                                         : 0,
                      error) ||
            !state_body(g, entity, &body, error))
            return false;
        body.angles.y += q1_random(g) * 25;
        return qa_world_body_write(g->services.world, entity->id, &body, error);
    case Q1_ACTION_OGRE_SMASH6:
    case Q1_ACTION_OGRE_SMASH7:
    case Q1_ACTION_OGRE_SMASH8:
    case Q1_ACTION_OGRE_SMASH9:
        return chainsaw(g, entity, 0, error);
    case Q1_ACTION_OGRE_SMASH10:
        return chainsaw(g, entity, 1, error);
    case Q1_ACTION_OGRE_SMASH11:
        return chainsaw(g, entity, 0, error) &&
               q1_schedule(g, entity, 0.1 + q1_random(g) * 0.2, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_OGRE_NAIL4: {
        if (!has_target)
            return true;
        axes(g, body);
        qa_vec3 velocity =
            qa_vec_scale(qa_vec_normalize(qa_vec_sub(target.origin, body.origin)), 600);
        velocity.z = 200;
        return q1_effect(g, QA_BUILTIN_MUZZLE, entity->id, body.origin, 0, 0, error) &&
               q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_WEAPONS_GRENADE_WAV], 1, 1, 1, error) &&
               q1_projectile_spawn(g, entity->id, QA_Q1_WEAPON_COUNT, Q1_OGRE_GRENADE, body.origin,
                                   velocity, &missile, error);
    }
    case Q1_ACTION_OGRE_DIE3:
    case Q1_ACTION_OGRE_BDIE3: {
        float ammo[QA_Q1_AMMO_COUNT] = {[QA_Q1_ROCKETS] = 2};
        return q1_drop_backpack(g, entity, QA_Q1_WEAPON_COUNT, ammo, error);
    }
    case Q1_ACTION_HKNIGHT_WALK1:
    case Q1_ACTION_HKNIGHT_RUN1:
        if (q1_random(g) < 0.2f && !q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_HKNIGHT_IDLE_WAV], 2, 1, 1, error))
            return false;
        if (!q1_monster_ai(g, entity, action == Q1_ACTION_HKNIGHT_WALK1 ? Q1_AI_WALK : Q1_AI_RUN,
                           action == Q1_ACTION_HKNIGHT_WALK1 ? 2 : 20, error))
            return false;
        if (action == Q1_ACTION_HKNIGHT_RUN1 && has_target && g->time >= m->attack_finished &&
            fabsf(body.origin.z - target.origin.z) <= 20 &&
            qa_vec_length(qa_vec_sub(body.origin, target.origin)) >= 80) {
            if (!q1_monster_visible(g, entity, q1_ref_actor(g, m->enemy), &visible, error))
                return false;
            if (visible) {
                m->counter = 0;
                if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3)
                    m->attack_finished = g->time + 2;
                return q1_monster_play(g, entity, "hknight_char_a1", error);
            }
        }
        return true;
    case Q1_ACTION_HKNIGHT_CHAR_B1:
        if (g->time > m->attack_finished) {
            m->counter = 0;
            if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3)
                m->attack_finished = g->time + 3;
            if (!q1_monster_play(g, entity, "hknight_run1", error))
                return false;
        } else if (!q1_sound_resource(g, entity->id, q1_random(g) > 0.5f ? g->runtime_names[Q1_NAME_RESOURCE_KNIGHT_SWORD2_WAV] : g->runtime_names[Q1_NAME_RESOURCE_KNIGHT_SWORD1_WAV], 1, 1, 1, error))
            return false;
        return q1_monster_ai(g, entity, Q1_AI_CHARGE, 23, error) &&
               q1_monster_ai(g, entity, Q1_AI_MELEE, 0, error);
    case Q1_ACTION_SHAM_SMASH10:
        if (!q1_monster_ai(g, entity, Q1_AI_CHARGE, 0, error))
            return false;
        if (!has_target || qa_vec_length(qa_vec_sub(body.origin, target.origin)) > 100)
            return true;
        if (!q1_can_damage(g, q1_ref_actor(g, m->enemy), entity->id, &visible, error))
            return false;
        return !visible || (q1_monster_melee(g, entity, 100, 40, 3, false, error) &&
                            q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_SHAMBLER_SMACK_WAV], 2, 1, 1, error) &&
                            meat(g, entity, 1, error) && meat(g, entity, 1, error));
    case Q1_ACTION_SHAM_SWINGL7:
    case Q1_ACTION_SHAM_SWINGR7:
        if (!q1_monster_ai(g, entity, Q1_AI_CHARGE, 10, error))
            return false;
        if (!has_target || !state_body(g, entity, &body, error) ||
            qa_vec_length(qa_vec_sub(body.origin, target.origin)) > 100)
            return true;
        return q1_monster_melee(g, entity, 100, 20, 3, false, error) &&
               q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_SHAMBLER_SMACK_WAV], 2, 1, 1, error) &&
               meat(g, entity, action == Q1_ACTION_SHAM_SWINGL7 ? 250 : -250, error);
    case Q1_ACTION_SHAM_SWINGL9:
    case Q1_ACTION_SHAM_SWINGR9:
        if (q1_random(g) < 0.5f)
            m->next_frame =
                q1_frame_index(action == Q1_ACTION_SHAM_SWINGL9 ? "sham_swingr1" : "sham_swingl1");
        return true;
    case Q1_ACTION_SHAM_MAGIC3:
        if (!q1_schedule(g, entity, 0.3, Q1_THINK_MONSTER_FRAME, error) ||
            !q1_effect(g, QA_BUILTIN_MUZZLE, entity->id, body.origin, 0, 0, error) ||
            !q1_monster_face(g, entity, error) ||
            !q1_create(g, g->runtime_names[Q1_NAME_CLASS_SHAMBLER_LIGHT], Q1_TIMER, entity->id, &missile, error))
            return false;
        entity->owner = q1_ref_from(g, missile->id);
        return q1_model(g, missile, "progs/s_light.mdl", error) &&
               qa_world_body_write(g->services.world, missile->id, &body, error) &&
               q1_link(g, missile, error) && q1_schedule(g, missile, 0.7, Q1_THINK_REMOVE, error);
    case Q1_ACTION_SHAM_MAGIC4:
    case Q1_ACTION_SHAM_MAGIC5:
        missile = q1_entity(g, q1_ref_actor(g, entity->owner));
        if (missile)
            missile->frame = action == Q1_ACTION_SHAM_MAGIC4 ? 1 : 2;
        return q1_effect(g, QA_BUILTIN_MUZZLE, entity->id, body.origin, 0, 0, error);
    case Q1_ACTION_SHAM_MAGIC6:
        missile = q1_entity(g, q1_ref_actor(g, entity->owner));
        if (missile && !q1_remove(g, missile, error))
            return false;
        return lightning(g, entity, error) &&
               q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_SHAMBLER_SBOOM_WAV], 1, 1, 1, error);
    case Q1_ACTION_SHAM_MAGIC9:
    case Q1_ACTION_SHAM_MAGIC10:
        return lightning(g, entity, error);
    case Q1_ACTION_WIZ_WALK1:
    case Q1_ACTION_WIZ_RUN1:
    case Q1_ACTION_WIZ_SIDE1: {
        float r = q1_random(g) * 5;
        if (m->idle_until < g->time) {
            m->idle_until = g->time + 2;
            if (r > 4.5f)
                return q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_WIZARD_WIDLE1_WAV], 2, 2, 1, error);
            if (r < 1.5f)
                return q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_WIZARD_WIDLE2_WAV], 2, 2, 1, error);
        }
        return true;
    }
    case Q1_ACTION_WIZ_FAST1:
        return wizard_fast(g, entity, error);
    case Q1_ACTION_WIZ_FAST10:
        m->counter = 0;
        if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3)
            m->attack_finished = g->time + 2;
        if (!q1_monster_visible(g, entity, q1_ref_actor(g, m->enemy), &visible, error))
            return false;
        m->sliding =
            has_target && qa_vec_length(qa_vec_sub(body.origin, target.origin)) < 500 && visible;
        m->next_frame = q1_frame_index(m->sliding ? "wiz_side1" : "wiz_run1");
        return true;
    case Q1_ACTION_WIZ_DEATH1: {
        float x = -200 + 400 * q1_random(g), y = -200 + 400 * q1_random(g),
              z = 100 + 100 * q1_random(g);
        body.velocity = qa_v3(x, y, z);
        body.ground = (qa_actor_reference){0};
        return qa_world_body_write(g->services.world, entity->id, &body, error) &&
               q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_WIZARD_WDEATH_WAV], 2, 1, 1, error);
    }
    case Q1_ACTION_SHAL_ATTACK9: {
        if (!has_target)
            return true;
        qa_vec3 direction =
            qa_vec_normalize(qa_vec_sub(qa_vec_add(target.origin, qa_v3(0, 0, 10)), body.origin));
        if (!q1_projectile_spawn(g, entity->id, QA_Q1_WEAPON_COUNT, Q1_VORE_BALL,
                                 qa_vec_add(body.origin, qa_v3(0, 0, 10)),
                                 qa_vec_scale(direction, 400), &missile, error))
            return false;
        missile->state.projectile.enemy = m->enemy;
        return q1_effect(g, QA_BUILTIN_MUZZLE, entity->id, body.origin, 0, 0, error) &&
               q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_SHALRATH_ATTACK2_WAV], 1, 1, 1, error) &&
               q1_schedule(g, missile,
                           fmax(0.1, qa_vec_length(qa_vec_sub(target.origin, body.origin)) * 0.002),
                           Q1_THINK_VORE, error);
    }
    case Q1_ACTION_TBABY_FLY4:
        return ++m->counter != 4 || q1_monster_play(g, entity, "tbaby_jump5", error);
    case Q1_ACTION_TBABY_JUMP5:
        return jump(g, entity, true, false, error);
    case Q1_ACTION_TBABY_DIE1:
        return true;
    case Q1_ACTION_TBABY_DIE2:
        return q1_radius(g, entity->id, entity->id, 120, (qa_actor_id){0}, QA_Q1_WEAPON_COUNT,
                         error) &&
               q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_BLOB_DEATH1_WAV], 2, 1, 1, error) &&
               q1_effect(g, QA_BUILTIN_EXPLOSION, entity->id,
                         qa_vec_sub(body.origin, qa_vec_scale(qa_vec_normalize(body.velocity), 8)),
                         0, 1, error) &&
               q1_remove(g, entity, error);
    case Q1_ACTION_F_ATTACK3:
    case Q1_ACTION_F_ATTACK9:
    case Q1_ACTION_F_ATTACK15:
        if (!has_target || qa_vec_length(qa_vec_sub(body.origin, target.origin)) > 60)
            return true;
        return q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_FISH_BITE_WAV], 2, 1, 1, error) &&
               q1_monster_melee(g, entity, 60, 3, 2, false, error);
    case Q1_ACTION_ZOMBIE_CRUC2:
    case Q1_ACTION_ZOMBIE_CRUC3:
    case Q1_ACTION_ZOMBIE_CRUC4:
    case Q1_ACTION_ZOMBIE_CRUC5:
    case Q1_ACTION_ZOMBIE_CRUC6:
        return q1_schedule(g, entity, 0.1 + q1_random(g) * 0.1, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_ZOMBIE_RUN1:
        m->in_pain = 0;
        return true;
    case Q1_ACTION_ZOMBIE_ATTA13:
    case Q1_ACTION_ZOMBIE_ATTB14:
    case Q1_ACTION_ZOMBIE_ATTC12: {
        if (!has_target)
            return true;
        qa_vec3 offset = action == Q1_ACTION_ZOMBIE_ATTA13   ? qa_v3(-10, -22, 30)
                         : action == Q1_ACTION_ZOMBIE_ATTB14 ? qa_v3(-10, -24, 29)
                                                             : qa_v3(-12, -19, 29);
        qa_vec3 origin =
            qa_vec_add(body.origin, qa_vec_add(qa_vec_scale(g->forward, offset.x),
                                               qa_vec_add(qa_vec_scale(g->right, offset.y),
                                                          qa_vec_scale(g->up, offset.z - 24))));
        if (!q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_ZOMBIE_Z_SHOT1_WAV], 1, 1, 1, error))
            return false;
        axes(g, body);
        qa_vec3 velocity = qa_vec_scale(qa_vec_normalize(qa_vec_sub(target.origin, origin)), 600);
        velocity.z = 200;
        return q1_projectile_spawn(g, entity->id, QA_Q1_WEAPON_COUNT, Q1_ZOMBIE_GRENADE, origin,
                                   velocity, &missile, error);
    }
    case Q1_ACTION_ZOMBIE_PAINE1:
        return qa_combat_set_health(g->services.combat, entity->id, 60, error);
    case Q1_ACTION_ZOMBIE_PAINE11:
        return qa_combat_set_health(g->services.combat, entity->id, 60, error) &&
               q1_schedule(g, entity, 5.1, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_ZOMBIE_PAINE12:
        if (!qa_combat_set_health(g->services.combat, entity->id, 60, error) ||
            !q1_sound_resource(g, entity->id, g->runtime_names[Q1_NAME_RESOURCE_ZOMBIE_Z_IDLE_WAV], 2, 2, 1, error))
            return false;
        entity->physics.solid = QA_PHYSICS_BOX;
        if (!qa_physics_walk_move(g->services.physics, entity->id, 0, 0, (float)g->elapsed, true,
                                  true, &moved, error))
            return false;
        if (!moved) {
            m->next_frame = q1_frame_index("zombie_paine11");
            entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        }
        return q1_link(g, entity, error);
    case Q1_ACTION_BOSS_DEATH9:
        return q1_effect(g, QA_BUILTIN_IMPACT, entity->id, body.origin, 0, 10, error);
    case Q1_ACTION_BOSS_DEATH10:
        return q1_remove(g, entity, error);
    case Q1_ACTION_OLD_THRASH15:
        if (++m->counter != 3)
            m->next_frame = q1_frame_index("old_thrash1");
        return true;
    case Q1_ACTION_OLD_THRASH20:
        if (g->host.finale)
            return g->host.finale(g->host.context, entity->id, true, error);
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 finale completion has no campaign owner");
        return false;
    default:
        return q1_mission_monster_action(g, entity, action, error);
    }
}
