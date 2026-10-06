#include "internal.h"
#include "qa/recovery.h"
#include <math.h>

struct qa_recovery {
    qa_demo_recorder *recorder;
    qa_fs_root *root;
    char *name;
};

static bool input_fields(qa_source_save_io *io, qa_recovery_input *input)
{
    qa_movement_command *command = &input->command;
    uint32_t kind = command->kind;
    bool ok = qa_source_save_u32(io, &input->seat) &&
        qa_source_save_u32(io, &kind) && kind <= QA_MOVEMENT_Q3 &&
        qa_source_save_u64(io, &command->sequence) &&
        qa_source_save_u32(io, &command->milliseconds) &&
        qa_source_save_i32(io, &command->server_time_ms) &&
        qa_source_save_i32(io, &command->server_frame) &&
        qa_source_save_f64(io, &command->acknowledged_server_seconds) &&
        qa_source_save_vec3(io, &command->angles);
    for (unsigned i = 0; ok && i < 3; ++i)
        ok = qa_source_save_i32(io, &command->angle_words[i]);
    ok = ok && qa_source_save_f32(io, &command->forward_move) &&
        qa_source_save_f32(io, &command->side_move) &&
        qa_source_save_f32(io, &command->up_move) &&
        qa_source_save_u32(io, &command->buttons) &&
        qa_source_save_u8(io, &command->impulse) &&
        qa_source_save_u8(io, &command->light_level) &&
        qa_source_save_u8(io, &command->weapon);
    if (ok && (!isfinite(command->acknowledged_server_seconds) ||
        !qa_vec_finite(command->angles) || !isfinite(command->forward_move) ||
        !isfinite(command->side_move) || !isfinite(command->up_move)))
        ok = persistence_io_fail(io, QA_ERROR_FORMAT, "Recovery input contains nonfinite movement");
    if (ok) command->kind = (qa_movement_kind)kind;
    return ok || persistence_io_fail(io, QA_ERROR_FORMAT, "Invalid recovery input fields");
}

bool qa_recovery_input_decode(qa_bytes bytes, qa_recovery_input *out, qa_error *error)
{
    if (!out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Recovery input requires an output");
    qa_recovery_input value = {0};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && input_fields(&io, &value) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) *out = value;
    return ok;
}

static bool recovery_read(qa_fs_root *root, const char *name, qa_demo **out, qa_error *error)
{
    if (!qa_demo_read(root, name, true, out, error)) return false;
    bool ok = true;
    for (size_t i = 0; ok && i < qa_demo_record_count(*out); ++i) {
        const qa_demo_record *record = qa_demo_record_at(*out, i);
        if (record->kind != QA_DEMO_KEYFRAME) continue;
        qa_save_image *image = NULL;
        ok = qa_save_image_decode(record->payload, &image, error);
        if (ok && qa_save_image_metadata(image)->purpose != QA_SAVE_RECOVERY &&
            qa_save_image_metadata(image)->purpose != QA_SAVE_LEVEL_ENTRY &&
            qa_save_image_metadata(image)->purpose != QA_SAVE_MANUAL)
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Recording is not a recovery journal");
        if (!qa_save_image_destroy_checked(&image, error)) ok = false;
    }
    if (!ok) { qa_demo_destroy(*out); *out = NULL; }
    return ok;
}

bool qa_recovery_available(qa_fs_root *root, const char *name, bool *available, qa_error *error)
{
    if (!root || !name || !available)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Recovery inspection requires its directory and output");
    qa_fs_entry_kind kind;
    if (!qa_fs_root_status(root, name, &kind, NULL, error)) return false;
    *available = false;
    if (kind == QA_FS_MISSING) return true;
    qa_demo *demo = NULL;
    bool ok = recovery_read(root, name, &demo, error);
    if (ok) *available = !qa_demo_complete(demo);
    qa_demo_destroy(demo);
    return ok;
}

bool qa_save_eligible(const qa_save_eligibility *state, qa_save_purpose purpose, qa_error *error)
{
    if (!state || (unsigned)state->family > QA_GAME_Q3 || (unsigned)state->authority > QA_SAVE_REMOTE ||
        (unsigned)purpose > QA_SAVE_DEMO_KEYFRAME || (state->player_count && !state->player_health))
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid save eligibility state");
    if (!state->active) return persistence_fail(error, QA_ERROR_ARGUMENT, "No active world to save");
    if (purpose == QA_SAVE_TRANSITION || purpose == QA_SAVE_RECOVERY || purpose == QA_SAVE_DEMO_KEYFRAME)
        return state->authority != QA_SAVE_REMOTE ||
               persistence_fail(error, QA_ERROR_ARGUMENT, "Only the authoritative world can retain engine state");
    if (state->authority != QA_SAVE_OFFLINE)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Save/load unavailable during a network game");
    if (state->family != QA_GAME_Q3 && state->deathmatch)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Cannot save a deathmatch game");
    if (state->family != QA_GAME_Q3 && state->intermission)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Cannot save during intermission");
    if (!state->player_count) return persistence_fail(error, QA_ERROR_ARGUMENT, "No active player to save");
    for (size_t i = 0; state->family != QA_GAME_Q3 && i < state->player_count; ++i)
        if (!isfinite(state->player_health[i]) || state->player_health[i] <= 0)
            return persistence_fail(error, QA_ERROR_ARGUMENT, "Cannot save with a dead player");
    return true;
}

void qa_autosave_configure(qa_autosave_state *state, bool enabled)
{
    if (!state) return;
    state->enabled = enabled;
    if (!enabled) state->pending = false;
}

