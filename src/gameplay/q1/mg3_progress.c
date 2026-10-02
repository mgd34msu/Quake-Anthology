#include "internal.h"
#include <stdlib.h>

static float capacity(float base, uint32_t bits) {
    for (; bits; bits &= bits - 1)
        base += 10;
    return base;
}
static uint32_t *upgrade_bits(q1_player *player, unsigned type) {
    switch (type) {
    case 0:
        return &player->mg3_progress.health;
    case 1:
        return &player->mg3_progress.shells;
    case 2:
        return &player->mg3_progress.nails;
    case 3:
        return &player->mg3_progress.rockets;
    case 4:
        return &player->mg3_progress.cells;
    default:
        return NULL;
    }
}
static bool upgrade_current(qa_q1_game *game, qa_actor_id actor,
    const q1_player *player, qa_error *error) {
    if (!game->destroy_pending && !game->continuation_pending && q1_alive(game, actor) &&
        q1_player_get(game, actor) == player) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "MG3 upgrade lost its actual player");
    return false;
}
bool q1_mg3_upgrade(qa_q1_game *g, q1_player *player, unsigned type, uint32_t flag, bool *collected,
                    float *maximum, qa_error *error) {
    qa_actor_id actor = player->id;
    if (!upgrade_current(g, actor, player, error)) return false;
    uint32_t *bits = upgrade_bits(player, type);
    if (!bits || flag > 8388607u) {
        qa_error_set(error, QA_ERROR_ARGUMENT, type, "invalid MG3 upgrade source value");
        return false;
    }
    *collected = (*bits & flag) != 0;
    *maximum = player->max_health;
    if (!*collected) {
        if (type == 0) {
            *bits |= flag;
            *maximum = player->max_health += 10;
            qa_combat_state combat;
            if (!qa_combat_read(g->services.combat, actor, &combat, error) ||
                !upgrade_current(g, actor, player, error)) return false;
            float health = combat.health;
            if (health > 0 && health < *maximum &&
                !qa_combat_set_health(g->services.combat, player->id,
                                      fminf(*maximum, health + *maximum), error))
                return false;
            if (!upgrade_current(g, actor, player, error)) return false;
        } else {
            qa_inventory_entry entry;
            qa_error local = {0};
            bool present = qa_inventory_entry_read(g->services.inventory, actor, g->ammo[type - 1],
                                         &entry, &local);
            if (!upgrade_current(g, actor, player, error)) return false;
            if (!present) {
                if (local.code != QA_ERROR_NOT_FOUND) {
                    if (error)
                        *error = local;
                    return false;
                }
                static const float base[] = {50, 100, 20, 100};
                entry = (qa_inventory_entry){.item = g->ammo[type - 1],
                                             .capacity = base[type - 1],
                                             .policy = QA_COUNT_SOURCE_FLOAT};
            }
            *bits |= flag;
            double updated = entry.capacity + 10;
            *maximum = (float)updated;
            entry.count = entry.capacity = updated;
            if (!qa_inventory_configure(g->services.inventory, player->id, &entry, NULL, NULL,
                                        error))
                return false;
            if (!upgrade_current(g, actor, player, error)) return false;
        }
    }
    size_t count = 0;
    if (!qa_inventory_entries(g->services.inventory, actor, NULL, 0, &count, error) ||
        !upgrade_current(g, actor, player, error)) return false;
    if (count > SIZE_MAX / sizeof(qa_inventory_entry)) {
        qa_error_set(error, QA_ERROR_MEMORY, actor.slot, "MG3 upgrade inventory extent overflows");
        return false;
    }
    qa_inventory_entry *entries = count ? malloc(count * sizeof(*entries)) : NULL;
    if (count && !entries) {
        qa_error_set(error, QA_ERROR_MEMORY, actor.slot, "Reading actual MG3 upgrade inventory");
        return false;
    }
    size_t written = 0;
    bool okay = qa_inventory_entries(g->services.inventory, actor, entries, count, &written, error) &&
        upgrade_current(g, actor, player, error);
    if (okay && written > count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "MG3 upgrade inventory changed its actual extent");
        okay = false;
    }
    for (size_t i = 0; okay && i < written; ++i) {
        qa_bytes name = qa_strings_text(qa_session_strings(g->services.session), entries[i].item);
        if (name.size < sizeof("q1:ammo/") - 1 ||
            memcmp(name.data, "q1:ammo/", sizeof("q1:ammo/") - 1) ||
            entries[i].count <= entries[i].capacity) continue;
        entries[i].count = entries[i].capacity;
        okay = qa_inventory_configure(g->services.inventory, actor, &entries[i], NULL, NULL, error) &&
            upgrade_current(g, actor, player, error);
    }
    free(entries);
    if (!okay) return false;
    if (!q1_alive(g, player->id)) return true;
    if (player->source_client) {
        bool selected;
        return q1_source_select_weapon(g, player->id, player->weapon, &selected, error);
    }
    return !player->arsenal || qa_q1_player_select(g, player->id, player->weapon, error);
}
static bool capacities_current(qa_q1_game_operation *operation, qa_actor_id actor,
    q1_player *player, qa_error *error) {
    if (qa_q1_game_operation_live(operation) && q1_player_get(operation->game, actor) == player)
        return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
        "MG3 capacity initialization retired its source player");
    return false;
}
bool q1_mg3_capacities(qa_q1_game *g, q1_player *player, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    qa_actor_id actor = player->id;
    bool ok = capacities_current(&operation, actor, player, error);
    if (!ok)
        goto finish;
    const qa_q1_mg3_progress *state = &player->mg3_progress;
    player->max_health = g->options.deathmatch ? 100 : capacity(50, state->health);
    qa_combat_state combat;
    ok = qa_combat_read(g->services.combat, actor, &combat, error) &&
        capacities_current(&operation, actor, player, error);
    if (ok && combat.health > player->max_health)
        ok = qa_combat_set_health(g->services.combat, actor, player->max_health, error) &&
            capacities_current(&operation, actor, player, error);
    if (!ok)
        goto finish;
    static const float base[] = {50, 100, 20, 100}, deathmatch[] = {100, 200, 100, 200};
    for (unsigned i = 0; ok && i < 4; ++i) {
        float maximum = g->options.deathmatch ? deathmatch[i] :
            capacity(base[i], *upgrade_bits(player, i + 1));
        double count;
        ok = qa_inventory_count_read(g->services.inventory, actor, g->ammo[i], &count, error) &&
            capacities_current(&operation, actor, player, error);
        if (!ok)
            break;
        qa_inventory_entry entry = {.item = g->ammo[i],
                                    .capacity = maximum,
                                    .count = fmin(count, maximum),
                                    .policy = QA_COUNT_SOURCE_FLOAT};
        ok = qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, error) &&
            capacities_current(&operation, actor, player, error);
    }
finish:
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool qa_q1_mg3_progress_read(const qa_q1_game *g, qa_actor_id actor, qa_q1_mg3_progress *out) {
    if (!g || !out || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return false;
    const q1_player *player = g->players[actor.slot];
    if (!player || !player->active || !qa_actor_id_equal(player->id, actor))
        return false;
    *out = player->mg3_progress;
    return true;
}
bool qa_q1_mg3_progress_restore(qa_q1_game *g, qa_actor_id actor, const qa_q1_mg3_progress *state,
                                qa_error *error) {
    if (!g || !state ||
        (state->health | state->shells | state->nails | state->rockets | state->cells |
         state->bloody) > 8388607u) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid MG3 travel progress");
        return false;
    }
    q1_player *player = q1_player_allocate(g, actor, error);
    if (!player)
        return false;
    player->mg3_progress = *state;
    return q1_mg3_capacities(g, player, error);
}
