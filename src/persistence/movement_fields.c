#include "qa/persistence_fields.h"
#include <limits.h>
#include <stdlib.h>

static bool fail(qa_source_save_io *io, const char *text)
{ io->failed = true; qa_error_set(io->error, QA_ERROR_FORMAT, 0, "%s", text); return false; }

#define FIELD(method, value) do { if (!qa_source_save_##method(io, &(value))) return false; } while (0)
#define V(value) FIELD(vec3, value)
#define F(value) FIELD(f32, value)
#define I(value) FIELD(i32, value)
#define U(value) FIELD(u32, value)
#define B(value) FIELD(bool, value)
#define A(value) FIELD(actor, value)

static bool short_field(qa_source_save_io *io, int16_t *value)
{
    int32_t word = *value;
    if (!qa_source_save_i32(io, &word) || word < INT16_MIN || word > INT16_MAX)
        return fail(io, "Movement short field is outside source range");
    *value = (int16_t)word; return true;
}

bool qa_persistence_bounds(qa_source_save_io *io, qa_bounds *value)
{ return value && qa_source_save_vec3(io, &value->mins) && qa_source_save_vec3(io, &value->maxs); }

bool qa_persistence_body(qa_source_save_io *io, qa_body_state *value)
{
    return value && qa_source_save_vec3(io, &value->origin) && qa_source_save_vec3(io, &value->angles) &&
        qa_source_save_vec3(io, &value->velocity) && qa_persistence_bounds(io, &value->bounds) &&
        qa_source_save_actor(io, &value->ground);
}

bool qa_persistence_ground(qa_source_save_io *io, qa_movement_ground *value)
{
    if (!value) return false;
    uint32_t hit = value->hit;
    if (!qa_source_save_u32(io, &hit) || hit > QA_TRACE_HIT_ACTOR) return fail(io, "Invalid saved movement ground kind");
    value->hit = (qa_trace_hit)hit;
    return qa_source_save_actor(io, &value->actor) && qa_source_save_u32(io, &value->model);
}

bool qa_persistence_movement(qa_source_save_io *io, qa_movement_state *state)
{
    if (!state) return false;
    uint32_t kind = state->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_MOVEMENT_Q3) return fail(io, "Invalid saved movement family");
    state->kind = (qa_movement_kind)kind;
    switch (state->kind) {
    case QA_MOVEMENT_NETQUAKE: {
        qa_nq_movement_state *s = &state->data.nq;
        V(s->origin); V(s->velocity); V(s->angles); V(s->old_origin); V(s->angular_velocity);
        V(s->view_angles); V(s->punch_angles); V(s->water_jump_direction);
        I(s->move_type); F(s->health); U(s->flags);
        if (!qa_persistence_ground(io, &s->ground)) return false;
        I(s->water_level); I(s->water_type); FIELD(f64, s->teleport_time_seconds); F(s->ideal_pitch); B(s->fix_angle);
        return true;
    }
    case QA_MOVEMENT_QUAKEWORLD: {
        qa_qw_movement_state *s = &state->data.qw;
        V(s->origin); V(s->velocity); V(s->angles); U(s->old_buttons); F(s->water_jump_time_seconds);
        B(s->dead); I(s->spectator); return qa_persistence_ground(io, &s->ground);
    }
    case QA_MOVEMENT_Q2_CLASSIC: {
        qa_q2_movement_state *s = &state->data.q2;
        I(s->type);
        for (size_t i = 0; i < 3; ++i)
            if (!short_field(io, &s->origin_eighths[i]) || !short_field(io, &s->velocity_eighths[i])) return false;
        U(s->flags); FIELD(u8, s->time_eight_ms);
        if (!short_field(io, &s->gravity)) return false;
        for (size_t i = 0; i < 3; ++i) if (!short_field(io, &s->delta_angle_shorts[i])) return false;
        return true;
    }
    case QA_MOVEMENT_Q2_RERELEASE: {
        qa_q2r_movement_state *s = &state->data.q2r;
        I(s->type); V(s->origin); V(s->velocity); U(s->flags); U(s->time_ms);
        if (!short_field(io, &s->gravity)) return false;
        V(s->delta_angles); F(s->view_height); return true;
    }
    case QA_MOVEMENT_Q3: {
        qa_q3_movement_state *s = &state->data.q3;
        I(s->command_time_ms); I(s->movement_type); I(s->bob_cycle); U(s->movement_flags); I(s->movement_time_ms);
        V(s->origin); V(s->velocity); I(s->gravity); I(s->speed);
        for (size_t i = 0; i < 3; ++i) I(s->delta_angle_words[i]);
        I(s->movement_direction); V(s->grapple_point); U(s->flags); V(s->view_angles); F(s->view_height);
        if (!qa_persistence_ground(io, &s->ground)) return false;
        U(s->event_sequence); A(s->jump_pad); I(s->movement_frame); I(s->jump_pad_frame); return true;
    }
    }
    return fail(io, "Invalid movement state");
}

static bool parameters(qa_source_save_io *io, qa_q1_movement_parameters *p)
{
    F(p->gravity); F(p->stop_speed); F(p->max_speed); F(p->spectator_max_speed);
    F(p->accelerate); F(p->air_accelerate); F(p->water_accelerate); F(p->friction); F(p->water_friction); F(p->entity_gravity);
    return true;
}

