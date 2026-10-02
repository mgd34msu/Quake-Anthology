#include "internal.h"

static bool current(qa_q1_game *game, qa_actor_id actor, const q1_player *player,
    qa_error *error) {
    if (!game->destroy_pending && !game->continuation_pending && q1_alive(game, actor) &&
        q1_player_get(game, actor) == player) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "MG3 command lost its actual source player");
    return false;
}
static bool item_count(qa_q1_game *game, q1_player *player, qa_item_id item,
    double *out, qa_error *error) {
    qa_actor_id actor = player->id;
    return current(game, actor, player, error) &&
        qa_inventory_count_read(game->services.inventory, actor, item, out, error) &&
        current(game, actor, player, error);
}
static bool enough(qa_q1_game *game, q1_player *player, qa_q1_weapon weapon,
    bool *out, qa_error *error) {
    int ammo = q1_weapon_declared_ammo(weapon);
    *out = true;
    if (ammo < 0) return true;
    double count;
    if (!item_count(game, player, game->ammo[ammo], &count, error)) return false;
    *out = count >= (weapon == QA_Q1_SUPER_SHOTGUN || weapon == QA_Q1_SUPER_NAILGUN ? 2 : 1);
    return true;
}
static bool select_weapon(qa_q1_game *game, q1_player *player, qa_q1_weapon weapon,
    bool *selected, qa_error *error) {
    qa_actor_id actor = player->id;
    if (!current(game, actor, player, error)) return false;
    if (player->source_client)
        return q1_source_select_weapon(game, actor, weapon, selected, error);
    double count;
    if (!item_count(game, player, game->weapons[weapon], &count, error)) return false;
    *selected = count != 0;
    return !*selected || (q1_player_select_read(game, actor, player, weapon, error) &&
        current(game, actor, player, error));
}
static bool melee(qa_q1_game *game, q1_player *player, qa_q1_weapon *out,
    qa_error *error) {
    double count;
    if (!item_count(game, player, game->weapons[QA_Q1_MG3_MJOLNIR], &count, error)) return false;
    *out = count != 0 ? QA_Q1_MG3_MJOLNIR : QA_Q1_AXE;
    return true;
}
static bool cycle(qa_q1_game *game, q1_player *player, bool reverse, qa_error *error) {
    qa_q1_weapon first;
    if (!melee(game, player, &first, error)) return false;
    const qa_q1_weapon order[] = {first, QA_Q1_SHOTGUN, QA_Q1_SUPER_SHOTGUN,
        QA_Q1_NAILGUN, QA_Q1_SUPER_NAILGUN, QA_Q1_GRENADE, QA_Q1_ROCKET,
        QA_Q1_LIGHTNING, QA_Q1_MG3_LASER};
    int index = -1;
    for (int i = 0; i < 9; ++i) if (order[i] == player->weapon) index = i;
    if (player->weapon == QA_Q1_AXE || player->weapon == QA_Q1_MG3_MJOLNIR) index = 0;
    if (index < 0) return true;
    for (int remaining = 10; remaining > 0; --remaining) {
        index = (index + (reverse ? -1 : 1) + 9) % 9;
        bool available, selected;
        if (!enough(game, player, order[index], &available, error)) return false;
        if (available) {
            if (!select_weapon(game, player, order[index], &selected, error)) return false;
            if (selected) return true;
        }
    }
    return true;
}
static bool set_count(qa_q1_game *g, q1_player *player, qa_item_id item, double count,
                      double capacity, qa_error *error) {
    qa_actor_id actor = player->id;
    if (!current(g, actor, player, error)) return false;
    qa_inventory_entry entry;
    qa_error local = {0};
    bool present = qa_inventory_entry_read(g->services.inventory, actor, item, &entry, &local);
    if (!current(g, actor, player, error)) return false;
    if (!present) {
        if (local.code != QA_ERROR_NOT_FOUND) {
            if (error)
                *error = local;
            return false;
        }
        entry = (qa_inventory_entry){
            .item = item, .capacity = capacity, .policy = QA_COUNT_SOURCE_FLOAT};
    }
    entry.count = count;
    return qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, error) &&
        current(g, actor, player, error);
}
static bool selected_cheat(qa_q1_game *g, q1_player *player, qa_q1_cheat_grant grant, bool *handled,
                           qa_error *error) {
    qa_actor_id actor = player->id;
    *handled = false;
    return current(g, actor, player, error) && (!g->host.cheat_arsenal ||
           g->host.cheat_arsenal(g->host.context, actor, grant, handled, error)) &&
        current(g, actor, player, error);
}
static bool restock(qa_q1_game *g, q1_player *player, qa_error *error) {
    bool handled;
    if (!selected_cheat(g, player, QA_Q1_CHEAT_AMMO, &handled, error))
        return false;
    if (handled)
        return true;
    static const double count[] = {100, 200, 100, 200}, capacity[] = {100, 200, 100, 100};
    for (unsigned i = 0; i < 4; ++i) {
        if (!set_count(g, player, g->ammo[i], count[i], capacity[i], error))
            return false;
        if (!q1_alive(g, player->id))
            return true;
    }
    return true;
}
static bool next_upgrade(qa_q1_game *g, q1_player *player, unsigned type, qa_error *error) {
    uint32_t flags[] = {player->mg3_progress.health, player->mg3_progress.shells,
                        player->mg3_progress.nails, player->mg3_progress.rockets,
                        player->mg3_progress.cells};
    for (uint32_t bit = 1; bit <= 16384; bit <<= 1) {
        if (flags[type] & bit)
            continue;
        bool collected;
        float maximum;
        return q1_mg3_upgrade(g, player, type, bit, &collected, &maximum, error) &&
               q1_message(g, player->id, "$mg3_qc_upgrade_success", error) &&
               q1_sound(g, player->id, type == 0 ? "player/tornoff2.wav" : "weapons/lock4.wav", 3,
                        1, error);
    }
    return true;
}
bool q1_mg3_impulse(qa_q1_game *g, q1_player *player, uint8_t impulse, bool *handled,
                    qa_error *error) {
    if (!player || !current(g, player->id, player, error)) return false;
    *handled = true;
    if (impulse == 1 || impulse == 225) {
        qa_q1_weapon weapon = QA_Q1_MG3_LASER;
        if (impulse == 1 && !melee(g, player, &weapon, error)) return false;
        double count;
        if (!item_count(g, player, g->weapons[weapon], &count, error)) return false;
        if (count == 0) return q1_message(g, player->id, "$qc_no_weapon", error);
        bool available, selected;
        if (!enough(g, player, weapon, &available, error)) return false;
        if (!available) return q1_message(g, player->id, "$qc_not_enough_ammo", error);
        return select_weapon(g, player, weapon, &selected, error);
    }
    if (impulse == 10 || impulse == 12) return cycle(g, player, impulse == 12, error);
    if (impulse == 9 || impulse == 99) {
        if (g->options.deathmatch || g->options.coop) {
            qa_string_id name;
            float enabled = 0;
            if (!qa_builtin_resource(&g->services, "sv_cheats", &name, error) ||
                (g->services.cvar && !g->services.cvar(q1_cvar_context(g), name, &enabled, error)))
                return false;
            if (!enabled)
                return true;
        }
        if (!restock(g, player, error))
            return false;
        if (!q1_alive(g, player->id))
            return true;
        bool selected;
        if (!selected_cheat(g, player, QA_Q1_CHEAT_WEAPONS, &selected, error))
            return false;
        if (!q1_alive(g, player->id))
            return true;
        if (!selected) {
            for (unsigned i = QA_Q1_AXE; i <= QA_Q1_LIGHTNING; ++i)
                if (!set_count(g, player, g->weapons[i], 1, 1, error))
                    return false;
            if (!set_count(g, player, g->weapons[QA_Q1_MG3_LASER], 1, 1, error))
                return false;
        }
        if (impulse != 99) {
            qa_string_id silver, gold;
            if (!qa_builtin_resource(&g->services, "q1:key/silver", &silver, error) ||
                !qa_builtin_resource(&g->services, "q1:key/gold", &gold, error) ||
                !set_count(g, player, silver, 1, 1, error) ||
                !set_count(g, player, gold, 1, 1, error))
                return false;
        }
        if (selected || !q1_alive(g, player->id)) return true;
        if (player->source_client) {
            bool source_selected;
            return q1_source_select_weapon(g, player->id, QA_Q1_ROCKET, &source_selected, error);
        }
        return qa_q1_player_select(g, player->id, QA_Q1_ROCKET, error);
    }
    if (impulse == 100) {
        player->max_health = 100;
        if (!q1_message(g, player->id, "Resetting to defaults\n", error) ||
            !qa_combat_set_health(g->services.combat, player->id, 100, error))
            return false;
        static const float capacity[] = {100, 200, 100, 100};
        for (unsigned i = 0; i < 4; ++i) {
            qa_inventory_entry entry = {.item = g->ammo[i],
                                        .count = q1_ammo_count(g, player->id, (qa_q1_ammo)i),
                                        .capacity = capacity[i],
                                        .policy = QA_COUNT_SOURCE_FLOAT};
            if (!qa_inventory_configure(g->services.inventory, player->id, &entry, NULL, NULL,
                                        error))
                return false;
        }
        return true;
    }
    if (impulse >= 111 && impulse <= 115)
        return next_upgrade(g, player, impulse - 111u, error);
    if (impulse == 118)
        return set_count(g, player, g->weapons[QA_Q1_MG3_MJOLNIR], 1, 1, error);
    if (impulse == 122) {
        if (!q1_message(g, player->id, "$m_inf_ammo", error))
            return false;
        player->mg3_infinite_ammo = !player->mg3_infinite_ammo;
        return !player->mg3_infinite_ammo || restock(g, player, error);
    }
    if (impulse == 227 || impulse == 228) {
        uint32_t bit = impulse == 227 ? 1u : 2u;
        player->mg3_progress.bloody ^= bit;
        return q1_message(g, player->id,
                          player->mg3_progress.bloody & bit
                              ? (impulse == 227 ? "activated 'bloody shotgun' upgrade\n"
                                                : "activated 'bloody Super Shotgun' upgrade\n")
                              : (impulse == 227 ? "deactivated 'bloody shotgun' upgrade\n"
                                                : "deactivated 'bloody Super Shotgun' upgrade\n"),
                          error);
    }
    *handled = false;
    return true;
}
