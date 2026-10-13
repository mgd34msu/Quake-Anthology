#include "player/internal.h"
#include "qa/game_q2_source.h"
#include "qa/text.h"

static const char *setting_name(const qa_q2_game *game, qa_q2_source_setting setting) {
    static const char *const names[QA_Q2_SOURCE_SETTING_COUNT] = {
        [QA_Q2_SOURCE_GRAVITY] = "sv_gravity",
        [QA_Q2_SOURCE_SELECT_EMPTY] = "g_select_empty",
        [QA_Q2_SOURCE_FAST_SWITCH] = "fastswitch",
        [QA_Q2_SOURCE_STRONG_MINES] = "strong_mines",
        [QA_Q2_SOURCE_TEAMPLAY] = "teamplay",
        [QA_Q2_SOURCE_INSTAGIB] = "g_instagib",
        [QA_Q2_SOURCE_CTF_FLAGS] = "ctfflags",
        [QA_Q2_SOURCE_DISABLED_WEAPONS] = "disabled_weps",
        [QA_Q2_SOURCE_NO_QUADFIRE_DROP] = "g_dm_no_quadfire_drop",
    };
    return setting == QA_Q2_SOURCE_STRONG_MINES && game->options.rules == QA_RULESET_Q2_RERELEASE ?
        "g_dm_strong_mines" : names[setting];
}

void qa_q2_source_bind(qa_q2_game *game, const qa_cvars *cvars) {
    if (game->source_cvars != cvars) {
        for (unsigned i = 0; i < QA_Q2_SOURCE_SETTING_COUNT; ++i)
            game->source_settings[i] = qa_cvars_resolve(cvars,
                setting_name(game, (qa_q2_source_setting)i));
        game->source_cvars = cvars;
    }
}

bool qa_q2_source_value(qa_q2_game *game, qa_q2_source_setting setting, float default_value,
                        float *out, qa_error *error) {
    if (!game || (unsigned)setting >= QA_Q2_SOURCE_SETTING_COUNT || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 source value requires its GAME owner");
        return false;
    }
    *out = default_value;
    if (!game->services.cvar) return true;
    if (game->source_cvars) {
        const qa_cvar_view *value = qa_cvars_read(game->source_cvars, game->source_settings[setting]);
        if (value && value->owner && value->owner != game->options.owner) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 cvar belongs to another source");
            return false;
        }
        *out = value ? value->number : 0;
        return true;
    }
    qa_string_id id;
    return qa_builtin_resource(&game->services, setting_name(game, setting), &id, error) &&
        game->services.cvar(game->services.cvar_context ? game->services.cvar_context :
            game->services.context, id, out, error);
}

static float number(const qa_cvars *cvars, const char *name) {
    const qa_cvar_view *value = qa_cvars_find(cvars, name);
    return value ? value->number : 0;
}
static bool integer_enabled(const qa_cvars *cvars, const char *name) {
    const qa_cvar_view *value = qa_cvars_find(cvars, name);
    return value && value->integer != 0;
}
static const char *text(const qa_cvars *cvars, const char *name) {
    const qa_cvar_view *value = qa_cvars_find(cvars, name);
    return value ? value->value : "";
}
static bool rerelease(const qa_cvars *cvars) {
    return qa_cvars_dialect(cvars) == QA_RULESET_Q2_RERELEASE;
}
uint32_t qa_q2_source_deathmatch_flags(const qa_cvars *cvars) {
    const qa_cvar_view *value = cvars ? qa_cvars_find(cvars, "dmflags") : NULL;
    uint32_t flags = value ? (uint32_t)value->integer : 0;
    if (!cvars || !rerelease(cvars)) return flags;
    static const struct { const char *name; uint32_t mask; bool inverted; } rules[] = {
        {"g_dm_weapons_stay", 4, false}, {"g_dm_instant_items", 16, false},
        {"g_dm_same_level", 32, false}, {"g_dm_no_quad_drop", 16384, true},
    };
    for (size_t i = 0; i < sizeof(rules) / sizeof(*rules); ++i) {
        bool enabled = (number(cvars, rules[i].name) != 0) != rules[i].inverted;
        flags = enabled ? flags | rules[i].mask : flags & ~rules[i].mask;
    }
    static const struct { const char *name; uint32_t mask; bool inverted; } integer_rules[] = {
        {"g_no_health", 1, false}, {"g_no_items", 2, false}, {"g_no_armor", 2048, false},
        {"g_friendly_fire", 256, true},
    };
    for (size_t i = 0; i < sizeof(integer_rules) / sizeof(*integer_rules); ++i) {
        const qa_cvar_view *setting = qa_cvars_find(cvars, integer_rules[i].name);
        bool enabled = (setting && setting->integer != 0) != integer_rules[i].inverted;
        flags = enabled ? flags | integer_rules[i].mask : flags & ~integer_rules[i].mask;
    }
    return flags;
}

