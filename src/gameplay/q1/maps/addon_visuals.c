#include "internal.h"
#include "qa/game_q1_wire.h"
#include <float.h>
#include <limits.h>

static q1_actor *visual(qa_q1_game *g, qa_actor_id id) {
    q1_actor *e = q1_entity(g, id);
    return e && e->map ? e : NULL;
}
bool q1_map_frame_tick_add(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    if (!g->maps->frame_ticks) {
        g->maps->frame_ticks = calloc(g->capacity, sizeof(*g->maps->frame_ticks));
        if (!g->maps->frame_ticks) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 authored frame ticks");
            return false;
        }
    }
    for (uint32_t i = 0; i < g->maps->frame_tick_count; ++i)
        if (qa_actor_id_equal(g->maps->frame_ticks[i], id))
            return true;
    if (g->maps->frame_tick_count >= g->capacity)
        return q1_map_fail(error, "Q1 authored frame tick roster is full");
    memmove(g->maps->frame_ticks + 1, g->maps->frame_ticks,
              g->maps->frame_tick_count * sizeof(*g->maps->frame_ticks));
    g->maps->frame_ticks[0] = id;
    ++g->maps->frame_tick_count;
    return true;
}
void q1_map_frame_tick_remove(qa_q1_game *g, qa_actor_id id) {
    if (!g->maps)
        return;
    for (uint32_t i = 0; i < g->maps->frame_tick_count; ++i)
        if (qa_actor_id_equal(g->maps->frame_ticks[i], id)) {
            --g->maps->frame_tick_count;
            memmove(g->maps->frame_ticks + i, g->maps->frame_ticks + i + 1,
                       (g->maps->frame_tick_count - i) * sizeof(*g->maps->frame_ticks));
            return;
        }
}
static bool ramp_tick(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id, owner = q1_ref_actor(g, e->owner);
    uint8_t phase = e->map->pending.addon.phase;
    double delta = g->elapsed * e->delay;
    if (fabs(delta) <= FLT_MAX)
        delta = (float)delta;
    double fraction = e->map->counter_value + (phase == 3 ? -delta : phase == 1 ? delta : 0);
    if (fraction > 0 && fraction < 1)
        fraction = (float)fraction;
    unsigned maximum = e->spawnflags & 1 ? 25 : 12;
    if (!q1_alive(g, owner))
        return q1_map_fail(error, "Q1 light ramp lost its light");
    double style = 0;
    qa_targets_number(g->maps->options.targets, owner, qa_targets_field_keys(g->maps->options.targets)[QA_TARGET_KEY_STYLE], &style);
    if (!visual(g, id))
        return true;
    if (!isfinite(style) || floor(style) != style || style < INT32_MIN || style > INT32_MAX)
        return q1_map_fail(error, "Invalid Q1 light ramp style");
    char pattern[2] = {(char)('a' + (fraction <= 0 ? 0 : fraction >= 1 ? maximum
                                            : (unsigned)floor(fraction * (maximum + 1)))), 0};
    qa_string_id value;
    if (!qa_builtin_resource(&g->services, pattern, &value, error))
        return false;
    if (!visual(g, id))
        return true;
    if (!g->maps->options.lightstyle(g->maps->options.context, (int32_t)style, value, error))
        return false;
    e = visual(g, id);
    if (!e)
        return true;
    if (fraction >= 1 || fraction <= 0) {
        e->map->counter_value = fraction >= 1 ? 1 : 0;
        e->map->pending.addon.phase = fraction >= 1 ? 2 : 0;
        q1_map_frame_tick_remove(g, id);
    } else
        e->map->counter_value = (float)fraction;
    return true;
}
static bool rope_segment(qa_q1_game *g, qa_actor_id parent_id, bool *published,
                         qa_error *error) {
    *published = false;
    q1_actor *child;
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_MISC_ROPE_SEGMENT], Q1_MAP, (qa_actor_id){0}, &child, error))
        return false;
    qa_actor_id child_id = child->id;
    if (!q1_map_allocate(g, child, error))
        goto failure;
    child->map->kind = Q1_MAP_ROPE_SEGMENT;
    child->frame = 3;
    child->physics.solid = QA_PHYSICS_NOT_SOLID;
    child->physics.motion = QA_PHYSICS_STATIONARY;
    if (!q1_model(g, child, g->runtime_names[Q1_NAME_RESOURCE_PROGS_ROPEX_MDL], error))
        goto failure;
    q1_actor *parent = visual(g, parent_id);
    child = visual(g, child_id);
    if (!parent || !child)
        goto retired;
    child->skin = parent->skin;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, parent_id, &body, error))
        goto failure;
    parent = visual(g, parent_id);
    child = visual(g, child_id);
    if (!parent || !child)
        goto retired;
    body.bounds = (qa_bounds){{-4, -4, 0}, {4, 4, 128}};
    if (!qa_world_body_write(g->services.world, parent_id, &body, error))
        goto failure;
    parent = visual(g, parent_id);
    child = visual(g, child_id);
    if (!parent || !child)
        goto retired;
    if (!q1_link(g, parent, error))
        goto failure;
    parent = visual(g, parent_id);
    child = visual(g, child_id);
    if (!parent || !child)
        goto retired;
    child->map->pending.addon.chain = parent->map->pending.addon.chain;
    parent->map->pending.addon.chain = q1_ref_from(g, child_id);
    ++parent->count;
    *published = true;
    return true;
