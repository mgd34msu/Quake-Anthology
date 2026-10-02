#include "native_q3_objectives.h"
#include "native_q3_clients.h"
#include "native_q3_console.h"
#include "native_q3_settings.h"
#include "native_q3_wire_state.h"
#include "native_q3_rank.h"
#include "native_q3_match.h"
#include "native_q3_log.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_map.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "../../gameplay/modes/q3_objective_source.h"

#include <stdio.h>
#include <string.h>

typedef struct objective_call {
    application_provider *provider;
    qa_application *application;
    qa_q3_game *game;
    qa_modes *modes;
    qa_mode_id mode;
    qa_actor_owner owner;
    uint64_t publication_generation, command_generation, map_revision;
    bool source_entered;
} objective_call;

static bool source_live(void *opaque, qa_error *error)
{
    objective_call *call = opaque;
    application_provider *provider = call->provider;
    qa_mode_view mode;
    if (!provider || provider->application != call->application ||
        provider->kind != APPLICATION_PROVIDER_Q3 || provider->state.q3 != call->game ||
        provider->owner != call->owner || !provider->constructed || !provider->attached ||
        provider->close_pending || call->application->destroy_requested ||
        call->application->modes != call->modes ||
        call->application->publication_generation != call->publication_generation ||
        call->application->command_generation != call->command_generation ||
        call->application->map_revision != call->map_revision)
        return application_fail(error, QA_ERROR_NOT_FOUND, "TEAM source retired during its callback");
    return (!call->source_entered ||
        application_native_q3_source_mode_current(provider, call->mode, error)) &&
        qa_modes_read(call->modes, call->mode, &mode, error);
}

static bool begin(application_provider *provider, qa_modes *modes, qa_mode_id mode,
    objective_call *call, qa_error *error)
{
    qa_application *application = provider ? provider->application : NULL;
    if (!application || !modes || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->state.q3 || !provider->constructed || !provider->attached ||
        provider->close_pending || application->destroy_requested ||
        (application->operation != APPLICATION_IDLE &&
         application->operation != APPLICATION_CONFIGURING &&
         application->operation != APPLICATION_ADVANCING))
        return application_fail(error, QA_ERROR_ARGUMENT, "TEAM has no admitted native source operation");
    *call = (objective_call){.provider = provider, .application = application,
        .game = provider->state.q3, .modes = modes, .mode = mode, .owner = provider->owner,
        .publication_generation = application->publication_generation,
        .command_generation = application->command_generation,
        .map_revision = application->map_revision,
        .source_entered = application_native_q3_source_entered(provider)};
    return source_live(call, error) && application_native_q3_console_borrow(provider, error);
}

static bool primary(application_provider *provider, objective_call *call, qa_error *error)
{
    qa_application *application = provider ? provider->application : NULL;
    qa_mode_id mode;
    bool found;
    if (!application)
        return application_fail(error, QA_ERROR_NOT_FOUND, "TEAM has no actual associated match owner");
    if (!application_native_q3_source_mode(provider, &mode, &found, error)) return false;
    if (!found)
        return application_fail(error, QA_ERROR_NOT_FOUND, "TEAM has no actual associated match owner");
    return begin(provider, application->modes, mode, call, error);
}

static bool view_provider(qa_application *application, qa_mode_id mode,
    application_provider **out, qa_error *error)
{
    application_provider *chosen = application_mode_provider(application, mode);
    if (chosen && application_native_q3_source_entered(chosen)) {
        if (!application_native_q3_source_mode_current(chosen, mode, error)) return false;
        *out = chosen;
    } else *out = application_world_provider(application, QA_ROLE_ENTITIES, "");
    return true;
}

static bool finish(objective_call *call, bool okay, qa_error *error)
{
    if (okay) okay = source_live(call, error);
    application_native_q3_console_release(call->provider);
    return okay;
}

