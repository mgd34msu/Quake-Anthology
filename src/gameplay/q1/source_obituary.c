#include "maps/internal.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q1_source_obituary.h"

static double remaining(const q1_player *player, qa_q1_power power, double seconds)
{
    double value = (player && player->power_order[power] ? player->power_expires[power] : 0)
        - seconds;
    return value <= 0 ? 0 : value;
}

bool qa_q1_source_obituary_read(const qa_q1_game *game, qa_actor_id actor,
    qa_q1_source_obituary_actor *out, qa_error *error)
{
    if (!game || !out || game->destroy_pending || game->continuation_pending || !game->wire) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
            "Source obituary requires its live physical native GAME");
        return false;
    }
    const qa_actor_record *record = qa_actors_get(qa_session_actors(game->services.session), actor);
    if (!record) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot, "Source obituary actor retired");
        return false;
    }
    const q1_actor *entity = q1_entity_const(game, actor);
    if (entity && (!entity->native || record->owner != game->options.provider)) entity = NULL;
    if (entity && !record->has_source) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
            "Source obituary entity lost its physical source ordinal");
        return false;
    }
    const q1_player *player = qa_q1_player_source_present(game, actor)
        ? game->players[actor.slot] : NULL;
    qa_q1_source_client_view client;
    bool has_client = player && player->source_client;
    if (has_client) {
        uint32_t slot;
        if (!qa_q1_native_client_slot(game, actor, &slot, error) ||
            slot != player->client_slot || !qa_q1_source_client_read(game, actor, &client)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                "Source obituary client lost its actual physical binding");
            return false;
        }
    }
    qa_strings *strings = qa_session_strings(game->services.session);
    qa_q1_source_obituary_actor result = {
        .owner = entity ? entity->owner : (qa_actor_id){0},
        .classname = entity ? entity->classname : 0,
        .kill_string = entity ? entity->source_kill_string : 0,
        .death_type = entity ? entity->source_death_type : 0,
        .name = has_client ? client.name : entity ? qa_strings_cstr(strings,
            entity->map ? entity->map->netname : entity->source_netname) : NULL,
        .team = has_client ? client.team : 0,
        .quad_remaining = remaining(player, QA_Q1_QUAD, game->time),
        .invulnerable_remaining = remaining(player, QA_Q1_INVULNERABILITY, game->time),
        .weapon = player ? player->weapon : QA_Q1_WEAPON_COUNT,
        .movement = entity ? entity->physics.motion : QA_PHYSICS_STATIONARY,
        .water_level = entity ? entity->physics.water_level : 0,
        .entity = entity != NULL, .client = has_client,
        .monster = entity && (entity->physics.flags & QA_PHYSICS_MONSTER),
        .brush = entity && entity->physics.solid == QA_PHYSICS_BRUSH,
        .horde_source_die = entity && entity->kind == Q1_MONSTER && entity->state.monster.horde
    };
    if (entity && !has_client && !q1_source_number_read(
        qa_strings_text(strings, entity->source_team), &result.team, error)) return false;
    *out = result;
    return true;
}
