#include "internal.h"

bool qa_q1_grapple_weapon_resume(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!g) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "missing Q1 grapple provider");
        return false;
    }
    q1_player *player = q1_player_allocate(g, actor, error);
    if (!player)
        return false;
    player->grapple_weapon.selected = true;
    player->grapple_weapon.frame = 0;
    return true;
}
bool qa_q1_grapple_weapon_holster(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!g) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "missing Q1 grapple provider");
        return false;
    }
    q1_player *player = q1_player_allocate(g, actor, error);
    if (!player)
        return false;
    player->grapple_weapon.selected = false;
    player->grapple_weapon.frame = 0;
    return true;
}
static bool moving_frame(qa_q1_game *g, q1_player *player, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    player->grapple_weapon.frame = qa_vec_length(body.velocity) >= 750 ? 4 : 3;
    return true;
}
bool qa_q1_grapple_weapon_tick(qa_q1_game *g, qa_actor_id actor, const qa_q1_input *input,
                               bool available, qa_error *error) {
    if (!g || !input) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 grapple weapon tick");
        return false;
    }
    q1_player *player = q1_player_allocate(g, actor, error);
    if (!player)
        return false;
    player->grapple_weapon.available = available;
    if (!qa_q1_grapple_input(g, actor, input, player->grapple_weapon.selected && !input->attack,
                             error))
        return false;
    if (!player->grapple_weapon.selected)
        return true;
    if (input->attack && available && player->grapple_weapon.attack_finished <= g->time) {
        player->grapple_weapon.attack_finished = g->time + 0.1;
        if (q1_entity(g, q1_ref_actor(g, player->hook))) {
            if (!moving_frame(g, player, error))
                return false;
        } else if (!q1_entity(g, q1_ref_actor(g, player->grapple_weapon.animation))) {
            q1_actor *timer;
            if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_CTF_HOOK_ANIMATION], Q1_TIMER, actor, &timer, error))
                return false;
            player->grapple_weapon.animation = q1_ref_from(g, timer->id);
            player->grapple_weapon.frame = 2;
            if (!q1_schedule(g, timer, 0.1, Q1_THINK_HOOK_LAUNCH, error))
                return false;
            if (g->host.grapple_weapon_frame &&
                !g->host.grapple_weapon_frame(g->host.context, actor, 2, error)) return false;
        }
    }
    return true;
}
static bool frame_event(qa_q1_game *g, q1_player *player, int32_t frame, qa_error *error) {
    player->grapple_weapon.frame = player->weapon_frame = frame;
    qa_builtin_event event = {.kind = QA_BUILTIN_ANIMATION,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = player->id,
                              .time_ns = g->time_ns,
                              .frame = frame,
                              .flags = QA_Q1_CTF_GRAPPLE};
    if (!qa_builtin_resource(&g->services, "progs/v_star.mdl", &event.resource, error))
        return false;
    return qa_builtin_emit(&g->services, &event, error);
}
bool q1_grapple_weapon_frame(qa_q1_game *g, q1_player *player, qa_error *error) {
    qa_actor_id actor = player->id;
    q1_actor *hook = q1_entity(g, q1_ref_actor(g, player->hook));
    if (hook && hook->kind == Q1_PROJECTILE && hook->state.projectile.kind == Q1_CTF_HOOK) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, actor, &body, error))
            return !q1_player_get(g, actor);
        player = q1_player_get(g, actor);
        if (!player)
            return true;
        int32_t next = qa_vec_length(body.velocity) >= 750 ? 4 : 3;
        return next == player->grapple_weapon.frame || frame_event(g, player, next, error);
    }
    if (!q1_entity(g, q1_ref_actor(g, player->grapple_weapon.animation)) && player->grapple_weapon.frame != 0) {
        if (player->grapple_weapon.frame != 5) {
            if (!frame_event(g, player, 5, error))
                return false;
            player = q1_player_get(g, actor);
            if (!player)
                return true;
            return q1_think_deadline(g->time, 0.1, &player->grapple_weapon.release_time, error);
        }
        if (g->time >= player->grapple_weapon.release_time)
            return frame_event(g, player, 0, error);
    }
    return true;
}
bool qa_q1_grapple_weapon_frame(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    q1_player *player = q1_player_get(g, actor);
    bool ok = !player || q1_grapple_weapon_frame(g, player, error);
    if (!qa_q1_game_operation_live(&operation)) {
        if (ok || (error && error->code == QA_OK))
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                         "Q1 teardown requested during grapple weapon frame");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool q1_grapple_weapon_launch(qa_q1_game *g, q1_actor *timer, qa_error *error) {
    q1_player *player = q1_player_get(g, q1_ref_actor(g, timer->owner));
    if (player) {
        player->grapple_weapon.animation = (q1_ref){0};
        if (q1_health(g, player->id) > 0 && player->grapple_weapon.selected) {
            player->grapple_weapon.frame = 3;
            if (player->grapple_weapon.available &&
                !qa_q1_grapple_fire(g, player->id, true, &player->grapple_input, error))
                return false;
        }
    }
    return !q1_alive(g, timer->id) || q1_remove(g, timer, error);
}
bool qa_q1_grapple_weapon_read(const qa_q1_game *g, qa_actor_id actor,
                               qa_q1_grapple_weapon_view *out) {
    if (!g || !out || actor.slot >= g->capacity)
        return false;
    const q1_player *player = g->players[actor.slot];
    if (!player || !player->active || !qa_actor_id_equal(player->id, actor))
        return false;
    const q1_actor *hook = q1_entity_const(g, q1_ref_actor(g, player->hook));
    int32_t frame = player->grapple_weapon.frame;
    *out = (qa_q1_grapple_weapon_view){
        .weapon_frame = frame,
        .character_frame = frame == 2 ? 137
                           : frame == 3
                               ? hook && g->time < hook->state.projectile.expires - 4.9 ? 138 : 139
                           : frame == 4 ? 73
                           : frame == 5 ? 140
                                        : -1,
        .attack_finished = player->grapple_weapon.attack_finished,
        .release_time = player->grapple_weapon.release_time,
        .selected = player->grapple_weapon.selected,
        .available = player->grapple_weapon.available,
        .animating = q1_entity_const(g, q1_ref_actor(g, player->grapple_weapon.animation)) != NULL,
        .pulling = player->grapple_pulling,
        .axe_pose = true};
    return true;
}
