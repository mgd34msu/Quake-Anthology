/* Source behavior from id Software g_team.c and the TypeScript TeamRuntime.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later. */
#include "map/internal.h"
#include "source_objectives.h"

static q3_actor *obelisk(qa_q3_game *game, qa_actor_id actor)
{
    q3_actor *value = q3_actor_get(game, actor);
    return value && value->kind == Q3_ACTOR_OBELISK ? value : NULL;
}

static int32_t multiply(int32_t left, int32_t right)
{
    uint32_t bits = (uint32_t)left * (uint32_t)right;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static int32_t health_word(double value)
{
    return value >= -2147483648.0 && value < 2147483648.0 ? (int32_t)value : INT32_MIN;
}

static int32_t ratio(int32_t numerator, int32_t denominator)
{
    uint32_t bits = denominator ? (uint32_t)((int64_t)numerator / denominator) : 0;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static bool settings(qa_q3_game *game, qa_q3_obelisk_settings *out, qa_error *error)
{
    if (!game->options.hooks.source_obelisk_settings)
        return q3_fail(error, "Obelisk has no actual source settings owner");
    return game->options.hooks.source_obelisk_settings(game->options.hooks.context, out, error);
}

static bool admission(void *opaque, const qa_damage_request *request,
    bool *handled, qa_error *error)
{
    qa_q3_game *game = opaque;
    *handled = false;
    q3_actor *entry = obelisk(game, request->target);
    if (!entry || entry->state.obelisk.think == QA_Q3_OBELISK_NONE) return true;
    if (game->observation_depth == SIZE_MAX) return q3_fail(error, "Obelisk callback depth exhausted");
    ++game->observation_depth;
    qa_combat_state combat;
    bool okay = qa_combat_read(game->options.services.combat, request->target, &combat, error);
    entry = obelisk(game, request->target);
    if (okay && entry && !combat.can_take_damage) *handled = true;
    uint32_t slot;
    bool native = okay && entry && !*handled &&
        qa_q3_native_client_slot(game, request->attack.attacker, &slot, NULL);
    if (native) {
        int32_t team = entry->spawnflags;
        int32_t attacker = game->clients[slot].session.team;
        if (team == attacker) *handled = true;
        else {
            qa_q3_source_team_state state;
            okay = qa_q3_source_team_state_read(game, &state, error);
            int32_t threshold = q3_sub_time(game->now_ms, 20000);
            bool red = team == 1, blue = team == 2;
            if (okay && ((red && state.red_obelisk_attacked_ms < threshold) ||
                         (blue && state.blue_obelisk_attacked_ms < threshold))) {
                q3_wire_entity_source *source = q3_wire_entity(game, request->target);
                if (!source) okay = q3_fail(error, "Obelisk attack lost its actual source wire row");
                else okay = qa_q3_source_team_sound(game, source->position.base,
                                                    red ? 6 : 7, error);
                entry = obelisk(game, request->target);
                if (okay && entry) {
                    okay = qa_q3_source_team_state_read(game, &state, error);
                    if (red) state.red_obelisk_attacked_ms = game->now_ms;
                    else state.blue_obelisk_attacked_ms = game->now_ms;
                    if (okay) okay = qa_q3_source_team_state_write(game, &state, error);
                }
            }
        }
    }
    --game->observation_depth;
    return okay;
}

bool q3_obelisk_reconnect(qa_q3_game *game, qa_actor_id actor, qa_error *error)
{
    q3_actor *entry = obelisk(game, actor);
    if (!entry) return q3_fail(error, "Obelisk reconnect lost its actual source actor");
    if (entry->state.obelisk.think == QA_Q3_OBELISK_NONE) return true;
    qa_combat_admission binding = {.context = game, .admit = admission};
    return qa_combat_set_admission(game->options.services.combat, actor, &binding, error);
}

bool qa_q3_source_obelisk_read(const qa_q3_game *game, qa_actor_id actor,
    qa_q3_obelisk_state *out, qa_error *error)
{
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!out || !entry || entry->kind != Q3_ACTOR_OBELISK) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "actor has no actual native Obelisk owner");
        return false;
    }
    *out = (qa_q3_obelisk_state){.model = entry->state.obelisk.model,
        .team = entry->spawnflags, .next_think_ms = entry->state.obelisk.next_think_ms,
        .think = entry->state.obelisk.think};
    return true;
}

