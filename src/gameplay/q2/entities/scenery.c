#include "internal.h"

static bool model(qa_q2_game *g, q2_actor *a, const char *name, qa_bounds bounds,
                  qa_physics_solid solid, qa_error *e) {
    qa_body_state b;
    if (!qa_builtin_resource(&g->services, name, &a->entity->visual.models[0], e) ||
        !qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    b.bounds = bounds;
    return q2_entity_body(g, a, &b, false, e) && q2_entity_solid(g, a, solid, e) &&
           (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
}
static bool health(qa_q2_game *g, q2_actor *a, float value, float mass, bool damageable,
                   bool invulnerable, qa_error *e) {
    return qa_combat_create_actor(g->services.combat, a->id,
                                  &(qa_combat_state){.health = value,
                                                     .mass = mass,
                                                     .can_take_damage = damageable,
                                                     .invulnerable = invulnerable},
                                  e);
}
static bool damageable(qa_q2_game *g, q2_actor *a, bool enabled, qa_error *e) {
    qa_combat_state state;
    if (!qa_combat_read_traits(g->services.combat, a->id, &state, e))
        return false;
    state.can_take_damage = enabled;
    return qa_combat_set_traits(g->services.combat, a->id, &state, e);
}
static bool effect(qa_q2_game *g, q2_actor *a, const char *name, int count, int color,
                   qa_error *e) {
    qa_body_state body;
    qa_string_id resource;
    return qa_world_body_read(g->services.world, a->id, &body, e) &&
           qa_builtin_resource(&g->services, name, &resource, e) &&
           qa_builtin_emit(&g->services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_IMPACT,
                                               .family = QA_GAME_Q2,
                                               .provider = g->options.owner,
                                               .actor = a->id,
                                               .resource = resource,
                                               .origin = body.origin,
                                               .count = count,
                                               .code = color,
                                               .time_ns = g->now_ns},
                           e);
}
static bool explode(qa_q2_game *g, q2_actor *a, int type, qa_error *e) {
    return effect(g, a, type == 1 ? "q2:explosion1" : "q2:explosion2", 1, 0, e) &&
           (!q2_actor_live(g, a->id) || qa_session_release(g->services.session, a->id, e));
}
static qa_vec3 random_point(qa_q2_game *g, qa_vec3 center, qa_vec3 size) {
    float x = q2_crandom(g) * size.x, y = q2_crandom(g) * size.y, z = q2_crandom(g) * size.z;
    return qa_vec_add(center, qa_v3(x, y, z));
}
static bool break_apart(qa_q2_game *g, q2_actor *a, qa_actor_id inflictor, qa_actor_id attacker,
                        qa_error *e) {
    q2_entity_state *s = a->entity;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_vec3 size = qa_vec_scale(qa_vec_sub(body.bounds.maxs, body.bounds.mins), .5f);
    body.origin = qa_vec_add(qa_vec_add(body.origin, body.bounds.mins), size);
    if (!q2_entity_body(g, a, &body, false, e))
        return false;
    if (q2_target_damageable(g, a->id) && !damageable(g, a, false, e))
        return false;
    if (s->damage != 0 && !q2_entity_radius(g, a, attacker, s->damage, s->damage + 40, 25, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_body_state from = body;
    if (q2_actor_live(g, inflictor) && !qa_world_body_read(g->services.world, inflictor, &from, e))
        return false;
    body.velocity = qa_vec_scale(qa_vec_normalize(qa_vec_sub(body.origin, from.origin)), 150);
    if (!q2_entity_body(g, a, &body, false, e))
        return false;
    float mass = q2_field_float(g, s, "mass", 75);
    if (mass == 0)
        mass = 75;
    int large = (int)q2_clamp(truncf(mass / 100), 0, 8),
        small = (int)q2_clamp(truncf(mass / 25), 0, 16);
    size = qa_vec_scale(size, .5f);
    for (int i = 0; i < large; i++) {
        qa_vec3 point = random_point(g, body.origin, size);
        if (!q2_spawn_model_debris(g, a->id, "models/objects/debris1/tris.md2", 1, point, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    for (int i = 0; i < small; i++) {
        qa_vec3 point = random_point(g, body.origin, size);
        if (!q2_spawn_model_debris(g, a->id, "models/objects/debris2/tris.md2", 2, point, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    if (!q2_entity_targets(g, a, attacker, false, e))
        return false;
    return !q2_actor_live(g, a->id) ||
           (s->damage != 0 ? explode(g, a, 1, e) : qa_session_release(g->services.session, a->id, e));
}
static bool barrel_blast(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    if (!q2_entity_radius(g, a, s->activator.registry ? s->activator : a->id, s->damage,
                          s->damage + 40, 26, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_vec3 size = qa_vec_sub(body.bounds.maxs, body.bounds.mins),
            low = qa_vec_add(body.origin, body.bounds.mins),
            center = qa_vec_add(low, qa_vec_scale(size, .5f));
    for (int i = 0; i < 2; i++) {
        qa_vec3 point = random_point(g, center, size);
        if (!q2_spawn_model_debris(g, a->id, "models/objects/debris1/tris.md2",
                                   1.5f * s->damage / 200, point, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    for (int i = 0; i < 4; i++) {
        qa_vec3 point = qa_vec_add(low, qa_v3((i & 1) ? size.x : 0, (i & 2) ? size.y : 0, 0));
        if (!q2_spawn_model_debris(g, a->id, "models/objects/debris3/tris.md2",
                                   1.75f * s->damage / 200, point, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    for (int i = 0; i < 8; i++) {
        qa_vec3 point = random_point(g, center, size);
        if (!q2_spawn_model_debris(g, a->id, "models/objects/debris2/tris.md2", 2 * s->damage / 200,
                                   point, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    return explode(g, a, body.ground.registry ? 2 : 1, e);
}
static bool animate(qa_q2_game *g, q2_actor *a, int first, int end, float delay, qa_error *e) {
    a->entity->animation_first = first;
    a->entity->animation_end = end;
    a->entity->visual.frame = first;
    return q2_entity_show(g, a, e) &&
           (!q2_actor_live(g, a->id) || q2_entity_schedule(g, a, Q2ET_SCENERY, delay));
}
static void reset_clock(q2_entity_state *s) {
    s->activator = (qa_actor_id){0};
    if (s->spawnflags & 1) {
        s->clock_value = 0;
        s->wait = (float)s->count;
    } else if (s->spawnflags & 2) {
        s->clock_value = s->count;
        s->wait = 0;
    }
}
static bool clock_tick(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    if (!q2_actor_live(g, s->enemy) && !q2_map_find(g, NULL, s->target, 0, &s->enemy))
        return true;
    char text[64];
    int value = s->clock_value;
    if (s->spawnflags & 3) {
        if (s->style == 0)
            snprintf(text, sizeof(text), "%2d", value);
        else if (s->style == 1)
            snprintf(text, sizeof(text), "%2d:%02d", value / 60, value % 60);
        else
            snprintf(text, sizeof(text), "%2d:%02d:%02d", value / 3600, (value % 3600) / 60,
                     value % 60);
        if (s->spawnflags & 1) {
            if (s->clock_value < INT_MAX)
                s->clock_value++;
        } else if (s->clock_value > INT_MIN)
            s->clock_value--;
    } else {
        qa_q2_entity_services *services = &g->entity_runtime->services;
        int hour, minute, second;
        if (!services->local_time) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 wall clock requires local time service");
            return false;
        }
        if (!services->local_time(services->context, &hour, &minute, &second, e))
            return false;
        snprintf(text, sizeof(text), "%2d:%02d:%02d", hour, minute, second);
    }
    text[15] = 0;
    if (!qa_builtin_resource(&g->services, text, &s->message, e))
        return false;
    q2_actor *display = q2_ent(g, s->enemy);
    if (display) {
        display->entity->message = s->message;
        if (!qa_q2_entity_use(g, display->id, a->id, a->id, e))
            return false;
    } else {
        qa_q2_entity_services *services = &g->entity_runtime->services;
        if (!services->set_message || !services->invoke_use) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                         "Q2 clock requires shared target message/use services");
            return false;
        }
        if (!services->set_message(services->context, s->enemy, s->message, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (q2_actor_live(g, s->enemy) &&
            !services->invoke_use(services->context, s->enemy, a->id, a->id, e))
            return false;
    }
    if (!q2_actor_live(g, a->id))
        return true;
    if (((s->spawnflags & 1) && (float)s->clock_value > s->wait) ||
        ((s->spawnflags & 2) && (float)s->clock_value < s->wait)) {
        qa_string_id path = q2_field_id(g, s, "pathtarget");
        if (path) {
            qa_string_id target = s->target, message = s->message;
            s->target = path;
            s->message = 0;
            bool okay = q2_entity_targets(g, a, s->activator, false, e);
            if (q2_actor_live(g, a->id)) {
                s->target = target;
                s->message = message;
            }
            if (!okay)
                return false;
        }
        if (!q2_actor_live(g, a->id) || !(s->spawnflags & 8))
            return true;
        reset_clock(s);
        if (s->spawnflags & 4)
            return true;
    }
    return q2_entity_schedule(g, a, Q2ET_SCENERY, 1);
}
static bool sparks(qa_q2_game *g, q2_actor *a, int count, qa_error *e) {
    int color = 0xe0 + (int)floorf(q2_random(g) * 8);
    return effect(g, a, "q2:welding_sparks", count, color, e);
}
static bool mal_switch(qa_q2_game *g, q2_actor *a, bool on, qa_error *e) {
    q2_entity_state *s = a->entity;
    s->visual.visible = on;
    if (on) {
        if (!s->activator.registry)
            s->activator = a->id;
        s->spawnflags |= 0x80000001u;
    } else
        s->spawnflags &= ~1u;
    if (!q2_entity_show(g, a, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    q2_entity_schedule(g, a, on ? Q2ET_SCENERY : Q2ET_NONE, s->wait + s->delay);
    if (on)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    return qa_builtin_emit(&g->services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_BEAM,
                                               .family = QA_GAME_Q2,
                                               .provider = g->options.owner,
                                               .actor = a->id,
                                               .origin = body.origin,
                                               .end = body.origin,
                                               .value = (float)s->visual.frame,
                                               .code = s->visual.skin,
                                               .time_ns = g->now_ns},
                           e);
}
static bool nuke(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_builtin_snapshot_frame *frame = q2_scratch_acquire(g, e);
    if (!frame)
        return false;
    bool okay = false;
    if (!qa_builtin_observations(&g->services, &frame->snapshot, e))
        goto out;
    for (size_t i = 0; i < frame->snapshot.count; i++) {
        qa_actor_id id = frame->snapshot.ids[i];
        qa_builtin_actor_traits traits = {0};
        if (!q2_actor_live(g, id) || qa_actor_id_equal(id, a->id) || !g->services.actor_traits ||
            !g->services.actor_traits(g->services.context, id, &traits))
            continue;
        if (traits.player) {
            if (!q2_entity_damage(g, a, id, a->id, 100000, 1, 39, 0, e))
                goto out;
        } else if (traits.monster && !qa_session_release(g->services.session, id, e))
            goto out;
        if (!q2_actor_live(g, a->id)) {
            okay = true;
            goto out;
        }
    }
    a->entity->usable = false;
    okay = true;
out:
    qa_builtin_snapshot_release(frame);
    return okay;
}
bool q2_scenery_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    static const struct {
        const char *name;
        q2_scenery_kind kind;
    } names[] = {{"func_wall", Q2S_WALL},
                 {"func_explosive", Q2S_EXPLOSIVE},
                 {"misc_explobox", Q2S_BARREL},
                 {"misc_banner", Q2S_BANNER},
                 {"misc_ctf_banner", Q2S_BANNER},
                 {"misc_ctf_small_banner", Q2S_BANNER},
                 {"misc_satellite_dish", Q2S_SATELLITE},
                 {"misc_deadsoldier", Q2S_SOLDIER},
                 {"misc_gib_head", Q2S_GIB},
                 {"misc_gib_arm", Q2S_GIB},
                 {"misc_gib_leg", Q2S_GIB},
                 {"viewthing", Q2S_ANIMATION},
                 {"misc_blackhole", Q2S_BLACKHOLE},
                 {"misc_eastertank", Q2S_ANIMATION},
                 {"misc_easterchick", Q2S_ANIMATION},
                 {"misc_easterchick2", Q2S_ANIMATION},
                 {"monster_commander_body", Q2S_COMMANDER},
                 {"misc_bigviper", Q2S_NONE},
                 {"light_mine1", Q2S_NONE},
                 {"light_mine2", Q2S_NONE},
                 {"misc_viper_bomb", Q2S_BOMB},
                 {"target_character", Q2S_CHARACTER},
                 {"target_string", Q2S_STRING},
                 {"func_clock", Q2S_CLOCK},
                 {"misc_teleporter", Q2S_TELEPORTER},
                 {"misc_teleporter_dest", Q2S_NONE},
                 {"rotating_light", Q2S_ROTATING_LIGHT},
                 {"func_object_repair", Q2S_REPAIR},
                 {"misc_viper_missile", Q2S_MISSILE},
                 {"misc_amb4", Q2S_AMBIENCE},
                 {"misc_nuke", Q2S_NUKE},
                 {"target_mal_laser", Q2S_MAL_LASER}};
    *handled = false;
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++)
        if (!strcmp(name, names[i].name)) {
            s->scenery = names[i].kind;
            *handled = true;
            break;
        }
    if (!*handled)
        return true;
    s->kind = Q2E_SCENERY;
    s->visual.visible = true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    switch (s->scenery) {
    case Q2S_WALL:
        a->physics.motion = QA_PHYSICS_PUSH;
        if (s->spawnflags & 8)
            s->visual.effects |= 0x1000;
        if (s->spawnflags & 16)
            s->visual.effects |= 0x2000;
        if (s->spawnflags & 7) {
            s->spawnflags |= 1;
            if (s->spawnflags & 4)
                s->spawnflags |= 2;
            s->visual.visible = (s->spawnflags & 4) != 0;
            s->usable = true;
        }
        return q2_entity_solid(g, a, s->visual.visible ? QA_PHYSICS_BRUSH : QA_PHYSICS_NOT_SOLID,
                               e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    case Q2S_EXPLOSIVE:
        if (g->options.deathmatch)
            return qa_session_release(g->services.session, a->id, e);
        a->physics.motion = QA_PHYSICS_PUSH;
        if (s->spawnflags & 2)
            s->visual.effects |= 0x1000;
        if (s->spawnflags & 4)
            s->visual.effects |= 0x2000;
        if (s->spawnflags & 1) {
            s->visual.visible = false;
            s->usable = true;
            s->stage = 1;
        } else
            s->usable = s->targetname != 0;
        if ((s->spawnflags & 1) || !s->targetname) {
            if (s->health == 0)
                s->health = 100;
            float mass = q2_field_float(g, s, "mass", 75);
            if (!health(g, a, s->health, mass != 0 ? mass : 75, true, false, e))
                return false;
        }
        return q2_entity_solid(g, a, s->visual.visible ? QA_PHYSICS_BRUSH : QA_PHYSICS_NOT_SOLID,
                               e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    case Q2S_BARREL: {
        if (g->options.deathmatch)
            return qa_session_release(g->services.session, a->id, e);
        if (s->health == 0)
            s->health = 10;
        if (s->damage == 0)
            s->damage = 150;
        float mass = q2_field_float(g, s, "mass", 400);
        if (!health(g, a, s->health, mass != 0 ? mass : 400, true, false, e))
            return false;
        a->physics.motion = QA_PHYSICS_STEP;
        s->touchable = true;
        q2_entity_schedule(g, a, Q2ET_SCENERY, 2 * (float)g->frame_ns / Q2_NS);
        return model(g, a, "models/objects/barrels/tris.md2",
                     (qa_bounds){{-16, -16, 0}, {16, 16, 40}}, QA_PHYSICS_BOX, e);
    }
    case Q2S_BANNER:
        s->visual.frame = (int)floorf(q2_random(g) * 16);
        s->visual.skin = (s->spawnflags & 1) ? 1 : 0;
        if (!qa_builtin_resource(&g->services,
                                 !strcmp(name, "misc_ctf_banner") ? "models/ctf/banner/tris.md2"
                                 : !strcmp(name, "misc_ctf_small_banner")
                                     ? "models/ctf/banner/small.md2"
                                     : "models/objects/banner/tris.md2",
                                 &s->visual.models[0], e))
            return false;
        q2_entity_schedule(g, a, Q2ET_SCENERY, .1f);
        return q2_entity_solid(g, a, QA_PHYSICS_NOT_SOLID, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    case Q2S_SATELLITE:
        s->usable = true;
        return model(g, a, "models/objects/satellite/tris.md2",
                     (qa_bounds){{-64, -64, 0}, {64, 64, 128}}, QA_PHYSICS_BOX, e);
    case Q2S_SOLDIER:
        if (g->options.deathmatch)
            return qa_session_release(g->services.session, a->id, e);
        s->visual.frame = (s->spawnflags & 2)    ? 1
                          : (s->spawnflags & 4)  ? 2
                          : (s->spawnflags & 8)  ? 3
                          : (s->spawnflags & 16) ? 4
                          : (s->spawnflags & 32) ? 5
                                                 : 0;
        if (!health(g, a, s->health, 200, true, false, e))
            return false;
        return model(g, a, "models/deadbods/dude/tris.md2",
                     (qa_bounds){{-16, -16, 0}, {16, 16, 16}}, QA_PHYSICS_BOX, e);
    case Q2S_GIB: {
        const char *resource = !strcmp(name, "misc_gib_head")  ? "models/objects/gibs/head/tris.md2"
                               : !strcmp(name, "misc_gib_arm") ? "models/objects/gibs/arm/tris.md2"
                                                               : "models/objects/gibs/leg/tris.md2";
        s->visual.effects |= 2;
        float x = q2_random(g) * 200, y = q2_random(g) * 200, z = q2_random(g) * 200;
        a->physics.angular_velocity = qa_v3(x, y, z);
        a->physics.motion = QA_PHYSICS_TOSS;
        if (!health(g, a, 0, 0, true, false, e) ||
            !qa_builtin_resource(&g->services, resource, &s->visual.models[0], e))
            return false;
        q2_entity_schedule(g, a, Q2ET_FREE, 30);
        return q2_entity_solid(g, a, QA_PHYSICS_NOT_SOLID, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    }
    case Q2S_BLACKHOLE:
        s->usable = true;
        s->visual.render_flags = 32;
        if (!model(g, a, "models/objects/black/tris.md2", (qa_bounds){{-64, -64, 0}, {64, 64, 8}},
                   QA_PHYSICS_NOT_SOLID, e))
            return false;
        return !q2_actor_live(g, a->id) || animate(g, a, 0, 19, .2f, e);
    case Q2S_ANIMATION:
        if (!strcmp(name, "viewthing")) {
            s->visual.render_flags = 64;
            if (!model(g, a, "models/objects/banner/tris.md2",
                       (qa_bounds){{-16, -16, -24}, {16, 16, 32}}, QA_PHYSICS_BOX, e))
                return false;
            return !q2_actor_live(g, a->id) || animate(g, a, 0, 7, .5f, e);
        }
        if (!strcmp(name, "misc_eastertank")) {
            if (!model(g, a, "models/monsters/tank/tris.md2",
                       (qa_bounds){{-32, -32, -16}, {32, 32, 32}}, QA_PHYSICS_BOX, e))
                return false;
            return !q2_actor_live(g, a->id) || animate(g, a, 254, 293, .2f, e);
        }
        if (!model(g, a, "models/monsters/bitch/tris.md2", (qa_bounds){{-32, -32, 0}, {32, 32, 32}},
                   QA_PHYSICS_BOX, e))
            return false;
        return !q2_actor_live(g, a->id) ||
               animate(g, a, !strcmp(name, "misc_easterchick") ? 208 : 248,
                       !strcmp(name, "misc_easterchick") ? 247 : 287, .2f, e);
    case Q2S_COMMANDER:
        s->visual.render_flags |= 64;
        s->usable = true;
        if (!health(g, a, 0, 200, true, true, e))
            return false;
        q2_entity_schedule(g, a, Q2ET_SCENERY, 5 * (float)g->frame_ns / Q2_NS);
        return model(g, a, "models/monsters/commandr/tris.md2",
                     (qa_bounds){{-32, -32, 0}, {32, 32, 48}}, QA_PHYSICS_BOX, e);
    case Q2S_BOMB:
    case Q2S_MISSILE:
        s->visual.visible = false;
        if (s->damage == 0)
            s->damage = s->scenery == Q2S_BOMB ? 1000 : 250;
        s->usable = true;
        return model(g, a, "models/objects/bomb/tris.md2", (qa_bounds){{-8, -8, -8}, {8, 8, 8}},
                     QA_PHYSICS_NOT_SOLID, e);
    case Q2S_CHARACTER:
        s->visual.frame = 12;
        a->physics.motion = QA_PHYSICS_PUSH;
        return q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    case Q2S_STRING:
    case Q2S_NUKE:
        s->usable = true;
        return true;
    case Q2S_CLOCK:
        if (!s->target || ((s->spawnflags & 2) && !s->count))
            return qa_session_release(g->services.session, a->id, e);
        if ((s->spawnflags & 1) && !s->count)
            s->count = 3600;
        reset_clock(s);
        s->usable = (s->spawnflags & 4) != 0;
        return s->usable || q2_entity_schedule(g, a, Q2ET_SCENERY, 1);
    case Q2S_TELEPORTER: {
        if (!s->target)
            return qa_session_release(g->services.session, a->id, e);
        s->visual.skin = 1;
        s->visual.effects = 0x20000;
        if (!model(g, a, "models/objects/dmspot/tris.md2",
                   (qa_bounds){{-32, -32, -24}, {32, 32, -16}}, QA_PHYSICS_BOX, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (!q2_entity_sound(g, a, "world/amb10.wav", 0, 1, 3, 1, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        body.bounds = (qa_bounds){{-8, -8, 8}, {8, 8, 24}};
        q2_actor *trigger;
        if (!q2_entity_native_spawn(g, "teleporter_trigger", &body, Q2E_SCENERY, &trigger, e))
            return false;
        trigger->entity->scenery = Q2S_TELEPORT_TRIGGER;
        trigger->entity->target = s->target;
        trigger->entity->owner = a->id;
        trigger->entity->touchable = true;
        return q2_entity_solid(g, trigger, QA_PHYSICS_TRIGGER, e);
    }
    case Q2S_ROTATING_LIGHT:
        if (s->health == 0)
            s->health = 10;
        if (s->speed == 0)
            s->speed = 32;
        s->usable = true;
        s->visual.effects = (s->spawnflags & 1) ? 0 : 0x800000;
        a->physics.motion = QA_PHYSICS_STOP;
        if (!health(g, a, s->health, 0, true, false, e) ||
            !qa_builtin_resource(&g->services, "models/objects/light/tris.md2",
                                 &s->visual.models[0], e))
            return false;
        return q2_entity_solid(g, a, QA_PHYSICS_BOX, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    case Q2S_REPAIR:
        if (!qa_builtin_resource(&g->services, "object_repair", &s->classname, e))
            return false;
        if (s->delay == 0)
            s->delay = 1;
        if (!health(g, a, 100, 0, false, false, e))
            return false;
        body.bounds = (qa_bounds){{-8, -8, 8}, {8, 8, 8}};
        q2_entity_schedule(g, a, Q2ET_SCENERY, 1);
        return q2_entity_body(g, a, &body, true, e) && q2_entity_solid(g, a, QA_PHYSICS_BOX, e);
    case Q2S_AMBIENCE:
        return q2_entity_schedule(g, a, Q2ET_SCENERY, 1);
    case Q2S_MAL_LASER:
        s->visual.render_flags |= 0xa0;
        s->visual.frame = (s->spawnflags & 64) ? 16 : 4;
        s->visual.skin = (int32_t)((s->spawnflags & 2)    ? 0xf2f2f0f0u
                                   : (s->spawnflags & 4)  ? 0xd0d1d2d3u
                                   : (s->spawnflags & 8)  ? 0xf3f3f1f1u
                                   : (s->spawnflags & 16) ? 0xdcdddedfu
                                   : (s->spawnflags & 32) ? 0xe0e1e2e3u
                                                          : 0);
        s->direction = q2_movedir(body.angles);
        if (s->delay == 0)
            s->delay = .1f;
        if (s->wait == 0)
            s->wait = .1f;
        if (s->damage == 0)
            s->damage = 5;
        s->usable = true;
        body.angles = qa_v3(0, 0, 0);
        body.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
        return q2_entity_body(g, a, &body, true, e) &&
               q2_entity_solid(g, a, QA_PHYSICS_NOT_SOLID, e) &&
               (!q2_actor_live(g, a->id) || mal_switch(g, a, (s->spawnflags & 1) != 0, e));
    case Q2S_NONE:
        if (!strcmp(name, "misc_bigviper"))
            return model(g, a, "models/ships/bigviper/tris.md2",
                         (qa_bounds){{-176, -120, -24}, {176, 120, 72}}, QA_PHYSICS_BOX, e);
        if (!strcmp(name, "misc_teleporter_dest"))
            return model(g, a, "models/objects/dmspot/tris.md2",
                         (qa_bounds){{-32, -32, -24}, {32, 32, -16}}, QA_PHYSICS_BOX, e);
        if (!qa_builtin_resource(&g->services,
                                 !strcmp(name, "light_mine1")
                                     ? "models/objects/minelite/light1/tris.md2"
                                     : "models/objects/minelite/light2/tris.md2",
                                 &s->visual.models[0], e))
            return false;
        return qa_world_link(g->services.world, a->id, NULL, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    case Q2S_TELEPORT_TRIGGER:
        return true;
    }
    return true;
}
bool q2_scenery_use(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_actor_id activator,
                    bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    *handled = s->kind == Q2E_SCENERY;
    if (!*handled)
        return true;
    switch (s->scenery) {
    case Q2S_WALL:
    case Q2S_EXPLOSIVE: {
        if (s->scenery == Q2S_EXPLOSIVE && s->stage != 1)
            return break_apart(g, a, a->id, other, e);
        s->visual.visible = a->physics.solid == QA_PHYSICS_NOT_SOLID;
        if (!q2_entity_solid(g, a, s->visual.visible ? QA_PHYSICS_BRUSH : QA_PHYSICS_NOT_SOLID, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (s->visual.visible) {
            bool clear;
            if (!qa_q2_entities_killbox(g, a->id, a->id, &clear, e))
                return false;
        }
        if (!q2_actor_live(g, a->id))
            return true;
        if (s->scenery == Q2S_EXPLOSIVE || !(s->spawnflags & 2))
            s->usable = false;
        return q2_entity_show(g, a, e);
    }
    case Q2S_SATELLITE:
        s->visual.frame = 0;
        return q2_entity_schedule(g, a, Q2ET_SCENERY, .1f);
    case Q2S_BLACKHOLE:
        return qa_session_release(g->services.session, a->id, e);
    case Q2S_COMMANDER:
        if (!q2_entity_sound(g, a, "tank/pain.wav", 4, 1, 1, 0, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        s->stage = 1;
        return q2_entity_schedule(g, a, Q2ET_SCENERY, (float)g->frame_ns / Q2_NS);
    case Q2S_STRING: {
        const char *text = qa_strings_cstr(qa_session_strings(g->services.session), s->message);
        size_t length = text ? strlen(text) : 0;
        for (q2_actor *member = g->first_actor; member;) {
            q2_actor *next = member->live_next;
            q2_entity_state *m = member->entity;
            if (m && m->count &&
                (s->team ? m->team == s->team : qa_actor_id_equal(a->id, member->id))) {
                char c = m->count > 0 && (size_t)m->count <= length ? text[m->count - 1] : 0;
                m->visual.frame = c >= '0' && c <= '9' ? c - '0'
                                  : c == '-'           ? 10
                                  : c == ':'           ? 11
                                                       : 12;
                if (!q2_entity_show(g, member, e))
                    return false;
                if (!q2_actor_live(g, a->id))
                    return true;
            }
            member = next;
        }
        return true;
    }
    case Q2S_CLOCK:
        if (!(s->spawnflags & 8))
            s->usable = false;
        if (s->activator.registry)
            return true;
        s->activator = activator;
        return clock_tick(g, a, e);
    case Q2S_BOMB: {
        qa_actor_id viper;
        if (!q2_map_find(g, "misc_viper", UINT32_MAX, 0, &viper))
            return true;
        q2_actor *ship = q2_ent(g, viper);
        if (!ship || !ship->entity->mover)
            return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        s->direction = ship->entity->mover->motion.direction;
        s->timestamp_ns = g->now_ns;
        s->visual.visible = true;
        s->usable = false;
        s->activator = activator;
        s->visual.effects |= 16;
        body.velocity = qa_vec_scale(s->direction, ship->entity->speed);
        a->physics.motion = QA_PHYSICS_TOSS;
        s->touchable = true;
        s->stage = 1;
        return q2_entity_body(g, a, &body, false, e) && q2_entity_solid(g, a, QA_PHYSICS_BOX, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    }
    case Q2S_MISSILE: {
        qa_actor_id target;
        if (!q2_map_find(g, NULL, s->target, 0, &target))
            return true;
        qa_body_state from, to;
        if (!qa_world_body_read(g->services.world, a->id, &from, e) ||
            !qa_world_body_read(g->services.world, target, &to, e))
            return false;
        qa_vec3 direction = qa_vec_normalize(qa_vec_sub(to.origin, from.origin));
        s->enemy = target;
        if (!q2_fire_actor_rocket(g, a->id, a->id, from.origin, direction, s->damage, 500,
                                  s->damage, s->damage + 20, 8, 9, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (!q2_projectile_event(g, a->id, QA_BUILTIN_MUZZLE, "q2:monster-muzzle", 57, from.origin,
                                 direction, e))
            return false;
        return !q2_actor_live(g, a->id) || q2_entity_schedule(g, a, Q2ET_FREE, .1f);
    }
    case Q2S_NUKE:
        return nuke(g, a, e);
    case Q2S_ROTATING_LIGHT:
        if (s->spawnflags & 1) {
            s->spawnflags &= ~1u;
            s->visual.effects |= 0x800000;
            if (s->spawnflags & 2)
                q2_entity_schedule(g, a, Q2ET_SCENERY, .1f);
        } else {
            s->spawnflags |= 1;
            s->visual.effects &= ~UINT64_C(0x800000);
        }
        return q2_entity_show(g, a, e);
    case Q2S_MAL_LASER:
        s->activator = activator;
        return mal_switch(g, a, (s->spawnflags & 1) == 0, e);
    default:
        return true;
    }
}
bool q2_scenery_think(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    *handled = s->kind == Q2E_SCENERY;
    if (!*handled)
        return true;
    switch (s->scenery) {
    case Q2S_BANNER:
        s->visual.frame = (s->visual.frame + 1) % 16;
        return q2_entity_show(g, a, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_schedule(g, a, Q2ET_SCENERY, .1f));
    case Q2S_BLACKHOLE:
    case Q2S_ANIMATION:
        s->visual.frame++;
        if (s->visual.frame >= s->animation_end)
            s->visual.frame = s->animation_first;
        return q2_entity_show(g, a, e) &&
               (!q2_actor_live(g, a->id) ||
                q2_entity_schedule(g, a, Q2ET_SCENERY, (float)g->frame_ns / Q2_NS));
    case Q2S_SATELLITE:
        s->visual.frame++;
        return q2_entity_show(g, a, e) && (!q2_actor_live(g, a->id) || s->visual.frame >= 38 ||
                                           q2_entity_schedule(g, a, Q2ET_SCENERY, .1f));
    case Q2S_COMMANDER:
        if (!s->stage) {
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, a->id, &body, e))
                return false;
            body.origin.z += 2;
            a->physics.motion = QA_PHYSICS_TOSS;
            return q2_entity_body(g, a, &body, true, e);
        }
        s->visual.frame++;
        if (!q2_entity_show(g, a, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (s->visual.frame == 22 && !q2_entity_sound(g, a, "tank/thud.wav", 4, 1, 1, 0, e))
            return false;
        return !q2_actor_live(g, a->id) || s->visual.frame >= 24 ||
               q2_entity_schedule(g, a, Q2ET_SCENERY, (float)g->frame_ns / Q2_NS);
    case Q2S_CLOCK:
        return clock_tick(g, a, e);
    case Q2S_BARREL: {
        if (s->stage)
            return barrel_blast(g, a, e);
        qa_body_state body;
        qa_trace_result hit;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 1));
        if (!q2_player_trace(g, a->id, start, qa_vec_add(start, qa_v3(0, 0, -256)), &body.bounds,
                             0x2010003, &hit, e))
            return false;
        if (hit.all_solid || hit.fraction >= 1)
            return true;
        body.origin = hit.end;
        body.ground = hit.hit == QA_TRACE_HIT_ACTOR ? hit.actor
                      : hit.hit == QA_TRACE_HIT_WORLD && g->services.physics
                          ? g->services.physics->world_actor
                          : (qa_actor_id){0};
        return q2_entity_body(g, a, &body, true, e);
    }
    case Q2S_ROTATING_LIGHT:
        if (s->spawnflags & 1)
            return true;
        return q2_entity_sound(g, a, "misc/alarm.wav", 10, 1, 3, 0, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_schedule(g, a, Q2ET_SCENERY, 1));
    case Q2S_REPAIR: {
        if (s->stage == 1) {
            if (!q2_entity_targets(g, a, a->id, false, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            s->stage = 2;
            return q2_entity_schedule(g, a, Q2ET_SCENERY, .1f);
        }
        qa_combat_state state;
        if (!qa_combat_read(g->services.combat, a->id, &state, e))
            return false;
        if (!s->stage && state.health < 0) {
            s->stage = 1;
            return q2_entity_schedule(g, a, Q2ET_SCENERY, .1f);
        }
        if (s->stage == 2 && state.health <= 100) {
            if (!qa_combat_set_health(g->services.combat, a->id, state.health + 1, e))
                return false;
        } else if (!sparks(g, a, 10, e))
            return false;
        return !q2_actor_live(g, a->id) || q2_entity_schedule(g, a, Q2ET_SCENERY, s->delay);
    }
    case Q2S_AMBIENCE:
        return q2_entity_sound(g, a, "world/amb4.wav", 2, 1, 0, 0, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_schedule(g, a, Q2ET_SCENERY, 2.7f));
    case Q2S_MAL_LASER:
        if (!q2_laser_think(g, a, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        s->spawnflags |= 0x80000000u;
        return q2_entity_schedule(g, a, Q2ET_SCENERY, s->wait + .1f);
    default:
        return true;
    }
}
bool q2_scenery_prethink(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    if (s->kind != Q2E_SCENERY || s->scenery != Q2S_BOMB || !s->stage)
        return true;
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    float diff = g->now_ns >= s->timestamp_ns
                     ? -fminf(1, (float)(g->now_ns - s->timestamp_ns) / Q2_NS)
                     : fminf(1, (float)(s->timestamp_ns - g->now_ns) / Q2_NS);
    qa_vec3 direction = qa_vec_scale(s->direction, 1 + diff);
    direction.z = diff;
    b.angles = qa_v3(-atan2f(direction.z, hypotf(direction.x, direction.y)) * 57.29577951308232f,
                     atan2f(direction.y, direction.x) * 57.29577951308232f, b.angles.z + 10);
    b.ground = (qa_actor_id){0};
    return q2_entity_body(g, a, &b, false, e);
}
bool q2_scenery_touch(qa_q2_game *g, q2_actor *a, const qa_touch_contact *contact, bool *handled,
                      qa_error *e) {
    q2_entity_state *s = a->entity;
    *handled = s->kind == Q2E_SCENERY;
    if (!*handled)
        return true;
    if (s->scenery == Q2S_TELEPORT_TRIGGER) {
        qa_builtin_actor_traits traits = {0};
        if (!g->services.actor_traits ||
            !g->services.actor_traits(g->services.context, contact->other, &traits) ||
            !traits.player)
            return true;
        qa_actor_id destination;
        qa_body_state body;
        if (!q2_map_find(g, NULL, s->target, 0, &destination))
            return true;
        if (!qa_world_body_read(g->services.world, destination, &body, e))
            return false;
        return q2_entity_teleport(g, a, contact->other, &body, false, true, e);
    }
    if (s->scenery == Q2S_BOMB) {
        if (!q2_entity_targets(g, a, s->activator, false, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        body.origin.z += body.bounds.mins.z + 1;
        if (!q2_entity_body(g, a, &body, false, e) ||
            !q2_entity_radius(g, a, a->id, s->damage, s->damage + 40, 27, e))
            return false;
        return !q2_actor_live(g, a->id) || explode(g, a, 2, e);
    }
    if (s->scenery == Q2S_BARREL) {
        qa_body_state other, body;
        qa_combat_state state, mine;
        if (!qa_world_body_read(g->services.world, contact->other, &other, NULL) ||
            !qa_combat_read(g->services.combat, contact->other, &state, NULL))
            return true;
        if (!other.ground.registry || qa_actor_id_equal(other.ground, a->id))
            return true;
        if (!qa_world_body_read(g->services.world, a->id, &body, e) ||
            !qa_combat_read(g->services.combat, a->id, &mine, e))
            return false;
        qa_vec3 direction = qa_vec_sub(body.origin, other.origin);
        direction.z = 0;
        direction = qa_vec_normalize(direction);
        float distance =
            mine.mass > 0 ? 20 * state.mass / mine.mass * (float)g->frame_ns / Q2_NS : 0;
        qa_trace_result hit;
        if (!q2_player_trace(g, a->id, body.origin,
                             qa_vec_add(body.origin, qa_vec_scale(direction, distance)),
                             &body.bounds, 0x2010003, &hit, e))
            return false;
        if (hit.fraction < 1)
            return true;
        body.origin = hit.end;
        return q2_entity_body(g, a, &body, true, e);
    }
    return true;
}
bool q2_scenery_reaction(qa_q2_game *g, q2_actor *a, const qa_damage_outcome *out, qa_error *e) {
    q2_entity_state *s = a->entity;
    if (s->kind != Q2E_SCENERY || out->result.reaction != QA_REACTION_DEATH)
        return true;
    switch (s->scenery) {
    case Q2S_EXPLOSIVE:
        return break_apart(g, a, out->request.attack.inflictor, out->request.attack.attacker, e);
    case Q2S_BARREL:
        if (!damageable(g, a, false, e))
            return false;
        s->activator = out->request.attack.attacker;
        s->stage = 1;
        return q2_entity_schedule(g, a, Q2ET_SCENERY, 2 * (float)g->frame_ns / Q2_NS);
    case Q2S_GIB:
        return qa_session_release(g->services.session, a->id, e);
    case Q2S_SOLDIER: {
        qa_combat_state state;
        if (!qa_combat_read(g->services.combat, a->id, &state, e))
            return false;
        if (state.health > -80)
            return true;
        if (!q2_entity_sound(g, a, "misc/udeath.wav", 4, 1, 1, 0, e))
            return false;
        for (int i = 0; i < 4; i++) {
            if (!q2_actor_live(g, a->id))
                return true;
            if (!q2_spawn_gib(g, a->id, "models/objects/gibs/sm_meat/tris.md2",
                              out->result.applied_damage, 0, 0, 1, e))
                return false;
        }
        return !q2_actor_live(g, a->id) ||
               q2_spawn_gib(g, a->id, "models/objects/gibs/head2/tris.md2",
                            out->result.applied_damage, Q2_GIB_HEAD, 0, 1, e);
    }
    case Q2S_ROTATING_LIGHT:
        if (!sparks(g, a, 30, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        s->visual.effects &= ~UINT64_C(0x800000);
        s->usable = false;
        return q2_entity_show(g, a, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_schedule(g, a, Q2ET_FREE, .1f));
    default:
        return true;
    }
}
