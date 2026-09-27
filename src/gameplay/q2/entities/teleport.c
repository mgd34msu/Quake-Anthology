#include "internal.h"

static bool teleport_event(qa_q2_game *g, qa_actor_id id, qa_vec3 origin, qa_error *e) {
    return qa_builtin_emit(&g->services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_ANIMATION,
                                               .family = QA_GAME_Q2,
                                               .provider = g->options.owner,
                                               .actor = id,
                                               .origin = origin,
                                               .code = 6,
                                               .time_ns = g->now_ns},
                           e);
}
bool q2_entity_teleport(qa_q2_game *g, q2_actor *source, qa_actor_id id,
                        const qa_body_state *destination, bool ctf, bool pad, qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    if (ctf && !qa_q2_grapple_reset(g, id, QA_Q2_CTF_GRAPPLE, e))
        return false;
    if (!q2_actor_live(g, id) || !q2_actor_live(g, source->id))
        return true;
    qa_vec3 from = body.origin;
    if (!ctf && !pad &&
        !q2_projectile_event(g, id, QA_BUILTIN_TELEPORT, "q2:teleport_effect", 0, from,
                             qa_v3(0, 0, 0), e))
        return false;
    if (!q2_actor_live(g, id) || !q2_actor_live(g, source->id))
        return true;
    body.origin = destination->origin;
    if (!ctf)
        body.origin.z += 10;
    body.velocity = ctf ? qa_vec_scale(q2_movedir(destination->angles), 200) : qa_v3(0, 0, 0);
    body.angles = ctf ? qa_v3(0, destination->angles.y, 0) : qa_v3(0, 0, 0);
    body.ground = (qa_actor_id){0};
    if (!qa_world_unlink(g->services.world, id, e) ||
        !qa_world_body_write(g->services.world, id, &body, e))
        return false;
    q2_actor *native = q2_actor_get(g, id, false, NULL);
    if (native && native->client) {
        qa_q2_player_movement observed;
        if (!q2_player_observe(g, native, &observed, e) ||
            !q2_player_move(g, native,
                            &(qa_q2_player_motion){.kind = QA_Q2_PLAYER_TELEPORT,
                                                   .origin = body.origin,
                                                   .velocity = body.velocity,
                                                   .angles = destination->angles,
                                                   .command_angles = observed.command_angles,
                                                   .hold_ns = 160 * Q2_MS,
                                                   .spectator = native->client->info.spectator},
                            e))
            return false;
    } else {
        if (!g->services.motion_changed) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                         "Q2 teleport requires selected movement service");
            return false;
        }
        if (!g->services.motion_changed(
                g->services.context, id,
                &(qa_builtin_motion_change){.reason = QA_BUILTIN_MOTION_TELEPORT,
                                            .body = body,
                                            .view_angles = destination->angles,
                                            .hold_ns = 160 * Q2_MS,
                                            .force_view_angles = true},
                e))
            return false;
    }
    if (!q2_actor_live(g, id) || !q2_actor_live(g, source->id))
        return true;
    if (ctf) {
        if (q2_actor_live(g, source->entity->enemy) &&
            !teleport_event(g, source->entity->enemy, from, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
        if (!teleport_event(g, id, body.origin, e))
            return false;
    } else if (pad) {
        qa_body_state origin;
        qa_actor_id emitter =
            q2_actor_live(g, source->entity->owner) ? source->entity->owner : source->id;
        if (!qa_world_body_read(g->services.world, emitter, &origin, e) ||
            !q2_projectile_event(g, emitter, QA_BUILTIN_TELEPORT, "q2:player-teleport", 0,
                                 origin.origin, qa_v3(0, 0, 0), e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
        if (!q2_projectile_event(g, id, QA_BUILTIN_TELEPORT, "q2:player-teleport", 0, body.origin,
                                 qa_v3(0, 0, 0), e))
            return false;
    }
    if (!q2_actor_live(g, id))
        return true;
    bool clear;
    return qa_q2_entities_killbox(g, id, id, &clear, e) &&
           (!q2_actor_live(g, id) || qa_world_link(g->services.world, id, NULL, e));
}
