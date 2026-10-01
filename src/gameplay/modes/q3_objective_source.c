/* Source behavior from id Software g_team.c and the TypeScript TeamRuntime.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later. */
#include "internal.h"
#include "q3_objective_source.h"

#include <stdio.h>

enum { TEAM_FREE, TEAM_RED, TEAM_BLUE, TEAM_SPECTATOR };
enum { FLAG_HOME, FLAG_TAKEN, FLAG_TAKEN_RED, FLAG_TAKEN_BLUE, FLAG_DROPPED };

typedef struct team_call {
    qa_modes *modes;
    qa_mode_id mode;
    uint64_t serial;
    const mode_q3_objective_services *source;
} team_call;

static mode_instance *instance(const team_call *call, qa_error *error)
{
    mode_instance *value = mode_get(call->modes, call->mode);
    if (!value || value->serial != call->serial) {
        mode_fail(error, "native TEAM mode retired during its source callback");
        return NULL;
    }
    return value;
}

static bool after(const team_call *call, bool okay, qa_error *error)
{
    return okay && call->source->live(call->source->context, error) &&
        instance(call, error);
}

static bool begin(qa_modes *modes, qa_mode_id id,
    const mode_q3_objective_services *source, team_call *call, qa_error *error)
{
    mode_instance *value = mode_get(modes, id);
    if (!value || !source || !source->live || !source->team_read || !source->team_write ||
        !source->player_team_read || !source->player_team_write ||
        !source->team_score || !source->ranks || !source->tokens || !source->set_tokens ||
        !source->entity_count || !source->item_at || !source->client_at ||
        !source->powerup || !source->set_powerup || !source->respawn || !source->retire ||
        !source->configstring || !source->print || !source->warn || !source->sound || !source->gesture ||
        !source->award || !source->pers_award || !source->score_plum || !source->rank_capture || !source->rank_pickup ||
        source->max_clients > 64)
        return mode_fail(error, "TEAM operation requires its actual native source owners");
    *call = (team_call){modes, id, value->serial, source};
    return source->live(source->context, error);
}

static qa_team_id team_id(const mode_instance *value, int32_t team)
{
    return team == TEAM_RED ? value->value.rules.teams[0] :
        team == TEAM_BLUE ? value->value.rules.teams[1] : 0;
}

static const char *team_name(int32_t team)
{
    return team == TEAM_RED ? "RED" : team == TEAM_BLUE ? "BLUE" : "FREE";
}

static bool message(const team_call *call, qa_actor_id recipient, const char *text,
    qa_error *error)
{
    return after(call, call->source->print(call->source->context, recipient, text, error), error);
}

static bool sound(const team_call *call, const mode_q3_source_item *item,
    int32_t parameter, const char *warning, qa_error *error)
{
    if (!item->actor.registry)
        return after(call, call->source->warn(call->source->context, warning, error), error);
    return after(call, call->source->sound(call->source->context,
        item->trajectory_base, parameter, error), error);
}

static char ctf_status(int32_t status)
{
    return status == -1 ? '\0' : status == FLAG_HOME ? '0' :
        status == FLAG_TAKEN ? '1' : status == FLAG_DROPPED ? '2' : '*';
}

static bool status(const team_call *call, int32_t team, int32_t next, qa_error *error)
{
    qa_q3_source_team_state state;
    if (!after(call, call->source->team_read(call->source->context, &state, error), error)) return false;
    int32_t *current = team == TEAM_RED ? &state.red_status :
        team == TEAM_BLUE ? &state.blue_status : team == TEAM_FREE ? &state.neutral_status : NULL;
    if (!current || *current == next) return true;
    *current = next;
    if (!after(call, call->source->team_write(call->source->context, &state, error), error)) return false;
    char text[16];
    if (call->source->game_type == 4) {
        text[0] = ctf_status(state.red_status);
        text[1] = ctf_status(state.blue_status);
        text[2] = 0;
    } else snprintf(text, sizeof(text), "%d", state.neutral_status);
    return after(call, call->source->configstring(call->source->context, 23, text, error), error);
}

static bool item_read(const team_call *call, uint32_t slot,
    mode_q3_source_item *item, bool *present, qa_error *error)
{
    return after(call, call->source->item_at(call->source->context,
        slot, item, present, error), error);
}

static bool actual_item(const team_call *call, qa_actor_id actor,
    mode_q3_source_item *out, qa_error *error)
{
    for (uint32_t slot = 64;; ++slot) {
        uint32_t count;
        if (!after(call, call->source->entity_count(call->source->context, &count, error), error))
            return false;
        if (slot >= count) break;
        mode_q3_source_item item;
        bool present;
        if (!item_read(call, slot, &item, &present, error)) return false;
        if (present && qa_actor_id_equal(item.actor, actor)) { *out = item; return true; }
    }
    return mode_fail(error, "TEAM item has no actual physical source row");
}