static bool actual_actor(objective_call *call, qa_actor_id actor, uint32_t *slot,
    qa_error *error)
{
    qa_q3_source_binding row;
    if (!source_live(call, error) || !qa_q3_source_actor_slot(call->game, actor, slot, error) ||
        !qa_q3_source_binding_read(call->game, *slot, &row, error) || !row.in_use ||
        !qa_actor_id_equal(row.actor, actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "TEAM actor lost its physical source generation");
    return true;
}

static qa_q3_product product(const objective_call *call)
{
    return !strcmp(call->provider->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
}

static bool definition(const objective_call *call, uint32_t index,
    const qa_q3_item **out, int32_t *team, qa_error *error)
{
    size_t count;
    const qa_q3_item *items = qa_q3_items(product(call), &count);
    if (!index || index >= count || items[index].kind != QA_Q3_ITEM_TEAM)
        return application_fail(error, QA_ERROR_ARGUMENT, "TEAM item is outside its source product table");
    *out = &items[index];
    *team = items[index].tag == 7 ? 1 : items[index].tag == 8 ? 2 : items[index].tag == 9 ? 0 : -1;
    return true;
}

static bool item_at(void *opaque, uint32_t slot, mode_q3_source_item *out,
    bool *present, qa_error *error)
{
    objective_call *call = opaque;
    qa_q3_source_binding row;
    *present = false;
    *out = (mode_q3_source_item){0};
    if (!source_live(call, error) || !qa_q3_source_binding_read(call->game, slot, &row, error))
        return false;
    if (!row.in_use || !row.actor.registry) return true;
    qa_q3_item_spawn spawn;
    bool finished;
    qa_error local = {0};
    if (!qa_q3_source_item_spawn_read(call->game, row.actor, &spawn, &finished, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = local;
        return false;
    }
    const qa_q3_item *entry;
    int32_t team;
    size_t count;
    const qa_q3_item *items = qa_q3_items(product(call), &count);
    if (!spawn.item_index || spawn.item_index >= count)
        return application_fail(error, QA_ERROR_ARGUMENT, "TEAM source row has an invalid item index");
    if (items[spawn.item_index].kind != QA_Q3_ITEM_TEAM) return true;
    if (!definition(call, spawn.item_index, &entry, &team, error)) return false;
    qa_body_state body;
    if (!qa_world_body_read(call->application->world, row.actor, &body, error) ||
        !source_live(call, error)) return false;
    uint32_t actual;
    if (!actual_actor(call, row.actor, &actual, error) || actual != slot)
        return application_fail(error, QA_ERROR_NOT_FOUND, "TEAM source row changed during its body read");
    int32_t spawn_team;
    if (!qa_q3_source_spawnflags_read(call->game, row.actor, &spawn_team, error)) return false;
    qa_q3_item_state item;
    qa_vec3 trajectory_base = spawn.origin;
    if (finished) {
        if (!qa_q3_item_read(call->game, row.actor, &item))
            return application_fail(error, QA_ERROR_NOT_FOUND, "TEAM item lost its finished native item owner");
        trajectory_base = item.trajectory.base;
    }
    *out = (mode_q3_source_item){.actor = row.actor, .origin = body.origin,
        .trajectory_base = trajectory_base, .flag_team = team,
        .spawn_team = spawn_team, .dropped = spawn.dropped};
    *present = true;
    return true;
}

static bool entity_count(void *opaque, uint32_t *out, qa_error *error)
{
    objective_call *call = opaque;
    return source_live(call, error) && qa_q3_source_entity_count(call->game, out, error);
}

static bool client_at(void *opaque, uint32_t slot, mode_q3_source_client *out, qa_error *error)
{
    objective_call *call = opaque;
    qa_q3_source_binding row;
    qa_q3_native_client client;
    qa_q3_client_session session;
    if (!source_live(call, error) || !qa_q3_source_binding_read(call->game, slot, &row, error) ||
        !qa_q3_client_slot_read(call->game, slot, &client, error)) return false;
    *out = (mode_q3_source_client){.actor = row.actor,
        .in_use = row.in_use && row.actor.registry != 0};
    memcpy(out->name, client.netname, sizeof(out->name));
    if (!out->in_use) return true;
    if (!qa_q3_client_session_slot_read(call->game, slot, &session, error)) return false;
    out->team = session.team;
    return true;
}

static bool powerup(void *opaque, qa_actor_id actor, int32_t tag, int32_t *out, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    qa_q3_player_state player;
    if (tag < 7 || tag > 9 || !actual_actor(call, actor, &slot, error) || slot >= 64 ||
        !qa_q3_player_read(call->game, actor, &player))
        return application_fail(error, QA_ERROR_NOT_FOUND, "TEAM powerup has no actual source client");
    *out = player.powerups[tag];
    return true;
}

static bool set_powerup(void *opaque, qa_actor_id actor, int32_t tag,
    int32_t value, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) && slot < 64 &&
        qa_q3_client_flag_powerup(call->game, actor, (uint32_t)tag, value, error) &&
        actual_actor(call, actor, &slot, error);
}

static bool team_read(void *opaque, qa_q3_source_team_state *out, qa_error *error)
{
    objective_call *call = opaque;
    return source_live(call, error) && qa_q3_source_team_state_read(call->game, out, error);
}

static bool team_write(void *opaque, const qa_q3_source_team_state *value, qa_error *error)
{
    objective_call *call = opaque;
    return source_live(call, error) && qa_q3_source_team_state_write(call->game, value, error);
}

static bool player_team_read(void *opaque, qa_actor_id actor,
    qa_q3_source_player_team_state *out, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) && slot < QA_Q3_SOURCE_CLIENTS &&
        qa_q3_client_team_state_read(call->game, slot, out, error) &&
        actual_actor(call, actor, &slot, error);
}

static bool player_team_write(void *opaque, qa_actor_id actor,
    const qa_q3_source_player_team_state *state, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) && slot < QA_Q3_SOURCE_CLIENTS &&
        qa_q3_client_team_state_write(call->game, slot, state, error) &&
        actual_actor(call, actor, &slot, error);
}

