#include "internal.h"
#include "qa/console.h"
#include <ctype.h>

static bool named(const char *left, const char *right) {
    while (*left && *right)
        if (tolower((unsigned char)*left++) != tolower((unsigned char)*right++))
            return false;
    return *left == *right;
}
static bool print(qa_q3_game *game, qa_actor_id actor, const char *text, qa_error *error) {
    qa_string_id resource;
    if (!qa_builtin_resource(&game->options.services, text, &resource, error))
        return false;
    return !q3_actor_get(game, actor) ||
           qa_builtin_emit(&game->options.services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_MESSAGE,
                                               .family = QA_GAME_Q3,
                                               .provider = game->options.owner,
                                               .actor = actor, .text = resource,
                                               .time_ns = (uint64_t)(uint32_t)game->now_ms * 1000000},
                           error);
}
static void arguments(const qa_command_invocation *command, char *text, size_t capacity) {
    size_t used = 0;
    text[0] = 0;
    for (size_t i = 1; i < command->argc; ++i) {
        size_t length = strlen(command->argv[i]);
        if (length + used >= capacity - 1)
            break;
        memcpy(text + used, command->argv[i], length);
        used += length;
        if (i + 1 < command->argc)
            text[used++] = ' ';
        text[used] = 0;
    }
}
static bool count_write(qa_q3_game *game, qa_actor_id actor, qa_item_id item,
                         double count, double capacity, qa_error *error) {
    qa_inventory_entry entry;
    qa_error observed = {0};
    if (!qa_inventory_entry_read(game->options.services.inventory, actor, item, &entry, &observed)) {
        if (observed.code != QA_ERROR_NOT_FOUND) {
            if (error) *error = observed;
            return false;
        }
        entry = (qa_inventory_entry){.item = item, .capacity = capacity,
                                     .policy = QA_COUNT_SOURCE_INT32};
    }
    entry.count = count;
    return qa_inventory_configure(game->options.services.inventory, actor, &entry, NULL, NULL, error);
}
bool qa_q3_game_grant_arsenal(qa_q3_game *game, qa_actor_id actor, bool ammo, qa_error *error) {
    if (!game || game->source_restored || !q3_actor_get(game, actor) ||
        game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 arsenal grant requires a native actor");
    ++game->observation_depth;
    bool okay = true;
    int limit = game->options.product == QA_Q3_ARENA ? 11 : QA_Q3_WEAPON_COUNT;
    for (int weapon = 1; okay && weapon < limit && q3_actor_get(game, actor); ++weapon) {
        qa_item_id item = ammo ? game->ammo_items[weapon] : game->weapon_items[weapon];
        if (!item || (!ammo && weapon == QA_Q3_W_GRAPPLE))
            continue;
        okay = count_write(game, actor, item, ammo ? 999 : 1, ammo ? 200 : 1, error);
    }
    --game->observation_depth;
    return okay;
}
static bool give(qa_q3_game *game, qa_actor_id actor, const qa_command_invocation *command,
                  bool selected, qa_error *error) {
    char text[1024];
    arguments(command, text, sizeof(text));
    bool all = named(text, "all");
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (all || named(text, "health")) {
        if (!qa_combat_set_health(game->options.services.combat, actor,
                                  (float)entry->state.player.max_health, error))
            return false;
        if (!all || !q3_actor_get(game, actor))
            return true;
    }
    for (int category = 0; category < 2; ++category)
        if (all || named(text, category ? "ammo" : "weapons")) {
            bool handled = false;
            if (selected && game->options.hooks.grant_arsenal &&
                !game->options.hooks.grant_arsenal(game->options.hooks.context, actor,
                                                   category != 0, &handled, error))
                return false;
            if (!q3_actor_get(game, actor))
                return true;
            if (!handled && !qa_q3_game_grant_arsenal(game, actor, category != 0, error))
                return false;
            if (!all || !q3_actor_get(game, actor))
                return true;
        }
    if (all || named(text, "armor")) {
        uint32_t index = 0;
        qa_q3_find_item(game->options.product, "item_armor_body", &index);
        qa_regular_armor armor = {.kind = QA_ARMOR_Q3, .points = 200,
                                  .item = qa_q3_item_identity(game, index),
                                  .protection.q3_protection = .66f};
        if (!qa_combat_set_regular_armor(game->options.services.combat, actor, &armor, error))
            return false;
        if (!all || !q3_actor_get(game, actor))
            return true;
    }
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    int32_t *award = named(text, "excellent") ? &entry->state.player.excellent_count
                     : named(text, "impressive") ? &entry->state.player.impressive_count
                     : named(text, "gauntletaward") ? &entry->state.player.gauntlet_frag_count : NULL;
    if (award) {
        uint32_t bits = (uint32_t)*award + 1;
        memcpy(award, &bits, sizeof(bits));
        return true;
    }
    if (named(text, "defend") || named(text, "assist")) {
        if (!game->options.hooks.award)
            return q3_fail(error, "Q3 award requires the selected mode statistics owner");
        return game->options.hooks.award(game->options.hooks.context, actor,
                                         named(text, "defend") ? QA_Q3_AWARD_DEFEND : QA_Q3_AWARD_ASSIST,
                                         error);
    }
    if (all)
        return true;
    size_t count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    uint32_t index = 0;
    for (size_t i = 1; i < count; ++i)
        if (named(items[i].name, text) || named(items[i].classname, text)) {
            index = (uint32_t)i;
            break;
        }
    if (!index) {
        bool handled = false;
        return !selected || !game->options.hooks.give_item ||
               game->options.hooks.give_item(game->options.hooks.context, actor,
                                             command->argc - 1, command->argv + 1,
                                             &handled, error);
    }
    if (game->options.services.cvar) {
        char name[128];
        snprintf(name, sizeof(name), "disable_%s", items[index].classname);
        qa_string_id variable;
        float disabled = 0;
        if (!qa_builtin_resource(&game->options.services, name, &variable, error) ||
            !game->options.services.cvar(game->options.services.context, variable, &disabled, error))
            return false;
        if (disabled != 0 || !q3_actor_get(game, actor))
            return true;
    }
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    qa_actor_id temporary;
    if (!qa_q3_spawn_item(game, &(qa_q3_item_spawn){.item_index = index, .origin = body.origin},
                           &temporary, error))
        return false;
    bool accepted;
    bool okay = q3_item_touch(game, temporary, actor, true, &accepted, error);
    qa_error cleanup = {0};
    if (q3_actor_get(game, temporary) &&
        !qa_session_release(game->options.services.session, temporary, &cleanup)) {
        if (okay && error) *error = cleanup;
        return false;
    }
    return okay;
}
bool qa_q3_game_give_item(qa_q3_game *game, qa_actor_id actor, size_t count,
                          const char *const *args, bool *handled, qa_error *error) {
    if (!game || game->source_restored || !handled || (count && !args) ||
        count >= SIZE_MAX / sizeof(*args) ||
        game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 source item grant");
    *handled = false;
    for (size_t i = 0; i < count; ++i)
        if (!args[i])
            return q3_fail(error, "missing Q3 source item name");
    const char **argv = malloc((count + 1) * sizeof(*argv));
    if (!argv) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 source item arguments");
        return false;
    }
    argv[0] = "give";
    for (size_t i = 0; i < count; ++i)
        argv[i + 1] = args[i];
    qa_command_invocation command = {.argc = count + 1, .argv = argv};
    char text[1024];
    arguments(&command, text, sizeof(text));
    size_t item_count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &item_count);
    for (size_t i = 1; i < item_count; ++i)
        if (named(items[i].name, text) || named(items[i].classname, text)) {
            *handled = true;
            break;
        }
    ++game->observation_depth;
    bool okay = !*handled || give(game, actor, &command, false, error);
    --game->observation_depth;
    free(argv);
    return okay;
}
static bool dispatch(qa_q3_game *game, qa_actor_id actor, const qa_command_invocation *command,
                       bool *handled, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    const char *name = command->argv[0];
    if (named(name, "stats")) {
        /* Cmd_Stats_f has an empty body in the actual source. */
        *handled = true;
        return true;
    }
    if (named(name, "where")) {
        *handled = true;
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error)) return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        char text[64];
        int length = snprintf(text, sizeof(text), "(%i %i %i)",
            q3_source_float_to_int(body.origin.x), q3_source_float_to_int(body.origin.y),
            q3_source_float_to_int(body.origin.z));
        if (length >= 32) {
            char warning[80];
            snprintf(warning, sizeof(warning), "Com_sprintf: overflow of %i in 32\n", length);
            if (!game->options.hooks.console_print)
                return q3_fail(error, "Q3 vector overflow requires the source console print sink");
            if (!game->options.hooks.console_print(game->options.hooks.context, warning, error))
                return false;
            entry = q3_actor_get(game, actor);
            if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
            length = 31;
        }
        text[length] = '\n'; text[length + 1] = 0;
        return print(game, actor, text, error);
    }
    if (named(name, "setviewpos")) {
        *handled = true;
        if (!game->options.hooks.cheats_enabled ||
            !game->options.hooks.cheats_enabled(game->options.hooks.context))
            return print(game, actor, "Cheats are not enabled on this server.\n", error);
        if (!q3_actor_get(game, actor)) return true;
        if (command->argc != 5)
            return print(game, actor, "usage: setviewpos x y z yaw\n", error);
        float coordinates[4];
        for (size_t i = 0; i < 4; ++i) {
            const char *argument = command->argv[i + 1];
            coordinates[i] = q3_source_atof((qa_bytes){(const uint8_t *)argument, strlen(argument)});
        }
        return qa_q3_teleport(game, actor, qa_v3(coordinates[0], coordinates[1], coordinates[2]),
                              qa_v3(0, coordinates[3], 0), error);
    }
    if (named(name, "kill")) {
        *handled = true;
        qa_combat_state combat;
        if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
            return false;
        if (entry->state.player.spectator || combat.health <= 0 || !q3_actor_get(game, actor))
            return true;
        if (!game->options.hooks.suicide)
            return q3_fail(error, "Q3 suicide requires the selected direct death provider");
        return game->options.hooks.suicide(game->options.hooks.context, actor, error);
    }
    if (named(name, "use") || named(name, "drop")) {
        char text[1024];
        arguments(command, text, sizeof(text));
        size_t count;
        const qa_q3_item *items = qa_q3_items(game->options.product, &count);
        for (size_t i = 1; i < count; ++i)
            if (named(items[i].name, text) || named(items[i].classname, text)) {
                *handled = true;
                return qa_inventory_item_action(game->options.services.inventory, actor,
                                                  game->item_ids[i],
                                                  named(name, "use") ? QA_ITEM_USE : QA_ITEM_DROP,
                                                  error);
            }
        return true;
    }
    if (named(name, "useitem")) {
        *handled = true;
        return qa_q3_activate_holdable(game, actor, entry->state.player.holdable, false, error);
    }
    if (!named(name, "give") && !named(name, "god") && !named(name, "notarget") &&
        !named(name, "noclip"))
        return true;
    *handled = true;
    if (!game->options.hooks.cheats_enabled ||
        !game->options.hooks.cheats_enabled(game->options.hooks.context))
        return print(game, actor, "Cheats are not enabled on this server.\n", error);
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
        return false;
    if (combat.health <= 0)
        return print(game, actor, "You must be alive to use this command.\n", error);
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (named(name, "give"))
        return give(game, actor, command, true, error);
    bool enabled;
    if (named(name, "god")) {
        if (!qa_combat_read_traits(game->options.services.combat, actor, &combat, error))
            return false;
        if (!q3_actor_get(game, actor))
            return true;
        enabled = !combat.invulnerable;
        combat.invulnerable = enabled;
        if (!qa_combat_set_traits(game->options.services.combat, actor, &combat, error))
            return false;
    } else if (named(name, "notarget")) {
        if (!qa_q3_player_notarget(game, actor, &enabled, error))
            return false;
    } else {
        enabled = !entry->state.player.noclip;
        if (!game->options.hooks.console_motion)
            return q3_fail(error, "Q3 noclip requires the selected movement provider");
        if (!game->options.hooks.console_motion(game->options.hooks.context, actor, enabled, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER)
            return true;
        entry->state.player.noclip = enabled;
    }
    char text[64];
    snprintf(text, sizeof(text), "%s %s\n", named(name, "god") ? "godmode" : name,
             enabled ? "ON" : "OFF");
    return print(game, actor, text, error);
}
bool qa_q3_game_console_command(qa_q3_game *game, qa_actor_id actor,
                                 const qa_command_invocation *command, bool *handled,
                                 qa_error *error) {
    if (!game || game->source_restored || !command || !handled || !command->argc ||
        !command->argv || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 console invocation");
    *handled = false;
    for (size_t i = 0; i < command->argc; ++i)
        if (!command->argv[i])
            return q3_fail(error, "missing Q3 command argument");
    ++game->observation_depth;
    bool okay = dispatch(game, actor, command, handled, error);
    --game->observation_depth;
    return okay;
}