bool qa_q2_source_player_rules(const qa_cvars *cvars, qa_q2_player_rules *rules,
                                qa_error *error) {
    if (!cvars || !rules || (qa_cvars_dialect(cvars) != QA_RULESET_Q2_CLASSIC && !rerelease(cvars))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 rules require their actual source registry");
        return false;
    }
    qa_q2_player_rules candidate = *rules;
    float clients = number(cvars, "maxclients"), spectators = number(cvars, "maxspectators");
    float flood = number(cvars, "flood_msgs"), lives = number(cvars, "g_coop_num_lives");
    if (!isfinite(clients) || clients < 1 || clients > 64 ||
        !isfinite(spectators) || spectators < 0 || (double)spectators > UINT32_MAX ||
        !isfinite(flood) || flood < 0 || (double)flood > UINT_MAX ||
        !isfinite(lives) || lives < 0 || (double)lives > INT_MAX) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 source capacity or player count is outside its native domain");
        return false;
    }
    candidate.max_clients = (uint32_t)clients;
    candidate.max_spectators = (uint32_t)spectators;
    candidate.flood_messages = (unsigned)flood;
    candidate.password = text(cvars, "password");
    candidate.spectator_password = text(cvars, "spectator_password");
    candidate.cheats = rerelease(cvars) ? integer_enabled(cvars, "cheats") :
        number(cvars, "cheats") != 0;
    candidate.teamplay = rerelease(cvars) && integer_enabled(cvars, "teamplay");
    candidate.flood_seconds = number(cvars, "flood_persecond");
    candidate.flood_wait_seconds = number(cvars, "flood_waitdelay");
    candidate.roll_speed = number(cvars, "sv_rollspeed");
    candidate.roll_angle = number(cvars, "sv_rollangle");
    candidate.run_pitch = number(cvars, "run_pitch");
    candidate.run_roll = number(cvars, "run_roll");
    candidate.bob_up = number(cvars, "bob_up");
    candidate.bob_pitch = number(cvars, "bob_pitch");
    candidate.bob_roll = number(cvars, "bob_roll");
    candidate.gun_offset = (qa_vec3){number(cvars, "gun_x"), number(cvars, "gun_y"), number(cvars, "gun_z")};
    if (rerelease(cvars)) {
        candidate.start_items = text(cvars, "g_start_items");
        candidate.map_list_shuffle = number(cvars, "g_map_list_shuffle") != 0;
        candidate.coop_squad_respawn = number(cvars, "g_coop_squad_respawn") != 0;
        candidate.coop_instanced_items = number(cvars, "g_coop_instanced_items") != 0;
        candidate.coop_lives = number(cvars, "g_coop_enable_lives") != 0;
        candidate.coop_player_collision = number(cvars, "g_coop_player_collision") != 0;
        candidate.coop_num_lives = (int)lives;
        candidate.force_respawn = number(cvars, "g_dm_force_respawn") != 0;
        candidate.force_respawn_seconds = number(cvars, "g_dm_force_respawn_time");
        candidate.no_fall_damage = number(cvars, "g_dm_no_fall_damage") != 0;
        candidate.spawn_farthest = number(cvars, "g_dm_spawn_farthest") != 0;
        candidate.deathmatch_allow_exit = number(cvars, "g_dm_allow_exit") != 0;
    }
    if (!isfinite(candidate.roll_speed) || candidate.roll_speed <= 0 ||
        !isfinite(candidate.flood_seconds) || candidate.flood_seconds < 0 ||
        !isfinite(candidate.flood_wait_seconds) || candidate.flood_wait_seconds < 0 ||
        !isfinite(candidate.force_respawn_seconds) || candidate.force_respawn_seconds < 0 ||
        !isfinite(candidate.roll_angle) || !isfinite(candidate.run_pitch) ||
        !isfinite(candidate.run_roll) || !isfinite(candidate.bob_up) ||
        !isfinite(candidate.bob_pitch) || !isfinite(candidate.bob_roll) ||
        !qa_vec_finite(candidate.gun_offset)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 source player settings are outside their native domain");
        return false;
    }
    *rules = candidate;
    return true;
}

