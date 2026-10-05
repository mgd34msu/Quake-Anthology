#include "internal.h"
#include "../items/internal.h"

enum { BREAK_FIRST = 0, BREAK_LAST = 31, DRAIN_FIRST = 39,
       DRAIN_WAIT_ONE = 42, DRAIN_WAIT_TWO = 43, DRAIN_HIT = 44,
       DRAIN_PULL_ONE = 50, DRAIN_PULL_TWO = 51, DRAIN_FINISH = 52 };

static const qa_vec3 break_offsets[] = {
    {7, 0, 7}, {6.3f, 14.5f, 4}, {8.5f, 0, 5.6f}, {5, -15.25f, 4},
    {9.5f, -1.8f, 5.9f}, {6.2f, 14, 4}, {12.25f, 7.5f, 1.4f}, {13.8f, 0, -2.4f},
    {13.8f, 0, -4}, {.1f, 0, -.7f}, {5, 0, 3.7f}, {11, 0, 4},
    {13.5f, 0, -4}, {13.5f, 0, -4}, {.2f, 0, -.7f}, {3.9f, 0, 3.6f},
    {8.5f, 0, 5}, {14, 0, -4}, {14, 0, -4}, {.1f, 0, -.5f}
};
static const qa_vec3 drain_offsets[] = {
    {-1.7f, 0, 1.2f}, {-2.2f, 0, -.6f}, {7.7f, 0, 7.2f}, {7.2f, 0, 5.7f},
    {6.2f, 0, 7.8f}, {4.7f, 0, 6.7f}, {5, 0, 9}, {5, 0, 7},
    {5, 0, 10.5f}, {4.5f, 0, 9.7f}, {1.5f, 0, 12}, {2.9f, 0, 11}, {2.1f, 0, 7.6f}
};

static qa_vec3 mouth(const q2m_context *owner) {
    int frame = owner->monster->frame;
    qa_vec3 offset = {8, 0, 6};
    if (frame >= BREAK_FIRST && (size_t)(frame - BREAK_FIRST) < sizeof(break_offsets) / sizeof(*break_offsets))
        offset = break_offsets[frame - BREAK_FIRST];
    else if (frame >= DRAIN_FIRST && (size_t)(frame - DRAIN_FIRST) < sizeof(drain_offsets) / sizeof(*drain_offsets))
        offset = drain_offsets[frame - DRAIN_FIRST];
    return q2m_project_offset(owner, offset);
}

static bool owner_context(qa_q2_game *game, const q2_actor *tip, q2m_context *out) {
    q2_actor *owner = q2_actor_get(game, qa_actor_reference_resolve(qa_session_actors(game->services.session), tip->projectile.owner), false, NULL);
    if (!owner || !owner->monster || !owner->monster->definition)
        return false;
    *out = (q2m_context){.game = game, .actor = owner, .monster = owner->monster};
    return true;
}

static bool move_part(qa_q2_game *game, qa_actor_id id, const qa_body_state *body, qa_error *error) {
    return qa_world_body_write(game->services.world, id, body, error) &&
           (!q2_actor_live(game, id) || qa_world_link(game->services.world, id, NULL, error));
}

static bool reset(qa_q2_game *game, q2_actor *tip, qa_error *error) {
    qa_actor_id id = tip->id, segment = qa_actor_reference_resolve(qa_session_actors(game->services.session), tip->projectile.child);
    q2m_context owner;
    if (owner_context(game, tip, &owner))
        owner.monster->proboscis = (qa_actor_id){0};
    if (q2_actor_live(game, segment) && !qa_session_release(game->services.session, segment, error))
        return false;
    return !q2_actor_live(game, id) || qa_session_release(game->services.session, id, error);
}

static bool stop(qa_q2_game *game, q2_actor *tip, qa_error *error) {
    tip->physics.motion = QA_PHYSICS_STATIONARY;
    tip->physics.solid = QA_PHYSICS_NOT_SOLID;
    return qa_world_set_collision(game->services.world, tip->id, NULL, error);
}

