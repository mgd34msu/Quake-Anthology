#include "internal.h"
#include "qa/game_q1_source_birth.h"

static bool source_current(qa_q1_game_operation *operation, qa_actor_id actor,
    const q1_player *player, uint32_t expected_slot, qa_error *error)
{
    uint32_t slot;
    if (qa_q1_game_operation_live(operation) &&
        q1_player_get(operation->game, actor) == player &&
        qa_q1_native_client_slot(operation->game, actor, &slot, error) &&
        slot == expected_slot) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
        "Q1 source weapon selection lost its actual physical client");
    return false;
}

bool qa_q1_source_select_base_weapon(qa_q1_game *game, qa_actor_id actor,
    qa_q1_weapon weapon, bool *selected, qa_error *error)
{
    if (!selected || weapon < QA_Q1_AXE || weapon > QA_Q1_LIGHTNING) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
            "Q1 source selection requires a foundation weapon and result");
        return false;
    }
    *selected = false;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    uint32_t slot;
    q1_player *player = q1_player_get(game, actor);
    bool okay = false;
    if (!qa_q1_native_client_slot(game, actor, &slot, error) ||
        !source_current(&operation, actor, player, slot, error)) goto finish;
    double owned;
    if (!qa_inventory_count_read(game->services.inventory, actor,
            game->weapons[weapon], &owned, error) ||
        !source_current(&operation, actor, player, slot, error)) goto finish;
    if (owned == 0) {
        okay = true;
        goto finish;
    }
    player->weapon = weapon;
    player->weapon_frame = 0;
    player->continuous = false;
    player->animation_at = -1;
    if (!q1_weapon_event(game, player, 0, 0, error) ||
        !source_current(&operation, actor, player, slot, error)) goto finish;
    *selected = true;
    okay = true;
finish:
    qa_q1_game_operation_end(&operation);
    return okay;
}