static bool spawn_trigger(qa_q3_game *game, qa_actor_id model, qa_vec3 origin,
    int32_t team, uint32_t flags, qa_actor_id *out, qa_error *error)
{
    qa_q3_obelisk_settings current;
    if (!settings(game, &current, error)) return false;
    bool overload = game->options.rules.game_type == 6;
    qa_bounds bounds = {qa_v3(-15, -15, 0), qa_v3(15, 15, 87)};
    qa_actor_collision collision = {.family = QA_COLLISION_Q3, .shape = QA_SHAPE_BOX,
        .contents = overload ? 1 : Q3_CONTENTS_TRIGGER,
        .role = overload ? QA_COLLISION_SOLID : QA_COLLISION_TRIGGER};
    qa_combat_state combat = {.health = (float)current.health, .mass = 200,
        .can_take_damage = true, .no_knockback = true};
    qa_builtin_spawn spawn = {.owner = game->options.owner, .definition = game->source_noclass,
        .body = {.origin = origin, .bounds = bounds}, .collision = &collision,
        .combat = overload ? &combat : NULL};
    qa_actor_id actor;
    if (!q3_spawn_actor(game, &spawn, &actor, error)) return false;
    game->actors[actor.slot] = (q3_actor){.actor = actor, .kind = Q3_ACTOR_OBELISK,
        .alpha = 1,
        .state.obelisk = {
            .think = overload ? QA_Q3_OBELISK_REGEN : QA_Q3_OBELISK_NONE,
            .next_think_ms = overload ? q3_add_time(game->now_ms,
                multiply(current.regen_period_seconds, 1000)) : 0}};
    if (overload) q3_postgame_native_think_assigned(game, actor);
    qa_body_state body = spawn.body;
    q3_wire_entity_source *wire = q3_wire_entity(game, actor);
    if (!wire) return q3_rollback_spawn(game, actor, error);
    wire->type = 0;
    wire->authored_origin = origin;
    wire->position = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = origin};
    if (!(flags & 1u)) {
        qa_vec3 start = origin;
        start.z = (start.z + 1);
        wire->authored_origin = start;
        qa_vec3 end = start;
        end.z = (end.z + -4096);
        qa_trace_query query = {.start = start, .end = end, .pass_actor = actor,
            .shape = {.kind = QA_SHAPE_BOX, .bounds = bounds},
            .policy = {.family = QA_COLLISION_Q3, .contents_mask = 1, .curves = true}};
        qa_trace_result trace;
        if (!qa_world_trace(game->options.services.world, &query, &trace, error))
            return q3_rollback_spawn(game, actor, error);
        if (!obelisk(game, actor)) return q3_fail(error, "Obelisk retired during its floor trace");
        if (trace.start_solid || trace.all_solid) {
            start.z = (start.z + -1);
            wire = q3_wire_entity(game, actor);
            if (!wire) return q3_fail(error, "Obelisk startsolid lost its true source row");
            wire->authored_origin = start;
            wire->ground_entity = 1023;
            char warning[160];
            snprintf(warning, sizeof(warning), "SpawnObelisk: noclass startsolid at (%d %d %d)\n",
                q3_source_float_to_int(start.x), q3_source_float_to_int(start.y),
                q3_source_float_to_int(start.z));
            q3_map_warn(game, actor, warning);
            body.origin = start;
        } else {
            body.origin = trace.end;
            const qa_actor_record *ground = qa_actors_get(qa_session_actors(game->options.services.session), trace.actor);
            body.ground = trace.hit == QA_TRACE_HIT_ACTOR ?
                ground && ground->owner == game->options.owner && ground->has_source ?
                    qa_actor_reference_source(ground->owner, ground->source_slot) : qa_actor_reference_lifetime(trace.actor) :
                trace.hit == QA_TRACE_HIT_WORLD ? qa_actor_reference_source(game->options.owner, QA_Q3_SOURCE_WORLD) :
                (qa_actor_reference){0};
            wire = q3_wire_entity(game, actor);
            if (!wire) return q3_fail(error, "Obelisk floor trace lost its real wire row");
            wire->ground_entity = trace.hit == QA_TRACE_HIT_WORLD ? 1022 :
                trace.hit == QA_TRACE_HIT_ACTOR ? q3_entity_number(game, trace.actor) : 1023;
        }
    }
    wire = q3_wire_entity(game, actor);
    if (!wire || !obelisk(game, actor)) return q3_fail(error, "Obelisk source actor retired during placement");
    wire->position = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = body.origin};
    if (!qa_world_body_write(game->options.services.world, actor, &body, error))
        return q3_rollback_spawn(game, actor, error);
    q3_actor *entry = obelisk(game, actor);
    if (!entry) return q3_fail(error, "Obelisk retired during its actual origin publication");
    entry->spawnflags = team;
    if (!q3_obelisk_reconnect(game, actor, error) ||
        !qa_q3_wire_link(game, actor, NULL, error) || !q3_wire_entity_ready(game, actor, error))
        return q3_rollback_spawn(game, actor, error);
    entry = obelisk(game, actor);
    if (!entry) return q3_fail(error, "Obelisk retired during its true link");
    entry->state.obelisk.model = team ? model : (qa_actor_id){0};
    *out = actor;
    return true;
}