static char *copy(const char *value) {
    size_t size = strlen(value) + 1;
    char *out = malloc(size);
    if (out) memcpy(out, value, size);
    return out;
}
static bool rotation_token(qa_bytes *input, bool commas, qa_bytes *out) {
    size_t first = 0;
    while (first < input->size && (input->data[first] <= ' ' ||
                                  (commas && input->data[first] == ','))) ++first;
    size_t end = first;
    while (end < input->size && input->data[end] > ' ' &&
           (!commas || input->data[end] != ',')) ++end;
    *out = (qa_bytes){input->data + first, end - first};
    input->data += end;
    input->size -= end;
    return end != first;
}

static bool rotation_row(qa_bytes *input, bool limits, bool commas, qa_bytes *name,
                          qa_map_rotation_entry *row) {
    while (input->size) {
        qa_bytes fields = *input;
        if (limits) {
            size_t end = 0;
            while (end < fields.size && fields.data[end] != '\n') ++end;
            fields.size = end;
            size_t consumed = end + (end < input->size);
            input->data += consumed;
            input->size -= consumed;
        }
        if (!rotation_token(&fields, commas, name)) {
            if (!limits) *input = fields;
            continue;
        }
        *row = (qa_map_rotation_entry){.maximum_players = limits ? 99 : INT32_MAX};
        if (limits) {
            int32_t *values[] = {&row->minimum_players, &row->maximum_players};
            for (size_t i = 0; i < 2; ++i) {
                qa_bytes number;
                if (!rotation_token(&fields, false, &number)) break;
                size_t digit = number.size && (number.data[0] == '+' || number.data[0] == '-');
                if (digit >= number.size || number.data[digit] < '0' || number.data[digit] > '9')
                    break;
                qa_number_cursor cursor = {.text = number};
                *values[i] = qa_parse_decimal_i32_cursor(&cursor, QA_DECIMAL_ASCII_CLAMP, false);
            }
        } else *input = fields;
        return true;
    }
    return false;
}

static bool rotation(qa_q2_game *game, const qa_cvars *cvars,
                     qa_map_rotation_entry **out, size_t *count, bool *player_limits,
                     qa_error *error) {
    bool classic = !rerelease(cvars), limits = false;
    qa_bytes source = {0};
    void *lease = NULL;
    if (game->hooks.rotation_source &&
        !game->hooks.rotation_source(game->hooks.context, &source, &limits, &lease, error))
        return false;
    if (!source.size) {
        const char *list = text(cvars, classic ? "sv_maplist" : "g_map_list");
        source = (qa_bytes){(const uint8_t *)list, strlen(list)};
        limits = false;
    }
    uint8_t *lowercase = limits && source.size ? malloc(source.size) : NULL;
    if (limits && source.size && !lowercase) {
        if (lease) game->hooks.rotation_release(game->hooks.context, lease);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Reading map rotation names");
        return false;
    }
    if (lowercase) {
        for (size_t i = 0; i < source.size; ++i) {
            unsigned char c = source.data[i];
            lowercase[i] = c >= 'A' && c <= 'Z' ? (unsigned char)(c + 'a' - 'A') : c;
        }
        source.data = lowercase;
    }
    qa_bytes input = source, name;
    qa_map_rotation_entry row;
    size_t total = 0;
    while (rotation_row(&input, limits, classic && !limits, &name, &row)) ++total;
    if (!total && limits) {
        free(lowercase);
        lowercase = NULL;
        limits = false;
        const char *list = text(cvars, classic ? "sv_maplist" : "g_map_list");
        source = (qa_bytes){(const uint8_t *)list, strlen(list)};
        input = source;
        while (rotation_row(&input, false, classic, &name, &row)) ++total;
    }
    if (total > UINT32_MAX || total > SIZE_MAX / sizeof(**out)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Map rotation exceeds native storage");
        goto fail;
    }
    qa_map_rotation_entry *maps = total ? malloc(total * sizeof(*maps)) : NULL;
    if (total && !maps) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining map rotation");
        goto fail;
    }
    qa_strings *strings = qa_session_strings(game->services.session);
    input = source;
    size_t written = 0;
    while (rotation_row(&input, limits, classic && !limits, &name, &row)) {
        if (!qa_strings_intern(strings, name, &row.map, error)) {
            free(maps);
            goto fail;
        }
        maps[written++] = row;
    }
    if (limits) {
        for (size_t i = 1; i < written; ++i) {
            row = maps[i];
            size_t j = i;
            while (j && strcmp(qa_strings_cstr(strings, maps[j - 1].map),
                               qa_strings_cstr(strings, row.map)) > 0) {
                maps[j] = maps[j - 1];
                --j;
            }
            maps[j] = row;
        }
    }
    free(lowercase);
    if (lease) game->hooks.rotation_release(game->hooks.context, lease);
    *out = maps;
    *count = written;
    *player_limits = limits;
    return true;
