#include "internal.h"
#include "qa/game_q2_source.h"

static bool sky(const qa_trace_result *t) {
    return (t->surface_flags & 4) != 0 ||
           (t->has_surface &&
            ((t->surface.flags & 4) != 0 || strncmp(t->surface.name, "sky", 3) == 0));
}
static bool damageable(qa_q2_game *g, qa_actor_id id, qa_combat_state *out) {
    qa_error ignored;
    return id.registry != 0 && q2_actor_live(g, id) &&
           qa_combat_read(g->services.combat, id, out, &ignored) && out->can_take_damage;
}
bool q2_damage(qa_q2_game *g, const qa_attack *attack, qa_actor_id target, float amount, float kick,
               qa_vec3 direction, qa_vec3 point, qa_vec3 normal, bool radius, qa_error *e) {
    qa_damage_request request = {.attack = *attack,
                                 .target = target,
                                 .amount = amount,
                                 .knockback = kick,
                                 .direction = direction,
                                 .point = point,
                                 .normal = normal,
                                 .radius = radius};
    bool allowed = true;
    if (!q2_prepare_damage(g, &request, &allowed, e))
        return false;
    if (!allowed)
        return true;
    qa_damage_outcome outcome = {0};
    bool result = qa_combat_apply(g->services.combat, &request, &outcome, e);
    qa_damage_outcome_free(&outcome);
    return result;
}
bool q2_prepare_damage(void *context, qa_damage_request *request, bool *allowed, qa_error *e) {
    qa_q2_game *g = context;
    if (!*allowed)
        return true;
    if (g->sequence == UINT64_MAX) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 damage sequence exhausted");
        return false;
    }
    request->attack.sequence = ++g->sequence;
    request->attack.time_ns = g->now_ns;
    if (g->hooks.prepare_damage)
        return g->hooks.prepare_damage(g->hooks.context, request, allowed, e);
    if (g->options.edition != QA_Q2_RERELEASE || request->attack.cause.kind != QA_CAUSE_Q2 ||
        qa_actor_id_equal(request->target, request->attack.attacker) ||
        qa_attack_flags(&request->attack).no_protection)
        return true;
    bool target_player, attacker_player;
    if (!q2_target_creature(g, request->target, NULL, &target_player, e) ||
        !q2_target_creature(g, request->attack.attacker, NULL, &attacker_player, e))
        return false;
    if (!q2_actor_live(g, request->target)) {
        *allowed = false;
        return true;
    }
    if (!target_player || !attacker_player || !q2_actor_live(g, request->attack.attacker))
        return true;
    bool same_team = g->options.cooperative;
    if (!same_team) {
        float teamplay;
        if (!qa_q2_source_value(g, "teamplay", 0, &teamplay, e))
            return false;
        if (g->arsenal_rules != QA_Q2_WEAPON_RULES_CTF && truncf(teamplay) == 0)
            return true;
        qa_combat_state target, attacker;
        if (!qa_combat_read(g->services.combat, request->target, &target, e) ||
            !qa_combat_read(g->services.combat, request->attack.attacker, &attacker, e))
            return false;
        same_team = target.team != 0 && target.team == attacker.team;
    }
    if (same_team) {
        request->attack.cause.source.q2.friendly_fire = true;
        request->attack.cause.source.q2.means_of_death |= INT32_C(0x08000000);
        if (request->attack.cause.source.q2.native == QA_Q2_CAUSE_CLASSIC)
            request->attack.cause.source.q2.native_value |= INT32_C(0x08000000);
        uint32_t means = (uint32_t)request->attack.cause.source.q2.means_of_death &
                         ~UINT32_C(0x08000000);
        if ((g->options.deathmatch_flags & 256u) != 0 && means != 47)
            request->amount = 0;
    }
    return true;
}
bool q2_prepare_radius_damage(void *context, qa_damage_request *request, bool *allowed,
                              qa_error *e) {
    request->amount = truncf(request->amount);
    request->knockback = truncf(request->knockback);
    return q2_prepare_damage(context, request, allowed, e);
}
bool q2_radius_damage(qa_q2_game *g, const qa_builtin_radius *radius, size_t *damaged,
                      qa_error *e) {
    q2_trace_frame *scratch = q2_nearby(g, radius->origin, radius->radius, e);
    if (scratch == NULL)
        return false;
    qa_builtin_radius request = *radius;
    request.has_candidates = true;
    request.candidate_radius_only = true;
    request.candidates = scratch->snapshot.ids;
    request.candidate_count = scratch->snapshot.count;
    bool ok = qa_builtin_radius_damage(&g->services, &request, damaged, e);
    scratch->active = false;
    return ok;
}
static qa_vec3 spread(q2_weapon_call *c, qa_vec3 origin, qa_vec3 direction, float hs, float vs) {
    float horizontal = sqrtf(direction.x * direction.x + direction.y * direction.y);
    qa_vec3 angles = qa_v3(-atan2f(direction.z, horizontal) * 57.29577951308232f,
                           atan2f(direction.y, direction.x) * 57.29577951308232f, 0);
    qa_vec3 f, r, u;
    qa_builtin_angle_vectors(angles, &f, &r, &u);
    float x = q2_crandom(c->game) * hs, y = q2_crandom(c->game) * vs;
    return qa_vec_add(qa_vec_add(qa_vec_add(origin, qa_vec_scale(f, 8192)), qa_vec_scale(r, x)),
                      qa_vec_scale(u, y));
}
static bool contents(q2_weapon_call *c, qa_vec3 origin, int32_t *out, qa_error *e) {
    qa_point_query query = {.point = origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    qa_point_contents result;
    if (!qa_world_point_contents(c->game->services.world, &query, &result, e))
        return false;
    *out = result.contents;
    return true;
}
static bool lead(q2_weapon_call *c, qa_vec3 start, qa_vec3 direction, float damage, float kick,
                 float hs, float vs, int mod, bool shotgun, qa_error *e) {
    qa_body_state own;
    if (!qa_world_body_read(c->game->services.world, c->actor->id, &own, e))
        return false;
    int32_t water;
    if (!contents(c, start, &water, e))
        return false;
    bool wet = ((uint32_t)water & Q2_WATER_MASK) != 0, initial = true;
    qa_vec3 water_start = start;
    uint32_t shot = c->rerelease
                        ? (c->input.players_collide ? Q2_PROJECTILE_MASK
                                                    : Q2_PROJECTILE_MASK & ~Q2_PLAYER_CONTENTS)
                        : Q2_SHOT_MASK;
    uint32_t mask = shot | (wet ? 0 : Q2_WATER_MASK);
    qa_trace_query query = {.start = own.origin,
                            .end = start,
                            .pass_actor = c->actor->id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    qa_trace_result trace = {0};
    qa_actor_id excluded[16];
    size_t count = 0;
    qa_attack attack = q2_attack(c, mod, 16);
    for (;;) {
        if (!q2_actor_live(c->game, c->actor->id))
            return true;
        query.policy.contents_mask = initial && !c->rerelease ? shot : mask;
        if (!qa_world_trace_excluding(c->game->services.world, &query, excluded, count, &trace, e))
            return false;
        if (trace.fraction == 1 && initial) {
            initial = false;
            count = 0;
            query.start = start;
            query.end = spread(c, start, direction, hs, vs);
            continue;
        }
        if (trace.fraction == 1)
            break;
        if (((uint32_t)trace.contents & Q2_WATER_MASK & mask) != 0) {
            wet = true;
            water_start = trace.end;
            if (qa_vec_length(qa_vec_sub(start, trace.end)) != 0) {
                int color =
                    ((uint32_t)trace.contents & 32u) != 0
                        ? (trace.has_surface && strcmp(trace.surface.name,
                                                       c->rerelease ? "brwater" : "*brwater") == 0
                               ? 3
                               : 2)
                    : ((uint32_t)trace.contents & 16u) != 0 ? 4
                                                            : 5;
                qa_builtin_event event = {.kind = QA_BUILTIN_IMPACT,
                                          .family = QA_GAME_Q2,
                                          .provider = c->game->options.owner,
                                          .actor = c->actor->id,
                                          .time_ns = c->now_ns,
                                          .origin = trace.end,
                                          .direction = trace.contact_plane.normal,
                                          .count = 8,
                                          .code = color};
                if (!qa_builtin_resource(&c->game->services, "q2:splash", &event.resource, e) ||
                    !qa_builtin_emit(&c->game->services, &event, e))
                    return false;
                query.end = spread(c, trace.end, qa_vec_sub(query.end, start), hs * 2, vs * 2);
            }
            mask &= ~Q2_WATER_MASK;
            if (!c->rerelease)
                query.start = trace.end;
            continue;
        }
        qa_actor_id target = trace.hit == QA_TRACE_HIT_ACTOR ? trace.actor : (qa_actor_id){0};
        qa_combat_state combat;
        if ((c->rerelease || !sky(&trace)) && damageable(c->game, target, &combat)) {
            if (!q2_damage(c->game, &attack, target, damage, kick, direction, trace.end,
                           trace.contact ? trace.contact_plane.normal : qa_v3(0, 0, 0), false, e))
                return false;
            qa_actor_collision collision;
            bool dead = damageable(c->game, target, &combat) && combat.health <= 0;
            qa_error observed = {0};
            bool monster = qa_world_get_collision(c->game->services.world, target, &collision,
                                                   &observed) && collision.monster;
            if (observed.code) {
                if (e)
                    *e = observed;
                return false;
            }
            if (c->rerelease &&
                (((uint32_t)trace.contents & UINT32_C(0x04000000)) != 0 || (monster && dead))) {
                bool duplicate = false;
                for (size_t i = 0; i < count; ++i)
                    duplicate |= qa_actor_id_equal(excluded[i], target);
                if (!duplicate && count < 16) {
                    excluded[count++] = target;
                    continue;
                }
            }
        } else if (!sky(&trace)) {
            if (!q2_event_named(c, QA_BUILTIN_IMPACT,
                shotgun ? "q2:shotgun" : "q2:gunshot",
                mod, trace.end, trace.contact_plane.normal, e))
                return false;
            if (!q2_noise_for_actor(c->game, c->actor->id, trace.end, true, e))
                return false;
        }
        break;
    }
    if (wet) {
        qa_vec3 pos = qa_vec_add(
            trace.end, qa_vec_scale(qa_vec_normalize(qa_vec_sub(trace.end, water_start)), -2));
        if (!contents(c, pos, &water, e))
            return false;
        qa_vec3 water_end = pos;
        if (((uint32_t)water & Q2_WATER_MASK) == 0) {
            query.start = pos;
            query.end = water_start;
            query.pass_actor = trace.hit == QA_TRACE_HIT_ACTOR ? trace.actor : (qa_actor_id){0};
            query.policy.contents_mask = Q2_WATER_MASK;
            if (!qa_world_trace(c->game->services.world, &query, &trace, e))
                return false;
            water_end = trace.end;
        }
        if (!q2_event_named(c, QA_BUILTIN_BEAM, "q2:bubble-trail", 11, water_start, water_end, e))
            return false;
    }
    return true;
}
bool q2_bullet(q2_weapon_call *c, qa_vec3 start, qa_vec3 direction, float damage, float kick,
               float hs, float vs, int count, int mod, bool shotgun, qa_error *e) {
    for (int i = 0; i < count && q2_actor_live(c->game, c->actor->id); ++i)
        if (!lead(c, start, direction, damage, kick, hs, vs, mod, shotgun, e))
            return false;
    return true;
}
static bool rail_run(q2_weapon_call *c, qa_vec3 start, qa_vec3 direction, float damage, float kick,
                     int mod, uint32_t flags, qa_actor_id *excluded, qa_error *e) {
    qa_trace_query query = {.start = start,
                            .end = qa_vec_add(start, qa_vec_scale(direction, 8192)),
                            .pass_actor = c->actor->id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask =
        (c->rerelease ? (c->input.players_collide ? Q2_PROJECTILE_MASK
                                                  : Q2_PROJECTILE_MASK & ~Q2_PLAYER_CONTENTS)
                      : Q2_SHOT_MASK) |
        24u;
    qa_trace_result trace = {0};
    size_t count = 0, limit = c->rerelease ? 16 : c->game->capacity;
    bool water = false;
    qa_attack attack = q2_attack(c, mod, flags);
    for (;;) {
        if (!q2_actor_live(c->game, c->actor->id))
            return true;
        if (!qa_world_trace_excluding(c->game->services.world, &query, excluded, count, &trace, e))
            return false;
        if (trace.fraction == 1)
            break;
        if (((uint32_t)trace.contents & query.policy.contents_mask & 24u) != 0) {
            query.policy.contents_mask &= ~24u;
            water = true;
        } else {
            if (trace.hit != QA_TRACE_HIT_ACTOR)
                break;
            qa_actor_id target = trace.actor;
            qa_combat_state combat;
            if (!qa_actor_id_equal(target, c->actor->id) && damageable(c->game, target, &combat))
                if (!q2_damage(c->game, &attack, target, damage, kick, direction, trace.end,
                               trace.contact ? trace.contact_plane.normal : qa_v3(0, 0, 0), false,
                               e))
                    return false;
            qa_builtin_actor_traits traits = {0};
            if (c->game->services.actor_traits != NULL)
                c->game->services.actor_traits(c->game->services.context, target, &traits);
            qa_physics_properties physical;
            bool has_physics = c->game->services.physics != NULL &&
                               c->game->services.physics->services.read != NULL &&
                               c->game->services.physics->services.read(
                                   c->game->services.physics->services.context, target, &physical);
            qa_actor_collision collision;
            qa_error observed = {0};
            bool has_collision =
                qa_world_get_collision(c->game->services.world, target, &collision, &observed);
            if (observed.code) {
                if (e)
                    *e = observed;
                return false;
            }
            bool box = has_physics ? physical.solid == QA_PHYSICS_BOX
                                   : has_collision && !collision.inline_model &&
                                         collision.role != QA_COLLISION_TRIGGER;
            bool pierce =
                traits.monster || traits.player || box || (has_collision && collision.monster);
            if (c->rerelease && (!q2_actor_live(c->game, target) || traits.damageable_target ||
                                 (has_physics && (physical.solid == QA_PHYSICS_NOT_SOLID ||
                                                  physical.solid == QA_PHYSICS_TRIGGER))))
                pierce = true;
            bool duplicate = false;
            for (size_t i = 0; i < count; ++i)
                duplicate |= qa_actor_id_equal(excluded[i], target);
            if (!pierce || duplicate || count >= limit)
                break;
            excluded[count++] = target;
            if (!c->rerelease)
                query.pass_actor = target;
        }
        if (!c->rerelease)
            query.start = trace.end;
    }
    return q2_event_named(c, QA_BUILTIN_BEAM, "q2:rail", mod, start, trace.end, e) &&
           (!(water && !c->rerelease) || q2_event_named(c, QA_BUILTIN_BEAM,
                "q2:rail-water", 12, start, trace.end, e)) &&
           q2_noise_for_actor(c->game, c->actor->id, trace.end, true, e);
}
q2_trace_frame *q2_scratch_acquire(qa_q2_game *g, qa_error *e) {
    q2_trace_frame *scratch = g->trace_frames;
    while (scratch != NULL && scratch->active)
        scratch = scratch->next;
    if (scratch == NULL) {
        scratch = calloc(1, sizeof(*scratch));
        if (scratch == NULL) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating nested Q2 scratch");
            return NULL;
        }
        if (!qa_builtin_snapshot_reserve(&scratch->snapshot, g->capacity < 16 ? 16 : g->capacity,
                                         e)) {
            free(scratch);
            return NULL;
        }
        scratch->next = g->trace_frames;
        g->trace_frames = scratch;
    }
    scratch->active = true;
    return scratch;
}
q2_trace_frame *q2_nearby(qa_q2_game *g, qa_vec3 origin, float radius, qa_error *e) {
    q2_trace_frame *scratch = q2_scratch_acquire(g, e);
    if (scratch != NULL &&
        !qa_builtin_nearby(&g->services, origin, radius, &scratch->snapshot, e)) {
        scratch->active = false;
        return NULL;
    }
    return scratch;
}
q2_trace_frame *q2_player_roster(qa_q2_game *g, qa_error *e) {
    q2_trace_frame *scratch = q2_scratch_acquire(g, e);
    if (scratch != NULL && !qa_builtin_players(&g->services, &scratch->snapshot, e)) {
        scratch->active = false;
        return NULL;
    }
    return scratch;
}
bool q2_rail(q2_weapon_call *c, qa_vec3 start, qa_vec3 direction, float damage, float kick, int mod,
             uint32_t flags, qa_error *e) {
    q2_trace_frame *scratch = q2_scratch_acquire(c->game, e);
    if (scratch == NULL)
        return false;
    bool result = rail_run(c, start, direction, damage, kick, mod, flags, scratch->snapshot.ids, e);
    scratch->active = false;
    return result;
}

bool q2_heatbeam(q2_weapon_call *c, qa_vec3 start, qa_vec3 direction, float damage, float kick,
                 qa_error *e) {
    direction = qa_vec_normalize(direction);
    int32_t content;
    if (!contents(c, start, &content, e))
        return false;
    bool underwater = ((uint32_t)content & Q2_WATER_MASK) != 0, water = false;
    qa_vec3 water_start = start;
    qa_trace_query query = {.start = start,
                            .end = qa_vec_add(start, qa_vec_scale(direction, 8192)),
                            .pass_actor = c->actor->id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    uint32_t mask = c->rerelease
                        ? (c->input.players_collide ? Q2_PROJECTILE_MASK
                                                    : Q2_PROJECTILE_MASK & ~Q2_PLAYER_CONTENTS)
                        : Q2_SHOT_MASK;
    query.policy.contents_mask = mask | (underwater ? 0 : Q2_WATER_MASK);
    qa_trace_result trace;
    if (!qa_world_trace(c->game->services.world, &query, &trace, e))
        return false;
    if (((uint32_t)trace.contents & Q2_WATER_MASK) != 0) {
        water = true;
        water_start = trace.end;
        if (qa_vec_length(qa_vec_sub(start, water_start)) != 0 &&
            !q2_event_named(c, QA_BUILTIN_IMPACT, "q2:heatbeam_sparks", 44,
                water_start, trace.contact_plane.normal, e))
            return false;
        query.start = water_start;
        query.policy.contents_mask = mask;
        if (!qa_world_trace(c->game->services.world, &query, &trace, e))
            return false;
    }
    qa_actor_id target = trace.hit == QA_TRACE_HIT_ACTOR ? trace.actor : (qa_actor_id){0};
    qa_combat_state combat;
    if (!sky(&trace) && trace.fraction < 1) {
        if (damageable(c->game, target, &combat)) {
            qa_attack attack = q2_attack(c, 44, 4);
            if (!q2_damage(c->game, &attack, target, water ? truncf(damage * 0.5f) : damage, kick,
                           direction, trace.end, trace.contact_plane.normal, false, e))
                return false;
        } else if (!water &&
                   !q2_event_named(c, QA_BUILTIN_IMPACT, "q2:heatbeam_steam", 45,
                       trace.end, trace.contact_plane.normal, e))
            return false;
    }
    qa_vec3 end = trace.end;
    if (water || underwater) {
        qa_vec3 pos = qa_vec_add(end,
                                 qa_vec_scale(qa_vec_normalize(qa_vec_sub(end, water_start)), -2)),
                water_end = pos;
        if (!contents(c, pos, &content, e))
            return false;
        if (((uint32_t)content & Q2_WATER_MASK) == 0) {
            query.start = pos;
            query.end = water_start;
            query.pass_actor = target;
            query.policy.contents_mask = Q2_WATER_MASK;
            if (!qa_world_trace(c->game->services.world, &query, &trace, e))
                return false;
            water_end = trace.end;
        }
        if (!q2_event_named(c, QA_BUILTIN_BEAM, "q2:bubble-trail", 11, water_start, water_end, e))
            return false;
    }
    bool player;
    if (!q2_target_creature(c->game, c->actor->id, NULL, &player, e)) return false;
    return q2_event_named(c, QA_BUILTIN_BEAM,
        player ? "q2:heatbeam" : "q2:monster-heatbeam", 44, start, end, e);
}
static qa_vec3 closest(qa_vec3 p, qa_bounds b) {
    return qa_v3(fmaxf(b.mins.x, fminf(b.maxs.x, p.x)), fmaxf(b.mins.y, fminf(b.maxs.y, p.y)),
                 fmaxf(b.mins.z, fminf(b.maxs.z, p.z)));
}
static bool chainfist_run(q2_weapon_call *c, qa_builtin_actor_snapshot *snapshot, qa_error *e) {
    qa_q2_weapon_state *s = c->state;
    if (c->rerelease && !q2_continues(c) && (s->frame == 13 || s->frame == 23 || s->frame >= 32)) {
        s->frame = 33;
        return true;
    }
    qa_vec3 start, dir;
    if (!q2_project(c, c->input.angles, qa_v3(0, c->rerelease ? 0 : 8, -4), &start, &dir, e))
        return false;
    qa_body_state own;
    if (!qa_world_body_read(c->game->services.world, c->actor->id, &own, e))
        return false;
    if (!c->rerelease) {
        qa_vec3 f, u;
        qa_builtin_angle_vectors(c->input.angles, &f, NULL, &u);
        q2_kick(c, qa_vec_scale(f, -2), qa_v3(-1, 0, 0), 0);
        qa_trace_query query = {.start = start,
                                .end = qa_vec_add(start, qa_vec_scale(dir, 64)),
                                .pass_actor = c->actor->id,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        query.policy.contents_mask = Q2_SHOT_MASK;
        qa_trace_result trace;
        if (!qa_world_trace(c->game->services.world, &query, &trace, e))
            return false;
        qa_combat_state combat;
        if (trace.fraction < 1 && trace.hit == QA_TRACE_HIT_ACTOR &&
            damageable(c->game, trace.actor, &combat)) {
            own.velocity =
                qa_vec_add(qa_vec_add(own.velocity, qa_vec_scale(f, 75)), qa_vec_scale(u, 75));
            if (!qa_world_body_write(c->game->services.world, c->actor->id, &own, e))
                return false;
            qa_attack attack = q2_attack(c, 40, 72);
            qa_body_state target;
            float multiplier;
            if (!q2_multiplier(c, &multiplier, e) ||
                !qa_world_body_read(c->game->services.world, trace.actor, &target, e) ||
                !q2_damage(c->game, &attack, trace.actor,
                           (c->game->options.deathmatch ? 30 : 15) * multiplier, 50, qa_v3(0, 0, 0),
                           target.origin, qa_v3(0, 0, 0), false, e))
                return false;
        } else if (trace.fraction < 1 &&
                   !q2_event(c, QA_BUILTIN_IMPACT, 4, trace.end, trace.contact_plane.normal, e))
            return false;
        ++s->frame;
        return q2_noise(c, start, e) &&
               q2_weapon_fired(c->game, c->actor->id, c->definition->weapon, e);
    }
    qa_bounds own_bounds = qa_bounds_translate(own.bounds, own.origin), search = own_bounds;
    search.mins = qa_vec_sub(search.mins, qa_v3(23, 23, 23));
    search.maxs = qa_vec_add(search.maxs, qa_v3(23, 23, 23));
    if (!qa_builtin_observations(&c->game->services, snapshot, e))
        return false;
    int count = 0;
    bool hit = false;
    for (size_t i = 0; i < snapshot->count; ++i) {
        qa_actor_id target = snapshot->ids[i];
        qa_combat_state combat;
        if (qa_actor_id_equal(target, c->actor->id) || !damageable(c->game, target, &combat))
            continue;
        q2_actor *native = target.slot < c->game->capacity ? c->game->actors[target.slot] : NULL;
        bool source =
            native != NULL && qa_actor_id_equal(native->id, target) && native->physics_bound;
        if (source && (native->physics.solid == QA_PHYSICS_NOT_SOLID ||
                       native->physics.solid == QA_PHYSICS_TRIGGER))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(c->game->services.world, target, &body, e))
            return false;
        qa_bounds bounds = qa_bounds_translate(body.bounds, body.origin);
        if (!qa_bounds_overlap(bounds, search))
            continue;
        qa_vec3 point = closest(start, bounds), near = closest(point, own_bounds);
        if (qa_vec_length(qa_vec_sub(point, near)) > 24)
            continue;
        qa_bounds a = bounds, b = own_bounds;
        a.mins = qa_vec_add(a.mins, qa_v3(2, 2, 2));
        a.maxs = qa_vec_sub(a.maxs, qa_v3(2, 2, 2));
        b.mins = qa_vec_add(b.mins, qa_v3(2, 2, 2));
        b.maxs = qa_vec_sub(b.maxs, qa_v3(2, 2, 2));
        if (!qa_bounds_overlap(a, b) &&
            qa_vec_dot(qa_vec_normalize(qa_vec_sub(
                           qa_vec_scale(qa_vec_add(bounds.mins, bounds.maxs), 0.5f), start)),
                       dir) < 0.7f)
            continue;
        if (++count > 4)
            break;
        bool visible;
        qa_trace_policy policy = qa_collision_default_policy(QA_COLLISION_Q2);
        policy.contents_mask = 1;
        if (!qa_builtin_can_damage(&c->game->services, source ? body.origin : own.origin,
                                   source ? c->actor->id : target, source ? target : c->actor->id,
                                   policy, false, &visible, e))
            return false;
        if (!visible)
            continue;
        if (target.slot < c->game->capacity && c->game->actors[target.slot] != NULL &&
            qa_actor_id_equal(c->game->actors[target.slot]->id, target) &&
            c->game->actors[target.slot]->monster != NULL &&
            c->game->actors[target.slot]->projectile.kind == Q2_PROJECTILE_NONE) {
            uint64_t advance = (uint64_t)((0.005 + q2_random(c->game) * 0.07) * 1e9);
            q2_monster_pain_advance(c->game, target, advance);
        }
        qa_attack attack = q2_attack(c, 40, 72);
        float multiplier;
        if (!q2_multiplier(c, &multiplier, e) || !q2_damage(c->game, &attack, target,
                       (c->game->options.deathmatch ? 15 : 7) * multiplier, 50, dir, point,
                       qa_vec_scale(dir, -1), false, e))
            return false;
        hit = true;
    }
    if (hit && s->empty_sound_ns < c->now_ns) {
        s->empty_sound_ns = q2_deadline(c->now_ns, 500 * Q2_MS);
        if (!q2_sound(c, "weapons/sawslice.wav", 1, 1, e))
            return false;
    }
    if (!q2_noise(c, start, e))
        return false;
    ++s->frame;
    if (q2_continues(c)) {
        if (s->frame == 12)
            s->frame = 14;
        else if (s->frame == 22)
            s->frame = 24;
        else if (s->frame >= 32)
            s->frame = 7;
    }
    return q2_attack_animation(c, 1, e) &&
           q2_weapon_fired(c->game, c->actor->id, c->definition->weapon, e);
}
bool q2_fire_chainfist(q2_weapon_call *c, qa_error *e) {
    if (!c->rerelease)
        return chainfist_run(c, NULL, e);
    q2_trace_frame *scratch = q2_scratch_acquire(c->game, e);
    if (scratch == NULL)
        return false;
    bool ok = chainfist_run(c, &scratch->snapshot, e);
    scratch->active = false;
    return ok;
}
