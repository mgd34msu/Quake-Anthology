#include "internal.h"
#include <float.h>

static bool ammo(qa_q1_game *g, q1_actor **source, qa_q1_ammo item, float used, qa_error *error) {
    qa_actor_id id = (*source)->id;
    double count;
    if (!qa_inventory_adjust(g->services.inventory, id, g->ammo[item], -used, &count,
                             error))
        return false;
    *source = q1_entity(g, id);
    if (!*source)
        return true;
    if (!isfinite(count) || fabs(count) >= 0x1.ffffffp127) {
        qa_error_set(error, QA_ERROR_FORMAT, id.slot,
                     "Gremlin current ammo exceeds finite native storage");
        return false;
    }
    (*source)->state.monster.source.gremlin.current_ammo = fabs(count) > FLT_MAX
        ? (signbit(count) ? -FLT_MAX : FLT_MAX) : (float)count;
    return true;
}
bool q1_gremlin_has_ammo(q1_actor *entity) {
    if (entity->state.monster.source.gremlin.current_ammo > 0)
        return true;
    entity->state.monster.source.gremlin.stolen = false;
    return false;
}
bool q1_gremlin_steal(qa_q1_game *g, q1_actor *entity, bool *out, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    *out = false;
    qa_q1_target traits;
    qa_body_state body, target;
    if (m->source.gremlin.stolen || !q1_target(g, q1_ref_actor(g, m->enemy), &traits) || !traits.player)
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !qa_world_body_read(g->services.world, q1_ref_actor(g, m->enemy), &target, error))
        return false;
    if (qa_vec_length(qa_vec_sub(target.origin, body.origin)) > 100 || q1_random(g) < 0.5f)
        return true;
    q1_player *victim = q1_player_get(g, q1_ref_actor(g, m->enemy));
    if (!victim || !victim->arsenal)
        return true;
    qa_q1_weapon weapon = victim->weapon;
    if (weapon == QA_Q1_AXE || weapon == QA_Q1_SHOTGUN || weapon == QA_Q1_MJOLNIR)
        return true;
    qa_inventory_entry entry;
    bool consumed;
    if (!qa_inventory_entry_read(g->services.inventory, victim->id, g->weapons[weapon], &entry,
                                 error) ||
        !qa_inventory_consume(g->services.inventory, victim->id, entry.item, entry.count, &consumed,
                              error))
        return false;
    entry = (qa_inventory_entry){
        .item = g->weapons[weapon], .count = 1, .capacity = 1, .policy = QA_COUNT_SOURCE_FLOAT};
    if (!qa_inventory_configure(g->services.inventory, entity->id, &entry, NULL, NULL, error))
        return false;
    m->source.gremlin.weapon = weapon;
    int item = q1_weapon_ammo(weapon);
    if (item >= 0) {
        double amount = fmin(
            q1_ammo_count(g, victim->id, (qa_q1_ammo)item),
            weapon == QA_Q1_SUPER_SHOTGUN                                                    ? 20
            : weapon == QA_Q1_GRENADE || weapon == QA_Q1_ROCKET || weapon == QA_Q1_PROXIMITY ? 5
                                                                                             : 40);
        if (!qa_inventory_consume(g->services.inventory, victim->id, g->ammo[item], amount,
                                  &consumed, error))
            return false;
        entry =
            (qa_inventory_entry){.item = g->ammo[item],
                                 .count = q1_ammo_count(g, entity->id, (qa_q1_ammo)item) + amount,
                                 .capacity = 1000000,
                                 .policy = QA_COUNT_SOURCE_FLOAT};
        if (!qa_inventory_configure(g->services.inventory, entity->id, &entry, NULL, NULL, error))
            return false;
        m->source.gremlin.current_ammo = (float)entry.count;
        static const struct {
            qa_q1_weapon weapon;
            const char *localized, *classic;
        } labels[] = {
            {QA_Q1_SUPER_SHOTGUN, "$qc_gremlin_ssg", "Gremlin stole your Super Shotgun\n"},
            {QA_Q1_NAILGUN, "$qc_gremlin_ng", "Gremlin stole your Nailgun\n"},
            {QA_Q1_SUPER_NAILGUN, "$qc_gremlin_sng", "Gremlin stole your Super Nailgun\n"},
            {QA_Q1_GRENADE, "$qc_gremlin_gl", "Gremlin stole your Grenade Launcher\n"},
            {QA_Q1_ROCKET, "$qc_gremlin_rl", "Gremlin stole your Rocket Launcher\n"},
            {QA_Q1_LIGHTNING, "$qc_gremlin_lg", "Gremlin stole your Lightning Gun\n"},
            {QA_Q1_LASER, "$qc_gremlin_lc", "Gremlin stole your Laser Cannon\n"},
            {QA_Q1_PROXIMITY, "$qc_gremlin_prox", "Gremlin stole your Proximity Gun\n"}};
        for (size_t i = 0; i < sizeof(labels) / sizeof(*labels); ++i)
            if (labels[i].weapon == weapon &&
                !q1_message(g, victim->id,
                            g->options.edition == QA_Q1_RERELEASE ? labels[i].localized
                                                                  : labels[i].classic,
                            error))
                return false;
    }
    if (!q1_alive(g, victim->id) || !q1_alive(g, entity->id))
        return true;
    if (!qa_q1_player_select(g, victim->id, q1_best_weapon(g, victim), error))
        return false;
    m->source.gremlin.stolen = true;
    m->attack_finished = g->time;
    m->source.gremlin.last_victim = q1_ref_from(g, q1_random(g) > 0.65f ? victim->id : entity->id);
    qa_actor_id next;
    if (!q1_gremlin_find_victim(g, entity, &next, error))
        return false;
    if (next.registry) {
        if (!q1_monster_found(g, entity, next, error))
            return false;
        m->attack_finished = g->time;
        m->search_until = g->time + 1;
    }
    *out = true;
    return true;
}
static float aim_float(double value) {
    if (isnan(value))
        return NAN;
    if (fabs(value) >= 0x1.ffffffp127)
        return value < 0 ? -INFINITY : INFINITY;
    return fabs(value) > FLT_MAX ? (value < 0 ? -FLT_MAX : FLT_MAX) : (float)value;
}
static qa_vec3 aim_scale(qa_vec3 value, double scale) {
    return qa_v3(aim_float((double)value.x * scale), aim_float((double)value.y * scale),
                 aim_float((double)value.z * scale));
}
static qa_vec3 aim_normalize(qa_vec3 value) {
    float x = aim_float((double)value.x * value.x);
    float y = aim_float((double)value.y * value.y);
    float z = aim_float((double)value.z * value.z);
    float xy = aim_float((double)x + y);
    float squared = aim_float((double)xy + z);
    float magnitude = aim_float(sqrt((double)squared));
    return magnitude == 0 ? qa_v3(0, 0, 0) : aim_scale(value, 1.0 / (double)magnitude);
}
static qa_vec3 aim_angles(qa_vec3 direction) {
    const double pi = 3.14159265358979323846264338327950288;
    bool vertical = direction.x == 0 && direction.y == 0;
    double yaw = vertical ? 0 : atan2((double)direction.y, direction.x) * 180 / pi;
    double pitch = vertical ? (direction.z > 0 ? 90 : 270)
        : atan2((double)direction.z, hypot((double)direction.x, direction.y)) * 180 / pi;
    return qa_v3(aim_float(pitch < 0 ? pitch + 360 : pitch),
                 aim_float(yaw < 0 ? yaw + 360 : yaw), 0);
}
static void aim_vectors(qa_q1_game *g, qa_vec3 angles) {
    const double pi = 3.14159265358979323846264338327950288;
    double yaw = (double)angles.y * pi / 180;
    double pitch = (double)angles.x * pi / 180;
    double roll = (double)angles.z * pi / 180;
    double sy = sin(yaw), cy = cos(yaw), sp = sin(pitch), cp = cos(pitch);
    double sr = sin(roll), cr = cos(roll);
    g->forward = qa_v3(aim_float(cp * cy), aim_float(cp * sy), aim_float(-sp));
    g->right = qa_v3(aim_float(-sr * sp * cy + cr * sy),
                      aim_float(-sr * sp * sy - cr * cy), aim_float(-sr * cp));
    g->up = qa_v3(aim_float(cr * sp * cy + sr * sy),
                   aim_float(cr * sp * sy - sr * cy), aim_float(cr * cp));
}
static bool aim(qa_q1_game *g, q1_actor **source, double spread, qa_vec3 *out, qa_error *error) {
    q1_actor *entity = *source;
    qa_actor_id id = entity->id, enemy = q1_ref_actor(g, entity->state.monster.enemy);
    qa_body_state body, target = {0};
    if (enemy.registry && !qa_world_body_read(g->services.world, enemy, &target, NULL))
        target = (qa_body_state){0};
    *source = entity = q1_entity(g, id);
    if (!entity)
        return true;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    *source = entity = q1_entity(g, id);
    if (!entity)
        return true;
    qa_vec3 direction = aim_normalize(qa_vec_sub(target.origin, body.origin));
    qa_vec3 angles = aim_angles(direction);
    entity->state.monster.source.gremlin.view_angles = angles;
    aim_vectors(g, angles);
    double right = ((double)q1_random(g) * 2 - 1) * spread;
    qa_vec3 offset = qa_vec_add(direction, aim_scale(g->right, right));
    double up = ((double)q1_random(g) * 2 - 1) * spread;
    *out = aim_normalize(qa_vec_add(offset, aim_scale(g->up, up)));
    return true;
}
bool q1_gremlin_fire_nail(qa_q1_game *g, q1_actor *entity, bool laser, qa_error *error) {
    if (!ammo(g, &entity, laser ? QA_Q1_CELLS : QA_Q1_NAILS, 1, error))
        return false;
    if (!entity)
        return true;
    qa_actor_id source = entity->id;
    entity->effects |= 2;
    if (!q1_sound(g, source, "weapons/rocket1i.wav", 1, 1, error))
        return false;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    qa_vec3 direction;
    qa_body_state body;
    if (!aim(g, &entity, 0.1, &direction, error))
        return false;
    if (!entity)
        return true;
    if (!qa_world_body_read(g->services.world, source, &body, error))
        return false;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    qa_vec3 origin = qa_vec_add(body.origin, qa_v3(0, 0, 16));
    q1_actor *shot;
    return laser ? q1_hipnotic_launch_laser(g, entity->id, QA_Q1_LASER, origin, direction, false,
                                            error)
                 : q1_projectile_spawn(g, entity->id, QA_Q1_WEAPON_COUNT, Q1_SPIKE, origin,
                                       qa_vec_scale(direction, 1000), &shot, error);
}
static bool shotgun(qa_q1_game *g, q1_actor *entity, bool double_shot, qa_error *error) {
    if (!ammo(g, &entity, QA_Q1_SHELLS, double_shot ? 2 : 1, error))
        return false;
    if (!entity)
        return true;
    qa_actor_id source = entity->id;
    entity->effects |= 2;
    if (!q1_sound(g, entity->id, double_shot ? "weapons/shotgn2.wav" : "weapons/guncock.wav", 1, 1,
                  error))
        return false;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    qa_vec3 direction;
    if (!aim(g, &entity, double_shot ? 0.3 : 0.1, &direction, error))
        return false;
    if (!entity)
        return true;
    qa_vec3 angles = aim_angles(direction);
    entity->state.monster.source.gremlin.view_angles = angles;
    return q1_bullets(g, entity->id, direction, angles, double_shot ? 14 : 6,
                      double_shot ? 0.14f : 0.04f, double_shot ? 0.08f : 0.04f,
                      double_shot ? QA_Q1_SUPER_SHOTGUN : QA_Q1_SHOTGUN, error);
}
static bool missile(qa_q1_game *g, q1_actor *entity, bool proximity, qa_error *error) {
    if (!ammo(g, &entity, QA_Q1_ROCKETS, 1, error))
        return false;
    if (!entity)
        return true;
    qa_actor_id source = entity->id;
    entity->effects |= 2;
    if (!q1_sound(g, entity->id, proximity ? "weapons/grenade.wav" : "weapons/sgun1.wav", 1, 1,
                  error))
        return false;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    qa_vec3 direction;
    qa_body_state body;
    if (!aim(g, &entity, 0.1, &direction, error))
        return false;
    if (!entity)
        return true;
    if (!qa_world_body_read(g->services.world, source, &body, error))
        return false;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    if (proximity) {
        qa_vec3 velocity = qa_vec_scale(direction, 600);
        velocity.z = 200;
        return q1_hipnotic_launch_proximity(g, entity->id, body.origin, velocity, error);
    }
    qa_vec3 origin =
        qa_vec_add(body.origin, qa_vec_add(qa_vec_scale(g->forward, 8), qa_v3(0, 0, 16)));
    q1_actor *shot;
    return q1_projectile_spawn(g, entity->id, QA_Q1_ROCKET, Q1_ROCKET, origin,
                               qa_vec_scale(direction, 1000), &shot, error);
}
bool q1_gremlin_lightning(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id source = entity->id;
    if (entity->physics.water_type <= -3) {
        double cells = q1_ammo_count(g, entity->id, QA_Q1_CELLS);
        qa_inventory_entry entry = {
            .item = g->ammo[QA_Q1_CELLS], .capacity = 1000000, .policy = QA_COUNT_SOURCE_FLOAT};
        return qa_inventory_configure(g->services.inventory, entity->id, &entry, NULL, NULL,
                                      error) &&
               q1_radius_typed(g, entity->id, entity->id, 35 * (float)cells,
                               g->services.physics->world_actor, QA_Q1_LIGHTNING, "discharge",
                               error);
    }
    entity->effects |= 2;
    if (!q1_monster_face(g, entity, error))
        return false;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    if (!ammo(g, &entity, QA_Q1_CELLS, 2, error))
        return false;
    if (!entity)
        return true;
    qa_body_state body;
    qa_vec3 direction;
    if (!qa_world_body_read(g->services.world, source, &body, error))
        return false;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 16));
    if (!aim(g, &entity, 0.1, &direction, error))
        return false;
    if (!entity)
        return true;
    if (!qa_world_body_read(g->services.world, source, &body, error))
        return false;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    qa_trace_result wall;
    if (!q1_trace(g, start, qa_vec_add(body.origin, qa_vec_scale(direction, 600)), source,
                  false, &wall, error))
        return false;
    if (!q1_entity(g, source))
        return true;
    qa_builtin_event beam = {.kind = QA_BUILTIN_BEAM,
                             .family = QA_GAME_Q1,
                             .provider = g->options.provider,
                             .actor = source,
                             .time_ns = g->time_ns,
                             .origin = start,
                             .end = wall.end,
                             .code = 2};
    if (!qa_builtin_emit(&g->services, &beam, error))
        return false;
    return !q1_entity(g, source) ||
           q1_lightning_rays(g, source, source, start,
                             qa_vec_add(wall.end, qa_vec_scale(direction, 4)), 30, 120, 1,
                             qa_v3(0, 0, 0), Q1_LIGHTNING_REMEMBER_ALL, QA_Q1_LIGHTNING, "electric",
                             error);
}
bool q1_gremlin_weapon_attack(qa_q1_game *g, q1_actor *entity, bool *out, qa_error *error) {
    qa_actor_id source = entity->id;
    *out = q1_gremlin_has_ammo(entity);
    if (!*out)
        return true;
    entity->state.monster.hostile_until = g->time + 1;
    qa_q1_weapon weapon = entity->state.monster.source.gremlin.weapon;
    const char *frame =
        weapon == QA_Q1_SHOTGUN || weapon == QA_Q1_SUPER_SHOTGUN   ? "gremlin_shot1"
        : weapon == QA_Q1_NAILGUN || weapon == QA_Q1_SUPER_NAILGUN ? "gremlin_nail3"
        : weapon == QA_Q1_LIGHTNING                                ? "gremlin_light1"
        : weapon == QA_Q1_LASER                                    ? "gremlin_laser3"
        : weapon == QA_Q1_GRENADE || weapon == QA_Q1_ROCKET || weapon == QA_Q1_PROXIMITY
            ? "gremlin_rocket1"
            : NULL;
    if (!frame)
        return true;
    if (!q1_monster_play(g, entity, frame, error))
        return false;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    switch (weapon) {
    case QA_Q1_SHOTGUN:
    case QA_Q1_SUPER_SHOTGUN:
        if (!shotgun(g, entity, weapon == QA_Q1_SUPER_SHOTGUN, error))
            return false;
        break;
    case QA_Q1_GRENADE:
        if (!q1_monster_action(g, entity, Q1_ACTION_OGRE_NAIL4, error))
            return false;
        entity = q1_entity(g, source);
        if (!entity)
            return true;
        if (!ammo(g, &entity, QA_Q1_ROCKETS, 1, error))
            return false;
        break;
    case QA_Q1_ROCKET:
    case QA_Q1_PROXIMITY:
        if (!missile(g, entity, weapon == QA_Q1_PROXIMITY, error))
            return false;
        break;
    default:
        break;
    }
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3)
        entity->state.monster.attack_finished = g->time + 1;
    entity->state.monster.counter = 0;
    return weapon != QA_Q1_LIGHTNING || q1_sound(g, entity->id, "weapons/lstart.wav", 0, 1, error);
}
bool q1_gremlin_backpack(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id source = entity->id;
    q1_actor *pack;
    if (!q1_create(g, "item_backpack", Q1_PICKUP, (qa_actor_id){0}, &pack, error))
        return false;
    qa_actor_id child = pack->id;
    pack->touch_disabled = true;
    if (!q1_entity(g, source))
        goto cancelled;
    qa_q1_weapon selected = QA_Q1_WEAPON_COUNT;
    const qa_q1_weapon order[] = {QA_Q1_AXE,       QA_Q1_SHOTGUN,       QA_Q1_SUPER_SHOTGUN,
                                  QA_Q1_NAILGUN,   QA_Q1_SUPER_NAILGUN, QA_Q1_GRENADE,
                                  QA_Q1_ROCKET,    QA_Q1_LIGHTNING,     QA_Q1_LASER,
                                  QA_Q1_PROXIMITY, QA_Q1_MJOLNIR};
    for (size_t i = 0; i < sizeof(order) / sizeof(*order); ++i) {
        qa_inventory_entry entry;
        bool present = qa_inventory_entry_read(g->services.inventory, source,
                                                g->weapons[order[i]], &entry, NULL);
        if (!q1_entity(g, source) || !q1_entity(g, child))
            goto cancelled;
        if (present && entry.count > 0) {
            selected = order[i];
            break;
        }
    }
    pack = q1_entity(g, child);
    pack->state.pickup.weapon = selected;
    for (unsigned i = 0; i < 4; ++i) {
        double count = q1_ammo_count(g, source, (qa_q1_ammo)i);
        pack = q1_entity(g, child);
        if (!pack || !q1_entity(g, source))
            goto cancelled;
        if (!isfinite(count) || count > FLT_MAX) {
            qa_error_set(error, QA_ERROR_FORMAT, source.slot,
                         "Gremlin backpack cargo exceeds finite native storage");
            goto failed;
        }
        pack->state.pickup.ammo[i] = count > 0 ? (float)count : 0;
    }
    if (!q1_backpack_definition(g, pack, error))
        goto failed;
    pack->physics.solid = QA_PHYSICS_TRIGGER;
    pack->physics.motion = QA_PHYSICS_TOSS;
    pack->physics.flags = QA_PHYSICS_KILL_VELOCITY;
    pack->source_movement_flags = 256u;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, source, &body, error))
        goto failed;
    if (!q1_entity(g, source) || !q1_entity(g, child))
        goto cancelled;
    qa_vec3 origin = qa_vec_add(body.origin, qa_v3(0, 0, -24));
    float x = (float)(-100.0 + (double)q1_random(g) * 200.0);
    float y = (float)(-100.0 + (double)q1_random(g) * 200.0);
    if (!qa_world_body_read(g->services.world, child, &body, error))
        goto failed;
    if (!q1_entity(g, source) || !q1_entity(g, child))
        goto cancelled;
    body.origin = origin;
    body.velocity = qa_v3(x, y, 300);
    body.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
    if (!qa_world_body_write(g->services.world, child, &body, error))
        goto failed;
    pack = q1_entity(g, child);
    if (!pack || !q1_entity(g, source))
        goto cancelled;
    pack->touch_disabled = false;
    if (!q1_schedule(g, pack, 120, Q1_THINK_REMOVE, error) || !q1_link(g, pack, error))
        goto failed;
    if (!q1_entity(g, source) || !q1_entity(g, child))
        goto cancelled;
    return true;
cancelled:
    if (qa_actors_get(qa_session_actors(g->services.session), child))
        (void)qa_session_release(g->services.session, child, NULL);
    return true;
failed:
    if (qa_actors_get(qa_session_actors(g->services.session), child))
        (void)qa_session_release(g->services.session, child, NULL);
    return false;
}
