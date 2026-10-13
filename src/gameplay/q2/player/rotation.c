#include "../entities/internal.h"
#include "qa/text.h"

bool qa_q2_players_rotation_contains(const qa_q2_game *game, qa_string_id map) {
    if (!game || !game->player_runtime || !map) return false;
    const qa_q2_player_rules *rules = &game->player_runtime->rules;
    for (size_t i = 0; i < rules->map_list_count; ++i)
        if (rules->map_list[i].map == map) return true;
    return false;
}

static bool random_rotation(qa_q2_game *game, const qa_q2_player_rules *rules,
                            qa_string_id *next, qa_error *error) {
    q2_player_list *players = q2_player_list_acquire(game, false, error);
    if (!players) return false;
    int32_t count = 0;
    for (size_t i = 0; i < players->count; ++i)
        count += players->rows[i].info.connected && !players->rows[i].info.spectator;
    q2_player_list_release(players);
    uint32_t eligible = 0;
    for (size_t i = 0; i < rules->map_list_count; ++i) {
        const qa_map_rotation_entry *entry = &rules->map_list[i];
        eligible += entry->minimum_players <= count && entry->maximum_players >= count;
    }
    if (!eligible) return true;
    uint32_t selected = q2_random_bounded(game, eligible);
    for (size_t i = 0; i < rules->map_list_count; ++i) {
        const qa_map_rotation_entry *entry = &rules->map_list[i];
        if (entry->minimum_players > count || entry->maximum_players < count) continue;
        if (!selected--) { *next = entry->map; break; }
    }
    return true;
}

bool qa_q2_players_end_deathmatch_level(qa_q2_game *game, qa_error *error) {
    if (!game || !game->player_runtime) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 deathmatch level end requires its player rules");
        return false;
    }
    q2_players *players = game->player_runtime;
    qa_strings *strings = qa_session_strings(game->services.session);
    const char *current = players->rules.map_name ? players->rules.map_name : "", *next = current;
    if (!(game->options.deathmatch_flags & 32)) {
        size_t count = players->rules.map_list_count, index = 0;
        qa_string_id random_map = QA_STRING_NONE;
        if (players->rules.map_list_random &&
            !random_rotation(game, &players->rules, &random_map, error)) return false;
        while (index < count &&
               !qa_text_ascii_equal(qa_strings_cstr(strings, players->rotation_maps[index].map), current))
            ++index;
        if (random_map) next = qa_strings_cstr(strings, random_map);
        else if (index < count) {
            if (game->options.rules == QA_RULESET_Q2_RERELEASE &&
                players->rules.map_list_shuffle && count > 1 && index == count - 1) {
                qa_map_rotation_entry *shuffled = malloc(count * sizeof(*shuffled));
                if (!shuffled) {
                    qa_error_set(error, QA_ERROR_MEMORY, 0, "Copying Q2 source rotation for shuffle");
                    return false;
                }
                memcpy(shuffled, players->rotation_maps, count * sizeof(*shuffled));
                for (size_t i = count - 1; i > 0; --i) {
                    size_t selected = q2_random_bounded(game, (uint32_t)(i + 1));
                    qa_map_rotation_entry swap = shuffled[i];
                    shuffled[i] = shuffled[selected];
                    shuffled[selected] = swap;
                }
                if (!strcmp(qa_strings_cstr(strings, shuffled[0].map), current)) {
                    qa_map_rotation_entry swap = shuffled[0];
                    shuffled[0] = shuffled[count - 1];
                    shuffled[count - 1] = swap;
                }
                qa_string_id first = shuffled[0].map;
                if (game->hooks.rotation_changed) {
                    bool published = game->hooks.rotation_changed(game->hooks.context, shuffled, count, error);
                    free(shuffled);
                    if (!published) return false;
                } else {
                    free(players->rotation_maps);
                    players->rotation_maps = shuffled;
                    players->rules.map_list = shuffled;
                }
                next = qa_strings_cstr(strings, first);
            } else if (game->options.rules != QA_RULESET_Q2_RERELEASE || count != 1)
                next = qa_strings_cstr(strings, players->rotation_maps[(index + 1) % count].map);
        } else if (count && players->rules.map_list_file)
            next = qa_strings_cstr(strings, players->rotation_maps[0].map);
        else {
            const char *configured = qa_strings_cstr(strings, players->rules.next_map);
            if (configured && *configured) next = configured;
            else {
                qa_actor_id changelevel;
                if (q2_map_find(game, game->runtime_names[Q2_NAME_TARGET_CHANGELEVEL], UINT32_MAX, 0, &changelevel)) {
                    q2_actor *source = q2_actor_get(game, changelevel, false, NULL);
                    const char *authored = source && source->entity ?
                        qa_strings_cstr(strings, source->entity->map) : NULL;
                    if (authored && *authored) next = authored;
                }
            }
        }
    }
    return qa_q2_players_intermission(game, next, NULL, 0, error);
}
