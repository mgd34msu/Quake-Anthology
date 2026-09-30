#ifndef QA_PERSISTENCE_SLOTS_H
#define QA_PERSISTENCE_SLOTS_H

#include "qa/save.h"

typedef struct qa_save_slot_entry {
    char *name; /* Contained path usable by qa_save_read and qa_save_write. */
    qa_save_metadata metadata;
    qa_error error; /* QA_OK means the complete save envelope was verified. */
} qa_save_slot_entry;

typedef struct qa_save_slot_listing {
    qa_save_slot_entry *entries;
    size_t count;
} qa_save_slot_listing;

/* Reads and validates the complete image, including its digest and owner set.
 * Metadata does not establish compatibility with currently installed content.
 * Outputs remain unchanged on failure. */
bool qa_save_slot_inspect(qa_fs_root *, const char *name, qa_save_metadata *, qa_error *);
/* Lists immediate regular .sav files in a contained directory. Empty selects
 * the root; a missing directory publishes an empty listing. Reserved current
 * names, links and invalid slot paths are excluded. Entries sort by exact path.
 * Unreadable or malformed files retain their own error and zero metadata.
 * Memory or enumeration failure leaves out unchanged. Free a previous listing
 * before replacement. The caller supplies its actual user save directory. */
bool qa_save_slots_list(qa_fs_root *, const char *directory, qa_save_slot_listing *, qa_error *);
void qa_save_slot_listing_free(qa_save_slot_listing *);

#endif
