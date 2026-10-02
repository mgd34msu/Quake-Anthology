#include "internal.h"
#include "qa/game_q1_source_birth.h"
#include "qa/game_q1_maps.h"

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

static bool grant(qa_q1_game_operation *operation, qa_actor_id actor,
    const q1_player *player, uint32_t slot, qa_item_id item, double count,
    double capacity, qa_error *error)
{
    qa_inventory_entry entry = {.item = item, .count = count, .capacity = capacity,
        .policy = QA_COUNT_SOURCE_FLOAT};
    return source_current(operation, actor, player, slot, error) &&
        qa_inventory_configure(operation->game->services.inventory, actor, &entry,
            NULL, NULL, error) && source_current(operation, actor, player, slot, error);
}

static bool named_grant(qa_q1_game_operation *operation, qa_actor_id actor,
    const q1_player *player, uint32_t slot, const char *name, double count,
    qa_error *error)
{
    qa_item_id item;
    return qa_builtin_resource(&operation->game->services, name, &item, error) &&
        source_current(operation, actor, player, slot, error) &&
        grant(operation, actor, player, slot, item, count, 1, error);
}

bool qa_q1_source_ctf_spawn_arsenal(qa_q1_game *game, qa_actor_id actor,
    bool native_grapple_enabled, bool grapple_disabled, qa_error *error)
{
    if (!game || game->options.program != QA_Q1_CTF) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
            "ThreeWave birth requires its actual physical source program");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    uint32_t slot;
    q1_player *player = q1_player_get(game, actor);
    bool okay = false;
    if (!qa_q1_native_client_slot(game, actor, &slot, error) ||
        !source_current(&operation, actor, player, slot, error)) goto finish;
    const char *map = qa_strings_cstr(qa_session_strings(game->services.session),
        qa_q1_game_map_name(game));
    if (!map || !*map) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
            "ThreeWave birth lost its actual source map");
        goto finish;
    }
    bool start = !strcmp(map, "start");
    if (!qa_q1_player_powers_clear(game, actor, error) ||
        !source_current(&operation, actor, player, slot, error)) goto finish;
    static const char *const cleared[] = {"q1:key/silver", "q1:key/gold",
        "q1:ctf/rune/resistance", "q1:ctf/rune/strength",
        "q1:ctf/rune/haste", "q1:ctf/rune/regeneration"};
    for (size_t i = 0; i < sizeof(cleared) / sizeof(*cleared); ++i)
        if (!named_grant(&operation, actor, player, slot, cleared[i], 0, error)) goto finish;
    for (unsigned weapon = QA_Q1_AXE; weapon <= QA_Q1_LIGHTNING; ++weapon)
        if (!grant(&operation, actor, player, slot, game->weapons[weapon],
            weapon == QA_Q1_AXE || (weapon == QA_Q1_SHOTGUN && !start) ? 1 : 0,
            1, error)) goto finish;
    for (unsigned ammo = QA_Q1_SHELLS; ammo <= QA_Q1_CELLS; ++ammo)
        if (!grant(&operation, actor, player, slot, game->ammo[ammo],
            ammo == QA_Q1_SHELLS && !start ? 40 : 0, ammo == QA_Q1_NAILS ? 200 : 100,
            error)) goto finish;
    if (!grant(&operation, actor, player, slot, game->weapons[QA_Q1_CTF_GRAPPLE],
        native_grapple_enabled && !start && !grapple_disabled ? 1 : 0, 1, error)) goto finish;
    qa_armor armor = {0};
    if (!start) {
        qa_item_id item;
        if (!qa_builtin_resource(&game->services, "q1:item_armor1", &item, error) ||
            !source_current(&operation, actor, player, slot, error)) goto finish;
        armor.regular = (qa_regular_armor){.kind = QA_ARMOR_Q1, .item = item,
            .points = 50, .protection.q1_absorption = 0.3f};
    }
    bool selected;
    okay = qa_combat_set_armor(game->services.combat, actor, &armor, error) &&
        source_current(&operation, actor, player, slot, error) &&
        qa_q1_source_select_base_weapon(game, actor, start ? QA_Q1_AXE : QA_Q1_SHOTGUN,
            &selected, error) && source_current(&operation, actor, player, slot, error);
finish:
    qa_q1_game_operation_end(&operation);
    return okay;
}
