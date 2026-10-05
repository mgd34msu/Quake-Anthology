#include "internal.h"
#include "qa/game_q2_source.h"

enum { MINE_FLIGHT, MINE_OPENING, MINE_ACTIVE, MINE_WARNING, MINE_CONSUMING, MINE_FINISHED };

static uint64_t mine_life(float multiplier) {
    return (uint64_t)(multiplier == 2   ? 30
                      : multiplier == 4 ? 15
                      : multiplier == 8 ? 10
                                        : 45) *
           Q2_NS;
}
static bool contents_at(qa_q2_game *g, qa_vec3 point, uint32_t *out, qa_error *e) {
    qa_point_query query = {.point = point, .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, e))
        return false;
    *out = (uint32_t)contents.contents;
    return true;
}
static bool visible(qa_q2_game *g, qa_actor_id from, qa_vec3 start, qa_actor_id target, qa_error *e,
                    bool *out) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, target, &body, e))
        return false;
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits != NULL)
        g->services.actor_traits(g->services.context, target, &traits);
    qa_trace_query query = {.start = start,
                            .end = qa_vec_add(body.origin, qa_v3(0, 0, traits.view_height)),
                            .pass_actor = from,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = 25;
    qa_trace_result trace;
    if (!qa_world_trace(g->services.world, &query, &trace, e))
        return false;
    *out = trace.fraction == 1;
    return true;
}
static bool player_start(qa_q2_game *g, qa_actor_id id) {
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits == NULL ||
        !g->services.actor_traits(g->services.context, id, &traits))
        return false;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), traits.classname);
    if (name == NULL)
        return false;
    if (g->options.edition == QA_Q2_RERELEASE)
        return strncmp(name, "info_player_", 12) == 0 ||
               strcmp(name, "misc_teleporter_dest") == 0 || strncmp(name, "item_flag_", 10) == 0;
    return strcmp(name, "info_player_deathmatch") == 0 || strcmp(name, "info_player_start") == 0 ||
           strcmp(name, "info_player_coop") == 0 || strcmp(name, "misc_teleporter_dest") == 0;
}
static bool mine_sound(qa_q2_game *g, q2_actor *a, const char *path, int channel, float attenuation,
                       qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = a->id,
                              .time_ns = g->now_ns,
                              .origin = body.origin,
                              .channel = channel,
                              .volume = 1,
                              .attenuation = attenuation};
    return qa_builtin_resource(&g->services, path, &event.resource, e) &&
           qa_builtin_emit(&g->services, &event, e);
}
static bool sound(qa_q2_game *g, q2_actor *a, const char *path, int channel, qa_error *e) {
    return mine_sound(g, a, path, channel, 1, e);
}
static bool frame_event(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_builtin_event event = {.kind = QA_BUILTIN_ANIMATION,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = a->id,
                              .time_ns = g->now_ns,
                              .frame = a->projectile.frame,
                              .flags = (uint32_t)a->projectile.effects};
    return qa_builtin_emit(&g->services, &event, e);
}
static bool disarm(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    qa_combat_state state;
    qa_error ignored;
    if (!qa_combat_read_traits(g->services.combat, id, &state, &ignored))
        return true;
    state.can_take_damage = false;
    return qa_combat_set_traits(g->services.combat, id, &state, e);
}
static bool remove_children(qa_q2_game *g, qa_actor_id first, qa_error *e) {
    qa_actor_id child = first;
    while (q2_actor_live(g, child)) {
        q2_actor *a = q2_actor_get(g, child, false, e);
        if (a == NULL)
            return false;
        qa_actor_id next = qa_actor_reference_resolve(qa_session_actors(g->services.session), a->projectile.child);
        if (!qa_session_release(g->services.session, child, e))
            return false;
        child = next;
    }
    return true;
}
static bool explode(qa_q2_game *g, q2_actor *a, bool blow, qa_error *e) {
    qa_actor_id id = a->id;
    q2_projectile p = a->projectile;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e) || !remove_children(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), p.child), e) ||
        !disarm(g, id, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (p.kind == Q2_TESLA && blow) {
        p.damage *= 50;
        p.radius = 200;
    }
    bool radius = p.kind == Q2_PROX || p.kind == Q2_TESLA;
    if (radius) {
        if (!q2_projectile_noise(g, &p, body.origin, e))
            return false;
        if (p.damage > (p.kind == Q2_PROX ? 90 : 150) && !sound(g, a, "items/damage3.wav", 3, e))
            return false;
        if (!q2_projectile_radius(g, id, &p, body.origin, p.kind == Q2_PROX ? id : (qa_actor_id){0},
                                  p.damage, p.kind == Q2_PROX ? 192 : p.radius,
                                  p.kind == Q2_PROX ? 46 : 7, 0, e))
            return false;
    }
    if (!q2_actor_live(g, id))
        return true;
    uint32_t contents;
    if (!contents_at(g, body.origin, &contents, e))
        return false;
    const char *effect =
        !qa_actor_reference_present(body.ground)
            ? ((contents & 56u) != 0 ? "q2:rocket-explosion-water" : "q2:rocket-explosion")
            : ((contents & 56u) != 0 ? "q2:grenade-explosion-water" : "q2:grenade-explosion");
    return q2_projectile_event(g, id, QA_BUILTIN_EXPLOSION, effect, 0,
                               qa_vec_add(body.origin, qa_vec_scale(body.velocity, -0.02f)),
                               qa_v3(0, 0, 0), e) &&
           qa_session_release(g->services.session, id, e);
}
static bool field(qa_q2_game *g, q2_actor *mine, q2_projectile_kind kind, qa_bounds bounds,
                  qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, mine->id, &body, e))
        return false;
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, kind == Q2_PROX_FIELD ? "prox_field" : "tesla trigger",
                             &definition, e))
        return false;
    qa_actor_collision collision = {.family = QA_COLLISION_Q2,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = qa_collision_q2_source_contents(1, 0,
                                        g->options.edition == QA_Q2_RERELEASE),
                                    .role = QA_COLLISION_TRIGGER,
                                    .owner = qa_actor_reference_source(g->options.owner, mine->wire_slot)};
    qa_builtin_spawn spawn = {.owner = g->options.owner,
                              .definition = definition,
                              .collision = &collision,
                              .link = true,
                              .body = {.origin = body.origin, .bounds = bounds}};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    q2_actor *child = q2_actor_get(g, id, true, e);
    if (child == NULL)
        return false;
    child->projectile = (q2_projectile){.kind = kind,
        .owner = qa_actor_reference_source(g->options.owner, mine->wire_slot)};
    child->physics_bound = true;
    child->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    child->physics.motion = QA_PHYSICS_STATIONARY;
    child->physics.solid = QA_PHYSICS_TRIGGER;
    mine->projectile.child = qa_actor_reference_source(g->options.owner, child->wire_slot);
    return true;
}
static bool clear_collision_owner(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_actor_collision collision;
    qa_error observed = {0};
    if (!qa_world_get_collision(g->services.world, a->id, &collision, &observed)) {
        if (observed.code && e)
            *e = observed;
        return observed.code == QA_OK;
    }
    collision.owner = (qa_actor_reference){0};
    return qa_world_set_collision(g->services.world, a->id, &collision, e);
}
static bool prox_open(qa_q2_game *g, q2_actor *a, qa_builtin_actor_snapshot *snapshot,
                      qa_error *e) {
    q2_projectile *p = &a->projectile;
    if (p->frame != 9) {
        if (p->frame == 0 && !sound(g, a, "weapons/proxopen.wav", 2, e))
            return false;
        ++p->frame;
        p->next_ns = q2_deadline(g->now_ns,
                                 g->options.edition == QA_Q2_RERELEASE ? 100 * Q2_MS : 50 * Q2_MS);
        return frame_event(g, a, e);
    }
    if ((g->options.edition == QA_Q2_CLASSIC || g->options.deathmatch) &&
        !clear_collision_owner(g, a, e))
        return false;
    p->armed = true;
    q2_actor *trigger = q2_actor_get(g, qa_actor_reference_resolve(
        qa_session_actors(g->services.session), p->child), false, NULL);
    if (trigger) trigger->projectile.armed = true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    if (!qa_builtin_nearby(&g->services, body.origin, 202, snapshot, e))
        return false;
    for (size_t i = 0; i < snapshot->count; ++i) {
        qa_actor_id target = snapshot->ids[i];
        if (!q2_actor_live(g, target))
            continue;
        if (qa_actor_id_equal(target, a->id))
            continue;
        qa_body_state other;
        qa_error ignored;
        if (!qa_world_body_read(g->services.world, target, &other, &ignored))
            continue;
        bool player, is_creature;
        if (!q2_target_creature(g, target, &is_creature, &player, e))
            return false;
        qa_combat_state health;
        bool living = is_creature &&
                      qa_combat_read(g->services.combat, target, &health, &ignored) &&
                      health.health > 0;
        if (g->options.edition == QA_Q2_RERELEASE) {
            if (player && !g->options.deathmatch)
                living = false;
            if (g->hooks.can_target != NULL &&
                !g->hooks.can_target(g->hooks.context, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner), target))
                continue;
        }
        if (!living && !(g->options.deathmatch && player_start(g, target)))
            continue;
        bool seen;
        if (!visible(g, a->id, body.origin, target, e, &seen))
            return false;
        if (seen)
            return sound(g, a, "weapons/proxwarn.wav", 2, e) && explode(g, a, false, e);
    }
    p->phase = MINE_ACTIVE;
    qa_actor_id id = a->id;
    float strong;
    if (!qa_q2_source_value(g, g->options.edition == QA_Q2_RERELEASE ?
            "g_dm_strong_mines" : "strong_mines", 0, &strong, e)) return false;
    if (!q2_actor_live(g, id)) return true;
    p->expire_ns = q2_deadline(g->now_ns, strong != 0 ? 45 * Q2_NS : mine_life(p->damage / 90));
    p->next_ns = q2_deadline(g->now_ns, 200 * Q2_MS);
    return true;
}
static bool tesla_activate(qa_q2_game *g, q2_actor *a, qa_builtin_actor_snapshot *snapshot,
                           qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    uint32_t contents;
    if (!contents_at(g, body.origin, &contents, e))
        return false;
    if ((contents & 56u) != 0)
        return explode(g, a, true, e);
    if (g->options.deathmatch) {
        if (!qa_builtin_nearby(&g->services, body.origin, 192, snapshot, e))
            return false;
        for (size_t i = 0; i < snapshot->count; ++i) {
            qa_actor_id target = snapshot->ids[i];
            if (!q2_actor_live(g, target))
                continue;
            if (!player_start(g, target))
                continue;
            qa_body_state other;
            if (!qa_world_body_read(g->services.world, target, &other, e))
                return false;
            bool seen;
            if (!visible(g, target, other.origin, a->id, e, &seen))
                return false;
            if (seen)
                return explode(g, a, false, e);
        }
    }
    if (!field(g, a, Q2_TESLA_FIELD,
               (qa_bounds){qa_v3(-128, -128, body.bounds.mins.z), qa_v3(128, 128, 128)}, e))
        return false;
    body.angles = qa_v3(0, 0, 0);
    if (!qa_world_body_write(g->services.world, a->id, &body, e) ||
        (g->options.deathmatch && !clear_collision_owner(g, a, e)))
        return false;
    a->projectile.phase = MINE_ACTIVE;
    a->projectile.expire_ns = q2_deadline(g->now_ns, 30 * Q2_NS);
    a->projectile.next_ns =
        q2_deadline(g->now_ns, g->options.edition == QA_Q2_RERELEASE ? 100 * Q2_MS : g->frame_ns);
    return true;
}
static bool tesla_active(qa_q2_game *g, q2_actor *a, qa_builtin_actor_snapshot *snapshot,
                         qa_error *e) {
    qa_actor_id id = a->id;
    q2_projectile p = a->projectile;
    if (g->now_ns > p.expire_ns)
        return explode(g, a, false, e);
    qa_body_state mine, trigger;
    if (!qa_world_body_read(g->services.world, id, &mine, e) ||
        !qa_world_body_read(g->services.world, qa_actor_reference_resolve(qa_session_actors(g->services.session), p.child), &trigger, e))
        return false;
    qa_bounds bounds = qa_bounds_translate(trigger.bounds, trigger.origin);
    qa_vec3 start = qa_vec_add(mine.origin, qa_v3(0, 0, 16));
    if (!qa_builtin_observations(&g->services, snapshot, e))
        return false;
    for (size_t i = 0; i < snapshot->count; ++i) {
        qa_actor_id target = snapshot->ids[i];
        if (!q2_actor_live(g, id))
            return true;
        if (qa_actor_id_equal(target, id) || !q2_target_damageable(g, target))
            continue;
        qa_combat_state health;
        if (!qa_combat_read(g->services.combat, target, &health, e))
            return false;
        if (health.health < 1)
            continue;
        bool player, is_creature;
        if (!q2_target_creature(g, target, &is_creature, &player, e))
            return false;
        if (player && !g->options.deathmatch)
            continue;
        if (g->options.edition == QA_Q2_RERELEASE && player && g->hooks.can_target != NULL &&
            !g->hooks.can_target(g->hooks.context, qa_actor_reference_resolve(qa_session_actors(g->services.session), p.owner), target))
            continue;
        qa_builtin_actor_traits traits = {0};
        if (g->services.actor_traits != NULL)
            g->services.actor_traits(g->services.context, target, &traits);
        if (g->options.edition == QA_Q2_RERELEASE && !g->options.deathmatch &&
            traits.no_source_friendly_fire)
            continue;
        if (!is_creature && !traits.damageable_target)
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, target, &body, e))
            return false;
        if (!qa_bounds_overlap(bounds, qa_bounds_translate(body.bounds, body.origin)))
            continue;
        qa_trace_query query = {.start = start,
                                .end = body.origin,
                                .pass_actor = id,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        query.policy.contents_mask =
            g->options.edition == QA_Q2_RERELEASE ? Q2_PROJECTILE_MASK : Q2_SHOT_MASK;
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, e))
            return false;
        if (trace.fraction < 1 &&
            !(trace.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(trace.actor, target)))
            continue;
        if (p.damage > 3 && !sound(g, a, "items/damage3.wav", 3, e))
            return false;
        qa_physics_properties physics;
        bool grounded_monster = !player && is_creature && g->services.physics != NULL &&
                                g->services.physics->services.read(
                                    g->services.physics->services.context, target, &physics) &&
                                (physics.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING)) == 0;
        qa_attack attack = q2_projectile_attack(g, id, &p, 45, 0);
        if (!q2_damage(g, &attack, target, p.damage, grounded_monster ? 0 : 8,
                       qa_vec_sub(body.origin, start), trace.end, trace.contact_plane.normal, false,
                       e))
            return false;
        qa_builtin_event beam = {.kind = QA_BUILTIN_BEAM,
            .family = QA_GAME_Q2, .provider = g->options.owner, .actor = id,
            .time_ns = g->now_ns, .origin = start, .end = trace.end,
            .value = (float)((double)g->frame_ns / 1e9)};
        if (!qa_builtin_resource(&g->services, "q2:bfg-lightning", &beam.resource, e) ||
            !qa_builtin_emit(&g->services, &beam, e))
            return false;
    }
    a->projectile.next_ns =
        q2_deadline(g->now_ns, g->options.edition == QA_Q2_RERELEASE ? 100 * Q2_MS : g->frame_ns);
    return true;
}

