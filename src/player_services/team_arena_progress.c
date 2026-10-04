/* UI_CalcPostGameStats and native postGameInfo_t score files. */
#include "qa/team_arena_progress.h"
#include "qa/binary.h"
#include "save_io.h"

#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

struct qa_team_arena_progress {
    qa_fs_root *root;
    uint64_t nonce;
    char *saved_root, *admitted_root;
    qa_fs_identity saved_identity, admitted_identity;
    bool busy, restore_pending;
};
static bool fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
static int32_t signed_bits(uint32_t bits)
{ int32_t value; memcpy(&value, &bits, sizeof(value)); return value; }
static int32_t qvm_integer(double value)
{
    volatile float stored = value > FLT_MAX ? INFINITY : value < -FLT_MAX ? -INFINITY : (float)value;
    return stored >= -2147483648.0 && stored < 2147483648.0 ? (int32_t)stored : INT32_MIN;
}
static int32_t js_integer(double value)
{
    if (!isfinite(value) || value == 0) return 0;
    double remainder = fmod(trunc(value), 4294967296.0);
    if (remainder < 0) remainder += 4294967296.0;
    return signed_bits((uint32_t)remainder);
}
bool qa_team_arena_score_calculate(const qa_team_arena_score_input *input,
    const qa_team_arena_score *previous, qa_team_arena_score_result *out, qa_error *error)
{
    if (!input || !previous || !out)
        return fail(error, QA_ERROR_ARGUMENT, "Team Arena score calculation needs its actual source inputs");
    const qa_team_arena_stats *stats = &input->stats;
    volatile float end = (float)stats->end_time, start = (float)input->match_start_time;
    volatile float elapsed = end - start, seconds = elapsed / 1000.0f;
    int32_t time = qvm_integer(seconds);
    int32_t bonus = time < input->time_to_beat
        ? signed_bits((uint32_t)js_integer(input->time_to_beat - time) * 10u) : 0;
    bool won = stats->red_score > stats->blue_score;
    int32_t shutout = won && stats->blue_score <= 0 ? 100 : 0;
    int32_t skill = qvm_integer(input->skill);
    if (skill < 1) skill = 1;
    qa_team_arena_score current = {
        .score = signed_bits(((uint32_t)stats->base_score + (uint32_t)shutout + (uint32_t)bonus) * (uint32_t)skill),
        .red_score = stats->red_score, .blue_score = stats->blue_score, .perfects = stats->perfects,
        .accuracy = stats->accuracy, .impressives = stats->impressives, .excellents = stats->excellents,
        .defends = stats->defends, .assists = stats->assists, .gauntlets = stats->gauntlets,
        .captures = stats->captures, .time = time, .time_bonus = bonus,
        .shutout_bonus = shutout, .skill_bonus = skill, .base_score = stats->base_score};
    *out = (qa_team_arena_score_result){.current = current, .previous = *previous,
        .won = won, .new_high_score = won && current.score > previous->score,
        .new_best_time = time < previous->time};
    return true;
}
void qa_team_arena_score_encode(const qa_team_arena_score *score, uint8_t out[68])
{
    const int32_t fields[16] = {score->score, score->red_score, score->blue_score, score->perfects,
        score->accuracy, score->impressives, score->excellents, score->defends, score->assists,
        score->gauntlets, score->captures, score->time, score->time_bonus, score->shutout_bonus,
        score->skill_bonus, score->base_score};
    qa_store_u32le(out, 64);
    for (size_t i = 0; i < 16; ++i) qa_store_u32le(out + 4 + i * 4, (uint32_t)fields[i]);
}
void qa_team_arena_score_decode(qa_bytes bytes, qa_team_arena_score *out)
{
    uint8_t padded[68] = {0};
    size_t size = bytes.size < sizeof(padded) ? bytes.size : sizeof(padded);
    if (bytes.data && size) memcpy(padded, bytes.data, size);
    if (qa_load_i32le(padded) != 64) memset(padded, 0, sizeof(padded));
    int32_t fields[16];
    for (size_t i = 0; i < 16; ++i) fields[i] = qa_load_i32le(padded + 4 + i * 4);
    *out = (qa_team_arena_score){fields[0], fields[1], fields[2], fields[3], fields[4], fields[5],
        fields[6], fields[7], fields[8], fields[9], fields[10], fields[11], fields[12], fields[13], fields[14], fields[15]};
}
void qa_team_arena_score_path(const char *map, int32_t game_type, char out[64])
{
    char bounded[64]; size_t size = 0;
    while (size < 63 && map[size]) { bounded[size] = map[size]; ++size; }
    bounded[size] = 0;
    char full[96]; snprintf(full, sizeof(full), "games/%s_%" PRId32 ".game", bounded, game_type);
    size = strlen(full); if (size > 63) size = 63;
    memcpy(out, full, size); out[size] = 0;
}
static bool root_snapshot(const qa_team_arena_progress *owner, char **path,
    qa_fs_identity *identity, qa_error *error)
{
    qa_fs_entry_kind kind;
    if (!qa_fs_root_join(owner->root, "", path, error) ||
        !qa_fs_root_status(owner->root, "", &kind, identity, error)) return false;
    return kind == QA_FS_DIRECTORY || fail(error, QA_ERROR_FORMAT, "Team Arena score root is not its retained directory");
}
static bool same_root(const qa_fs_identity *a, const qa_fs_identity *b)
{ return a->words[0] == b->words[0] && a->words[1] == b->words[1]; }
bool qa_team_arena_progress_create(qa_fs_root *root, qa_team_arena_progress **out, qa_error *error)
{
    if (!root || !out || *out) return fail(error, QA_ERROR_ARGUMENT, "Team Arena progression needs its actual source root and empty output");
    qa_team_arena_progress *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Allocating Team Arena score owner");
    owner->root = root; qa_fs_root_retain(root); *out = owner; return true;
}
void qa_team_arena_progress_destroy(qa_team_arena_progress *owner)
{
    if (!owner) return;
    qa_fs_root_close(owner->root); free(owner->saved_root); free(owner->admitted_root); free(owner);
}
bool qa_team_arena_progress_ready(const qa_team_arena_progress *owner, qa_error *error)
{ return (owner && owner->root && !owner->busy) || fail(error, QA_ERROR_ARGUMENT, "Team Arena score owner is absent or executing"); }
const qa_fs_root *qa_team_arena_progress_root(const qa_team_arena_progress *owner)
{ return owner ? owner->root : NULL; }
static bool read_score(qa_team_arena_progress *owner, const char *path,
    qa_team_arena_score *out, qa_error *error)
{
    qa_fs_entry_kind kind; qa_fs_identity identity;
    if (!qa_fs_root_status(owner->root, path, &kind, &identity, error)) return false;
    if (kind == QA_FS_MISSING) { qa_team_arena_score_decode((qa_bytes){0}, out); return true; }
    qa_fs_file *file = NULL; qa_buffer bytes = {0};
    bool okay = qa_fs_root_file_open(owner->root, path, &file, &identity, error) &&
        qa_fs_file_read_snapshot(file, &identity, &bytes, error);
    if (okay) qa_team_arena_score_decode((qa_bytes){bytes.data, bytes.size}, out);
    qa_buffer_free(&bytes); qa_fs_file_close(file); return okay;
}
bool qa_team_arena_progress_record(qa_team_arena_progress *owner, const char *map,
    int32_t game_type, const qa_team_arena_score_input *input,
    qa_team_arena_score_result *out, qa_error *error)
{
    if (!qa_team_arena_progress_ready(owner, error) || owner->restore_pending || !map || !input || !out)
        return fail(error, QA_ERROR_ARGUMENT, "Team Arena score write needs its published actual source owner");
    owner->busy = true;
    char path[64]; qa_team_arena_score previous; qa_team_arena_score_result result;
    qa_team_arena_score_path(map, game_type, path);
    bool okay = read_score(owner, path, &previous, error) &&
        qa_team_arena_score_calculate(input, &previous, &result, error);
    if (okay && result.new_high_score) {
        uint8_t bytes[68]; qa_team_arena_score_encode(&result.current, bytes);
        if (owner->nonce == UINT64_MAX) okay = fail(error, QA_ERROR_ARGUMENT, "Team Arena score replacement exhausted its source nonce");
        else okay = qa_fs_root_replace(owner->root, path, (qa_bytes){bytes, sizeof(bytes)}, ++owner->nonce, error);
    }
    if (okay) *out = result;
    owner->busy = false; return okay;
}
static bool identity_fields(qa_source_save_io *io, qa_fs_identity *identity)
{
    for (size_t i = 0; i < QA_FS_IDENTITY_WORDS; ++i)
        if (!qa_source_save_u64(io, identity->words + i)) return false;
    return true;
}
static const uint8_t magic[8] = {'Q','A','T','A',1,0,0,0};
bool qa_team_arena_progress_checkpoint(const qa_team_arena_progress *owner, qa_buffer *out, qa_error *error)
{
    if (!qa_team_arena_progress_ready(owner, error) || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "Team Arena score capture needs its idle real owner and empty output");
    char *current = NULL; qa_fs_identity identity;
    bool okay = root_snapshot(owner, &current, &identity, error);
    if (okay && owner->saved_root && (strcmp(current, owner->admitted_root) || !same_root(&identity, &owner->admitted_identity)))
        okay = fail(error, QA_ERROR_FORMAT, "Team Arena score root changed after its source admission");
    char *path = owner->saved_root ? owner->saved_root : current;
    if (owner->saved_root) identity = owner->saved_identity;
    uint64_t nonce = owner->nonce; qa_source_save_io io = {0};
    if (okay) okay = qa_source_save_writer(&io, NULL, error) && ps_magic(&io, magic) &&
        qa_source_save_owned_text(&io, &path) && identity_fields(&io, &identity) && qa_source_save_u64(&io, &nonce) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); free(current); return okay;
}
bool qa_team_arena_progress_restore(qa_bytes bytes, qa_fs_root *root,
    const qa_team_arena_progress_refs *refs, qa_team_arena_progress **out, qa_error *error)
{
    if (!root || !out || *out) return fail(error, QA_ERROR_ARGUMENT, "Team Arena score reconstruction needs its qualified candidate root");
    qa_team_arena_progress *owner = NULL;
    if (!qa_team_arena_progress_create(root, &owner, error)) return false;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && ps_magic(&io, magic) &&
        qa_source_save_owned_text(&io, &owner->saved_root);
    if (okay && (!owner->saved_root || !owner->saved_root[0]))
        okay = fail(error, QA_ERROR_FORMAT, "Team Arena score record has no actual saved source root");
    if (okay) okay = identity_fields(&io, &owner->saved_identity) && qa_source_save_u64(&io, &owner->nonce) &&
        qa_source_save_finish(&io, NULL) && root_snapshot(owner, &owner->admitted_root, &owner->admitted_identity, error);
    if (okay && refs && refs->root_ready)
        okay = refs->root_ready(refs->context, root, owner->saved_root, &owner->saved_identity, error);
    else if (okay && (strcmp(owner->saved_root, owner->admitted_root) ||
                      !same_root(&owner->saved_identity, &owner->admitted_identity)))
        okay = fail(error, QA_ERROR_FORMAT, "Team Arena score root differs from its saved actual source owner");
    qa_source_save_dispose(&io);
    if (!okay) { qa_team_arena_progress_destroy(owner); return false; }
    owner->restore_pending = true; *out = owner; return true;
}
void qa_team_arena_progress_publish_restored(qa_team_arena_progress *owner)
{ if (owner) owner->restore_pending = false; }