static bool source_client(const team_call *call, qa_actor_id actor,
    mode_q3_source_client *out, qa_error *error)
{
    for (uint32_t slot = 0; slot < call->source->max_clients; ++slot) {
        mode_q3_source_client row;
        if (!after(call, call->source->client_at(call->source->context, slot, &row, error), error))
            return false;
        if (row.in_use && qa_actor_id_equal(row.actor, actor)) { *out = row; return true; }
    }
    return mode_fail(error, "TEAM player has no actual source client generation");
}

static mode_member *member(const team_call *call, qa_actor_id actor, qa_error *error)
{
    mode_member *result = mode_member_get(call->modes, instance(call, error), actor);
    if (!result) mode_fail(error, "TEAM player retired during its source callback");
    return result;
}

static bool points(const team_call *call, qa_actor_id actor, qa_vec3 origin,
    int32_t amount, qa_error *error)
{
    qa_q3_source_team_state state;
    if (!member(call, actor, error) ||
        !after(call, call->source->team_read(call->source->context, &state, error), error)) return false;
    if (state.warmup_time_ms) return true;
    return after(call, call->source->score_plum(call->source->context,
            actor, origin, amount, error), error) && member(call, actor, error) &&
        after(call, qa_modes_add_score(call->modes, call->mode, actor, amount, error), error) &&
        member(call, actor, error) &&
        after(call, call->source->ranks(call->source->context, error), error);
}

static bool powerup(const team_call *call, qa_actor_id actor, int32_t team,
    int32_t *out, qa_error *error)
{
    int32_t tag = team == TEAM_RED ? 7 : team == TEAM_BLUE ? 8 : 9;
    return after(call, call->source->powerup(call->source->context, actor, tag, out, error), error);
}

static bool set_powerup(const team_call *call, qa_actor_id actor, int32_t team,
    int32_t count, qa_error *error)
{
    int32_t tag = team == TEAM_RED ? 7 : team == TEAM_BLUE ? 8 : 9;
    return after(call, call->source->set_powerup(call->source->context,
        actor, tag, count, error), error);
}

static bool reset_flag(const team_call *call, int32_t team,
    mode_q3_source_item *base, qa_error *error)
{
    *base = (mode_q3_source_item){0};
    for (uint32_t slot = 64;; ++slot) {
        uint32_t count;
        if (!after(call, call->source->entity_count(call->source->context, &count, error), error))
            return false;
        if (slot >= count) break;
        mode_q3_source_item item;
        bool present;
        if (!item_read(call, slot, &item, &present, error)) return false;
        if (!present || item.flag_team != team) continue;
        if (item.dropped) {
            if (!after(call, call->source->retire(call->source->context, item.actor, error), error))
                return false;
            continue;
        }
        mode_object *object = mode_object_get(call->modes, item.actor);
        if (!object || !object->q3_source_owned)
            return mode_fail(error, "TEAM base lost its actual mode adoption");
        qa_actor_id carrier = object->value.carrier.registry ?
            object->value.carrier : object->value.previous_owner;
        mode_instance *value = instance(call, error);
        if (!value) return false;
        bool pending_clear = false;
        if (mode_live(call->modes, carrier)) {
            int32_t held;
            if (!powerup(call, carrier, team, &held, error)) return false;
            pending_clear = held != 0;
            object = mode_object_get(call->modes, item.actor);
            value = instance(call, error);
            if (!object || !value) return mode_fail(error, "TEAM base retired during its source carrier read");
            if (!pending_clear && !after(call,
                    mode_object_count(call->modes, value, object, carrier, 0, error), error)) return false;
        }
        object = mode_object_get(call->modes, item.actor);
        value = instance(call, error);
        if (!object || !value) return mode_fail(error, "TEAM base retired while clearing its carrier");
        mode_member *player = mode_member_get(call->modes, value, carrier);
        if (!pending_clear && player && qa_actor_id_equal(player->flag, item.actor))
            player->flag = (qa_actor_id){0};
        object->value.carrier = (qa_actor_id){0};
        object->value.previous_owner = pending_clear ? carrier : (qa_actor_id){0};
        object->value.phase = QA_OBJECTIVE_HOME;
        object->value.visible = true;
        object->value.deadline_ns = object->expire_ns = object->next_ns = 0;
        object->dropped_actor = (qa_actor_id){0};
        object->dropped = false;
        *base = item;
        if (!after(call, call->source->respawn(call->source->context, item.actor, error), error))
            return false;
    }
    return status(call, team, FLAG_HOME, error);
}

static bool return_sound(const team_call *call, const mode_q3_source_item *base,
    int32_t team, qa_error *error)
{
    return sound(call, base, team == TEAM_BLUE ? 2 : 3,
        "Warning:  NULL passed to Team_ReturnFlagSound\n", error);
}

static bool take_sound(const team_call *call, const mode_q3_source_item *item,
    int32_t team, qa_error *error)
{
    qa_q3_source_team_state state;
    if (!after(call, call->source->team_read(call->source->context, &state, error), error)) return false;
    int32_t threshold = mode_add_i32(call->source->time_ms, -10000);
    if (team == TEAM_RED) {
        if (state.blue_status != FLAG_HOME && state.blue_taken_ms > threshold)
            return true;
        state.blue_taken_ms = call->source->time_ms;
    } else if (team == TEAM_BLUE) {
        if (state.red_status != FLAG_HOME && state.red_taken_ms > threshold)
            return true;
        state.red_taken_ms = call->source->time_ms;
    }
    if (!after(call, call->source->team_write(call->source->context, &state, error), error)) return false;
    return sound(call, item, team == TEAM_BLUE ? 4 : 5,
        "Warning:  NULL passed to Team_TakeFlagSound\n", error);
}

