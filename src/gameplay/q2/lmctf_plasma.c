#include "internal.h"

bool qa_q2_lmctf_plasma_mode(qa_q2_game *g, qa_actor_id id, bool *bounce, qa_error *e) {
    if (g == NULL || bounce == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing LMCTF plasma mode output");
        return false;
    }
    q2_actor *a = q2_actor_get(g, id, false, e);
    if (a == NULL)
        return false;
    *bounce = a->lmctf_plasma_bounce;
    return true;
}
bool q2_lmctf_plasma_mode(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (!q2_actor_live(g, a->id))
        return true;
    qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = a->id,
                              .time_ns = g->now_ns,
                              .code = 2};
    return qa_builtin_resource(&g->services,
                               a->lmctf_plasma_bounce ? "bounce plasma\n" : "spread plasma\n",
                               &event.text, e) &&
           qa_builtin_emit(&g->services, &event, e);
}
bool q2_lmctf_plasma_weapon(q2_weapon_call *c, qa_error *e) {
    c->game->lmctf_plasma_quad = c->input.quad_until_ns > c->now_ns;
    qa_q2_weapon_state *s = c->state;
    bool activating = s->phase == QA_Q2_ACTIVATING && s->frame == 3;
    if (s->phase == QA_Q2_READY && s->frame == 35 && !c->input.attack && !s->latched_attack &&
        s->pending == QA_Q2_WEAPON_NONE && !q2_sound(c, "weapons/plasma/vent.wav", 1, 1, e))
        return false;
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    if (!q2_generic_classic(c, e))
        return false;
    return !activating || s->phase != QA_Q2_READY || q2_lmctf_plasma_mode(c->game, c->actor, e);
}
static bool launch(q2_weapon_call *c, qa_vec3 start, qa_vec3 direction, bool bounce, qa_error *e) {
    qa_q2_game *g = c->game;
    const float yaw_offsets[] = {0, 10, -10};
    qa_vec3 angles =
        qa_v3(-atan2f(direction.z, hypotf(direction.x, direction.y)) * 57.29577951308232f,
              atan2f(direction.y, direction.x) * 57.29577951308232f, 0);
    qa_string_id classname, model, sound;
    if (!qa_builtin_resource(&g->services, "goop", &classname, e) ||
        !qa_builtin_resource(&g->services, "sprites/s_plasma1.sp2", &model, e) ||
        !qa_builtin_resource(&g->services, "weapons/plasma/flyby.wav", &sound, e))
        return false;
    for (unsigned i = 0; i < (bounce ? 1u : 3u); ++i) {
        if (!q2_actor_live(g, c->actor->id))
            return true;
        qa_vec3 forward = direction;
        if (i != 0) {
            qa_vec3 shot = angles;
            shot.y += yaw_offsets[i];
            qa_builtin_angle_vectors(shot, &forward, NULL, NULL);
        }
        qa_vec3 velocity = qa_vec_scale(forward, 1200);
        qa_actor_collision collision = {.family = QA_COLLISION_Q2,
                                        .shape = QA_SHAPE_BOX,
                                        .contents = 2,
                                        .owner = c->actor->id,
                                        .role = QA_COLLISION_SOLID};
        qa_builtin_spawn spawn = {.owner = g->options.owner,
                                  .definition = classname,
                                  .collision = &collision,
                                  .body = {.origin = start,
                                           .velocity = velocity,
                                           .angles = bounce ? velocity : qa_v3(0, 0, 0)}};
        if (bounce)
            spawn.body.bounds = (qa_bounds){.mins = {-12, -12, -12}, .maxs = {12, 12, 12}};
        qa_actor_id id;
        if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
            return false;
        q2_actor *a = q2_actor_get(g, id, true, e);
        if (a == NULL)
            return false;
        a->projectile =
            (q2_projectile){.kind = bounce ? Q2_LMCTF_PLASMA_BOUNCE : Q2_LMCTF_PLASMA_SPREAD,
                            .attack = q2_attack(c, 34, 0),
                            .owner = c->actor->id,
                            .damage = bounce ? 39 : 1,
                            .speed = 1200,
                            .born_ns = c->now_ns,
                            .expire_ns = q2_deadline(c->now_ns, bounce ? 1500 * Q2_MS : 3 * Q2_NS),
                            .classname = classname,
                            .model = model,
                            .loop_sound = sound,
                            .effects = UINT64_C(0x102000),
                            .render_flags = 32,
                            .scale = 1,
                            .visible = true};
        a->physics_bound = true;
        a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
        a->physics.q2_rerelease = g->options.edition == QA_Q2_RERELEASE;
        a->physics.motion = bounce ? QA_PHYSICS_WALL_BOUNCE : QA_PHYSICS_FLY_MISSILE;
        a->physics.solid = QA_PHYSICS_BOX;
        a->physics.clip_mask = Q2_SHOT_MASK;
        if (!qa_world_link(g->services.world, id, NULL, e) ||
            !q2_projectile_event(g, id, QA_BUILTIN_ANIMATION, "sprites/s_plasma1.sp2", 0, start,
                                 spawn.body.angles, e))
            return false;
        if (!q2_actor_live(g, id))
            continue;
        qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                                  .family = QA_GAME_Q2,
                                  .provider = g->options.owner,
                                  .actor = id,
                                  .time_ns = c->now_ns,
                                  .origin = start,
                                  .resource = sound,
                                  .volume = 1,
                                  .attenuation = 1,
                                  .flags = 1};
        if (!qa_builtin_emit(&g->services, &event, e))
            return false;
    }
    return true;
}
bool q2_lmctf_plasma_fire(q2_weapon_call *c, qa_error *e) {
    qa_q2_game *g = c->game;
    int ammo;
    if (!q2_ammo(c, &ammo, e))
        return false;
    if (ammo < 1) {
        ++c->state->frame;
        if (c->now_ns >= c->state->empty_sound_ns) {
            c->state->empty_sound_ns = q2_deadline(c->now_ns, Q2_NS);
            if (!q2_sound(c, "weapons/plasma/empty.wav", 2, 1, e))
                return false;
        }
        return !q2_actor_live(g, c->actor->id) || q2_no_ammo(c, false, e);
    }
    if (c->state->frame == 4) {
        qa_vec3 start, direction, forward, kick_origin, kick_angles;
        if (!q2_project(c, c->input.angles, qa_v3(8, 8, -8), &start, &direction, e))
            return false;
        qa_builtin_angle_vectors(c->input.angles, &forward, NULL, NULL);
        q2_recoil(c, &kick_origin, &kick_angles);
        q2_kick(c, qa_vec_scale(forward, -2), kick_angles, c->rerelease ? 0.2f : 0);
        bool bounce = c->actor->lmctf_plasma_bounce;
        if (!q2_sound(c, bounce ? "weapons/plasma/fire1.wav" : "weapons/plasma/fire2.wav", 1, 1,
                      e) ||
            !launch(c, start, direction, bounce, e))
            return false;
        if (!q2_actor_live(g, c->actor->id))
            return true;
        qa_item_id cells = g->ammo[QA_Q2_LMCTF_PLASMA];
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(g->services.inventory, c->actor->id, cells, &entry, e))
            return false;
        entry.policy = QA_COUNT_SOURCE_INT32;
        if (!qa_inventory_configure(g->services.inventory, c->actor->id, &entry, NULL, NULL, e))
            return false;
        if (!q2_actor_live(g, c->actor->id))
            return true;
        double remaining;
        if (!qa_inventory_adjust(g->services.inventory, c->actor->id, cells, -9, &remaining, e))
            return false;
        if (!q2_actor_live(g, c->actor->id))
            return true;
        if ((g->options.deathmatch_flags & 8192u) == 0 &&
            !qa_inventory_adjust(g->services.inventory, c->actor->id, cells, -1, &remaining, e))
            return false;
        if (!q2_actor_live(g, c->actor->id))
            return true;
        if (g->hooks.ammo_changed != NULL &&
            !g->hooks.ammo_changed(g->hooks.context, c->actor->id, cells, e))
            return false;
        if (!q2_actor_live(g, c->actor->id))
            return true;
        if (c->actor->client != NULL &&
            !q2_player_damage_view(g, c->actor->id, -2, q2_crandom(g) * 2,
                                   q2_deadline(c->now_ns, 500 * Q2_MS), e))
            return false;
        if (!q2_noise(c, start, e))
            return false;
    }
    if (q2_actor_live(g, c->actor->id))
        ++c->state->frame;
    return true;
}
static bool impact_sound(qa_q2_game *g, qa_actor_id id, qa_vec3 origin, const char *path,
                         float attenuation, qa_error *e) {
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = id,
                              .time_ns = g->now_ns,
                              .origin = origin,
                              .channel = 4,
                              .volume = 1,
                              .attenuation = attenuation};
    return qa_builtin_resource(&g->services, path, &event.resource, e) &&
           qa_builtin_emit(&g->services, &event, e);
}
bool q2_lmctf_plasma_touch(qa_q2_game *g, const qa_touch_contact *contact, qa_error *e) {
    q2_actor *a = q2_actor_get(g, contact->self, false, e);
    if (a == NULL)
        return false;
    q2_projectile p = a->projectile;
    if (p.phase != 0)
        return true;
    if (contact->has_surface && (contact->surface.flags & 4u) != 0)
        return qa_session_release(g->services.session, a->id, e);
    bool bounce = p.kind == Q2_LMCTF_PLASMA_BOUNCE;
    if (!bounce && contact->other.slot < g->capacity) {
        const q2_actor *other = g->actors[contact->other.slot];
        if (other != NULL && qa_actor_id_equal(other->id, contact->other) &&
            other->projectile.classname == p.classname)
            return true;
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_vec3 normal = contact->has_plane ? contact->plane.normal : qa_v3(0, 0, 0);
    float damage = (bounce ? 39.0f : 28.0f) * (g->lmctf_plasma_quad ? 4.0f : 1.0f);
    bool hurt = q2_target_damageable(g, contact->other), player = false;
    if (!q2_target_creature(g, p.owner, NULL, &player, e))
        return false;
    if (player && !q2_projectile_noise(g, &p, body.origin, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (hurt && q2_actor_live(g, contact->other)) {
        qa_attack attack = q2_projectile_attack(g, a->id, &p, 34, 4);
        if (!q2_damage(g, &attack, contact->other, damage, 1, body.velocity, body.origin, normal,
                       false, e))
            return false;
    } else if (!hurt) {
        qa_builtin_event event = {.kind = QA_BUILTIN_IMPACT,
                                  .family = QA_GAME_Q2,
                                  .provider = g->options.owner,
                                  .actor = a->id,
                                  .time_ns = g->now_ns,
                                  .origin = body.origin,
                                  .direction = normal,
                                  .count = 32,
                                  .code = 176};
        if (!qa_builtin_resource(&g->services, "q2:laser_sparks", &event.resource, e) ||
            !qa_builtin_emit(&g->services, &event, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (!q2_projectile_radius(g, a->id, &p, body.origin, (qa_actor_id){0}, damage, damage + 70,
                                  34, 0, e))
            return false;
    }
    if (!q2_actor_live(g, a->id))
        return true;
    if (bounce && !hurt)
        return impact_sound(g, a->id, body.origin, "weapons/plasma/bounce.wav", 3, e);
    if (!impact_sound(g, a->id, body.origin, "weapons/plasma/hit.wav", 2, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    a->projectile.phase = 1;
    a->projectile.frame = 0;
    a->projectile.loop_sound = 0;
    a->projectile.expire_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
    a->physics.solid = QA_PHYSICS_NOT_SOLID;
    body.origin = qa_vec_add(body.origin, qa_vec_scale(body.velocity, -0.1f));
    body.velocity = qa_v3(0, 0, 0);
    return qa_world_body_write(g->services.world, a->id, &body, e) &&
           qa_world_set_collision(g->services.world, a->id, NULL, e) &&
           qa_world_link(g->services.world, a->id, NULL, e) &&
           q2_projectile_event(g, a->id, QA_BUILTIN_ANIMATION, "sprites/s_plasma2.sp2", 0,
                               body.origin, body.angles, e);
}