static bool team_score(void *opaque, int32_t team, int32_t amount, qa_error *error)
{
    objective_call *call = opaque;
    qa_q3_source_team_state state;
    if (team < 0 || team > 3 || !team_read(call, &state, error)) return false;
    uint32_t bits = (uint32_t)state.team_scores[team] + (uint32_t)amount;
    memcpy(&state.team_scores[team], &bits, sizeof(bits));
    if (!team_write(call, &state, error)) return false;
    qa_mode_view mode;
    if (!qa_modes_read(call->modes, call->mode, &mode, error)) return false;
    qa_team_id shared_team = team == 1 ? mode.rules.teams[0] : team == 2 ? mode.rules.teams[1] : 0;
    return !shared_team || (qa_modes_team_score(call->modes, call->mode, shared_team, amount, error) &&
        source_live(call, error));
}

static bool ranks(void *opaque, qa_error *error)
{
    objective_call *call = opaque;
    return source_live(call, error) && application_native_q3_rank(call->provider, error) &&
        source_live(call, error);
}

static bool tokens(void *opaque, qa_actor_id actor, int32_t *out, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) && slot < 64 &&
        qa_q3_client_tokens_read(call->game, actor, out, error);
}

static bool set_tokens(void *opaque, qa_actor_id actor, int32_t count, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) && slot < 64 &&
        qa_q3_client_tokens_write(call->game, actor, count, error) &&
        actual_actor(call, actor, &slot, error);
}

static bool respawn(void *opaque, qa_actor_id actor, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) &&
        qa_q3_source_item_respawn(call->game, actor, error) && source_live(call, error);
}

static bool retire(void *opaque, qa_actor_id actor, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) &&
        qa_session_release(call->application->session, actor, error) && source_live(call, error);
}

static bool configstring(void *opaque, uint32_t index, const char *text, qa_error *error)
{
    objective_call *call = opaque;
    return source_live(call, error) && qa_q3_configstring_write(call->game, index, text, error) &&
        source_live(call, error);
}

static bool print(void *opaque, qa_actor_id recipient, const char *text, qa_error *error)
{
    objective_call *call = opaque;
    char command[1040];
    size_t length = strlen(text);
    if (length > 1024)
        return application_fail(error, QA_ERROR_ARGUMENT, "PrintMsg overrun");
    uint32_t slot = 0;
    if (recipient.registry && !actual_actor(call, recipient, &slot, error)) return false;
    memcpy(command, "print \"", 7);
    for (size_t index = 0; index < length; ++index)
        command[index + 7] = text[index] == '"' ? '\'' : text[index];
    command[length + 7] = '"'; command[length + 8] = 0;
    return application_native_q3_send_command(call->provider,
        recipient.registry ? (int32_t)slot : -1, command, error) && source_live(call, error);
}

static bool warn(void *opaque, const char *text, qa_error *error)
{
    objective_call *call = opaque;
    return application_native_q3_console_print(call->provider, text, error) && source_live(call, error);
}

static bool sound(void *opaque, qa_vec3 origin, int32_t parameter, qa_error *error)
{
    objective_call *call = opaque;
    return qa_q3_source_team_sound(call->game, origin, parameter, error) && source_live(call, error);
}

static bool gesture(void *opaque, int32_t team, qa_error *error)
{
    objective_call *call = opaque;
    return qa_q3_source_team_gesture(call->game, team, error) && source_live(call, error);
}

static bool award(void *opaque, qa_actor_id actor, uint32_t flags, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) &&
        qa_q3_source_award_visual(call->game, actor, flags, error) &&
        actual_actor(call, actor, &slot, error);
}

static bool pers_award(void *opaque, qa_actor_id actor, qa_q3_source_award field,
    int32_t amount, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) &&
        qa_q3_client_award(call->game, actor, field, amount, error) &&
        actual_actor(call, actor, &slot, error);
}

static bool rank_capture(void *opaque, qa_actor_id actor, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) && qa_q3_ranking_capture(call->game, actor, error) &&
        actual_actor(call, actor, &slot, error);
}

static bool score_plum(void *opaque, qa_actor_id actor, qa_vec3 origin,
    int32_t score, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) &&
        qa_q3_source_score_plum(call->game, actor, origin, score, error) &&
        actual_actor(call, actor, &slot, error);
}

static bool rank_pickup(void *opaque, qa_actor_id actor, qa_error *error)
{
    objective_call *call = opaque;
    uint32_t slot;
    return actual_actor(call, actor, &slot, error) && qa_q3_ranking_flag_pickup(call->game, actor, error) &&
        actual_actor(call, actor, &slot, error);
}

