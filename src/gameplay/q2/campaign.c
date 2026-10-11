#include "entities/internal.h"

static qa_q2_campaign_level *level(q2_entities *state, qa_string_id map) {
    for (uint32_t i = 0; i < state->level_count; ++i)
        if (state->levels[i].map == map) return state->levels + i;
    return NULL;
}
static bool resource(qa_q2_game *game, const char *text, qa_string_id *out, qa_error *error) {
    return qa_builtin_resource(&game->services, text, out, error);
}
static bool add_level(qa_q2_game *game, qa_string_id map,
                       qa_q2_campaign_level **out, qa_error *error) {
    q2_entities *state = game->entity_runtime;
    *out = level(state, map);
    if (*out) return true;
    if (state->level_count == QA_Q2_CAMPAIGN_LEVEL_LIMIT)
        return q2_player_print(game, (qa_actor_id){0}, 0,
            "More than 8 maps in unit; cannot track remaining levels", error);
    *out = state->levels + state->level_count++;
    **out = (qa_q2_campaign_level){.map = map};
    return true;
}
static bool visit(q2_entities *state, qa_string_id map, qa_error *error) {
    for (size_t i = 0; i < state->visited_count; ++i)
        if (state->visited_maps[i] == map) return true;
    if (state->visited_count == SIZE_MAX / sizeof(*state->visited_maps)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Q2 campaign visited-map roster overflows");
        return false;
    }
    if (state->visited_count == state->visited_capacity) {
        size_t capacity = state->visited_count + 1;
        qa_string_id *maps = realloc(state->visited_maps, capacity * sizeof(*maps));
        if (!maps) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining Q2 campaign visited maps");
            return false;
        }
        state->visited_maps = maps; state->visited_capacity = capacity;
    }
    state->visited_maps[state->visited_count++] = map;
    return true;
}
bool q2_campaign_enter(qa_q2_game *game, qa_error *error) {
    if (game->options.edition != QA_Q2_RERELEASE || game->options.deathmatch) return true;
    q2_actor *world = NULL;
    for (q2_actor *actor = game->first_actor; actor; actor = actor->live_next)
        if (actor->entity && actor->entity->kind == Q2E_WORLD) { world = actor; break; }
    if (world && q2_field_float(game, world->entity, game->field_keys[QA_TARGET_KEY_HUB_MAP], 0) != 0) return true;
    const char *name = game->player_runtime->rules.map_name;
    if (!name || !*name) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 campaign entry lacks its actual map name");
        return false;
    }
    qa_string_id map;
    qa_q2_campaign_level *entry;
    if (!resource(game, name, &map, error) || !add_level(game, map, &entry, error)) return false;
    if (!entry) return true;
    qa_strings *strings = qa_session_strings(game->services.session);
    const char *label = entry->name ? qa_strings_cstr(strings, entry->name) : "";
    if (!label || !*label) {
        qa_string_id message = world ? world->entity->message : 0;
        const char *text = message ? qa_strings_cstr(strings, message) : "";
        uint32_t maximum = 0;
        for (uint32_t i = 0; i < game->entity_runtime->level_count; ++i)
            if (game->entity_runtime->levels[i].visit_order > maximum)
                maximum = game->entity_runtime->levels[i].visit_order;
        if (maximum == UINT32_MAX) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 campaign visit order overflows");
            return false;
        }
        if (!visit(game->entity_runtime, map, error)) return false;
        entry->name = text && *text ? message : map;
        entry->visit_order = maximum + 1;
        if (game->player_runtime->rules.coop_lives)
            for (q2_actor *actor = game->first_actor; actor; actor = actor->live_next)
                if (actor->client && actor->client->info.lives < game->player_runtime->rules.coop_num_lives + 1)
                    ++actor->client->info.lives;
    }
    for (q2_actor *actor = game->first_actor; actor; actor = actor->live_next) {
        if (!actor->entity || actor->entity->kind != Q2E_CHANGELEVEL || !actor->entity->map) continue;
        const char *target = qa_strings_cstr(strings, actor->entity->map);
        if (!target || !*target || strchr(target, '*')) continue;
        const char *destination = strchr(target, '+'); destination = destination ? destination + 1 : target;
        if (strstr(destination, ".cin") || strstr(destination, ".pcx")) continue;
        size_t bytes = strcspn(destination, "$");
        if (!bytes) continue;
        char *copy = malloc(bytes + 1);
        if (!copy) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Reading Q2 campaign exit map"); return false; }
        memcpy(copy, destination, bytes); copy[bytes] = 0;
        bool okay = resource(game, copy, &map, error); free(copy);
        if (!okay || !add_level(game, map, &entry, error)) return false;
        if (!entry) return true;
    }
    return true;
}
void q2_campaign_update(qa_q2_game *game) {
    if (game->options.edition != QA_Q2_RERELEASE) return;
    const char *map = game->player_runtime->rules.map_name;
    qa_strings *strings = qa_session_strings(game->services.session);
    q2_entities *state = game->entity_runtime;
    for (uint32_t i = 0; i < state->level_count; ++i) {
        qa_q2_campaign_level *entry = state->levels + i;
        const char *name = qa_strings_cstr(strings, entry->map);
        if (map && name && !strcmp(map, name)) {
            entry->total_secrets = (uint32_t)state->total_secrets;
            entry->found_secrets = (uint32_t)state->found_secrets;
            entry->total_monsters = state->total_monsters;
            entry->killed_monsters = state->killed_monsters;
            return;
        }
    }
}
bool q2_campaign_frame(qa_q2_game *game, qa_error *error) {
    if (game->options.edition != QA_Q2_RERELEASE || game->player_runtime->intermission) return true;
    bool connected = false;
    for (q2_actor *actor = game->first_actor; actor; actor = actor->live_next)
        if (actor->client && actor->client->info.slot == 0) { connected = actor->client->info.connected; break; }
    if (!connected) return true;
    const char *map = game->player_runtime->rules.map_name;
    qa_strings *strings = qa_session_strings(game->services.session);
    for (uint32_t i = 0; i < game->entity_runtime->level_count; ++i) {
        qa_q2_campaign_level *entry = game->entity_runtime->levels + i;
        const char *name = qa_strings_cstr(strings, entry->map);
        if (!map || !name || strcmp(map, name)) continue;
        double elapsed = entry->time_seconds + (double)game->frame_ns / 1e9;
        if (!isfinite(elapsed)) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 campaign time overflows"); return false; }
        entry->time_seconds = elapsed; break;
    }
    return true;
}
void q2_campaign_leave(qa_q2_game *game) {
    if (game->options.edition != QA_Q2_RERELEASE) return;
    memset(game->entity_runtime->levels, 0, sizeof(game->entity_runtime->levels));
    game->entity_runtime->level_count = 0;
    if (game->player_runtime->rules.coop_lives)
        for (q2_actor *actor = game->first_actor; actor; actor = actor->live_next)
            if (actor->client) actor->client->info.lives = game->player_runtime->rules.coop_num_lives + 1;
}
bool qa_q2_campaign_leave_unit(qa_q2_game *game, qa_error *error) {
    if (!game || !qa_session_safe(game->services.session) || !q2_checkpoint_idle(game, error)) {
        if (!game || !qa_session_safe(game->services.session))
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 unit travel requires its safe actual campaign owner");
        return false;
    }
    q2_campaign_leave(game);
    return true;
}
static uint32_t order(const qa_q2_campaign_level *entry) {
    return entry->visit_order ? entry->visit_order : entry->name ? 9u : 10u;
}
bool q2_campaign_end_unit(qa_q2_game *game, qa_actor_id actor, uint64_t button,
                          qa_error *error) {
    q2_campaign_update(game);
    qa_q2_campaign_level report[QA_Q2_CAMPAIGN_LEVEL_LIMIT];
    size_t count = game->entity_runtime->level_count;
    memcpy(report, game->entity_runtime->levels, count * sizeof(*report));
    for (size_t i = 1; i < count; ++i) {
        qa_q2_campaign_level entry = report[i]; size_t j = i;
        while (j && order(report + j - 1) > order(&entry)) { report[j] = report[j - 1]; --j; }
        report[j] = entry;
    }
    return q2_map_event(game, &(qa_q2_map_event){.kind = QA_Q2_MAP_END_UNIT,
        .actor = actor, .levels = report, .level_count = count, .button_time_ns = button}, error);
}
bool q2_campaign_monster_count(qa_q2_game *game, qa_q2_monster_count kind, qa_error *error) {
    uint32_t *counter = kind == QA_Q2_MONSTER_COUNT_TOTAL ? &game->entity_runtime->total_monsters :
        kind == QA_Q2_MONSTER_COUNT_KILLED ? &game->entity_runtime->killed_monsters : NULL;
    if (!counter || *counter == UINT32_MAX) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 campaign monster counter is invalid"); return false;
    }
    ++*counter; return true;
}
bool qa_q2_monster_pick_target(qa_q2_game *game, qa_string_id name, qa_actor_id *out) {
    return q2_entity_pick(game, name, out);
}
bool qa_q2_campaign_monster_count(qa_q2_game *game, qa_q2_monster_count kind, qa_error *error) {
    return q2_campaign_monster_count(game, kind, error);
}
bool q2_campaign_saved_valid(qa_q2_game *game, const qa_q2_entities_checkpoint *saved, qa_error *error) {
    if (saved->level_count > QA_Q2_CAMPAIGN_LEVEL_LIMIT ||
        saved->total_secrets < 0 || saved->found_secrets < 0 ||
        (saved->visited_count && !saved->visited_maps) || saved->visited_count > SIZE_MAX / sizeof(qa_string_id))
        goto invalid;
    qa_strings *strings = qa_session_strings(game->services.session);
    for (uint32_t i = 0; i < saved->level_count; ++i) {
        const qa_q2_campaign_level *entry = saved->levels + i;
        const char *map = qa_strings_cstr(strings, entry->map);
        const char *name = entry->name ? qa_strings_cstr(strings, entry->name) : "";
        if (!map || !*map || !name || entry->visit_order > saved->level_count ||
            !isfinite(entry->time_seconds) || entry->time_seconds < 0 ||
            (entry->visit_order != 0) != (*name != 0)) goto invalid;
        for (uint32_t j = 0; j < i; ++j)
            if (saved->levels[j].map == entry->map || (entry->visit_order &&
                saved->levels[j].visit_order == entry->visit_order)) goto invalid;
        if (entry->visit_order) {
            bool visited = false;
            for (size_t j = 0; j < saved->visited_count; ++j)
                visited |= saved->visited_maps[j] == entry->map;
            if (!visited) goto invalid;
        }
    }
    for (size_t i = 0; i < saved->visited_count; ++i) {
        const char *map = qa_strings_cstr(strings, saved->visited_maps[i]);
        if (!map || !*map) goto invalid;
        for (size_t j = 0; j < i; ++j) if (saved->visited_maps[j] == saved->visited_maps[i]) goto invalid;
    }
    return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid Q2 campaign level ledger"); return false;
}