static bool add_team_score(const team_call *call, const mode_q3_source_item *item,
    int32_t team, int32_t count, qa_error *error)
{
    qa_q3_source_team_state state;
    if (!after(call, call->source->team_read(call->source->context, &state, error), error)) return false;
    int32_t red = state.team_scores[TEAM_RED], blue = state.team_scores[TEAM_BLUE];
    int32_t next = mode_add_i32(team == TEAM_RED ? red : blue, count);
    int32_t parameter = team == TEAM_RED ? next == blue ? 12 : red <= blue && next > blue ? 10 : 8
        : next == red ? 12 : blue <= red && next > red ? 11 : 9;
    if (!after(call, call->source->sound(call->source->context,
            item->trajectory_base, parameter, error), error)) return false;
    return after(call, call->source->team_score(call->source->context, team, count, error), error);
}

static bool capture(const team_call *call, const mode_q3_source_item *item,
    const mode_q3_source_client *client, int32_t team, bool one_flag, qa_error *error)
{
    char text[128];
    if (one_flag) snprintf(text, sizeof(text), "%s^7 captured the flag!\n", client->name);
    else snprintf(text, sizeof(text), "%s^7 captured the %s flag!\n", client->name,
        team_name(team == TEAM_RED ? TEAM_BLUE : TEAM_RED));
    if (!message(call, (qa_actor_id){0}, text, error) ||
        !set_powerup(call, client->actor, one_flag ? TEAM_FREE :
            client->team == TEAM_RED ? TEAM_BLUE : TEAM_RED, 0, error)) return false;
    mode_instance *value = instance(call, error);
    if (!value) return false;
    qa_q3_source_team_state state;
    if (!after(call, call->source->team_read(call->source->context, &state, error), error)) return false;
    state.last_flag_capture_ms = (float)call->source->time_ms;
    state.last_capture_team = team;
    if (!after(call, call->source->team_write(call->source->context, &state, error), error)) return false;
    if (!add_team_score(call, item, client->team, 1, error) ||
        !after(call, call->source->gesture(call->source->context, client->team, error), error))
        return false;
    mode_member *carrier = member(call, client->actor, error);
    value = instance(call, error);
    if (!carrier || !value) return false;
    qa_q3_source_player_team_state carrier_team;
    if (!after(call, call->source->player_team_read(call->source->context,
            client->actor, &carrier_team, error), error)) return false;
    carrier_team.captures = mode_add_i32(carrier_team.captures, 1);
    if (!after(call, call->source->player_team_write(call->source->context,
            client->actor, &carrier_team, error), error)) return false;
    mode_stat_add(value, &carrier->stats.captures, 1);
    int index = mode_team_index(value, team_id(value, client->team));
    if (index >= 0) value->value.team_captures[index] = mode_add_i32(value->value.team_captures[index], 1);
    if (!after(call, call->source->rank_capture(call->source->context, client->actor, error), error) ||
        !after(call, call->source->award(call->source->context, client->actor, 0x800, error), error))
        return false;
    if (!after(call, call->source->pers_award(call->source->context,
            client->actor, QA_Q3_AWARD_CAPTURE, 1, error), error)) return false;
    if (!points(call, client->actor, item->origin, call->source->missionpack ? 100 : 5, error) ||
        !sound(call, item, team == TEAM_BLUE ? 1 : 0,
            "Warning:  NULL passed to Team_CaptureFlagSound\n", error)) return false;
    for (uint32_t slot = 0; slot < call->source->max_clients; ++slot) {
        mode_q3_source_client teammate;
        if (!after(call, call->source->client_at(call->source->context,
                slot, &teammate, error), error)) return false;
        if (!teammate.in_use) continue;
        mode_member *player = member(call, teammate.actor, error);
        if (!player) return false;
        if (teammate.team != client->team) {
            qa_q3_source_player_team_state player_team;
            if (!after(call, call->source->player_team_read(call->source->context,
                    teammate.actor, &player_team, error), error)) return false;
            player_team.last_hurt_carrier_ms = -5;
            if (!after(call, call->source->player_team_write(call->source->context,
                    teammate.actor, &player_team, error), error)) return false;
            player->stats.hurt_carrier = false;
            continue;
        }
        if (!qa_actor_id_equal(teammate.actor, client->actor) &&
            !points(call, teammate.actor, item->origin, call->source->missionpack ? 25 : 0, error)) return false;
        player = member(call, teammate.actor, error);
        if (!player) return false;
        qa_q3_source_player_team_state player_team;
        if (!after(call, call->source->player_team_read(call->source->context,
                teammate.actor, &player_team, error), error)) return false;
        float now = (float)call->source->time_ms;
        volatile float returned = player_team.last_returned_flag_ms + 10000.0f;
        volatile float fragged = player_team.last_fragged_carrier_ms + 10000.0f;
        int32_t bonus = returned > now ? call->source->missionpack ? 10 : 1 :
            fragged > now ? call->source->missionpack ? 10 : 2 : -1;
        if (bonus < 0) continue;
        if (!points(call, teammate.actor, item->origin, bonus, error)) return false;
        if (!after(call, call->source->player_team_read(call->source->context,
                client->actor, &carrier_team, error), error)) return false;
        carrier_team.assists = mode_add_i32(carrier_team.assists, 1);
        if (!after(call, call->source->player_team_write(call->source->context,
                client->actor, &carrier_team, error), error)) return false;
        carrier = member(call, client->actor, error);
        player = member(call, teammate.actor, error);
        value = instance(call, error);
        if (!carrier || !player || !value) return false;
        mode_stat_add(value, &carrier->stats.assists, 1);
        mode_stat_add(value, &player->stats.assist_awards, 1);
        if (!after(call, call->source->pers_award(call->source->context,
                teammate.actor, QA_Q3_AWARD_ASSIST, 1, error), error) ||
            !after(call, call->source->award(call->source->context, teammate.actor, 0x20000, error), error))
            return false;
    }
    mode_q3_source_item base;
    if (call->source->game_type == 4) {
        if (!reset_flag(call, TEAM_RED, &base, error) || !reset_flag(call, TEAM_BLUE, &base, error))
            return false;
    } else if (call->source->missionpack && call->source->game_type == 5 &&
        !reset_flag(call, TEAM_FREE, &base, error)) return false;
    return after(call, call->source->ranks(call->source->context, error), error);
}