static bool spawn(qa_q3_game *game, qa_actor_id model, int32_t team,
    uint32_t flags, qa_actor_id *out, qa_error *error)
{
    *out = (qa_actor_id){0};
    qa_q3_obelisk_settings current;
    if (!settings(game, &current, error)) return false;
    int32_t type = game->options.rules.game_type;
    if (team == 0 ? type != 5 && type != 7 : type <= 3)
        return qa_session_release(game->options.services.session, model, error);
    q3_wire_entity_source *wire = q3_wire_entity(game, model);
    if (!wire) return q3_fail(error, "authored Obelisk model has no actual source wire owner");
    qa_vec3 origin = wire->authored_origin;
    wire->type = 12;
    qa_actor_id trigger = {0};
    if (type == 6 || type == 7) {
        if (!spawn_trigger(game, model, origin, team, flags, &trigger, error)) return false;
        wire = q3_wire_entity(game, model);
        if (!wire) return q3_rollback_spawn(game, trigger, error);
        if (type == 6) { wire->model2 = 255; wire->frame = 0; }
        if (team == 0) {
            qa_q3_source_team_state state;
            if (!qa_q3_source_team_state_read(game, &state, error)) return q3_rollback_spawn(game, trigger, error);
            state.neutral_obelisk = trigger;
            if (!qa_q3_source_team_state_write(game, &state, error)) return q3_rollback_spawn(game, trigger, error);
        }
    }
    wire->model = team;
    if (!qa_q3_wire_link(game, model, NULL, error) || !q3_wire_entity_ready(game, model, error))
        return trigger.registry ? q3_rollback_spawn(game, trigger, error) : false;
    if (trigger.registry && game->options.hooks.objective_obelisk_admitted &&
        !game->options.hooks.objective_obelisk_admitted(game->options.hooks.context,
            trigger, model, team, error)) return q3_rollback_spawn(game, trigger, error);
    *out = trigger;
    return true;
}

