#ifndef QA_RECOVERY_H
#define QA_RECOVERY_H

#include "qa/demo.h"
#include "qa/source_save.h"
#include "qa/movement.h"
#include "qa/gameplay.h"

typedef enum qa_save_authority { QA_SAVE_OFFLINE, QA_SAVE_SERVER, QA_SAVE_REMOTE } qa_save_authority;
typedef struct qa_save_eligibility {
    qa_game_family family;
    qa_save_authority authority;
    bool active, deathmatch, intermission;
    const float *player_health;
    size_t player_count;
} qa_save_eligibility;
bool qa_save_eligible(const qa_save_eligibility *, qa_save_purpose, qa_error *);

/* Committed level-entry identity, never a timer. Restore and hub revisits do
 * not request a new autosave. Failed writes retain the pending entry so an
 * explicit retry can complete it; a newer entry replaces the pending identity. */
typedef struct qa_autosave_state {
    uint64_t entered_generation, saved_generation;
    bool enabled, pending;
} qa_autosave_state;
void qa_autosave_configure(qa_autosave_state *, bool enabled);
bool qa_autosave_level_entry(qa_autosave_state *, uint64_t world_generation,
                             bool fresh_entry, qa_error *);

typedef struct qa_recovery qa_recovery;
/* Logical player identity and the actual accepted movement command. Device
 * state, input bindings and presentation are reconstructed normally. */
typedef struct qa_recovery_input {
    uint32_t seat;
    qa_movement_command command;
} qa_recovery_input;
bool qa_recovery_input_decode(qa_bytes, qa_recovery_input *, qa_error *);
/* Missing and cleanly closed journals are not recovery candidates. A corrupt
 * complete record is an error; only an incomplete final block is discarded. */
bool qa_recovery_available(qa_fs_root *, const char *, bool *, qa_error *);
/* Recovery stores the latest explicit save or level-entry checkpoint.
 * Ordinary frame execution never appends input or simulation records.
 * Existing recorded tails remain readable at startup. */
/* A committed level-entry image is also the recovery checkpoint; its payload
 * does not need a second capture just to change the purpose label. */
bool qa_recovery_begin(qa_fs_root *, const char *relative_name, const qa_save_image *,
                        qa_recovery **, qa_error *);
bool qa_recovery_checkpoint(qa_recovery *, const qa_save_image *, qa_error *);
bool qa_recovery_close_clean(qa_recovery *, qa_error *);
void qa_recovery_destroy(qa_recovery *);
bool qa_recovery_restore(qa_fs_root *, const char *, void *, const qa_demo_seek_ops *,
                          uint64_t *reached_ns, qa_error *);

#endif