static bool services(objective_call *call, mode_q3_objective_services *out, qa_error *error)
{
    *out = (mode_q3_objective_services){.context = call,
        .missionpack = product(call) == QA_Q3_TEAM_ARENA, .live = source_live,
        .team_read = team_read, .team_write = team_write, .team_score = team_score,
        .player_team_read = player_team_read, .player_team_write = player_team_write,
        .ranks = ranks, .tokens = tokens, .set_tokens = set_tokens,
        .entity_count = entity_count, .item_at = item_at, .client_at = client_at,
        .powerup = powerup, .set_powerup = set_powerup,
        .respawn = respawn, .retire = retire, .configstring = configstring, .print = print, .warn = warn,
        .sound = sound, .gesture = gesture, .award = award, .pers_award = pers_award,
        .score_plum = score_plum,
        .rank_capture = rank_capture, .rank_pickup = rank_pickup};
    return qa_q3_source_clock(call->game, &out->time_ms, error) &&
        application_native_q3_settings_integer(call->provider, "g_gametype", &out->game_type, error) &&
        qa_q3_source_max_clients(call->game, &out->max_clients, error) && source_live(call, error);
}

static bool adopt(objective_call *call, qa_actor_id actor,
    const qa_mode_object_spec *spec, qa_error *error)
{
    uint32_t slot;
    qa_q3_item_spawn item;
    bool finished;
    if (!actual_actor(call, actor, &slot, error) || slot < 64 || slot >= 1022 ||
        !qa_q3_source_item_spawn_read(call->game, actor, &item, &finished, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "TEAM adoption lacks its genuine prepared source item");
    const qa_q3_item *entry;
    int32_t team;
    if (!definition(call, item.item_index, &entry, &team, error)) return false;
    qa_mode_object_spec requested = *spec;
    if ((team >= 0 && requested.kind != QA_MODE_OBJECT_FLAG) ||
        (team < 0 && requested.kind != QA_MODE_OBJECT_CUBE))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "TEAM metadata kind differs from its actual source item");
    requested.actor = actor;
    requested.retain_body = true;
    requested.suspended = true;
    if (requested.kind == QA_MODE_OBJECT_FLAG && !requested.item) {
        char name[96];
        int length = snprintf(name, sizeof(name), "q3:item/%s", entry->classname);
        if (length < 0 || (size_t)length >= sizeof(name) ||
            !qa_strings_intern_cstr(qa_session_strings(call->application->session), name,
                &requested.item, error)) return false;
    }
    qa_mode_object_view existing;
    bool okay;
    if (qa_modes_object_read(call->modes, actor, &existing)) {
        okay = existing.mode.slot == call->mode.slot && existing.mode.generation == call->mode.generation &&
            existing.kind == requested.kind && existing.team == requested.team;
        if (!okay) application_fail(error, QA_ERROR_ARGUMENT, "TEAM source item has conflicting mode adoption");
    } else okay = qa_modes_q3_source_object_adopt(call->modes, call->mode, call->owner,
        &requested, item.dropped, error);
    qa_q3_item_state published;
    bool visible = finished && qa_q3_item_read(call->game, actor, &published) && !published.hidden;
    return okay && qa_modes_q3_source_object_available(call->modes, call->mode,
        actor, visible, error) && source_live(call, error);
}

bool application_native_q3_objective_adopt(application_provider *provider, qa_modes *modes,
    qa_mode_id mode, qa_actor_id actor, const qa_mode_object_spec *spec, qa_error *error)
{
    objective_call call;
    if (!spec || !begin(provider, modes, mode, &call, error)) return false;
    return finish(&call, adopt(&call, actor, spec, error), error);
}

static bool item_spec(objective_call *call, qa_actor_id actor, uint32_t index,
    qa_mode_object_spec *spec, int32_t *team, qa_error *error)
{
    const qa_q3_item *entry;
    qa_mode_view mode;
    if (!definition(call, index, &entry, team, error) ||
        !qa_modes_read(call->modes, call->mode, &mode, error)) return false;
    *spec = (qa_mode_object_spec){.actor = actor,
        .kind = *team >= 0 ? QA_MODE_OBJECT_FLAG : QA_MODE_OBJECT_CUBE,
        .team = *team == 1 ? mode.rules.teams[0] : *team == 2 ? mode.rules.teams[1] : 0,
        .suspended = true, .retain_body = true};
    if (*team < 0) {
        int32_t source_team;
        if (!qa_q3_source_spawnflags_read(call->game, actor, &source_team, error)) return false;
        spec->team = source_team == 1 ? mode.rules.teams[0] : source_team == 2 ? mode.rules.teams[1] : 0;
    }
    return true;
}

