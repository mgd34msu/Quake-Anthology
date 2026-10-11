#include "qa/q2_sound.h"
#include "internal.h"
#include "qa/game_q2_monsters.h"
#include "qa/text.h"

static qa_q2_fog fog_fields(qa_q2_game *g, q2_entity_state *s, bool off) {
    qa_q2_fog f = {0};
#define SCALAR(field, name) f.field = q2_field_float(g, s, \
    g->field_keys[off ? QA_TARGET_KEY_##name##_OFF : QA_TARGET_KEY_##name], 0)
#define VECTOR(field, name) f.field = q2_field_vec(g, s, \
    g->field_keys[off ? QA_TARGET_KEY_##name##_OFF : QA_TARGET_KEY_##name], qa_v3(0, 0, 0))
    SCALAR(density, FOG_DENSITY);
    SCALAR(sky_factor, FOG_SKY_FACTOR);
    VECTOR(color, FOG_COLOR);
    VECTOR(start_color, HEIGHTFOG_START_COLOR);
    VECTOR(end_color, HEIGHTFOG_END_COLOR);
    SCALAR(start_distance, HEIGHTFOG_START_DIST);
    SCALAR(end_distance, HEIGHTFOG_END_DIST);
    SCALAR(height_density, HEIGHTFOG_DENSITY);
    SCALAR(falloff, HEIGHTFOG_FALLOFF);
#undef SCALAR
#undef VECTOR
    return f;
}
static qa_q2_fog fog_lerp(qa_q2_fog a, qa_q2_fog b, float t) {
#define SCALAR(field) a.field += (b.field - a.field) * t
#define VECTOR(field) a.field = qa_vec_add(a.field, qa_vec_scale(qa_vec_sub(b.field, a.field), t))
    SCALAR(density);
    SCALAR(sky_factor);
    VECTOR(color);
    VECTOR(start_color);
    VECTOR(end_color);
    SCALAR(start_distance);
    SCALAR(end_distance);
    SCALAR(height_density);
    SCALAR(falloff);
#undef SCALAR
#undef VECTOR
    return a;
}
static bool vector_equal(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool fog_equal(qa_q2_fog a, qa_q2_fog b) {
    return a.density == b.density && a.sky_factor == b.sky_factor &&
           vector_equal(a.color, b.color) && vector_equal(a.start_color, b.start_color) &&
           vector_equal(a.end_color, b.end_color) && a.start_distance == b.start_distance &&
           a.end_distance == b.end_distance && a.height_density == b.height_density &&
           a.falloff == b.falloff;
}
static qa_bounds absolute_bounds(qa_body_state b) {
    return (qa_bounds){qa_vec_add(b.origin, b.bounds.mins), qa_vec_add(b.origin, b.bounds.maxs)};
}
static bool intersects(qa_bounds a, qa_bounds b) {
    return a.mins.x <= b.maxs.x && a.maxs.x >= b.mins.x && a.mins.y <= b.maxs.y &&
           a.maxs.y >= b.mins.y && a.mins.z <= b.maxs.z && a.maxs.z >= b.mins.z;
}
static bool use_actor(qa_q2_game *g, qa_actor_id target, qa_actor_id source, qa_error *e) {
    if (!q2_actor_live(g, target))
        return true;
    if (q2_ent(g, target))
        return qa_q2_entity_use(g, target, source, source, e);
    qa_q2_entity_services *services = &g->entity_runtime->services;
    if (!services->invoke_use) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 map requires shared direct use service");
        return false;
    }
    return services->invoke_use(services->context, target, source, source, e);
}
static bool flashlight(qa_q2_game *g, qa_actor_id id, bool enabled, qa_error *e) {
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a || !a->client) {
        qa_q2_entity_services *services = &g->entity_runtime->services;
        if (!services->flashlight) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 flashlight requires recipient capability");
            return false;
        }
        return services->flashlight(services->context, id, enabled, e);
    }
    if (a->client->info.flashlight == enabled)
        return true;
    a->client->info.flashlight = enabled;
    qa_body_state body;
    qa_string_id sound;
    if (!qa_world_body_read(g->services.world, id, &body, e) ||
        !qa_builtin_resource(&g->services,
                             enabled ? QA_Q2_SOUND_ITEMS_FLASHLIGHT_ON : QA_Q2_SOUND_ITEMS_FLASHLIGHT_OFF,
                             &sound, e) ||
        !qa_builtin_emit(&g->services,
                         &(qa_builtin_event){.kind = QA_BUILTIN_SOUND,
                                             .family = QA_GAME_Q2,
                                             .provider = g->options.owner,
                                             .actor = id,
                                             .origin = body.origin,
                                             .resource = sound,
                                             .volume = 1,
                                             .attenuation = 3,
                                             .time_ns = g->now_ns},
                         e))
        return false;
    return !q2_actor_live(g, id) ||
           q2_player_emit(g,
                          &(qa_q2_player_event){.kind = QA_Q2_PLAYER_FLASHLIGHT,
                                                .actor = id,
                                                .hand = a->client->rule.hand,
                                                .visible = enabled},
                          e);
}
static bool fog_touch(qa_q2_game *g, q2_actor *a, qa_actor_id id, qa_error *e) {
    q2_entity_state *s = a->entity;
    if (s->timestamp_ns > g->now_ns)
        return true;
    s->timestamp_ns = q2_deadline(g->now_ns, q2_item_seconds(s->wait));
    q2_actor *target = q2_ent(g, s->goal);
    q2_entity_state *values = target ? target->entity : s;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    float transition = (s->spawnflags & 4) ? 0 : values->delay != 0 ? values->delay : .5f;
    qa_q2_fog value;
    if (s->spawnflags & 16) {
        qa_body_state trigger;
        if (!qa_world_body_read(g->services.world, a->id, &trigger, e))
            return false;
        qa_bounds bounds = absolute_bounds(trigger);
        qa_vec3 center = qa_vec_scale(qa_vec_add(bounds.mins, bounds.maxs), .5f);
        qa_vec3 size = qa_vec_scale(qa_vec_add(qa_vec_sub(bounds.maxs, bounds.mins),
                                               qa_vec_sub(body.bounds.maxs, body.bounds.mins)),
                                    .5f),
                dir = s->direction;
        qa_vec3 start = qa_v3(-dir.x * size.x, -dir.y * size.y, -dir.z * size.z),
                end = qa_vec_scale(start, -1), relative = qa_vec_sub(body.origin, center);
        qa_vec3 distance =
            qa_v3(relative.x * fabsf(dir.x), relative.y * fabsf(dir.y), relative.z * fabsf(dir.z));
        float span = qa_vec_length(qa_vec_sub(start, end));
        float fraction =
            span > 0 ? q2_clamp(qa_vec_length(qa_vec_sub(distance, start)) / span, 0, 1) : 0;
        value = fog_lerp(fog_fields(g, values, true), fog_fields(g, values, false), fraction);
    } else {
        if (!(s->spawnflags & 8) && qa_vec_length(body.velocity) <= .0001f)
            return true;
        bool on =
            (s->spawnflags & 8) || qa_vec_dot(qa_vec_normalize(body.velocity), s->direction) > 0;
        value = fog_fields(g, values, !on);
    }
    q2_actor *player = q2_actor_get(g, id, false, NULL);
    if (player && player->client) {
        q2_client_state *p = player->client;
        p->rule.fog_transition = transition;
        if (s->spawnflags & 1) {
            p->rule.wanted_fog.density = value.density;
            p->rule.wanted_fog.color = value.color;
            p->rule.wanted_fog.sky_factor = value.sky_factor;
        }
        if (s->spawnflags & 2) {
            p->rule.wanted_fog.start_color = value.start_color;
            p->rule.wanted_fog.end_color = value.end_color;
            p->rule.wanted_fog.start_distance = value.start_distance;
            p->rule.wanted_fog.end_distance = value.end_distance;
            p->rule.wanted_fog.height_density = value.height_density;
            p->rule.wanted_fog.falloff = value.falloff;
        }
        return true;
    }
    qa_q2_entity_services *services = &g->entity_runtime->services;
    if (!services->player_fog) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 fog requires recipient capability");
        return false;
    }
    return services->player_fog(services->context, id, &value, s->spawnflags & 3, transition, e);
}
static bool relay_fire(qa_q2_game *g, q2_actor *a, qa_actor_id activator, qa_error *e) {
    qa_string_id message = a->entity->message;
    a->entity->message = 0;
    bool okay = q2_entity_targets(g, a, activator, false, e);
    if (q2_actor_live(g, a->id))
        a->entity->message = message;
    return okay;
}
static bool eligible(qa_q2_game *g, qa_actor_id id, bool *result, qa_error *e) {
    *result = false;
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!q2_actor_live(g, id))
        return true;
    if (!a || !a->client) {
        qa_q2_entity_services *services = &g->entity_runtime->services;
        if (!services->relay_eligible) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                         "Q2 cooperative relay requires selected player state");
            return false;
        }
        return services->relay_eligible(services->context, id, result, e);
    }
    q2_player_source_info *p = &a->client->info;
    if (p->dead || p->spectator || p->noclip)
        return true;
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, id, &combat, e))
        return false;
    *result = combat.health > 0;
    return true;
}
static bool relay(qa_q2_game *g, q2_actor *a, qa_actor_id activator, bool periodic, qa_error *e) {
    if (!periodic && !g->options.cooperative)
        return relay_fire(g, a, activator, e);
    q2_entity_state *s = a->entity;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_bounds bounds = absolute_bounds(body);
    qa_builtin_snapshot_frame *frame = q2_player_roster(g, e);
    if (!frame)
        return false;
    bool okay = false;
    size_t inside = 0, outside = 0;
    for (size_t i = 0; i < frame->snapshot.count; i++) {
        qa_actor_id id = frame->snapshot.ids[i];
        bool active;
        if (!eligible(g, id, &active, e))
            goto out;
        if (!active)
            continue;
        if (!qa_world_body_read(g->services.world, id, &body, e))
            goto out;
        if (intersects(bounds, absolute_bounds(body)))
            inside++;
        else
            outside++;
    }
    if (!outside) {
        qa_actor_id first = frame->snapshot.count ? frame->snapshot.ids[0] : (qa_actor_id){0};
        if (!relay_fire(g, a, periodic ? first : activator, e))
            goto out;
        okay = !periodic || !q2_actor_live(g, a->id) ||
               qa_session_release(g->services.session, a->id, e);
        goto out;
    }
    if ((!periodic || inside) && s->timestamp_ns < g->now_ns) {
        for (size_t i = 0; i < frame->snapshot.count; i++) {
            qa_actor_id id = frame->snapshot.ids[i];
            if (!q2_actor_live(g, id))
                continue;
            bool active;
            if (!eligible(g, id, &active, e))
                goto out;
            if (!periodic && !active)
                continue;
            if (!qa_world_body_read(g->services.world, id, &body, e))
                goto out;
            bool is_inside = active && intersects(bounds, absolute_bounds(body));
            if (!periodic && is_inside)
                continue;
            qa_string_id text = periodic && is_inside ? s->message : s->map;
            if (!q2_entity_message(
                    g, a, id, qa_strings_cstr(qa_session_strings(g->services.session), text), e))
                goto out;
            if (!q2_actor_live(g, a->id)) {
                okay = true;
                goto out;
            }
        }
        if (!periodic && q2_actor_live(g, activator) &&
            !q2_entity_message(g, a, activator,
                               qa_strings_cstr(qa_session_strings(g->services.session), s->message),
                               e))
            goto out;
        if (!q2_actor_live(g, a->id)) {
            okay = true;
            goto out;
        }
        s->timestamp_ns = q2_deadline(g->now_ns, 5 * Q2_NS);
    } else if (!periodic)
        s->timestamp_ns = q2_deadline(g->now_ns, 5 * Q2_NS);
    okay = !periodic || q2_entity_schedule(g, a, Q2ET_COOP_RELAY, s->wait);
out:
    qa_builtin_snapshot_release(frame);
    return okay;
}
bool q2_rerelease_poi(qa_q2_game *g, q2_actor *source, qa_actor_id activator, qa_error *e) {
    q2_entities *r = g->entity_runtime;
    q2_entity_state *s = source->entity;
    s->spawnflags &= ~8u;
    if (s->count && r->poi_stage > s->count)
        return true;
    q2_actor *selected = source, *master = source, *fallback = NULL;
    if (s->team) {
        for (q2_actor *m = g->first_actor; m; m = m->live_next)
            if (m->entity && m->entity->team == s->team) {
                master = m;
                break;
            }
        selected = NULL;
        float best_distance = INFINITY;
        int best_style = INT_MAX;
        qa_body_state from = {0};
        if (q2_actor_live(g, activator) &&
            !qa_world_body_read(g->services.world, activator, &from, e))
            return false;
        for (q2_actor *m = g->first_actor; m; m = m->live_next) {
            q2_entity_state *v = m->entity;
            if (!v || v->team != s->team || (v->spawnflags & 8))
                continue;
            if (v->spawnflags & 2) {
                fallback = m;
                continue;
            }
            if ((v->count && r->poi_stage > v->count) || v->style > best_style)
                continue;
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, m->id, &body, e))
                return false;
            qa_vec3 points[128];
            size_t count = 0;
            bool reachable;
            if (!q2_map_navigation(g, activator, from.origin, body.origin, points, 128, &count,
                                   &reachable, e))
                return false;
            if (!q2_actor_live(g, source->id))
                return true;
            float distance = INFINITY;
            if (reachable) {
                if (!count) {
                    qa_vec3 delta = qa_vec_sub(body.origin, from.origin);
                    distance = qa_vec_dot(delta, delta);
                } else {
                    qa_vec3 last = from.origin;
                    float length = 0;
                    for (size_t i = 0; i < count; i++) {
                        length += qa_vec_length(qa_vec_sub(points[i], last));
                        last = points[i];
                    }
                    length += qa_vec_length(qa_vec_sub(body.origin, last));
                    distance = length * length;
                }
            }
            bool nearest = (master->entity->spawnflags & 1) != 0;
            if (nearest && selected && distance > best_distance)
                continue;
            if (v->style < best_style) {
                if (nearest && !isfinite(distance))
                    continue;
                best_style = v->style;
                if (nearest)
                    best_distance = distance;
                selected = m;
            } else if (!nearest || distance < best_distance) {
                best_distance = distance;
                selected = m;
            }
        }
        if (!selected && fallback && (fallback->entity->spawnflags & 4))
            selected = fallback;
    }
    if (!selected)
        return true;
    q2_entity_state *v = selected->entity;
    if (v->kind == Q2E_POI && (v->spawnflags & 2) && !(v->spawnflags & 4))
        return true;
    if (v->count)
        r->poi_stage = v->count;
    r->poi = selected->id;
    r->poi_dynamic = (qa_actor_id){0};
    if (v->spawnflags & 4)
        for (q2_actor *m = g->first_actor; m; m = m->live_next)
            if (m->entity &&
                (s->team ? m->entity->team == s->team : qa_actor_id_equal(m->id, source->id)) &&
                (m->entity->spawnflags & 2)) {
                r->poi_dynamic = m->id;
                break;
            }
    r->poi_image = q2_field_id(v, g->field_keys[QA_TARGET_KEY_IMAGE]);
    return r->poi_image || qa_builtin_resource(&g->services, "friend", &r->poi_image, e);
}
static bool world_text(qa_q2_game *g, q2_actor *a, qa_error *e) {
    static const qa_vec3 colors[] = {
        {1, 1, 1}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0},
        {1, 1, 0}, {0, 0, 0}, {0, 1, 1}, {116.0f / 255, 61.0f / 255, 50.0f / 255}};
    q2_entity_state *s = a->entity;
    float radius = q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_RADIUS], 0);
    if (radius >= 0) {
        float color = q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_SOUNDS], 0);
        unsigned index = color >= 0 && color < 8 && truncf(color) == color ? (unsigned)color : 0;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        float yaw = fmodf(body.angles.y, 360);
        if (yaw < 0)
            yaw += 360;
        yaw += 180;
        if (yaw > 360)
            yaw -= 360;
        qa_strings *strings = qa_session_strings(g->services.session);
        qa_bytes message = qa_strings_text(strings, s->message);
        char text[254];
        size_t at = 0, length = 0, glyphs = 0;
        uint32_t scalar;
        while (glyphs < 127 && qa_utf8_next(message, &at, &scalar)) {
            if (scalar > 0xffff) {
                uint32_t pair = scalar - 0x10000;
                length += qa_utf8_encode((0xd800u + (pair >> 10)) & 255u, text + length);
                if (++glyphs == 127)
                    break;
                scalar = 0xdc00u + (pair & 1023u);
            }
            length += qa_utf8_encode(scalar & 255u, text + length);
            ++glyphs;
        }
        qa_string_id resource;
        if (!qa_strings_intern(strings, (qa_bytes){(const uint8_t *)text, length}, &resource, e))
            return false;
        if (!q2_map_event(g,
                          &(qa_q2_map_event){.kind = QA_Q2_MAP_WORLD_TEXT,
                                             .actor = a->id,
                                             .text = resource,
                                             .origin = body.origin,
                                             .direction = qa_v3(0, yaw, 0),
                                             .color = colors[index],
                                             .alpha = 1,
                                             .value = (radius == 0 ? .2f : radius) * 8,
                                             .duration = (float)g->frame_ns / Q2_NS,
                                             .flags = 1u | (body.angles.y == -3 ? 2u : 0u),
                                             .visible = true},
                          e))
            return false;
    }
    return !q2_actor_live(g, a->id) ||
           q2_entity_schedule(g, a, Q2ET_WORLD_TEXT, (float)g->frame_ns / Q2_NS);
}
static bool sky(qa_q2_game *g, q2_actor *a, bool initial, qa_error *e) {
    q2_entities *r = g->entity_runtime;
    q2_entity_state *s = a->entity;
    if (initial || q2_field_id(s, g->field_keys[QA_TARGET_KEY_SKY]))
        r->sky = q2_field_id(s, g->field_keys[QA_TARGET_KEY_SKY]);
    if (initial && !r->sky && !qa_builtin_resource(&g->services, "unit1_", &r->sky, e))
        return false;
    if (initial || q2_field_id(s, g->field_keys[QA_TARGET_KEY_SKYROTATE]))
        r->sky_rotation = q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_SKYROTATE], 0);
    if (initial || q2_field_id(s, g->field_keys[QA_TARGET_KEY_SKYAUTOROTATE]))
        r->sky_auto = q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_SKYAUTOROTATE], 1) != 0;
    if (initial || q2_field_id(s, g->field_keys[QA_TARGET_KEY_SKYAXIS]))
        r->sky_axis = q2_field_vec(g, s, g->field_keys[QA_TARGET_KEY_SKYAXIS], qa_v3(0, 0, 1));
    return q2_map_event(g,
                        &(qa_q2_map_event){.kind = QA_Q2_MAP_SKY,
                                           .actor = a->id,
                                           .resource = r->sky,
                                           .direction = r->sky_axis,
                                           .value = r->sky_rotation,
                                           .flags = r->sky_auto ? 1u : 0u},
                        e);
}
bool q2_rerelease_entity_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_entities *r = g->entity_runtime;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    *handled = true;
    if (!strcmp(name, "worldspawn")) {
        r->world_fog = fog_fields(g, s, false);
        r->goals = q2_field_id(s, g->field_keys[QA_TARGET_KEY_GOALS]);
        r->has_goals = r->goals != 0;
        if (q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_HUB_MAP], 0) != 0) {
            r->primary = r->secondary = 0;
            r->primary_changes = r->secondary_changes = 0;
            for (q2_actor *p = g->first_actor; p; p = p->live_next)
                if (p->client)
                    p->client->rule.mission_primary = p->client->rule.mission_secondary = 0;
        }
        if (r->has_goals)
            r->primary_changes++;
        *handled = false;
        return sky(g, a, true, e);
    }
    if (!strcmp(name, "misc_flare"))
        s->kind = Q2E_FLARE;
    else if (!strcmp(name, "info_world_text"))
        s->kind = Q2E_WORLD_TEXT;
    else if (!strcmp(name, "trigger_flashlight"))
        s->kind = Q2E_FLASHLIGHT;
    else if (!strcmp(name, "trigger_fog"))
        s->kind = Q2E_FOG;
    else if (!strcmp(name, "trigger_coop_relay"))
        s->kind = Q2E_COOP_RELAY;
    else if (!strcmp(name, "target_poi"))
        s->kind = Q2E_POI;
    else if (!strcmp(name, "target_music"))
        s->kind = Q2E_MUSIC;
    else if (!strcmp(name, "target_sky"))
        s->kind = Q2E_SKY;
    else if (!strcmp(name, "target_crossunit_trigger"))
        s->kind = Q2E_CROSS_UNIT_TRIGGER;
    else if (!strcmp(name, "target_crossunit_target"))
        s->kind = Q2E_CROSS_UNIT_TARGET;
    else if (!strcmp(name, "target_autosave"))
        s->kind = Q2E_AUTOSAVE;
    else if (!strcmp(name, "target_achievement"))
        s->kind = Q2E_ACHIEVEMENT;
    else if (!strcmp(name, "target_story"))
        s->kind = Q2E_STORY;
    else if (!strcmp(name, "target_healthbar"))
        s->kind = Q2E_HEALTHBAR;
    else {
        *handled = false;
        return true;
    }
    s->usable = true;
    s->visual.visible = false;
    if (g->options.deathmatch &&
        (s->kind == Q2E_POI || (s->kind >= Q2E_CROSS_UNIT_TRIGGER && s->kind <= Q2E_HEALTHBAR)))
        return qa_session_release(g->services.session, a->id, e);
    switch (s->kind) {
    case Q2E_FLARE: {
        s->visual.render_flags = 0x200000 | ((s->spawnflags & 7) << 10) |
                                 ((s->spawnflags & 8) ? 1u : 0u) |
                                 (q2_field_id(s, g->field_keys[QA_TARGET_KEY_IMAGE]) ? 256u : 0u);
        s->visual.scale = q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_RADIUS], 0);
        s->visual.models[0] = q2_field_id(s, g->field_keys[QA_TARGET_KEY_IMAGE]);
        s->visual.visible = true;
        s->usable = s->targetname != 0;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        body.bounds = (qa_bounds){{-32, -32, -32}, {32, 32, 32}};
        return q2_entity_body(g, a, &body, false, e) &&
               q2_entity_solid(g, a, QA_PHYSICS_NOT_SOLID, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    }
    case Q2E_WORLD_TEXT:
        if (!s->message)
            return qa_session_release(g->services.session, a->id, e);
        if (!(s->spawnflags & 1)) {
            s->activator = a->id;
            q2_entity_schedule(g, a, Q2ET_WORLD_TEXT, (float)g->frame_ns / Q2_NS);
        }
        return true;
    case Q2E_FLASHLIGHT:
    case Q2E_FOG:
    case Q2E_COOP_RELAY:
        if (!q2_entity_init_trigger(g, a, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        s->usable = false;
        if (s->kind == Q2E_FLASHLIGHT) {
            s->direction.z = q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_HEIGHT], 0);
            return true;
        }
        if (s->kind == Q2E_FOG) {
            if (s->delay == 0)
                s->delay = .5f;
            q2_entity_pick(g, s->target, &s->goal);
            return true;
        }
        s->touchable = false;
        if (s->wait == 0)
            s->wait = 1;
        if (!s->message &&
            !qa_builtin_resource(&g->services, "$g_coop_wait_for_players", &s->message, e))
            return false;
        if (!s->map &&
            !qa_builtin_resource(&g->services, "$g_coop_players_waiting_for_you", &s->map, e))
            return false;
        if (s->spawnflags & 1)
            return q2_entity_schedule(g, a, Q2ET_COOP_RELAY, s->wait);
        s->usable = true;
        return true;
    case Q2E_POI:
        return q2_entity_schedule(g, a, Q2ET_POI, .001f);
    case Q2E_CROSS_UNIT_TARGET:
        s->usable = false;
        if (s->delay == 0)
            s->delay = 1;
        return q2_entity_schedule(g, a, Q2ET_CROSS, s->delay);
    case Q2E_HEALTHBAR:
        if (!s->target || !s->message)
            return qa_session_release(g->services.session, a->id, e);
        return q2_entity_schedule(g, a, Q2ET_HEALTHBAR, .025f);
    default:
        return true;
    }
}
bool q2_rerelease_entity_use(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_actor_id activator,
                             bool *handled, qa_error *e) {
    (void)other;
    q2_entity_state *s = a->entity;
    q2_entities *r = g->entity_runtime;
    *handled = true;
    switch (s->kind) {
    case Q2E_FLARE:
        s->visual.visible = !s->visual.visible;
        return qa_world_link(g->services.world, a->id, NULL, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    case Q2E_WORLD_TEXT: {
        if (!s->activator.registry) {
            s->activator = activator;
            if (!world_text(g, a, e))
                return false;
        } else {
            q2_entity_schedule(g, a, Q2ET_NONE, 0);
            s->activator = (qa_actor_id){0};
        }
        if (!q2_actor_live(g, a->id))
            return true;
        if (s->spawnflags & 2)
            s->usable = false;
        qa_actor_id target;
        if (q2_entity_pick(g, s->target, &target) && !use_actor(g, target, a->id, e))
            return false;
        return !q2_actor_live(g, a->id) || !(s->spawnflags & 4) ||
               qa_session_release(g->services.session, a->id, e);
    }
    case Q2E_COOP_RELAY:
        return relay(g, a, activator, false, e);
    case Q2E_POI:
        return q2_rerelease_poi(g, a, activator, e);
    case Q2E_MUSIC: {
        qa_string_id track = q2_field_id(s, g->field_keys[QA_TARGET_KEY_SOUNDS]);
        if (!track && !qa_builtin_resource(&g->services, "0", &track, e))
            return false;
        return q2_map_event(
            g, &(qa_q2_map_event){.kind = QA_Q2_MAP_MUSIC, .actor = a->id, .resource = track}, e);
    }
    case Q2E_SKY:
        return sky(g, a, false, e);
    case Q2E_CROSS_UNIT_TRIGGER: {
        uint32_t flags;
        if (!q2_entity_flags(g, false, true, &flags, e))
            return false;
        flags |= s->spawnflags;
        return q2_entity_flags(g, true, true, &flags, e) &&
               (!q2_actor_live(g, a->id) || qa_session_release(g->services.session, a->id, e));
    }
    case Q2E_AUTOSAVE:
        if (g->now_ns <=
            q2_deadline(r->last_autosave_ns,
                        q2_item_seconds(g->player_runtime->rules.autosave_minimum_seconds)))
            return true;
        if (!q2_map_event(g, &(qa_q2_map_event){.kind = QA_Q2_MAP_AUTOSAVE, .actor = a->id}, e))
            return false;
        r->last_autosave_ns = g->now_ns;
        return true;
    case Q2E_ACHIEVEMENT:
        return q2_map_event(g,
                            &(qa_q2_map_event){.kind = QA_Q2_MAP_ACHIEVEMENT,
                                               .actor = a->id,
                                               .text = q2_field_id(s, g->field_keys[QA_TARGET_KEY_ACHIEVEMENT])},
                            e);
    case Q2E_STORY:
        r->story = s->message;
        return q2_map_event(
            g, &(qa_q2_map_event){.kind = QA_Q2_MAP_STORY, .actor = a->id, .text = r->story}, e);
    case Q2E_HEALTHBAR: {
        qa_actor_id target;
        if (!q2_entity_pick(g, s->target, &target) || !qa_actor_id_equal(target, s->goal))
            return qa_session_release(g->services.session, a->id, e);
        for (size_t i = 0; i < 2; i++)
            if (!r->bars[i].controller.registry) {
                s->enemy = target;
                r->bars[i] = (q2_healthbar){.controller = a->id, .target = target};
                return true;
            }
        return qa_session_release(g->services.session, a->id, e);
    }
    default:
        *handled = false;
        return true;
    }
}
bool q2_rerelease_entity_think(qa_q2_game *g, q2_actor *a, q2_entity_think think, bool *handled,
                               qa_error *e) {
    q2_entity_state *s = a->entity;
    *handled = true;
    switch (think) {
    case Q2ET_WORLD_TEXT:
        return world_text(g, a, e);
    case Q2ET_COOP_RELAY:
        return relay(g, a, (qa_actor_id){0}, true, e);
    case Q2ET_POI:
        if (s->spawnflags & 5)
            for (q2_actor *m = g->first_actor; m; m = m->live_next)
                if (m->entity &&
                    (s->team ? m->entity->team == s->team : qa_actor_id_equal(m->id, a->id)))
                    m->entity->spawnflags |= s->spawnflags & 5;
        return true;
    case Q2ET_HEALTHBAR: {
        qa_actor_id target;
        qa_builtin_actor_traits traits = {0};
        if (!q2_entity_pick(g, s->target, &target) || !g->services.actor_traits ||
            !g->services.actor_traits(g->services.context, target, &traits) || !traits.monster)
            return qa_session_release(g->services.session, a->id, e);
        s->goal = target;
        return true;
    }
    case Q2ET_CROSS:
        if (s->kind == Q2E_CROSS_UNIT_TARGET) {
            uint32_t flags;
            if (!q2_entity_flags(g, false, true, &flags, e))
                return false;
            if (s->spawnflags == (flags & 0xff00ff & s->spawnflags)) {
                if (!q2_entity_targets(g, a, a->id, false, e))
                    return false;
                return !q2_actor_live(g, a->id) ||
                       qa_session_release(g->services.session, a->id, e);
            }
            return true;
        }
        *handled = false;
        return true;
    default:
        *handled = false;
        return true;
    }
}
bool q2_rerelease_entity_touch(qa_q2_game *g, q2_actor *a, const qa_touch_contact *contact,
                               bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    *handled = s->kind == Q2E_FLASHLIGHT || s->kind == Q2E_FOG;
    if (!*handled)
        return true;
    qa_builtin_actor_traits traits = {0};
    if (!g->services.actor_traits ||
        !g->services.actor_traits(g->services.context, contact->other, &traits) || !traits.player)
        return true;
    if (s->kind == Q2E_FOG)
        return fog_touch(g, a, contact->other, e);
    if (s->spawnflags & 1) {
        bool inside;
        if (!q2_entity_clip(g, a, contact->other, &inside, e))
            return false;
        if (!inside)
            return true;
    }
    if (s->style == 1 || s->style == 2)
        return flashlight(g, contact->other, s->style == 1, e);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, contact->other, &body, e))
        return false;
    return qa_vec_dot(body.velocity, body.velocity) <= 32 ||
           flashlight(g, contact->other,
                      qa_vec_dot(qa_vec_normalize(body.velocity), s->direction) > 0, e);
}
bool qa_q2_healthbar_transfer(qa_q2_game *g, qa_actor_id old, qa_actor_id replacement,
                              qa_error *e) {
    if (!g || !q2_actor_live(g, replacement)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 healthbar replacement");
        return false;
    }
    for (size_t i = 0; i < 2; i++)
        if (qa_actor_id_equal(g->entity_runtime->bars[i].target, old)) {
            g->entity_runtime->bars[i].target = replacement;
            q2_actor *controller = q2_ent(g, g->entity_runtime->bars[i].controller);
            if (controller)
                controller->entity->enemy = replacement;
        }
    return true;
}
bool qa_q2_entities_player_begin(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    if (!g || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 map recipient");
        return false;
    }
    if (g->options.edition != QA_Q2_RERELEASE)
        return true;
    q2_actor *player = q2_actor_get(g, id, false, NULL);
    if (player && player->client) {
        q2_client_state *p = player->client;
        if (!fog_equal(p->rule.fog, p->rule.wanted_fog)) {
            qa_q2_fog fog = p->rule.wanted_fog;
            float milliseconds = q2_clamp(truncf(p->rule.fog_transition * 1000), 0, 65535);
            if (!q2_map_event(g,
                              &(qa_q2_map_event){.kind = QA_Q2_MAP_FOG,
                                                 .recipient = id,
                                                 .fog = fog,
                                                 .duration = milliseconds / 1000},
                              e))
                return false;
            if (!q2_actor_live(g, id))
                return true;
            p->rule.fog = fog;
        }
        if (!q2_rerelease_notify(g, player, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
    }
    return true;
}
bool qa_q2_entities_player_frame(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    if (!g || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 map recipient");
        return false;
    }
    if (g->options.edition != QA_Q2_RERELEASE)
        return true;
    q2_entities *r = g->entity_runtime;
    for (size_t slot = 0; slot < 2; slot++) {
        q2_healthbar *bar = &r->bars[slot];
        if (!bar->controller.registry)
            continue;
        q2_actor *controller = q2_ent(g, bar->controller);
        float health = 0;
        qa_builtin_actor_traits traits = {0};
        if (q2_actor_live(g, bar->target)) {
            qa_combat_state state;
            if (!qa_combat_read(g->services.combat, bar->target, &state, e))
                return false;
            health = state.health;
            if (g->services.actor_traits)
                g->services.actor_traits(g->services.context, bar->target, &traits);
        }
        bool remove = !controller || (bar->dying && bar->dead_until_ns < g->now_ns);
        bool holds = r->services.holds_healthbar
                         ? r->services.holds_healthbar(r->services.context, bar->target)
                         : qa_q2_monster_holds_healthbar(g, bar->target);
        if (!remove && health <= 0 && !bar->dying && !holds) {
            if (controller->entity->delay == 0)
                remove = true;
            else {
                bar->dying = true;
                bar->dead_until_ns =
                    q2_deadline(g->now_ns, q2_item_seconds(controller->entity->delay));
            }
        }
        qa_q2_map_event event = {.kind = QA_Q2_MAP_HEALTHBAR,
                                 .actor = bar->controller,
                                 .recipient = id,
                                 .target = bar->target,
                                 .text = controller ? controller->entity->message : 0,
                                 .slot = (int)slot};
        if (remove) {
            *bar = (q2_healthbar){0};
            qa_builtin_snapshot_frame *players = q2_player_roster(g, e);
            if (!players)
                return false;
            bool okay = true;
            for (size_t i = 0; i < players->snapshot.count; i++) {
                event.recipient = players->snapshot.ids[i];
                if (q2_actor_live(g, event.recipient) && !q2_map_event(g, &event, e)) {
                    okay = false;
                    break;
                }
            }
            qa_builtin_snapshot_release(players);
            if (!okay)
                return false;
            if (!q2_actor_live(g, id))
                return true;
            continue;
        }
        bool empty = bar->dying || health <= 0;
        event.visible = empty || !(controller->entity->spawnflags & 1);
        if (!event.visible && q2_actor_live(g, bar->target)) {
            qa_body_state one, two;
            if (!qa_world_body_read(g->services.world, id, &one, e) ||
                !qa_world_body_read(g->services.world, bar->target, &two, e))
                return false;
            qa_collision_leaf from, to;
            qa_collision_geometry *geometry = qa_world_geometry(g->services.world);
            if (!qa_collision_point_leaf(geometry, one.origin, QA_LEAF_COLLISION, &from, e) ||
                !qa_collision_point_leaf(geometry, two.origin, QA_LEAF_COLLISION, &to, e) ||
                !qa_collision_cluster_visible(geometry, (int32_t)from.cluster, (int32_t)to.cluster,
                                              false, &event.visible, e))
                return false;
        }
        event.value =
            empty || traits.max_health <= 0 ? 0 : q2_clamp(health / traits.max_health, 0, 1);
        if (!q2_map_event(g, &event, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
    }
    q2_actor *player = q2_actor_get(g, id, false, NULL);
    return g->player_runtime->intermission || !player || !player->client ||
           q2_rerelease_goal_frame(g, player, e);
}
