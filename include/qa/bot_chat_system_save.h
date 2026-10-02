#ifndef QA_BOT_CHAT_SYSTEM_SAVE_H
#define QA_BOT_CHAT_SYSTEM_SAVE_H

#include "qa/bot_chat.h"
/* Runtime callers import the actual library MEMORY and asset pointer map
 * before this component resolves the source cache's 132-byte heap aliases. */

typedef struct qa_bot_chat_asset_save_refs {
    void *context;
    bool (*encode)(void *, const qa_bot_chat_asset *, uint64_t *, qa_error *);
    bool (*decode)(void *, uint64_t, qa_bot_chat_asset **borrowed, qa_error *);
} qa_bot_chat_asset_save_refs;
typedef struct qa_bot_chat_restored_states {
    qa_bot_chat **states;
    size_t count;
} qa_bot_chat_restored_states;

/* Qualified asset callbacks only read the actual shared asset registry. The
 * stable detached system must have no states or borrowed references. The
 * returned array indexes states in captured physical owner order; it owns only
 * the array, while the actual system owns each state. Free the array after
 * restoring runtime handles. All console cells/free links are preserved. */
bool qa_bot_chat_system_state_index(const qa_bot_chat_system *, const qa_bot_chat *, size_t *);
bool qa_bot_chat_system_capture(const qa_bot_chat_system *, const qa_bot_chat_asset_save_refs *,
                                qa_buffer *, qa_error *);
bool qa_bot_chat_system_restore_bytes(qa_bot_chat_system *, qa_bytes, const qa_bot_chat_asset_save_refs *,
                                      qa_bot_chat_restored_states *, qa_error *);
void qa_bot_chat_restored_states_free(qa_bot_chat_restored_states *);

#endif