failure:
    if (qa_actors_get(qa_session_actors(g->services.session), child_id))
        qa_session_release(g->services.session, child_id, NULL);
    return false;
retired:
    if (qa_actors_get(qa_session_actors(g->services.session), child_id))
        return qa_session_release(g->services.session, child_id, error);
    return true;
}
static bool rope_tick(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    if (!isfinite(e->count) || e->count < 0 || e->count > 32 || floorf(e->count) != e->count)
        return q1_map_fail(error, "Invalid Q1 rope segment count");
    qa_vec3 original = e->map->pending.addon.origin;
    qa_trace_result top, bottom;
    if (!q1_trace(g, original, qa_vec_add(original, qa_v3(0, 0, 2048)), id, false, &top, error))
        return false;
    e = visual(g, id);
    if (!e)
        return true;
    if (!q1_trace(g, original, qa_vec_add(original, qa_v3(0, 0, -2048)), id, false, &bottom, error))
        return false;
    e = visual(g, id);
    if (!e)
        return true;
    double length = floor((double)qa_vec_length(qa_vec_sub(bottom.end, top.end)) / 32);
    if (!isfinite(length) || length < 0 || length > INT32_MAX)
        return q1_map_fail(error, "Invalid Q1 rope trace length");
    int32_t segments = (int32_t)length;
    int32_t models = segments / 4;
    if (models > 32)
        models = 32;
    while (e->count > (float)models) {
        qa_actor_id first_id = q1_ref_actor(g, e->map->pending.addon.chain);
        q1_actor *first = visual(g, first_id);
        qa_actor_id following = q1_ref_actor(g, first ? first->map->pending.addon.chain : (q1_ref){0});
        if (first && first->map->kind == Q1_MAP_ROPE_SEGMENT && !q1_remove(g, first, error))
            return false;
        e = visual(g, id);
        if (!e)
            return true;
        e->map->pending.addon.chain = q1_ref_from(g, following);
        --e->count;
    }
    while (e->count < (float)models) {
        bool published;
        if (!rope_segment(g, id, &published, error))
            return false;
        e = visual(g, id);
        if (!e || !published)
            return true;
    }
    qa_vec3 position = bottom.end;
    qa_actor_id child_id = q1_ref_actor(g, e->map->pending.addon.chain);
    for (int32_t i = 0; i < models; ++i) {
        q1_actor *child = visual(g, child_id);
        if (!child || child->map->kind != Q1_MAP_ROPE_SEGMENT)
            return q1_map_fail(error, "Q1 rope lost its segment chain");
        qa_actor_id following = q1_ref_actor(g, child->map->pending.addon.chain);
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, child_id, &body, error))
            return false;
        if (!visual(g, id))
            return true;
        if (visual(g, child_id)) {
            body.origin = position;
            if (!qa_world_body_write(g->services.world, child_id, &body, error))
                return false;
            child = visual(g, child_id);
            if (child && !q1_link(g, child, error))
                return false;
        }
        position.z += 128;
        child_id = following;
        segments -= 4;
    }
    e = visual(g, id);
    if (!e)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    e = visual(g, id);
    if (!e)
        return true;
    e->frame = segments;
    body.origin = position;
    body.bounds = (qa_bounds){{-4, -4, 0}, {4, 4, 32.0f * ((float)segments + 1)}};
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    e = visual(g, id);
    return !e || q1_link(g, e, error);
}
bool q1_map_addon_frame(qa_q1_game *g, qa_error *error) {
    if (!g->maps || !g->maps->frame_tick_count)
        return true;
    qa_builtin_snapshot_frame *list;
    if (!q1_snapshot_actors(g, &list, error))
        return false;
    list->snapshot.count = g->maps->frame_tick_count;
    memcpy(list->snapshot.ids, g->maps->frame_ticks, list->snapshot.count * sizeof(*list->snapshot.ids));
    bool ok = true;
    for (size_t i = 0; ok && i < list->snapshot.count; ++i) {
        q1_actor *e = visual(g, list->snapshot.ids[i]);
        if (!e)
            continue;
        if (e->map->kind == Q1_MAP_LIGHT_RAMP)
            ok = ramp_tick(g, e, error);
        else if (e->map->kind == Q1_MAP_ROPE)
            ok = rope_tick(g, e, error);
        else if (e->map->kind == Q1_MAP_ADDON_BOB || e->map->kind == Q1_MAP_ADDON_ROTATE)
            ok = q1_map_addon_brush_frame(g, e, error);
        else
            ok = q1_map_fail(error, "Invalid Q1 authored frame continuation");
    }
    qa_builtin_snapshot_release(list);
    return ok;
}
bool q1_map_addon_visual_think(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    if (e->map->kind != Q1_MAP_LIGHT_RAMP)
        return q1_map_fail(error, "Invalid Q1 light initialization");
    if (!q1_map_text(g, e->target) || !q1_map_text(g, e->targetname))
        return q1_map_fail(error, "Q1 light ramp requires target and targetname");
    qa_actor_id owner;
    if (!qa_targets_first(g->maps->options.targets, e->target, &owner))
        return q1_map_fail(error, "Q1 light ramp has an unmatched target");
    double flags = 0;
    qa_targets_number(g->maps->options.targets, owner, qa_targets_field_keys(g->maps->options.targets)[QA_TARGET_KEY_SPAWNFLAGS], &flags);
    e = visual(g, id);
    if (!e)
        return true;
    if (!isfinite(flags) || flags < 0 || flags > UINT32_MAX || floor(flags) != flags)
        return q1_map_fail(error, "Invalid Q1 light owner flags");
    bool off = ((uint32_t)flags & 1) != 0;
    e->owner = q1_ref_from(g, owner);
    e->map->pending.addon.phase = off ? 0 : 2;
    e->map->counter_value = off ? 0 : 1;
    e->map->use_enabled = true;
    q1_map_cancel(g, e);
    return true;
}
bool q1_map_addon_visual_use(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (e->map->kind != Q1_MAP_LIGHT_RAMP)
        return true;
    uint8_t phase = e->map->pending.addon.phase;
    e->map->pending.addon.phase = phase == 3 || phase == 0 ? 1 : 3;
    return (phase != 0 && phase != 2) || q1_map_frame_tick_add(g, e->id, error);
}
bool q1_map_addon_light_spawn(qa_q1_game *g, q1_actor *e, bool *handled, qa_error *error) {
    *handled = false;
    qa_actor_id id = e->id;
    bool light = q1_classnamed(g, id, g->runtime_names[Q1_NAME_LIGHT]), spark = q1_classnamed(g, id, g->runtime_names[Q1_NAME_LIGHT_FLUOROSPARK]);
    bool flame = q1_classnamed(g, id, g->runtime_names[Q1_NAME_LIGHT_TORCH_SMALL_WALLTORCH]) ||
                 q1_classnamed(g, id, g->runtime_names[Q1_NAME_LIGHT_FLAME_LARGE_YELLOW]) ||
                 q1_classnamed(g, id, g->runtime_names[Q1_NAME_LIGHT_FLAME_SMALL_YELLOW]) ||
                 q1_classnamed(g, id, g->runtime_names[Q1_NAME_LIGHT_FLAME_SMALL_WHITE]);
    if (!light && !spark && !flame)
        return true;
    *handled = true;
    if (light) {
        if (!q1_map_text(g, e->targetname))
            return q1_remove(g, e, error);
        if (e->map->style < 32)
            return true;
        if (e->spawnflags & 2) {
            if (!g->maps->options.lightstyle(g->maps->options.context, e->map->style,
                                              e->targetname, error))
                return false;
            e = visual(g, id);
            return !e || q1_remove(g, e, error);
        }
        e->map->use_enabled = true;
        return q1_map_lightstyle(g, e, e->spawnflags & 1 ? g->runtime_names[Q1_NAME_RESOURCE_A] : g->runtime_names[Q1_NAME_RESOURCE_M], error);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    e = visual(g, id);
    if (!e)
        return true;
    if (spark)
        return q1_map_ambient(g, body.origin, g->runtime_names[Q1_NAME_RESOURCE_AMBIENCE_BUZZ1_WAV], .5f, error);
    qa_string_id model = q1_classnamed(g, id, g->runtime_names[Q1_NAME_LIGHT_TORCH_SMALL_WALLTORCH])
        ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_FLAME_MDL] : g->runtime_names[Q1_NAME_RESOURCE_PROGS_FLAME2_MDL];
    if (!q1_model(g, e, model, error))
        return false;
    e = visual(g, id);
    if (!e)
        return true;
    if (!qa_q1_wire_declare_model(g, qa_strings_cstr(qa_session_strings(g->services.session),model), error)) return false;
    e = visual(g, id);
    if (!e) return true;
    if (q1_classnamed(g, id, g->runtime_names[Q1_NAME_LIGHT_FLAME_LARGE_YELLOW])) e->frame = 1;
    if (e->spawnflags & 4) {
        if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
        if (!visual(g, id)) return true;
        body.angles = qa_v3(180, 0, 0);
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
    }
    if (!visual(g, id))
        return true;
    if (!qa_q1_wire_declare_sound(g, "ambience/fire1.wav", error)) return false;
    if (!visual(g, id)) return true;
    if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
    if (!visual(g, id)) return true;
    if (!q1_map_ambient(g, body.origin, g->runtime_names[Q1_NAME_RESOURCE_AMBIENCE_FIRE1_WAV], .5f, error))
        return false;
    e = visual(g, id);
    return !e || q1_map_make_static(g, e, error);
}
bool q1_map_addon_visual_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_kind kind = e->map->kind;
    if (kind == Q1_MAP_DYNAMIC_LIGHT)
        return !(g->options.coop && (e->spawnflags & 1)) || q1_remove(g, e, error);
    if (kind == Q1_MAP_LIGHT_RAMP) {
        e->delay = 1 / (e->delay != 0 ? e->delay : 1);
        if (!isfinite(e->delay))
            return q1_map_fail(error, "Q1 light ramp period is too small");
        return q1_map_schedule(g, e, .1, Q1_MAP_LIGHT_RAMP_INIT, error);
    }
    if (kind == Q1_MAP_CANDLE) {
        if (!qa_q1_wire_declare_model(g, "progs/candle.mdl", error)) return false;
        e = visual(g, id);
        if (!e) return true;
    }
    if (!q1_model(g, e, kind == Q1_MAP_ROPE ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_ROPEX_MDL]
                             : kind == Q1_MAP_CANDLE ? g->runtime_names[Q1_NAME_RESOURCE_PROGS_CANDLE_MDL] : g->runtime_names[Q1_NAME_RESOURCE_PROGS_FLAME3_MDL], error))
        return false;
    e = visual(g, id);
    if (!e)
        return true;
    if (kind == Q1_MAP_CANDLE)
        return q1_map_make_static(g, e, error);
    if (kind == Q1_MAP_ROPE) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        e = visual(g, id);
        if (!e)
            return true;
        e->map->pending.addon.origin = body.origin;
        e->count = 0;
        e->physics.motion = QA_PHYSICS_STATIONARY;
        e->physics.solid = QA_PHYSICS_NOT_SOLID;
        body.bounds = (qa_bounds){{-4, -4, 0}, {4, 4, 32}};
        if (!q1_map_damageable(g, e, false, error))
            return false;
        if (!visual(g, id))
            return true;
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        e = visual(g, id);
        if (!e)
            return true;
        if (!q1_link(g, e, error))
            return false;
        return !visual(g, id) || q1_map_frame_tick_add(g, id, error);
    }
    e->alpha = .6f;
    q1_actor *child;
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_GAS_FLAME], Q1_MAP, (qa_actor_id){0}, &child, error))
        return false;
    qa_actor_id child_id = child->id;
    e = visual(g, id);
    if (!e || !q1_entity(g, child_id))
        goto retired;
    if (!q1_map_allocate(g, child, error))
        goto failure;
    child->map->kind = Q1_MAP_GAS_SEGMENT;
    child->model = e->model;
    child->frame = 1;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        goto failure;
    e = visual(g, id);
    child = visual(g, child_id);
    if (!e || !child)
        goto retired;
    qa_body_state second;
    if (!qa_world_body_read(g->services.world, child_id, &second, error))
        goto failure;
    if (!visual(g, id) || !visual(g, child_id))
        goto retired;
    second.origin = body.origin;
    if (!qa_world_body_write(g->services.world, child_id, &second, error))
        goto failure;
    child = visual(g, child_id);
    if (!visual(g, id) || !child)
        goto retired;
    if (!q1_link(g, child, error))
        goto failure;
    child = visual(g, child_id);
    if (!visual(g, id) || !child)
        goto retired;
    child->alpha = .4f;
    if (!q1_link(g, child, error))
        goto failure;
    if (!visual(g, id) || !visual(g, child_id))
        goto retired;
    return true;
failure:
    if (qa_actors_get(qa_session_actors(g->services.session), child_id))
        qa_session_release(g->services.session, child_id, NULL);
    return false;
retired:
    if (qa_actors_get(qa_session_actors(g->services.session), child_id))
        return qa_session_release(g->services.session, child_id, error);
    return true;
}