static bool trap_gibs(qa_q2_game *g, q2_actor *a, const qa_body_state *body, qa_error *e) {
    qa_vec3 forward, right, up;
    qa_builtin_angle_vectors(body->angles, &forward, &right, &up);
    for (int i = 0; i < 3; ++i) {
        float radians = ((float)(120 * i) + a->projectile.delay) * 0.017453292519943295f;
        qa_vec3 rotated =
            qa_vec_add(qa_vec_add(qa_vec_scale(right, cosf(radians)),
                                  qa_vec_scale(qa_vec_cross(up, right), sinf(radians))),
                       qa_vec_scale(up, qa_vec_dot(up, right) * (1 - cosf(radians))));
        qa_vec3 origin = qa_vec_add(
            qa_vec_add(body->origin, qa_vec_scale(rotated, 1 + (float)a->projectile.wait / 2)),
            forward);
        origin.z = body->origin.z + (float)a->projectile.wait;
        qa_actor_definition definition;
        if (!qa_builtin_resource(&g->services, "trap_gib", &definition, e))
            return false;
        qa_combat_state combat = {.can_take_damage = true};
        qa_builtin_spawn spawn = {.owner = g->options.owner,
                                  .definition = definition,
                                  .body = {.origin = origin, .angles = body->angles},
                                  .combat = &combat};
        qa_actor_id id;
        if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
            return false;
        q2_actor *gib = q2_actor_get(g, id, true, e);
        if (gib == NULL)
            return false;
        gib->projectile =
            (q2_projectile){.kind = Q2_TRAP_GIB, .expire_ns = q2_deadline(g->now_ns, 100 * Q2_MS)};
        gib->physics_bound = true;
        gib->physics = qa_physics_properties_default(QA_COLLISION_Q2);
        gib->physics.motion = QA_PHYSICS_TOSS;
        gib->physics.solid = QA_PHYSICS_NOT_SOLID;
        gib->projectile.effects = (a->projectile.gekk ? 26u : 1u) | 2u;
        gib->projectile.scale = 1;
        const char *model =
            a->projectile.gekk ? "models/objects/gekkgib/torso/tris.md2"
            : truncf(a->projectile.captured_mass / (g->options.deathmatch ? 4 : 10)) > 200
                ? "models/objects/gibs/chest/tris.md2"
                : "models/objects/gibs/sm_meat/tris.md2";
        if (!q2_projectile_event(g, id, QA_BUILTIN_ANIMATION, model, 0, origin, body->angles, e))
            return false;
    }
    return true;
}
static bool trap_think(qa_q2_game *g, q2_actor *a, qa_builtin_actor_snapshot *snapshot,
                       qa_error *e) {
    q2_projectile *p = &a->projectile;
    qa_actor_id id = a->id;
    bool rerelease = g->options.edition == QA_Q2_RERELEASE;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    if (p->expire_ns < g->now_ns)
        return explode(g, a, false, e);
    p->next_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
    if (!qa_actor_reference_present(body.ground))
        return true;
    if (p->frame > 4) {
        if (p->frame == 5) {
            if (p->wait == 64 && !mine_sound(g, a, "weapons/trapdown.wav", 2, 2, e))
                return false;
            p->wait -= 2;
            p->delay += rerelease ? 2 : (float)((double)g->now_ns / 1e9);
            if (!rerelease && !trap_gibs(g, a, &body, e))
                return false;
            if (p->wait < 19)
                ++p->frame;
        } else if (++p->frame == 8) {
            p->effects &= ~UINT64_C(0x2000000);
            float scale = rerelease ? 1 + (p->captured_mass - 100) / 300 : 1;
            qa_vec3 origin = qa_vec_add(body.origin, qa_v3(0, 0, rerelease ? 24 * scale : 16));
            int health = (int)truncf(p->captured_mass / (g->options.deathmatch ? 4 : 10));
            if (!q2_item_food_cube(g, id, origin, scale, health, qa_v3(0, 0, 400), e) ||
                (g->hooks.food_cube != NULL &&
                 !g->hooks.food_cube(g->hooks.context, id, origin, scale, health, qa_v3(0, 0, 400),
                                     e)))
                return false;
            p->phase = MINE_FINISHED;
            p->expire_ns = q2_deadline(g->now_ns, Q2_NS);
            p->next_ns = p->expire_ns;
        }
        return frame_event(g, a, e);
    }
    if (p->frame >= 4) {
        p->effects |= UINT64_C(0x2000000);
        p->armed = true;
        if (!rerelease) {
            body.bounds = (qa_bounds){0};
            if (!qa_world_body_write(g->services.world, id, &body, e) ||
                !qa_world_link(g->services.world, id, NULL, e))
                return false;
        } else if (g->options.deathmatch && !clear_collision_owner(g, a, e))
            return false;
    } else {
        p->effects &= ~UINT64_C(0x2000000);
        ++p->frame;
        if (rerelease)
            return frame_event(g, a, e);
    }
    qa_actor_id best = {0};
    float nearest = 8000;
    if (!qa_builtin_nearby(&g->services, body.origin, 256, snapshot, e))
        return false;
    for (size_t i = 0; i < snapshot->count; ++i) {
        qa_actor_id target = snapshot->ids[i];
        if (!q2_actor_live(g, target))
            continue;
        if (qa_actor_id_equal(target, id))
            continue;
        qa_body_state other;
        qa_error ignored;
        if (!qa_world_body_read(g->services.world, target, &other, &ignored))
            continue;
        float distance = qa_vec_length(qa_vec_sub(body.origin, other.origin));
        bool seen;
        if (rerelease && g->options.deathmatch && player_start(g, target)) {
            if (!visible(g, target, other.origin, id, e, &seen))
                return false;
            if (seen)
                return explode(g, a, false, e);
        }
        bool player, is_creature;
        if (!q2_target_creature(g, target, &is_creature, &player, e))
            return false;
        qa_combat_state combat;
        if (!is_creature || (rerelease && player && !g->options.deathmatch) ||
            !qa_combat_read(g->services.combat, target, &combat, &ignored) || combat.health <= 0)
            continue;
        if (rerelease && !qa_actor_id_equal(target, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner)) && g->hooks.can_target != NULL &&
            !g->hooks.can_target(g->hooks.context, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner), target))
            continue;
        if (!visible(g, id, body.origin, target, e, &seen))
            return false;
        if (!seen)
            continue;
        if (!rerelease && best.registry == 0) {
            best = target;
            continue;
        }
        if (!rerelease)
            distance = truncf(distance);
        if (best.registry == 0 || distance < nearest) {
            best = target;
            nearest = distance;
        }
    }
    if (best.registry != 0 && q2_actor_live(g, best)) {
        qa_body_state target;
        if (!qa_world_body_read(g->services.world, best, &target, e))
            return false;
        if (qa_actor_reference_present(target.ground))
            target.origin.z += 1;
        target.ground = (qa_actor_reference){0};
        qa_vec3 delta = qa_vec_sub(body.origin, target.origin);
        float distance = qa_vec_length(delta);
        bool player;
        if (!q2_target_creature(g, best, NULL, &player, e))
            return false;
        if (rerelease) {
            float max = player ? 290 : 150;
            target.velocity =
                qa_vec_add(target.velocity, qa_vec_scale(qa_vec_normalize(delta),
                                                         fmaxf(64, fminf(max, max - distance))));
        } else if (player)
            target.velocity =
                qa_vec_add(target.velocity, qa_vec_scale(qa_vec_normalize(delta), 250));
        else {
            qa_physics_properties physics;
            if (g->services.physics != NULL &&
                g->services.physics->services.read(g->services.physics->services.context, best,
                                                   &physics)) {
                physics.ideal_yaw = atan2f(delta.y, delta.x) * 57.29577951308232f;
                if (!g->services.physics->services.write(g->services.physics->services.context,
                                                         best, &physics, e) ||
                    !qa_physics_change_yaw(g->services.physics, best,
                                           (float)((double)g->frame_ns / 1e9), e))
                    return false;
                qa_body_state turned;
                if (!qa_world_body_read(g->services.world, best, &turned, e))
                    return false;
                target.angles = turned.angles;
            }
            qa_vec3 forward;
            qa_builtin_angle_vectors(target.angles, &forward, NULL, NULL);
            target.velocity = qa_vec_scale(forward, 256);
        }
        if (!qa_world_body_write(g->services.world, best, &target, e) ||
            !qa_world_link(g->services.world, best, NULL, e))
            return false;
        if (rerelease) {
            if (!q2_projectile_loop(g, a, "weapons/trapsuck.wav", false, e))
                return false;
        } else {
            qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                                      .family = QA_GAME_Q2,
                                      .provider = g->options.owner,
                                      .actor = id,
                                      .time_ns = g->now_ns,
                                      .origin = body.origin,
                                      .channel = 2,
                                      .volume = 1,
                                      .attenuation = 2};
            if (!qa_builtin_resource(&g->services, "weapons/trapsuck.wav", &event.resource, e) ||
                !qa_builtin_emit(&g->services, &event, e))
                return false;
        }
        if (!q2_actor_live(g, id) || !q2_actor_live(g, best))
            return true;
        if ((rerelease ? distance : truncf(distance)) < (rerelease ? 48 : 32)) {
            qa_combat_state combat;
            if (!qa_combat_read(g->services.combat, best, &combat, e))
                return false;
            if (combat.mass >= 400)
                return explode(g, a, false, e);
            float mass = combat.mass;
            qa_builtin_actor_traits traits = {0};
            if (g->services.actor_traits != NULL)
                g->services.actor_traits(g->services.context, best, &traits);
            const char *classname =
                qa_strings_cstr(qa_session_strings(g->services.session), traits.classname);
            p->gekk = classname != NULL && strcmp(classname, "monster_gekk") == 0;
            if (rerelease &&
                (!disarm(g, id, e) || !qa_world_set_collision(g->services.world, id, NULL, e)))
                return false;
            qa_actor_reference target_reference = qa_actor_reference_from_actor(qa_session_actors(g->services.session), g->options.owner, best);
            qa_attack attack = q2_projectile_attack(g, id, p, 39, 0);
            if (!q2_damage(g, &attack, best, q2_trap_capture_damage(), 1, qa_v3(0, 0, 0), target.origin,
                           qa_v3(0, 0, 0), false, e))
                return false;
            if (!q2_actor_live(g, id))
                return true;
            p->enemy = target_reference;
            p->wait = 64;
            p->expire_ns = q2_deadline(g->now_ns, 30 * Q2_NS);
            p->captured_mass = mass;
            p->frame = 5;
            p->phase = MINE_CONSUMING;
            if (rerelease && !q2_trap_capture_gibs(g, a, e))
                return false;
        }
    }
    return frame_event(g, a, e);
}
static bool mine_scan(qa_q2_game *g, q2_actor *a,
                      bool (*run)(qa_q2_game *, q2_actor *, qa_builtin_actor_snapshot *,
                                  qa_error *),
                      qa_error *e) {
    qa_builtin_snapshot_frame *scratch = q2_scratch_acquire(g, e);
    if (scratch == NULL)
        return false;
    bool ok = run(g, a, &scratch->snapshot, e);
    qa_builtin_snapshot_release(scratch);
    return ok;
}
bool q2_mine_think(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_projectile *p = &a->projectile;
    if (p->kind == Q2_PROX_FIELD || p->kind == Q2_TESLA_FIELD || p->kind == Q2_BAD_AREA) {
        if ((p->kind != Q2_BAD_AREA && !q2_actor_live(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner))) ||
            (p->expire_ns != 0 && g->now_ns >= p->expire_ns))
            return qa_session_release(g->services.session, a->id, e);
        return true;
    }
    if (p->phase == MINE_FINISHED)
        return g->now_ns >= p->expire_ns ? qa_session_release(g->services.session, a->id, e) : true;
    if (p->next_ns > g->now_ns)
        return true;
    if (p->kind == Q2_TRAP)
        return mine_scan(g, a, trap_think, e);
    if (p->kind == Q2_PROX) {
        if (p->phase == MINE_WARNING || (p->phase == MINE_FLIGHT && g->now_ns >= p->expire_ns) ||
            (p->phase == MINE_ACTIVE && g->now_ns > p->expire_ns))
            return explode(g, a, false, e);
        if (p->phase == MINE_OPENING)
            return mine_scan(g, a, prox_open, e);
        if (p->phase == MINE_ACTIVE) {
            if (++p->frame > 13)
                p->frame = 9;
            p->next_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
            return frame_event(g, a, e);
        }
        p->next_ns = q2_deadline(g->now_ns, g->frame_ns);
        return true;
    }
    if (p->kind == Q2_TESLA) {
        if (p->phase == MINE_ACTIVE)
            return mine_scan(g, a, tesla_active, e);
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        uint32_t content;
        if (!contents_at(g, body.origin, &content, e))
            return false;
        if ((content & 24u) != 0)
            return explode(g, a, false, e);
        body.angles = qa_v3(0, 0, 0);
        if (!qa_world_body_write(g->services.world, a->id, &body, e))
            return false;
        if (p->phase == MINE_OPENING)
            return mine_scan(g, a, tesla_activate, e);
        if (p->frame == 0 && !sound(g, a, "weapons/teslaopen.wav", 2, e))
            return false;
        ++p->frame;
        p->next_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
        if (p->frame == 15) {
            p->frame = 14;
            p->phase = MINE_OPENING;
        }
        if (p->frame == 10) {
            if (q2_actor_live(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner)) &&
                !q2_noise_for_actor(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner), body.origin, false, e))
                return false;
            p->skin = 1;
        } else if (p->frame == 12)
            p->skin = 2;
        else if (p->frame == 14)
            p->skin = 3;
        return frame_event(g, a, e);
    }
    return true;
}

