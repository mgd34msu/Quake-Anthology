#include "internal.h"
#include "qa/recovery.h"
#include <math.h>

struct qa_recovery { qa_demo_recorder *recorder; };

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
    if (!out || !initial || qa_save_image_metadata(initial)->purpose != QA_SAVE_RECOVERY)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Recovery requires an explicit recovery checkpoint");
    qa_recovery *recovery = calloc(1, sizeof(*recovery));
    if (!recovery) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating recovery owner");
    if (!qa_demo_record_begin(root, name, initial, &recovery->recorder, error)) { free(recovery); return false; }
    *out = recovery;
    return true;
}
bool qa_recovery_append(qa_recovery *recovery, qa_demo_record_kind kind, uint64_t elapsed_ns,
                         qa_net_protocol_id protocol, qa_bytes payload, qa_error *error)
{
    if (!recovery || kind == QA_DEMO_KEYFRAME || kind == QA_DEMO_END)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid recovery journal record");
    return qa_demo_record_append(recovery->recorder, kind, elapsed_ns, protocol, payload, error);
}
bool qa_recovery_checkpoint(qa_recovery *recovery, const qa_save_image *image, qa_error *error)
{
    if (!recovery || !image || qa_save_image_metadata(image)->purpose != QA_SAVE_RECOVERY)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid recovery checkpoint replacement");
    return qa_demo_record_keyframe(recovery->recorder, image, error);
}
bool qa_recovery_close_clean(qa_recovery *recovery, qa_error *error)
{
    return recovery ? qa_demo_record_end(recovery->recorder, error) :
        persistence_fail(error, QA_ERROR_ARGUMENT, "Absent recovery owner");
}
void qa_recovery_destroy(qa_recovery *recovery)
{ if (recovery) { qa_demo_recorder_destroy(recovery->recorder); free(recovery); } }
bool qa_recovery_restore(qa_fs_root *root, const char *name, void *context,
                          const qa_demo_seek_ops *ops, uint64_t *reached_ns, qa_error *error)
{
    qa_demo *demo = NULL;
    if (!qa_demo_read(root, name, true, &demo, error)) return false;
    const qa_demo_record *first = qa_demo_record_at(demo, 0);
    qa_save_image *image = NULL;
    bool ok = first && qa_save_image_decode(first->payload, &image, error);
    if (ok && qa_save_image_metadata(image)->purpose != QA_SAVE_RECOVERY)
        ok = persistence_fail(error, QA_ERROR_FORMAT, "Recording is not a recovery journal");
    qa_save_image_destroy(image);
    if (ok) ok = qa_demo_seek(demo, qa_demo_end_time(demo), context, ops, reached_ns, error);
    qa_demo_destroy(demo);
    return ok;
}
