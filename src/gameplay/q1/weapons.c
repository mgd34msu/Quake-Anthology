#include "internal.h"

double q1_ammo_count(qa_q1_game *g, qa_actor_id actor, qa_q1_ammo ammo) {
    qa_inventory_entry entry;
    return qa_inventory_entry_read(g->services.inventory, actor, g->ammo[ammo], &entry, NULL)
               ? entry.count
               : 0;
}
bool q1_consume(qa_q1_game *g, qa_actor_id actor, qa_q1_ammo ammo, float amount, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (player && player->mg3_infinite_ammo)
        return true;
    bool consumed;
    if (!qa_inventory_consume(g->services.inventory, actor, g->ammo[ammo], amount, &consumed,
                              error))
        return false;
    if (!consumed)
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 ammunition changed during admitted attack");
    return consumed;
}
static bool owns(qa_q1_game *g, qa_actor_id actor, qa_q1_weapon weapon) {
    qa_inventory_entry entry;
    return qa_inventory_entry_read(g->services.inventory, actor, g->weapons[weapon], &entry,
                                   NULL) &&
           entry.count > 0;
}
int q1_weapon_ammo(qa_q1_weapon weapon) {
    static const int ammunition[QA_Q1_WEAPON_COUNT] = {-1,
                                                       QA_Q1_SHELLS,
                                                       QA_Q1_SHELLS,
                                                       QA_Q1_NAILS,
                                                       QA_Q1_NAILS,
                                                       QA_Q1_ROCKETS,
                                                       QA_Q1_ROCKETS,
                                                       QA_Q1_CELLS,
                                                       QA_Q1_CELLS,
                                                       -1,
                                                       QA_Q1_ROCKETS,
                                                       QA_Q1_LAVA_NAILS,
                                                       QA_Q1_LAVA_NAILS,
                                                       QA_Q1_MULTI_ROCKETS,
                                                       QA_Q1_MULTI_ROCKETS,
                                                       QA_Q1_PLASMA_CELLS,
                                                       -1,
                                                       QA_Q1_CELLS,
                                                       -1,
                                                       -1};
    return ammunition[weapon];
}
qa_q1_weapon q1_best_weapon(qa_q1_game *g, q1_player *player) {
    return q1_best_weapon_before(g, player, NULL, 0);
}
qa_q1_weapon q1_best_weapon_before(qa_q1_game *g, q1_player *player,
                                   const qa_pickup_receipt *receipts, size_t count) {
    static const qa_q1_weapon base[] = {QA_Q1_LIGHTNING, QA_Q1_SUPER_NAILGUN, QA_Q1_SUPER_SHOTGUN,
                                        QA_Q1_NAILGUN,   QA_Q1_SHOTGUN,       QA_Q1_AXE};
    static const qa_q1_weapon hipnotic[] = {QA_Q1_LIGHTNING,     QA_Q1_LASER,   QA_Q1_SUPER_NAILGUN,
                                            QA_Q1_SUPER_SHOTGUN, QA_Q1_NAILGUN, QA_Q1_SHOTGUN,
                                            QA_Q1_MJOLNIR,       QA_Q1_AXE};
    static const qa_q1_weapon rogue[] = {
        QA_Q1_LIGHTNING, QA_Q1_LAVA_SUPER_NAILGUN, QA_Q1_SUPER_NAILGUN, QA_Q1_LAVA_NAILGUN,
        QA_Q1_NAILGUN,   QA_Q1_SUPER_SHOTGUN,      QA_Q1_SHOTGUN,       QA_Q1_AXE};
    static const qa_q1_weapon mg3[] = {QA_Q1_LIGHTNING, QA_Q1_SUPER_NAILGUN, QA_Q1_SUPER_SHOTGUN,
                                       QA_Q1_NAILGUN,   QA_Q1_SHOTGUN,       QA_Q1_MG3_MJOLNIR,
                                       QA_Q1_AXE};
    const qa_q1_weapon *order = base;
    size_t length = sizeof(base) / sizeof(*base);
    if (g->options.program == QA_Q1_HIPNOTIC) {
        order = hipnotic;
        length = sizeof(hipnotic) / sizeof(*hipnotic);
    }
    if (g->options.program == QA_Q1_ROGUE) {
        order = rogue;
        length = sizeof(rogue) / sizeof(*rogue);
    }
    if (g->options.program == QA_Q1_MG3) {
        order = mg3;
        length = sizeof(mg3) / sizeof(*mg3);
    }
    for (size_t i = 0; i < length; ++i) {
        qa_q1_weapon weapon = order[i];
        int ammo = q1_weapon_ammo(weapon);
        if (!owns(g, player->id, weapon) ||
            (weapon == QA_Q1_LIGHTNING && player->input.water_level > 1))
            continue;
        float needed = weapon == QA_Q1_SUPER_NAILGUN || weapon == QA_Q1_SUPER_SHOTGUN ||
                               weapon == QA_Q1_LAVA_SUPER_NAILGUN
                           ? 2
                           : 1;
        double available = ammo < 0 ? 1 : q1_ammo_count(g, player->id, (qa_q1_ammo)ammo);
        if (ammo >= 0)
            for (size_t j = 0; j < count; ++j)
                if (receipts[j].item == g->ammo[ammo]) {
                    available = receipts[j].before;
                    break;
                }
        if (available >= needed)
            return weapon;
    }
    return QA_Q1_AXE;
}
bool q1_weapon_event(qa_q1_game *g, q1_player *player, float punch, int32_t attack,
                     qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_ANIMATION,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = player->id,
                              .time_ns = g->time_ns,
                              .resource = g->weapon_models[player->weapon],
                              .frame = player->weapon_frame,
                              .value = punch,
                              .code = attack,
                              .flags = (uint32_t)player->weapon};
    const char *model = player->weapon == QA_Q1_MG3_MJOLNIR && player->mg3_hammer_glow &&
                                player->mg3_hammer_until > g->time
                            ? "progs/v_hammer_glow.mdl"
                        : player->weapon == QA_Q1_SHOTGUN && (player->mg3_progress.bloody & 1)
                            ? "progs/v_bloodshot.mdl"
                        : player->weapon == QA_Q1_SUPER_SHOTGUN && (player->mg3_progress.bloody & 2)
                            ? "progs/v_bloodshot2.mdl"
                            : NULL;
    if (model && !qa_builtin_resource(&g->services, model, &event.resource, error))
        return false;
    return qa_builtin_emit(&g->services, &event, error);
}
bool qa_q1_player_attach(qa_q1_game *g, qa_actor_id actor, bool initial_inventory,
                         qa_error *error) {
    if (!g || !q1_alive(g, actor)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 arsenal needs a live shared player");
        return false;
    }
    q1_player *existing = q1_player_get(g, actor);
    if (existing && existing->arsenal)
        return true;
    if (initial_inventory) {
        qa_inventory_entry entries[QA_Q1_WEAPON_COUNT + QA_Q1_AMMO_COUNT];
        for (size_t i = 0; i < QA_Q1_WEAPON_COUNT; ++i)
            entries[i] = (qa_inventory_entry){.item = g->weapons[i],
                                              .count = i == QA_Q1_AXE || i == QA_Q1_SHOTGUN ? 1 : 0,
                                              .capacity = 1,
                                              .policy = QA_COUNT_SOURCE_FLOAT};
        if (g->options.program == QA_Q1_ROGUE && g->options.deathmatch && g->options.teamplay >= 4)
            entries[QA_Q1_ROGUE_GRAPPLE].count = 1;
        for (size_t i = 0; i < QA_Q1_AMMO_COUNT; ++i)
            entries[QA_Q1_WEAPON_COUNT + i] = (qa_inventory_entry){
                .item = g->ammo[i],
                .count = i == QA_Q1_SHELLS ? 25 : 0,
                .capacity = i == QA_Q1_NAILS || i == QA_Q1_LAVA_NAILS ? 200 : 100,
                .policy = QA_COUNT_SOURCE_FLOAT};
        size_t count = sizeof(entries) / sizeof(*entries);
        if (!qa_inventory_has(g->services.inventory, actor)) {
            if (!qa_inventory_create_actor(g->services.inventory, actor, entries, count, error))
                return false;
        } else
            for (size_t i = 0; i < count; ++i)
                if (!qa_inventory_configure(g->services.inventory, actor, &entries[i], NULL, NULL,
                                            error))
                    return false;
    }
    q1_player *player = q1_player_allocate(g, actor, error);
    if (!player)
        return false;
    player->arsenal = true;
    if (initial_inventory && g->options.program == QA_Q1_MG3 &&
        !q1_mg3_capacities(g, player, error))
        return false;
    qa_body_state body;
    if (qa_world_body_read(g->services.world, actor, &body, NULL))
        player->input.view_angles = body.angles;
    return q1_weapon_event(g, player, 0, 0, error);
}
bool qa_q1_player_input(qa_q1_game *g, qa_actor_id actor, const qa_q1_input *input,
                        qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal || !input || !qa_vec_finite(input->view_angles) ||
        input->water_level > 3) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "invalid Q1 player input");
        return false;
    }
    player->input = *input;
    player->grapple_input = *input;
    player->grapple_release = !input->attack && (player->weapon == QA_Q1_ROGUE_GRAPPLE ||
                                                 player->weapon == QA_Q1_CTF_GRAPPLE);
    if (!input->attack && player->continuous) {
        player->continuous = false;
        player->animation_at = -1;
        player->weapon_frame = 0;
        return q1_weapon_event(g, player, 0, 0, error);
    }
    return true;
}
bool qa_q1_player_select(qa_q1_game *g, qa_actor_id actor, qa_q1_weapon weapon, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal || weapon < QA_Q1_AXE || weapon >= QA_Q1_WEAPON_COUNT ||
        !owns(g, actor, weapon))
        return false;
    player->weapon = weapon;
    player->weapon_frame = 0;
    player->continuous = false;
    player->animation_at = -1;
    return q1_weapon_event(g, player, 0, 0, error);
}
bool qa_q1_player_read(const qa_q1_game *g, qa_actor_id actor, qa_q1_player_view *out) {
    if (!g || !out || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return false;
    const q1_player *player = g->players[actor.slot];
    if (!player || !player->active || !player->arsenal || !qa_actor_id_equal(player->id, actor))
        return false;
    *out = (qa_q1_player_view){.weapon = player->weapon,
                               .weapon_frame = player->weapon_frame,
                               .punch_angles = player->punch,
                               .max_health = player->max_health,
                               .holstered = player->input.holstered};
    memcpy(out->power_expires, player->power_expires, sizeof(out->power_expires));
    return true;
}
bool qa_q1_player_power(qa_q1_game *g, qa_actor_id actor, qa_q1_power power, double expires,
                        qa_error *error) {
    q1_player *player = q1_player_allocate(g, actor, error);
    if (!player || power < QA_Q1_QUAD || power >= QA_Q1_POWER_COUNT || !isfinite(expires)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "invalid Q1 timed power");
        return false;
    }
    if (power == QA_Q1_ANTIGRAV && (expires > g->time || player->power_expires[power] != 0)) {
        if (!g->host.set_gravity) {
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                         "Q1 anti-gravity requires selected gravity owner");
            return false;
        }
        if (!g->host.set_gravity(g->host.context, actor, expires > g->time ? 0.25f : 1, error))
            return false;
        if (!q1_alive(g, actor))
            return true;
    }
    player->power_expires[power] = expires;
    player->power_warned &= (uint16_t)~(1u << power);
    return q1_effect(g, QA_BUILTIN_ITEM, actor, qa_v3(0, 0, 0), (float)expires, (int32_t)power,
                     error);
}
bool qa_q1_game_invulnerable(const qa_q1_game *g, qa_actor_id actor) {
    return g && qa_q1_game_power_expires(g, actor, QA_Q1_INVULNERABILITY) > g->time;
}
double qa_q1_game_power_expires(const qa_q1_game *g, qa_actor_id actor, qa_q1_power power) {
    if (power < QA_Q1_QUAD || power >= QA_Q1_POWER_COUNT)
        return 0;
    if (!g || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return 0;
    const q1_player *player = g->players[actor.slot];
    return player && player->active && qa_actor_id_equal(player->id, actor)
               ? player->power_expires[power]
               : 0;
}
bool qa_q1_player_prethink(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player)
        return true;
    if (g->options.program == QA_Q1_MG3 && !q1_mg3_weapon_frame(g, player, error))
        return false;
    if (!q1_power_frame(g, player, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    if (!q1_grapple_frame(g, player, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    if (player->mega_rot_at >= 0 && player->mega_rot_at <= g->time) {
        float health = q1_health(g, actor);
        if (health > player->max_health) {
            if (!qa_combat_set_health(g->services.combat, actor, health - 1, error))
                return false;
            player->mega_rot_at = g->time + 1;
        } else
            player->mega_rot_at = -1;
    }
    for (size_t i = 0; i < QA_Q1_POWER_COUNT; ++i)
        if (player->power_expires[i] > 0 && player->power_expires[i] <= g->time &&
            !qa_q1_player_power(g, actor, (qa_q1_power)i, 0, error))
            return false;
    if (!player->continuous && player->animation_at >= 0 && player->weapon != QA_Q1_ROGUE_GRAPPLE &&
        player->weapon != QA_Q1_CTF_GRAPPLE) {
        int32_t frame = (int32_t)floor((g->time - player->animation_at) / 0.1);
        int32_t count = player->weapon == QA_Q1_AXE || player->weapon == QA_Q1_MG3_MJOLNIR ? 4 : 6;
        int32_t next = frame >= count ? 0 : player->animation_base + frame;
        if (next != player->weapon_frame) {
            player->weapon_frame = next;
            if (!q1_weapon_event(g, player, 0, 0, error))
                return false;
        }
        if (frame >= count)
            player->animation_at = -1;
    }
    float magnitude = qa_vec_length(player->punch);
    if (magnitude > 0)
        player->punch =
            qa_vec_scale(player->punch, fmaxf(0, magnitude - (float)g->elapsed * 10) / magnitude);
    return true;
}
bool q1_environment_damage(qa_q1_game *g, qa_actor_id actor, float amount, qa_hazard hazard,
                           qa_error *error) {
    qa_damage_request request = {.attack =
                                     q1_attack(g, (qa_actor_id){0}, actor, QA_Q1_WEAPON_COUNT),
                                 .target = actor,
                                 .amount = amount};
    request.attack.cause = (qa_damage_cause){.kind = QA_CAUSE_ENVIRONMENT, .source.hazard = hazard};
    if (g->host.combat_provider)
        request.attack.combat_provider = g->host.combat_provider(g->host.context, actor);
    if (!qa_attack_next(&g->attack_sequence, &request.attack, error))
        return false;
    qa_damage_outcome outcome = {0};
    bool ok = qa_combat_apply(g->services.combat, &request, &outcome, error);
    qa_damage_outcome_free(&outcome);
    return ok;
}
bool qa_q1_player_environment(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || q1_health(g, actor) < 0)
        return true;
    bool suit = player->power_expires[QA_Q1_SUIT] > g->time;
    bool lava_suit = player->power_expires[QA_Q1_LAVA_SUIT] > g->time;
    if (player->input.water_level != 3 || suit || lava_suit) {
        player->air_finished = g->time + 12;
        player->drown_damage = 2;
    } else if (player->air_finished < g->time && player->drown_at < g->time) {
        player->drown_damage += 2;
        if (player->drown_damage > 15)
            player->drown_damage = 10;
        if (!q1_environment_damage(g, actor, player->drown_damage, QA_HAZARD_DROWN, error))
            return false;
        player->drown_at = g->time + 1;
    }
    if (player->input.water_level && player->hazard_at < g->time) {
        if (player->input.water_type == -5 && !lava_suit) {
            player->hazard_at = g->time + (suit ? 1 : 0.2);
            return q1_environment_damage(g, actor, 10.0f * player->input.water_level,
                                         QA_HAZARD_LAVA, error);
        }
        if (player->input.water_type == -4 && !suit && !lava_suit) {
            player->hazard_at = g->time + 1;
            return q1_environment_damage(g, actor, 4.0f * player->input.water_level,
                                         QA_HAZARD_SLIME, error);
        }
    }
    return true;
}
bool qa_q1_player_postthink(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->arsenal)
        return true;
    if (player->input.impulse && g->time >= player->attack_finished) {
        if (!q1_weapon_impulse(g, player, player->input.impulse, error))
            return false;
        if (!q1_alive(g, actor))
            return true;
        player->input.impulse = 0;
    }
    return !player->input.attack || q1_fire(g, player, error);
}

bool q1_aim(qa_q1_game *g, qa_actor_id actor, qa_vec3 forward, qa_vec3 *out, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 20));
    qa_trace_result trace;
    if (!q1_trace(g, start, qa_vec_add(start, qa_vec_scale(forward, 2048)), actor, true, &trace,
                  error))
        return false;
    qa_combat_state attacker;
    bool has_team =
        qa_combat_read(g->services.combat, actor, &attacker, NULL) && attacker.team != 0;
    float best = g->options.aim_threshold;
    qa_vec3 selected = {0};
    bool found = false;
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (qa_actors_next(qa_session_actors(g->services.session), &cursor, &record)) {
        qa_actor_id target = record->id;
        if (qa_actor_id_equal(target, actor))
            continue;
        qa_q1_target observation;
        qa_combat_state combat;
        if (!q1_target(g, target, &observation) ||
            (!observation.aimed_damage && !observation.player) ||
            !qa_combat_read(g->services.combat, target, &combat, NULL) || !combat.can_take_damage ||
            (g->options.teamplay && has_team && attacker.team == combat.team))
            continue;
        if (trace.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(trace.actor, target)) {
            *out = forward;
            return true;
        }
        qa_body_state other;
        if (!qa_world_body_read(g->services.world, target, &other, error))
            return false;
        qa_vec3 end = qa_vec_add(
            other.origin, qa_vec_scale(qa_vec_add(other.bounds.mins, other.bounds.maxs), 0.5f));
        float alignment = qa_vec_dot(qa_vec_normalize(qa_vec_sub(end, start)), forward);
        if (alignment < best)
            continue;
        qa_trace_result sight;
        if (!q1_trace(g, start, end, actor, true, &sight, error))
            return false;
        if (sight.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(sight.actor, target)) {
            best = alignment;
            selected = other.origin;
            found = true;
        }
    }
    if (!found) {
        *out = forward;
        return true;
    }
    qa_vec3 delta = qa_vec_sub(selected, body.origin),
            result = qa_vec_scale(forward, qa_vec_dot(delta, forward));
    result.z = delta.z;
    *out = qa_vec_normalize(result);
    return true;
}
bool q1_bullets(qa_q1_game *g, qa_actor_id actor, qa_vec3 direction, qa_vec3 angles, unsigned count,
                float spread_x, float spread_y, qa_q1_weapon weapon, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
    qa_vec3 source = qa_vec_add(body.origin, qa_vec_scale(g->forward, 10));
    source.z =
        body.origin.z + body.bounds.mins.z + (body.bounds.maxs.z - body.bounds.mins.z) * 0.7f;
    qa_actor_id pending = {0};
    float amount = 0;
    for (unsigned i = 0; i < count; ++i) {
        float x = (q1_random(g) * 2 - 1) * spread_x, y = (q1_random(g) * 2 - 1) * spread_y;
        qa_vec3 ray =
            qa_vec_add(direction, qa_vec_add(qa_vec_scale(g->right, x), qa_vec_scale(g->up, y)));
        qa_trace_result trace;
        if (!q1_trace(g, source, qa_vec_add(source, qa_vec_scale(ray, 2048)), actor, true, &trace,
                      error))
            return false;
        if (trace.fraction == 1)
            continue;
        qa_vec3 point = qa_vec_sub(trace.end, qa_vec_scale(ray, 4));
        if (trace.hit == QA_TRACE_HIT_ACTOR && q1_damageable(g, trace.actor)) {
            if (!q1_effect(g, QA_BUILTIN_IMPACT, trace.actor, point, 4, 1, error))
                return false;
            if (!qa_actor_id_equal(pending, trace.actor)) {
                if (pending.registry && q1_alive(g, pending) &&
                    !q1_damage(g, pending, actor, actor, amount, weapon, error))
                    return false;
                pending = trace.actor;
                amount = 0;
            }
            amount += 4;
        } else if (!q1_effect(g, QA_BUILTIN_IMPACT, trace.actor, point, 1, 2, error))
            return false;
    }
    return !pending.registry || !q1_alive(g, pending) ||
           q1_damage(g, pending, actor, actor, amount, weapon, error);
}
static bool lightning(qa_q1_game *g, q1_player *player, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    float cells = (float)q1_ammo_count(g, player->id, QA_Q1_CELLS);
    if (player->input.water_level > 1)
        return q1_consume(g, player->id, QA_Q1_CELLS, cells, error) &&
               q1_radius_typed(g, player->id, player->id, 35 * cells, (qa_actor_id){0},
                               QA_Q1_LIGHTNING, "discharge", error);
    if (!q1_consume(g, player->id, QA_Q1_CELLS, 1, error))
        return false;
    qa_builtin_angle_vectors(player->input.view_angles, &g->forward, &g->right, &g->up);
    qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 16));
    qa_trace_result wall;
    if (!q1_trace(g, start, qa_vec_add(start, qa_vec_scale(g->forward, 600)), player->id, false,
                  &wall, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_BEAM,
                              .family = QA_GAME_Q1,
                              .actor = player->id,
                              .provider = g->options.provider,
                              .origin = start,
                              .end = wall.end,
                              .code = 2,
                              .time_ns = g->time_ns};
    if (!qa_builtin_emit(&g->services, &event, error))
        return false;
    qa_vec3 end = qa_vec_add(wall.end, qa_vec_scale(g->forward, 4));
    return q1_lightning_rays(g, player->id, player->id, body.origin, end, 30, 120, 1,
                             QA_Q1_LIGHTNING, NULL, error);
}

