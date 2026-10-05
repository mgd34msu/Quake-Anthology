#ifndef QA_PERSISTENCE_SLOTS_H
#define QA_PERSISTENCE_SLOTS_H

#include "qa/save.h"
#include "qa/q2_save.h"

typedef enum qa_save_slot_format {
    QA_SAVE_SLOT_SHARED, QA_SAVE_SLOT_Q1_V5, QA_SAVE_SLOT_Q1_V6, QA_SAVE_SLOT_Q2_CLASSIC
} qa_save_slot_format;
/* Owned original header and exact source text values. Missing globals remain
 * NULL; these strings do not pretend to be interpreted QC numeric fields.
 * The final authored duplicate is retained, matching source import order. */
typedef struct qa_q1_save_slot_metadata {
    char *comment, *map, *game_directories, *world_message;
    char *killed_monsters, *total_monsters, *found_secrets, *total_secrets;
    double time;
    int32_t skill;
    size_t entity_count;
    bool player_record_present;
} qa_q1_save_slot_metadata;

typedef struct qa_q2_save_slot_metadata {
    char comment[QA_Q2_SAVE_COMMENT_BYTES];
    char map_command[QA_Q2_SAVE_MAP_COMMAND_BYTES];
    char game_directory[QA_Q2_SAVE_CVAR_BYTES];
} qa_q2_save_slot_metadata;

typedef struct qa_save_slot_entry {
    char *name; /* Contained path usable by the selected save codec. */
    qa_save_slot_format format;
    qa_save_metadata metadata; /* Valid only for QA_SAVE_SLOT_SHARED. */
    qa_q1_save_slot_metadata source; /* Owned only for original formats. */
    qa_q2_save_slot_metadata q2; /* Q2 engine header; GAME validates its files when loaded. */
    qa_error error;
} qa_save_slot_entry;

typedef struct qa_save_slot_listing {
    qa_save_slot_entry *entries;
    size_t count;
} qa_save_slot_listing;

/* Signature selection reads shared metadata or original source state. Q2
 * inspection reads server.ssv and observes its GAME file; the actual module
 * validates that opaque payload at load. Metadata does not establish installed
 * product/backend compatibility. Outputs remain
 * unchanged on failure; dispose previous source metadata before replacement. */
bool qa_save_slot_inspect(qa_fs_root *, const char *name, qa_save_slot_format *,
    qa_save_metadata *, qa_q1_save_slot_metadata *, qa_q2_save_slot_metadata *, qa_error *);
void qa_q1_save_slot_metadata_dispose(qa_q1_save_slot_metadata *);
/* Lists immediate regular .sav files and Q2 save directories. Empty selects
 * the root; a missing directory publishes an empty listing. Reserved current
 * names, links and invalid slot paths are excluded. Entries sort by exact path.
 * Unreadable or malformed files retain their own error and zero metadata.
 * Memory or enumeration failure leaves out unchanged. Free a previous listing
 * before replacement. The caller supplies its actual user save directory. */
bool qa_save_slots_list(qa_fs_root *, const char *directory, qa_save_slot_listing *, qa_error *);
void qa_save_slot_listing_free(qa_save_slot_listing *);

#endif