static bool retract(qa_q2_game *game, q2_actor *tip, qa_error *error) {
    q2m_context owner;
    if (owner_context(game, tip, &owner) && owner.monster->move &&
        (owner.monster->move->id == Q2M_MOVE_parasite_move_fire_proboscis))
        owner.monster->next_frame = DRAIN_PULL_ONE;
    if (tip->projectile.phase != Q2_PROBOSCIS_RETRACTING)
        tip->projectile.speed *= 2;
    tip->projectile.phase = Q2_PROBOSCIS_RETRACTING;
    return stop(game, tip, error) &&
           (!q2_actor_live(game, tip->id) || qa_world_link(game->services.world, tip->id, NULL, error));
}

bool q2m_parasite_interrupt(q2m_context *context, bool death, qa_error *error) {
    q2_actor *tip = q2_actor_get(context->game, context->monster->proboscis, false, NULL);
    if (!tip || tip->projectile.kind != Q2_PROBOSCIS || tip->projectile.phase == Q2_PROBOSCIS_RETRACTING)
        return true;
    return death ? reset(context->game, tip, error) : retract(context->game, tip, error);
}

static bool show(qa_q2_game *game, q2_actor *actor, qa_error *error) {
    q2_projectile *p = &actor->projectile;
    qa_q2_visual visual = {.models = {p->model}, .render_flags = p->render_flags,
                           .scale = 1, .alpha = 1, .visible = true};
    return q2_publish_visual(game, actor->id, &visual, error);
}

static bool draw(qa_q2_game *game, q2_actor *segment, const qa_vec3 *launch_origin,
                   qa_error *error) {
    q2_actor *tip = q2_actor_get(game, qa_actor_reference_resolve(qa_session_actors(game->services.session), segment->projectile.owner), false, NULL);
    q2m_context owner;
    if (!tip || !owner_context(game, tip, &owner))
        return true;
    qa_body_state tip_body, segment_body;
    if (!launch_origin && !q2m_refresh(&owner, error))
        return !q2m_alive(&owner);
    if (!qa_world_body_read(game->services.world, tip->id, &tip_body, error))
        return false;
    if (!q2_actor_live(game, segment->id) || !q2_actor_live(game, tip->id) || !q2m_alive(&owner))
        return true;
    if (!qa_world_body_read(game->services.world, segment->id, &segment_body, error))
        return false;
    if (!q2_actor_live(game, segment->id) || !q2_actor_live(game, tip->id) || !q2m_alive(&owner))
        return true;
    qa_vec3 from = launch_origin ? *launch_origin : mouth(&owner);
    qa_vec3 offset = qa_vec_scale(qa_vec_normalize(qa_vec_sub(tip_body.origin, from)),
                                   launch_origin ? 8 : -8);
    segment->projectile.movedir = qa_vec_add(tip_body.origin, offset);
    segment_body.origin = from;
    return move_part(game, segment->id, &segment_body, error);
}

