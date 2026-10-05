/* Source game/g_utils.c AddRemap and BuildShaderStateConfig, and the QVM
 * bg_lib.c AddFloat formatter. Id Software, GPL-2.0-or-later. */
#include "map/internal.h"
#include "shader_remap.h"

#define ENTRY_BYTES (QA_Q3_SHADER_PATH * 2u + 5u)

static bool path_copy(char out[QA_Q3_SHADER_PATH], const char *text, qa_error *error) {
    if (!text) return q3_fail(error, "Q3 shader remap requires actual byte paths");
    size_t length = 0;
    while (length < QA_Q3_SHADER_PATH && text[length]) ++length;
    if (length == QA_Q3_SHADER_PATH)
        return q3_fail(error, "Q3 shader remap path exceeds MAX_QPATH");
    memset(out, 0, QA_Q3_SHADER_PATH);
    memcpy(out, text, length);
    return true;
}
static unsigned char folded(unsigned char byte) {
    return byte >= 'a' && byte <= 'z' ? (unsigned char)(byte - 'a' + 'A') : byte;
}
static bool path_equal(const char *left, const char *right) {
    for (size_t i = 0; i < QA_Q3_SHADER_PATH; ++i) {
        unsigned char a = (unsigned char)left[i], b = (unsigned char)right[i];
        if (folded(a) != folded(b)) return false;
        if (!a) return true;
    }
    return false;
}
static bool time_valid(float value) {
    return isfinite(value) && fabs((double)value) <= 2147483647.0;
}
static bool add(qa_q3_game *game, const char *old_name, const char *new_name,
                  float time_offset, qa_error *error) {
    qa_q3_shader_remap row = {.time_offset = time_offset};
    if (!path_copy(row.old_name, old_name, error) ||
        !path_copy(row.new_name, new_name, error)) return false;
    if (!time_valid(time_offset))
        return q3_fail(error, "Q3 shader time exceeds the source formatter domain");
    qa_q3_shader_remap_state *state = &game->shader_remaps;
    for (uint32_t i = 0; i < state->count; ++i)
        if (path_equal(state->rows[i].old_name, row.old_name)) {
            memcpy(state->rows[i].new_name, row.new_name, sizeof(row.new_name));
            state->rows[i].time_offset = time_offset;
            return true;
        }
    if (state->count < QA_Q3_SHADER_REMAPS) state->rows[state->count++] = row;
    return true;
}

/* %5.2f pads the integer part to five bytes, then truncates two fractional
 * digits through separate stored-float subtraction and multiplication. */
static size_t source_float(char out[32], float value) {
    bool negative = value < 0;
    float remaining = negative ? -value : value;
    uint32_t integer = (uint32_t)trunc((double)remaining);
    char reversed[16];
    size_t digits = 0, at = 0;
    do {
        reversed[digits++] = (char)('0' + integer % 10u);
        integer /= 10u;
    } while (integer);
    if (negative) reversed[digits++] = '-';
    while (at + digits < 5) out[at++] = ' ';
    while (digits) out[at++] = reversed[--digits];
    out[at++] = '.';
    for (unsigned i = 0; i < 2; ++i) {
        float fraction = remaining - (float)trunc((double)remaining);
        float scaled = fraction * 10.0f;
        remaining = scaled;
        out[at++] = (char)('0' + (uint32_t)trunc((double)remaining) % 10u);
    }
    out[at] = 0;
    return at;
}
static size_t format_entry(char out[192], const qa_q3_shader_remap *row) {
    size_t at = strlen(row->old_name), length = strlen(row->new_name);
    memcpy(out, row->old_name, at);
    out[at++] = '=';
    memcpy(out + at, row->new_name, length);
    at += length;
    out[at++] = ':';
    at += source_float(out + at, row->time_offset);
    out[at++] = '@';
    out[at] = 0;
    return at;
}
static bool publish(qa_q3_game *game, qa_error *error) {
    char state[QA_Q3_SHADER_STATE_BYTES] = {0};
    size_t used = 0;
    /* Source array iteration sees rows appended by a nested print callback.
     * The current row is already formatted before that callback runs. */
    for (uint32_t i = 0; i < game->shader_remaps.count; ++i) {
        qa_q3_shader_remap row = game->shader_remaps.rows[i];
        char entry[192];
        size_t length = format_entry(entry, &row);
        if (length >= ENTRY_BYTES) {
            char notice[80];
            (void)snprintf(notice, sizeof(notice), "Com_sprintf: overflow of %u in %u\n",
                           (unsigned)length, (unsigned)ENTRY_BYTES);
            if (!game->options.hooks.console_print)
                return q3_fail(error, "Q3 shader formatter has no source print hook");
            if (!game->options.hooks.console_print(game->options.hooks.context, notice, error))
                return false;
        }
        if (length >= ENTRY_BYTES) length = ENTRY_BYTES - 1;
        size_t writable = sizeof(state) - used - 1;
        if (length > writable) length = writable;
        memcpy(state + used, entry, length);
        used += length;
        state[used] = 0;
    }
    return qa_q3_configstring_write(game, QA_Q3_CS_SHADERSTATE, state, error);
}
static bool enter(qa_q3_game *game, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 source shader remap boundary");
    ++game->observation_depth;
    return true;
}
bool qa_q3_shader_remap_add(qa_q3_game *game, const char *old_name,
                            const char *new_name, float time_offset, qa_error *error) {
    if (!enter(game, error)) return false;
    bool okay = add(game, old_name, new_name, time_offset, error);
    --game->observation_depth;
    return okay;
}
bool qa_q3_shader_remap_publish(qa_q3_game *game, qa_error *error) {
    if (!enter(game, error)) return false;
    bool okay = publish(game, error);
    --game->observation_depth;
    return okay;
}
bool qa_q3_shader_remap_apply(qa_q3_game *game, const char *old_name,
                              const char *new_name, float time_offset, qa_error *error) {
    if (!enter(game, error)) return false;
    bool okay = add(game, old_name, new_name, time_offset, error) && publish(game, error);
    --game->observation_depth;
    return okay;
}
float qa_q3_shader_remap_source_time(int32_t level_time_ms) {
    float time = (float)level_time_ms;
    float seconds = time * 0.001f;
    return seconds;
}
bool qa_q3_shader_remap_teams(qa_q3_game *game, const char *red_team,
                              const char *blue_team, int32_t level_time_ms, qa_error *error) {
    if (!enter(game, error)) return false;
    bool okay = true;
    if (game->options.product == QA_Q3_TEAM_ARENA) {
        if (!red_team || !blue_team) {
            okay = q3_fail(error, "Q3 team remap requires actual cached vmCvar strings");
        } else {
            char red[1024], blue[1024];
            (void)snprintf(red, sizeof(red), "team_icon/%s_red", red_team);
            (void)snprintf(blue, sizeof(blue), "team_icon/%s_blue", blue_team);
            float time = qa_q3_shader_remap_source_time(level_time_ms);
            okay = add(game, "textures/ctf2/redteam01", red, time, error) &&
                   add(game, "textures/ctf2/redteam02", red, time, error) &&
                   add(game, "textures/ctf2/blueteam01", blue, time, error) &&
                   add(game, "textures/ctf2/blueteam02", blue, time, error) &&
                   publish(game, error);
        }
    }
    --game->observation_depth;
    return okay;
}
bool q3_shader_remap_target(void *context, qa_actor_id source, qa_string_id old_name,
                             qa_string_id new_name, uint64_t time_ns, qa_error *error) {
    (void)time_ns;
    qa_q3_game *game = context;
    if (!game || !q3_map_const(game, source))
        return q3_fail(error, "Q3 shader target has no current native map owner");
    qa_strings *strings = qa_session_strings(game->options.services.session);
    const char *old_path = qa_strings_cstr(strings, old_name);
    const char *new_path = qa_strings_cstr(strings, new_name);
    return qa_q3_shader_remap_apply(game, old_path, new_path,
                                    qa_q3_shader_remap_source_time(game->now_ms), error);
}

