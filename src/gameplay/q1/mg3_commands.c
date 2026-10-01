#include "internal.h"

static bool set_count(qa_q1_game *g, q1_player *player, qa_item_id item, double count,
                      double capacity, qa_error *error) {
    qa_inventory_entry entry;
    qa_error local = {0};
    if (!qa_inventory_entry_read(g->services.inventory, player->id, item, &entry, &local)) {
        if (local.code != QA_ERROR_NOT_FOUND) {
            if (error)
                *error = local;
            return false;
        }
        entry = (qa_inventory_entry){
            .item = item, .capacity = capacity, .policy = QA_COUNT_SOURCE_FLOAT};
    }
    entry.count = count;
    return qa_inventory_configure(g->services.inventory, player->id, &entry, NULL, NULL, error);
}
static bool selected_cheat(qa_q1_game *g, q1_player *player, qa_q1_cheat_grant grant, bool *handled,
                           qa_error *error) {
    *handled = false;
    return !g->host.cheat_arsenal ||
           g->host.cheat_arsenal(g->host.context, player->id, grant, handled, error);
}
static bool restock(qa_q1_game *g, q1_player *player, qa_error *error) {
    bool handled;
    if (!selected_cheat(g, player, QA_Q1_CHEAT_AMMO, &handled, error))
        return false;
    if (handled || !q1_alive(g, player->id))
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
    *handled = true;
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
        return selected || !q1_alive(g, player->id) ||
               qa_q1_player_select(g, player->id, QA_Q1_ROCKET, error);
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