static bool hit(qa_q2_game *game, q2_actor *tip, qa_actor_id other, qa_vec3 point,
                 qa_vec3 normal, bool start_solid, qa_error *error) {
    q2m_context owner;
    if (!owner_context(game, tip, &owner) || !owner.monster->move ||
        (owner.monster->move->id != Q2M_MOVE_parasite_move_fire_proboscis))
        return true;
    qa_actor_id id = tip->id, owner_id = owner.actor->id;
    qa_body_state body, target;
    if (!qa_world_body_read(game->services.world, id, &body, error))
        return false;
    qa_error missing = {0};
    bool has_target = q2_actor_live(game, other) &&
                      qa_world_body_read(game->services.world, other, &target, &missing);
    if (!q2_actor_live(game, id) || !q2m_alive(&owner))
        return true;
    qa_builtin_actor_traits traits = {0};
    if (game->services.actor_traits)
        game->services.actor_traits(game->services.context, other, &traits);
    if (!q2_actor_live(game, id) || !q2m_alive(&owner))
        return true;
    qa_vec3 position;
    if (has_target && (traits.player || qa_actor_id_equal(owner.monster->enemy, other))) {
        position = start_solid ? point
            : qa_vec_sub(point, qa_vec_scale(qa_vec_normalize(qa_vec_sub(body.origin, point)), 12));
        owner.monster->next_frame = DRAIN_HIT;
        tip->projectile.phase = Q2_PROBOSCIS_ATTACHED;
        tip->projectile.movedir = qa_vec_sub(position, target.origin);
        const qa_actor_record *reference_enemy = qa_actors_get(qa_session_actors(game->services.session), other);
        tip->projectile.enemy = reference_enemy && reference_enemy->owner == game->options.owner && reference_enemy->has_source ?
            qa_actor_reference_source(reference_enemy->owner, reference_enemy->source_slot) :
            qa_actor_reference_lifetime(other);
        tip->projectile.render_flags |= 32;
        if (!stop(game, tip, error) ||
            !q2_projectile_event(game, id, QA_BUILTIN_SOUND, "parasite/paratck3.wav", 1,
                                  body.origin, body.origin, error))
            return false;
    } else {
        position = qa_vec_add(point, normal);
        qa_physics_properties physics = {0};
        bool corpse = game->services.physics &&
            game->services.physics->services.read(game->services.physics->services.context,
                                                  other, &physics) &&
            (physics.flags & QA_PHYSICS_DEAD);
        if (!q2_actor_live(game, id) || !q2m_alive(&owner))
            return true;
        if (traits.monster || corpse) {
            if (!retract(game, tip, error))
                return false;
        } else {
            if (!q2m_set_move(&owner, Q2M_MOVE_parasite_move_break, false, error))
                return false;
            tip->projectile.phase = Q2_PROBOSCIS_ATTACHED;
            if (!stop(game, tip, error) || !q2m_refresh(&owner, error))
                return false;
            if (!q2m_alive(&owner))
                return true;
            owner.body.angles.y = body.angles.y;
            if (!q2m_write_body(&owner, true, error))
                return false;
        }
    }
    if (!q2_actor_live(game, id))
        return true;
    bool damageable = q2_target_damageable(game, other);
    if (!q2_actor_live(game, id))
        return true;
    if (damageable) {
        qa_attack attack = q2_projectile_attack(game, id, &tip->projectile, 0, 0);
        if (!q2_damage(game, &attack, other, 5, 0, normal, point, normal, false, error))
            return false;
    }
    if (!q2_projectile_event(game, owner_id, QA_BUILTIN_SOUND, "parasite/paratck2.wav", 0,
                              point, point, error))
        return false;
    if (!q2_actor_live(game, id))
        return true;
    if (!qa_world_body_read(game->services.world, id, &body, error))
        return false;
    if (!q2_actor_live(game, id))
        return true;
    body.origin = position;
    tip->projectile.next_ns = q2_deadline(game->now_ns, game->frame_ns);
    return move_part(game, id, &body, error);
}

bool q2_proboscis_touch(qa_q2_game *game, const qa_touch_contact *contact, qa_error *error) {
    q2_actor *tip = q2_actor_get(game, contact->self, false, NULL);
    if (!tip || tip->projectile.kind != Q2_PROBOSCIS)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(game->services.world, tip->id, &body, error))
        return false;
    return !q2_actor_live(game, tip->id) || hit(game, tip, contact->other, body.origin,
        contact->has_plane ? contact->plane.normal : qa_v3(0, 0, 0), false, error);
}

bool q2_proboscis_reaction(qa_q2_game *game, q2_actor *tip,
                           const qa_damage_outcome *outcome, qa_error *error) {
    const qa_damage_cause *cause = &outcome->request.attack.cause;
    return cause->kind != QA_CAUSE_Q2 || cause->source.q2.means_of_death != 20 ||
           reset(game, tip, error);
}