bool q2_mine_touch(qa_q2_game *g, const qa_touch_contact *contact, qa_error *e) {
    q2_actor *a = q2_actor_get(g, contact->self, false, e);
    if (a == NULL)
        return false;
    q2_projectile *p = &a->projectile;
    if (p->kind == Q2_BAD_AREA || p->kind == Q2_TESLA_FIELD || p->kind == Q2_TRAP)
        return true;
    if (p->kind == Q2_PROX_FIELD) {
        if (!p->armed) return true;
        bool creature, player;
        if (!q2_target_creature(g, contact->other, &creature, &player, e))
            return false;
        if (!creature)
            return true;
        if (!q2_actor_live(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner)))
            return qa_session_release(g->services.session, a->id, e);
        q2_actor *mine = q2_actor_get(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner), false, e);
        if (mine == NULL)
            return false;
        if (mine->projectile.kind == Q2_PROX && (mine->projectile.phase == MINE_WARNING ||
            (g->options.edition == QA_Q2_CLASSIC && mine->projectile.phase == MINE_FLIGHT)))
            return true;
        if (!qa_actor_id_equal(qa_actor_reference_resolve(qa_session_actors(g->services.session), mine->projectile.child), a->id))
            return qa_session_release(g->services.session, a->id, e);
        if (g->options.edition == QA_Q2_RERELEASE &&
            ((player && !g->options.deathmatch) ||
             (g->hooks.can_target != NULL &&
              !g->hooks.can_target(g->hooks.context, qa_actor_reference_resolve(qa_session_actors(g->services.session), mine->projectile.owner), contact->other))))
            return true;
        mine->projectile.phase = MINE_WARNING;
        mine->projectile.next_ns = q2_deadline(g->now_ns, 500 * Q2_MS);
        return sound(g, a, "weapons/proxwarn.wav", 2, e);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    uint32_t content;
    if (p->kind == Q2_TESLA) {
        if (contact->has_plane) {
            if (!contents_at(g, qa_vec_add(body.origin, qa_vec_scale(contact->plane.normal, -20)),
                             &content, e))
                return false;
            if ((content & 24u) != 0)
                return explode(g, a, true, e);
        }
        return sound(g, a, q2_random(g) > 0.5f ? "weapons/hgrenb1a.wav" : "weapons/hgrenb2a.wav", 2,
                     e);
    }
    if (p->kind != Q2_PROX || p->phase != MINE_FLIGHT)
        return true;
    if (contact->has_surface && (contact->surface.flags & 4) != 0)
        return qa_session_release(g->services.session, a->id, e);
    if (contact->has_plane) {
        if (!contents_at(g, qa_vec_add(body.origin, qa_vec_scale(contact->plane.normal, -10)),
                         &content, e))
            return false;
        if ((content & 24u) != 0)
            return explode(g, a, false, e);
    }
    bool creature;
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits != NULL)
        g->services.actor_traits(g->services.context, contact->other, &traits);
    if (!q2_target_creature(g, contact->other, &creature, NULL, e))
        return false;
    if (creature || traits.damageable_target)
        return qa_actor_id_equal(contact->other, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner)) || explode(g, a, false, e);
    qa_physics_motion motion = QA_PHYSICS_STATIONARY;
    if (contact->other.registry != 0 &&
        (g->services.physics == NULL ||
         !qa_actor_id_equal(contact->other, g->services.physics->world_actor))) {
        if (!contact->has_plane)
            return explode(g, a, false, e);
        qa_vec3 out = qa_vec_sub(
            body.velocity, qa_vec_scale(contact->plane.normal,
                                        qa_vec_dot(body.velocity, contact->plane.normal) * 1.5f));
        if (out.z > 60)
            return true;
        qa_physics_properties other;
        bool pusher = g->services.physics != NULL &&
                      g->services.physics->services.read(g->services.physics->services.context,
                                                         contact->other, &other) &&
                      other.motion == QA_PHYSICS_PUSH;
        if (!pusher || contact->plane.normal.z <= 0.7f)
            return contact->plane.normal.z > 0.7f ? explode(g, a, false, e) : true;
        motion = QA_PHYSICS_BOUNCE;
    }
    if (!contact->has_plane)
        return explode(g, a, false, e);
    if (!contents_at(g, body.origin, &content, e))
        return false;
    if ((content & 24u) != 0)
        return explode(g, a, false, e);
    if (!field(g, a, Q2_PROX_FIELD, (qa_bounds){qa_v3(-96, -96, -96), qa_v3(96, 96, 96)}, e))
        return false;
    qa_vec3 normal = contact->plane.normal;
    body.angles = qa_v3(-atan2f(normal.z, hypotf(normal.x, normal.y)) * 57.29577951308232f + 90,
                        atan2f(normal.y, normal.x) * 57.29577951308232f, 0);
    body.velocity = qa_v3(0, 0, 0);
    a->physics.angular_velocity = qa_v3(0, 0, 0);
    a->physics.motion = motion;
    qa_combat_state combat = {.health = 20, .can_take_damage = true};
    if (!qa_combat_create_actor(g->services.combat, a->id, &combat, e) ||
        !qa_world_body_write(g->services.world, a->id, &body, e))
        return false;
    p->phase = MINE_OPENING;
    p->next_ns = q2_deadline(g->now_ns, g->options.edition == QA_Q2_RERELEASE ? 0 : 50 * Q2_MS);
    if (g->options.edition == QA_Q2_RERELEASE) {
        p->dodgeable = false;
        qa_actor_collision collision;
        if (!qa_world_get_collision(g->services.world, a->id, &collision, e))
            return false;
        collision.contents = qa_collision_q2_source_contents(2, 0, true);
        if (!qa_world_set_collision(g->services.world, a->id, &collision, e))
            return false;
    }
    return true;
}
qa_vec3 q2_mine_velocity(qa_vec3 direction, float speed, float lift, float side) {
    qa_vec3 angles = qa_v3(-atan2f(direction.z, hypotf(direction.x, direction.y)) * 57.29577951308232f,
                           atan2f(direction.y, direction.x) * 57.29577951308232f, 0), right, up;
    qa_builtin_angle_vectors(angles, NULL, &right, &up);
    return qa_vec_add(qa_vec_add(qa_vec_scale(direction, speed), qa_vec_scale(up, lift)),
                      qa_vec_scale(right, side));
}