bool qa_q3_source_obelisk_spawn(qa_q3_game *game, qa_actor_id model, int32_t team,
    uint32_t flags, qa_actor_id *out, qa_error *error)
{
    uint32_t slot;
    if (!game || !out || game->options.product != QA_Q3_TEAM_ARENA || team < 0 || team > 2 ||
        game->source_restored || game->observation_depth == SIZE_MAX ||
        !qa_q3_source_actor_slot(game, model, &slot, error) || slot < 64 || slot >= 1022)
        return q3_fail(error, "Obelisk spawn requires its actual authored missionpack source model");
    ++game->observation_depth;
    bool okay = spawn(game, model, team, flags, out, error);
    --game->observation_depth;
    return okay;
}

bool q3_obelisk_step(qa_q3_game *game, qa_actor_id actor, qa_error *error)
{
    q3_actor *entry = obelisk(game, actor);
    if (!entry) return true;
    bool replaced;
    if (!q3_postgame_think_override(game, actor, &replaced, error)) return false;
    if (replaced) return true;
    int32_t think_time = entry->state.obelisk.next_think_ms;
    if (think_time <= 0 || think_time > game->now_ms) return true;
    qa_q3_obelisk_think think = entry->state.obelisk.think;
    entry->state.obelisk.next_think_ms = 0;
    qa_q3_obelisk_settings current;
    if (!settings(game, &current, error)) return false;
    entry = obelisk(game, actor);
    if (!entry) return true;
    qa_actor_id model = entry->state.obelisk.model;
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, actor, &combat, error)) return false;
    if (!obelisk(game, actor)) return true;
    if (think == QA_Q3_OBELISK_RESPAWN) {
        combat.can_take_damage = true;
        if (!qa_combat_set_traits(game->options.services.combat, actor, &combat, error) ||
            !qa_combat_set_health(game->options.services.combat, actor, (float)current.health, error)) return false;
        entry = obelisk(game, actor);
        if (!entry) return true;
        entry->state.obelisk.think = QA_Q3_OBELISK_REGEN;
        q3_postgame_native_think_assigned(game, actor);
        entry->state.obelisk.next_think_ms = q3_add_time(game->now_ms,
            multiply(current.regen_period_seconds, 1000));
    } else {
        entry = obelisk(game, actor);
        if (!entry) return true;
        entry->state.obelisk.next_think_ms = q3_add_time(game->now_ms,
            multiply(current.regen_period_seconds, 1000));
        if (combat.health >= (float)current.health) return true;
        if (!q3_wire_add_event(game, actor, 63, 0, error)) return false;
        int32_t health = q3_add_time(health_word(combat.health), current.regen_amount);
        if (health > current.health) health = current.health;
        if (!qa_combat_set_health(game->options.services.combat, actor, (float)health, error)) return false;
        q3_wire_entity_source *wire = q3_wire_entity(game, model);
        if (!wire) return q3_fail(error, "Obelisk regen lost its actual model actor");
        wire->model2 = ratio(multiply(health, 255), current.health);
    }
    q3_wire_entity_source *wire = q3_wire_entity(game, model);
    if (!wire) return q3_fail(error, "Obelisk think lost its actual model actor");
    wire->frame = 0;
    return true;
}

bool q3_obelisk_touch(qa_q3_game *game, qa_actor_id actor, qa_actor_id other, qa_error *error)
{
    q3_actor *entry = obelisk(game, actor);
    uint32_t slot;
    if (!entry || entry->state.obelisk.think != QA_Q3_OBELISK_NONE ||
        !qa_q3_native_client_slot(game, other, &slot, NULL)) return true;
    int32_t team = game->clients[slot].session.team;
    int32_t opposing = team == 1 ? 2 : team == 2 ? 1 : team;
    if (opposing != entry->spawnflags) return true;
    if (!game->options.hooks.objective_obelisk_touch)
        return q3_fail(error, "Harvester trigger lost its actual source TEAM owner");
    return game->options.hooks.objective_obelisk_touch(game->options.hooks.context, actor, other, error);
}

