#include "boss_internal.h"
#include "qa/game_q1_maps.h"

static bool read(qa_q1_game *g, q1_actor *e, qa_body_state *body, qa_error *error) {
    return qa_world_body_read(g->services.world, e->id, body, error);
}
static float random_signed(qa_q1_game *g) { return 2 * q1_random(g) - 1; }
static void spin(qa_q1_game *g, q1_actor *e) {
    float x = 300 * random_signed(g), y = 300 * random_signed(g), z = 300 * random_signed(g);
    e->physics.angular_velocity = qa_v3(x, y, z);
}
static bool rock(qa_q1_game *g, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction,
                 qa_vec3 velocity, const char *model, q1_actor **out, qa_error *error) {
    if (!q1_boss_shot(g, owner, origin, direction, velocity, model, Q1_FINAL_ROCK, out, error))
        return false;
    return qa_builtin_resource(&g->services, "rock", &(*out)->classname, error) &&
           q1_link(g, *out, error);
}
static bool face(qa_q1_game *g, q1_actor *e, qa_error *error) {
    q1_monster *m = &e->state.monster;
    if (q1_health(g, q1_ref_actor(g, m->enemy)) <= 0 || q1_random(g) < .02f) {
        qa_builtin_snapshot_frame *snapshot;
        if (!q1_snapshot_players(g, &snapshot, error))
            return false;
        qa_actor_id first = {0}, next = {0};
        for (size_t i = 0; i < snapshot->snapshot.count; ++i) {
            qa_actor_id id = snapshot->snapshot.ids[i];
            if (!first.registry || id.slot < first.slot)
                first = id;
            if (id.slot > (q1_ref_present(m->enemy) ? q1_ref_actor(g, m->enemy).slot : 0) &&
                (!next.registry || id.slot < next.slot))
                next = id;
        }
        qa_builtin_snapshot_release(snapshot);
        m->enemy = q1_ref_from(g, next.registry ? next : first);
    }
    return q1_health(g, q1_ref_actor(g, m->enemy)) <= 0 || q1_monster_face(g, e, error);
}
bool q1_final_awake(qa_q1_game *g, q1_actor *e, qa_actor_id activator, qa_error *error) {
    qa_body_state body;
    if (!read(g, e, &body, error))
        return false;
    q1_monster *m = &e->state.monster;
    e->physics.solid = QA_PHYSICS_BOX;
    e->physics.motion = QA_PHYSICS_STEP;
    e->aimed_damage = true;
    m->attack_finished = g->time + 3;
    m->enemy = q1_ref_from(g, activator);
    m->source.boss.awake = true;
    body.bounds = m->species->bounds;
    e->max_health = 12000;
    e->physics.yaw_speed = 20;
    return q1_boss_damageable(g, e, true, error) && q1_model(g, e, "progs/boss.mdl", error) &&
           qa_world_body_write(g->services.world, e->id, &body, error) && q1_link(g, e, error) &&
           qa_combat_set_health(g->services.combat, e->id, 12000, error) &&
           q1_effect(g, QA_BUILTIN_IMPACT, e->id, body.origin, 0, 10, error) &&
           (!q1_alive(g, e->id) || q1_monster_play(g, e, "boss_final_rise1", error));
}
bool q1_final_pain(qa_q1_game *g, q1_actor *e, qa_error *error) {
    q1_monster *m = &e->state.monster;
    if (m->attack_finished > g->time || m->pain_finished > g->time || m->source.boss.immune)
        return true;
    const char *frame = NULL;
    if (m->source.boss.stage == 0) {
        frame = "boss_final_shocka1";
        m->source.boss.stage = 1;
    }
    static const float fractions[] = {.83f, .66f, .5f, .33f, .16f};
    for (unsigned i = 0; i < 5; ++i)
        if (m->source.boss.stage == i + 1 && q1_health(g, e->id) < e->max_health * fractions[i]) {
            frame = i == 2 || i == 4 ? "boss_final_shockb1" : "boss_final_shocka1";
            ++m->source.boss.stage;
        }
    if (!frame)
        return true;
    m->source.boss.immune = true;
    if (!q1_sound(g, e->id, "boss1/pain.wav", 1, 1, error))
        return false;
    m->pain_finished = g->time + 3;
    return !q1_alive(g, e->id) || q1_monster_play(g, e, frame, error);
}
static bool muzzle(qa_q1_game *g, q1_actor *e, qa_vec3 offset, qa_vec3 angles, qa_vec3 *out,
                   qa_error *error) {
    qa_body_state body;
    if (!read(g, e, &body, error))
        return false;
    qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
    *out = qa_vec_add(qa_vec_add(body.origin, qa_vec_scale(g->forward, offset.x)),
                      qa_vec_add(qa_vec_scale(g->right, offset.y), qa_v3(0, 0, offset.z)));
    return true;
}
static bool radius(qa_q1_game *g, q1_actor *e, float amount, qa_error *error) {
    qa_body_state source;
    if (!read(g, e, &source, error))
        return false;
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    bool ok = true;
    for (size_t i = snapshot->snapshot.count; i > 0; --i) {
        qa_actor_id id = snapshot->snapshot.ids[i - 1];
        qa_body_state body;
        if (qa_actor_id_equal(id, e->id) || q1_classnamed(g, id, "monster_lava_man") ||
            !q1_damageable(g, id) || !qa_world_body_read(g->services.world, id, &body, NULL))
            continue;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f));
        float distance = qa_vec_length(qa_vec_sub(source.origin, center));
        if (distance > amount + 40)
            continue;
        float points = amount - .5f * distance;
        bool visible;
        if (points > 0) {
            if (!q1_can_damage(g, id, e->id, &visible, error) ||
                (visible && !q1_damage(g, id, e->id, e->id, points, QA_Q1_WEAPON_COUNT, error))) {
                ok = false;
                break;
            }
            if (!q1_alive(g, e->id))
                break;
        }
    }
    qa_builtin_snapshot_release(snapshot);
    return ok;
}
static bool blast(qa_q1_game *g, q1_actor *e, qa_vec3 offset, float spread, bool effect,
                  qa_error *error) {
    qa_body_state body, target = {0};
    if (!read(g, e, &body, error))
        return false;
    (void)qa_world_body_read(g->services.world, q1_ref_actor(g, e->state.monster.enemy), &target, NULL);
    float speed = g->options.skill > 2 ? 500 : g->options.skill > 0 ? 450 : 400;
    float width = .15f + .2f * fabsf(spread);
    unsigned count = (unsigned)floorf(q1_random(g) * 6 + .5f);
    qa_vec3 angles = q1_boss_angles(qa_vec_sub(target.origin, body.origin)), origin;
    if (!muzzle(g, e, offset, angles, &origin, error))
        return false;
    qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
    qa_vec3 right = g->right, up = g->up;
    float time = qa_vec_length(qa_vec_sub(target.origin, origin)) / speed;
    target.velocity.z = 0;
    qa_vec3 direction = qa_vec_normalize(
        qa_vec_sub(qa_vec_add(target.origin, qa_vec_scale(target.velocity, time / 4)), origin));
    if (effect && !q1_effect(g, QA_BUILTIN_EXPLOSION, e->id, origin, 0, 0, error))
        return false;
    for (unsigned remaining = count; remaining; --remaining) {
        if (!q1_alive(g, e->id))
            return true;
        float r = random_signed(g) * width, u = random_signed(g) * width;
        qa_vec3 aim = qa_vec_normalize(
            qa_vec_add(direction, qa_vec_add(qa_vec_scale(right, r), qa_vec_scale(up, u))));
        q1_actor *shot;
        if (!rock(g, e->id, qa_vec_add(origin, qa_vec_scale(aim, 8)), aim, aim,
                  "progs/rogue/sphere.mdl", &shot, error))
            return false;
        qa_body_state projectile;
        if (!read(g, shot, &projectile, error))
            return false;
        projectile.velocity = qa_vec_scale(aim, speed + random_signed(g) * 100);
        if (!qa_world_body_write(g->services.world, shot->id, &projectile, error))
            return false;
        spin(g, shot);
        if (remaining % 2 == 0)
            shot->effects |= 64;
    }
    return true;
}
static bool line(qa_q1_game *g, q1_actor *e, qa_vec3 offset, qa_error *error) {
    unsigned count = g->options.skill == 0   ? 4
                     : g->options.skill == 1 ? 8
                     : g->options.skill == 3 ? 15
                                             : 11;
    qa_body_state body;
    if (!read(g, e, &body, error))
        return false;
    qa_vec3 origin;
    if (!muzzle(g, e, offset, body.angles, &origin, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    qa_vec3 right = g->right;
    qa_vec3 direction = qa_vec_sub(q1_boss_target(g, e), origin);
    direction.z = 0;
    direction = qa_vec_normalize(direction);
    if (!q1_effect(g, QA_BUILTIN_EXPLOSION, e->id, origin, 0, 0, error))
        return false;
    float start = 4.0f / (float)count * (offset.y < 0 ? 1 : -1),
          increment = 2.0f / (float)count * (offset.y < 0 ? -1 : 1);
    for (unsigned i = 0; i < count; ++i) {
        if (!q1_alive(g, e->id))
            return true;
        if (!radius(g, e, 10 * q1_random(g) + 10, error))
            return false;
        if (!q1_alive(g, e->id))
            return true;
        qa_vec3 aim = qa_vec_normalize(
            qa_vec_add(direction, qa_vec_add(qa_vec_scale(right, start),
                                             qa_vec_scale(right, increment * (float)i))));
        aim.z = 0;
        aim = qa_vec_normalize(aim);
        q1_actor *shot;
        if (!rock(g, e->id, qa_vec_add(origin, qa_vec_scale(aim, 8)), aim, aim,
                  "progs/rogue/sphere.mdl", &shot, error))
            return false;
        qa_body_state projectile;
        if (!read(g, shot, &projectile, error))
            return false;
        projectile.velocity = qa_vec_scale(aim, 300 + random_signed(g) * 100);
        if (q1_random(g) > .5f)
            projectile.velocity.z = (q1_random(g) + 1) * 8;
        if (!qa_world_body_write(g->services.world, shot->id, &projectile, error))
            return false;
        spin(g, shot);
        if (i % 2 == 0) {
            shot->count = 1;
            shot->effects |= 64;
        }
    }
    return true;
}
static bool missile(qa_q1_game *g, q1_actor *e, qa_vec3 offset, qa_error *error) {
    if (e->spawnflags & 2) {
        (void)q1_random(g);
        return line(g, e, offset, error);
    }
    qa_body_state body;
    if (!read(g, e, &body, error))
        return false;
    qa_vec3 target = q1_boss_target(g, e), origin;
    if (!muzzle(g, e, offset, q1_boss_angles(qa_vec_sub(target, body.origin)), &origin, error))
        return false;
    qa_vec3 direction = qa_vec_normalize(qa_vec_sub(target, origin));
    q1_actor *shot;
    if (!q1_boss_shot(g, e->id, origin, direction, qa_vec_scale(direction, 300),
                      "progs/lavaball.mdl", Q1_ROCKET, &shot, error))
        return false;
    shot->physics.angular_velocity = qa_v3(200, 100, 300);
    if (!q1_sound(g, e->id, "boss1/throw.wav", 1, 1, error))
        return false;
    return !q1_alive(g, e->id) || q1_health(g, q1_ref_actor(g, e->state.monster.enemy)) > 0 ||
           q1_monster_play(g, e, "boss_final_idle1", error);
}
static bool upgrade(qa_q1_game *g, q1_actor *e, qa_error *error) {
    q1_monster *m = &e->state.monster;
    e->wait++;
    m->source.boss.immune = false;
    int index = e->wait == 2 ? 0 : e->wait == 3 ? 3 : e->wait == 4 ? 1 : e->wait == 5 ? 2 : -1;
    if (e->wait == 3) {
        e->frame = 0;
        m->source.boss.immune = true;
        if (!q1_schedule(g, e, 0, Q1_THINK_NONE, error))
            return false;
    }
    if (index >= 0 && !q1_boss_targets(g, e, q1_ref_actor(g, e->activator), m->source.boss.waves[index], error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    if (e->wait == 3 && !q1_final_teleport(g, false, error))
        return false;
    if (e->wait == 5) {
        qa_body_state body;
        if (!read(g, e, &body, error))
            return false;
        qa_vec3 direction = qa_vec_sub(q1_boss_target(g, e), body.origin);
        direction.z = 0;
        qa_body_state spawn = {.origin = qa_vec_add(body.origin, qa_v3(0, 0, 60)),
                               .angles = q1_boss_angles(qa_vec_normalize(direction))};
        q1_actor *spiral;
        if (!q1_boss_child_create(g, "", Q1_CHILD_FINAL_SPIRAL, e->id, &spiral, error))
            return false;
        spiral->delay = g->options.skill <= 2 ? .2f : .15f;
        spiral->count = 50;
        return qa_world_body_write(g->services.world, spiral->id, &spawn, error) &&
               q1_schedule(g, spiral, spiral->delay, Q1_THINK_BOSS_CHILD, error);
    }
    return true;
}
static bool death_start(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (!q1_sound(g, e->id, "boss1/death.wav", 2, 1, error))
        return false;
    qa_body_state body;
    if (!read(g, e, &body, error))
        return false;
    qa_body_state spawn = {.origin = qa_vec_add(body.origin, qa_v3(0, 0, 60))};
    for (unsigned delay = 1; delay <= 5; delay += 2) {
        q1_actor *timer;
        if (!q1_boss_child_create(g, "", Q1_CHILD_FINAL_CIRCLE, (qa_actor_id){0}, &timer, error))
            return false;
        if (!qa_world_body_write(g->services.world, timer->id, &spawn, error) ||
            !q1_schedule(g, timer, delay, Q1_THINK_BOSS_CHILD, error))
            return false;
    }
    return true;
}
static bool death_finish(qa_q1_game *g, q1_actor *e, qa_error *error) {
    q1_monster *m = &e->state.monster;
    if (!m->counted_death) {
        m->counted_death = true;
        if (!q1_monster_death_report(g, e, q1_ref_actor(g, m->enemy), true, error))
            return false;
    }
    ++m->source.boss.stage;
    while (m->source.boss.stage < 5) {
        if (!q1_boss_targets(g, e, q1_ref_actor(g, e->activator), e->target, error))
            return false;
        if (!q1_alive(g, e->id))
            return true;
        ++m->source.boss.stage;
    }
    if (!q1_sound(g, e->id, "boss2/pop2.wav", 2, 1, error) || !q1_boss_gib_vectors(g, e, error))
        return false;
    q1_actor *timer;
    return q1_boss_child_create(g, "", Q1_CHILD_FINAL_END, (qa_actor_id){0}, &timer, error) &&
           q1_schedule(g, timer, 8, Q1_THINK_BOSS_CHILD, error) && q1_remove(g, e, error);
}
bool q1_final_action(qa_q1_game *g, q1_actor *e, q1_frame_action action, qa_error *error) {
    q1_monster *m = &e->state.monster;
    if (action >= Q1_ACTION_BOSS_FINAL_IDLE10 && action <= Q1_ACTION_BOSS_FINAL_IDLE9) {
        if (!face(g, e, error))
            return false;
        return action != Q1_ACTION_BOSS_FINAL_IDLE31 ||
               q1_monster_play(g, e, q1_ref_present(m->enemy) ? "boss_final_missile1" : "boss_final_idle1",
                               error);
    }
    switch (action) {
    case Q1_ACTION_BOSS_FINAL_DECIDE:
        return q1_monster_play(
            g, e, e->wait > 0 && q1_random(g) < .3f ? "boss_final_mg1" : "boss_final_missile1",
            error);
    case Q1_ACTION_BOSS_FINAL_RISE1:
        return q1_sound(g, e->id, "boss1/out1.wav", 1, 1, error);
    case Q1_ACTION_BOSS_FINAL_RISE2:
        return q1_sound(g, e->id, "boss1/sight1.wav", 2, 1, error);
    case Q1_ACTION_BOSS_FINAL_SHOCKA10:
    case Q1_ACTION_BOSS_FINAL_SHOCKB10:
        return upgrade(g, e, error);
    case Q1_ACTION_BOSS_FINAL_SHOCKA2:
    case Q1_ACTION_BOSS_FINAL_SHOCKA5:
    case Q1_ACTION_BOSS_FINAL_SHOCKA8:
    case Q1_ACTION_BOSS_FINAL_SHOCKB2:
    case Q1_ACTION_BOSS_FINAL_SHOCKB5:
    case Q1_ACTION_BOSS_FINAL_SHOCKB8:
    case Q1_ACTION_BOSS_FINAL_SHOCKC2:
    case Q1_ACTION_BOSS_FINAL_SHOCKC5:
    case Q1_ACTION_BOSS_FINAL_SHOCKC8:
        return q1_boss_pain_lightning(g, e, qa_v3(0, 0, 100), error);
    case Q1_ACTION_BOSS_FINAL_MG7:
        m->source.boss.shots = m->source.boss.shocks =
            (int32_t)(((float)g->options.skill + 3) / 6 * 30 + floorf(random_signed(g) * 10 + .5f));
        if (g->options.skill > 0)
            m->attack_finished = g->time + 5;
        return face(g, e, error);
    case Q1_ACTION_BOSS_FINAL_MG8: {
        int32_t shots = m->source.boss.shots;
        bool wide = shots % 2 == 0;
        float spread = 1 - (float)shots / (float)m->source.boss.shocks;
        spread *= spread;
        if (!blast(g, e, qa_v3(270, 60, 210), wide ? spread : spread * .5f, wide, error))
            return false;
        if (!q1_alive(g, e->id))
            return true;
        if (shots != 0)
            m->next_frame = q1_frame_index("boss_final_mg8");
        m->source.boss.shots = shots - 1;
        return face(g, e, error);
    }
    case Q1_ACTION_BOSS_FINAL_MISSILE1:
        m->source.boss.immune = false;
        return face(g, e, error);
    case Q1_ACTION_BOSS_FINAL_MISSILE10:
        return missile(g, e, qa_v3(200, 100, 60), error);
    case Q1_ACTION_BOSS_FINAL_MISSILE21:
        return missile(g, e, qa_v3(200, -100, 60), error);
    case Q1_ACTION_BOSS_FINAL_DEATH1:
        return death_start(g, e, error);
    case Q1_ACTION_BOSS_FINAL_DEATH9: {
        qa_body_state body;
        if (!read(g, e, &body, error))
            return false;
        return q1_sound(g, e->id, "boss1/out1.wav", 4, 1, error) &&
               q1_effect(g, QA_BUILTIN_IMPACT, e->id, body.origin, 0, 10, error);
    }
    case Q1_ACTION_BOSS_FINAL_DEATH10:
        return death_finish(g, e, error);
    default:
        if ((action >= Q1_ACTION_BOSS_FINAL_MG1 && action <= Q1_ACTION_BOSS_FINAL_MG8) ||
            (action >= Q1_ACTION_BOSS_FINAL_MISSILE1 && action <= Q1_ACTION_BOSS_FINAL_MISSILE9))
            return face(g, e, error);
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown final boss continuation");
        return false;
    }
}
bool q1_final_rock_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other,
                         const qa_touch_contact *contact, qa_error *error) {
    if (q1_ref_equal(e->owner, q1_ref_from(g, other)))
        return true;
    if (q1_classnamed(g, other, "monster_orb") || q1_classnamed(g, other, "monster_lava_man"))
        return q1_remove(g, e, error);
    qa_physics_properties p;
    if (q1_classnamed(g, other, "rock") ||
        (g->services.physics->services.read(g->services.physics->services.context, other, &p) &&
         p.solid == QA_PHYSICS_TRIGGER))
        return true;
    qa_body_state body;
    if (!read(g, e, &body, error))
        return false;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    if (qa_collision_point_contents_export(contents.contents, QA_COLLISION_Q1, contents.q1_opaque_token) == -6)
        return q1_remove(g, e, error);
    if (q1_damageable(g, other)) {
        float up = q1_random(g) - .5f, right = q1_random(g) - .5f;
        qa_vec3 direction = qa_vec_normalize(
            qa_vec_add(qa_vec_normalize(body.velocity),
                       qa_vec_add(qa_vec_scale(g->up, up), qa_vec_scale(g->right, right))));
        qa_vec3 normal = contact && contact->has_plane ? contact->plane.normal : qa_v3(0, 0, 0);
        qa_vec3 spray = qa_vec_scale(qa_vec_add(direction, qa_vec_scale(normal, 2)), 40);
        qa_builtin_event event = {.kind = QA_BUILTIN_PARTICLES,
                                  .family = QA_GAME_Q1,
                                  .provider = g->options.provider,
                                  .actor = e->id,
                                  .time_ns = g->time_ns,
                                  .origin = qa_vec_add(body.origin, qa_vec_scale(spray, .01f)),
                                  .direction = qa_vec_scale(spray, .1f),
                                  .code = 73,
                                  .count = 36};
        if (!qa_builtin_emit(&g->services, &event, error) ||
            !q1_damage(g, other, e->id, q1_ref_actor(g, e->owner), 18, QA_Q1_WEAPON_COUNT, error))
            return false;
    } else if (e->count != 0 && !q1_effect(g, QA_BUILTIN_IMPACT, e->id, body.origin, 0, 8, error))
        return false;
    return !q1_alive(g, e->id) || q1_remove(g, e, error);
}
bool q1_final_end(qa_q1_game *g, qa_error *error) {
    q1_ref first;
    if (!q1_boss_first_player(g, &first, error))
        return false;
    if (g->destroy_pending)
        return true;
    if (!g->options.coop && q1_health(g, q1_ref_actor(g, first)) <= 0)
        return true;
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_players(g, &snapshot, error))
        return false;
    uint32_t flags = qa_q1_game_campaign_flags(g);
    bool reset =
        (flags & QA_Q1_BLOODY_NIGHTMARE_ACTIVE) && !(flags & QA_Q1_BLOODY_NIGHTMARE_NEWGAME);
    bool ok = true;
    for (size_t i = 0; !g->destroy_pending && i < snapshot->snapshot.count; ++i) {
        qa_actor_id actor = snapshot->snapshot.ids[i];
        q1_player *player = q1_player_get(g, actor);
        if (!player)
            continue;
        for (unsigned ammo = 0; q1_alive(g, actor) && ammo < 4; ++ammo) {
            qa_inventory_entry entry;
            qa_error local = {0};
            if (!qa_inventory_entry_read(g->services.inventory, actor, g->ammo[ammo], &entry,
                                         &local)) {
                if (local.code == QA_ERROR_NOT_FOUND)
                    continue;
                if (error)
                    *error = local;
                ok = false;
                break;
            }
            entry.count = ammo == QA_Q1_SHELLS ? 25 : 0;
            if (!qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, error)) {
                ok = false;
                break;
            }
        }
        if (!ok)
            break;
        if (!q1_alive(g, actor))
            continue;
        qa_armor armor = {.regular.kind = QA_ARMOR_NONE, .powered.kind = QA_POWER_NONE};
        if (!qa_combat_set_armor(g->services.combat, actor, &armor, error)) {
            ok = false;
            break;
        }
        player = q1_player_get(g, actor);
        if (!player)
            continue;
        player->weapon = QA_Q1_SHOTGUN;
        if (reset) {
            uint32_t bloody = player->mg3_progress.bloody;
            player->mg3_progress = (qa_q1_mg3_progress){.bloody = bloody};
        }
    }
    qa_builtin_snapshot_release(snapshot);
    return ok && (g->destroy_pending || qa_q1_game_map_finish_addon(g, QA_Q1_MAP_END_MG3, error));
}
bool q1_final_child_think(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (e->state.boss_child.kind == Q1_CHILD_FINAL_END)
        return q1_final_end(g, error);
    qa_body_state body;
    if (!read(g, e, &body, error))
        return false;
    bool spiral = e->state.boss_child.kind == Q1_CHILD_FINAL_SPIRAL;
    if (spiral) {
        body.angles.y += 5;
        if (!qa_world_body_write(g->services.world, e->id, &body, error))
            return false;
        qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
        if (g->options.skill < 2 && (int32_t)e->count % 10 < 5) {
            e->count--;
            return q1_schedule(g, e, e->delay, Q1_THINK_BOSS_CHILD, error);
        }
    }
    for (unsigned remaining = spiral ? 4 : 72; remaining; --remaining) {
        qa_vec3 angles = spiral ? qa_vec_add(body.angles, qa_v3(0, 90 * (float)(remaining - 1), 0))
                                : qa_v3(0, (72 - (float)remaining) * 5, 0);
        qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
        qa_vec3 direction = g->forward;
        q1_actor *shot;
        qa_vec3 origin =
            spiral ? qa_vec_add(body.origin, qa_vec_scale(direction, 100)) : body.origin;
        if (!rock(g, q1_ref_actor(g, spiral ? e->owner : q1_ref_from(g, e->id)), origin, direction, direction,
                  spiral ? "progs/rogue/plasma.mdl" : "progs/rogue/sphere.mdl", &shot, error))
            return false;
        qa_body_state projectile;
        if (!read(g, shot, &projectile, error))
            return false;
        projectile.velocity = qa_vec_scale(direction, spiral ? 100 : 250 + random_signed(g) * 100);
        if (!qa_world_body_write(g->services.world, shot->id, &projectile, error))
            return false;
        spin(g, shot);
        if (!q1_schedule(g, shot, spiral ? 30 : 20, Q1_THINK_REMOVE, error))
            return false;
        if (!spiral && remaining % 4 == 0)
            shot->effects |= 8;
    }
    if (!spiral)
        return q1_remove(g, e, error);
    e->count--;
    return q1_health(g, q1_ref_actor(g, e->owner)) <= 0 ? q1_remove(g, e, error)
                                       : q1_schedule(g, e, e->delay, Q1_THINK_BOSS_CHILD, error);
}
bool q1_final_map_spawn(qa_q1_game *g, q1_actor *e, bool *handled, qa_error *error) {
    bool boss = q1_classnamed(g, e->id, "info_boss_teleport_boss"),
         first = q1_classnamed(g, e->id, "info_boss_teleport_first"),
         second = q1_classnamed(g, e->id, "info_boss_teleport_second");
    *handled = boss || first || second || q1_classnamed(g, e->id, "trigger_boss_teleport");
    if (!boss && !first && !second)
        return true;
    qa_body_state body;
    if (!read(g, e, &body, error))
        return false;
    e->initial_angles = body.angles;
    e->model = 0;
    body.angles = qa_v3(0, 0, 0);
    if (!boss)
        body.origin.z += 27;
    return qa_world_body_write(g->services.world, e->id, &body, error) && q1_link(g, e, error);
}
bool q1_final_teleport(qa_q1_game *g, bool variant, qa_error *error) {
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    qa_actor_id boss = {0}, destination = {0};
    const char *classname = variant ? "info_boss_teleport_second" : "info_boss_teleport_first";
    for (size_t i = 0; i < snapshot->snapshot.count; ++i) {
        q1_actor *e = q1_entity(g, snapshot->snapshot.ids[i]);
        if (!e)
            continue;
        if (!boss.registry && q1_classnamed(g, e->id, "monster_boss"))
            boss = e->id;
        if (!destination.registry && q1_classnamed(g, e->id, "info_boss_teleport_boss"))
            destination = e->id;
        if (q1_classnamed(g, e->id, classname))
            e->wait = 0;
    }
    bool ok = true;
    if (variant && boss.registry && destination.registry) {
        q1_actor *e = q1_entity(g, boss);
        qa_body_state body, point;
        if (!read(g, e, &body, error) ||
            !qa_world_body_read(g->services.world, destination, &point, error))
            ok = false;
        else {
            body.origin = point.origin;
            e->state.monster.next_frame = q1_frame_index("boss_final_rise1");
            ok = qa_world_body_write(g->services.world, boss, &body, error) &&
                 q1_link(g, e, error) &&
                 q1_effect(g, QA_BUILTIN_IMPACT, boss, body.origin, 0, 10, error) &&
                 q1_schedule(g, e, .1, Q1_THINK_MONSTER_FRAME, error);
        }
    }
    qa_builtin_snapshot_frame *players = NULL;
    if (ok) ok = q1_snapshot_players(g, &players, error);
    for (size_t p = 0; ok && p < players->snapshot.count; ++p) {
        q1_actor *point = NULL;
        for (size_t i = 0; i < snapshot->snapshot.count; ++i) {
            q1_actor *candidate = q1_entity(g, snapshot->snapshot.ids[i]);
            if (candidate && candidate->wait == 0 && q1_classnamed(g, candidate->id, classname)) {
                point = candidate;
                break;
            }
        }
        if (!point) {
            qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE,
                                      .family = QA_GAME_Q1,
                                      .provider = g->options.provider,
                                      .code = 2,
                                      .time_ns = g->time_ns};
            ok = qa_builtin_resource(&g->services,
                                     "ERROR: Could not find valid teleport destination!\n",
                                     &event.text, error) &&
                 qa_builtin_emit(&g->services, &event, error);
            break;
        }
        point->wait = 1;
        qa_body_state target, body;
        if (!read(g, point, &target, error)) {
            ok = false;
            break;
        }
        qa_actor_id player = players->snapshot.ids[p];
        if (!qa_world_body_read(g->services.world, player, &body, NULL))
            continue;
        body.origin = target.origin;
        body.angles = point->initial_angles;
        body.velocity = qa_v3(0, 0, 300);
        body.ground = (qa_actor_reference){0};
        q1_actor *native = q1_entity(g, player);
        if (native)
            native->physics.flags &= ~(uint32_t)QA_PHYSICS_FLYING;
        q1_player *state = q1_player_get(g, player);
        if (state)
            state->input.view_angles = body.angles;
        if (!qa_world_body_write(g->services.world, player, &body, error) ||
            !qa_world_link(g->services.world, player, NULL, error)) {
            ok = false;
            break;
        }
        if (!g->services.motion_changed) {
            qa_error_set(error, QA_ERROR_ARGUMENT, player.slot,
                         "MG3 boss teleport requires movement continuation");
            ok = false;
            break;
        }
        qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_TELEPORT,
                                           .body = body,
                                           .view_angles = body.angles,
                                           .force_view_angles = true};
        if (!g->services.motion_changed(g->services.context, player, &change, error)) {
            ok = false;
            break;
        }
        qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
        ok = qa_q1_spawn_teleport_fog(g, qa_vec_add(body.origin, qa_vec_scale(g->forward, 32)),
                                      NULL, error);
    }
    if (players) qa_builtin_snapshot_release(players);
    qa_builtin_snapshot_release(snapshot);
    return ok;
}
