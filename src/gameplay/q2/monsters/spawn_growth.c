#include "reinforcements.h"
#include "../items/internal.h"

static bool show_growth(qa_q2_game *game, q2_actor *actor, qa_error *error) {
    q2_projectile *p = &actor->projectile;
    qa_entity_visual visual = {.models = {p->model},
                           .frame = p->frame,
                           .skin = p->skin,
                           .render_flags = p->render_flags,
                           .scale = p->scale,
                           .alpha = p->alpha,
                           .visible = true};
    return q2_publish_visual(game, actor->id, &visual, error);
}

static bool beam_tick(qa_q2_game *game, q2_actor *beam, qa_error *error) {
    qa_actor_id id = beam->id;
    qa_entity_visual owner = {0};
    (void)qa_q2_presentation_read(game, qa_actor_reference_resolve(
        qa_session_actors(game->services.session), beam->projectile.owner), &owner);
    qa_body_state body;
    if (!qa_world_body_read(game->services.world, id, &body, error))
        return false;
    if (!q2_actor_live(game, id))
        return true;
    float theta = q2_rerelease_float(game, 0, 6.283185307179586f);
    float phi = acosf(q2_rerelease_float(game, -1, 1));
    qa_vec3 direction = {sinf(phi) * cosf(theta), sinf(phi) * sinf(theta), cosf(phi)};
    beam->projectile.movedir =
        qa_vec_add(body.origin, qa_vec_scale(direction, owner.scale * 9));
    if (!qa_world_link(game->services.world, id, NULL, error))
        return false;
    if (!q2_actor_live(game, id))
        return true;
    if (!show_growth(game, beam, error))
        return false;
    if (q2_actor_live(game, id))
        beam->projectile.next_ns = q2_deadline(game->now_ns, Q2_MS);
    return true;
}

bool q2m_rerelease_growth_tick(qa_q2_game *game, q2_actor *actor, qa_error *error) {
    if (!actor->projectile.next_ns || actor->projectile.next_ns > game->now_ns)
        return true;
    if (actor->projectile.kind == Q2_RERELEASE_SPAWN_BEAM)
        return beam_tick(game, actor, error);
    qa_actor_id id = actor->id;
    if (game->now_ns >= actor->projectile.expire_ns) {
        q2_actor *beam = q2_actor_get(game, qa_actor_reference_resolve(qa_session_actors(game->services.session), actor->projectile.child), false, NULL);
        if (beam) {
            qa_actor_id beam_id = beam->id;
            if (q2_actor_live(game, beam_id) &&
                !qa_session_release(game->services.session, beam_id, error))
                return false;
        }
        return !q2_actor_live(game, id) || qa_session_release(game->services.session, id, error);
    }
    qa_body_state body;
    if (!qa_world_body_read(game->services.world, id, &body, error))
        return false;
    if (!q2_actor_live(game, id))
        return true;
    body.angles = qa_vec_add(body.angles, qa_vec_scale(actor->physics.angular_velocity,
                                                       (float)((double)game->frame_ns / Q2_NS)));
    if (!qa_world_body_write(game->services.world, id, &body, error))
        return false;
    if (!q2_actor_live(game, id))
        return true;
    if (!qa_world_link(game->services.world, id, NULL, error))
        return false;
    if (!q2_actor_live(game, id))
        return true;
    q2_projectile *p = &actor->projectile;
    int64_t elapsed_ms = (int64_t)(game->now_ns / Q2_MS) - (int64_t)(p->born_ns / Q2_MS);
    float t = 1 - ((float)elapsed_ms / 1000.0f) / p->delay;
    p->scale = fmaxf(.001f, fminf(16,
        ((p->radius * t) + (p->radius_damage * (1 - t))) / 16));
    p->alpha = t * t;
    if (!show_growth(game, actor, error))
        return false;
    if (q2_actor_live(game, id)) {
        /* SV_RunThink clears nextthink before Source adds FRAME_TIME_MS. */
        p->next_ns = game->frame_ns;
    }
    return true;
}