static bool think(qa_q2_game *game, q2_actor *tip, qa_error *error) {
    q2m_context owner;
    if (!owner_context(game, tip, &owner) || tip->projectile.phase == Q2_PROBOSCIS_RETURNED)
        return reset(game, tip, error);
    qa_actor_id id = tip->id;
    tip->projectile.next_ns = q2_deadline(game->now_ns, game->frame_ns);
    if (!q2m_refresh(&owner, error))
        return !q2m_alive(&owner) && reset(game, tip, error);
    qa_body_state body;
    if (!qa_world_body_read(game->services.world, id, &body, error))
        return false;
    if (!q2_actor_live(game, id) || !q2m_alive(&owner))
        return true;
    q2_projectile *p = &tip->projectile;
    qa_vec3 from = mouth(&owner);
    if (p->phase == Q2_PROBOSCIS_RETRACTING) {
        qa_vec3 direction = qa_vec_sub(body.origin, from);
        float distance = qa_vec_length(direction);
        float step = p->speed * (float)((double)game->frame_ns / Q2_NS);
        if (distance <= step * 2) {
            p->phase = Q2_PROBOSCIS_RETURNED;
            body.origin = from;
        } else
            body.origin = qa_vec_sub(body.origin, qa_vec_scale(qa_vec_normalize(direction), step));
        return move_part(game, id, &body, error);
    }
    if (p->phase == Q2_PROBOSCIS_ATTACHED && qa_actor_reference_present(p->enemy)) {
        qa_actor_id enemy = qa_actor_reference_resolve(qa_session_actors(game->services.session), p->enemy);
        qa_body_state target;
        qa_combat_state combat;
        qa_error missing = {0};
        bool valid = q2_actor_live(game, enemy) &&
            qa_world_body_read(game->services.world, enemy, &target, &missing) &&
            qa_combat_read(game->services.combat, enemy, &combat, &missing) &&
            combat.health > 0 && combat.can_take_damage && q2_actor_live(game, enemy);
        if (!q2_actor_live(game, id) || !q2m_alive(&owner))
            return true;
        if (!valid)
            return retract(game, tip, error);
        qa_vec3 previous = body.origin;
        body.origin = qa_vec_add(target.origin, p->movedir);
        qa_trace_query query = {.start = from, .end = body.origin,
                               .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        query.policy.contents_mask = 3;
        qa_trace_result trace;
        if (!qa_world_trace(game->services.world, &query, &trace, error))
            return false;
        if (!q2_actor_live(game, id) || !q2m_alive(&owner))
            return true;
        body.angles = q2m_vector_angles(qa_vec_normalize(qa_vec_sub(body.origin, from)));
        if (!move_part(game, id, &body, error))
            return false;
        if (!q2_actor_live(game, id) || !q2m_alive(&owner))
            return true;
        if (trace.fraction != 1) {
            if (!retract(game, tip, error))
                return false;
            body.origin = previous;
            return !q2_actor_live(game, id) ||
                   move_part(game, id, &body, error);
        }
        if (p->effect_ns <= game->now_ns) {
            qa_vec3 normal = trace.contact ? trace.contact_plane.normal : qa_v3(0, 0, 0);
            qa_attack attack = q2_projectile_attack(game, id, p, 0, 0);
            if (!q2_damage(game, &attack, enemy, 2, 0, normal, trace.end, normal, false, error))
                return false;
            if (!q2_actor_live(game, id) || !q2m_alive(&owner))
                return true;
            if (!qa_combat_read(game->services.combat, owner.actor->id, &combat, error))
                return false;
            if (!q2_actor_live(game, id) || !q2m_alive(&owner))
                return true;
            float hp = fminf(owner.monster->base_health, combat.health + 2);
            if (!qa_combat_set_health(game->services.combat, owner.actor->id, hp, error))
                return false;
            if (!q2_actor_live(game, id) || !q2m_alive(&owner))
                return true;
            owner.monster->skin = hp < owner.monster->base_health / 2 ? 1 : 0;
            p->effect_ns = q2_deadline(game->now_ns, Q2M_TENTH);
        }
        return qa_world_link(game->services.world, id, NULL, error);
    }
    if (p->phase == Q2_PROBOSCIS_FLYING) {
        qa_body_state target;
        qa_combat_state combat;
        qa_actor_id enemy = owner.monster->enemy;
        qa_error missing = {0};
        bool valid = q2_actor_live(game, enemy) &&
            qa_world_body_read(game->services.world, enemy, &target, &missing) &&
            qa_combat_read(game->services.combat, enemy, &combat, &missing) && combat.health > 0 &&
            q2_actor_live(game, enemy);
        if (!q2_actor_live(game, id) || !q2m_alive(&owner))
            return true;
        if (!valid)
            return retract(game, tip, error);
        qa_vec3 delta = qa_vec_sub(body.origin, target.origin);
        if (qa_vec_length(delta) > p->speed * 2 / 15 &&
            qa_vec_dot(qa_vec_normalize(delta),
                       qa_vec_normalize(qa_vec_sub(body.origin, owner.body.origin))) > 0)
            return retract(game, tip, error);
    }
    return true;
}

bool q2_proboscis_tick(qa_q2_game *game, q2_actor *actor, qa_error *error) {
    if (actor->projectile.kind == Q2_PROBOSCIS_SEGMENT)
        return draw(game, actor, NULL, error);
    if (actor->projectile.next_ns <= game->now_ns && !think(game, actor, error))
        return false;
    if (!q2_actor_live(game, actor->id))
        return true;
    qa_source_frame frame = {.provider = game->options.owner, .kind = QA_CLOCK_Q2_RERELEASE,
                             .phase = QA_ENTITY_PHYSICS, .time_ns = game->now_ns,
                             .elapsed_ns = game->frame_ns,
                             .start_ns = game->now_ns >= game->frame_ns ? game->now_ns - game->frame_ns : 0};
    qa_physics_result result;
    return qa_physics_step(game->services.physics, actor->id, &frame, &result, error);
}

static bool create_part(q2m_context *context, bool segment, qa_vec3 from, qa_vec3 direction,
                         qa_actor_id owner, q2_actor **out, qa_error *error) {
    qa_q2_game *game = context->game;
    qa_string_id classname, model;
    if (!qa_builtin_resource(&game->services, segment ? "parasite_proboscis_segment" : "parasite_proboscis", &classname, error) ||
        !qa_builtin_resource(&game->services, segment ? "models/monsters/parasite/segment/tris.md2" : "models/monsters/parasite/tip/tris.md2", &model, error))
        return false;
    qa_actor_collision collision = {.family = QA_COLLISION_Q2, .shape = QA_SHAPE_BOX,
                                    .contents = 2, .owner = owner, .role = QA_COLLISION_SOLID};
    qa_combat_state combat = {.can_take_damage = true, .no_knockback = true};
    qa_builtin_spawn spawn = {.owner = game->options.owner, .definition = classname,
        .body = {.origin = from, .angles = q2m_vector_angles(direction),
                  .velocity = segment ? qa_v3(0, 0, 0) : qa_vec_scale(direction, 1250)},
        .collision = segment ? NULL : &collision, .combat = segment ? NULL : &combat};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&game->services, &spawn, &id, error))
        return false;
    q2_actor *actor = q2_actor_get(game, id, true, error);
    if (!actor) {
        qa_session_release(game->services.session, id, NULL);
        return false;
    }
    const qa_actor_record *reference_owner = qa_actors_get(qa_session_actors(game->services.session), owner);
    actor->projectile = (q2_projectile){.kind = segment ? Q2_PROBOSCIS_SEGMENT : Q2_PROBOSCIS,
        .owner = reference_owner && reference_owner->owner == game->options.owner && reference_owner->has_source ?
            qa_actor_reference_source(reference_owner->owner, reference_owner->source_slot) :
            qa_actor_reference_lifetime(owner), .model = model, .classname = classname, .scale = 1, .visible = true,
        .speed = segment ? 0 : 1250, .next_ns = q2_deadline(game->now_ns, game->frame_ns),
        .expire_ns = UINT64_MAX, .render_flags = segment ? 128 : 0,
        .attack = {.attacker = owner, .weapon_provider = game->options.owner,
                    .combat_provider = game->options.owner,
                    .cause = qa_q2_damage_cause(game->options.edition, game->options.product, 0, 0)}};
    actor->character_no_damage_effects = !segment;
    actor->physics_bound = true;
    actor->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    actor->physics.q2_rerelease = true;
    actor->physics.motion = segment ? QA_PHYSICS_STATIONARY : QA_PHYSICS_FLY_MISSILE;
    actor->physics.solid = segment ? QA_PHYSICS_NOT_SOLID : QA_PHYSICS_BOX;
    actor->physics.clip_mask = UINT32_C(0x42000003);
    *out = actor;
    if (!qa_world_link(game->services.world, id, NULL, error)) {
        if (q2_actor_live(game, id))
            qa_session_release(game->services.session, id, NULL);
        return false;
    }
    return true;
}

