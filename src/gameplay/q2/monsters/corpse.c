#include "internal.h"

static void schedule(q2m_context *c, q2m_corpse_phase phase, double seconds) {
    c->monster->corpse_phase = phase;
    c->monster->corpse_due_ns = q2m_after(c->game->now_ns, seconds);
}

static bool wet(q2m_context *c, bool *result, qa_error *error) {
    qa_point_query query = {.point = c->body.origin,
                           .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.point.z += c->body.bounds.mins.z + 1;
    qa_point_contents contents;
    if (!qa_world_point_contents(c->game->services.world, &query, &contents, error))
        return false;
    *result = ((uint32_t)contents.contents & Q2M_WATER_MASK) != 0;
    return true;
}

static bool flies(q2m_context *c, bool enabled, qa_error *error) {
    if (enabled)
        c->actor->extra_effects |= UINT64_C(0x4000);
    else
        c->actor->extra_effects &= ~UINT64_C(0x4000);
    qa_builtin_event event = {.kind = enabled ? QA_BUILTIN_SOUND : QA_BUILTIN_STOP_SOUND,
                              .family = QA_GAME_Q2,
                              .provider = c->game->options.owner,
                              .actor = c->actor->id,
                              .time_ns = c->game->now_ns,
                              .origin = c->body.origin,
                              .volume = 1,
                              .attenuation = 1,
                              .flags = enabled ? 1u : 0u};
    if (!qa_builtin_resource(&c->game->services, "infantry/inflies1.wav",
                             &event.resource, error))
        return false;
    if (!q2m_alive(c))
        return true;
    if (!qa_builtin_emit(&c->game->services, &event, error))
        return false;
    return !q2m_alive(c) || q2m_show(c, error);
}

static bool dead_think_species(q2m_species species) {
    switch (species) {
    case Q2M_INFANTRY:
    case Q2M_TURRET_DRIVER:
    case Q2M_SOLDIER_LIGHT:
    case Q2M_SOLDIER:
    case Q2M_SOLDIER_SS:
    case Q2M_BERSERK:
    case Q2M_BRAIN:
    case Q2M_CHICK:
    case Q2M_CHICK_HEAT:
    case Q2M_FLIPPER:
    case Q2M_GLADIATOR:
    case Q2M_GLADB:
    case Q2M_GUNNER:
    case Q2M_MAKRON:
    case Q2M_MEDIC:
    case Q2M_MEDIC_COMMANDER:
    case Q2M_MUTANT:
    case Q2M_PARASITE:
    case Q2M_TANK:
    case Q2M_TANK_COMMANDER:
    case Q2M_INSANE:
    case Q2M_GUN_COMMANDER:
    case Q2M_SHAMBLER:
        return true;
    default:
        return false;
    }
}

bool q2m_corpse_phase_valid(const qa_q2_game *game, q2m_species species,
                            q2m_corpse_phase phase) {
    switch (phase) {
    case Q2M_CORPSE_IDLE:
        return true;
    case Q2M_CORPSE_HOVER:
        return species == Q2M_HOVER || species == Q2M_DAEDALUS;
    case Q2M_CORPSE_DEAD_THINK:
        return game->options.edition == QA_Q2_RERELEASE && dead_think_species(species);
    case Q2M_CORPSE_FLIES_ON:
    case Q2M_CORPSE_FLIES_OFF:
        return game->options.edition == QA_Q2_CLASSIC &&
               (species == Q2M_INFANTRY || species == Q2M_TURRET_DRIVER ||
                species == Q2M_MUTANT);
    }
    return false;
}

bool q2m_corpse_callback(q2m_context *c, q2m_callback_id callback, qa_error *error) {
    if (!q2m_alive(c) || c->monster->corpse)
        return true;
    struct qa_q2_monster *m = c->monster;
    q2m_species species = m->definition->species;
    bool rerelease = c->game->options.edition == QA_Q2_RERELEASE;
    if (species == Q2M_GEKK && m->water_level > 0)
        return true;
    bool widow2 = species == Q2M_WIDOW2;
    bool hover = species == Q2M_HOVER || species == Q2M_DAEDALUS;
    bool hanging = species == Q2M_INSANE && (m->spawnflags & 8u);
    qa_bounds bounds = {.mins = {-16, -16, -24}, .maxs = {16, 16, -8}};
    if (hanging || (rerelease && species == Q2M_MUTANT))
        bounds = c->body.bounds;
    else if (species == Q2M_SUPERTANK || species == Q2M_BOSS5 || species == Q2M_MAKRON)
        bounds = (qa_bounds){.mins = {-60, -60, 0},
                             .maxs = {60, 60, rerelease && species == Q2M_MAKRON ? 24 : 72}};
    else if (species == Q2M_BOSS2 || species == Q2M_CARRIER || species == Q2M_WIDOW)
        bounds = (qa_bounds){.mins = {-56, -56, 0}, .maxs = {56, 56, 80}};
    else if (widow2)
        bounds = (qa_bounds){.mins = {-70, -70, 0}, .maxs = {70, 70, 80}};
    else if (species == Q2M_TANK || species == Q2M_TANK_COMMANDER)
        bounds = (qa_bounds){.mins = {-16, -16, -16}, .maxs = {16, 16, 0}};
    else if (species == Q2M_STALKER)
        bounds = (qa_bounds){.mins = {-28, -28, -18}, .maxs = {28, 28, -4}};
    else if (rerelease && (species == Q2M_CHICK || species == Q2M_CHICK_HEAT))
        bounds = (qa_bounds){.mins = {-16, -16, 0}, .maxs = {16, 16, 8}};
    else if (rerelease && species == Q2M_FLIPPER)
        bounds = (qa_bounds){.mins = {-16, -16, -8}, .maxs = {16, 16, 8}};
    else if (species == Q2M_SHAMBLER)
        bounds.maxs.z = 0;
    else if (species == Q2M_GUN_COMMANDER) {
        bounds.mins = qa_vec_scale(bounds.mins, m->entity_scale);
        bounds.maxs = qa_vec_scale(bounds.maxs, m->entity_scale);
    }
    if (callback && (callback == Q2M_CALLBACK_soldier_dead2)) {
        qa_trace_query query = {.start = qa_vec_add(c->body.origin, qa_v3(0, 0, 1)),
                                .pass_actor = c->actor->id,
                                .shape = {.kind = QA_SHAPE_BOX,
                                          .bounds = {.mins = {-32, -32, -24},
                                                     .maxs = {32, 32, -8}}},
                                .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        query.end = query.start;
        query.policy.contents_mask = 3;
        qa_trace_result trace;
        if (!qa_world_trace(c->game->services.world, &query, &trace, error))
            return false;
        if (!q2m_alive(c))
            return true;
        if (!trace.start_solid && !trace.all_solid)
            bounds = query.shape.bounds;
    }
    m->corpse = true;
    m->corpse_phase = Q2M_CORPSE_IDLE;
    m->corpse_due_ns = 0;
    m->next_frame_ns = UINT64_MAX;
    if (widow2) {
        if (!q2m_damageable(c, true, error))
            return false;
        if (!q2m_alive(c))
            return true;
        if (!qa_world_body_read(c->game->services.world, c->actor->id, &c->body, error))
            return false;
        if (!q2m_alive(c))
            return true;
    }
    c->body.bounds = bounds;
    if (hanging)
        c->actor->physics.flags |= QA_PHYSICS_FLYING;
    else
        c->actor->physics.motion = QA_PHYSICS_TOSS;
    if (!hover && !widow2) {
        c->actor->physics.solid = QA_PHYSICS_CORPSE;
        c->actor->physics.flags |= QA_PHYSICS_DEAD;
        qa_actor_collision collision = {.family = QA_COLLISION_Q2,
                                        .shape = QA_SHAPE_BOX,
                                        .contents = qa_collision_q2_source_contents(2, 2,
                                            c->game->options.edition == QA_Q2_RERELEASE),
                                        .role = QA_COLLISION_SOLID,
                                        .dead_monster = true};
        if (!qa_world_set_collision(c->game->services.world, c->actor->id, &collision, error))
            return false;
    }
    if (!hanging && !q2m_write_body(c, false, error))
        return false;
    if (!q2m_alive(c))
        return true;
    if (!q2m_link(c, error))
        return false;
    if (!q2m_alive(c))
        return true;
    if (hover) {
        m->corpse_end_ns = q2m_after(c->game->now_ns, 15);
        schedule(c, Q2M_CORPSE_HOVER, .1);
    } else if (rerelease && dead_think_species(species) &&
               (!callback || (callback != (Q2M_CALLBACK_soldier_dead2)))) {
        m->flies_ns = 0;
        schedule(c, Q2M_CORPSE_DEAD_THINK, .1);
    } else if (!rerelease && (species == Q2M_INFANTRY || species == Q2M_TURRET_DRIVER ||
                              species == Q2M_MUTANT)) {
        bool underwater;
        if (!wet(c, &underwater, error))
            return false;
        if (!q2m_alive(c) || underwater || q2m_random(c->game) > .5f)
            return true;
        schedule(c, Q2M_CORPSE_FLIES_ON, 5 + 10 * q2m_random(c->game));
    }
    return true;
}

bool q2m_corpse(q2m_context *c, qa_error *error) {
    return q2m_corpse_callback(c, Q2M_CALLBACK_NONE, error);
}

bool q2m_corpse_tick(q2m_context *c, bool *handled, qa_error *error) {
    struct qa_q2_monster *m = c->monster;
    *handled = m->corpse;
    if (!*handled || m->corpse_phase == Q2M_CORPSE_IDLE ||
        c->game->now_ns < m->corpse_due_ns)
        return true;
    q2m_corpse_phase phase = m->corpse_phase;
    m->corpse_phase = Q2M_CORPSE_IDLE;
    m->corpse_due_ns = 0;
    switch (phase) {
    case Q2M_CORPSE_FLIES_ON: {
        bool underwater;
        if (!wet(c, &underwater, error))
            return false;
        if (!q2m_alive(c) || underwater)
            return true;
        if (!flies(c, true, error))
            return false;
        if (q2m_alive(c))
            schedule(c, Q2M_CORPSE_FLIES_OFF, 60);
        return true;
    }
    case Q2M_CORPSE_FLIES_OFF:
        return flies(c, false, error);
    case Q2M_CORPSE_DEAD_THINK:
        if (m->definition->species == Q2M_INFANTRY ||
            m->definition->species == Q2M_MUTANT) {
            if (!m->flies_ns)
                m->flies_ns = q2m_after(c->game->now_ns, 5 + 10 * q2m_random(c->game));
            else if (m->flies_ns < c->game->now_ns) {
                bool enabled = !(c->actor->extra_effects & UINT64_C(0x4000));
                if (!flies(c, enabled, error))
                    return false;
                if (!q2m_alive(c))
                    return true;
                m->flies_ns = enabled ? q2m_after(c->game->now_ns, 60) : UINT64_MAX;
            }
        }
        if (m->frame != m->move->last_frame) {
            if (m->frame == INT_MAX) {
                qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 corpse frame overflow");
                return false;
            }
            ++m->frame;
        }
        if (!q2m_show(c, error))
            return false;
        if (q2m_alive(c))
            schedule(c, Q2M_CORPSE_DEAD_THINK, .1);
        return true;
    case Q2M_CORPSE_HOVER:
        if (!(c->actor->physics.flags & QA_PHYSICS_ONGROUND) &&
            !qa_actor_reference_present(c->body.ground) && c->game->now_ns < m->corpse_end_ns) {
            schedule(c, Q2M_CORPSE_HOVER, .1);
            return true;
        }
        return q2m_hover_explode(c, error);
    case Q2M_CORPSE_IDLE:
        return true;
    }
    return false;
}

bool q2m_hover_dying(q2m_context *c, qa_error *error) {
    if ((c->actor->physics.flags & QA_PHYSICS_ONGROUND) || qa_actor_reference_present(c->body.ground))
        return q2m_hover_explode(c, error);
    if (q2_random_bounded(c->game, 2))
        return true;
    if (!q2m_emit(c, QA_BUILTIN_EXPLOSION, "q2:plain-explosion", 0,
                  c->body.origin, c->body.origin, 1, error))
        return false;
    if (!q2m_alive(c))
        return true;
    bool organic = q2_random_bounded(c->game, 2) != 0;
    return q2_spawn_gib(c->game, c->actor->id,
                        organic ? "models/objects/gibs/sm_meat/tris.md2"
                                : "models/objects/gibs/sm_metal/tris.md2",
                        120, organic ? 0 : Q2_GIB_METALLIC,
                        c->monster->skin, c->monster->entity_scale != 0 ? c->monster->entity_scale : 1, error);
}