float q2_trap_capture_damage(void) { return 100000; }

float q2_mine_lift(q2_projectile_kind kind, bool rerelease, float gravity, float noise) {
    float lift = q2_grenade_lift(rerelease, gravity, noise);
    if (kind == Q2_TRAP && rerelease)
        lift *= q2_grenade_gravity_scale(rerelease, gravity);
    return lift;
}

bool q2_mine_spawn(q2_weapon_call *c, q2_projectile_kind kind, qa_vec3 start, qa_vec3 direction,
                   float damage, float speed, float range, float splash, float fuse, bool held,
                   qa_error *e) {
    qa_q2_game *g = c->game;
    const char *classname = kind == Q2_PROX    ? (c->rerelease ? "prox_mine" : "prox")
                            : kind == Q2_TESLA ? (c->rerelease ? "tesla_mine" : "tesla")
                                               : (c->rerelease ? "food_cube_trap" : "htrap");
    const char *model = kind == Q2_PROX    ? "models/weapons/g_prox/tris.md2"
                        : kind == Q2_TESLA ? "models/weapons/g_tesla/tris.md2"
                                           : "models/weapons/z_trap/tris.md2";
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, classname, &definition, e))
        return false;
    float horizontal = hypotf(direction.x, direction.y);
    qa_vec3 angles = qa_v3(-atan2f(direction.z, horizontal) * 57.29577951308232f,
                           atan2f(direction.y, direction.x) * 57.29577951308232f, 0);
    float lift = q2_mine_lift(kind, c->rerelease, c->input.gravity, q2_crandom(g));
    float side = q2_crandom(g) * 10;
    qa_body_state body = {
        .origin = start,
        .angles = kind == Q2_PROX ? qa_vec_add(angles, qa_v3(-90, 0, 0)) : qa_v3(0, 0, 0),
        .velocity = q2_mine_velocity(direction, speed, lift, side),
        .bounds = kind == Q2_PROX    ? (qa_bounds){qa_v3(-6, -6, -6), qa_v3(6, 6, 6)}
                  : kind == Q2_TESLA ? (qa_bounds){qa_v3(-12, -12, 0), qa_v3(12, 12, 20)}
                                     : (qa_bounds){qa_v3(-4, -4, 0), qa_v3(4, 4, 8)}};
    qa_actor_reference owner_reference = qa_actor_reference_from_actor(qa_session_actors(g->services.session), g->options.owner, c->actor->id);
    qa_actor_collision collision = {.family = QA_COLLISION_Q2,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = qa_collision_q2_source_contents(2,
                                        c->rerelease && kind == Q2_PROX ? 128u : 0u, c->rerelease),
                                    .owner = owner_reference,
                                    .role = QA_COLLISION_SOLID};
    qa_combat_state combat = {.can_take_damage =
                                  kind == Q2_TESLA || (kind == Q2_TRAP && c->rerelease),
                              .health = kind == Q2_TESLA ? (g->options.deathmatch ? 20
                                                            : c->rerelease        ? 50
                                                                                  : 30)
                                        : c->rerelease   ? 20
                                                         : 0};
    qa_builtin_spawn spawn = {.owner = g->options.owner,
                              .definition = definition,
                              .body = body,
                              .collision = &collision,
                              .combat = kind == Q2_PROX ? NULL : &combat};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    int mod = kind == Q2_PROX ? 46 : kind == Q2_TESLA ? 45 : 39;
    a->projectile = (q2_projectile){
        .kind = kind,
        .attack = q2_attack(c, mod, 0),
        .owner = owner_reference,
        .damage = damage,
        .radius = range,
        .radius_damage = splash,
        .speed = speed,
        .gravity = c->input.gravity,
        .born_ns = c->now_ns,
        .direct_mod = mod,
        .splash_mod = mod,
        .held = held,
        .phase = MINE_FLIGHT,
        .effects = kind == Q2_TRAP ? 0 : 32,
        .render_flags = kind == Q2_TRAP ? 0 : 0x8000,
        .visible = true,
        .scale = c->rerelease ? 0 : 1,
        .dodgeable = true,
        .expire_ns = q2_deadline(c->now_ns, kind == Q2_PROX ? mine_life(damage / 90) : 30 * Q2_NS),
        .next_ns =
            q2_deadline(c->now_ns, kind == Q2_PROX    ? (c->rerelease ? 0 : mine_life(damage / 90))
                                   : kind == Q2_TESLA ? 3 * Q2_NS
                                                      : Q2_NS)};
    a->physics_bound = true;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics.q2_rerelease = c->rerelease;
    a->physics.motion = QA_PHYSICS_BOUNCE;
    a->physics.solid = QA_PHYSICS_BOX;
    a->physics.clip_mask =
        (c->rerelease ? Q2_PROJECTILE_MASK : Q2_SHOT_MASK) | (kind == Q2_TRAP ? 0 : 24u);
    if (c->rerelease && !c->input.players_collide)
        a->physics.clip_mask &= ~Q2_PLAYER_CONTENTS;
    if (kind == Q2_TRAP) {
        a->physics.angular_velocity = qa_v3(0, 300, 0);
        if (c->rerelease)
            a->physics.clip_mask &= ~UINT32_C(0x04000000);
    } else if (kind == Q2_TESLA && c->rerelease)
        a->physics.clip_mask &= ~UINT32_C(0x04000000);
    if (kind == Q2_TRAP && !c->rerelease && fuse <= 0) {
        q2_projectile p = a->projectile;
        if (!q2_projectile_noise(g, &p, start, e) ||
            !q2_projectile_radius(g, id, &p, start, (qa_actor_id){0}, damage, range, held ? 24 : 16,
                                  0, e))
            return false;
        return explode(g, a, false, e);
    }
    if (!q2_launch_behavior(g, a, QA_BUILTIN_GRENADE, NULL, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!qa_world_body_read(g->services.world, id, &body, e) ||
        !qa_world_link(g->services.world, id, NULL, e) ||
        !q2_projectile_event(g, id, QA_BUILTIN_ANIMATION, model, 0, body.origin, body.angles, e))
        return false;
    return kind != Q2_TRAP || q2_projectile_loop(g, a, "weapons/traploop.wav", false, e);
}
typedef struct projectile_reaction_call {
    qa_q2_game *game;
    qa_damage_outcome outcome;
} projectile_reaction_call;

