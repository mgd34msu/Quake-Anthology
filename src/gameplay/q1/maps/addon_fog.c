#include "internal.h"
#include <float.h>

bool qa_q1_game_map_fog_read(const qa_q1_game *g, qa_actor_id player, qa_q1_fog_state *out) {
    if (!g || !out || !g->maps || !g->maps->addon_contacts || player.slot >= g->capacity ||
        g->destroy_pending || !qa_actors_get(qa_session_actors(g->services.session), player))
        return false;
    const q1_addon_contact *row = &g->maps->addon_contacts[player.slot];
    if (!qa_actor_id_equal(row->actor, player))
        return false;
    *out = (qa_q1_fog_state){q1_ref_actor(g, row->fog_active), row->fog_density, row->fog_color};
    return true;
}
static q1_actor *fog_actor(qa_q1_game *g, qa_actor_id id) {
    q1_actor *e = q1_entity(g, id);
    return e && e->map && q1_map_is_fog(e->map->kind) ? e : NULL;
}
typedef struct fog_value {
    float density;
    qa_vec3 color;
    uint32_t flags;
} fog_value;
static bool fog_round(double value, float *out, qa_error *error) {
    if (!isfinite(value) || fabs(value) >= 0x1.ffffffp127) {
        q1_map_fail(error, "Q1 addon fog result exceeds native range");
        return false;
    }
    *out = fabs(value) > FLT_MAX ? (value < 0 ? -FLT_MAX : FLT_MAX) : (float)value;
    return true;
}
static bool fog_blend(float first, float second, double tween, float *out,
                        qa_error *error) {
    float left, right;
    return fog_round((double)first * (1 - tween), &left, error) &&
           fog_round((double)second * tween, &right, error) &&
           fog_round((double)left + right, out, error);
}
static bool fog_info(qa_q1_game *g, qa_string_id name, fog_value *out) {
    *out = (fog_value){0};
    qa_actor_id id;
    if (!q1_map_text(g, name) || !qa_targets_first(g->maps->options.targets, name, &id))
        return false;
    qa_authored_target fields;
    if (!qa_targets_read(g->maps->options.targets, id, &fields))
        return false;
    const char *classname = qa_strings_cstr(qa_session_strings(g->services.session), fields.classname);
    if (!classname || strcmp(classname, "info_fog"))
        return false;
    q1_actor *e = fog_actor(g, id);
    if (e && e->map->kind == Q1_MAP_FOG_INFO) {
        *out = (fog_value){e->map->fog_density, e->map->fog_color, e->spawnflags};
        return true;
    }
    double density = 0, flags = 0;
    (void)qa_targets_number(g->maps->options.targets, id, qa_targets_field_keys(g->maps->options.targets)[QA_TARGET_KEY_FOG_DENSITY], &density);
    (void)qa_targets_number(g->maps->options.targets, id, qa_targets_field_keys(g->maps->options.targets)[QA_TARGET_KEY_SPAWNFLAGS], &flags);
    (void)qa_targets_vector(g->maps->options.targets, id, qa_targets_field_keys(g->maps->options.targets)[QA_TARGET_KEY_FOG_COLOR], &out->color);
    out->density = (float)density;
    out->flags = flags >= 0 && flags <= UINT32_MAX ? (uint32_t)flags : 0;
    return true;
}
static bool set_fog(qa_q1_game *g, qa_actor_id source, qa_actor_id player,
                     fog_value value, float duration, qa_error *error) {
    if (!fog_actor(g, source) || !q1_alive(g, player))
        return true;
    if (!isfinite(value.density) || !qa_vec_finite(value.color) || !isfinite(duration))
        return q1_map_fail(error, "Nonfinite Q1 addon fog result");
    q1_addon_contact *row = q1_map_addon_contact(g, player, true, error);
    if (!row)
        return !q1_alive(g, player);
    row->fog_density = value.density;
    row->fog_color = value.color;
    if (!g->maps->options.fog_player)
        return q1_map_fail(error, "Q1 addon fog requires the selected presentation owner");
    return g->maps->options.fog_player(g->maps->options.context, player, value.density,
                                      value.color, duration, error);
}
bool q1_map_addon_fog_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (e->map->kind == Q1_MAP_FOG_INFO) {
        if (e->map->fog_density == 0)
            e->map->fog_density = .05f;
        else if (e->map->fog_color.x > 1 || e->map->fog_color.y > 1 || e->map->fog_color.z > 1) {
            qa_vec3 color = e->map->fog_color;
            const double scale = 1.0 / 255.0;
            if (!fog_round(color.x * scale, &color.x, error) ||
                !fog_round(color.y * scale, &color.y, error) ||
                !fog_round(color.z * scale, &color.z, error)) return false;
            e->map->fog_color = color;
        }
        return true;
    }
    if (g->options.coop || g->options.deathmatch != 0)
        return q1_remove(g, e, error);
    bool transition = e->map->kind == Q1_MAP_FOG_TRANSITION;
    if (transition && (e->map->style < 0 || e->map->style > 2))
        return q1_map_fail(error, "Invalid style for trigger_fog_transition");
    qa_actor_id id = e->id;
    if (!transition)
        e->delay = e->delay != 0 ? e->delay : .5f;
    if (!q1_map_trigger_init(g, e, true, error))
        return false;
    e = fog_actor(g, id);
    if (!e)
        return true;
    e->map->touch_enabled = transition || !(e->spawnflags & 1u);
    e->map->use_enabled = !transition;
    return q1_link(g, e, error);
}
bool q1_map_addon_fog_activate(qa_q1_game *g, q1_actor *e, qa_actor_id player,
                               qa_error *error) {
    qa_actor_id id = e->id;
    if (!q1_map_player(g, player) || !q1_alive(g, player))
        return true;
    e = fog_actor(g, id);
    if (!e)
        return true;
    if (e->map->kind == Q1_MAP_FOG_TRANSITION) {
        qa_string_id first_name = e->map->fog_info_entity, second_name = e->target;
        int32_t axis = e->map->style;
        qa_body_state body, trigger;
        if (!qa_world_body_read(g->services.world, player, &body, error) ||
            !qa_world_body_read(g->services.world, id, &trigger, error))
            return false;
        if (!fog_actor(g, id) || !q1_alive(g, player))
            return true;
        qa_vec3 size = qa_vec_sub(trigger.bounds.maxs, trigger.bounds.mins);
        qa_vec3 position = qa_vec_sub(body.origin, trigger.bounds.mins);
        float numerator = axis == 0 ? position.x : axis == 1 ? position.y : position.z;
        float denominator = axis == 0 ? size.x : axis == 1 ? size.y : size.z;
        if (denominator == 0 && numerator == 0)
            return q1_map_fail(error, "Nonfinite Q1 addon fog transition position");
        double tween = denominator == 0 ? (signbit(numerator) == signbit(denominator) ? 1 : 0)
                                       : (double)numerator / denominator;
        if (isnan(tween))
            return q1_map_fail(error, "Nonfinite Q1 addon fog transition position");
        tween = fmin(1, fmax(0, tween));
        fog_value first, second;
        (void)fog_info(g, first_name, &first);
        (void)fog_info(g, second_name, &second);
        fog_value value = {0};
        if (!fog_round((1 - tween) * first.density + tween * second.density,
                        &value.density, error) ||
            !fog_blend(first.color.x, second.color.x, tween, &value.color.x, error) ||
            !fog_blend(first.color.y, second.color.y, tween, &value.color.y, error) ||
            !fog_blend(first.color.z, second.color.z, tween, &value.color.z, error)) return false;
        return set_fog(g, id, player, value, 0, error);
    }
    if (e->map->kind != Q1_MAP_FOG_TRIGGER)
        return true;
    q1_addon_contact *row = q1_map_addon_contact(g, player, true, error);
    if (!row)
        return !q1_alive(g, player);
    if (q1_ref_equal(row->fog_active, q1_ref_from(g, id)))
        return true;
    row->fog_active = q1_ref_from(g, id);
    qa_string_id name = e->map->fog_info_entity;
    float delay = e->delay;
    fog_value value;
    if (!fog_info(g, name, &value) ||
        (value.density == 0 && qa_vec_dot(value.color, value.color) == 0 && !(value.flags & 1u)))
        return true;
    return set_fog(g, id, player, value, delay, error);
}
