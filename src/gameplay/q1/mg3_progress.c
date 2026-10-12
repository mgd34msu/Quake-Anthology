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
static bool upgrade_current(qa_q1_game *game, qa_actor_id actor,
    const q1_player *player, qa_error *error) {
    if (!game->destroy_pending && !game->continuation_pending && q1_alive(game, actor) &&
        q1_player_get(game, actor) == player) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "MG3 upgrade lost its actual player");
    return false;
}
typedef struct upgrade_inventory_read {
    qa_q1_game *game;
    qa_actor_id actor;
    q1_player *player;
} upgrade_inventory_read;
static bool upgrade_inventory_current(void *context, qa_error *error) {
    const upgrade_inventory_read *read = context;
    return upgrade_current(read->game, read->actor, read->player, error);
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
    qa_inventory *inventory = g->services.inventory;
    qa_arena storage;
    qa_inventory_entry *entries;
    size_t written;
    upgrade_inventory_read read = {.game = g, .actor = actor, .player = player};
    if (!qa_inventory_entries_snapshot(inventory, actor, &storage, &entries, &written,
                                      upgrade_inventory_current, &read, error))
        return false;
    bool okay = upgrade_current(g, actor, player, error);
    for (size_t i = 0; okay && i < written; ++i) {
        if (!(qa_strings_name_flags(qa_session_strings(g->services.session),
                                   entries[i].item) & QA_NAME_Q1_AMMO) ||
            entries[i].count <= entries[i].capacity) continue;
        entries[i].count = entries[i].capacity;
        okay = qa_inventory_configure(inventory, actor, &entries[i], NULL, NULL, error) &&
            upgrade_current(g, actor, player, error);
    }
    qa_inventory_snapshot_release(inventory, &storage);
    if (!okay) return false;
    if (!q1_alive(g, player->id)) return true;
    if (player->source_client) {
        bool selected;
        return q1_source_select_weapon(g, player->id, player->weapon, &selected, error);
    }
    return !player->arsenal || qa_q1_player_select(g, player->id, player->weapon, error);
}
static bool capacities_current(qa_q1_game_operation *operation, qa_actor_id actor,
    q1_player *player, bool source_client, uint32_t source_slot, qa_error *error) {
    uint32_t slot;
    if (qa_q1_game_operation_live(operation) && q1_player_get(operation->game, actor) == player &&
        (!source_client || (qa_q1_native_client_slot(operation->game, actor, &slot, error) &&
            slot == source_slot))) return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
        "MG3 capacity initialization retired its source player");
    return false;
}
bool q1_mg3_capacities(qa_q1_game *g, q1_player *player, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    qa_actor_id actor = player->id;
    bool source_client = player->source_client;
    uint32_t source_slot = player->client_slot;
    bool ok = capacities_current(&operation, actor, player, source_client, source_slot, error);
    if (!ok)
        goto finish;
    const qa_q1_mg3_progress *state = &player->mg3_progress;
    player->max_health = g->options.deathmatch ? 100 : capacity(50, state->health);
    qa_combat_state combat;
    ok = qa_combat_read(g->services.combat, actor, &combat, error) &&
        capacities_current(&operation, actor, player, source_client, source_slot, error);
    if (ok && combat.health > player->max_health)
        ok = qa_combat_set_health(g->services.combat, actor, player->max_health, error) &&
            capacities_current(&operation, actor, player, source_client, source_slot, error);
    if (!ok)
        goto finish;
    static const float base[] = {50, 100, 20, 100}, deathmatch[] = {100, 200, 100, 200};
    for (unsigned i = 0; ok && i < 4; ++i) {
        float maximum = g->options.deathmatch ? deathmatch[i] :
            capacity(base[i], *upgrade_bits(player, i + 1));
        double count;
        ok = qa_inventory_count_read(g->services.inventory, actor, g->ammo[i], &count, error) &&
            capacities_current(&operation, actor, player, source_client, source_slot, error);
        if (!ok)
            break;
        qa_inventory_entry entry = {.item = g->ammo[i],
                                    .capacity = maximum,
                                    .count = fmin(count, maximum),
                                    .policy = QA_COUNT_SOURCE_FLOAT};
        ok = qa_inventory_configure(g->services.inventory, actor, &entry, NULL, NULL, error) &&
            capacities_current(&operation, actor, player, source_client, source_slot, error);
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
