#include "internal.h"

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
bool q1_mg3_upgrade(qa_q1_game *g, q1_player *player, unsigned type, uint32_t flag, bool *collected,
                    float *maximum, qa_error *error) {
    uint32_t *bits = upgrade_bits(player, type);
    if (!bits || flag > 8388607u) {
        qa_error_set(error, QA_ERROR_ARGUMENT, type, "invalid MG3 upgrade source value");
        return false;
    }
    *collected = (*bits & flag) != 0;
    *maximum = player->max_health;
    if (!*collected) {
        *bits |= flag;
        if (type == 0) {
            *maximum = player->max_health += 10;
            float health = q1_health(g, player->id);
            if (health > 0 && health < *maximum &&
                !qa_combat_set_health(g->services.combat, player->id,
                                      fminf(*maximum, health + *maximum), error))
                return false;
        } else {
            qa_inventory_entry entry;
            qa_error local = {0};
            if (!qa_inventory_entry_read(g->services.inventory, player->id, g->ammo[type - 1],
                                         &entry, &local)) {
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
            *maximum = (float)entry.capacity + 10;
            entry.count = entry.capacity = *maximum;
            if (!qa_inventory_configure(g->services.inventory, player->id, &entry, NULL, NULL,
                                        error))
                return false;
        }
    }
    for (unsigned i = 0; i < 4; ++i) {
        qa_inventory_entry entry;
        if (!qa_inventory_entry_read(g->services.inventory, player->id, g->ammo[i], &entry, NULL) ||
            entry.count <= entry.capacity)
            continue;
        entry.count = entry.capacity;
        if (!qa_inventory_configure(g->services.inventory, player->id, &entry, NULL, NULL, error))
            return false;
    }
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
