#include "internal.h"

static bool collision(q2_save_io *io, qa_actor_collision *s) {
    Q2U(family); Q2U(shape); Q2B(inline_model); Q2U(model); Q2I(contents); Q2U(role);
    Q2B(monster); Q2B(dead_monster); Q2B(q1_corpse); Q2B(has_q3_owner);
    Q2I(q3_entity_number); Q2I(q3_owner_number);
    return q2_save_actor_pointer(io, &s->owner);
}
static bool mover(q2_save_io *io, q2_mover *s) {
    Q2V(start); Q2V(end); Q2V(intermediate); Q2V(safe_direction);
    Q2F(distance); Q2F(water_divisor); Q2I(phase); Q2I(stage);
    Q2B(angular); Q2B(reversed); Q2B(activated); Q2B(ship); Q2B(moving);
    Q2V(motion.direction); Q2V(motion.destination); Q2V(motion.reference);
    Q2F(motion.remaining); Q2F(motion.current_speed); Q2F(motion.move_speed);
    Q2F(motion.next_speed); Q2F(motion.decel_distance); Q2F(motion.curve_from);
    Q2F(motion.curve_to); Q2F(motion.curve_distance);
    Q2U(motion.curve_frame); Q2U8(motion.curve_subframe); Q2U8(motion.curve_subframes);
    Q2T(motion.curve_frames_done);
    Q2U(motion.done); Q2B(motion.angular); Q2B(motion.accelerated);
    Q2B(motion.curve); Q2B(motion.final_sample); return true;
}
static bool turret(q2_save_io *io, q2_turret *s) {
    Q2V(goal); Q2V(muzzle); Q2F(pitch_min); Q2F(pitch_max); Q2F(yaw_min); Q2F(yaw_max);
    Q2F(radius); Q2F(yaw_offset); Q2F(height); return true;
}
static bool q64(q2_save_io *io, q2_q64 *s) {
    Q2V(neutral); Q2V(eye_position); Q2V(angles); Q2F(vision_cone); Q2F(remaining);
    Q2F(distance); Q2F(speed); Q2F(fade_remaining); Q2F(fade_duration);
    Q2U(hackflags); Q2B(fading); return true;
}
static bool optional(q2_save_io *io, void **value, size_t size) {
    bool present = *value != NULL;
    if (!q2_save_bool(io, &present)) return false;
    if (io->reading && present) {
        *value = calloc(1, size);
        if (!*value) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Allocating Q2 entity continuation");
            return false;
        }
    }
    return true;
}
static bool state(q2_save_io *io, qa_q2_entity_state *s) {
    Q2U(kind); Q2U(think); Q2N(classname); Q2N(targetname); Q2N(target); Q2N(killtarget);
    Q2N(message); Q2N(team); Q2N(map); Q2N(noise); Q2N(loop_sound);
    void *fields = s->fields;
    if (!q2_save_count(io, &s->field_count, 8, sizeof(*s->fields), &fields)) return false;
    s->fields = fields;
    for (size_t i = 0; i < s->field_count; ++i) { Q2N(fields[i].key); Q2N(fields[i].value); }
    Q2U(ordinal); Q2U(spawnflags);
    if (!q2_save_visual(io, &s->visual) || !collision(io, &s->collision) ||
        !q2_save_actor_pointer(io, &s->team_master) ||
        !q2_save_actor_pointer(io, &s->team_next)) return false;
    Q2V(direction); Q2V(beam_end); Q2V(multicast_origin);
    Q2F(speed); Q2F(accel); Q2F(decel); Q2F(wait);
    Q2F(delay); Q2F(damage); Q2F(health); Q2F(random); Q2F(volume); Q2F(attenuation);
    Q2T(due_ns); Q2T(timestamp_ns); Q2T(debounce_ns); Q2T(sound_ns); Q2T(expires_ns);
    Q2T(air_ns); Q2T(pain_ns); Q2T(environment_ns);
    Q2I(count); Q2I(style); Q2I(stage); Q2B(usable); Q2B(touchable); Q2B(active);
    Q2B(dispatching); Q2B(has_inline); Q2B(dirty);
    void *extension = s->mover;
    if (!optional(io, &extension, sizeof(*s->mover))) return false;
    s->mover = extension;
    if (s->mover && !mover(io, s->mover)) return false;
    extension = s->turret;
    if (!optional(io, &extension, sizeof(*s->turret))) return false;
    s->turret = extension;
    if (s->turret && !turret(io, s->turret)) return false;
    extension = s->q64;
    if (!optional(io, &extension, sizeof(*s->q64))) return false;
    s->q64 = extension;
    if (s->q64 && !q64(io, s->q64)) return false;
    Q2I(animation_first); Q2I(animation_end); Q2I(clock_value); Q2U(scenery);
    extension = s->trail;
    if (!optional(io, &extension, sizeof(*s->trail))) return false;
    s->trail = extension;
    return !s->trail || (q2_save_actor_pointer(io, &s->trail->owner) &&
        q2_save_actor_pointer(io, &s->trail->older) &&
        q2_save_actor_pointer(io, &s->trail->newer));
}
bool q2_save_entity(q2_save_io *io, qa_q2_entity_checkpoint *s) {
    Q2B(present);
    if (s->present && !state(io, &s->value)) return false;
    Q2R(activator); Q2R(owner); Q2R(enemy); Q2R(goal);
    Q2R(destination); Q2R(turret_breach); return true;
}
static bool campaign_level(q2_save_io *io, qa_q2_campaign_level *s) {
    Q2N(map); Q2N(name); Q2U(visit_order); Q2U(total_secrets); Q2U(found_secrets);
    Q2U(total_monsters); Q2U(killed_monsters); Q2S(f64, time_seconds);
    return true;
}
bool q2_save_entities(q2_save_io *io, qa_q2_entities_checkpoint *s) {
    Q2R(poi); Q2R(poi_dynamic); Q2N(poi_image); Q2N(story);
    Q2I(poi_stage); Q2I(steam_id); Q2I(total_secrets); Q2I(found_secrets);
    Q2I(total_goals); Q2I(found_goals); Q2T(last_autosave_ns);
    for (size_t i = 0; i < 2; ++i) {
        Q2R(bars[i].controller); Q2R(bars[i].target); Q2T(bars[i].dead_until_ns); Q2B(bars[i].dying);
    }
    if (!q2_save_fog(io, &s->world_fog)) return false;
    Q2N(sky); Q2N(goals);
    if (!io->level_only || io->game->options.edition != QA_Q2_RERELEASE) {
        Q2N(primary); Q2N(secondary);
    }
    Q2V(sky_axis); Q2F(sky_rotation);
    if (!io->level_only || io->game->options.edition != QA_Q2_RERELEASE) {
        Q2U(primary_changes); Q2U(secondary_changes);
    }
    Q2U(goal_number); Q2B(sky_auto); Q2B(has_goals);
    void *wind = s->wind;
    if (!q2_save_count(io, &s->wind_count, 9, sizeof(*s->wind), &wind)) return false;
    s->wind = wind;
    for (size_t i = 0; i < s->wind_count; ++i) { Q2R(wind[i].actor); Q2T(wind[i].until_ns); }
    Q2U(total_monsters); Q2U(killed_monsters);
    if (io->level_only) return true;
    Q2U(level_count);
    if (s->level_count > QA_Q2_CAMPAIGN_LEVEL_LIMIT) return q2_save_fail(io, "Too many Q2 campaign levels");
    for (uint32_t i = 0; i < s->level_count; ++i)
        if (!campaign_level(io, s->levels + i)) return false;
    void *visited = s->visited_maps;
    if (!q2_save_count(io, &s->visited_count, 4, sizeof(*s->visited_maps), &visited)) return false;
    s->visited_maps = visited;
    for (size_t i = 0; i < s->visited_count; ++i) Q2N(visited_maps[i]);
    return true;
}