static bool pickup(const team_call *call, qa_actor_id item_actor, qa_actor_id player_actor,
    int32_t team, bool *accepted, qa_error *error)
{
    *accepted = false;
    mode_q3_source_item item;
    mode_q3_source_client client;
    if (!actual_item(call, item_actor, &item, error) ||
        !source_client(call, player_actor, &client, error)) return false;
    if (call->source->missionpack && (call->source->game_type == 6 || call->source->game_type == 7)) {
        if (call->source->game_type == 7 && item.spawn_team != client.team) {
            int32_t tokens;
            if (!after(call, call->source->tokens(call->source->context, player_actor, &tokens, error), error) ||
                !after(call, call->source->set_tokens(call->source->context, player_actor,
                    mode_add_i32(tokens, 1), error), error)) return false;
            mode_member *player = member(call, player_actor, error);
            if (!player) return false;
            player->stats.tokens = mode_add_i32(tokens, 1);
        }
        return after(call, call->source->retire(call->source->context, item_actor, error), error);
    }
    if (team < TEAM_FREE || team > TEAM_BLUE)
        return message(call, player_actor, "Don't know what team the flag is on.\n", error);
    bool one = call->source->missionpack && call->source->game_type == 5;
    bool ours = one ? team != TEAM_FREE && team != client.team : team == client.team;
    if (one && team != TEAM_FREE && team == client.team) return true;
    if (ours) {
        if (!one && item.dropped) {
            char text[128];
            snprintf(text, sizeof(text), "%s^7 returned the %s flag!\n", client.name, team_name(team));
            if (!message(call, (qa_actor_id){0}, text, error) ||
                !points(call, player_actor, item.origin, call->source->missionpack ? 10 : 1, error)) return false;
            mode_member *player = member(call, player_actor, error);
            mode_instance *value = instance(call, error);
            if (!player || !value) return false;
            qa_q3_source_player_team_state player_team;
            if (!after(call, call->source->player_team_read(call->source->context,
                    player_actor, &player_team, error), error)) return false;
            player_team.flag_recovery = mode_add_i32(player_team.flag_recovery, 1);
            player_team.last_returned_flag_ms = (float)call->source->time_ms;
            if (!after(call, call->source->player_team_write(call->source->context,
                    player_actor, &player_team, error), error)) return false;
            mode_stat_add(value, &player->stats.recoveries, 1);
            player->stats.returned_ns = (uint64_t)(uint32_t)call->source->time_ms * MODE_MILLISECOND;
            player->stats.returned = true;
            mode_q3_source_item base;
            return reset_flag(call, team, &base, error) && return_sound(call, &base, team, error);
        }
        int32_t carried;
        if (!powerup(call, player_actor, one ? TEAM_FREE :
                client.team == TEAM_RED ? TEAM_BLUE : TEAM_RED, &carried, error)) return false;
        return !carried || capture(call, &item, &client, one ? client.team : team, one, error);
    }
    char text[128];
    if (one) snprintf(text, sizeof(text), "%s^7 got the flag!\n", client.name);
    else snprintf(text, sizeof(text), "%s^7 got the %s flag!\n", client.name, team_name(team));
    if (!message(call, (qa_actor_id){0}, text, error) ||
        !set_powerup(call, player_actor, one ? TEAM_FREE : team, INT32_MAX, error)) return false;
    if (!one && !after(call, call->source->rank_pickup(call->source->context, player_actor, error), error))
        return false;
    if (!status(call, one ? TEAM_FREE : team, one ?
            client.team == TEAM_RED ? FLAG_TAKEN_RED : FLAG_TAKEN_BLUE : FLAG_TAKEN, error))
        return false;
    if (!points(call, player_actor, item.origin, call->source->missionpack ? 10 : 0, error)) return false;
    mode_member *player = member(call, player_actor, error);
    mode_instance *value = instance(call, error);
    if (!player || !value) return false;
    qa_q3_source_player_team_state player_team;
    if (!after(call, call->source->player_team_read(call->source->context,
            player_actor, &player_team, error), error)) return false;
    player_team.flag_since_ms = (float)call->source->time_ms;
    if (!after(call, call->source->player_team_write(call->source->context,
            player_actor, &player_team, error), error)) return false;
    player->stats.flag_since_ns = (uint64_t)(uint32_t)call->source->time_ms * MODE_MILLISECOND;
    mode_object *object = mode_object_get(call->modes, item_actor);
    if (!object || !object->q3_source_owned) return mode_fail(error, "TEAM item lost its adopted mode state");
    if (object->base.registry) object = mode_object_get(call->modes, object->base);
    if (!object || !object->q3_source_owned) return mode_fail(error, "TEAM dropped flag lost its actual base");
    qa_actor_id base_actor = object->actor;
    if (!after(call, mode_object_count(call->modes, value, object, player_actor, 1, error), error)) return false;
    player = member(call, player_actor, error);
    object = mode_object_get(call->modes, base_actor);
    if (!player || !object) return mode_fail(error, "TEAM acquisition retired during shared inventory publication");
    player->flag = base_actor;
    object->value.phase = QA_OBJECTIVE_CARRIED;
    object->value.carrier = player_actor;
    object->value.visible = false;
    object->value.deadline_ns = object->expire_ns = 0;
    object->dropped_actor = (qa_actor_id){0};
    if (!take_sound(call, &item, one ? client.team : team, error)) return false;
    *accepted = true;
    return true;
}

