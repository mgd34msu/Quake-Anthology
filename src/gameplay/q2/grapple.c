#include "internal.h"

typedef enum q2_anchor {
    ANCHOR_NONE,
    ANCHOR_BOX,
    ANCHOR_BRUSH,
    ANCHOR_WORLD,
    ANCHOR_PLAYER,
    ANCHOR_CORPSE
} q2_anchor;
static q2_actor *find(qa_q2_game *g, qa_actor_id id) {
    return q2_actor_live(g, id) && id.slot < g->capacity && g->actors[id.slot] != NULL &&
                   qa_actor_id_equal(g->actors[id.slot]->id, id)
               ? g->actors[id.slot]
               : NULL;
}
static qa_q2_grapple_kind kind_of(const q2_actor *hook) {
    return hook->projectile.kind == Q2_LMCTF_HOOK ? QA_Q2_LMCTF_GRAPPLE : QA_Q2_CTF_GRAPPLE;
}
static qa_q2_weapon weapon_of(qa_q2_grapple_kind kind) {
    return kind == QA_Q2_LMCTF_GRAPPLE ? QA_Q2_LMCTF_HOOK : QA_Q2_GRAPPLE;
}
static bool pose(qa_q2_game *g, q2_actor *a, qa_q2_grapple_kind kind, qa_q2_grapple_pose *out,
                 qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    *out = (qa_q2_grapple_pose){.angles = a->input.angles,
                                .hand = a->input.hand,
                                .view_height = a->input.view_height,
                                .gravity_scale = 1,
                                .gravity_direction = {0, 0, -1},
                                .previous_velocity = body.velocity};
    if (g->hooks.grapple_pose != NULL &&
        !g->hooks.grapple_pose(g->hooks.context, a->id, kind, out, e))
        return false;
    if ((unsigned)out->hand > QA_Q2_CENTER_HAND || !qa_vec_finite(out->angles) ||
        !qa_vec_finite(out->gravity_direction) || !qa_vec_finite(out->previous_velocity) ||
        !isfinite(out->view_height) || !isfinite(out->gravity_scale)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid selected grapple pose");
        return false;
    }
    return true;
}
static bool motion(qa_q2_game *g, qa_actor_id id, const qa_q2_grapple_motion *change, qa_error *e) {
    return g->hooks.grapple_motion == NULL ||
           g->hooks.grapple_motion(g->hooks.context, id, change, e);
}
static bool velocity(qa_q2_game *g, qa_actor_id id, qa_vec3 v, qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    body.velocity = v;
    return qa_world_body_write(g->services.world, id, &body, e) &&
           qa_world_link(g->services.world, id, NULL, e);
}
static bool sound(qa_q2_game *g, qa_actor_id id, qa_actor_id owner, const char *path, int channel,
                  float volume, bool reliable, qa_error *e) {
    if (!q2_actor_live(g, id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    q2_actor *a = find(g, owner);
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = id,
                              .time_ns = g->now_ns,
                              .origin = body.origin,
                              .channel = channel,
                              .volume =
                                  volume < 0 ? (a != NULL && a->silencer > 0 ? 0.2f : 1) : volume,
                              .attenuation = 1,
                              .flags = reliable ? 2u : 0u};
    return qa_builtin_resource(&g->services, path, &event.resource, e) &&
           qa_builtin_emit(&g->services, &event, e);
}
static bool loop(qa_q2_game *g, q2_actor *hook, const char *path, qa_error *e) {
    return q2_projectile_loop(g, hook, path, true, e);
}
static q2_anchor anchor(qa_q2_game *g, qa_actor_id id, qa_q2_grapple_kind kind, qa_error *error) {
    if (!q2_actor_live(g, id))
        return ANCHOR_NONE;
    if (g->services.physics != NULL && qa_actor_id_equal(id, g->services.physics->world_actor))
        return ANCHOR_WORLD;
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits != NULL)
        g->services.actor_traits(g->services.context, id, &traits);
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
    if (!record)
        return ANCHOR_NONE;
    const char *name =
        qa_strings_cstr(qa_session_strings(g->services.session),
                        traits.classname != 0 ? traits.classname : record->definition);
    if (name != NULL && strcmp(name, "bodyque") == 0)
        return ANCHOR_CORPSE;
    if (kind == QA_Q2_LMCTF_GRAPPLE && name != NULL &&
        (strncmp(name, "info_flag", 9) == 0 || strncmp(name, "func", 4) == 0))
        return ANCHOR_BRUSH;
    qa_physics_properties physical;
    if (g->services.physics != NULL && g->services.physics->services.read != NULL &&
        g->services.physics->services.read(g->services.physics->services.context, id, &physical)) {
        if (physical.solid == QA_PHYSICS_NOT_SOLID)
            return ANCHOR_NONE;
        if (traits.player)
            return ANCHOR_PLAYER;
        if (physical.solid == QA_PHYSICS_BOX)
            return ANCHOR_BOX;
        if (physical.solid == QA_PHYSICS_BRUSH)
            return ANCHOR_BRUSH;
        if (physical.solid == QA_PHYSICS_CORPSE)
            return ANCHOR_CORPSE;
        return ANCHOR_NONE;
    }
    qa_actor_collision collision;
    if (!qa_world_get_collision(g->services.world, id, &collision, error))
        return ANCHOR_NONE;
    return traits.player                            ? ANCHOR_PLAYER
           : collision.inline_model                 ? ANCHOR_BRUSH
           : collision.role == QA_COLLISION_TRIGGER ? ANCHOR_NONE
                                                    : ANCHOR_BOX;
}
static bool dead(qa_q2_game *g, qa_actor_id id) {
    if (!q2_actor_live(g, id))
        return true;
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits != NULL)
        g->services.actor_traits(g->services.context, id, &traits);
    qa_combat_state state;
    qa_error ignored = {0};
    return qa_combat_read(g->services.combat, id, &state, &ignored) &&
           (state.can_take_damage || traits.player) && state.health <= 0;
}
static bool restore_knockback(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_q2_grapple_state *s = &a->grapples[QA_Q2_CTF_GRAPPLE];
    if (!s->has_saved_no_knockback)
        return true;
    bool previous = s->saved_no_knockback;
    s->has_saved_no_knockback = false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, a->id, &combat, e))
        return false;
    combat.no_knockback = previous;
    return qa_combat_set_traits(g->services.combat, a->id, &combat, e);
}
static bool clear_owner(qa_q2_game *g, q2_actor *a, qa_q2_grapple_kind kind, qa_error *e) {
    qa_q2_grapple_state *s = &a->grapples[kind];
    s->hook = (qa_actor_id){0};
    s->phase = QA_Q2_GRAPPLE_FLY;
    s->hook_state = 0;
    s->hook_length = 0;
    if (kind == QA_Q2_CTF_GRAPPLE) {
        s->release_ns = q2_deadline(g->now_ns, g->options.edition == QA_Q2_RERELEASE ? Q2_NS : 0);
        if (!restore_knockback(g, a, e))
            return false;
        qa_q2_grapple_motion change = {.set_prediction = true, .prediction_suppressed = false};
        return g->options.edition != QA_Q2_CLASSIC || !q2_actor_live(g, a->id) ||
               motion(g, a->id, &change, e);
    }
    if (s->equipment_bound && s->equipment.phase == QA_Q2_FIRING)
        s->equipment.phase = QA_Q2_READY;
    if (a->weapon_bound && a->weapon.weapon == QA_Q2_LMCTF_HOOK && a->weapon.phase == QA_Q2_FIRING)
        a->weapon.phase = QA_Q2_READY;
    return true;
}
bool qa_q2_grapple_reset(qa_q2_game *g, qa_actor_id id, qa_q2_grapple_kind kind, qa_error *e) {
    if (g == NULL || (unsigned)kind > QA_Q2_LMCTF_GRAPPLE) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 grapple kind");
        return false;
    }
    q2_actor *a = find(g, id);
    if (a == NULL)
        return true;
    qa_q2_grapple_state *s = &a->grapples[kind];
    qa_actor_id hook_id = s->hook;
    if (kind == QA_Q2_LMCTF_GRAPPLE) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, e))
            return false;
        if (body.ground.registry != 0) {
            qa_q2_grapple_pose p;
            if (!pose(g, a, kind, &p, e))
                return false;
            p.previous_velocity.z = 0;
            body.velocity.z = 0;
            qa_q2_grapple_motion change = {.set_previous_velocity = true,
                                           .previous_velocity = p.previous_velocity};
            if (!motion(g, id, &change, e) || !velocity(g, id, body.velocity, e))
                return false;
        }
    } else if (q2_actor_live(g, hook_id)) {
        if (!sound(g, id, id, "weapons/grapple/grreset.wav", 1, -1,
                   g->options.edition == QA_Q2_CLASSIC, e))
            return false;
        if (!q2_actor_live(g, id) || !qa_actor_id_equal(s->hook, hook_id))
            return true;
    } else if (hook_id.registry == 0)
        return true;
    if (!clear_owner(g, a, kind, e))
        return false;
    q2_actor *hook = find(g, hook_id);
    if (hook == NULL)
        return true;
    if (kind == QA_Q2_CTF_GRAPPLE && !loop(g, hook, "", e))
        return false;
    if (!q2_actor_live(g, hook_id))
        return true;
    hook->projectile.enemy = (qa_actor_id){0};
    return qa_world_detach(g->services.world, hook_id, e) &&
           qa_session_release(g->services.session, hook_id, e);
}
bool q2_grapple_released(qa_q2_game *g, q2_actor *a, qa_error *e) {
    bool ok = true;
    for (unsigned i = 0; i < 2; ++i) {
        qa_actor_id child = a->grapples[i].hook;
        a->grapples[i].hook = (qa_actor_id){0};
        q2_actor *hook = find(g, child);
        if (hook == NULL)
            continue;
        qa_error local = {0};
        if (i == QA_Q2_CTF_GRAPPLE && !loop(g, hook, "", &local)) {
            if (ok && e != NULL)
                *e = local;
            ok = false;
        }
        if (q2_actor_live(g, child) && !qa_session_release(g->services.session, child, &local)) {
            if (ok && e != NULL)
                *e = local;
            ok = false;
        }
    }
    if (a->projectile.kind == Q2_CTF_HOOK || a->projectile.kind == Q2_LMCTF_HOOK) {
        q2_actor *owner = find(g, a->projectile.owner);
        qa_q2_grapple_kind kind = kind_of(a);
        if (owner != NULL && qa_actor_id_equal(owner->grapples[kind].hook, a->id)) {
            qa_error local = {0};
            if (!clear_owner(g, owner, kind, &local)) {
                if (ok && e != NULL)
                    *e = local;
                ok = false;
            }
        }
    }
    return ok;
}
static bool reset_hook(qa_q2_game *g, q2_actor *hook, qa_error *e) {
    q2_actor *owner = find(g, hook->projectile.owner);
    return owner == NULL ? qa_session_release(g->services.session, hook->id, e)
                         : qa_q2_grapple_reset(g, owner->id, kind_of(hook), e);
}
static bool attach(qa_q2_game *g, q2_actor *hook, qa_actor_id target, q2_anchor classification,
                   qa_error *e) {
    qa_body_state body, other;
    if (!qa_world_body_read(g->services.world, hook->id, &body, e) ||
        !qa_world_body_read(g->services.world, target, &other, e))
        return false;
    bool lm = hook->projectile.kind == Q2_LMCTF_HOOK;
    qa_body_attachment attachment = {.anchor = target};
    if (lm) {
        attachment.follow = QA_BODY_FOLLOW_BOUNDS_MIN;
        attachment.offset = qa_vec_sub(body.origin, qa_vec_add(other.origin, other.bounds.mins));
    } else if (classification == ANCHOR_BOX || classification == ANCHOR_PLAYER ||
               classification == ANCHOR_CORPSE)
        attachment.follow = QA_BODY_FOLLOW_CENTER;
    else {
        attachment.follow = QA_BODY_FOLLOW_TRANSLATION;
        attachment.offset = qa_vec_sub(body.origin, other.origin);
    }
    hook->projectile.enemy = target;
    hook->physics.solid = lm ? QA_PHYSICS_TRIGGER : QA_PHYSICS_NOT_SOLID;
    qa_actor_collision collision;
    qa_error observed = {0};
    bool has_collision = lm && qa_world_get_collision(g->services.world, hook->id, &collision,
                                                       &observed);
    if (observed.code) {
        if (e)
            *e = observed;
        return false;
    }
    if (has_collision) {
        collision.role = QA_COLLISION_TRIGGER;
        if (!qa_world_set_collision(g->services.world, hook->id, &collision, e))
            return false;
    } else if (!lm && !qa_world_set_collision(g->services.world, hook->id, NULL, e))
        return false;
    return qa_world_attach(g->services.world, hook->id, &attachment, e);
}
bool q2_grapple_touch(qa_q2_game *g, const qa_touch_contact *contact, qa_error *e) {
    q2_actor *hook = find(g, contact->self);
    if (hook == NULL)
        return true;
    qa_actor_id owner_id = hook->projectile.owner, hook_id = hook->id;
    q2_actor *owner = find(g, owner_id);
    if (owner == NULL)
        return reset_hook(g, hook, e);
    qa_q2_grapple_kind kind = kind_of(hook);
    qa_q2_grapple_state *s = &owner->grapples[kind];
    bool lm = kind == QA_Q2_LMCTF_GRAPPLE;
    if (qa_actor_id_equal(contact->other, owner_id) || (!lm && s->phase != QA_Q2_GRAPPLE_FLY) ||
        (lm && hook->projectile.enemy.registry != 0 &&
         !qa_actor_id_equal(hook->projectile.enemy, contact->other)))
        return true;
    qa_error observed = {0};
    q2_anchor classification = anchor(g, contact->other, kind, &observed);
    if (observed.code) {
        if (e)
            *e = observed;
        return false;
    }
    if (!q2_actor_live(g, hook_id))
        return true;
    if ((contact->has_surface && (contact->surface.flags & 4u) != 0) ||
        (lm && (classification == ANCHOR_NONE || classification == ANCHOR_BOX ||
                dead(g, contact->other) ||
                (g->hooks.grapple_can_attach != NULL &&
                 !g->hooks.grapple_can_attach(g->hooks.context, owner_id, contact->other, kind)))))
        return reset_hook(g, hook, e);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, hook_id, &body, e))
        return false;
    body.velocity = qa_v3(0, 0, 0);
    if (!qa_world_body_write(g->services.world, hook_id, &body, e))
        return false;
    qa_vec3 normal = contact->has_plane ? contact->plane.normal : qa_v3(0, 0, 0);
    if (!lm) {
        if (!q2_noise_for_actor(g, owner_id, body.origin, true, e))
            return false;
        if (!q2_actor_live(g, hook_id) || !q2_actor_live(g, owner_id))
            return true;
        if (q2_target_damageable(g, contact->other)) {
            qa_attack attack = q2_projectile_attack(g, hook_id, &hook->projectile, 56, 0);
            if ((g->options.edition == QA_Q2_CLASSIC || hook->projectile.damage != 0) &&
                !q2_damage(g, &attack, contact->other, hook->projectile.damage, 1, body.velocity,
                           body.origin, normal, false, e))
                return false;
            return !q2_actor_live(g, hook_id) || !q2_actor_live(g, owner_id) ||
                   reset_hook(g, hook, e);
        }
        s->phase = QA_Q2_GRAPPLE_PULL;
        if (classification == ANCHOR_NONE)
            return reset_hook(g, hook, e);
        if (!attach(g, hook, contact->other, classification, e))
            return false;
        if (g->options.edition == QA_Q2_CLASSIC &&
            !sound(g, owner_id, owner_id, "weapons/grapple/grpull.wav", 1, -1, true, e))
            return false;
        if (!q2_actor_live(g, hook_id))
            return true;
        if (!sound(g, hook_id, owner_id, "weapons/grapple/grhit.wav", 1, -1, false, e))
            return false;
        if (!q2_actor_live(g, hook_id))
            return true;
        if (g->options.edition == QA_Q2_RERELEASE &&
            !loop(g, hook, "weapons/grapple/grpull.wav", e))
            return false;
    } else {
        s->hook_state = 2;
        if (g->hooks.grapple_can_damage == NULL ||
            g->hooks.grapple_can_damage(g->hooks.context, owner_id, contact->other, kind)) {
            uint64_t frame =
                g->now_ns / (100 * Q2_MS) + (g->now_ns % (100 * Q2_MS) >= 50 * Q2_MS ? 1u : 0u);
            bool repeated = qa_actor_id_equal(hook->projectile.enemy, contact->other);
            if (!repeated || (frame % 7 == 0 && frame != hook->projectile.effect_ns)) {
                bool player = false;
                if (!q2_target_creature(g, contact->other, NULL, &player, e))
                    return false;
                if (g->hooks.grapple_player_hit != NULL)
                    player = g->hooks.grapple_player_hit(g->hooks.context, contact->other);
                if (player &&
                    !sound(g, hook_id, owner_id,
                           repeated ? "weapons/grapple/gkilling.wav" : "weapons/grapple/ghit.wav",
                           0, 1, false, e))
                    return false;
                if (!player && !repeated &&
                    !sound(g, hook_id, owner_id, "weapons/grapple/ghitwall.wav", 0, 0.8f, false, e))
                    return false;
                if (!q2_actor_live(g, hook_id) || !q2_actor_live(g, owner_id))
                    return true;
                if (q2_target_damageable(g, contact->other)) {
                    qa_attack attack = q2_projectile_attack(g, hook_id, &hook->projectile, 60, 4);
                    float amount = repeated ? 1 : 8;
                    if (!q2_damage(g, &attack, contact->other, amount, amount, qa_v3(0, 0, 0),
                                   body.origin, normal, false, e))
                        return false;
                    if (!q2_actor_live(g, hook_id) || !q2_actor_live(g, owner_id))
                        return true;
                }
                if (repeated)
                    hook->projectile.effect_ns = frame;
            }
        }
        if (dead(g, contact->other))
            return reset_hook(g, hook, e);
        if (hook->projectile.enemy.registry == 0 &&
            !attach(g, hook, contact->other, classification, e))
            return false;
    }
    return !q2_actor_live(g, hook_id) ||
           q2_projectile_event(g, hook_id, QA_BUILTIN_IMPACT, lm ? "q2:blaster" : "q2:sparks", 0,
                               body.origin, normal, e);
}
static bool cable(qa_q2_game *g, qa_actor_id owner, qa_vec3 start, qa_vec3 end, qa_vec3 offset,
                  qa_error *e) {
    qa_builtin_event event = {.kind = QA_BUILTIN_BEAM,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = owner,
                              .time_ns = g->now_ns,
                              .origin = start,
                              .end = end,
                              .direction = offset};
    return qa_builtin_resource(&g->services, "q2:grapple-cable", &event.resource, e) &&
           qa_builtin_emit(&g->services, &event, e);
}
static bool draw_lm(qa_q2_game *g, qa_actor_id owner, qa_vec3 start, qa_vec3 end, qa_error *e) {
    return qa_vec_length(qa_vec_sub(end, start)) <= 64 ||
           cable(g, owner, start, end, qa_v3(0, 0, 0), e);
}
static bool launch(qa_q2_game *g, q2_actor *owner, qa_q2_grapple_kind kind, qa_vec3 start,
                   qa_vec3 direction, bool *launched, qa_error *e) {
    *launched = false;
    if (g->services.physics == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 grapple requires shared physics");
        return false;
    }
    qa_q2_grapple_state *s = &owner->grapples[kind];
    if (s->hook.registry != 0)
        return true;
    bool lm = kind == QA_Q2_LMCTF_GRAPPLE, rr = !lm && g->options.edition == QA_Q2_RERELEASE;
    float speed = lm ? 800 : rr ? g->grapple_options.fly_speed : 650;
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, lm ? "noclass" : "grapple", &definition, e))
        return false;
    direction = qa_vec_normalize(direction);
    qa_actor_collision collision = {.family = QA_COLLISION_Q2,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = 2,
                                    .owner = owner->id,
                                    .role = QA_COLLISION_SOLID};
    qa_combat_state combat = {
        .health = lm ? 59 : 0, .can_take_damage = lm || rr, .no_knockback = rr};
    qa_builtin_spawn spawn = {
        .owner = g->options.owner,
        .definition = definition,
        .collision = &collision,
        .combat = (lm || rr) ? &combat : NULL,
        .body = {
            .origin = start,
            .velocity = qa_vec_scale(direction, speed),
            .angles = {-atan2f(direction.z, hypotf(direction.x, direction.y)) * 57.29577951308232f +
                           (lm ? 90 : 0),
                       atan2f(direction.y, direction.x) * 57.29577951308232f, 0}}};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    q2_actor *hook = q2_actor_get(g, id, true, e);
    if (hook == NULL)
        return false;
    qa_q2_weapon weapon = weapon_of(kind);
    q2_weapon_call call = {.game = g,
                           .actor = owner,
                           .state = &owner->weapon,
                           .input = owner->input,
                           .definition = &g->definitions[weapon],
                           .now_ns = g->now_ns,
                           .frame_ns = g->frame_ns,
                           .rerelease = rr};
    hook->projectile = (q2_projectile){.kind = lm ? Q2_LMCTF_HOOK : Q2_CTF_HOOK,
                                       .owner = owner->id,
                                       .attack = q2_attack(&call, lm ? 60 : 56, lm ? 4 : 0),
                                       .damage = lm   ? 2
                                                 : rr ? g->grapple_options.damage
                                                      : 10,
                                       .speed = speed,
                                       .visible = true,
                                       .scale = 1,
                                       .born_ns = g->now_ns,
                                       .expire_ns = UINT64_MAX,
                                       .next_ns = lm ? q2_deadline(g->now_ns, Q2_NS) : UINT64_MAX};
    hook->character_no_damage_effects = rr;
    hook->physics_bound = true;
    hook->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    hook->physics.motion = QA_PHYSICS_FLY_MISSILE;
    hook->physics.solid = QA_PHYSICS_BOX;
    hook->physics.q2_rerelease = rr;
    hook->physics.clip_mask =
        rr ? (g->grapple_options.players_collide ? Q2_PROJECTILE_MASK
                                                 : Q2_PROJECTILE_MASK & ~Q2_PLAYER_CONTENTS)
           : Q2_SHOT_MASK;
    s->hook = id;
    s->phase = QA_Q2_GRAPPLE_FLY;
    if (lm && !sound(g, owner->id, owner->id, "weapons/grapple/grfire.wav", 0, 0.8f, false, e))
        return false;
    if (!q2_actor_live(g, id) || !q2_actor_live(g, owner->id))
        return true;
    if (!q2_launch_behavior(g, hook, QA_BUILTIN_GRAPPLE, NULL, e))
        return false;
    if (!q2_actor_live(g, id) || !q2_actor_live(g, owner->id))
        return true;
    qa_body_state body, player;
    if (!qa_world_body_read(g->services.world, id, &body, e) ||
        !qa_world_body_read(g->services.world, owner->id, &player, e))
        return false;
    if (!qa_world_link(g->services.world, id, NULL, e) ||
        !q2_projectile_event(g, id, QA_BUILTIN_ANIMATION,
                             lm ? "models/objects/ghook/tris.md2"
                                : "models/weapons/grapple/hook/tris.md2",
                             0, body.origin, body.angles, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    qa_trace_query query = {.start = player.origin,
                            .end = body.origin,
                            .pass_actor = lm ? owner->id : id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = hook->physics.clip_mask;
    qa_trace_result trace;
    if (!qa_world_trace(g->services.world, &query, &trace, e))
        return false;
    if (trace.fraction < 1) {
        body.origin =
            rr ? qa_vec_add(trace.end, trace.contact_plane.normal)
               : qa_vec_add(body.origin, qa_vec_scale(qa_vec_normalize(body.velocity), -10));
        if (!qa_world_body_write(g->services.world, id, &body, e) ||
            !qa_world_link(g->services.world, id, NULL, e))
            return false;
        qa_touch_contact contact = {.self = id,
                                    .other = trace.hit == QA_TRACE_HIT_ACTOR
                                                 ? trace.actor
                                                 : g->services.physics->world_actor,
                                    .has_plane = rr && trace.contact,
                                    .plane = trace.contact_plane,
                                    .has_surface = rr && trace.has_surface,
                                    .surface = trace.surface};
        return q2_grapple_touch(g, &contact, e);
    }
    *launched = true;
    return !rr || loop(g, hook, "weapons/grapple/grfly.wav", e);
}
static bool fire_ctf(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (a->grapples[QA_Q2_CTF_GRAPPLE].phase != QA_Q2_GRAPPLE_FLY)
        return true;
    qa_q2_grapple_pose p;
    if (!pose(g, a, QA_Q2_CTF_GRAPPLE, &p, e))
        return false;
    q2_weapon_call call = {.game = g,
                           .actor = a,
                           .state = &a->weapon,
                           .input = a->input,
                           .rerelease = g->options.edition == QA_Q2_RERELEASE};
    call.input.hand = p.hand;
    call.input.view_height = p.view_height;
    call.input.players_collide = g->grapple_options.players_collide;
    qa_vec3 start, direction;
    if (!q2_project(&call, p.angles, qa_v3(24, 8, -6), &start, &direction, e))
        return false;
    if (!call.rerelease && !sound(g, a->id, a->id, "weapons/grapple/grfire.wav", 1, -1, true, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    bool launched;
    if (!launch(g, a, QA_Q2_CTF_GRAPPLE, start, direction, &launched, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (call.rerelease && launched &&
        !sound(g, a->id, a->id, "weapons/grapple/grfire.wav", 1, -1, false, e))
        return false;
    call.now_ns = g->now_ns;
    return !q2_actor_live(g, a->id) || q2_noise(&call, start, e);
}
static bool fire_lm(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_q2_grapple_state *s = &a->grapples[QA_Q2_LMCTF_GRAPPLE];
    qa_q2_grapple_pose p;
    qa_body_state body;
    if (!pose(g, a, QA_Q2_LMCTF_GRAPPLE, &p, e) ||
        !qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_vec3 forward, right;
    qa_builtin_angle_vectors(p.angles, &forward, &right, NULL);
    float side = p.hand == QA_Q2_LEFT_HAND ? -8 : p.hand == QA_Q2_CENTER_HAND ? 0 : 8;
    qa_vec3 start = qa_vec_add(
        qa_vec_add(qa_vec_add(body.origin, qa_vec_scale(forward, 8)), qa_vec_scale(right, side)),
        qa_v3(0, 0, p.view_height - 8));
    bool created = s->hook_state == 0;
    if (created) {
        s->hook_state = 1;
        bool launched;
        if (!launch(g, a, QA_Q2_LMCTF_GRAPPLE, start, forward, &launched, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    q2_actor *hook = find(g, s->hook);
    if (hook == NULL) {
        s->hook = (qa_actor_id){0};
        s->hook_state = 0;
        return true;
    }
    qa_body_state hook_body;
    if (!qa_world_body_read(g->services.world, hook->id, &hook_body, e))
        return false;
    if (created)
        return draw_lm(g, a->id, start, hook_body.origin, e) &&
               (!q2_actor_live(g, a->id) || !q2_actor_live(g, hook->id) ||
                draw_lm(g, a->id, start, hook_body.origin, e));
    if (s->hook_state == 1)
        return draw_lm(g, a->id, start, hook_body.origin, e);
    qa_body_attachment attachment;
    if (qa_world_attachment(g->services.world, hook->id, &attachment) &&
        q2_actor_live(g, attachment.anchor)) {
        qa_body_state target;
        if (!qa_world_body_read(g->services.world, attachment.anchor, &target, e))
            return false;
        hook_body.origin =
            qa_vec_add(qa_vec_add(target.origin, target.bounds.mins), attachment.offset);
        if (!qa_world_body_write(g->services.world, hook->id, &hook_body, e) ||
            !qa_world_link(g->services.world, hook->id, NULL, e))
            return false;
    }
    if (!draw_lm(g, a->id, start, hook_body.origin, e))
        return false;
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, hook->id))
        return true;
    qa_vec3 delta = qa_vec_sub(hook_body.origin, start);
    float distance = truncf(qa_vec_length(delta));
    s->hook_length = distance >= INT_MAX ? INT_MAX : (int)distance;
    float speed = distance > 120   ? 800
                  : distance > 100 ? distance * 5
                  : distance > 80  ? distance * 4
                  : distance > 40  ? distance * 3
                  : distance > 20  ? distance * 2
                  : distance > 10  ? distance
                                   : 1;
    qa_vec3 v = qa_vec_scale(qa_vec_normalize(delta), speed);
    qa_q2_grapple_motion change = {.set_previous_velocity = true, .previous_velocity = v};
    return velocity(g, a->id, v, e) && motion(g, a->id, &change, e);
}
bool q2_grapple_fire(q2_weapon_call *c, qa_error *e) {
    bool lm = c->definition->weapon == QA_Q2_LMCTF_HOOK;
    qa_q2_grapple_state *s = &c->actor->grapples[lm ? QA_Q2_LMCTF_GRAPPLE : QA_Q2_CTF_GRAPPLE];
    if (!lm && s->phase != QA_Q2_GRAPPLE_FLY) {
        if (!c->rerelease)
            ++c->state->frame;
        return true;
    }
    c->state->source_firing = !lm || s->hook_state == 0;
    if ((lm && c->state->source_firing) || (!lm && !c->rerelease)) {
        qa_q2_grapple_pose p;
        qa_vec3 forward;
        if (!pose(c->game, c->actor, lm ? QA_Q2_LMCTF_GRAPPLE : QA_Q2_CTF_GRAPPLE, &p, e))
            return false;
        qa_builtin_angle_vectors(p.angles, &forward, NULL, NULL);
        qa_vec3 previous_origin, angles;
        q2_recoil(c, &previous_origin, &angles);
        angles.x = -1;
        q2_kick(c, qa_vec_scale(forward, -2), angles, c->rerelease ? 0.2f : 0);
    }
    if (!(lm ? fire_lm(c->game, c->actor, e) : fire_ctf(c->game, c->actor, e)))
        return false;
    if (!lm && !c->rerelease && q2_actor_live(c->game, c->actor->id))
        ++c->state->frame;
    return true;
}
static bool pull_ctf(qa_q2_game *g, q2_actor *a, bool damage_pulse, qa_error *e) {
    qa_q2_grapple_state *s = &a->grapples[QA_Q2_CTF_GRAPPLE];
    q2_actor *hook = find(g, s->hook);
    if (hook == NULL)
        return true;
    qa_actor_id hook_id = hook->id;
    qa_q2_grapple_pose p;
    qa_body_state hook_body;
    if (!pose(g, a, QA_Q2_CTF_GRAPPLE, &p, e) ||
        !qa_world_body_read(g->services.world, hook_id, &hook_body, e))
        return false;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    qa_actor_id target = hook->projectile.enemy;
    if (target.registry != 0) {
        qa_error observed = {0};
        q2_anchor classification = anchor(g, target, QA_Q2_CTF_GRAPPLE, &observed);
        if (observed.code) {
            if (e)
                *e = observed;
            return false;
        }
        if (!q2_actor_live(g, hook_id))
            return true;
        if (classification == ANCHOR_NONE)
            return reset_hook(g, hook, e);
        qa_body_state other;
        if (!qa_world_body_read(g->services.world, target, &other, e))
            return false;
        if (classification == ANCHOR_BOX || classification == ANCHOR_PLAYER ||
            classification == ANCHOR_CORPSE)
            hook_body.origin = qa_vec_add(
                other.origin, qa_vec_scale(qa_vec_add(other.bounds.mins, other.bounds.maxs), 0.5f));
        else
            hook_body.velocity = other.velocity;
        if (!qa_world_body_write(g->services.world, hook_id, &hook_body, e) ||
            !qa_world_link(g->services.world, hook_id, NULL, e))
            return false;
        if (!rr && damage_pulse && q2_target_damageable(g, target) &&
            (g->hooks.grapple_can_damage == NULL ||
             g->hooks.grapple_can_damage(g->hooks.context, a->id, target, QA_Q2_CTF_GRAPPLE))) {
            qa_attack attack = q2_projectile_attack(g, hook_id, &hook->projectile, 56, 0);
            if (!q2_damage(g, &attack, target, 1, 1, hook_body.velocity, hook_body.origin,
                           qa_v3(0, 0, 0), false, e))
                return false;
            if (!q2_actor_live(g, hook_id) || !q2_actor_live(g, a->id))
                return true;
            if (!sound(g, hook_id, a->id, "weapons/grapple/grhurt.wav", 1, -1, false, e))
                return false;
        }
        if (!q2_actor_live(g, hook_id) || !q2_actor_live(g, a->id))
            return true;
        if (dead(g, target))
            return reset_hook(g, hook, e);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    if (rr) {
        if (s->phase != QA_Q2_GRAPPLE_HANG) {
            q2_weapon_call call = {
                .game = g, .actor = a, .state = &a->weapon, .input = a->input, .rerelease = true};
            call.input.hand = p.hand;
            call.input.view_height = p.view_height;
            call.input.players_collide = g->grapple_options.players_collide;
            qa_vec3 start, direction;
            if (!q2_project(&call, p.angles, qa_v3(7, 2, -9), &start, &direction, e) ||
                !cable(g, a->id, start, hook_body.origin, qa_v3(0, 0, 0), e))
                return false;
        }
    } else {
        qa_vec3 forward, right;
        qa_builtin_angle_vectors(p.angles, &forward, &right, NULL);
        float side = p.hand == QA_Q2_LEFT_HAND ? -16 : p.hand == QA_Q2_CENTER_HAND ? 0 : 16;
        qa_vec3 offset =
            qa_vec_add(qa_vec_add(qa_vec_scale(forward, 16), qa_vec_scale(right, side)),
                       qa_v3(0, 0, p.view_height - 8));
        if (qa_vec_length(qa_vec_sub(qa_vec_add(body.origin, offset), hook_body.origin)) >= 64 &&
            !cable(g, a->id, body.origin, hook_body.origin, offset, e))
            return false;
    }
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, hook_id) || s->phase == QA_Q2_GRAPPLE_FLY)
        return true;
    qa_vec3 direction =
        qa_vec_sub(hook_body.origin, qa_vec_add(body.origin, qa_v3(0, 0, p.view_height)));
    if (s->phase == QA_Q2_GRAPPLE_PULL && qa_vec_length(direction) < 64) {
        s->phase = QA_Q2_GRAPPLE_HANG;
        if (rr) {
            if (!loop(g, hook, "weapons/grapple/grhang.wav", e))
                return false;
        } else {
            qa_q2_grapple_motion change = {.set_prediction = true, .prediction_suppressed = true};
            if (!motion(g, a->id, &change, e) ||
                !sound(g, a->id, a->id, "weapons/grapple/grhang.wav", 1, -1, true, e))
                return false;
        }
    }
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, hook_id))
        return true;
    if (rr) {
        qa_combat_state combat;
        if (!qa_combat_read_traits(g->services.combat, a->id, &combat, e))
            return false;
        if (!s->has_saved_no_knockback) {
            s->has_saved_no_knockback = true;
            s->saved_no_knockback = combat.no_knockback;
        }
        combat.no_knockback = true;
        if (!qa_combat_set_traits(g->services.combat, a->id, &combat, e))
            return false;
    }
    float gravity = g->services.physics->gravity;
    qa_vec3 v = qa_vec_add(
        qa_vec_scale(qa_vec_normalize(direction), rr ? g->grapple_options.pull_speed : 650),
        qa_vec_scale(p.gravity_direction,
                     p.gravity_scale * gravity * (float)((double)g->frame_ns / 1e9)));
    return velocity(g, a->id, v, e);
}
bool q2_grapple_think(qa_q2_game *g, q2_actor *hook, qa_error *e) {
    if (hook->projectile.kind != Q2_LMCTF_HOOK || hook->projectile.next_ns > g->now_ns)
        return true;
    q2_actor *owner = find(g, hook->projectile.owner);
    if (owner == NULL)
        return qa_session_release(g->services.session, hook->id, e);
    hook->projectile.next_ns = UINT64_MAX;
    if (owner->grapples[QA_Q2_LMCTF_GRAPPLE].hook_length <= 126)
        return true;
    bool flying = hook->projectile.enemy.registry == 0;
    if (!sound(g, hook->id, owner->id,
               flying ? "weapons/grapple/gflyair.wav" : "weapons/grapple/gpulling.wav", 0, 1, false,
               e))
        return false;
    if (q2_actor_live(g, hook->id))
        hook->projectile.next_ns = q2_deadline(g->now_ns, (flying ? 400 : 800) * Q2_MS);
    return true;
}
bool q2_grapple_reaction(qa_q2_game *g, q2_actor *hook, const qa_damage_outcome *outcome,
                         qa_error *e) {
    if (outcome->result.reaction != QA_REACTION_DEATH)
        return true;
    if (hook->projectile.kind == Q2_LMCTF_HOOK)
        return reset_hook(g, hook, e);
    const qa_damage_cause *cause = &outcome->request.attack.cause;
    bool crush = false;
    switch (cause->kind) {
    case QA_CAUSE_Q1: {
        const char *name =
            qa_strings_cstr(qa_session_strings(g->services.session), cause->source.q1.death_type);
        crush = name != NULL && strcmp(name, "crush") == 0;
        break;
    }
    case QA_CAUSE_Q2:
        crush = cause->source.q2.means_of_death == 20;
        break;
    case QA_CAUSE_Q3:
        crush = cause->source.q3.means_of_death == 17;
        break;
    case QA_CAUSE_ENVIRONMENT:
        crush = cause->source.hazard == QA_HAZARD_CRUSH;
        break;
    }
    return !crush || reset_hook(g, hook, e);
}
bool qa_q2_grapple_configure(qa_q2_game *g, const qa_q2_grapple_options *options, qa_error *e) {
    if (g == NULL || options == NULL || !isfinite(options->fly_speed) || options->fly_speed <= 0 ||
        !isfinite(options->pull_speed) || options->pull_speed < 0 || !isfinite(options->damage) ||
        options->damage < 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 grapple settings");
        return false;
    }
    g->grapple_options = *options;
    return true;
}
bool qa_q2_grapple_read(qa_q2_game *g, qa_actor_id id, qa_q2_grapple_kind kind,
                        qa_q2_grapple_state *out, qa_error *e) {
    if (g == NULL || out == NULL || (unsigned)kind > QA_Q2_LMCTF_GRAPPLE || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 grapple state query");
        return false;
    }
    q2_actor *a = find(g, id);
    *out = a == NULL ? (qa_q2_grapple_state){0} : a->grapples[kind];
    return true;
}
bool qa_q2_grapple_offhand(qa_q2_game *g, qa_actor_id id, qa_q2_grapple_kind kind, bool pressed,
                           qa_error *e) {
    if (g == NULL || (unsigned)kind > QA_Q2_LMCTF_GRAPPLE || g->services.physics == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 grapple requires a kind and shared physics");
        return false;
    }
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    if (!pressed)
        return qa_q2_grapple_reset(g, id, kind, e);
    if (a->grapples[kind].hook.registry != 0)
        return true;
    return kind == QA_Q2_CTF_GRAPPLE ? fire_ctf(g, a, e) : fire_lm(g, a, e);
}
bool qa_q2_grapple_after_movement(qa_q2_game *g, qa_actor_id id, bool damage_pulse, uint64_t now,
                                  uint64_t frame, qa_error *e) {
    if (g == NULL || frame == 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 grapple frame");
        return false;
    }
    q2_actor *a = find(g, id);
    if (a == NULL)
        return true;
    g->now_ns = now;
    g->frame_ns = frame;
    qa_q2_grapple_state *s = &a->grapples[QA_Q2_CTF_GRAPPLE];
    qa_q2_weapon_state *weapon =
        s->equipment_bound && s->equipment.handoff != QA_Q2_PRIMARY_HOLSTERED ? &s->equipment
        : a->weapon_bound && a->weapon.weapon == QA_Q2_GRAPPLE                ? &a->weapon
                                                                              : NULL;
    if (weapon != NULL && weapon->handoff == QA_Q2_PRIMARY_ACTIVE &&
        weapon->pending == QA_Q2_WEAPON_NONE && weapon->phase != QA_Q2_FIRING &&
        weapon->phase != QA_Q2_ACTIVATING) {
        if (!qa_q2_grapple_reset(g, id, QA_Q2_CTF_GRAPPLE, e))
            return false;
    } else if (!pull_ctf(g, a, damage_pulse, e))
        return false;
    return !q2_actor_live(g, id) || a->grapples[QA_Q2_LMCTF_GRAPPLE].hook_state == 0 ||
           fire_lm(g, a, e);
}
float qa_q2_grapple_gravity_scale(qa_q2_game *g, qa_actor_id id) {
    q2_actor *a = g == NULL ? NULL : find(g, id);
    return a != NULL && a->grapples[QA_Q2_LMCTF_GRAPPLE].hook_state == 2 &&
                   a->grapples[QA_Q2_LMCTF_GRAPPLE].hook_length < 50
               ? 0
               : 1;
}
