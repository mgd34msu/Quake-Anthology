#include "internal.h"
#include "qa/game_q2_monsters.h"

static q2_actor *companion_actor(qa_q2_game *g, qa_actor_id id) {
    q2_actor *a = id.slot < g->capacity ? g->actors[id.slot] : NULL;
    return a && qa_actor_id_equal(a->id, id) && q2_actor_live(g, id) && a->item &&
                   a->item->companion
               ? a
               : NULL;
}
static qa_vec3 angles_for(qa_vec3 direction) {
    float yaw = atan2f(direction.y, direction.x) * (180.0f / 3.14159265358979323846f);
    float pitch =
        atan2f(direction.z, sqrtf(direction.x * direction.x + direction.y * direction.y)) *
        (180.0f / 3.14159265358979323846f);
    return qa_v3(-pitch, yaw < 0 ? yaw + 360 : yaw, 0);
}
static bool set_body(qa_q2_game *g, q2_actor *a, const qa_body_state *body, qa_error *e) {
    return qa_world_body_write(g->services.world, a->id, body, e) &&
           (!q2_actor_live(g, a->id) || qa_world_link(g->services.world, a->id, NULL, e));
}
static bool loop(qa_q2_game *g, q2_actor *a, const char *path, qa_error *e) {
    q2_companion *c = a->item->companion;
    qa_string_id sound;
    if (!qa_builtin_resource(&g->services, path, &sound, e))
        return false;
    if (c->loop_sound == sound)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    if (c->loop_sound && !qa_builtin_emit(&g->services,
                                          &(qa_builtin_event){.kind = QA_BUILTIN_STOP_SOUND,
                                                              .family = QA_GAME_Q2,
                                                              .provider = g->options.owner,
                                                              .actor = a->id,
                                                              .resource = c->loop_sound,
                                                              .time_ns = g->now_ns},
                                          e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    c->loop_sound = sound;
    return qa_builtin_emit(&g->services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_SOUND,
                                               .family = QA_GAME_Q2,
                                               .provider = g->options.owner,
                                               .actor = a->id,
                                               .origin = body.origin,
                                               .resource = sound,
                                               .volume = 1,
                                               .attenuation = 1,
                                               .flags = 1,
                                               .time_ns = g->now_ns},
                           e);
}
static bool expire(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_companion *c = a->item->companion;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    if (c->camera && q2_actor_live(g, c->owner)) {
        if (!q2_client_sphere_camera(g, c->owner, (qa_actor_id){0}, body.origin, body.angles, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    if (q2_actor_live(g, c->child)) {
        q2_actor *child = companion_actor(g, c->child);
        if (child ? !expire(g, child, e) : !qa_session_release(g->services.session, c->child, e))
            return false;
    }
    if (!q2_actor_live(g, a->id))
        return true;
    if (!qa_builtin_emit(&g->services,
                         &(qa_builtin_event){.kind = QA_BUILTIN_EXPLOSION,
                                             .family = QA_GAME_Q2,
                                             .provider = g->options.owner,
                                             .actor = a->id,
                                             .origin = body.origin,
                                             .code = 1,
                                             .time_ns = g->now_ns},
                         e))
        return false;
    return !q2_actor_live(g, a->id) || qa_session_release(g->services.session, a->id, e);
}
static bool sight(qa_q2_game *g, q2_actor *a, qa_actor_id target, bool *visible, qa_error *e) {
    qa_body_state from, to;
    *visible = false;
    if (!q2_actor_live(g, target))
        return true;
    if (!qa_world_body_read(g->services.world, a->id, &from, e) ||
        !qa_world_body_read(g->services.world, target, &to, e))
        return false;
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits)
        g->services.actor_traits(g->services.context, target, &traits);
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, target))
        return true;
    to.origin.z += traits.view_height;
    qa_trace_query query = {.start = from.origin,
                            .end = to.origin,
                            .shape = {.kind = QA_SHAPE_POINT},
                            .pass_actor = a->id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = 25;
    qa_trace_result trace;
    if (!qa_world_trace(g->services.world, &query, &trace, e))
        return false;
    *visible = trace.fraction == 1;
    return true;
}
static bool launch(qa_q2_game *g, qa_actor_id owner, q2_companion_kind kind, bool decoy,
                   qa_actor_id credit, qa_actor_id *out, qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, owner, &body, e))
        return false;
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, "sphere", &definition, e))
        return false;
    qa_builtin_spawn spawn = {
        .owner = g->options.owner,
        .definition = definition,
        .body = {.origin = qa_vec_add(body.origin, qa_v3(0, 0, body.bounds.maxs.z)),
                 .angles = qa_v3(0, body.angles.y, 0)}};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (!a)
        goto fail;
    a->item = calloc(1, sizeof(*a->item));
    if (!a->item)
        goto memory;
    a->item->companion = calloc(1, sizeof(*a->item->companion));
    if (!a->item->companion)
        goto memory;
    *a->item->companion = (q2_companion){.kind = kind,
                                         .owner = decoy ? (qa_actor_id){0} : owner,
                                         .credit = credit,
                                         .decoy = decoy,
                                         .expires_ns = q2_deadline(g->now_ns, 30 * Q2_NS),
                                         .next_ns = q2_deadline(g->now_ns, 100 * Q2_MS)};
    a->item->owner = decoy ? credit : owner;
    a->item->visible = true;
    a->item->visual = (qa_q2_visual){
        .scale = 1, .alpha = 1, .visible = true, .old_frame = -1, .render_flags = 8 | 0x8000};
    const char *model = kind == Q2_SPHERE_DEFENDER ? "models/items/defender/tris.md2"
                        : kind == Q2_SPHERE_HUNTER ? "models/items/hunter/tris.md2"
                                                   : "models/items/vengnce/tris.md2";
    if (!qa_builtin_resource(&g->services, model, &a->item->visual.models[0], e) ||
        (kind == Q2_SPHERE_DEFENDER &&
         !qa_builtin_resource(&g->services, "models/items/shell/tris.md2",
                              &a->item->visual.models[1], e)))
        goto fail;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics_bound = true;
    a->physics.q2_rerelease = g->options.edition == QA_Q2_RERELEASE;
    a->physics.motion = QA_PHYSICS_FLY_MISSILE;
    a->physics.clip_mask = a->physics.q2_rerelease ? Q2_PROJECTILE_MASK : Q2_SHOT_MASK;
    if (kind == Q2_SPHERE_VENGEANCE)
        a->physics.angular_velocity = qa_v3(30, 30, 0);
    if (!q2_item_change_collision(g, a, QA_PHYSICS_BOX, e) || !q2_actor_live(g, id))
        goto fail;
    if (!q2_item_visual(g, a, e) || !q2_actor_live(g, id))
        goto fail;
    if (!loop(g, a,
              kind == Q2_SPHERE_DEFENDER ? "spheres/d_idle.wav"
              : kind == Q2_SPHERE_HUNTER ? "spheres/h_idle.wav"
                                         : "spheres/v_idle.wav",
              e))
        goto fail;
    if (!decoy && q2_actor_live(g, owner)) {
        q2_power_state *p = q2_powers(g, owner, e);
        if (!p)
            goto fail;
        p->sphere = id;
    }
    *out = id;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 sphere");