bool q1_weapon_parameters(qa_q1_game *g, qa_actor_id actor, qa_q1_weapon weapon,
                          qa_q1_weapon_parameters *parameters, qa_error *error) {
    if (g->host.weapon_parameters &&
        !g->host.weapon_parameters(g->host.context, actor, weapon, parameters, error))
        return false;
    if (!isfinite(parameters->interval) || parameters->interval <= 0 ||
        !isfinite(parameters->nail_speed) || parameters->nail_speed <= 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "invalid selected Q1 weapon timing or projectile speed");
        return false;
    }
    return true;
}

bool q1_fire(qa_q1_game *g, q1_player *player, qa_error *error) {
    if (player->input.holstered || q1_health(g, player->id) <= 0 ||
        g->time < (player->continuous ? player->next_weapon_frame : player->attack_finished))
        return true;
    qa_q1_weapon weapon = player->weapon;
    int ammo = q1_weapon_ammo(weapon);
    if (ammo >= 0 && q1_ammo_count(g, player->id, (qa_q1_ammo)ammo) < 1)
        return qa_q1_player_select(g, player->id, q1_best_weapon(g, player), error);
    if (weapon > QA_Q1_LIGHTNING)
        return q1_expansion_fire(g, player, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    bool repeating = player->continuous;
    qa_builtin_angle_vectors(player->input.view_angles, &g->forward, &g->right, &g->up);
    qa_vec3 forward = g->forward, right = g->right, up = g->up;
    static const float intervals[] = {0.5f, 0.5f, 0.7f, 0.2f, 0.2f, 0.6f, 0.8f, 0.1f};
    qa_q1_weapon_parameters parameters = {
        .interval = weapon == QA_Q1_LIGHTNING && repeating ? 0.2f : intervals[weapon],
        .nail_speed = 1000};
    if (weapon == QA_Q1_SHOTGUN && (player->mg3_progress.bloody & 1))
        parameters.interval = 0.28f;
    if (!q1_weapon_parameters(g, player->id, weapon, &parameters, error))
        return false;
    float punch = -2;
    int32_t attack = (int32_t)weapon;
    player->continuous =
        weapon == QA_Q1_NAILGUN || weapon == QA_Q1_SUPER_NAILGUN || weapon == QA_Q1_LIGHTNING;
    player->next_weapon_frame = g->time + 0.1;
    if (!player->continuous) {
        player->animation_at = g->time;
        player->animation_base = 1;
    }
    player->hostile_until = g->time + 1;
    q1_actor *projectile;
    qa_vec3 direction;
    switch (weapon) {
    case QA_Q1_AXE: {
        punch = 0;
        if (!q1_sound(g, player->id, "weapons/ax1.wav", 1, 1, error))
            return false;
        float choice = q1_random(g);
        attack = choice < 0.25f ? 0 : choice < 0.5f ? 1 : choice < 0.75f ? 2 : 3;
        player->animation_base = (choice >= 0.25f && choice < 0.5f) || choice >= 0.75f ? 5 : 1;
        if (!q1_create(g, "axe_strike", Q1_TIMER, player->id, &projectile, error) ||
            !q1_schedule(g, projectile, 0.2, Q1_THINK_AXE, error))
            return false;
        break;
    }
    case QA_Q1_SHOTGUN:
    case QA_Q1_SUPER_SHOTGUN: {
        bool super =
            weapon == QA_Q1_SUPER_SHOTGUN && q1_ammo_count(g, player->id, QA_Q1_SHELLS) > 1;
        bool bloody = super && (player->mg3_progress.bloody & 2);
        punch = super ? -4 : -2;
        if (!q1_consume(g, player->id, QA_Q1_SHELLS, super ? 2 : 1, error) ||
            !q1_sound(g, player->id, super ? "weapons/shotgn2.wav" : "weapons/guncock.wav", 1, 1,
                      error) ||
            !q1_aim(g, player->id, forward, &direction, error) ||
            !q1_bullets(g, player->id, direction, player->input.view_angles,
                        bloody  ? 28u
                        : super ? 14u
                                : 6u,
                        bloody  ? 0.3f
                        : super ? 0.14f
                                : 0.04f,
                        super ? 0.08f : 0.04f, weapon, error))
            return false;
        break;
    }
    case QA_Q1_NAILGUN:
    case QA_Q1_SUPER_NAILGUN: {
        bool super =
            weapon == QA_Q1_SUPER_NAILGUN && q1_ammo_count(g, player->id, QA_Q1_NAILS) >= 2;
        qa_vec3 origin = qa_vec_add(qa_vec_add(body.origin, qa_v3(0, 0, 16)),
                                    qa_vec_scale(right, super ? 0 : (float)player->nail_side * 4));
        if (!q1_consume(g, player->id, QA_Q1_NAILS, super ? 2 : 1, error) ||
            !q1_sound(g, player->id, super ? "weapons/spike2.wav" : "weapons/rocket1i.wav", 1, 1,
                      error) ||
            !q1_aim(g, player->id, forward, &direction, error) ||
            !q1_projectile_spawn(g, player->id, weapon, super ? Q1_SUPERSPIKE : Q1_SPIKE, origin,
                                 qa_vec_scale(direction, parameters.nail_speed), &projectile,
                                 error))
            return false;
        player->nail_side = -player->nail_side;
        break;
    }
    case QA_Q1_GRENADE: {
        qa_vec3 velocity;
        if (player->input.view_angles.x == 0) {
            if (!q1_aim(g, player->id, forward, &direction, error))
                return false;
            velocity = qa_vec_scale(direction, 600);
            velocity.z = 200;
        } else {
            float x = (q1_random(g) * 2 - 1) * 10, y = (q1_random(g) * 2 - 1) * 10;
            velocity = qa_vec_add(qa_vec_add(qa_vec_scale(forward, 600), qa_vec_scale(up, 200)),
                                  qa_vec_add(qa_vec_scale(right, x), qa_vec_scale(up, y)));
        }
        if (!q1_consume(g, player->id, QA_Q1_ROCKETS, 1, error) ||
            !q1_sound(g, player->id, "weapons/grenade.wav", 1, 1, error) ||
            !q1_projectile_spawn(g, player->id, weapon, Q1_GRENADE, body.origin, velocity,
                                 &projectile, error))
            return false;
        break;
    }
    case QA_Q1_ROCKET:
        if (!q1_consume(g, player->id, QA_Q1_ROCKETS, 1, error) ||
            !q1_sound(g, player->id, "weapons/sgun1.wav", 1, 1, error) ||
            !q1_aim(g, player->id, forward, &direction, error) ||
            !q1_projectile_spawn(
                g, player->id, weapon, Q1_ROCKET,
                qa_vec_add(qa_vec_add(body.origin, qa_vec_scale(forward, 8)), qa_v3(0, 0, 16)),
                qa_vec_scale(direction, 1000), &projectile, error))
            return false;
        break;
    case QA_Q1_LIGHTNING:
        if (player->lightning_sound_at < g->time) {
            if (!q1_sound(g, player->id, "weapons/lhit.wav", 1, 1, error))
                return false;
            player->lightning_sound_at = g->time + 0.6;
        }
        if (!lightning(g, player, error) ||
            (!repeating && !q1_sound(g, player->id, "weapons/lstart.wav", 0, 1, error)))
            return false;
        break;
    default:
        return false;
    }
    if (!q1_horde_axe_delay(g, player, &parameters.interval, error))
        return false;
    player->attack_finished = g->time + parameters.interval;
    player->weapon_frame = player->continuous
                               ? player->weapon_frame % (weapon == QA_Q1_LIGHTNING ? 4 : 8) + 1
                               : player->animation_base;
    if (punch != 0)
        player->punch.x = punch;
    return q1_weapon_event(g, player, punch, attack, error) &&
           q1_effect(g, QA_BUILTIN_MUZZLE, player->id, body.origin, 0, 0, error);
}
bool q1_axe_strike(qa_q1_game *g, q1_actor *strike, qa_error *error) {
    q1_player *player = q1_player_get(g, strike->owner);
    if (!player)
        return q1_remove(g, strike, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    qa_builtin_angle_vectors(player->input.view_angles, &g->forward, &g->right, &g->up);
    qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 16));
    qa_trace_result trace;
    if (!q1_trace(g, start, qa_vec_add(start, qa_vec_scale(g->forward, 64)), player->id, true,
                  &trace, error))
        return false;
    if (trace.fraction < 1) {
        if (trace.hit == QA_TRACE_HIT_ACTOR && q1_damageable(g, trace.actor)) {
            if (!q1_effect(g, QA_BUILTIN_IMPACT, trace.actor, trace.end, 20, 1, error) ||
                !q1_damage(g, trace.actor, player->id, player->id, 20, QA_Q1_AXE, error))
                return false;
        } else if (!q1_sound(g, player->id, "player/axhit2.wav", 1, 1, error) ||
                   !q1_effect(g, QA_BUILTIN_IMPACT, (qa_actor_id){0}, trace.end, 3, 2, error))
            return false;
    }
    return q1_remove(g, strike, error);
}
