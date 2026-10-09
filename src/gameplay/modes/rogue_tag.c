#include "internal.h"

enum { TAG_REST, TAG_PLACE, TAG_FOLLOW, TAG_FALL, TAG_RESPAWN };

static bool announce(qa_modes *m, mode_instance *v, qa_actor_id actor, const char *text,
                     qa_error *e) {
    if (!m->options.hooks.event)
        return true;
    qa_mode_event event = {.kind = QA_MODE_TAG_CHANGED,
                           .mode = v->id,
                           .actor = actor,
                           .object = v->tag,
                           .time_ns = v->value.time_ns,
                           .detail = !strcmp(text, "$qc_got_quad")};
    if (!qa_builtin_resource(&m->options.services, text, &event.text, e))
        return false;
    return MODE_CALLBACK(m, m->options.hooks.event(m->options.hooks.context, &event, e));
}
static bool floor_token(qa_modes *m, mode_object *o, bool *landed, qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(m->options.services.world, o->actor, &body, e))
        return false;
    qa_trace_query query = {
        .start = body.origin,
        .end = qa_vec_add(body.origin, qa_v3(0, 0, -256)),
        .shape = {QA_SHAPE_BOX, body.bounds},
        .policy = {.family = QA_COLLISION_Q1, .contents_mask = qa_collision_contents_mask(3, QA_COLLISION_Q1), .q1_hull = -1},
        .pass_actor = o->actor};
    qa_trace_result result;
    if (!qa_world_trace(m->options.services.world, &query, &result, e))
        return false;
    *landed = result.fraction < 1 && !result.all_solid;
    if (!*landed)
        return true;
    body.origin = result.end;
    body.ground = qa_actor_reference_lifetime(result.actor);
    return qa_world_body_write(m->options.services.world, o->actor, &body, e) &&
           qa_world_link(m->options.services.world, o->actor, NULL, e);
}
static bool take(qa_modes *m, mode_instance *v, mode_object *o, qa_actor_id actor, uint64_t delay,
                 qa_error *e) {
    qa_actor_id previous = o->value.carrier;
    if (mode_live(m, previous) && !mode_object_count(m, v, o, previous, 0, e))
        return false;
    if (!mode_object_count(m, v, o, actor, 1, e))
        return false;
    v->tag_owner = actor;
    v->tag_count = 0;
    o->value.carrier = actor;
    o->value.phase = QA_OBJECTIVE_CARRIED;
    o->value.visible = true;
    o->physics.motion = QA_PHYSICS_STATIONARY;
    o->tag_stage = TAG_FOLLOW;
    o->animation_ns = v->value.time_ns + delay;
    o->next_ns = v->value.time_ns + MODE_SECOND / 10;
    o->expire_ns = 0;
    return mode_object_sync(m, o, e);
}
static bool respawn(qa_modes *m, mode_instance *v, mode_object *o, qa_error *e) {
    qa_mode_spawnpoint point;
    qa_body_state body;
    if (!qa_modes_spawnpoint(m, v->id, (qa_actor_id){0}, true, &point, e) ||
        !qa_world_body_read(m->options.services.world, o->actor, &body, e))
        return false;
    if (mode_live(m, o->value.carrier) && !mode_object_count(m, v, o, o->value.carrier, 0, e))
        return false;
    v->tag_owner = (qa_actor_id){0};
    v->tag_count = 0;
    o->value.carrier = (qa_actor_id){0};
    o->value.phase = QA_OBJECTIVE_HOME;
    o->value.visible = true;
    o->tag_stage = TAG_REST;
    o->next_ns = o->expire_ns = 0;
    o->physics.motion = QA_PHYSICS_TOSS;
    body.origin = point.origin;
    if (!qa_world_body_write(m->options.services.world, o->actor, &body, e))
        return false;
    bool landed;
    return floor_token(m, o, &landed, e) && mode_object_sync(m, o, e);
}
bool mode_rogue_tag_touch(qa_modes *m, mode_instance *v, mode_object *o, qa_actor_id actor,
                          bool *accepted, qa_error *e) {
    if (!take(m, v, o, actor, 30 * MODE_SECOND, e) ||
        !mode_sound(m, v, o->actor, "runes/end1.wav", 1, e))
        return false;
    *accepted = true;
    return announce(m, v, actor, "$qc_got_token", e);
}
bool mode_rogue_tag_drop(qa_modes *m, mode_instance *v, mode_object *o, qa_error *e) {
    qa_actor_id owner = o->value.carrier;
    if (owner.registry && !announce(m, v, owner, "$qc_lost_token", e))
        return false;
    if (mode_live(m, owner) && !mode_object_count(m, v, o, owner, 0, e))
        return false;
    v->tag_owner = (qa_actor_id){0};
    v->tag_count = 0;
    o->value.carrier = (qa_actor_id){0};
    o->value.phase = QA_OBJECTIVE_DROPPED;
    o->value.visible = true;
    o->physics.motion = QA_PHYSICS_TOSS;
    o->tag_stage = TAG_FALL;
    o->next_ns = v->value.time_ns + MODE_SECOND / 10;
    return mode_object_sync(m, o, e);
}
bool mode_rogue_tag_frame(qa_modes *m, mode_instance *v, mode_object *o, qa_error *e) {
    if (!o->next_ns || v->value.time_ns < o->next_ns)
        return true;
    o->next_ns = 0;
    if (o->tag_stage == TAG_FOLLOW) {
        qa_actor_id owner = o->value.carrier;
        if (!mode_alive(m, owner))
            return mode_rogue_tag_drop(m, v, o, e);
        if (o->animation_ns < v->value.time_ns) {
            o->animation_ns = v->value.time_ns + 30 * MODE_SECOND;
            if (!announce(m, v, owner, "$qc_has_token", e))
                return false;
        }
        qa_body_state player, token;
        if (!qa_world_body_read(m->options.services.world, owner, &player, e) ||
            !qa_world_body_read(m->options.services.world, o->actor, &token, e))
            return false;
        token.origin = qa_vec_add(player.origin, qa_v3(0, 0, 48));
        o->next_ns = v->value.time_ns + MODE_SECOND / 10;
        return qa_world_body_write(m->options.services.world, o->actor, &token, e) &&
               qa_world_link(m->options.services.world, o->actor, NULL, e);
    }
    if (o->tag_stage == TAG_RESPAWN)
        return respawn(m, v, o, e);
    bool initial = o->tag_stage == TAG_PLACE;
    if (initial) {
        qa_body_state body;
        if (!qa_world_body_read(m->options.services.world, o->actor, &body, e))
            return false;
        body.origin.z += 6;
        o->physics.motion = QA_PHYSICS_TOSS;
        if (!qa_world_body_write(m->options.services.world, o->actor, &body, e))
            return false;
    }
    bool landed;
    if (!floor_token(m, o, &landed, e))
        return false;
    if (initial && !landed)
        return qa_session_release(m->options.services.session, o->actor, e);
    o->tag_stage = initial ? TAG_REST : TAG_RESPAWN;
    o->next_ns = initial ? 0 : v->value.time_ns + 30 * MODE_SECOND;
    return mode_object_sync(m, o, e);
}
static bool score(qa_modes *m, mode_instance *v, qa_actor_id victim, qa_actor_id attacker,
                  int32_t *points, qa_error *e) {
    *points = 1;
    mode_object *o = mode_object_get(m, v->tag);
    if (!o)
        return true;
    if (qa_actor_id_equal(attacker, v->tag_owner)) {
        *points = 3;
        ++v->tag_count;
        if (v->tag_count == 5) {
            if (!m->options.hooks.give_quad)
                return mode_fail(e, "Rogue Tag needs selected timed-power provider");
            if (!announce(m, v, attacker, "$qc_got_quad", e))
                return false;
            return m->options.hooks.give_quad(m->options.hooks.context, v->id, attacker, QA_GAME_Q1,
                                              30 * MODE_SECOND, e);
        }
        if (v->tag_count == 10)
            return announce(m, v, attacker, "$qc_lost_token", e) && respawn(m, v, o, e);
    } else if (qa_actor_id_equal(victim, v->tag_owner)) {
        *points = 5;
        if (!mode_sound(m, v, victim, "runes/end1.wav", 1, e))
            return false;
        if (mode_player_get(m, attacker))
            return take(m, v, o, attacker, MODE_SECOND / 2, e);
    }
    return true;
}
bool qa_modes_rogue_tag_score(qa_modes *m, qa_mode_id id, qa_actor_id victim, qa_actor_id attacker,
                              int32_t *points, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !points || v->value.rules.source != QA_MODE_ROGUE ||
        v->value.rules.kind != QA_MODE_TAG)
        return mode_fail(e, "invalid Rogue Tag score decision");
    *points = 1;
    if (!v->value.rules.enabled)
        return true;
    return MODE_CALLBACK(m, score(m, v, victim, attacker, points, e));
}
