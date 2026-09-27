#include "internal.h"

bool q2_event(q2_weapon_call *c, qa_builtin_event_kind kind, int code, qa_vec3 origin, qa_vec3 end,
              qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    qa_builtin_event event = {.kind = kind,
                              .family = QA_GAME_Q2,
                              .provider = c->game->options.owner,
                              .actor = c->actor->id,
                              .time_ns = c->now_ns,
                              .origin = origin,
                              .end = end,
                              .code = code,
                              .flags = c->silenced ? 128u : 0u};
    return qa_builtin_emit(&c->game->services, &event, e);
}
bool q2_sound(q2_weapon_call *c, const char *path, int channel, float attenuation, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                              .family = QA_GAME_Q2,
                              .provider = c->game->options.owner,
                              .actor = c->actor->id,
                              .time_ns = c->now_ns,
                              .channel = channel,
                              .volume = 1,
                              .attenuation = attenuation};
    qa_body_state body;
    if (!qa_world_body_read(c->game->services.world, c->actor->id, &body, e) ||
        !qa_builtin_resource(&c->game->services, path, &event.resource, e))
        return false;
    event.origin = body.origin;
    return qa_builtin_emit(&c->game->services, &event, e);
}
bool q2_loop(q2_weapon_call *c, const char *path, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    qa_string_id resource = 0;
    if (*path != 0 && !qa_builtin_resource(&c->game->services, path, &resource, e))
        return false;
    if (c->state->loop_sound == resource)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(c->game->services.world, c->actor->id, &body, e))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_STOP_SOUND,
                              .family = QA_GAME_Q2,
                              .provider = c->game->options.owner,
                              .actor = c->actor->id,
                              .time_ns = c->now_ns,
                              .channel = 1,
                              .volume = 1,
                              .attenuation = 1,
                              .origin = body.origin};
    event.resource = c->state->loop_sound;
    c->state->loop_sound = resource;
    if (event.resource != 0) {
        if (!qa_builtin_emit(&c->game->services, &event, e))
            return false;
    }
    if (resource == 0 || !q2_actor_live(c->game, c->actor->id) || c->state->loop_sound != resource)
        return true;
    event.kind = QA_BUILTIN_SOUND;
    event.resource = resource;
    event.flags = 1;
    return qa_builtin_emit(&c->game->services, &event, e);
}
bool q2_noise(q2_weapon_call *c, qa_vec3 origin, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    if (c->rerelease) {
        uint64_t until = q2_deadline(c->now_ns, c->actor->silencer > 0 ? 400 * Q2_MS : 2 * Q2_NS);
        if (!q2_player_invisibility_reveal(c->game, c->actor->id, until, e) ||
            (c->game->hooks.invisibility_reveal != NULL &&
             !c->game->hooks.invisibility_reveal(c->game->hooks.context, c->actor->id, until, e)))
            return false;
        if (!q2_actor_live(c->game, c->actor->id))
            return true;
    }
    if (c->actor->silencer > 0) {
        --c->actor->silencer;
        return true;
    }
    if (c->game->options.deathmatch || c->input.notarget)
        return true;
    return q2_noise_for_actor(c->game, c->actor->id, origin, false, e);
}
bool q2_animation(q2_weapon_call *c, int priority, int first, int last, qa_error *e) {
    if (!c->input.animate_player || !q2_actor_live(c->game, c->actor->id))
        return true;
    qa_builtin_event event = {.kind = QA_BUILTIN_ANIMATION,
                              .family = QA_GAME_Q2,
                              .provider = c->game->options.owner,
                              .actor = c->actor->id,
                              .time_ns = c->now_ns,
                              .code = priority,
                              .frame = first,
                              .count = last,
                              .flags = c->rerelease ? 1u : 0u};
    return qa_builtin_emit(&c->game->services, &event, e);
}
bool q2_attack_animation(q2_weapon_call *c, int offset, qa_error *e) {
    return q2_animation(c, 0, (c->input.ducked ? 160 : 46) - offset, c->input.ducked ? 168 : 53, e);
}
bool q2_reverse_animation(q2_weapon_call *c, qa_error *e) {
    return q2_animation(c, 2, c->input.ducked ? 173 : 66, c->input.ducked ? 169 : 62, e);
}
bool q2_power_sound(q2_weapon_call *c, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    bool handled = false, ctf = c->input.source_rules == QA_Q2_WEAPON_RULES_CTF;
    if (ctf && c->game->hooks.ctf_strength_sound != NULL &&
        !c->game->hooks.ctf_strength_sound(c->game->hooks.context, c->actor->id, &handled, e))
        return false;
    bool quad = c->input.quad_until_ns > c->now_ns, twice = c->input.double_until_ns > c->now_ns;
    const char *path = quad && twice && c->rerelease ? "ctf/tech2x.wav"
                       : quad                        ? "items/damage3.wav"
                       : twice                       ? "misc/ddamage3.wav"
                                                     : NULL;
    if (!handled && path != NULL && !q2_sound(c, path, 3, 1, e))
        return false;
    return !q2_actor_live(c->game, c->actor->id) || !ctf ||
           c->game->hooks.ctf_haste_sound == NULL ||
           c->game->hooks.ctf_haste_sound(c->game->hooks.context, c->actor->id, e);
}
float q2_multiplier(q2_weapon_call *c) {
    bool quad = c->input.quad_until_ns > c->now_ns;
    float result =
        quad ? (c->game->hooks.quad_multiplier == NULL
                    ? 4
                    : c->game->hooks.quad_multiplier(c->game->hooks.context, c->actor->id))
             : 1;
    if (c->input.double_until_ns > c->now_ns && !(quad && c->input.no_stack_double))
        result *= 2;
    if (c->game->hooks.damage_multiplier != NULL)
        result *= c->game->hooks.damage_multiplier(c->game->hooks.context, c->actor->id);
    return result;
}
void q2_kick(q2_weapon_call *c, qa_vec3 origin, qa_vec3 angles, float seconds) {
    qa_q2_weapon_state *s = c->state;
    s->kick_origin = origin;
    s->kick_angles = angles;
    s->kick_ns = c->now_ns;
    s->kick_seconds = seconds;
    s->kick_until_ns = q2_deadline(c->now_ns, q2_duration(seconds));
}
void q2_recoil(q2_weapon_call *c, qa_vec3 *origin, qa_vec3 *angles) {
    qa_q2_weapon_state *s = c->state;
    float impulse = c->now_ns == s->kick_ns ? 1 : 0;
    float factor =
        s->kick_seconds == 0 ? impulse
        : c->now_ns >= s->kick_until_ns
            ? 0
            : fminf(1, (float)((double)(s->kick_until_ns - c->now_ns) / 1e9) / s->kick_seconds);
    *origin = qa_vec_scale(s->kick_origin, c->rerelease ? factor : impulse);
    *angles = qa_vec_scale(s->kick_angles, factor);
}
bool q2_present(q2_weapon_call *c, qa_error *e) {
    if (c->game->hooks.weapon_view == NULL || !q2_actor_live(c->game, c->actor->id))
        return true;
    qa_q2_weapon_state *s = c->state;
    const qa_q2_weapon_definition *d = qa_q2_weapon_definition_at(c->game, s->weapon);
    qa_q2_weapon_presentation view = {.actor = c->actor->id,
                                      .weapon = s->weapon,
                                      .frame = s->frame,
                                      .skin = s->view_skin,
                                      .rate = s->gun_rate,
                                      .player_model = d == NULL ? 0 : d->player_model,
                                      .model = s->handoff == QA_Q2_PRIMARY_HOLSTERED ? 0
                                               : s->view_model != 0
                                                   ? s->view_model
                                                   : c->game->view_models[s->weapon]};
    q2_recoil(c, &view.kick_origin, &view.kick_angles);
    return c->game->hooks.weapon_view(c->game->hooks.context, &view, e);
}
bool q2_project(q2_weapon_call *c, qa_vec3 angles, qa_vec3 offset, qa_vec3 *start,
                qa_vec3 *direction, qa_error *e) {
    qa_vec3 f, r, u;
    qa_builtin_angle_vectors(angles, &f, &r, &u);
    qa_body_state body;
    if (!qa_world_body_read(c->game->services.world, c->actor->id, &body, e))
        return false;
    float side = c->input.hand == QA_Q2_LEFT_HAND     ? -offset.y
                 : c->input.hand == QA_Q2_CENTER_HAND ? 0
                                                      : offset.y;
    qa_vec3 eye = qa_vec_add(body.origin, qa_v3(0, 0, c->input.view_height));
    *start = qa_vec_add(qa_vec_add(eye, qa_vec_scale(f, offset.x)), qa_vec_scale(r, side));
    *start = qa_vec_add(*start, c->rerelease ? qa_vec_scale(u, offset.z) : qa_v3(0, 0, offset.z));
    *direction = f;
    if (!c->rerelease)
        return true;
    qa_trace_query query = {
        .start = eye, .end = qa_vec_add(eye, qa_vec_scale(f, 8192)), .pass_actor = c->actor->id};
    query.policy = qa_collision_default_policy(QA_COLLISION_Q2);
    query.policy.contents_mask =
        (c->input.players_collide ? Q2_PROJECTILE_MASK : Q2_PROJECTILE_MASK & ~Q2_PLAYER_CONTENTS) &
        ~UINT32_C(0x04000000);
    qa_trace_result trace;
    if (!qa_world_trace(c->game->services.world, &query, &trace, e))
        return false;
    bool close = ((uint32_t)trace.contents & (UINT32_C(0x02000000) | Q2_PLAYER_CONTENTS)) != 0 &&
                 trace.fraction * 8192 < 128;
    if (!trace.start_solid && !close)
        *direction = qa_vec_normalize(qa_vec_sub(trace.end, *start));
    return true;
}