void q3_shader_remaps_clear(qa_q3_game *game) {
    game->shader_remaps = (qa_q3_shader_remap_state){0};
}
bool q3_shader_remaps_prepare(const qa_q3_shader_remap_state *saved,
                               qa_q3_shader_remap_state *out, qa_error *error) {
    if (!saved || !out || saved->count > QA_Q3_SHADER_REMAPS)
        return q3_fail(error, "invalid Q3 shader remap continuation count");
    qa_q3_shader_remap_state prepared = {.count = saved->count};
    for (uint32_t i = 0; i < saved->count; ++i) {
        const qa_q3_shader_remap *row = &saved->rows[i];
        if (!path_copy(prepared.rows[i].old_name, row->old_name, error) ||
            !path_copy(prepared.rows[i].new_name, row->new_name, error) ||
            !time_valid(row->time_offset))
            return q3_fail(error, "invalid Q3 shader remap continuation row");
        for (uint32_t j = 0; j < i; ++j)
            if (path_equal(prepared.rows[j].old_name, prepared.rows[i].old_name))
                return q3_fail(error, "duplicate Q3 source shader remap path");
        prepared.rows[i].time_offset = row->time_offset;
    }
    *out = prepared;
    return true;
}
bool q3_shader_remaps_capture(const qa_q3_game *game,
                               qa_q3_shader_remap_state *out, qa_error *error) {
    if (!game || game->observation_depth)
        return q3_fail(error, "Q3 shader capture requires an unborrowed source owner");
    return q3_shader_remaps_prepare(&game->shader_remaps, out, error);
}
void q3_shader_remaps_commit(qa_q3_game *game, const qa_q3_shader_remap_state *prepared) {
    game->shader_remaps = *prepared;
}
static bool codec_fail(qa_source_save_io *io, const char *message) {
    io->failed = true;
    qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message);
    return false;
}
static bool path_codec(qa_source_save_io *io, char path[QA_Q3_SHADER_PATH]) {
    uint8_t length = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        while (length < QA_Q3_SHADER_PATH && path[length]) ++length;
    }
    if (!qa_source_save_u8(io, &length)) return false;
    if (length >= QA_Q3_SHADER_PATH)
        return codec_fail(io, "invalid Q3 encoded shader path length");
    if (io->direction == QA_SOURCE_SAVE_READ) memset(path, 0, QA_Q3_SHADER_PATH);
    if (!qa_source_save_bytes(io, path, length)) return false;
    if (memchr(path, 0, length))
        return codec_fail(io, "embedded NUL in Q3 encoded shader path");
    return true;
}
bool q3_shader_remaps_codec(qa_source_save_io *io, qa_q3_shader_remap_state *state) {
    if (!qa_source_save_u32(io, &state->count)) return false;
    if (state->count > QA_Q3_SHADER_REMAPS)
        return codec_fail(io, "invalid Q3 encoded shader remap count");
    for (uint32_t i = 0; i < state->count; ++i)
        if (!path_codec(io, state->rows[i].old_name) ||
            !path_codec(io, state->rows[i].new_name) ||
            !qa_source_save_f32(io, &state->rows[i].time_offset)) return false;
    qa_q3_shader_remap_state prepared;
    if (!q3_shader_remaps_prepare(state, &prepared, io->error))
        return codec_fail(io, "invalid Q3 encoded shader remap authority");
    if (io->direction == QA_SOURCE_SAVE_READ) *state = prepared;
    return true;
}