static bool admit_part(qa_q2_game *game, const char *classname, qa_body_state body, q2_actor **out,
                       qa_error *error) {
    qa_string_id definition;
    if (!qa_builtin_resource(&game->services, classname, &definition, error))
        return false;
    qa_builtin_spawn spawn = {.owner = game->options.owner, .definition = definition, .body = body};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&game->services, &spawn, &id, error))
        return false;
    q2_actor *actor = q2_actor_get(game, id, true, error);
    if (!actor) {
        (void)qa_session_release(game->services.session, id, NULL);
        return false;
    }
    actor->projectile.classname = definition;
    actor->projectile.visible = true;
    actor->projectile.alpha = 1;
    actor->physics = qa_physics_properties_default(QA_GAME_Q2);
    actor->physics.q2_rerelease = true;
    actor->physics.motion = QA_PHYSICS_STATIONARY;
    actor->physics.solid = QA_PHYSICS_NOT_SOLID;
    actor->physics_bound = true;
    *out = actor;
    return true;
}

bool q2m_rerelease_spawn_growth(qa_q2_game *game, qa_vec3 origin, float radius, qa_error *error) {
    q2_actor *growth;
    if (!admit_part(game, "spawngro", (qa_body_state){0}, &growth, error))
        return false;
    qa_actor_id id = growth->id;
    qa_vec3 angles;
    angles.x = (float)q2_random_bounded(game, 360);
    angles.y = (float)q2_random_bounded(game, 360);
    angles.z = (float)q2_random_bounded(game, 360);
    if (!qa_world_body_write(game->services.world, id,
                             &(qa_body_state){.origin = origin, .angles = angles}, error))
        goto failed;
    if (!q2_actor_live(game, id))
        return true;
    q2_projectile *p = &growth->projectile;
    p->kind = Q2_RERELEASE_SPAWN_GROWTH;
    p->render_flags = 32768;
    p->skin = 1;
    p->radius = radius;
    p->radius_damage = radius * 2;
    p->delay = 1;
    p->scale = fmaxf(.001f, fminf(8, radius / 16));
    p->born_ns = game->now_ns;
    p->expire_ns = q2_deadline(game->now_ns, Q2_NS);
    p->next_ns = q2_deadline(game->now_ns, game->frame_ns);
    growth->physics.angular_velocity.x = q2_rerelease_float(game, 280, 360) * 2;
    growth->physics.angular_velocity.y = q2_rerelease_float(game, 280, 360) * 2;
    growth->physics.angular_velocity.z = q2_rerelease_float(game, 280, 360) * 2;
    if (!qa_builtin_resource(&game->services, "models/items/spawngro3/tris.md2", &p->model,
                             error) ||
        !qa_world_link(game->services.world, id, NULL, error))
        goto failed;
    if (!q2_actor_live(game, id))
        return true;
    if (!show_growth(game, growth, error))
        goto failed;
    if (!q2_actor_live(game, id))
        return true;
    q2_actor *beam;
    if (!admit_part(game, "spawngro_beam", (qa_body_state){.origin = origin}, &beam, error))
        goto failed;
    if (!q2_actor_live(game, id))
        return qa_session_release(game->services.session, beam->id, error);
    p->child = qa_actor_reference_source(game->options.owner, beam->wire_slot);
    beam->projectile.kind = Q2_RERELEASE_SPAWN_BEAM;
    beam->projectile.owner = qa_actor_reference_source(game->options.owner, growth->wire_slot);
    beam->projectile.frame = 1;
    beam->projectile.skin = 0x30303030;
    beam->projectile.render_flags = 128 | 512 | 64;
    beam->projectile.radius = radius * 2;
    beam->projectile.expire_ns = UINT64_MAX;
    if (beam_tick(game, beam, error))
        return true;
    if (q2_actor_live(game, beam->id))
        (void)qa_session_release(game->services.session, beam->id, NULL);
failed:
    if (q2_actor_live(game, id))
        (void)qa_session_release(game->services.session, id, NULL);
    return false;
}