fail:
    if (q2_actor_live(g, id))
        qa_session_release(g->services.session, id, NULL);
    return false;
}
static bool pain(qa_q2_game *g, q2_actor *a, qa_actor_id attacker, qa_error *e) {
    q2_companion *c = a->item->companion;
    if (!q2_actor_live(g, attacker))
        return true;
    if (c->kind == Q2_SPHERE_DEFENDER) {
        if (!qa_actor_id_equal(attacker, c->owner))
            c->enemy = attacker;
        return true;
    }
    if (c->active)
        return true;
    qa_combat_state owner = {0};
    bool live_owner = q2_actor_live(g, c->owner);
    if (live_owner && !qa_combat_read(g->services.combat, c->owner, &owner, e))
        return false;
    if (!c->decoy && ((c->kind == Q2_SPHERE_VENGEANCE && owner.health >= 25) ||
                      (c->kind == Q2_SPHERE_HUNTER && owner.health > 0) ||
                      qa_actor_id_equal(attacker, c->owner)))
        return true;
    c->active = true;
    c->enemy = attacker;
    uint64_t until = q2_deadline(g->now_ns, 15 * Q2_NS);
    if (c->expires_ns < until)
        c->expires_ns = until;
    a->item->visual.effects |= c->kind == Q2_SPHERE_HUNTER ? 8 | 0x4000000 : 16;
    if (!q2_item_visual(g, a, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (c->kind == Q2_SPHERE_HUNTER && !c->decoy && live_owner &&
        g->item_runtime->options.hunter_camera && !(g->options.deathmatch_flags & 1024)) {
        qa_body_state self, target, player;
        if (!qa_world_body_read(g->services.world, a->id, &self, e) ||
            !qa_world_body_read(g->services.world, attacker, &target, e) ||
            !qa_world_body_read(g->services.world, c->owner, &player, e))
            return false;
        if (qa_vec_length(qa_vec_sub(target.origin, self.origin)) >= 192) {
            self.origin = qa_vec_add(player.origin, qa_v3(0, 0, 22));
            if (!set_body(g, a, &self, e))
                return false;
            c->camera = true;
            return q2_client_sphere_camera(g, c->owner, a->id, self.origin, self.angles, e);
        }
    }
    return true;
}
static bool decoy(qa_q2_game *g, qa_actor_id owner, const qa_q2_item_definition *d, bool *used,
                  qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, owner, &body, e))
        return false;
    qa_vec3 forward;
    q2_actor *player = q2_actor_get(g, owner, false, NULL);
    float yaw = player && player->weapon_bound ? player->input.angles.y : body.angles.y;
    qa_builtin_angle_vectors(qa_v3(0, yaw, 0), &forward, NULL, NULL);
    qa_vec3 point = qa_vec_add(body.origin, qa_vec_scale(forward, 48));
    bool found, grounded;
    if (!qa_q2_rogue_find_spawn_point(g, point, body.bounds, 32, &found, &point, e))
        return false;
    if (!found)
        return true;
    if (!qa_q2_rogue_check_ground_spawn(g, point, body.bounds, 64, -1, &grounded, e))
        return false;
    if (!grounded)
        return true;
    bool consumed;
    if (!qa_inventory_consume(g->services.inventory, owner, d->item, 1, &consumed, e))
        return false;
    if (!consumed || !q2_actor_live(g, owner))
        return true;
    if (!qa_q2_rogue_spawn_growth(g, point, 0, e))
        return false;
    if (!q2_actor_live(g, owner))
        return true;
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, "doppleganger", &definition, e))
        return false;
    qa_combat_state combat = {.health = 30, .can_take_damage = true};
    qa_actor_id id, child_id = {0};
    qa_builtin_spawn spawn = {.owner = g->options.owner,
                              .definition = definition,
                              .body = {.origin = point,
                                       .angles = qa_v3(0, yaw, 0),
                                       .bounds = {{-16, -16, -24}, {16, 16, 32}}},
                              .combat = &combat};
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (!a)
        goto fail;
    a->item = calloc(1, sizeof(*a->item));
    if (!a->item)
        goto memory;
    a->item->companion = calloc(1, sizeof(*a->item->companion));
    if (!a->item->companion)
        goto memory;
    *a->item->companion = (q2_companion){.kind = Q2_DOPPLEGANGER,
                                         .owner = owner,
                                         .credit = owner,
                                         .next_ns = q2_deadline(g->now_ns, 30 * Q2_NS),
                                         .expires_ns = q2_deadline(g->now_ns, 30 * Q2_NS)};
    a->item->owner = owner;
    a->item->visible = false;
    a->item->visual = (qa_q2_visual){.render_flags = 0x8000, .scale = 1, .alpha = 1};
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics_bound = true;
    a->physics.motion = QA_PHYSICS_TOSS;
    a->physics.q2_rerelease = g->options.edition == QA_Q2_RERELEASE;
    if (!q2_item_change_collision(g, a, QA_PHYSICS_BOX, e) || !q2_item_visual(g, a, e))
        goto fail;
    if (!q2_actor_live(g, id) || !q2_actor_live(g, owner)) {
        *used = true;
        return true;
    }
    if (!qa_builtin_resource(&g->services, "doppleganger_body", &definition, e))
        goto fail;
    qa_builtin_spawn child_spawn = {
        .owner = g->options.owner,
        .definition = definition,
        .body = {.origin = qa_vec_add(point, qa_v3(0, 0, 8)), .angles = body.angles},
        .link = true};
    if (!qa_builtin_spawn_actor(&g->services, &child_spawn, &child_id, e))
        goto fail;
    q2_actor *child = q2_actor_get(g, child_id, true, e);
    if (!child)
        goto fail;
    child->item = calloc(1, sizeof(*child->item));
    if (!child->item)
        goto memory;
    child->item->companion = calloc(1, sizeof(*child->item->companion));
    if (!child->item->companion)
        goto memory;
    *child->item->companion = (q2_companion){.kind = Q2_DOPPLEGANGER_BODY,
                                             .owner = id,
                                             .next_ns = q2_deadline(g->now_ns, g->frame_ns),
                                             .expires_ns = q2_deadline(g->now_ns, 30 * Q2_NS)};
    child->item->visible = true;
    if (!qa_q2_entity_visual(g, owner, &child->item->visual, e))
        goto fail;
    child->item->visual.visible = true;
    a->item->companion->child = child_id;
    if (!q2_item_visual(g, child, e))
        goto fail;
    *used = true;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 decoy");