static bool adopt(qa_modes *modes, qa_mode_id id, qa_actor_owner owner,
    const qa_mode_object_spec *spec, bool dropped, qa_error *error)
{
    mode_instance *value = mode_get(modes, id);
    uint64_t serial = value ? value->serial : 0;
    const qa_actor_record *record = spec ? qa_actors_get(
        qa_session_actors(modes ? modes->options.services.session : NULL), spec->actor) : NULL;
    if (!value || !spec || !record || spec->actor.slot >= modes->actor_capacity ||
        (spec->kind != QA_MODE_OBJECT_FLAG && spec->kind != QA_MODE_OBJECT_CUBE &&
         spec->kind != QA_MODE_OBJECT_OBELISK) || mode_object_get(modes, spec->actor) ||
        modes->objects[spec->actor.slot].admitting || !owner ||
        !modes->options.hooks.q3_source_object)
        return mode_fail(error, "TEAM adoption requires an unclaimed real G_Spawn actor");
    bool source_owned = false;
    if (!modes->options.hooks.q3_source_object(modes->options.hooks.context,
            id, spec->actor, &source_owned, error) || !source_owned)
        return mode_fail(error, "TEAM adoption lacks its actual physical source binding");
    qa_actor_id actor = spec->actor;
    value = mode_get(modes, id);
    if (!value || value->serial != serial || !mode_live(modes, actor) ||
        mode_object_get(modes, actor) || modes->objects[actor.slot].admitting)
        return mode_fail(error, "TEAM source actor changed during its ownership qualification");
    qa_body_state body = {0};
    qa_actor_collision collision = {0};
    modes->objects[actor.slot] = (mode_object){.actor = actor, .admitting = true};
    bool okay = qa_world_body_read(modes->options.services.world, actor, &body, error);
    if (okay) {
        qa_error local = {0};
        (void)qa_world_get_collision(modes->options.services.world, actor, &collision, &local);
        if (local.code != QA_OK) { if (error) *error = local; okay = false; }
    }
    mode_object object = {.modes = modes, .actor = actor, .mode = id, .spec = *spec,
        .home = body.origin, .collision = collision, .active = true, .admitting = true,
        .q3_source_owned = true, .dropped = dropped,
        .value = {.mode = id, .kind = spec->kind, .team = spec->team,
            .phase = dropped ? QA_OBJECTIVE_DROPPED : QA_OBJECTIVE_HOME, .visible = true}};
    object.spec.actor = actor;
    object.spec.retain_body = true;
    if (okay && spec->kind == QA_MODE_OBJECT_FLAG && !object.spec.item)
        okay = mode_fail(error, "TEAM flag adoption requires its true source item identity");
    if (okay && object.spec.item) {
        qa_item_id scoped;
        okay = mode_inventory_item(modes, value, object.spec.item, &scoped, error);
    }
    if (okay) okay = mode_object_bind_objective(modes, &object, error);
    value = mode_get(modes, id);
    if (okay && (!value || value->serial != serial || !mode_live(modes, actor) ||
        !modes->objects[actor.slot].admitting ||
        !qa_actor_id_equal(modes->objects[actor.slot].actor, actor)))
        okay = mode_fail(error, "TEAM source actor retired during its mode adoption");
    if (!okay) {
        if (object.objective.serial) qa_modes_unbind_objective(modes, object.objective, NULL);
        if (modes->objects[actor.slot].admitting &&
            qa_actor_id_equal(modes->objects[actor.slot].actor, actor))
            modes->objects[actor.slot] = (mode_object){0};
        return false;
    }
    object.admitting = false;
    modes->objects[actor.slot] = object;
    if (object.objective.serial) (void)mode_commit_objective(modes, object.objective);
    if (!dropped && (spec->kind == QA_MODE_OBJECT_FLAG || spec->kind == QA_MODE_OBJECT_OBELISK)) {
        int team = mode_team_index(value, spec->team);
        if (!value->bases[team < 0 ? 2 : team].registry)
            value->bases[team < 0 ? 2 : team] = actor;
    }
    return true;
}

