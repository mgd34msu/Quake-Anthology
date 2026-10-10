#include "client_private.h"
#include "qa/game_type.h"

static unsigned char upper(unsigned char value) {
    return value >= 'a' && value <= 'z' ? (unsigned char)(value - ('a' - 'A')) : value;
}

static bool key_equal(const char *text, size_t length, const char *wanted) {
    size_t i = 0;
    while (i < length && wanted[i]) {
        if (upper((unsigned char)text[i]) != upper((unsigned char)wanted[i])) return false;
        ++i;
    }
    return i == length && !wanted[i];
}

void qa_q3_client_info_value(const char *source, const char *wanted,
                            char *out, size_t capacity) {
    if (!out || !capacity) return;
    out[0] = 0;
    if (!source || !wanted) return;
    char info[1024];
    size_t length = 0;
    while (length < sizeof(info) - 1 && source[length]) {
        info[length] = source[length];
        ++length;
    }
    info[length] = 0;
    size_t cursor = info[0] == '\\' ? 1 : 0;
    while (cursor < length) {
        size_t key = cursor;
        while (cursor < length && info[cursor] != '\\') ++cursor;
        if (cursor == length) return;
        size_t key_length = cursor++ - key;
        size_t value = cursor;
        while (cursor < length && info[cursor] != '\\') ++cursor;
        if (key_equal(info + key, key_length, wanted)) {
            size_t count = cursor - value;
            if (count >= capacity) count = capacity - 1;
            memcpy(out, info + value, count);
            out[count] = 0;
            return;
        }
        if (cursor < length) ++cursor;
    }
}

void qa_q3_client_clean_name(const char *source, char out[QA_Q3_NATIVE_NETNAME]) {
    size_t cursor = 0, used = 0, colorless = 0, spaces = 0;
    if (!source) source = "";
    while (cursor < 1023 && source[cursor]) {
        unsigned char value = (unsigned char)source[cursor++];
        if (!used && value == ' ') continue;
        if (value == '^') {
            if (cursor == 1023 || !source[cursor]) break;
            unsigned char color = (unsigned char)source[cursor++];
            if (((color - '0') & 7) == 0) continue;
            if (used > QA_Q3_NATIVE_NETNAME - 3) break;
            out[used++] = '^';
            out[used++] = (char)color;
            continue;
        }
        if (value == ' ') {
            if (++spaces > 3) continue;
        } else spaces = 0;
        if (used > QA_Q3_NATIVE_NETNAME - 2) break;
        out[used++] = (char)value;
        ++colorless;
    }
    if (!used || !colorless) {
        memcpy(out, "UnnamedPlayer", sizeof("UnnamedPlayer"));
        return;
    }
    out[used] = 0;
}