fail:
    if (q2_actor_live(g, child_id))
        qa_session_release(g->services.session, child_id, NULL);
    if (q2_actor_live(g, id))
        qa_session_release(g->services.session, id, NULL);
    return false;
}
bool q2_companion_use(qa_q2_game *g, qa_actor_id owner, const qa_q2_item_definition *d, bool *used,
                      qa_error *e) {
    *used = false;
    if (d->kind == QA_Q2_ITEM_DECOY)
        return decoy(g, owner, d, used, e);
    q2_power_state *p = q2_powers(g, owner, e);
    if (!p)
        return false;
    if (q2_actor_live(g, p->sphere))
        return true;
    bool consumed;
    if (!qa_inventory_consume(g->services.inventory, owner, d->item, 1, &consumed, e))
        return false;
    if (!consumed || !q2_actor_live(g, owner))
        return true;
    q2_companion_kind kind = !strcmp(d->classname, "item_sphere_defender") ? Q2_SPHERE_DEFENDER
                             : !strcmp(d->classname, "item_sphere_hunter") ? Q2_SPHERE_HUNTER
                                                                           : Q2_SPHERE_VENGEANCE;
    qa_actor_id sphere;
    if (!launch(g, owner, kind, false, owner, &sphere, e))
        return false;
    *used = true;
    return true;
}
bool q2_companion_tick(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_companion *c = a->item->companion;
    if (g->now_ns < c->next_ns)
        return true;
    c->next_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
    if (g->now_ns >= c->expires_ns)
        return expire(g, a, e);
    if (g->item_runtime->options.intermission &&
        g->item_runtime->options.intermission(g->item_runtime->options.context))
        return !q2_actor_live(g, a->id) || expire(g, a, e);
    if (!q2_actor_live(g, a->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    if (c->kind == Q2_DOPPLEGANGER)
        return true;
    if (c->kind == Q2_DOPPLEGANGER_BODY) {
        float yaw = qa_builtin_angle_mod(body.angles.y),
              delta = qa_builtin_angle_delta(c->goal.y, yaw);
        if (fabsf(delta) < 2) {
            if (c->turn_ns < g->now_ns && q2_random(g) < .1f) {
                c->goal.y = q2_random(g) * 350;
                c->turn_ns = q2_deadline(g->now_ns, Q2_NS);
            }
        } else
            body.angles.y = qa_builtin_angle_mod(yaw + fmaxf(-30, fminf(30, delta)));
        a->item->visual.frame = (a->item->visual.frame + 1) % 40;
        return set_body(g, a, &body, e) && (!q2_actor_live(g, a->id) || q2_item_visual(g, a, e));
    }
    bool owner_live = q2_actor_live(g, c->owner);
    qa_body_state owner;
    if (!owner_live && !c->decoy)
        return qa_session_release(g->services.session, a->id, e);
    if (owner_live && !qa_world_body_read(g->services.world, c->owner, &owner, e))
        return false;
    if (c->kind == Q2_SPHERE_DEFENDER) {
        qa_combat_state state;
        if (!owner_live || !qa_combat_read(g->services.combat, c->owner, &state, e))
            return false;
        if (state.health <= 0)
            return expire(g, a, e);
        a->item->visual.frame = (a->item->visual.frame + 1) % 20;
        if (q2_actor_live(g, c->enemy)) {
            qa_combat_state enemy;
            if (!qa_combat_read(g->services.combat, c->enemy, &enemy, e))
                return false;
            if (enemy.health <= 0)
                c->enemy = (qa_actor_id){0};
            else if (g->now_ns >= c->attack_ns) {
                bool visible;
                if (!sight(g, a, c->enemy, &visible, e))
                    return false;
                if (visible) {
                    qa_body_state target;
                    if (!qa_world_body_read(g->services.world, c->enemy, &target, e))
                        return false;
                    if (!q2_fire_actor_bolt(
                            g, c->owner, c->owner, qa_vec_add(body.origin, qa_v3(0, 0, 2)),
                            qa_vec_normalize(qa_vec_sub(target.origin, body.origin)), 10, 1000, 8,
                            50, true, e))
                        return false;
                    c->attack_ns = q2_deadline(g->now_ns, 400 * Q2_MS);
                }
            }
        }
    }
    if (!q2_actor_live(g, a->id))
        return true;
    if (c->active) {
        if (!q2_actor_live(g, c->enemy))
            return expire(g, a, e);
        qa_combat_state enemy;
        qa_body_state target;
        if (!qa_combat_read(g->services.combat, c->enemy, &enemy, e) ||
            !qa_world_body_read(g->services.world, c->enemy, &target, e))
            return false;
        if (enemy.health < 1)
            return expire(g, a, e);
        qa_builtin_actor_traits traits = {0};
        if (g->services.actor_traits)
            g->services.actor_traits(g->services.context, c->enemy, &traits);
        if (!q2_actor_live(g, a->id))
            return true;
        target.origin.z += traits.player ? traits.view_height : 0;
        bool visible = c->kind == Q2_SPHERE_VENGEANCE;
        if (!visible && !sight(g, a, c->enemy, &visible, e))
            return false;
        qa_vec3 direction;
        float speed;
        if (visible) {
            direction = qa_vec_normalize(qa_vec_sub(target.origin, body.origin));
            speed = 500;
            c->goal = target.origin;
        } else if (qa_vec_length(c->goal) == 0) {
            direction = qa_vec_normalize(qa_vec_sub(target.origin, body.origin));
            speed = 0;
        } else {
            direction = qa_vec_sub(c->goal, body.origin);
            float distance = qa_vec_length(direction);
            direction = qa_vec_normalize(direction);
            speed = distance > 500  ? 500
                    : distance < 20 ? distance / ((float)g->frame_ns / (float)Q2_NS)
                                    : distance;
            if (distance <= 1)
                speed = 0;
        }
        if (c->kind == Q2_SPHERE_HUNTER &&
            !loop(g, a, speed > 0 ? "spheres/h_active.wav" : "spheres/h_lurk.wav", e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        body.velocity = qa_vec_scale(direction, speed);
        body.angles = angles_for(direction);
    } else if (owner_live) {
        qa_vec3 destination = qa_vec_add(owner.origin, qa_v3(0, 0, owner.bounds.maxs.z + 4));
        if (g->now_ns % Q2_NS == 0) {
            bool visible;
            if (!sight(g, a, c->owner, &visible, e))
                return false;
            if (!visible)
                body.origin = destination;
        }
        body.velocity = qa_vec_scale(qa_vec_sub(destination, body.origin), 5);
        if (c->kind == Q2_SPHERE_HUNTER)
            body.angles.y = qa_builtin_angle_mod(
                body.angles.y +
                fmaxf(-40, fminf(40, qa_builtin_angle_delta(owner.angles.y, body.angles.y))));
    }
    if (!set_body(g, a, &body, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (c->camera && owner_live &&
        !q2_client_sphere_camera(g, c->owner, a->id, body.origin, body.angles, e))
        return false;
    return !q2_actor_live(g, a->id) || q2_item_visual(g, a, e);
}
bool q2_companion_touch(qa_q2_game *g, const qa_touch_contact *contact, qa_error *e) {
    q2_actor *a = companion_actor(g, contact->self);
    if (!a)
        return true;
    q2_companion *c = a->item->companion;
    if (!c->active || c->kind == Q2_SPHERE_DEFENDER || c->kind >= Q2_DOPPLEGANGER)
        return true;
    qa_actor_id credited = c->decoy ? c->credit : c->owner;
    if (qa_actor_id_equal(contact->other, credited))
        return true;
    if (!c->decoy) {
        const qa_actor_record *other =
            qa_actors_get(qa_session_actors(g->services.session), contact->other);
        const char *classname =
            other ? qa_strings_cstr(qa_session_strings(g->services.session), other->definition)
                  : NULL;
        if (classname && !strcmp(classname, "bodyque"))
            return true;
    }
    if (c->kind == Q2_SPHERE_HUNTER &&
        qa_actor_id_equal(contact->other, g->services.physics->world_actor))
        return true;
    if (contact->has_surface && (contact->surface.flags & 4))
        return qa_session_release(g->services.session, a->id, e);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    int mod = c->kind == Q2_SPHERE_HUNTER ? (c->decoy ? 55 : 49) : (c->decoy ? 54 : 48);
    const qa_actor_record *reference_owner = qa_actors_get(qa_session_actors(g->services.session), credited);
    q2_projectile attack_state = {.owner = reference_owner && reference_owner->owner == g->options.owner && reference_owner->has_source ?
        qa_actor_reference_source(reference_owner->owner, reference_owner->source_slot) :
        qa_actor_reference_lifetime(credited)};
    qa_attack attack = q2_projectile_attack(g, a->id, &attack_state, mod, 64);
    if (q2_target_damageable(g, contact->other)) {
        if (!q2_damage(g, &attack, contact->other, 10000, 1, body.velocity, body.origin,
                       contact->has_plane ? contact->plane.normal : qa_v3(0, 0, 0), false, e))
            return false;
    } else if (!q2_projectile_radius(g, a->id, &attack_state, body.origin, credited, 512, 256, mod,
                                     0, e))
        return false;
    return !q2_actor_live(g, a->id) || expire(g, a, e);
}
bool q2_companion_reaction(qa_q2_game *g, const qa_damage_outcome *outcome, qa_error *e) {
    q2_actor *a = companion_actor(g, outcome->request.target);
    if (!a)
        return true;
    q2_companion *c = a->item->companion;
    if (c->kind == Q2_DOPPLEGANGER) {
        if (outcome->result.reaction == QA_REACTION_PAIN)
            c->enemy = outcome->request.attack.attacker;
        if (outcome->result.reaction != QA_REACTION_DEATH)
            return true;
        if (q2_actor_live(g, c->enemy) && !qa_actor_id_equal(c->enemy, c->owner)) {
            qa_body_state target, self;
            if (!qa_world_body_read(g->services.world, c->enemy, &target, e) ||
                !qa_world_body_read(g->services.world, a->id, &self, e))
                return false;
            qa_actor_id sphere;
            bool distant = qa_vec_length(qa_vec_sub(target.origin, self.origin)) > 768;
            if (!launch(g, a->id, distant ? Q2_SPHERE_HUNTER : Q2_SPHERE_VENGEANCE, true, c->owner,
                        &sphere, e))
                return false;
            q2_actor *child = companion_actor(g, sphere);
            if (child && !pain(g, child, outcome->request.attack.attacker, e))
                return false;
        }
        return !q2_actor_live(g, a->id) || expire(g, a, e);
    }
    return outcome->result.reaction == QA_REACTION_DEATH &&
                   (c->kind == Q2_SPHERE_DEFENDER || !c->active)
               ? expire(g, a, e)
               : pain(g, a, outcome->request.attack.attacker, e);
}
bool q2_item_reaction(qa_q2_game *g, const qa_damage_outcome *outcome, qa_error *e) {
    if (!q2_actor_live(g, outcome->request.target))
        return true;
    q2_actor *owner =
        outcome->request.target.slot < g->capacity ? g->actors[outcome->request.target.slot] : NULL;
    if (owner && qa_actor_id_equal(owner->id, outcome->request.target) && owner->powers) {
        q2_actor *sphere = companion_actor(g, owner->powers->sphere);
        if (sphere) {
            if (!pain(g, sphere, outcome->request.attack.attacker, e))
                return false;
            if (q2_actor_live(g, sphere->id) && outcome->result.reaction == QA_REACTION_DEATH &&
                (sphere->item->companion->kind == Q2_SPHERE_DEFENDER ||
                 !sphere->item->companion->active) &&
                !expire(g, sphere, e))
                return false;
        }
    }
    return q2_companion_reaction(g, outcome, e);
}
bool q2_item_traits(qa_q2_game *g, qa_actor_id id, qa_builtin_actor_traits *out) {
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a || !a->item)
        return false;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
    *out = (qa_builtin_actor_traits){
        .classname =
            a->item->definition ? q2_item_classname(g, a->item->definition) : record->definition,
        .owner = a->item->owner,
        .damageable_target = a->item->companion && a->item->companion->kind == Q2_DOPPLEGANGER};
    return true;
}