bool qa_modes_q3_source_object_adopt(qa_modes *modes, qa_mode_id id,
    qa_actor_owner owner, const qa_mode_object_spec *spec, bool dropped, qa_error *error)
{
    if (!modes) return mode_fail(error, "TEAM adoption requires its actual mode owner");
    return MODE_CALLBACK(modes, adopt(modes, id, owner, spec, dropped, error));
}

static bool object_available(qa_modes *modes, qa_mode_id id,
    qa_actor_id actor, bool available, qa_error *error)
{
    mode_object *object = mode_object_get(modes, actor);
    if (!object || !object->q3_source_owned || object->mode.slot != id.slot ||
        object->mode.generation != id.generation)
        return mode_fail(error, "TEAM visibility requires its actual adopted source item");
    if (available && !object->dropped) {
        qa_body_state body;
        if (!qa_world_body_read(modes->options.services.world, actor, &body, error)) return false;
        object = mode_object_get(modes, actor);
        if (!object || !object->q3_source_owned || object->mode.slot != id.slot ||
            object->mode.generation != id.generation)
            return mode_fail(error, "TEAM source item retired while completing its placement");
        object->home = body.origin;
    }
    object->value.visible = available;
    return true;
}

bool qa_modes_q3_source_object_available(qa_modes *modes, qa_mode_id id,
    qa_actor_id actor, bool available, qa_error *error)
{
    if (!modes) return mode_fail(error, "TEAM visibility requires its actual mode owner");
    return MODE_CALLBACK(modes, object_available(modes, id, actor, available, error));
}

bool qa_modes_q3_source_team_initialize(qa_modes *modes, qa_mode_id id,
    const mode_q3_objective_services *source, qa_error *error)
{
    team_call call;
    if (!begin(modes, id, source, &call, error)) return false;
    qa_q3_source_team_state state;
    if (!after(&call, source->team_read(source->context, &state, error), error)) return false;
    state.last_flag_capture_ms = 0;
    state.last_capture_team = state.red_status = state.blue_status = state.neutral_status = 0;
    state.red_taken_ms = state.blue_taken_ms = 0;
    state.red_obelisk_attacked_ms = state.blue_obelisk_attacked_ms = 0;
    state.initialized = true;
    if (source->game_type == 4) {
        state.red_status = state.blue_status = -1;
        if (!after(&call, source->team_write(source->context, &state, error), error)) return false;
        return MODE_CALLBACK(modes, status(&call, TEAM_RED, FLAG_HOME, error) &&
            status(&call, TEAM_BLUE, FLAG_HOME, error));
    }
    if (source->missionpack && source->game_type == 5) {
        state.neutral_status = -1;
        if (!after(&call, source->team_write(source->context, &state, error), error)) return false;
        return MODE_CALLBACK(modes, status(&call, TEAM_FREE, FLAG_HOME, error));
    }
    return after(&call, source->team_write(source->context, &state, error), error);
}

bool qa_modes_q3_source_item_pickup(qa_modes *modes, qa_mode_id id,
    qa_actor_id item, qa_actor_id player, int32_t team,
    const mode_q3_objective_services *source, bool *accepted, qa_error *error)
{
    team_call call;
    if (!accepted || !begin(modes, id, source, &call, error)) return false;
    return MODE_CALLBACK(modes, pickup(&call, item, player, team, accepted, error));
}

static bool dropped(const team_call *call, qa_actor_id actor, int32_t team, qa_error *error)
{
    mode_q3_source_item item;
    if (!actual_item(call, actor, &item, error)) return false;
    mode_instance *value = instance(call, error);
    mode_object *child = mode_object_get(call->modes, actor);
    if (!value || !child || !child->q3_source_owned || !item.dropped)
        return mode_fail(error, "Team_CheckDroppedItem requires its actual LaunchItem actor");
    mode_object *base = NULL;
    for (uint32_t slot = 64;; ++slot) {
        uint32_t count;
        if (!after(call, call->source->entity_count(call->source->context, &count, error), error))
            return false;
        if (slot >= count) break;
        mode_q3_source_item candidate;
        bool present;
        if (!item_read(call, slot, &candidate, &present, error)) return false;
        if (!present || candidate.dropped || candidate.flag_team != team) continue;
        mode_object *object = mode_object_get(call->modes, candidate.actor);
        if (!object || !object->q3_source_owned) continue;
        if (!base) base = object;
        if (object->value.carrier.registry) { base = object; break; }
    }
    if (base && base != child && base->q3_source_owned) {
        qa_actor_id carrier = base->value.carrier;
        base->value.phase = QA_OBJECTIVE_DROPPED;
        base->value.previous_owner = carrier;
        base->value.carrier = (qa_actor_id){0};
        base->value.visible = false;
        base->dropped_actor = actor;
        base->expire_ns = base->next_ns = base->value.deadline_ns = 0;
        child->base = base->actor;
    }
    return team < TEAM_FREE || team > TEAM_BLUE || status(call, team, FLAG_DROPPED, error);
}