bool qa_q3_source_obelisk_reaction(qa_q3_game *game, const qa_damage_outcome *outcome,
    bool *handled, qa_error *error)
{
    if (!game || !outcome || !handled || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Obelisk reaction requires its actual source damage boundary");
    *handled = false;
    q3_actor *entry = obelisk(game, outcome->request.target);
    if (!entry || entry->state.obelisk.think == QA_Q3_OBELISK_NONE) return true;
    *handled = true;
    if (outcome->stale || outcome->result.reaction == QA_REACTION_NONE) return true;
    ++game->observation_depth;
    qa_actor_id actor = outcome->request.target, attacker = outcome->request.attack.attacker;
    qa_actor_id model = entry->state.obelisk.model;
    bool okay = true;
    if (outcome->result.reaction == QA_REACTION_DEATH) {
        if (!game->options.hooks.objective_obelisk_die) okay = false;
        else okay = game->options.hooks.objective_obelisk_die(game->options.hooks.context,
            actor, attacker, QA_Q3_OBELISK_DIE_TEAM_SCORE, error);
        entry = obelisk(game, actor);
        if (okay && entry) {
            qa_combat_state combat;
            okay = qa_combat_read(game->options.services.combat, actor, &combat, error);
            combat.can_take_damage = false;
            if (okay) okay = qa_combat_set_traits(game->options.services.combat, actor, &combat, error);
            entry = obelisk(game, actor);
            if (okay && entry) {
                entry->state.obelisk.think = QA_Q3_OBELISK_RESPAWN;
                q3_postgame_native_think_assigned(game, actor);
                qa_q3_obelisk_settings current;
                okay = settings(game, &current, error);
                entry = obelisk(game, actor);
                if (okay && entry) entry->state.obelisk.next_think_ms =
                    q3_add_time(game->now_ms, multiply(current.respawn_delay_seconds, 1000));
            }
            q3_wire_entity_source *wire = q3_wire_entity(game, model);
            if (okay && entry) {
                if (!wire) okay = q3_fail(error, "Obelisk death lost its actual model actor");
                else { wire->model2 = 255; wire->frame = 2; okay = q3_wire_add_event(game, model, 69, 0, error); }
            }
            if (okay && obelisk(game, actor)) okay =
                game->options.hooks.objective_obelisk_die(game->options.hooks.context,
                    actor, attacker, QA_Q3_OBELISK_DIE_PLAYER_SCORE, error);
            if (okay) {
                qa_q3_source_team_state state;
                okay = qa_q3_source_team_state_read(game, &state, error);
                state.red_obelisk_attacked_ms = state.blue_obelisk_attacked_ms = 0;
                if (okay) okay = qa_q3_source_team_state_write(game, &state, error);
            }
        }
    } else {
        qa_combat_state combat;
        qa_q3_obelisk_settings current;
        okay = qa_combat_read(game->options.services.combat, actor, &combat, error) && settings(game, &current, error);
        q3_wire_entity_source *wire = q3_wire_entity(game, model);
        if (okay && obelisk(game, actor)) {
            if (!wire) okay = q3_fail(error, "Obelisk pain lost its actual model actor");
            else {
                wire->model2 = ratio(multiply(health_word(combat.health), 255), current.health);
                if (!wire->frame) okay = q3_wire_add_event(game, actor, 70, 0, error);
                wire = q3_wire_entity(game, model);
                if (okay && wire) wire->frame = 1;
                else if (okay) okay = q3_fail(error, "Obelisk pain event retired its actual model");
            }
            if (okay && obelisk(game, actor)) {
                int32_t amount = health_word(outcome->result.applied_damage) / 10;
                if (amount < 1) amount = 1;
                okay = game->options.hooks.objective_obelisk_pain &&
                    game->options.hooks.objective_obelisk_pain(game->options.hooks.context, actor, attacker, amount, error);
            }
        }
    }
    --game->observation_depth;
    if (!okay && (!error || error->code == QA_OK)) return q3_fail(error, "Obelisk lost its source reaction callback");
    return okay;
}
