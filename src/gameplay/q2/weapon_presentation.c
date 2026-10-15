#include "qa/q2_sound.h"
#include "internal.h"

bool q2_event_named(q2_weapon_call *c, qa_builtin_event_kind kind, q2_runtime_name name,
    int code, qa_vec3 origin, qa_vec3 end, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    qa_builtin_event event = {.kind = kind,
                              .family = QA_GAME_Q2,
                              .provider = c->game->options.owner,
                              .actor = c->actor->id,
                              .time_ns = c->now_ns,
                              .origin = origin,
                              .end = end,
                              .direction = kind == QA_BUILTIN_IMPACT ? end : qa_v3(0, 0, 0),
                              .code = code,
                              .flags = c->silenced ? 128u : 0u};
    if (kind==QA_BUILTIN_BEAM) {
        switch (name) {
        case Q2_NAME_RESOURCE_Q2_BUBBLE_TRAIL:
            event.q2_multicast=(qa_builtin_q2_multicast){QA_BUILTIN_Q2_MULTICAST_PVS,
                qa_vec_scale(qa_vec_add(origin,end),.5f)};
            break;
        case Q2_NAME_RESOURCE_Q2_RAIL_WATER:
            event.q2_multicast=(qa_builtin_q2_multicast){QA_BUILTIN_Q2_MULTICAST_PHS,end};
            break;
        case Q2_NAME_RESOURCE_Q2_RAIL:
            if (c->rules == QA_RULESET_Q2_RERELEASE) {
                event.q2_multicast=(qa_builtin_q2_multicast){QA_BUILTIN_Q2_MULTICAST_PHS_LINE,origin};
                break;
            }
            /* Classic rail shares the actor-origin path with heat beams. */
            /* fall through */
        case Q2_NAME_RESOURCE_Q2_HEATBEAM:
        case Q2_NAME_RESOURCE_Q2_MONSTER_HEATBEAM: {
            qa_body_state body;
            if (!qa_world_body_read(c->game->services.world,c->actor->id,&body,e)) return false;
            if (!q2_actor_live(c->game,c->actor->id)) return true;
            event.q2_multicast=(qa_builtin_q2_multicast){
                name==Q2_NAME_RESOURCE_Q2_RAIL ? QA_BUILTIN_Q2_MULTICAST_PHS : QA_BUILTIN_Q2_MULTICAST_ALL,
                body.origin};
            break;
        }
        default: break;
        }
    }
    event.resource=c->game->runtime_names[name];
    return qa_builtin_emit(&c->game->services, &event, e);
}
bool q2_event(q2_weapon_call *c, qa_builtin_event_kind kind, int code, qa_vec3 origin, qa_vec3 end,
    qa_error *e) {
    return q2_event_named(c, kind, Q2_NAME_NONE, code, origin, end, e);
}
bool q2_emit_sound(qa_q2_game *g, qa_actor_id actor, qa_builtin_event_kind kind, const qa_vec3 *origin,
    qa_string_id resource, int channel, float volume, float attenuation, uint32_t flags, qa_error *e) {
    if (!q2_actor_live(g,actor)) return true;
    qa_body_state body;
    if (!origin && !qa_world_body_read(g->services.world,actor,&body,e)) return !q2_actor_live(g,actor);
    if (!q2_actor_live(g,actor)) return true;
    return qa_builtin_emit(&g->services,&(qa_builtin_event){.kind=kind,
        .family=QA_GAME_Q2,.provider=g->options.owner,.actor=actor,.time_ns=g->now_ns,
        .origin=origin?*origin:body.origin,.resource=resource,.channel=channel,
        .volume=volume,.attenuation=attenuation,.flags=flags},e);
}
bool q2_actor_loop(qa_q2_game *g, qa_actor_id actor, qa_string_id *current,
    qa_string_id next, int channel, bool stop_previous, qa_error *e) {
    if (!q2_actor_live(g,actor) || *current==next) return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world,actor,&body,e)) return !q2_actor_live(g,actor);
    if (!q2_actor_live(g,actor)) return true;
    qa_string_id previous=*current; *current=next;
    if (stop_previous && previous && !q2_emit_sound(g,actor,QA_BUILTIN_STOP_SOUND,&body.origin,previous,channel,1,1,0,e)) return false;
    if (!next || !q2_actor_live(g,actor) || *current!=next) return true;
    return q2_emit_sound(g,actor,QA_BUILTIN_SOUND,&body.origin,next,channel,1,1,1,e);
}
bool q2_sound(q2_weapon_call *c, q2_runtime_name name, int channel, float attenuation, qa_error *e) {
    return q2_emit_sound(c->game,c->actor->id,QA_BUILTIN_SOUND,NULL,c->game->runtime_names[name],channel,1,attenuation,0,e);
}
bool q2_loop(q2_weapon_call *c, q2_runtime_name name, qa_error *e) {
    return q2_actor_loop(c->game,c->actor->id,&c->state->loop_sound,
        name?c->game->runtime_names[name]:QA_STRING_NONE,1,true,e);
}
bool q2_noise(q2_weapon_call *c, qa_vec3 origin, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    if (c->rules == QA_RULESET_Q2_RERELEASE) {
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
    return q2_player_noise(c->game, c->actor->id, origin, Q2_NOISE_WEAPON, true, e);
}
bool q2_animation(q2_weapon_call *c, int priority, int first, int last, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    if (c->equipment && c->game->hooks.equipment_animation && (priority == 0 || priority == 2)) {
        bool handled = false;
        if (!c->game->hooks.equipment_animation(c->game->hooks.context, c->actor->id,
            priority == 2, &handled, e)) return false;
        if (handled || !q2_actor_live(c->game, c->actor->id)) return true;
    }
    if (!c->input.animate_player) return true;
    qa_builtin_event event = {.kind = QA_BUILTIN_Q2_PLAYER_ANIMATION,
                              .family = QA_GAME_Q2,
                              .provider = c->game->options.owner,
                              .actor = c->actor->id,
                              .time_ns = c->now_ns,
                              .code = priority,
                              .frame = first,
                              .count = last,
                              .flags = c->rules == QA_RULESET_Q2_RERELEASE ? 1u : 0u};
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
    bool quad = c->input.quad_until_ns > c->now_ns, twice = c->input.double_until_ns > c->now_ns;
    q2_runtime_name path = quad && twice && c->rules == QA_RULESET_Q2_RERELEASE ? QA_Q2_SFX_CTF_TECH2X
                       : quad                        ? QA_Q2_SFX_ITEMS_DAMAGE3
                       : twice                       ? QA_Q2_SFX_MISC_DDAMAGE3
                                                     : Q2_NAME_NONE;
    if (path != Q2_NAME_NONE && !q2_sound(c, path, 3, 1, e))
        return false;
    return true;
}
bool q2_multiplier(q2_weapon_call *c, float *out, qa_error *e) {
    if (c->game->hooks.source_damage_factor != NULL) {
        qa_actor_id actor = c->actor->id;
        bool handled = false;
        float source;
        if (!c->game->hooks.source_damage_factor(c->game->hooks.context, actor,
                                                 &source, &handled, e))
            return false;
        if (q2_actor_get(c->game, actor, false, e) != c->actor)
            return false;
        if (handled) {
            if (!isfinite(source) || source < 0) {
                qa_error_set(e, QA_ERROR_FORMAT, 0, "Source Q2 damage factor is not finite and nonnegative");
                return false;
            }
            *out = source;
            return true;
        }
    }
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
    *out = result;
    return true;
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
    *origin = qa_vec_scale(s->kick_origin, c->rules == QA_RULESET_Q2_RERELEASE ? factor : impulse);
    *angles = qa_vec_scale(s->kick_angles, factor);
}
qa_string_id qa_q2_weapon_view_model(const qa_q2_game *game, const qa_q2_weapon_state *state)
{ return state->view_model ? state->view_model : game->view_models[state->weapon]; }

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
                                               : qa_q2_weapon_view_model(c->game, s)};
    q2_recoil(c, &view.kick_origin, &view.kick_angles);
    return c->game->hooks.weapon_view(c->game->hooks.context, &view, e);
}
bool q2_project(q2_weapon_call *c, qa_vec3 angles, qa_vec3 offset, qa_vec3 *start,
                qa_vec3 *direction, qa_error *e) {
    qa_vec3 f, r, u;
    qa_angle_vectors(angles, &f, &r, &u,QA_ANGLE_FLOAT_MATH);
    qa_body_state body;
    if (!qa_world_body_read(c->game->services.world, c->actor->id, &body, e))
        return false;
    float side = c->input.hand == QA_Q2_LEFT_HAND     ? -offset.y
                 : c->input.hand == QA_Q2_CENTER_HAND ? 0
                                                      : offset.y;
    qa_vec3 eye = qa_vec_add(body.origin, qa_v3(0, 0, c->input.view_height));
    *start = qa_vec_add(qa_vec_add(eye, qa_vec_scale(f, offset.x)), qa_vec_scale(r, side));
    *start = qa_vec_add(*start, c->rules == QA_RULESET_Q2_RERELEASE ? qa_vec_scale(u, offset.z) : qa_v3(0, 0, offset.z));
    *direction = f;
    if (c->rules != QA_RULESET_Q2_RERELEASE)
        return true;
    qa_trace_query query = {
        .start = eye, .end = qa_vec_add(eye, qa_vec_scale(f, 8192)), .pass_actor = c->actor->id};
    query.policy = qa_collision_ruleset_policy(c->rules);
    query.policy.contents_mask =
        qa_collision_contents_mask((c->input.players_collide ? Q2_PROJECTILE_MASK : Q2_PROJECTILE_MASK & ~Q2_PLAYER_CONTENTS) &
        ~UINT32_C(0x04000000), QA_GAME_Q2);
    qa_trace_result trace;
    if (!qa_world_trace(c->game->services.world, &query, &trace, e))
        return false;
    bool close = ((uint32_t)qa_collision_contents_export(trace.contents, QA_GAME_Q2, trace.q1_opaque_token) & (UINT32_C(0x02000000) | Q2_PLAYER_CONTENTS)) != 0 &&
                 trace.fraction * 8192 < 128;
    if (!trace.start_solid && !close)
        *direction = qa_vec_normalize(qa_vec_sub(trace.end, *start));
    return true;
}