bool qa_modes_q3_source_item_dropped(qa_modes *modes, qa_mode_id id,
    qa_actor_id actor, int32_t team, const mode_q3_objective_services *source, qa_error *error)
{
    team_call call;
    if (!begin(modes, id, source, &call, error)) return false;
    return MODE_CALLBACK(modes, dropped(&call, actor, team, error));
}

static bool flags_cleared(const team_call *call, qa_actor_id actor, qa_error *error)
{
    for (uint32_t slot = 0; slot < call->modes->actor_capacity; ++slot) {
        mode_object *object = &call->modes->objects[slot];
        if (!object->active || !object->q3_source_owned || object->spec.kind != QA_MODE_OBJECT_FLAG ||
            object->mode.slot != call->mode.slot || object->mode.generation != call->mode.generation ||
            (!qa_actor_id_equal(object->value.carrier, actor) &&
             !qa_actor_id_equal(object->value.previous_owner, actor))) continue;
        mode_instance *value = instance(call, error);
        if (!value) return false;
        mode_q3_source_item source_item;
        if (!actual_item(call, object->actor, &source_item, error)) return false;
        int32_t count;
        if (!powerup(call, actor, source_item.flag_team, &count, error)) return false;
        if (count) continue;
        qa_actor_id objective = object->actor;
        if (!after(call, mode_object_count(call->modes, value, object, actor, 0, error), error))
            return false;
        object = mode_object_get(call->modes, objective);
        value = instance(call, error);
        if (!object || !value)
            return mode_fail(error, "TEAM source flag retired during inventory clear publication");
        mode_member *player = mode_member_get(call->modes, value, actor);
        if (player && qa_actor_id_equal(player->flag, objective)) player->flag = (qa_actor_id){0};
        object->value.carrier = object->value.previous_owner = (qa_actor_id){0};
    }
    return true;
}

bool qa_modes_q3_source_flags_cleared(qa_modes *modes, qa_mode_id id,
    qa_actor_id actor, const mode_q3_objective_services *source, qa_error *error)
{
    team_call call;
    if (!begin(modes, id, source, &call, error)) return false;
    return MODE_CALLBACK(modes, flags_cleared(&call, actor, error));
}

static bool expire(const team_call *call, qa_actor_id actor, int32_t team,
    bool no_drop, qa_error *error)
{
    if (team < TEAM_FREE || team > TEAM_BLUE)
        return after(call, call->source->retire(call->source->context, actor, error), error);
    mode_q3_source_item base;
    if (!reset_flag(call, team, &base, error) || !return_sound(call, &base, team, error)) return false;
    if (!no_drop) return true;
    char text[64];
    if (team == TEAM_FREE) snprintf(text, sizeof(text), "The flag has returned!\n");
    else snprintf(text, sizeof(text), "The %s flag has returned!\n", team_name(team));
    return message(call, (qa_actor_id){0}, text, error);
}

bool qa_modes_q3_source_item_expired(qa_modes *modes, qa_mode_id id,
    qa_actor_id actor, int32_t team, bool no_drop,
    const mode_q3_objective_services *source, qa_error *error)
{
    team_call call;
    if (!begin(modes, id, source, &call, error)) return false;
    return MODE_CALLBACK(modes, expire(&call, actor, team, no_drop, error));
}

bool qa_modes_q3_source_return_flag(qa_modes *modes, qa_mode_id id, int32_t team,
    const mode_q3_objective_services *source, qa_error *error)
{
    team_call call;
    if (team < TEAM_FREE || team > TEAM_BLUE || !begin(modes, id, source, &call, error))
        return mode_fail(error, "Team_ReturnFlag requires a real source flag team");
    return MODE_CALLBACK(modes, expire(&call, (qa_actor_id){0}, team, true, error));
}

static bool optional_client(const team_call *call, qa_actor_id actor,
    bool *present, qa_error *error)
{
    *present = false;
    for (uint32_t slot = 0; slot < call->source->max_clients; ++slot) {
        mode_q3_source_client row;
        if (!after(call, call->source->client_at(call->source->context, slot, &row, error), error)) return false;
        if (row.in_use && qa_actor_id_equal(row.actor, actor)) { *present = true; return true; }
    }
    return true;
}