static int32_t source_integer(const char *text) {
    while (*text) {
        unsigned char byte = (unsigned char)*text;
        int value = byte < 128 ? byte : (int)byte - 256;
        if (value > 32) break;
        ++text;
    }
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') ++text;
    uint32_t bits = 0;
    while (*text >= '0' && *text <= '9') bits = bits * 10u + (uint32_t)(*text++ - '0');
    if (negative) bits = 0u - bits;
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

bool qa_q3_client_userinfo(qa_q3_game *game, qa_actor_id actor, const char *source,
                          bool scoreboard, qa_error *error) {
    uint32_t slot;
    if (!q3_client_actor(game, actor, error) || !q3_client_slot(game, actor, &slot)) return false;
    return qa_q3_client_slot_userinfo(game, slot, source, scoreboard, error);
}
bool qa_q3_client_selected_presentation(qa_q3_game *game, qa_actor_id actor,
    const char *source, int32_t game_type, qa_error *error) {
    uint32_t slot;
    if (!game || game->source_restored || !source || game->observation_depth == SIZE_MAX ||
        !qa_q3_native_client_slot(game, actor, &slot, error))
        return q3_fail(error, "Selected Q3 presentation has no actual source client");
    char name[1024], model[64], head[64], red[1024], blue[1024], color1[1024], color2[1024], task[1024];
    qa_q3_client_info_value(source, "name", name, sizeof(name));
    qa_q3_client_info_value(source, qa_game_type_is_team(game_type) ? "team_model" : "model", model, sizeof(model));
    qa_q3_client_info_value(source, qa_game_type_is_team(game_type) ? "team_headmodel" : "headmodel", head, sizeof(head));
    qa_q3_client_info_value(source, "g_redteam", red, sizeof(red));
    qa_q3_client_info_value(source, "g_blueteam", blue, sizeof(blue));
    qa_q3_client_info_value(source, "color1", color1, sizeof(color1));
    qa_q3_client_info_value(source, "color2", color2, sizeof(color2));
    qa_q3_client_info_value(source, "teamtask", task, sizeof(task));
    q3_client_state *client = &game->clients[slot];
    char cleaned[QA_Q3_NATIVE_NETNAME];
    q3_client_name_bytes(game, client, cleaned);
    qa_q3_client_clean_name(name, cleaned);
    if (!q3_client_name_store(game, client, cleaned, error)) return false;
    qa_q3_client_session session = client->rule.session;
    char config[8192];
    snprintf(config, sizeof(config), "n\\%s\\t\\%d\\model\\%s\\hmodel\\%s\\g_redteam\\%s\\g_blueteam\\%s\\c1\\%s\\c2\\%s\\hc\\%d\\w\\%d\\l\\%d\\tt\\%d\\tl\\%d",
        q3_client_name(game, client), session.team, model, head, red, blue, color1, color2,
        client->rule.max_health, session.wins, session.losses, source_integer(task), session.team_leader);
    ++game->observation_depth;
    bool okay = qa_q3_configstring_write(game, 544u + slot, config, error);
    uint32_t current;
    if (okay && (!qa_q3_native_client_slot(game, actor, &current, error) || current != slot ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor)))
        okay = q3_fail(error, "Selected Q3 presentation lost its actual source client");
    --game->observation_depth;
    return okay;
}
bool qa_q3_client_slot_userinfo(qa_q3_game *game, uint32_t slot, const char *source,
                               bool scoreboard, qa_error *error) {
    if (!game || game->source_restored || !source || slot >= game->options.max_clients)
        return q3_fail(error, "Q3 userinfo has no configured fixed source client");
    q3_client_state *client = &game->clients[slot];
    q3_actor *entry = &game->client_actors[slot];
    char info[1024];
    size_t length = 0;
    while (length < sizeof(info) - 1 && source[length]) {
        info[length] = source[length];
        ++length;
    }
    info[length] = 0;
    if (strchr(info, '"') || strchr(info, ';'))
        memcpy(info, "\\name\\badinfo", sizeof("\\name\\badinfo"));
    char value[1024];
    qa_q3_client_info_value(info, "ip", value, sizeof(value));
    if (!strcmp(value, "localhost")) client->rule.local_client = true;
    qa_q3_client_info_value(info, "cg_predictItems", value, sizeof(value));
    client->rule.predict_item_pickup = source_integer(value) != 0;
    qa_q3_client_info_value(info, "name", value, sizeof(value));
    char cleaned[QA_Q3_NATIVE_NETNAME];
    q3_client_name_bytes(game, client, cleaned);
    qa_q3_client_clean_name(value, cleaned);
    if (client->player && (!client->player->present || !game->options.services.player_info)) {
        if (!qa_strings_intern_cstr(qa_session_strings(game->options.services.session),
                cleaned, &client->player->name, error)) return false;
        client->player->present = true;
    }
    if (scoreboard) memcpy(cleaned, "scoreboard", sizeof("scoreboard"));
    if (!q3_client_name_store(game, client, cleaned, error)) return false;
    qa_q3_client_info_value(info, "handicap", value, sizeof(value));
    int32_t health = source_integer(value);
    if (health < 1 || health > 100) health = 100;
    entry->state.player.handicap = health;
    if (game->options.product == QA_Q3_TEAM_ARENA &&
        entry->state.player.powerups[QA_Q3_P_GUARD]) health = 200;
    client->rule.max_health = entry->state.player.max_health = health;
    qa_q3_client_info_value(info, "teamoverlay", value, sizeof(value));
    client->rule.team_info = (game->options.product == QA_Q3_TEAM_ARENA &&
        qa_game_type_has_allies(game->options.rules.game_type)) || !*value || source_integer(value) != 0;
    return true;
}