static bool projectile_reaction(void *context, qa_actor_id id, qa_error *e) {
    projectile_reaction_call *call = context;
    qa_q2_game *g = call->game;
    const qa_damage_outcome *outcome = &call->outcome;
    if (g == NULL || outcome == NULL || outcome->result.reaction != QA_REACTION_DEATH)
        return true;
    if (!q2_actor_live(g, id) || id.slot >= g->capacity || g->actors[id.slot] == NULL ||
        !qa_actor_id_equal(g->actors[id.slot]->id, id))
        return true;
    q2_actor *a = g->actors[id.slot];
    if (a->projectile.kind == Q2_PROBOSCIS)
        return q2_proboscis_reaction(g, a, outcome, e);
    if (a->projectile.kind == Q2_CTF_HOOK || a->projectile.kind == Q2_LMCTF_HOOK)
        return q2_grapple_reaction(g, a, outcome, e);
    if (a->projectile.kind == Q2_GIB || a->projectile.kind == Q2_DEBRIS ||
        a->projectile.kind == Q2_TRAP_ORBIT_GIB)
        return q2_gib_reaction(g, a, outcome, e);
    if (a->projectile.kind == Q2_NUKE)
        return q2_nuke_reaction(g, a, outcome, e);
    if (a->projectile.kind == Q2_PROX) {
        if (!disarm(g, id, e))
            return false;
        qa_actor_id inflictor = outcome->request.attack.inflictor;
        bool prox = inflictor.slot < g->capacity && g->actors[inflictor.slot] != NULL &&
                    qa_actor_id_equal(g->actors[inflictor.slot]->id, inflictor) &&
                    g->actors[inflictor.slot]->projectile.kind == Q2_PROX;
        if (prox) {
            a->projectile.phase = MINE_WARNING;
            a->projectile.next_ns = q2_deadline(g->now_ns, g->frame_ns);
            return true;
        }
        return explode(g, a, false, e);
    }
    if (a->projectile.kind == Q2_TESLA || a->projectile.kind == Q2_TRAP)
        return explode(g, a, false, e);
    if (a->projectile.kind == Q2_TRAP_GIB)
        return qa_session_release(g->services.session, id, e);
    return true;
}
bool qa_q2_projectile_reaction(qa_q2_game *g, const qa_damage_outcome *outcome, qa_error *e) {
    if (!g || !outcome || outcome->result.reaction != QA_REACTION_DEATH ||
        !q2_actor_live(g, outcome->request.target))
        return true;
    projectile_reaction_call call = {.game = g, .outcome = *outcome};
    return qa_q2_run_actor(g, call.outcome.request.target, projectile_reaction, &call, e);
}
bool qa_q2_bad_area(qa_q2_game *g, qa_actor_id actor, qa_vec3 origin, qa_actor_id *hazard,
                    qa_error *e) {
    qa_body_state body;
    if (hazard == NULL || !qa_world_body_read(g->services.world, actor, &body, e))
        return false;
    qa_bounds bounds = qa_bounds_translate(body.bounds, origin);
    *hazard = (qa_actor_id){0};
    for (q2_actor *a = g->first_actor; a; a = a->live_next) {
        if (!q2_actor_live(g, actor))
            return true;
        if (!q2_actor_live(g, a->id) || a->projectile.kind != Q2_BAD_AREA ||
            a->physics.solid != QA_PHYSICS_TRIGGER)
            continue;
        qa_body_state area;
        if (!qa_world_body_read(g->services.world, a->id, &area, e))
            return false;
        if (!q2_actor_live(g, actor))
            return true;
        if (!q2_actor_live(g, a->id))
            continue;
        if (qa_bounds_overlap(bounds, qa_bounds_translate(area.bounds, area.origin))) {
            *hazard = a->id;
            break;
        }
    }
    return true;
}
bool qa_q2_spawn_bad_area(qa_q2_game *g, qa_bounds absolute, uint64_t lifespan, qa_actor_id owner,
                          qa_actor_id *out, qa_error *e) {
    if (g == NULL || out == NULL || !qa_vec_finite(absolute.mins) ||
        !qa_vec_finite(absolute.maxs) || absolute.mins.x > absolute.maxs.x ||
        absolute.mins.y > absolute.maxs.y || absolute.mins.z > absolute.maxs.z) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 bad-area bounds");
        return false;
    }
    qa_vec3 origin = qa_vec_scale(qa_vec_add(absolute.mins, absolute.maxs), 0.5f);
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, "bad_area", &definition, e))
        return false;
    qa_actor_reference owner_reference = qa_actor_reference_from_actor(qa_session_actors(g->services.session), g->options.owner, owner);
    qa_actor_collision collision = {.family = QA_COLLISION_Q2,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = qa_collision_q2_source_contents(1, 0,
                                        g->options.edition == QA_Q2_RERELEASE),
                                    .role = QA_COLLISION_TRIGGER,
                                    .owner = owner_reference};
    qa_builtin_spawn spawn = {
        .owner = g->options.owner,
        .definition = definition,
        .collision = &collision,
        .link = true,
        .body = {.origin = origin,
                 .bounds = {qa_vec_sub(absolute.mins, origin), qa_vec_sub(absolute.maxs, origin)}}};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    a->projectile =
        (q2_projectile){.kind = Q2_BAD_AREA,
                        .owner = owner_reference,
                        .expire_ns = lifespan == 0 ? 0 : q2_deadline(g->now_ns, lifespan)};
    a->physics_bound = true;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics.motion = QA_PHYSICS_STATIONARY;
    a->physics.solid = QA_PHYSICS_TRIGGER;
    *out = id;
    return true;
}
bool qa_q2_mark_tesla_area(qa_q2_game *g, qa_actor_id observer, qa_actor_id tesla, bool *created,
                           qa_error *e) {
    if (g == NULL || created == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Tesla area request");
        return false;
    }
    *created = false;
    if (!q2_actor_live(g, observer) || !q2_actor_live(g, tesla))
        return true;
    q2_actor *mine = q2_actor_get(g, tesla, false, e);
    if (mine == NULL || mine->projectile.kind != Q2_TESLA)
        return true;
    q2_actor *tail = mine, *trigger = NULL;
    while (q2_actor_live(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), tail->projectile.child))) {
        q2_actor *next = q2_actor_get(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), tail->projectile.child), false, e);
        if (next == NULL)
            return false;
        if (next->projectile.kind == Q2_BAD_AREA)
            return true;
        if (trigger == NULL)
            trigger = next;
        tail = next;
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, trigger == NULL ? tesla : trigger->id, &body, e))
        return false;
    qa_bounds bounds =
        trigger == NULL ? (qa_bounds){qa_v3(-128, -128, body.bounds.mins.z), qa_v3(128, 128, 128)}
                        : qa_bounds_translate(body.bounds, body.origin);
    uint64_t lifespan = trigger == NULL                         ? 30 * Q2_NS
                        : mine->projectile.phase == MINE_ACTIVE ? mine->projectile.expire_ns
                                                                : mine->projectile.next_ns;
    qa_actor_id area;
    if (!qa_q2_spawn_bad_area(g, bounds, lifespan, tesla, &area, e))
        return false;
    q2_actor *child = q2_actor_get(g, area, false, e);
    if (!child) return false;
    tail->projectile.child = qa_actor_reference_source(g->options.owner, child->wire_slot);
    *created = true;
    return true;
}