static bool physical_obelisk(objective_call *call, qa_actor_id actor,
    mode_q3_source_item *out, qa_q3_obelisk_state *state, qa_error *error)
{
    uint32_t slot;
    qa_body_state body;
    qa_q3_entity wire;
    qa_q3_wire_visibility visibility;
    if (!actual_actor(call, actor, &slot, error) || slot < 64 || slot >= 1022 ||
        !qa_q3_source_obelisk_read(call->game, actor, state, error) ||
        !qa_world_body_read(call->application->world, actor, &body, error) ||
        !actual_actor(call, actor, &slot, error) ||
        !qa_q3_wire_entity_read(call->game, slot, &wire, &visibility, error)) return false;
    *out = (mode_q3_source_item){.actor = actor, .origin = body.origin,
        .trajectory_base = qa_v3(wire.pos.base[0], wire.pos.base[1], wire.pos.base[2]),
        .flag_team = state->team};
    return source_live(call, error);
}

bool application_native_q3_obelisk_settings(void *opaque, qa_q3_obelisk_settings *out,
    qa_error *error)
{
    application_provider *provider = opaque;
    if (!out || !provider || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->state.q3 || !provider->constructed || !provider->attached ||
        provider->close_pending || provider->application->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Obelisk settings lost their actual source owner");
    qa_q3_game *game = provider->state.q3;
    qa_actor_owner owner = provider->owner;
    if (!application_native_q3_console_borrow(provider, error)) return false;
    bool okay = application_native_q3_settings_integer(provider, "g_obeliskHealth", &out->health, error) &&
        application_native_q3_settings_integer(provider, "g_obeliskRegenPeriod", &out->regen_period_seconds, error) &&
        application_native_q3_settings_integer(provider, "g_obeliskRegenAmount", &out->regen_amount, error) &&
        application_native_q3_settings_integer(provider, "g_obeliskRespawnDelay", &out->respawn_delay_seconds, error);
    if (okay && (provider->state.q3 != game || provider->owner != owner ||
        provider->close_pending || provider->application->destroy_requested))
        okay = application_fail(error, QA_ERROR_NOT_FOUND, "Obelisk source retired during its settings read");
    application_native_q3_console_release(provider);
    return okay;
}

bool application_native_q3_obelisk_admitted(void *opaque, qa_actor_id actor,
    qa_actor_id model, int32_t team, qa_error *error)
{
    objective_call call;
    if (!primary(opaque, &call, error)) return false;
    uint32_t slot;
    qa_q3_obelisk_state state;
    qa_mode_view mode;
    bool okay = actual_actor(&call, actor, &slot, error) &&
        qa_q3_source_obelisk_read(call.game, actor, &state, error) &&
        actual_actor(&call, model, &slot, error) && state.team == team &&
        (team == 0 ? !state.model.registry : qa_actor_id_equal(state.model, model)) &&
        qa_modes_read(call.modes, call.mode, &mode, error);
    if (okay) {
        qa_mode_object_spec spec = {.actor = actor, .kind = QA_MODE_OBJECT_OBELISK,
            .team = team == 1 ? mode.rules.teams[0] : team == 2 ? mode.rules.teams[1] : 0,
            .suspended = true, .retain_body = true};
        qa_mode_object_view existing;
        if (!qa_modes_object_read(call.modes, actor, &existing))
            okay = qa_modes_q3_source_object_adopt(call.modes, call.mode, call.owner, &spec, false, error);
        if (okay) okay = qa_modes_q3_source_object_available(call.modes, call.mode, actor, true, error);
    } else if (!error || error->code == QA_OK)
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Obelisk admission lost its true model and trigger identities");
    return finish(&call, okay, error);
}

bool application_native_q3_obelisk_touch(void *opaque, qa_actor_id actor,
    qa_actor_id player, qa_error *error)
{
    objective_call call;
    if (!primary(opaque, &call, error)) return false;
    mode_q3_objective_services source;
    mode_q3_source_item item;
    qa_q3_obelisk_state state;
    bool okay = services(&call, &source, error) && physical_obelisk(&call, actor, &item, &state, error) &&
        qa_modes_q3_source_obelisk_touch(call.modes, call.mode, &item, player, &source, error);
    return finish(&call, okay, error);
}

bool application_native_q3_obelisk_die(void *opaque, qa_actor_id actor,
    qa_actor_id attacker, qa_q3_obelisk_die_stage stage, qa_error *error)
{
    objective_call call;
    if (!primary(opaque, &call, error)) return false;
    mode_q3_objective_services source;
    mode_q3_source_item item;
    qa_q3_obelisk_state state;
    bool okay = services(&call, &source, error) && physical_obelisk(&call, actor, &item, &state, error) &&
        qa_modes_q3_source_obelisk_die(call.modes, call.mode, &item, attacker, stage, &source, error);
    return finish(&call, okay, error);
}

bool application_native_q3_obelisk_pain(void *opaque, qa_actor_id actor,
    qa_actor_id attacker, int32_t amount, qa_error *error)
{
    objective_call call;
    if (!primary(opaque, &call, error)) return false;
    mode_q3_objective_services source;
    mode_q3_source_item item;
    qa_q3_obelisk_state state;
    bool okay = services(&call, &source, error) && physical_obelisk(&call, actor, &item, &state, error) &&
        qa_modes_q3_source_obelisk_pain(call.modes, call.mode, &item, attacker, amount, &source, error);
    return finish(&call, okay, error);
}

static bool class_equal(const char *left, const char *right)
{
    while (*left && *right) {
        unsigned char a = (unsigned char)*left++, b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 'a' - 'A');
        if (a != b) return false;
    }
    return !*left && !*right;
}

