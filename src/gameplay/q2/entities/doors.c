#include "qa/q2_sound.h"
#include "internal.h"

bool q2_mover_portals(qa_q2_game *g, q2_actor *a, bool open, qa_error *e) {
    qa_actor_id id;
    qa_target_cursor cursor = {0};
    while (qa_targets_next(g->entity_runtime->services.targets, a->entity->target, &cursor, &id)) {
        qa_authored_target fields;
        if (!qa_targets_read(g->entity_runtime->services.targets, id, &fields))
            continue;
        const char *name =
            qa_strings_cstr(qa_session_strings(g->services.session), fields.classname);
        if (!name || strcmp(name, "func_areaportal"))
            continue;
        qa_q2_entity_services *s = &g->entity_runtime->services;
        if (!s->area_portal) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 door requires area portal service");
            return false;
        }
        if (!s->area_portal(s->context, q2_actor_field_flags(g, id, "style"), open, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    return true;
}
static bool sound(qa_q2_game *g, q2_actor *a, bool start, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    bool water = s->kind == Q2E_WATER, button = s->kind == Q2E_BUTTON;
    int sounds = (int)q2_field_float(g, s, "sounds", 0);
    bool enabled = water ? sounds == 1 || sounds == 2 : sounds != 1;
    const char *edge = water    ? (start ? QA_Q2_SOUND_WORLD_MOV_WATR : QA_Q2_SOUND_WORLD_STP_WATR)
                       : button ? (start ? QA_Q2_SOUND_SWITCHES_BUTN2 : "")
                                : (start ? QA_Q2_SOUND_DOORS_DR1_STRT : QA_Q2_SOUND_DOORS_DR1_END);
    const char *middle = water || button ? "" : QA_Q2_SOUND_DOORS_DR1_MID;
    if (!enabled)
        edge = middle = "";
    if (g->options.edition == QA_Q2_RERELEASE) {
        const char *key = start ? "noise_start" : "noise_end";
        if (q2_field_id(g, s, key))
            edge = q2_field_text(g, s, key);
        if (q2_field_id(g, s, "noise_middle"))
            middle = q2_field_text(g, s, "noise_middle");
        if (!strcmp(edge, "0") || !strcmp(edge, " "))
            edge = "";
        if (!strcmp(middle, "0") || !strcmp(middle, " "))
            middle = "";
    }
    float attenuation = g->options.edition == QA_Q2_RERELEASE && !water
                            ? q2_field_float(g, s, "attenuation", 3)
                            : 3;
    if (attenuation == -1)
        attenuation = 0;
    if ((!qa_actor_reference_present(s->team_master) || qa_actor_id_equal(qa_actor_reference_resolve(actors, s->team_master), a->id)) &&
        !q2_entity_sound(g, a, edge, 2, 1, attenuation, 0, e))
        return false;
    return !q2_actor_live(g, a->id) ||
           q2_entity_sound(g, a, middle, 2, 1, attenuation, start ? 1 : -1, e);
}
static bool health(qa_q2_game *g, q2_actor *a, bool enabled, qa_error *e) {
    if (a->entity->health <= 0 || a->entity->kind == Q2E_WATER)
        return true;
    qa_combat_state c;
    if (!qa_combat_read_traits(g->services.combat, a->id, &c, e))
        return false;
    c.can_take_damage = enabled;
    return qa_combat_set_health(g->services.combat, a->id, a->entity->health, e) &&
           (!q2_actor_live(g, a->id) || qa_combat_set_traits(g->services.combat, a->id, &c, e));
}
bool q2_door_down(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    m->phase = 3;
    if (!health(g, a, true, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (s->kind == Q2E_BUTTON) {
        s->visual.frame = 0;
        if (!q2_entity_show(g, a, e))
            return false;
    } else if (!sound(g, a, true, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!q2_move_start(g, a, m->start, m->angular, Q2MD_DOOR_BOTTOM, e))
        return false;
    return g->options.edition != QA_Q2_RERELEASE || s->kind == Q2E_BUTTON || !(s->spawnflags & 1) ||
           q2_mover_portals(g, a, true, e);
}
static bool up(qa_q2_game *g, q2_actor *a, qa_actor_id activator, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    if (m->phase == 1)
        return true;
    if (m->phase == 2)
        return s->kind == Q2E_BUTTON || s->wait < 0 || (s->spawnflags & 32) ||
               q2_entity_schedule(g, a, Q2ET_DOOR_DOWN, s->wait);
    s->activator = activator;
    m->phase = 1;
    if (!sound(g, a, true, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!q2_move_start(g, a, m->reversed ? qa_vec_scale(m->end, -1) : m->end, m->angular,
                       Q2MD_DOOR_TOP, e))
        return false;
    if (s->kind == Q2E_BUTTON)
        return true;
    if (!q2_entity_targets(g, a, activator, false, e))
        return false;
    return !q2_actor_live(g, a->id) ||
           (g->options.edition == QA_Q2_RERELEASE && (s->spawnflags & 1)) ||
           q2_mover_portals(g, a, true, e);
}
bool q2_door_use(qa_q2_game *g, q2_actor *a, qa_actor_id activator, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    if (qa_actor_reference_present(s->team_master) && !qa_actor_id_equal(qa_actor_reference_resolve(actors, s->team_master), a->id))
        return true;
    if (g->options.edition == QA_Q2_RERELEASE && m->angular && (s->spawnflags & 0x10000) &&
        !m->activated) {
        m->activated = true;
        s->usable = false;
        if (!health(g, a, true, e))
            return false;
        return !q2_actor_live(g, a->id) ||
               q2_entity_schedule(g, a, Q2ET_DOOR_PREPARE, (float)g->frame_ns / Q2_NS);
    }
    if (g->options.edition == QA_Q2_RERELEASE && m->angular && (s->spawnflags & 0x20000) &&
        (m->phase == 0 || m->phase == 3) && q2_actor_live(g, activator)) {
        qa_body_state b, who;
        if (!qa_world_body_read(g->services.world, a->id, &b, e) ||
            !qa_world_body_read(g->services.world, activator, &who, e))
            return false;
        m->reversed =
            qa_vec_dot(qa_vec_normalize(qa_vec_sub(who.origin, b.origin)), m->safe_direction) > 0;
    }
    bool close = s->kind != Q2E_BUTTON && (s->spawnflags & 32) && (m->phase == 1 || m->phase == 2);
    if (!close && g->options.edition == QA_Q2_RERELEASE && s->kind == Q2E_WATER &&
        (s->spawnflags & 2)) {
        qa_body_state b;
        qa_point_contents contents;
        if (!qa_world_body_read(g->services.world, a->id, &b, e) ||
            !qa_world_point_contents(
                g->services.world,
                &(qa_point_query){
                    .point = qa_vec_add(
                        b.origin, qa_vec_scale(qa_vec_add(b.bounds.mins, b.bounds.maxs), .5f)),
                    .policy = {.behavior = &qa_trace_behaviors[QA_RULESET_Q2_CLASSIC], .contents_mask = qa_collision_contents_mask(UINT32_MAX, QA_GAME_Q2)}},
                &contents, e))
            return false;
        if (qa_collision_point_contents_export(contents.contents, QA_GAME_Q2, contents.q1_opaque_token) & 56) {
            s->message = 0;
            s->touchable = false;
            s->enemy = activator;
            return q2_door_smart_water(g, a, e);
        }
    }
    qa_actor_id id = a->id;
    while (q2_actor_live(g, id)) {
        q2_actor *part = q2_ent(g, id);
        if (!part)
            break;
        qa_actor_reference next = part->entity->team_next;
        if (!q2_mover_state(part, e))
            return false;
        part->entity->message = 0;
        part->entity->touchable = false;
        if (!(close ? q2_door_down(g, part, e) : up(g, part, activator, e)))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        id = qa_actor_reference_resolve(actors, next);
    }
    return true;
}
bool q2_door_finished(qa_q2_game *g, q2_actor *a, bool top, qa_error *e) {
    q2_entity_state *s = a->entity;
    s->mover->phase = top ? 2 : 0;
    if (s->kind == Q2E_BUTTON) {
        s->visual.effects = (s->visual.effects & ~UINT64_C(0xc00)) | (top ? 0x800u : 0x400u);
        if (top)
            s->visual.frame = 1;
        if (!q2_entity_show(g, a, e))
            return false;
        if (top && q2_actor_live(g, a->id) && !q2_entity_targets(g, a, s->activator, false, e))
            return false;
    } else {
        if (!sound(g, a, false, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (!top && (g->options.edition == QA_Q2_CLASSIC || !(s->spawnflags & 1)) &&
            !q2_mover_portals(g, a, false, e))
            return false;
        if (top && (s->spawnflags & 32))
            return true;
    }
    if (!q2_actor_live(g, a->id))
        return true;
    if (top && s->wait >= 0)
        q2_entity_schedule(g, a, Q2ET_DOOR_DOWN, s->wait);
    return !top || g->options.edition != QA_Q2_RERELEASE || s->kind == Q2E_BUTTON ||
           !(s->spawnflags & 1) || q2_mover_portals(g, a, false, e);
}
bool q2_door_prepare(qa_q2_game *g, q2_actor *a, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    if (qa_actor_reference_present(s->team_master) && !qa_actor_id_equal(qa_actor_reference_resolve(actors, s->team_master), a->id))
        return true;
    if (g->options.edition == QA_Q2_RERELEASE && !m->angular && (s->spawnflags & 1) &&
        !q2_mover_portals(g, a, true, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    float shortest = fabsf(m->distance);
    qa_bounds bounds = {{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}};
    for (q2_actor *part = a; part;) {
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, part->id, &b, e))
            return false;
        shortest = fminf(shortest, part->entity->mover ? fabsf(part->entity->mover->distance) : 0);
        qa_vec3 lo = qa_vec_add(b.origin, b.bounds.mins), hi = qa_vec_add(b.origin, b.bounds.maxs);
        bounds.mins = qa_v3(fminf(bounds.mins.x, lo.x), fminf(bounds.mins.y, lo.y),
                            fminf(bounds.mins.z, lo.z));
        bounds.maxs = qa_v3(fmaxf(bounds.maxs.x, hi.x), fmaxf(bounds.maxs.y, hi.y),
                            fmaxf(bounds.maxs.z, hi.z));
        part = q2_ent(g, qa_actor_reference_resolve(actors, part->entity->team_next));
    }
    float time = shortest / s->speed;
    if (time > 0)
        for (q2_actor *part = a; part;) {
            q2_entity_state *p = part->entity;
            float speed = (p->mover ? fabsf(p->mover->distance) : 0) / time,
                  ratio = speed / p->speed;
            p->accel *= ratio;
            p->decel *= ratio;
            p->speed = speed;
            part = q2_ent(g, qa_actor_reference_resolve(actors, p->team_next));
        }
    if (s->health > 0 || (s->targetname && !m->activated))
        return true;
    bounds.mins.x -= 60;
    bounds.mins.y -= 60;
    bounds.maxs.x += 60;
    bounds.maxs.y += 60;
    q2_actor *trigger;
    if (!q2_entity_native_spawn(g, "door_trigger", &(qa_body_state){.bounds = bounds},
                                Q2E_DOOR_TRIGGER, &trigger, e))
        return false;
    trigger->entity->owner = a->id;
    trigger->entity->touchable = true;
    if (!q2_entity_solid(g, trigger, QA_PHYSICS_TRIGGER, e))
        return false;
    return !(s->spawnflags & 1) || q2_mover_portals(g, a, true, e);
}
bool q2_door_smart_water(qa_q2_game *g, q2_actor *a, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    float top = b.origin.z + b.bounds.maxs.z + 1;
    if (m->phase == 2)
        return s->wait < 0 || q2_entity_schedule(g, a, Q2ET_SMART_WATER, s->wait);
    if (s->health != 0 && top >= s->health) {
        b.velocity = qa_v3(0, 0, 0);
        m->phase = 2;
        return q2_entity_body(g, a, &b, false, e);
    }
    if (!sound(g, a, true, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_actor_id lowest = {0};
    float height = 999999;
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (qa_actors_next(actors, &cursor, &record)) {
        qa_builtin_actor_traits traits = {0};
        qa_combat_state combat;
        qa_body_state player;
        if (!g->services.actor_traits ||
            !g->services.actor_traits(g->services.context, record->id, &traits) || !traits.player)
            continue;
        if (!qa_combat_read(g->services.combat, record->id, &combat, e) ||
            !qa_world_body_read(g->services.world, record->id, &player, e))
            return false;
        float z = player.origin.z + player.bounds.mins.z - 1;
        if (combat.health > 0 && z < height) {
            height = z;
            lowest = record->id;
        }
    }
    if (!lowest.registry)
        return true;
    float distance = height - top;
    b.velocity = qa_v3(
        0, 0,
        fminf(s->speed, fmaxf(5, distance < m->water_divisor ? 5 : distance / m->water_divisor)));
    if (!q2_entity_body(g, a, &b, false, e))
        return false;
    if (m->phase != 1) {
        if (!q2_entity_targets(g, a, lowest, false, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (!q2_mover_portals(g, a, true, e))
            return false;
        m->phase = 1;
    }
    return q2_entity_schedule(g, a, Q2ET_SMART_WATER, (float)g->frame_ns / Q2_NS);
}
bool q2_door_spawn(qa_q2_game *g, q2_actor *a, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    bool button = s->kind == Q2E_BUTTON, water = s->kind == Q2E_WATER;
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    m->angular = !strcmp(qa_strings_cstr(qa_session_strings(g->services.session), s->classname),
                         "func_door_rotating");
    m->safe_direction =
        m->angular && (s->spawnflags & 0x20000) ? q2_movedir(b.angles) : qa_v3(0, 0, 0);
    m->water_divisor = s->accel != 0 ? s->accel : 20;
    s->direction = m->angular ? (s->spawnflags & 64)    ? qa_v3(0, 0, 1)
                                : (s->spawnflags & 128) ? qa_v3(1, 0, 0)
                                                        : qa_v3(0, 1, 0)
                              : q2_movedir(b.angles);
    if (m->angular && (s->spawnflags & 2))
        s->direction = qa_vec_scale(s->direction, -1);
    b.angles = qa_v3(0, 0, 0);
    a->physics.motion = button ? QA_PHYSICS_STOP : QA_PHYSICS_PUSH;
    if (s->speed == 0)
        s->speed = button ? 40 : water ? 25 : 100;
    if (!button && !m->angular && !water && g->options.deathmatch)
        s->speed *= 2;
    if (s->accel == 0)
        s->accel = s->speed;
    if (s->decel == 0)
        s->decel = s->speed;
    if (s->wait == 0)
        s->wait = water ? -1 : 3;
    if (s->damage == 0)
        s->damage = 2;
    qa_vec3 size = qa_vec_sub(b.bounds.maxs, b.bounds.mins),
            absolute = qa_v3(fabsf(s->direction.x), fabsf(s->direction.y), fabsf(s->direction.z));
    float authored = q2_field_float(g, s, m->angular ? "distance" : "lip", 0);
    m->distance = m->angular ? (authored != 0 ? authored : 90)
                             : qa_vec_dot(absolute, size) - (authored != 0 ? authored
                                                             : button ? 4
                                                             : water  ? 0
                                                                      : 8);
    m->start = m->angular ? qa_v3(0, 0, 0) : b.origin;
    m->end = qa_vec_add(m->start, qa_vec_scale(s->direction, m->distance));
    if (!button && (s->spawnflags & 1)) {
        if (g->options.edition == QA_Q2_RERELEASE && m->angular)
            s->spawnflags &= ~0x20000u;
        qa_vec3 old = m->start;
        m->start = m->end;
        m->end = old;
        if (m->angular) {
            b.angles = m->start;
            s->direction = qa_vec_scale(s->direction, -1);
        } else
            b.origin = m->start;
    }
    s->team_master = qa_actor_reference_from_actor(actors, g->options.owner, a->id);
    if (water) {
        s->accel = s->decel = s->speed;
        if (s->wait == -1)
            s->spawnflags |= 32;
    }
    if (button)
        s->visual.effects |= 0x400;
    else if (!water) {
        if (s->spawnflags & 16)
            s->visual.effects |= 0x1000;
        if (!m->angular && (s->spawnflags & 64))
            s->visual.effects |= 0x2000;
    }
    s->usable = true;
    s->visual.visible = true;
    bool dormant = g->options.edition == QA_Q2_RERELEASE && m->angular && (s->spawnflags & 0x10000);
    if (s->health > 0 && !water) {
        qa_combat_state c = {.health = s->health, .can_take_damage = !dormant};
        if (!qa_combat_create_actor(g->services.combat, a->id, &c, e))
            return false;
    } else if ((button && !s->targetname) || (!button && s->targetname && s->message))
        s->touchable = true;
    if (!q2_entity_body(g, a, &b, false, e) || !q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!button && !water && !dormant)
        q2_entity_schedule(g, a, Q2ET_DOOR_PREPARE, (float)g->frame_ns / Q2_NS);
    return q2_entity_show(g, a, e);
}
bool q2_door_touch(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_error *e) {
    q2_entity_state *s = a->entity;
    qa_builtin_actor_traits traits = {0};
    qa_combat_state combat;
    if (!g->services.actor_traits || !g->services.actor_traits(g->services.context, other, &traits))
        return true;
    if (s->kind == Q2E_DOOR_TRIGGER) {
        q2_actor *door = q2_ent(g, s->owner);
        if (!door || (!traits.player && !traits.monster) ||
            (traits.monster && (door->entity->spawnflags & 8)) || s->timestamp_ns > g->now_ns)
            return true;
        if (!qa_combat_read(g->services.combat, other, &combat, e))
            return false;
        if (combat.health <= 0)
            return true;
        s->timestamp_ns = q2_deadline(g->now_ns, Q2_NS);
        return q2_door_use(g, door, other, e);
    }
    if (!traits.player)
        return true;
    if (s->kind == Q2E_BUTTON) {
        if (!qa_combat_read(g->services.combat, other, &combat, e))
            return false;
        return combat.health <= 0 || q2_door_use(g, a, other, e);
    }
    if (s->timestamp_ns > g->now_ns)
        return true;
    s->timestamp_ns = q2_deadline(g->now_ns, 5 * Q2_NS);
    if (!q2_entity_message(g, a, other,
                           qa_strings_cstr(qa_session_strings(g->services.session), s->message), e))
        return false;
    return !q2_actor_live(g, a->id) || q2_entity_sound(g, a, QA_Q2_SOUND_MISC_TALK1, 0, 1, 1, 0, e);
}
bool q2_door_blocked(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    bool creature;
    if (!q2_target_creature(g, other, &creature, NULL, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    bool smart =
             s->kind == Q2E_WATER && (s->spawnflags & 2) && g->options.edition == QA_Q2_RERELEASE;
    if (q2_target_damageable(g, other) &&
        !q2_entity_damage(g, a, other, a->id, creature ? (smart ? 100 : s->damage) : 100000, 1,
                          smart ? 19 : 20, 0, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!creature)
        return !q2_ent(g, other) || qa_session_release(g->services.session, other, e);
    if (smart || (s->spawnflags & 4) || s->wait < 0)
        return true;
    bool reverse = s->mover->phase == 3;
    q2_actor *master = q2_ent(g, qa_actor_reference_resolve(actors, s->team_master));
    if (!master)
        master = a;
    for (q2_actor *part = master; part;) {
        qa_actor_reference next = part->entity->team_next;
        if (!q2_mover_state(part, e))
            return false;
        if (!(reverse ? up(g, part, part->entity->activator, e) : q2_door_down(g, part, e)))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        part = q2_ent(g, qa_actor_reference_resolve(actors, next));
    }
    return true;
}
bool q2_door_reaction(qa_q2_game *g, q2_actor *a, const qa_damage_outcome *o, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    if (o->result.reaction != QA_REACTION_DEATH)
        return true;
    q2_actor *master = q2_ent(g, qa_actor_reference_resolve(actors, a->entity->team_master));
    if (!master)
        master = a;
    for (q2_actor *part = master; part;) {
        qa_actor_reference next = part->entity->team_next;
        if (!health(g, part, false, e))
            return false;
        if (!q2_actor_live(g, master->id))
            return true;
        part = q2_ent(g, qa_actor_reference_resolve(actors, next));
    }
    return q2_door_use(g, master, o->request.attack.attacker, e);
}