fail:
    free(lowercase);
    if (lease) game->hooks.rotation_release(game->hooks.context, lease);
    return false;
}

static bool rotation_entry_equal(qa_map_rotation_entry first, qa_map_rotation_entry second) {
    return first.map == second.map && first.minimum_players == second.minimum_players &&
           first.maximum_players == second.maximum_players;
}

static bool same_rotation(const qa_map_rotation_entry *first,
                          const qa_map_rotation_entry *second, size_t count) {
    if (!count) return true;
    if (!first || !second) return false;
    for (size_t i = 0; i < count; ++i) {
        size_t first_count = 0, second_count = 0;
        for (size_t j = 0; j < count; ++j) {
            first_count += rotation_entry_equal(first[j], first[i]);
            second_count += rotation_entry_equal(second[j], first[i]);
        }
        if (first_count != second_count) return false;
    }
    return true;
}

bool qa_q2_source_apply(qa_q2_game *game, const qa_cvars *cvars, bool reset_rotation,
                         qa_error *error) {
    if (!game || !cvars || !game->player_runtime || !game->item_runtime ||
        qa_cvars_dialect(cvars) != (game->options.rules)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 source settings require their constructed GAME owner");
        return false;
    }
    q2_players *players = game->player_runtime;
    qa_q2_player_rules rules = players->rules;
    qa_q2_item_options items = game->item_runtime->options;
    if (!qa_q2_source_player_rules(cvars, &rules, error)) return false;
    if (rerelease(cvars)) {
        uint32_t flags = qa_q2_source_deathmatch_flags(cvars);
        items.instanced_coop = rules.coop_instanced_items;
        items.weapon_respawn_seconds = number(cvars, "g_weapon_respawn_time");
        items.random_items = number(cvars, "g_dm_random_items") != 0;
        items.no_mines = integer_enabled(cvars, "g_no_mines") || (flags & 0x20000u) != 0;
        items.no_nukes = integer_enabled(cvars, "g_no_nukes") || (flags & 0x80000u) != 0;
        items.no_spheres = integer_enabled(cvars, "g_no_spheres") || (flags & 0x40000u) != 0;
        items.hunter_camera = number(cvars, "huntercam") != 0;
    } else if (game->options.product == QA_Q2_ROGUE) {
        uint32_t flags = qa_q2_source_deathmatch_flags(cvars);
        items.random_items = number(cvars, "randomrespawn") != 0;
        items.hunter_camera = number(cvars, "huntercam") != 0;
        items.no_mines = (flags & 0x20000u) != 0;
        items.no_nukes = (flags & 0x80000u) != 0;
        items.no_spheres = (flags & 0x100000u) != 0;
    }
    if (!isfinite(items.weapon_respawn_seconds) || items.weapon_respawn_seconds < 0) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 weapon respawn setting is outside its native domain");
        return false;
    }
    char *strings[3] = {copy(rules.password), copy(rules.spectator_password), copy(rules.start_items)};
    qa_map_rotation_entry *maps = NULL;
    size_t map_count = 0;
    bool player_limits = false;
    if (!strings[0] || !strings[1] || !strings[2] || !rotation(game, cvars, &maps, &map_count, &player_limits, error)) {
        for (size_t i = 0; i < 3; ++i) free(strings[i]);
        free(maps);
        if (!error || !error->code) qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining Q2 source player strings");
        return false;
    }
    free(players->rule_strings[0]); free(players->rule_strings[1]); free(players->rule_strings[4]);
    players->rule_strings[0] = strings[0]; players->rule_strings[1] = strings[1]; players->rule_strings[4] = strings[2];
    rules.password = strings[0]; rules.spectator_password = strings[1]; rules.start_items = strings[2];
    if (!reset_rotation && players->rules.map_list_count == map_count &&
        same_rotation(maps, players->rotation_maps, map_count)) {
        free(maps);
    } else {
        free(players->rotation_maps);
        players->rotation_maps = maps;
    }
    rules.map_list = players->rotation_maps;
    rules.map_list_count = map_count;
    rules.map_list_file = player_limits;
    rules.map_list_random = player_limits && ((uint32_t)qa_source_to_i32(number(cvars, "ctfflags")) & 4096u);
    players->rules = rules;
    game->item_runtime->options = items;
    game->options.deathmatch = number(cvars, "deathmatch") != 0;
    game->options.cooperative = number(cvars, "coop") != 0;
    const qa_cvar_view *skill = qa_cvars_find(cvars, "skill");
    game->options.skill = skill ? skill->integer < 0 ? 0 : skill->integer > 3 ? 3 : skill->integer : 1;
    game->options.deathmatch_flags = qa_q2_source_deathmatch_flags(cvars);
    return true;
}