static bool team_item_warnings(objective_call *call,
    const mode_q3_objective_services *source, qa_error *error)
{
    static const char *const flag_classes[] = {"team_CTF_redflag", "team_CTF_blueflag", "team_CTF_neutralflag"};
    size_t flag_count = source->game_type == 4 ? 2 :
        source->missionpack && source->game_type == 5 ? 3 : 0;
    qa_q3_product product = source->missionpack ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    for (size_t index = 0; index < flag_count; ++index) {
        uint32_t item;
        bool registered = false;
        if (qa_q3_find_item(product, flag_classes[index], &item) &&
            !qa_q3_map_item_registered(call->game, item, &registered, error)) return false;
        if (!registered) {
            char message[96];
            snprintf(message, sizeof(message), "^3WARNING: No %s in map", flag_classes[index]);
            if (!application_native_q3_console_print(call->provider, message, error) ||
                !source_live(call, error)) return false;
        }
    }
    static const char *const obelisks[] = {"team_redobelisk", "team_blueobelisk", "team_neutralobelisk"};
    size_t obelisk_count = !source->missionpack ? 0 : source->game_type == 6 ? 2 :
        source->game_type == 7 ? 3 : 0;
    for (size_t index = 0; index < obelisk_count; ++index) {
        uint32_t count;
        if (!qa_q3_source_entity_count(call->game, &count, error)) return false;
        bool found = false;
        for (uint32_t slot = 0; slot < count && !found; ++slot) {
            qa_q3_source_binding row;
            if (!qa_q3_source_binding_read(call->game, slot, &row, error)) return false;
            if (!row.in_use || !row.classname) continue;
            const char *name = qa_strings_cstr(qa_session_strings(call->application->session), row.classname);
            if (!name) return application_fail(error, QA_ERROR_NOT_FOUND,
                "checkTeamItems lost its actual source classname");
            found = class_equal(name, obelisks[index]);
        }
        if (!found) {
            char message[96];
            snprintf(message, sizeof(message), "^3WARNING: No %s in map", obelisks[index]);
            if (!application_native_q3_console_print(call->provider, message, error) ||
                !source_live(call, error)) return false;
        }
    }
    return source_live(call, error);
}

bool application_native_q3_objectives_initialize(application_provider *provider, qa_error *error)
{
    objective_call call;
    if (!primary(provider, &call, error)) return false;
    mode_q3_objective_services source;
    bool okay = services(&call, &source, error) &&
        qa_modes_q3_source_team_initialize(call.modes, call.mode, &source, error) &&
        team_item_warnings(&call, &source, error);
    return finish(&call, okay, error);
}

bool application_native_q3_objective_pickup(void *opaque, qa_actor_id actor,
    qa_actor_id player, uint32_t index, bool *accepted, qa_error *error)
{
    objective_call call;
    if (!accepted || !primary(opaque, &call, error)) return false;
    qa_mode_object_spec spec;
    int32_t team;
    mode_q3_objective_services source;
    qa_mode_object_view existing;
    bool okay = item_spec(&call, actor, index, &spec, &team, error);
    if (okay && !qa_modes_object_read(call.modes, actor, &existing))
        okay = adopt(&call, actor, &spec, error);
    if (okay) okay = services(&call, &source, error) &&
        qa_modes_q3_source_item_pickup(call.modes, call.mode, actor, player, team,
            &source, accepted, error);
    return finish(&call, okay, error);
}

bool application_native_q3_objective_admitted(void *opaque, qa_actor_id actor,
    uint32_t index, bool finished, qa_error *error)
{
    objective_call call;
    if (!primary(opaque, &call, error)) return false;
    qa_mode_object_spec spec;
    int32_t team;
    qa_mode_object_view existing;
    bool okay = item_spec(&call, actor, index, &spec, &team, error);
    if (okay && qa_modes_object_read(call.modes, actor, &existing)) {
        qa_q3_item_state item;
        bool visible = finished && qa_q3_item_read(call.game, actor, &item) && !item.hidden;
        okay = qa_modes_q3_source_object_available(call.modes, call.mode, actor, visible, error);
    } else if (okay) okay = adopt(&call, actor, &spec, error);
    return finish(&call, okay, error);
}

