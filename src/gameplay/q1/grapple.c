#include "internal.h"

static bool threewave(const q1_actor *hook) { return hook->state.projectile.kind == Q1_CTF_HOOK; }
static q1_player *owner_state(qa_q1_game *g, qa_actor_id actor) {
    if (actor.slot >= g->capacity)
        return NULL;
    q1_player *player = g->players[actor.slot];
    return player && qa_actor_id_equal(player->id, actor) ? player : NULL;
}
static bool reset(qa_q1_game *g, q1_actor *hook, qa_error *error) {
    q1_player *player = owner_state(g, q1_ref_actor(g, hook->owner));
    bool ctf = threewave(hook);
    if (player && q1_ref_equal(player->hook, q1_ref_from(g, hook->id))) {
        player->hook = (q1_ref){0};
        player->grapple_pulling = false;
        if (!ctf) {
            player->weapon_frame = 0;
            player->attack_finished = g->time + 0.25;
            player->animation_at = -1;
        }
    }
    if (q1_alive(g, q1_ref_actor(g, hook->owner))) {
        if (ctf && g->options.edition == QA_Q1_CLASSIC &&
            !q1_sound(g, q1_ref_actor(g, hook->owner), "weapons/bounce2.wav", 1, 1, error))
            return false;
        if (!ctf && player && player->weapon == QA_Q1_ROGUE_GRAPPLE &&
            !q1_weapon_event(g, player, 0, 0, error))
            return false;
    }
    for (unsigned i = 0; i < 3; ++i) {
        q1_actor *link = q1_entity(g, q1_ref_actor(g, hook->state.projectile.links[i]));
        if (link && !q1_remove(g, link, error))
            return false;
    }
    if (!q1_alive(g, hook->id))
        return true;
    return qa_world_detach(g->services.world, hook->id, error) && q1_remove(g, hook, error);
}
bool qa_q1_grapple_release(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = g ? owner_state(g, actor) : NULL;
    q1_actor *hook = player ? q1_entity(g, q1_ref_actor(g, player->hook)) : NULL;
    if (player) {
        player->grapple_pulling = false;
        q1_actor *timer = q1_entity(g, q1_ref_actor(g, player->grapple_weapon.animation));
        player->grapple_weapon.animation = (q1_ref){0};
        if (timer && !q1_remove(g, timer, error))
            return false;
    }
    return !hook || reset(g, hook, error);
}
void q1_grapple_released(qa_q1_game *g, qa_actor_record released) {
    qa_actor_id actor = released.id;
    q1_ref reference = g->wire && released.owner == g->options.provider && released.has_source ?
        q1_ref_source(g, released.source_slot) : qa_actor_reference_lifetime(actor);
    q1_player *player = owner_state(g, actor);
    if (player && (q1_ref_present(player->hook) ||
                   q1_ref_present(player->grapple_weapon.animation) || player->grapple_pulling))
        (void)qa_q1_grapple_release(g, actor, NULL);
    q1_actor *entity = actor.slot < g->capacity ? g->actors[actor.slot] : NULL;
    if (entity && qa_actor_id_equal(entity->id, actor) && entity->kind == Q1_TIMER) {
        q1_player *owner = owner_state(g, q1_ref_actor(g, entity->owner));
        if (owner && q1_ref_equal(owner->grapple_weapon.animation, reference))
            owner->grapple_weapon.animation = (q1_ref){0};
    }
    if (entity && qa_actor_id_equal(entity->id, actor) && entity->kind == Q1_PROJECTILE &&
        (entity->state.projectile.kind == Q1_ROGUE_HOOK ||
         entity->state.projectile.kind == Q1_CTF_HOOK)) {
        q1_player *owner = owner_state(g, q1_ref_actor(g, entity->owner));
        if (owner && q1_ref_equal(owner->hook, reference)) {
            owner->hook = (q1_ref){0};
            owner->grapple_pulling = false;
        }
        for (unsigned i = 0; i < 3; ++i) {
            q1_actor *link = q1_entity(g, q1_ref_actor(g, entity->state.projectile.links[i]));
            if (link)
                (void)q1_remove(g, link, NULL);
        }
    }
}
bool qa_q1_grapple_pulling(const qa_q1_game *g, qa_actor_id actor) {
    if (!g || actor.slot >= g->capacity)
        return false;
    const q1_player *player = g->players[actor.slot];
    return player && player->active && qa_actor_id_equal(player->id, actor) &&
           player->grapple_pulling;
}
bool qa_q1_grapple_input(qa_q1_game *g, qa_actor_id actor, const qa_q1_input *input, bool release,
                         qa_error *error) {
    if (!g || !input || !qa_vec_finite(input->view_angles)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 grapple input");
        return false;
    }
    q1_player *player = q1_player_allocate(g, actor, error);
    if (!player)
        return false;
    player->grapple_input = *input;
    player->grapple_release = release;
    return true;
}
static bool anchor_info(qa_q1_game *g, qa_actor_id actor, bool *solid, bool *centered,
                        bool *player, qa_error *error) {
    qa_q1_target target;
    *player = q1_target(g, actor, &target) && target.player;
    qa_actor_collision collision;
    qa_error local = {0};
    *solid = qa_world_get_collision(g->services.world, actor, &collision, &local);
    if (local.code != QA_OK) { if (error) *error = local; return false; }
    *centered = *player;
    qa_physics_properties physics;
    if (g->services.physics && g->services.physics->services.read &&
        g->services.physics->services.read(g->services.physics->services.context, actor,
                                           &physics)) {
        *solid = physics.solid != QA_PHYSICS_NOT_SOLID;
        *centered = *centered || physics.solid == QA_PHYSICS_BOX;
    }
    return q1_alive(g, actor);
}
static bool allowed(qa_q1_game *g, q1_actor *hook, qa_actor_id actor, bool pulse, qa_error *error,
                    bool *out) {
    if (threewave(hook)) {
        if (!g->host.grapple_allowed) {
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                         "Threewave grapple needs selected team policy");
            return false;
        }
        *out = g->host.grapple_allowed(g->host.context, q1_ref_actor(g, hook->owner), actor, pulse);
    } else {
        qa_q1_target target;
        qa_combat_state owner, enemy;
        bool is_player = q1_target(g, actor, &target) && target.player;
        *out = !is_player || !qa_combat_read(g->services.combat, q1_ref_actor(g, hook->owner), &owner, NULL) ||
               !qa_combat_read(g->services.combat, actor, &enemy, NULL) || owner.team != enemy.team;
    }
    return true;
}
static bool pull_velocity(qa_q1_game *g, q1_actor *hook, q1_player *player, bool link,
                          qa_error *error) {
    if (!g->services.motion_changed) {
        qa_error_set(error, QA_ERROR_ARGUMENT, player->id.slot,
                     "grapple requires selected movement continuation");
        return false;
    }
    qa_body_state body, head;
    if (!qa_world_body_read(g->services.world, player->id, &body, error) ||
        !qa_world_body_read(g->services.world, hook->id, &head, error))
        return false;
    if (!q1_alive(g, player->id) || !q1_alive(g, hook->id))
        return true;
    qa_vec3 forward, up;
    qa_builtin_angle_vectors(body.angles, &forward, NULL, &up);
    qa_vec3 delta = qa_vec_sub(
        head.origin,
        qa_vec_add(body.origin, qa_vec_add(qa_vec_scale(up, player->grapple_input.jump ? 0 : 16),
                                           qa_vec_scale(forward, 16))));
    float distance = qa_vec_length(delta);
    body.velocity = qa_vec_scale(qa_vec_normalize(delta), distance <= 100 ? distance * 10 : 1000);
    if (!threewave(hook))
        body.ground = (qa_actor_reference){0};
    if (!qa_world_body_write(g->services.world, player->id, &body, error))
        return false;
    if (!q1_alive(g, player->id) || !q1_alive(g, hook->id))
        return true;
    qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_LAUNCH, .body = body};
    if (!g->services.motion_changed(g->services.context, player->id, &change, error))
        return false;
    return !link || !q1_alive(g, player->id) ||
           qa_world_link(g->services.world, player->id, NULL, error);
}
static bool position_link(qa_q1_game *g, q1_actor *link, qa_error *error) {
    q1_actor *hook = q1_entity(g, q1_ref_actor(g, link->owner));
    q1_player *player = hook ? q1_player_get(g, q1_ref_actor(g, hook->owner)) : NULL;
    if (!hook || !player)
        return q1_remove(g, link, error);
    qa_body_state body, head, chain;
    if (!qa_world_body_read(g->services.world, player->id, &body, error) ||
        !qa_world_body_read(g->services.world, hook->id, &head, error) ||
        !qa_world_body_read(g->services.world, link->id, &chain, error))
        return false;
    qa_vec3 forward, up;
    qa_builtin_angle_vectors(body.angles, &forward, NULL, &up);
    qa_vec3 end =
        qa_vec_add(body.origin, qa_vec_add(qa_vec_scale(up, player->grapple_input.jump ? 0 : 16),
                                           qa_vec_scale(forward, 16)));
    chain.origin = qa_vec_add(head.origin, qa_vec_scale(qa_vec_sub(end, head.origin), link->count));
    return qa_world_body_write(g->services.world, link->id, &chain, error) &&
           q1_schedule(g, link, 0.1, Q1_THINK_HOOK_LINK, error);
}
bool qa_q1_grapple_fire(qa_q1_game *g, qa_actor_id actor, bool ctf, const qa_q1_input *input,
                        qa_error *error) {
    if (!qa_q1_grapple_input(g, actor, input, false, error))
        return false;
    q1_player *player = q1_player_get(g, actor);
    if (q1_health(g, actor) <= 0)
        return true;
    if (!ctf)
        player->attack_finished = g->time + 0.1;
    if (q1_entity(g, q1_ref_actor(g, player->hook))) {
        if (ctf)
            return true;
        player->weapon_frame = 2;
        return q1_weapon_event(g, player, 0, 0, error);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    qa_vec3 forward, direction;
    qa_builtin_angle_vectors(input->view_angles, &forward, NULL, NULL);
    direction = forward;
    if (ctf && !q1_aim(g, actor, forward, &direction, error))
        return false;
    q1_actor *hook;
    if (!q1_create(g, ctf ? "ctf_hook" : "hook", Q1_PROJECTILE, actor, &hook, error))
        return false;
    player->hook = q1_ref_from(g, hook->id);
    hook->state.projectile.kind = ctf ? Q1_CTF_HOOK : Q1_ROGUE_HOOK;
    hook->state.projectile.weapon = ctf ? QA_Q1_CTF_GRAPPLE : QA_Q1_ROGUE_GRAPPLE;
    const qa_q1_weapon_view *shape=q1_weapon_shape(hook->state.projectile.weapon);
    hook->state.projectile.activator = q1_ref_from(g, actor);
    hook->state.projectile.expires = g->time + shape->lifetime;
    hook->state.projectile.attack = q1_attack(g, actor, hook->id, hook->state.projectile.weapon);
    hook->state.projectile.attack.projectile = hook->id;
    if (ctf &&
        !qa_builtin_resource(&g->services, "ctf:grapple",
                             &hook->state.projectile.attack.cause.source.q1.death_type, error))
        return false;
    hook->physics.motion = ctf ? QA_PHYSICS_FLY : QA_PHYSICS_FLY_MISSILE;
    hook->physics.solid = QA_PHYSICS_BOX;
    hook->frame = ctf ? 0 : 1;
    if (ctf)
        hook->physics.angular_velocity = qa_v3(0, 0, -500);
    body = (qa_body_state){
        .origin = qa_vec_add(body.origin, qa_vec_add(qa_vec_scale(forward, 16), qa_v3(0, 0, 16)))};
    if (!qa_world_body_write(g->services.world, hook->id, &body, error) ||
        !q1_missile_velocity(g, hook, qa_vec_scale(direction, shape->speed), error) ||
        !q1_model(g, hook, ctf ? "progs/star.mdl" : "progs/hook.mdl", error) ||
        !q1_link(g, hook, error) ||
        !q1_schedule(g, hook, ctf ? 0.1 : shape->lifetime, ctf ? Q1_THINK_HOOK_FLY : Q1_THINK_HOOK_RESET,
                     error) ||
        !q1_sound(g, actor, "weapons/chain1.wav", 1, 1, error))
        return false;
    if (!q1_alive(g, hook->id))
        return true;
    if (ctf && g->options.edition == QA_Q1_CLASSIC)
        for (int number = 3; number > 0; --number) {
            q1_actor *link;
            if (!q1_create(g, "ctf_hook_link", Q1_TIMER, hook->id, &link, error))
                return false;
            hook->state.projectile.links[number - 1] = q1_ref_from(g, link->id);
            link->count = (float)number / 4;
            link->physics.motion = QA_PHYSICS_NOCLIP;
            link->physics.angular_velocity = qa_v3(310, 410, 510);
            qa_body_state chain = {.angles = {31.0f * (float)number, 41.0f * (float)number, 51.0f * (float)number}};
            if (!q1_model(g, link, "progs/bit.mdl", error) ||
                !qa_world_body_write(g->services.world, link->id, &chain, error) ||
                !position_link(g, link, error))
                return false;
        }
    if (!ctf) {
        player->weapon_frame = 1;
        player->animation_at = g->time;
        player->continuous = false;
        player->punch.x = -2;
        if (!q1_weapon_event(g, player, -2, 0, error))
            return false;
    }
    return q1_launch_behavior(g, hook, QA_BUILTIN_GRAPPLE, error);
}
bool q1_grapple_touch(qa_q1_game *g, q1_actor *hook, qa_actor_id actor,
                      const qa_touch_contact *contact, qa_error *error) {
    if (hook->state.projectile.count)
        return true;
    q1_player *player = q1_player_get(g, q1_ref_actor(g, hook->owner));
    if (!player)
        return reset(g, hook, error);
    if (qa_actor_id_equal(actor, player->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, hook->id, &body, error))
        return false;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    bool ctf = threewave(hook), accept;
    if (qa_collision_contents_export(contents.contents, QA_COLLISION_Q1, contents.q1_opaque_token) == -6 ||
        (ctf && contact && contact->has_surface && (qa_collision_surface_export(contact->surface.flags, QA_COLLISION_Q1) & 4)))
        return reset(g, hook, error);
    if (!allowed(g, hook, actor, false, error, &accept))
        return false;
    if (!accept)
        return ctf ? true : reset(g, hook, error);
    bool solid, centered, target_player;
    qa_error local = {0};
    qa_actor_id hook_id = hook->id, player_id = player->id;
    bool anchor = anchor_info(g, actor, &solid, &centered, &target_player, &local);
    if (local.code != QA_OK) { if (error) *error = local; return false; }
    hook = q1_entity(g, hook_id);
    player = q1_player_get(g, player_id);
    if (!hook) return true;
    if (!player || !anchor) return reset(g, hook, error);
    if (q1_damageable(g, actor)) {
        if ((!ctf || !target_player) &&
            !q1_sound(g, hook->id, target_player ? "player/axhit1.wav" : "player/axhit2.wav", 1, 1,
                      error))
            return false;
        if (!q1_damage(g, actor, hook->id, player->id, ctf || target_player ? 10 : 1,
                       hook->state.projectile.weapon, error))
            return false;
        if (!q1_alive(g, hook->id) || !q1_alive(g, player->id) || !q1_alive(g, actor))
            return reset(g, hook, error);
        if (ctf && !q1_effect(g, QA_BUILTIN_IMPACT, actor, body.origin, 20, 73, error))
            return false;
    } else if (!q1_sound(g, hook->id, "player/axhit2.wav", 1, 1, error))
        return false;
    if (!q1_alive(g, hook->id))
        return true;
    if (!ctf) {
        hook->frame = 2;
        if (!q1_sound(g, player->id, "weapons/tink1.wav", 1, 1, error))
            return false;
    }
    if (!player->grapple_input.attack)
        return reset(g, hook, error);
    qa_body_state target;
    if (!qa_world_body_read(g->services.world, actor, &target, error))
        return false;
    if (ctf) {
        if (centered) {
            body.origin =
                qa_vec_add(target.origin,
                           qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), 0.5f));
            body.velocity = qa_v3(0, 0, 0);
        } else
            body.velocity = target.velocity;
        qa_body_attachment attachment = {.anchor = actor,
                                         .follow = centered ? QA_BODY_FOLLOW_CENTER
                                                            : QA_BODY_FOLLOW_TRANSLATION,
                                         .offset = qa_vec_sub(body.origin, target.origin)};
        if (!qa_world_body_write(g->services.world, hook->id, &body, error) ||
            !qa_world_attach(g->services.world, hook->id, &attachment, error))
            return false;
        if (g->options.edition == QA_Q1_CLASSIC &&
            !q1_sound(g, player->id, "weapons/chain2.wav", 1, 1, error))
            return false;
        hook->state.projectile.damage = 2;
    } else {
        if (!target_player) {
            body.velocity = qa_v3(0, 0, 0);
            hook->physics.angular_velocity = qa_v3(0, 0, 0);
        }
        if (!qa_world_body_write(g->services.world, hook->id, &body, error))
            return false;
        hook->physics.solid = QA_PHYSICS_NOT_SOLID;
        qa_body_state owner;
        if (!qa_world_body_read(g->services.world, player->id, &owner, error))
            return false;
        owner.ground = (qa_actor_reference){0};
        if (!qa_world_body_write(g->services.world, player->id, &owner, error))
            return false;
        if (!g->services.motion_changed) {
            qa_error_set(error, QA_ERROR_ARGUMENT, player->id.slot,
                         "grapple anchor needs movement continuation");
            return false;
        }
        qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_LAUNCH, .body = owner};
        if (!g->services.motion_changed(g->services.context, player->id, &change, error))
            return false;
    }
    hook->state.projectile.count = 1;
    hook->state.projectile.enemy = q1_ref_from(g, actor);
    if (!q1_link(g, hook, error))
        return false;
    return q1_schedule(g, hook, ctf ? 0.1 : 0, Q1_THINK_HOOK_TRACK, error);
}
bool q1_grapple_think(qa_q1_game *g, q1_actor *hook, q1_think_kind kind, qa_error *error) {
    if (kind == Q1_THINK_HOOK_LINK)
        return position_link(g, hook, error);
    q1_player *player = q1_player_get(g, q1_ref_actor(g, hook->owner));
    if (kind == Q1_THINK_HOOK_RESET || !player)
        return reset(g, hook, error);
    if (kind == Q1_THINK_HOOK_FLY)
        return g->time >= hook->state.projectile.expires || player->grapple_release
                   ? reset(g, hook, error)
                   : q1_schedule(g, hook, 0.1, kind, error);
    qa_actor_id enemy = q1_ref_actor(g, hook->state.projectile.enemy);
    bool ctf = threewave(hook), solid, centered, target_player;
    qa_error local = {0};
    qa_actor_id hook_id = hook->id, player_id = player->id;
    bool anchor = anchor_info(g, enemy, &solid, &centered, &target_player, &local);
    if (local.code != QA_OK) { if (error) *error = local; return false; }
    hook = q1_entity(g, hook_id);
    player = q1_player_get(g, player_id);
    if (!hook) return true;
    if (!player || !anchor ||
        q1_health(g, player->id) <= 0 || (target_player && q1_health(g, enemy) <= 0))
        return reset(g, hook, error);
    if (ctf) {
        player->grapple_pulling = true;
        if (player->grapple_release || player->grapple_input.teleport_until > g->time || !solid)
            return reset(g, hook, error);
    }
    qa_body_state target, body;
    if (!qa_world_body_read(g->services.world, enemy, &target, error) ||
        !qa_world_body_read(g->services.world, hook->id, &body, error))
        return false;
    bool pulse = target_player;
    if (ctf && !allowed(g, hook, enemy, true, error, &pulse))
        return false;
    if (ctf)
        pulse = pulse && q1_damageable(g, enemy);
    if (pulse) {
        if (ctf) {
            bool visible;
            if (!q1_can_damage(g, enemy, player->id, &visible, error))
                return false;
            if (!visible)
                return reset(g, hook, error);
        } else {
            q1_player *victim = q1_player_get(g, enemy);
            if (victim && victim->input.teleport_until > g->time)
                return reset(g, hook, error);
            body.origin = target.origin;
            if (!qa_world_body_write(g->services.world, hook->id, &body, error))
                return false;
        }
        if (!q1_sound(g, hook->id, ctf ? "blob/land1.wav" : "pendulum/hit.wav", 1, 1, error) ||
            !q1_damage(g, enemy, hook->id, player->id, 1, hook->state.projectile.weapon, error))
            return false;
        if (!q1_alive(g, hook->id) || !q1_alive(g, enemy) || !q1_alive(g, player->id))
            return reset(g, hook, error);
        float x = q1_random(g), y = q1_random(g), z = q1_random(g);
        qa_builtin_event event = {
            .kind = QA_BUILTIN_IMPACT,
            .family = QA_GAME_Q1,
            .provider = g->options.provider,
            .actor = enemy,
            .origin = ctf ? body.origin : target.origin,
            .time_ns = g->time_ns,
            .direction = ctf ? qa_v3((x * 2 - 1) * 10, (y * 2 - 1) * 10, (z * 2 - 1) * 10 + 5)
                             : qa_v3(0, 0, 0),
            .code = ctf ? 73 : 1,
            .count = ctf ? 40 : 20};
        if (!qa_builtin_emit(&g->services, &event, error))
            return false;
        if (!q1_alive(g, hook->id) || !q1_alive(g, player->id))
            return reset(g, hook, error);
    }
    if (centered) {
        body.origin = qa_vec_add(
            target.origin, qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), 0.5f));
        body.velocity = qa_v3(0, 0, 0);
    } else
        body.velocity = target.velocity;
    if (!qa_world_body_write(g->services.world, hook->id, &body, error))
        return false;
    if (ctf) {
        qa_body_state owner;
        if (!qa_world_body_read(g->services.world, player->id, &owner, error))
            return false;
        float traveled = qa_vec_length(qa_vec_sub(owner.origin, hook->state.projectile.right));
        float style = hook->state.projectile.damage;
        const char *sound = traveled > 10 && style == 3   ? "weapons/chain2.wav"
                            : traveled < 10 && style == 2 ? "weapons/chain3.wav"
                                                          : NULL;
        if (sound) {
            hook->state.projectile.damage = style == 2 ? 3 : 2;
            if (g->options.edition == QA_Q1_CLASSIC && !q1_sound(g, player->id, sound, 1, 1, error))
                return false;
        }
        hook->state.projectile.right = owner.origin;
        if (!pull_velocity(g, hook, player, true, error) || !q1_link(g, hook, error))
            return false;
    }
    return q1_schedule(g, hook, 0.1, Q1_THINK_HOOK_TRACK, error);
}
bool q1_grapple_frame(qa_q1_game *g, q1_player *player, qa_error *error) {
    q1_actor *hook = q1_entity(g, q1_ref_actor(g, player->hook));
    if (!hook)
        return true;
    bool ctf = threewave(hook);
    if (!ctf && hook->state.projectile.count) {
        player->grapple_pulling = true;
        if (player->grapple_release || player->grapple_input.teleport_until > g->time)
            return reset(g, hook, error);
        if (!pull_velocity(g, hook, player, false, error))
            return false;
    }
    if (!q1_alive(g, hook->id) || !q1_alive(g, player->id))
        return true;
    if (ctf && g->options.edition == QA_Q1_CLASSIC)
        return true;
    qa_body_state body, head;
    if (!qa_world_body_read(g->services.world, player->id, &body, error) ||
        !qa_world_body_read(g->services.world, hook->id, &head, error))
        return false;
    if (!ctf && qa_vec_length(qa_vec_sub(head.origin, body.origin)) <= 50)
        return true;
    qa_vec3 start = head.origin;
    if (ctf) {
        qa_vec3 forward;
        qa_builtin_angle_vectors(head.angles, &forward, NULL, NULL);
        qa_vec3 offset = qa_vec_scale(forward, -7);
        offset.z = -offset.z;
        start = qa_vec_add(start, offset);
    }
    qa_builtin_event beam = {.kind = QA_BUILTIN_BEAM,
                             .family = QA_GAME_Q1,
                             .provider = g->options.provider,
                             .actor = hook->id,
                             .origin = start,
                             .end = qa_vec_add(body.origin, qa_v3(0, 0, 16)),
                             .time_ns = g->time_ns,
                             .code = 6};
    if (!q1_alive(g, hook->id) || !q1_alive(g, player->id))
        return true;
    return qa_builtin_emit(&g->services, &beam, error);
}
