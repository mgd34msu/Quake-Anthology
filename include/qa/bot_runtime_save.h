#ifndef QA_BOT_RUNTIME_SAVE_H
#define QA_BOT_RUNTIME_SAVE_H

#include "qa/bot_runtime.h"
#include "qa/session.h"

typedef struct qa_bot_runtime_saved_map {
    const char *name;
    bool entities, source, navigation;
    size_t source_bytes;
} qa_bot_runtime_saved_map;

/* Inspection owns name and validates the complete enclosing record extent.
 * Map content/navigation are qualified by the application before restore.
 * Restore requires its actual newly constructed detached runtime, preserving
 * that address and service contexts. It imports actual owners and handles
 * without setup, map loading, admission, source commands or random calls.
 * A failed import can retain partial state only inside this isolated candidate;
 * the application must discard or retain the entire candidate for teardown. */
bool qa_bot_runtime_save_map_read(qa_bytes, qa_bot_runtime_saved_map *, qa_error *);
void qa_bot_runtime_saved_map_free(qa_bot_runtime_saved_map *);
bool qa_bot_runtime_save_capture(qa_session *, const qa_bot_runtime *, qa_buffer *, qa_error *);
bool qa_bot_runtime_save_restore(qa_session *, qa_bot_runtime *, qa_bytes,
                                 const qa_bot_runtime_map *qualified_map, qa_error *);

#endif