bool qa_autosave_level_entry(qa_autosave_state *state, uint64_t generation,
                             bool fresh_entry, qa_error *error)
{
    if (!state || !generation) return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid level-entry autosave identity");
    if (generation < state->entered_generation)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Stale level-entry autosave identity");
    if (generation == state->entered_generation) return true;
    state->entered_generation = generation;
    state->pending = state->enabled && fresh_entry && state->saved_generation != generation;
    return true;
}

bool qa_autosave_write(qa_autosave_state *state, qa_fs_root *root, const qa_save_image *image,
                        uint64_t nonce, qa_error *error)
{
    if (!state || !root || !image) return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid autosave write request");
    if (!state->enabled || !state->pending) return true;
    const qa_save_metadata *metadata = qa_save_image_metadata(image);
    if (metadata->purpose != QA_SAVE_LEVEL_ENTRY || metadata->world_generation != state->entered_generation)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Autosave image differs from pending level entry");
    if (!qa_save_write(root, "autosave.sav", image, nonce, error)) return false;
    state->saved_generation = state->entered_generation;
    state->pending = false;
    return true;
}

bool qa_recovery_begin(qa_fs_root *root, const char *name, const qa_save_image *initial,
                        qa_recovery **out, qa_error *error)
{
    if (!root || !name || !out || !initial || (qa_save_image_metadata(initial)->purpose != QA_SAVE_RECOVERY &&
        qa_save_image_metadata(initial)->purpose != QA_SAVE_LEVEL_ENTRY &&
        qa_save_image_metadata(initial)->purpose != QA_SAVE_MANUAL))
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Recovery requires an explicit recovery checkpoint");
    qa_recovery *recovery = calloc(1, sizeof(*recovery));
    if (!recovery) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating recovery owner");
    recovery->name = malloc(strlen(name) + 1);
    if (!recovery->name) { free(recovery); return persistence_fail(error, QA_ERROR_MEMORY, "Retaining recovery filename"); }
    strcpy(recovery->name, name);
    recovery->root = root;
    qa_fs_root_retain(root);
    if (!qa_demo_record_begin(root, name, initial, &recovery->recorder, error)) {
        qa_recovery_destroy(recovery); return false;
    }
    *out = recovery;
    return true;
}
bool qa_recovery_checkpoint(qa_recovery *recovery, const qa_save_image *image, qa_error *error)
{
    if (!recovery || !image || (qa_save_image_metadata(image)->purpose != QA_SAVE_RECOVERY &&
        qa_save_image_metadata(image)->purpose != QA_SAVE_LEVEL_ENTRY &&
        qa_save_image_metadata(image)->purpose != QA_SAVE_MANUAL))
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid recovery checkpoint replacement");
    qa_demo_recorder *replacement = NULL;
    if (!qa_demo_record_begin(recovery->root, recovery->name, image, &replacement, error)) return false;
    qa_demo_recorder_destroy(recovery->recorder);
    recovery->recorder = replacement;
    return true;
}
bool qa_recovery_close_clean(qa_recovery *recovery, qa_error *error)
{
    return recovery ? qa_demo_record_end(recovery->recorder, error) :
        persistence_fail(error, QA_ERROR_ARGUMENT, "Absent recovery owner");
}
void qa_recovery_destroy(qa_recovery *recovery)
{
    if (recovery) {
        qa_demo_recorder_destroy(recovery->recorder);
        qa_fs_root_close(recovery->root);
        free(recovery->name); free(recovery);
    }
}

typedef struct recovery_seek {
    const qa_demo_seek_ops *ops;
    void *context;
    uint64_t committed_sequence;
} recovery_seek;
static bool recovery_create(void *context, const qa_save_image *image, void **candidate, qa_error *error)
{
    recovery_seek *seek = context;
    return seek->ops->create(seek->context, image, candidate, error);
}
static bool recovery_apply(void *context, void *candidate, const qa_demo_record *record, qa_error *error)
{
    recovery_seek *seek = context;
    return record->sequence > seek->committed_sequence ||
        seek->ops->apply(seek->context, candidate, record, error);
}
static bool recovery_finish(void *context, void *candidate, qa_error *error)
{
    recovery_seek *seek = context;
    return seek->ops->finish(seek->context, candidate, error);
}
static bool recovery_publish(void *context, void *candidate, qa_error *error)
{
    recovery_seek *seek = context;
    return seek->ops->publish(seek->context, candidate, error);
}
static void recovery_discard(void *context, void *candidate)
{
    recovery_seek *seek = context;
    seek->ops->discard(seek->context, candidate);
}
bool qa_recovery_restore(qa_fs_root *root, const char *name, void *context,
                          const qa_demo_seek_ops *ops, uint64_t *reached_ns, qa_error *error)
{
    if (!ops || !ops->create || !ops->apply || !ops->finish || !ops->publish || !ops->discard)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Recovery requires actual isolated replay operations");
    qa_demo *demo = NULL;
    if (!recovery_read(root, name, &demo, error)) return false;
    bool ok = !qa_demo_complete(demo) ||
        persistence_fail(error, QA_ERROR_ARGUMENT, "Recovery journal was closed cleanly");
    recovery_seek seek = {.ops = ops, .context = context};
    for (size_t i = 0; i < qa_demo_record_count(demo); ++i) {
        const qa_demo_record *record = qa_demo_record_at(demo, i);
        if (record->kind == QA_DEMO_KEYFRAME || record->kind == QA_DEMO_ADVANCE)
            seek.committed_sequence = record->sequence;
    }
    const qa_demo_seek_ops committed = {.create = recovery_create, .apply = recovery_apply,
        .finish = recovery_finish, .publish = recovery_publish, .discard = recovery_discard};
    if (ok) ok = qa_demo_seek(demo, qa_demo_end_time(demo), &seek, &committed, reached_ns, error);
    qa_demo_destroy(demo);
    return ok;
}