bool qa_persistence_movement_profile(qa_source_save_io *io, qa_movement_profile *profile)
{
    if (!profile) return false;
    uint32_t kind = profile->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_MOVEMENT_Q3) return fail(io, "Invalid movement profile family");
    profile->kind = (qa_movement_kind)kind;
    switch (profile->kind) {
    case QA_MOVEMENT_NETQUAKE: {
        uint32_t edition = profile->data.nq.edition;
        if (!parameters(io, &profile->data.nq.parameters) || !qa_source_save_u32(io, &edition) || edition > QA_Q1_QUAKE64)
            return fail(io, "Invalid saved NetQuake profile");
        profile->data.nq.edition = (qa_q1_edition)edition;
        F(profile->data.nq.edge_friction); F(profile->data.nq.max_velocity); F(profile->data.nq.ideal_pitch_scale);
        F(profile->data.nq.roll_speed); F(profile->data.nq.roll_angle); B(profile->data.nq.no_clip_angle_hack);
        B(profile->data.nq.no_step); B(profile->data.nq.source_jump_authority); B(profile->data.nq.preserve_fixangle_roll);
        return true;
    }
    case QA_MOVEMENT_QUAKEWORLD:
        if (!parameters(io, &profile->data.qw.parameters)) return false;
        U(profile->data.qw.maximum_command_ms); B(profile->data.qw.shared_controls); return true;
    case QA_MOVEMENT_Q2_CLASSIC:
        F(profile->data.q2.air_accelerate); B(profile->data.q2.snap_initial); B(profile->data.q2.strafejump_hack); return true;
    case QA_MOVEMENT_Q2_RERELEASE:
        F(profile->data.q2r.air_accelerate); B(profile->data.q2r.n64_physics); return true;
    case QA_MOVEMENT_Q3:
        B(profile->data.q3.missionpack); B(profile->data.q3.no_footsteps); U(profile->data.q3.fixed_ms); return true;
    }
    return fail(io, "Invalid movement profile");
}

static bool plane(qa_source_save_io *io, qa_collision_plane *value)
{ V(value->normal); F(value->distance); I(value->type); FIELD(u8, value->signbits); return true; }

static bool surface(qa_source_save_io *io, qa_collision_surface *value)
{
    if (!qa_source_save_bytes(io, value->name, sizeof(value->name))) return false;
    I(value->flags); I(value->value);
    return qa_source_save_bytes(io, value->material, sizeof(value->material));
}

static bool trace(qa_source_save_io *io, qa_trace_result *value)
{
    uint32_t family = value->family, hit = value->hit;
    if (!qa_source_save_u32(io, &family) || family < QA_COLLISION_Q1 || family > QA_COLLISION_Q3)
        return fail(io, "Invalid movement contact family");
    value->family = (qa_collision_family)family;
    F(value->fraction); V(value->end); B(value->start_solid); B(value->all_solid); B(value->in_open); B(value->in_water); B(value->contact);
    if (!plane(io, &value->plane) || !plane(io, &value->contact_plane) || !qa_source_save_u32(io, &hit) || hit > QA_TRACE_HIT_ACTOR)
        return fail(io, "Invalid movement contact hit");
    value->hit = (qa_trace_hit)hit; U(value->model); A(value->actor); I(value->contents); I(value->surface_flags);
    B(value->has_surface); B(value->has_secondary);
    if (value->has_surface && !surface(io, &value->surface)) return false;
    if (value->has_secondary) {
        if (!plane(io, &value->secondary_plane)) return false;
        B(value->secondary_has_surface);
        if (value->secondary_has_surface && !surface(io, &value->secondary_surface)) return false;
    }
    return true;
}

bool qa_persistence_movement_result(qa_source_save_io *io, qa_movement_result *result)
{
    if (!result) return false;
    uint32_t status = result->status;
    if (!qa_source_save_u32(io, &status) || status > QA_MOVEMENT_ACTOR_REMOVED) return fail(io, "Invalid movement result status");
    result->status = (qa_movement_status)status;
    A(result->actor); FIELD(u64, result->command_sequence);
    if (!qa_persistence_movement(io, &result->state) || !qa_persistence_bounds(io, &result->bounds)) return false;
    V(result->view_angles); V(result->view_offset); F(result->view_height); F(result->horizontal_speed);
    if (!qa_persistence_ground(io, &result->ground)) return false;
    I(result->water_level); I(result->water_type);
    if (!qa_source_save_count(io, &result->contact_count, SIZE_MAX / sizeof(*result->contacts))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (result->contacts) return fail(io, "Movement result decode requires an empty owned contact array");
        result->contacts = result->contact_count ? calloc(result->contact_count, sizeof(*result->contacts)) : NULL;
        if (result->contact_count && !result->contacts) {
            io->failed = true; qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Allocating saved movement contacts"); return false;
        }
        result->contact_capacity = result->contact_count;
    } else if (result->contact_count > result->contact_capacity || (result->contact_count && !result->contacts))
        return fail(io, "Movement contact storage extent is invalid");
    for (size_t i = 0; i < result->contact_count; ++i) {
        if (!trace(io, &result->contacts[i].trace)) return false;
        U(result->contacts[i].substep);
    }
    FIELD(u64, result->effect_count);
    for (size_t i = 0; i < 4; ++i) F(result->screen_blend[i]);
    F(result->impact_delta); U(result->render_flags); B(result->jump_sound); B(result->step_clip); return true;
}

#undef A
#undef B
#undef U
#undef I
#undef F
#undef V
#undef FIELD
