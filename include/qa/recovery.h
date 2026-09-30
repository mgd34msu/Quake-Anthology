#ifndef QA_RECOVERY_H
#define QA_RECOVERY_H

#include "qa/demo.h"
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
bool qa_autosave_write(qa_autosave_state *, qa_fs_root *, const qa_save_image *,
                        uint64_t nonce, qa_error *);

typedef struct qa_recovery qa_recovery;
/* Recovery uses one shared checkpoint plus typed demo/journal records. Only
 * committed input/frame effects belong in the log. A record failure faults
 * recording and the valid prefix remains recoverable; it is never replayed
 * into the live application as an automatic retry. */
bool qa_recovery_begin(qa_fs_root *, const char *relative_name, const qa_save_image *,
                        qa_recovery **, qa_error *);
bool qa_recovery_append(qa_recovery *, qa_demo_record_kind, uint64_t elapsed_ns,
                         qa_net_protocol_id, qa_bytes, qa_error *);
bool qa_recovery_checkpoint(qa_recovery *, const qa_save_image *, qa_error *);
bool qa_recovery_close_clean(qa_recovery *, qa_error *);
void qa_recovery_destroy(qa_recovery *);
bool qa_recovery_restore(qa_fs_root *, const char *, void *, const qa_demo_seek_ops *,
                          uint64_t *reached_ns, qa_error *);

#endif
