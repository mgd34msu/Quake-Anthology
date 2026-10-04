#include "internal.h"
#include "qa/game_q1_source_travel.h"

static bool source_current(qa_q1_game_operation *operation, qa_actor_id actor,
    q1_player *player, qa_error *error) {
    if (qa_q1_game_operation_live(operation) && q1_player_get(operation->game, actor) == player &&
        player && player->source_client) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
        "Q1 source inventory lost its actual physical client");
    return false;
}
static bool configure(qa_q1_game_operation *operation, qa_actor_id actor,
    q1_player *player, qa_item_id item, double count, double capacity, bool preserve,
    qa_error *error) {
    if (!source_current(operation, actor, player, error)) return false;
    qa_inventory *inventory = operation->game->services.inventory;
    if (preserve && (!qa_inventory_count_read(inventory, actor, item, &count, error) ||
        !source_current(operation, actor, player, error))) return false;
    qa_inventory_entry entry = {.item = item, .count = count, .capacity = capacity,
        .policy = QA_COUNT_SOURCE_FLOAT};
    return qa_inventory_configure(inventory, actor, &entry, NULL, NULL, error) &&
        source_current(operation, actor, player, error);
}
bool qa_q1_source_inventory_initialize(qa_q1_game *game, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    q1_player *player = q1_player_get(game, actor);
    uint32_t slot;
    bool ok = qa_q1_native_client_slot(game, actor, &slot, error) &&
        source_current(&operation, actor, player, error);
    if (ok && !qa_inventory_has(game->services.inventory, actor))
        ok = qa_inventory_create_actor(game->services.inventory, actor, NULL, 0, error) &&
            source_current(&operation, actor, player, error);
    for (int weapon = QA_Q1_AXE; ok && weapon <= QA_Q1_LIGHTNING; ++weapon)
        ok = configure(&operation, actor, player, game->weapons[weapon],
            weapon == QA_Q1_AXE || weapon == QA_Q1_SHOTGUN ? 1 : 0, 1, false, error);
    for (int ammo = QA_Q1_SHELLS; ok && ammo <= QA_Q1_CELLS; ++ammo)
        ok = configure(&operation, actor, player, game->ammo[ammo],
            ammo == QA_Q1_SHELLS ? 25 : 0, ammo == QA_Q1_NAILS ? 200 : 100, false, error);
    const char *keys[] = {"q1:key/silver", "q1:key/gold"};
    for (size_t i = 0; ok && i < sizeof(keys) / sizeof(*keys); ++i) {
        qa_item_id item;
        ok = qa_builtin_resource(&game->services, keys[i], &item, error) &&
            source_current(&operation, actor, player, error) &&
            configure(&operation, actor, player, item, 0, 1, false, error);
    }
    int first = game->options.program == QA_Q1_HIPNOTIC ? QA_Q1_LASER : QA_Q1_LAVA_NAILGUN;
    int last = game->options.program == QA_Q1_HIPNOTIC ? QA_Q1_PROXIMITY : QA_Q1_PLASMA;
    if (game->options.program == QA_Q1_HIPNOTIC || game->options.program == QA_Q1_ROGUE)
        for (int weapon = first; ok && weapon <= last; ++weapon)
            ok = configure(&operation, actor, player, game->weapons[weapon], 0, 1, true, error);
    if (game->options.program == QA_Q1_ROGUE) {
        for (int ammo = QA_Q1_LAVA_NAILS; ok && ammo <= QA_Q1_PLASMA_CELLS; ++ammo)
            ok = configure(&operation, actor, player, game->ammo[ammo], 0,
                ammo == QA_Q1_LAVA_NAILS ? 200 : 100, true, error);
        if (ok) ok = configure(&operation, actor, player, game->vengeance_item, 0, 1, true, error);
        if (ok) ok = configure(&operation, actor, player, game->weapons[QA_Q1_ROGUE_GRAPPLE],
            game->options.deathmatch && game->options.teamplay >= 4 ? 1 : 0, 1, false, error);
    }
    if (ok && game->options.program == QA_Q1_MG3)
        ok = source_current(&operation, actor, player, error) &&
            q1_mg3_capacities(game, player, error) &&
            source_current(&operation, actor, player, error);
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool qa_q1_source_spawn_teledeath(qa_q1_game *game, qa_actor_id actor,
    qa_actor_id *out, qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
            "Q1 spawn overlap requires its actual teledeath output");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    q1_player *player = q1_player_get(game, actor);
    uint32_t slot;
    qa_body_state body;
    bool ok = qa_q1_native_client_slot(game, actor, &slot, error) &&
        source_current(&operation, actor, player, error) &&
        qa_world_body_read(game->services.world, actor, &body, error) &&
        source_current(&operation, actor, player, error);
    qa_actor_id death = {0};
    if (ok) ok = q1_spawn_teledeath(game, body.origin, actor, 0.2, false, &death, error) &&
        source_current(&operation, actor, player, error);
    if (ok) *out = death;
    qa_q1_game_operation_end(&operation);
    return ok;
}