static bool obelisk_touch(const team_call *call, const mode_q3_source_item *item,
    qa_actor_id actor, qa_error *error)
{
    mode_q3_source_client client;
    int32_t tokens;
    if (!source_client(call, actor, &client, error) ||
        !after(call, call->source->tokens(call->source->context, actor, &tokens, error), error)) return false;
    if (tokens <= 0) return true;
    char text[128];
    snprintf(text, sizeof(text), "%s^7 brought in %d skulls.\n", client.name, tokens);
    if (!message(call, (qa_actor_id){0}, text, error) ||
        !add_team_score(call, item, client.team, tokens, error) ||
        !after(call, call->source->gesture(call->source->context, client.team, error), error)) return false;
    uint32_t bits = (uint32_t)tokens * 100u;
    int32_t score;
    memcpy(&score, &bits, sizeof(score));
    if (!points(call, actor, item->origin, score, error) ||
        !after(call, call->source->award(call->source->context, actor, 0x800, error), error)) return false;
    if (!after(call, call->source->pers_award(call->source->context,
            actor, QA_Q3_AWARD_CAPTURE, tokens, error), error)) return false;
    if (!after(call, call->source->set_tokens(call->source->context, actor, 0, error), error)) return false;
    mode_member *player = member(call, actor, error);
    if (!player) return false;
    player->stats.tokens = 0;
    return after(call, call->source->ranks(call->source->context, error), error) &&
        sound(call, item, item->flag_team == TEAM_BLUE ? 1 : 0,
            "Warning:  NULL passed to Team_CaptureFlagSound\n", error);
}

bool qa_modes_q3_source_obelisk_touch(qa_modes *modes, qa_mode_id id,
    const mode_q3_source_item *item, qa_actor_id actor,
    const mode_q3_objective_services *source, qa_error *error)
{
    team_call call;
    if (!item || !begin(modes, id, source, &call, error)) return false;
    return MODE_CALLBACK(modes, obelisk_touch(&call, item, actor, error));
}

static bool obelisk_die(const team_call *call, const mode_q3_source_item *item,
    qa_actor_id actor, qa_q3_obelisk_die_stage stage, qa_error *error)
{
    if (stage == QA_Q3_OBELISK_DIE_TEAM_SCORE) {
        int32_t team = item->flag_team == TEAM_RED ? TEAM_BLUE :
            item->flag_team == TEAM_BLUE ? TEAM_RED : item->flag_team;
        return add_team_score(call, item, team, 1, error) &&
            after(call, call->source->gesture(call->source->context, team, error), error) &&
            after(call, call->source->ranks(call->source->context, error), error);
    }
    bool present;
    if (!optional_client(call, actor, &present, error)) return false;
    if (!present) return true;
    if (!points(call, actor, item->origin, 100, error) ||
        !after(call, call->source->award(call->source->context, actor, 0x800, error), error)) return false;
    return after(call, call->source->pers_award(call->source->context,
        actor, QA_Q3_AWARD_CAPTURE, 1, error), error);
}

bool qa_modes_q3_source_obelisk_die(qa_modes *modes, qa_mode_id id,
    const mode_q3_source_item *item, qa_actor_id actor, qa_q3_obelisk_die_stage stage,
    const mode_q3_objective_services *source, qa_error *error)
{
    team_call call;
    if (!item || (stage != QA_Q3_OBELISK_DIE_TEAM_SCORE && stage != QA_Q3_OBELISK_DIE_PLAYER_SCORE) ||
        !begin(modes, id, source, &call, error)) return false;
    return MODE_CALLBACK(modes, obelisk_die(&call, item, actor, stage, error));
}

static bool obelisk_pain(const team_call *call, const mode_q3_source_item *item,
    qa_actor_id actor, int32_t amount, qa_error *error)
{
    bool present;
    if (!optional_client(call, actor, &present, error)) return false;
    return !present || points(call, actor, item->origin, amount, error);
}

bool qa_modes_q3_source_obelisk_pain(qa_modes *modes, qa_mode_id id,
    const mode_q3_source_item *item, qa_actor_id actor, int32_t amount,
    const mode_q3_objective_services *source, qa_error *error)
{
    team_call call;
    if (!item || !begin(modes, id, source, &call, error)) return false;
    return MODE_CALLBACK(modes, obelisk_pain(&call, item, actor, amount, error));
}

bool qa_modes_q3_source_object_at(qa_modes *modes, qa_mode_id id, size_t index,
    qa_actor_id *actor, qa_mode_object_spec *spec, qa_mode_object_view *view,
    qa_actor_id *base, qa_error *error)
{
    if (!mode_get(modes, id) || !actor || !spec || !view || !base)
        return mode_fail(error, "TEAM source inventory observation requires its actual mode");
    for (uint32_t slot = 0; slot < modes->actor_capacity; ++slot) {
        mode_object *object = &modes->objects[slot];
        if (!object->active || !object->q3_source_owned || object->mode.slot != id.slot ||
            object->mode.generation != id.generation || !mode_live(modes, object->actor)) continue;
        if (index--) continue;
        *actor = object->actor; *spec = object->spec; *view = object->value; *base = object->base;
        return true;
    }
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "TEAM source object index outside its real retained rows");
    return false;
}
