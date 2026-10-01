#include "internal.h"
#include "qa/game_q1_inventory.h"

static bool fail(qa_error *error, qa_actor_id actor, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "%s", message);
    return false;
}
static size_t definitions(const qa_q1_game *game, qa_item_definition *out) {
    size_t count = 0;
    for (unsigned ordinal = 0; ordinal < QA_Q1_WEAPON_COUNT; ++ordinal) {
        unsigned i = ordinal;
        if (game->options.program == QA_Q1_ROGUE && ordinal >= QA_Q1_LAVA_NAILGUN &&
            ordinal <= QA_Q1_ROGUE_GRAPPLE)
            i = ordinal == QA_Q1_LAVA_NAILGUN ? QA_Q1_ROGUE_GRAPPLE : ordinal - 1;
        qa_q1_weapon_profile identity;
        if (!qa_q1_weapon_profile_identity(game->options.program, (qa_q1_weapon)i, &identity))
            continue;
        int ammo = q1_weapon_declared_ammo((qa_q1_weapon)i);
        out[count++] = (qa_item_definition){.item = game->weapons[i],
            .ammo = ammo < 0 ? 0 : game->ammo[ammo], .owner = game->options.provider,
            .label = identity.label, .weapon = true, .actions = QA_ITEM_USE};
    }
    return count;
}
static bool invoke(void *context, qa_item_id item, qa_item_action action, qa_error *error) {
    q1_player *player = context;
    qa_q1_game *game = player->inventory_game;
    if (!game || action != QA_ITEM_USE || !player->arsenal ||
        q1_player_get(game, player->id) != player)
        return fail(error, player->id, "Q1 item action lost its selected arsenal player");
    qa_q1_weapon weapon = QA_Q1_WEAPON_COUNT;
    for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT; ++i) {
        qa_q1_weapon_profile profile;
        if (game->weapons[i] == item &&
            qa_q1_weapon_profile_identity(game->options.program, (qa_q1_weapon)i, &profile)) {
            weapon = (qa_q1_weapon)i;
            break;
        }
    }
    if (weapon == QA_Q1_WEAPON_COUNT)
        return fail(error, player->id, "Q1 item action has no source weapon declaration");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error))
        return false;
    bool result = q1_player_select_read(game, player->id, player, weapon, error);
    if (result && !qa_q1_game_operation_live(&operation))
        result = fail(error, player->id, "Q1 item action retired its source");
    qa_q1_game_operation_end(&operation);
    return result;
}
bool q1_inventory_bind(qa_q1_game *game, q1_player *player, qa_error *error) {
    if (!game || !player || !player->arsenal || q1_player_get(game, player->id) != player)
        return fail(error, player ? player->id : (qa_actor_id){0},
            "Q1 weapon declarations need their selected player");
    if (qa_inventory_lease_current(game->services.inventory, player->weapon_definitions))
        return player->inventory_game == game ||
            fail(error, player->id, "Q1 weapon declarations belong to another source");
    qa_item_definition items[QA_Q1_WEAPON_COUNT];
    size_t count = definitions(game, items);
    qa_inventory_entry entries[QA_Q1_WEAPON_COUNT];
    for (size_t i = 0; i < count; ++i)
        entries[i] = (qa_inventory_entry){.item = items[i].item, .capacity = 1,
                                          .policy = QA_COUNT_SOURCE_FLOAT};
    qa_inventory_admission *admission = NULL;
    if (!qa_inventory_prepare_entries(game->services.inventory, player->id,
                                      entries, count, &admission, error))
        return false;
    if (q1_player_get(game, player->id) != player) {
        qa_inventory_admission_abort(admission);
        return fail(error, player->id, "Q1 weapon storage admission retired its player");
    }
    if (!qa_inventory_admission_commit(admission, error)) {
        qa_inventory_admission_abort(admission);
        return false;
    }
    qa_inventory_lease lease = {0};
    if (!qa_inventory_bind_definitions(game->services.inventory, player->id,
        game->options.provider, items, count, invoke, player, &lease, error))
        return false;
    player->inventory_game = game;
    player->weapon_definitions = lease;
    return true;
}
void q1_inventory_close(qa_q1_game *game, q1_player *player) {
    if (player->weapon_definitions.serial)
        qa_inventory_close_items(game->services.inventory, player->weapon_definitions, NULL);
    player->weapon_definitions = (qa_inventory_lease){0};
    player->inventory_game = NULL;
}
bool qa_q1_game_weapon_definitions_current(qa_q1_game *game, qa_actor_id actor) {
    q1_player *player = game ? q1_player_get(game, actor) : NULL;
    return player && player->arsenal && player->inventory_game == game &&
        qa_actor_id_equal(player->weapon_definitions.actor, actor) &&
        qa_inventory_lease_current(game->services.inventory, player->weapon_definitions);
}
bool qa_q1_game_weapon_item_read(qa_q1_game *game, qa_actor_id actor, qa_item_id item,
    qa_q1_weapon_view *out, bool *found, qa_error *error) {
    if (!game || !out || !found)
        return fail(error, actor, "Q1 weapon item observation needs its source and output");
    *out = (qa_q1_weapon_view){0};
    *found = false;
    if (!qa_q1_game_weapon_definitions_current(game, actor))
        return true;
    for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT; ++i) {
        qa_q1_weapon_profile profile;
        if (game->weapons[i] == item &&
            qa_q1_weapon_profile_identity(game->options.program, (qa_q1_weapon)i, &profile)) {
            qa_inventory_lease lease = q1_player_get(game, actor)->weapon_definitions;
            qa_q1_game_operation operation = {0};
            if (!qa_q1_game_operation_begin(game, &operation, error))
                return false;
            bool result = qa_q1_player_weapon_read(game, actor, (qa_q1_weapon)i,
                                                  out, found, error);
            q1_player *player = q1_player_get(game, actor);
            if (result && (!qa_q1_game_operation_live(&operation) || !player ||
                player->weapon_definitions.serial != lease.serial ||
                !qa_q1_game_weapon_definitions_current(game, actor)))
                result = fail(error, actor, "Q1 weapon observation retired its declaration");
            if (!result) {
                *out = (qa_q1_weapon_view){0};
                *found = false;
            }
            qa_q1_game_operation_end(&operation);
            return result;
        }
    }
    return true;
}
bool qa_q1_game_weapon_item_available(qa_q1_game *game, qa_actor_id actor, qa_item_id item,
    bool *out, bool *found, qa_error *error) {
    if (!game || !out || !found)
        return fail(error, actor, "Q1 weapon availability needs its source and output");
    *out = false;
    *found = false;
    if (!qa_q1_game_weapon_definitions_current(game, actor))
        return true;
    for (unsigned i = 0; i < QA_Q1_WEAPON_COUNT; ++i) {
        qa_q1_weapon_profile profile;
        if (game->weapons[i] != item ||
            !qa_q1_weapon_profile_identity(game->options.program, (qa_q1_weapon)i, &profile))
            continue;
        qa_inventory_lease lease = q1_player_get(game, actor)->weapon_definitions;
        qa_q1_game_operation operation = {0};
        if (!qa_q1_game_operation_begin(game, &operation, error))
            return false;
        bool result = q1_weapon_ui_available_read(game, actor, (qa_q1_weapon)i, out, error);
        q1_player *player = q1_player_get(game, actor);
        if (result && (!qa_q1_game_operation_live(&operation) || !player ||
            player->weapon_definitions.serial != lease.serial ||
            !qa_q1_game_weapon_definitions_current(game, actor)))
            result = fail(error, actor, "Q1 weapon availability retired its declaration");
        if (result)
            *found = true;
        else
            *out = false;
        qa_q1_game_operation_end(&operation);
        return result;
    }
    return true;
}
bool qa_q1_game_inventory_group(qa_q1_game *game, qa_actor_id actor, uint64_t serial,
    const qa_inventory_source_group *saved, qa_inventory_items *out, qa_error *error) {
    q1_player *player = game ? q1_player_get(game, actor) : NULL;
    if (!player || !player->arsenal || !saved || !out || !serial ||
        game->observation_depth || !saved->definitions_only ||
        saved->owner != game->options.provider || (saved->count && !saved->items) ||
        !qa_actor_id_equal(player->weapon_definitions.actor, actor) ||
        player->weapon_definitions.serial != serial || player->inventory_game != game)
        return fail(error, actor, "Q1 saved weapon definitions have no captured source lease");
    qa_item_definition items[QA_Q1_WEAPON_COUNT];
    size_t count = definitions(game, items);
    if (saved->count != count)
        return fail(error, actor, "Q1 saved weapon definition count differs from its source");
    for (size_t i = 0; i < count; ++i) {
        const qa_item_definition *actual = items + i, *value = &saved->items[i].definition;
        if (saved->items[i].replace_primary || actual->item != value->item ||
            actual->ammo != value->ammo || actual->owner != value->owner ||
            actual->weapon != value->weapon || actual->actions != value->actions ||
            !value->label || strcmp(actual->label, value->label))
            return fail(error, actor, "Q1 saved weapon definitions differ from their source");
    }
    *out = (qa_inventory_items){.owner = game->options.provider,
        .items = saved->items, .count = count, .action_context = player, .invoke = invoke};
    return true;
}