static bool fire(q2m_context *context, qa_error *error) {
    if (!q2m_parasite_interrupt(context, true, error))
        return false;
    if (!q2m_alive(context))
        return true;
    qa_q2_game *game = context->game;
    qa_vec3 from = mouth(context), direction;
    float offset = q2_rerelease_float(game, -0.9999999403953552f, 1) * .1f;
    bool available;
    if (!q2m_predict_from(context, from, 1250, false, offset, NULL, &direction, &available, error))
        return false;
    if (!available || !q2m_alive(context))
        return true;
    q2_actor *tip, *segment;
    if (!create_part(context, false, from, direction, context->actor->id, &tip, error))
        return false;
    if (!q2m_alive(context) || !q2_actor_live(game, tip->id))
        return !q2_actor_live(game, tip->id) || reset(game, tip, error);
    if (!create_part(context, true, from, direction, tip->id, &segment, error)) {
        qa_session_release(game->services.session, tip->id, NULL);
        return false;
    }
    tip->projectile.child = qa_actor_reference_source(game->options.owner, segment->wire_slot);
    if (!q2m_alive(context) || !q2_actor_live(game, tip->id) ||
        !q2_actor_live(game, segment->id))
        return reset(game, tip, error);
    context->monster->proboscis = tip->id;
    qa_trace_query query = {.start = from,
        .end = qa_vec_add(from, qa_vec_scale(direction, 1250 * (float)((double)game->frame_ns / Q2_NS))),
        .pass_actor = context->actor->id, .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = tip->physics.clip_mask;
    qa_trace_result trace;
    if (!qa_world_trace(game->services.world, &query, &trace, error))
        return false;
    if (!q2_actor_live(game, tip->id) || !q2_actor_live(game, segment->id))
        return true;
    if ((trace.start_solid || trace.fraction < 1) &&
        !hit(game, tip, trace.hit == QA_TRACE_HIT_ACTOR ? trace.actor : game->services.physics->world_actor,
             trace.start_solid ? from : trace.end,
             trace.start_solid ? qa_vec_scale(direction, -1)
                               : trace.contact ? trace.contact_plane.normal : qa_v3(0, 0, 0),
             trace.start_solid, error))
        return false;
    if (!q2_actor_live(game, tip->id) || !q2_actor_live(game, segment->id))
        return true;
    return draw(game, segment, &from, error) &&
           (!q2_actor_live(game, tip->id) || show(game, tip, error)) &&
           (!q2_actor_live(game, segment->id) || show(game, segment, error));
}

bool q2m_parasite_charge(q2m_context *context, float distance, qa_error *error) {
    bool breaking = context->monster->frame >= BREAK_FIRST && context->monster->frame <= BREAK_LAST;
    if (!q2m_run_ai(context, breaking ? Q2M_AI_MOVE : Q2M_AI_CHARGE, distance, error))
        return false;
    if (!q2m_alive(context))
        return true;
    q2_actor *tip = q2_actor_get(context->game, context->monster->proboscis, false, NULL);
    q2_actor *segment = tip ? q2_actor_get(context->game, qa_actor_reference_resolve(qa_session_actors(context->game->services.session), tip->projectile.child), false, NULL) : NULL;
    return !segment || draw(context->game, segment, NULL, error);
}

bool q2m_parasite_callback(q2m_context *context, q2m_callback_id name, bool *handled, qa_error *error) {
    *handled = false;
    if (context->game->options.edition != QA_Q2_RERELEASE ||
        context->monster->definition->species != Q2M_PARASITE)
        return true;
    *handled = true;
    struct qa_q2_monster *monster = context->monster;
    q2_actor *tip = q2_actor_get(context->game, monster->proboscis, false, NULL);
    if (name == Q2M_CALLBACK_parasite_fire_proboscis)
        return fire(context, error);
    if (name == Q2M_CALLBACK_parasite_proboscis_wait) {
        monster->next_frame = monster->frame == DRAIN_WAIT_ONE ? DRAIN_WAIT_TWO : DRAIN_WAIT_ONE;
    } else if (name == Q2M_CALLBACK_parasite_proboscis_pull_wait) {
        if (!tip || tip->projectile.phase == Q2_PROBOSCIS_RETURNED)
            monster->next_frame = DRAIN_FINISH;
        else {
            monster->next_frame = monster->frame == DRAIN_PULL_ONE ? DRAIN_PULL_TWO : DRAIN_PULL_ONE;
            return tip->projectile.phase == Q2_PROBOSCIS_RETRACTING || retract(context->game, tip, error);
        }
    } else if (name == Q2M_CALLBACK_parasite_break_retract) {
        return !tip || retract(context->game, tip, error);
    } else if (name == Q2M_CALLBACK_parasite_break_wait) {
        if (tip && tip->projectile.phase != Q2_PROBOSCIS_RETURNED)
            monster->next_frame = 18;
        else if (q2_random_bounded(context->game, 2)) {
            monster->next_frame = 30;
            return q2m_sound(context, "parasite/paratck4.wav", 1, 1, error);
        }
    } else if ((name == Q2M_CALLBACK_parasite_run) || (name == Q2M_CALLBACK_parasite_start_run)) {
        bool start = (name == Q2M_CALLBACK_parasite_start_run);
        if (!start && !q2m_parasite_interrupt(context, false, error))
            return false;
        return !q2m_alive(context) || q2m_set_move(context, monster->stand_ground ? Q2M_MOVE_parasite_move_stand
             : start ? Q2M_MOVE_parasite_move_start_run : Q2M_MOVE_parasite_move_run, false, error);
    } else if (name == Q2M_CALLBACK_parasite_break_noise) {
        return q2m_sound(context, "parasite/parsrch1.wav", 2, 1, error);
    } else if (name == Q2M_CALLBACK_parasite_break_sound) {
        monster->pain_ns = q2m_after(context->game->now_ns, 3);
        return q2m_sound(context, q2m_random(context->game) < .5f ? "parasite/parpain1.wav"
                                                               : "parasite/parpain2.wav", 2, 1, error);
    } else if ((name == Q2M_CALLBACK_parasite_tap) || (name == Q2M_CALLBACK_parasite_scratch)) {
        const char *path = (name == Q2M_CALLBACK_parasite_tap) ? "parasite/paridle1.wav" : "parasite/paridle2.wav";
        qa_string_id resource;
        return qa_builtin_resource(&context->game->services, path, &resource, error) &&
            qa_builtin_emit(&context->game->services, &(qa_builtin_event){.kind = QA_BUILTIN_SOUND,
                .family = QA_GAME_Q2, .provider = context->game->options.owner,
                .actor = context->actor->id, .resource = resource, .channel = 1,
                .origin = context->body.origin, .volume = .75f, .attenuation = 2.75f,
                .time_ns = context->game->now_ns}, error);
    } else
        *handled = false;
    return true;
}