bool application_native_q3_objective_dropped(void *opaque, qa_actor_id actor,
    uint32_t index, qa_error *error)
{
    objective_call call;
    if (!primary(opaque, &call, error)) return false;
    qa_mode_object_spec spec;
    int32_t team;
    mode_q3_objective_services source;
    bool okay = item_spec(&call, actor, index, &spec, &team, error) &&
        adopt(&call, actor, &spec, error) && services(&call, &source, error) &&
        qa_modes_q3_source_item_dropped(call.modes, call.mode, actor, team, &source, error);
    return finish(&call, okay, error);
}

bool application_native_q3_objective_drop(void *opaque, qa_actor_id actor, qa_error *error)
{
    objective_call call;
    if (!primary(opaque, &call, error)) return false;
    uint32_t slot;
    qa_q3_player_state player;
    bool okay = actual_actor(&call, actor, &slot, error) && slot < 64 &&
        qa_q3_player_read(call.game, actor, &player);
    static const uint32_t tags[] = {9, 7, 8};
    for (size_t ordinal = 0; okay && ordinal < 3; ++ordinal) {
        uint32_t tag = tags[ordinal];
        if (!player.powerups[tag]) continue;
        size_t count;
        const qa_q3_item *items = qa_q3_items(product(&call), &count);
        uint32_t index = 1;
        while (index < count && (items[index].kind != QA_Q3_ITEM_TEAM || items[index].tag != (int32_t)tag)) ++index;
        qa_actor_id dropped;
        if (index == count) okay = application_fail(error, QA_ERROR_NOT_FOUND, "carried source flag has no item definition");
        else okay = qa_q3_source_drop_item(call.game, actor, index, 0, &dropped, error) &&
            actual_actor(&call, actor, &slot, error) &&
            qa_q3_client_flag_powerup(call.game, actor, tag, 0, error);
        break;
    }
    return finish(&call, okay, error);
}

bool application_native_q3_source_flags_cleared(void *opaque, qa_actor_id actor, qa_error *error)
{
    objective_call call;
    if (!primary(opaque, &call, error)) return false;
    mode_q3_objective_services source;
    uint32_t slot;
    bool okay = actual_actor(&call, actor, &slot, error) && slot < 64 &&
        services(&call, &source, error) &&
        qa_modes_q3_source_flags_cleared(call.modes, call.mode, actor, &source, error);
    return finish(&call, okay, error);
}

static bool expired(application_provider *provider, qa_actor_id actor,
    uint32_t index, bool no_drop, qa_error *error)
{
    objective_call call;
    if (!primary(provider, &call, error)) return false;
    const qa_q3_item *entry;
    int32_t team;
    mode_q3_objective_services source;
    bool okay = definition(&call, index, &entry, &team, error) && services(&call, &source, error) &&
        qa_modes_q3_source_item_expired(call.modes, call.mode, actor, team, no_drop, &source, error);
    return finish(&call, okay, error);
}

bool application_native_q3_objective_expired(void *opaque, qa_actor_id actor,
    uint32_t index, qa_error *error)
{
    return expired(opaque, actor, index, false, error);
}

bool application_native_q3_objective_nodrop(void *opaque, qa_actor_id actor,
    uint32_t index, qa_error *error)
{
    return expired(opaque, actor, index, true, error);
}

bool application_native_q3_return_flag(application_provider *provider, int32_t team, qa_error *error)
{
    objective_call call;
    if (!primary(provider, &call, error)) return false;
    mode_q3_objective_services source;
    bool okay = services(&call, &source, error) &&
        qa_modes_q3_source_return_flag(call.modes, call.mode, team, &source, error);
    return finish(&call, okay, error);
}

