#include "internal.h"
#include "qa/text.h"

static uint32_t bits(float value) {
    return (uint32_t)qa_source_float_to_i32(value);
}
uint32_t q2_entity_color(const char *text) {
    if (!strchr(text, ' '))
        return (uint32_t)strtoll(text, NULL, 10);
    float c[4] = {0, 0, 0, 1};
    const char *p = text;
    for (unsigned i = 0; i < 4; i++) {
        while (isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        char *end;
        c[i] = (float)strtod(p, &end);
        if (end == p) {
            c[i] = 0;
            while (*p && !isspace((unsigned char)*p))
                p++;
        } else
            p = end;
    }
    float multiplier = c[0] > 1 || c[1] > 1 || c[2] > 1 || c[3] > 1 ? 1 : 255;
    return bits(c[3] * multiplier) | (bits(c[2] * multiplier) << 8) |
           (bits(c[1] * multiplier) << 16) | (bits(c[0] * multiplier) << 24);
}
static qa_vec3 color(uint32_t packed) {
    return qa_v3((float)((packed >> 24) & 255) / 255, (float)((packed >> 16) & 255) / 255,
                 (float)((packed >> 8) & 255) / 255);
}
static bool show(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_q2_map_event light = {.kind = QA_Q2_MAP_DYNAMIC_LIGHT,
                             .actor = a->id,
                             .origin = body.origin,
                             .color = color((uint32_t)s->visual.skin),
                             .radius = (float)s->visual.frame,
                             .visible = s->visual.visible,
                             .intensity = 1,
                             .style = -1};
    if (s->stage == 1) {
        light.color = s->visual.skin ? light.color : qa_v3(1, 1, 1);
        light.radius = q2_field_float(g, s, "shadowlightradius", 0);
        light.intensity = q2_field_float(g, s, "shadowlightintensity", 1);
        light.resolution = bits(q2_field_float(g, s, "shadowlightresolution", 0));
        light.fade_start = q2_field_float(g, s, "shadowlightstartfadedistance", 0);
        light.fade_end = q2_field_float(g, s, "shadowlightendfadedistance", 0);
        light.style = qa_source_float_to_i32(q2_field_float(g, s, "shadowlightstyle", -1));
        qa_actor_id style;
        if (q2_map_find(g, NULL, q2_field_id(g, s, "shadowlightstyletarget"), 0, &style)) {
            q2_actor *owner = q2_ent(g, style);
            if (owner)
                light.style = owner->entity->style;
        }
        qa_actor_id target;
        if (s->target && q2_map_find(g, NULL, s->target, 0, &target)) {
            qa_body_state to;
            if (!qa_world_body_read(g->services.world, target, &to, e))
                return false;
            light.direction = qa_vec_normalize(qa_vec_sub(to.origin, body.origin));
            light.cone_cosine =
                cosf(q2_field_float(g, s, "shadowlightconeangle", 45) * .01745329251994329577f);
            light.flags |= 1;
        }
    }
    return q2_map_event(g, &light, e);
}
bool q2_light_use(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    if (s->stage == 1) {
        s->visual.visible = !s->visual.visible;
        return show(g, a, e);
    }
    s->active = !s->active;
    s->visual.visible = s->active;
    if (!show(g, a, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!s->active)
        return q2_entity_schedule(g, a, Q2ET_NONE, 0);
    return !(s->goal.registry || (s->spawnflags & 4)) ||
           q2_entity_schedule(g, a, Q2ET_DYNAMIC_LIGHT, .1f);
}
bool q2_light_think(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    if ((s->spawnflags & 4) && q2_random(g) < .5f)
        s->visual.visible = !s->visual.visible;
    if (s->goal.registry) {
        qa_q2_entity_services *services = &g->entity_runtime->services;
        qa_string_id style;
        if (!services->lightstyle) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 target_light requires lightstyle service");
            return false;
        }
        if (!services->lightstyle(services->context, s->style, &style, e))
            return false;
        const char *pattern = qa_strings_cstr(qa_session_strings(g->services.session), style);
        size_t length = strlen(pattern);
        qa_q2_visual target;
        if (!length || !q2_actor_live(g, s->goal)) {
            qa_error_set(e, QA_ERROR_FORMAT, 0,
                         "Q2 target_light lost its color target or lightstyle");
            return false;
        }
        q2_actor *native = q2_ent(g, s->goal);
        if (native)
            target = native->entity->visual;
        else if (!services->read_visual) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 target_light requires shared visual read");
            return false;
        } else if (!services->read_visual(services->context, s->goal, &target, e))
            return false;
        s->delay += s->speed;
        double sample = trunc((double)s->delay), mod = fmod(sample, (double)length);
        size_t index = (size_t)(mod < 0 ? mod + (double)length : mod);
        float current = ((unsigned char)pattern[index] - 97) / 25.0f,
              next = ((unsigned char)pattern[(index + 1) % length] - 97) / 25.0f,
              fraction = fmodf(s->delay, 1);
        float t = (s->spawnflags & 2) ? current : next * fraction + current * (1 - fraction);
        uint32_t result = 0;
        for (unsigned shift = 8; shift <= 24; shift += 8) {
            float channel = (float)(((uint32_t)target.skin >> shift) & 255) * t +
                            (float)(((uint32_t)s->count >> shift) & 255) * (1 - t);
            result |= bits(channel) << shift;
        }
        s->visual.skin = (int32_t)result;
    }
    return show(g, a, e) &&
           (!q2_actor_live(g, a->id) || q2_entity_schedule(g, a, Q2ET_DYNAMIC_LIGHT, .1f));
}
bool q2_light_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    bool shadow = !strcmp(name, "dynamic_light"),
         target = !strcmp(name, "target_light") && g->options.edition == QA_Q2_RERELEASE;
    *handled = shadow || target;
    if (!*handled)
        return true;
    s->kind = Q2E_DYNAMIC_LIGHT;
    s->stage = shadow ? 1 : 0;
    s->usable = target || s->targetname != 0;
    if (shadow) {
        if (q2_field_float(g, s, "shadowlightradius", 0) > 0) {
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, a->id, &body, e))
                return false;
            body.bounds = (qa_bounds){0};
            s->visual.render_flags = 1u << 14;
            if (!q2_entity_body(g, a, &body, true, e))
                return false;
        }
        s->visual.visible = (s->spawnflags & 1) == 0;
        return true;
    }
    s->visual.visible = false;
    float radius = q2_field_float(g, s, "radius", 150);
    s->visual.frame = (int32_t)bits(radius != 0 ? radius : 150);
    s->count = s->visual.skin;
    q2_entity_pick(g, s->target, &s->goal);
    if ((s->spawnflags & 1) && !q2_light_use(g, a, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    s->speed = s->speed == 0 ? 1 : .1f / s->speed;
    if (g->options.product == QA_Q2_N64)
        s->style += 10;
    return qa_world_link(g->services.world, a->id, NULL, e) &&
           (!q2_actor_live(g, a->id) || show(g, a, e));
}
bool qa_q2_entities_present(qa_q2_game *g, qa_error *e) {
    if (!g) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing Q2 presentation provider");
        return false;
    }
    for (q2_actor *a = g->first_actor; a;) {
        q2_actor *next = a->live_next;
        if (a->entity && a->entity->kind == Q2E_DYNAMIC_LIGHT && a->entity->stage == 1 &&
            !show(g, a, e))
            return false;
        a = next;
    }
    return true;
}