bool application_native_q3_objective_bound(void *opaque, qa_mode_id mode,
    qa_actor_id actor, bool *native_source, qa_error *error)
{
    qa_application *application = opaque;
    if (!application || !native_source)
        return application_fail(error, QA_ERROR_ARGUMENT, "TEAM source qualification needs its actual application");
    *native_source = false;
    application_provider *provider;
    if (!view_provider(application, mode, &provider, error)) return false;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(application->session), actor);
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !record) return true;
    uint32_t slot;
    qa_q3_source_binding row;
    if (!qa_q3_source_actor_slot(provider->state.q3, actor, &slot, error) ||
        slot < 64 || slot >= 1022 ||
        !qa_q3_source_binding_read(provider->state.q3, slot, &row, error) ||
        !row.in_use || !qa_actor_id_equal(row.actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "TEAM source object lost its full physical actor generation");
    qa_q3_obelisk_state obelisk;
    qa_error local = {0};
    if (qa_q3_source_obelisk_read(provider->state.q3, actor, &obelisk, &local)) {
        *native_source = true;
        return true;
    }
    if (local.code != QA_ERROR_NOT_FOUND) { if (error) *error = local; return false; }
    qa_q3_item_spawn spawn;
    bool finished;
    local = (qa_error){0};
    if (!qa_q3_source_item_spawn_read(provider->state.q3, actor, &spawn, &finished, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND) return true;
        if (error) *error = local;
        return false;
    }
    size_t count;
    qa_q3_product product = !strcmp(provider->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    const qa_q3_item *items = qa_q3_items(product, &count);
    if (!spawn.item_index || spawn.item_index >= count)
        return application_fail(error, QA_ERROR_ARGUMENT, "TEAM source object has an invalid true item index");
    *native_source = items[spawn.item_index].kind == QA_Q3_ITEM_TEAM;
    return true;
}

bool application_native_q3_objective_view(void *opaque, qa_mode_id mode,
    qa_actor_id actor, qa_mode_object_view *out, qa_error *error)
{
    qa_application *application = opaque;
    bool native_source;
    if (!out || out->mode.slot != mode.slot || out->mode.generation != mode.generation)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "TEAM object view lost its actual physical source owner");
    if (!application_native_q3_objective_bound(application, mode, actor, &native_source, error)) return false;
    if (!native_source)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "TEAM object view lost its actual physical source owner");
    application_provider *provider;
    if (!view_provider(application, mode, &provider, error)) return false;
    if (out->kind != QA_MODE_OBJECT_OBELISK) {
        qa_q3_item_spawn spawn;
        qa_q3_item_state item;
        bool finished;
        if (!qa_q3_source_item_spawn_read(provider->state.q3, actor, &spawn, &finished, error))
            return false;
        out->visible = finished && qa_q3_item_read(provider->state.q3, actor, &item) && !item.hidden;
        return true;
    }
    qa_q3_obelisk_state source;
    if (!qa_q3_source_obelisk_read(provider->state.q3, actor, &source, error)) return false;
    out->phase = source.think == QA_Q3_OBELISK_RESPAWN ? QA_OBJECTIVE_DESTROYED : QA_OBJECTIVE_HOME;
    out->frame = out->health_fraction = 0;
    out->visible = false;
    if (!source.model.registry) return true;
    uint32_t slot;
    qa_q3_entity model;
    qa_q3_wire_visibility visibility;
    if (!qa_q3_source_actor_slot(provider->state.q3, source.model, &slot, error) ||
        !qa_q3_wire_entity_read(provider->state.q3, slot, &model, &visibility, error)) return false;
    if (model.eType != 12 || model.modelindex != source.team)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Obelisk view lost its actual TEAM model identity");
    out->frame = model.frame;
    out->health_fraction = model.modelindex2;
    out->visible = visibility.present && visibility.linked &&
        !(visibility.server_flags & 1u) && !((uint32_t)model.eFlags & 0x80u);
    return true;
}

bool application_native_q3_objectives_reconnect(application_provider *provider, qa_error *error)
{
    qa_application *application = provider ? provider->application : NULL;
    if (!application || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !application->modes || !application->primary_mode_ready ||
        application_world_provider(application, QA_ROLE_ENTITIES, "") != provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "TEAM restore has no exact source and match owners");
    for (size_t ordinal = 0;; ++ordinal) {
        qa_actor_id actor;
        qa_actor_id base;
        qa_mode_object_spec spec;
        qa_mode_object_view object;
        qa_error local = {0};
        if (!qa_modes_q3_source_object_at(application->modes, application->primary_mode,
                ordinal, &actor, &spec, &object, &base, &local)) {
            if (local.code == QA_ERROR_NOT_FOUND) return true;
            if (error) *error = local;
            return false;
        }
        bool native_source;
        if (!application_native_q3_objective_bound(application, application->primary_mode,
                actor, &native_source, error) || !native_source)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "restored TEAM object lacks its actual physical source owner");
        if (object.kind != QA_MODE_OBJECT_FLAG && object.kind != QA_MODE_OBJECT_CUBE) continue;
        uint32_t slot;
        qa_q3_source_binding row;
        qa_q3_item_spawn item;
        bool finished;
        if (!qa_q3_source_actor_slot(provider->state.q3, actor, &slot, error) || slot < 64 || slot >= 1022 ||
            !qa_q3_source_binding_read(provider->state.q3, slot, &row, error) || !row.in_use ||
            !qa_actor_id_equal(row.actor, actor) ||
            !qa_q3_source_item_spawn_read(provider->state.q3, actor, &item, &finished, error))
            return application_fail(error, QA_ERROR_ARGUMENT, "restored TEAM object lacks its real source item row");
        if (base.registry) {
            qa_q3_item_spawn source_base;
            if (!qa_q3_source_actor_slot(provider->state.q3, base, &slot, error) ||
                slot < 64 || slot >= 1022 ||
                !qa_q3_source_item_spawn_read(provider->state.q3, base, &source_base, &finished, error) ||
                source_base.dropped || source_base.item_index != item.item_index)
                return application_fail(error, QA_ERROR_ARGUMENT, "restored TEAM drop lost its exact source base actor");
        }
    }
}
