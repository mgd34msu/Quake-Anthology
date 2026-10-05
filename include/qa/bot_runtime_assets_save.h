#ifndef QA_BOT_RUNTIME_ASSETS_SAVE_H
#define QA_BOT_RUNTIME_ASSETS_SAVE_H

#include "qa/bot_runtime.h"

typedef enum qa_bot_saved_asset_kind {
    QA_BOT_SAVED_WEIGHTS, QA_BOT_SAVED_CHARACTER, QA_BOT_SAVED_WEAPONS,
    QA_BOT_SAVED_ITEMS, QA_BOT_SAVED_CHAT
} qa_bot_saved_asset_kind;
typedef struct qa_bot_saved_assets qa_bot_saved_assets;

/* Installed recipes rebuild parsed library data. The registry retains actual
 * live references while typed fuzzy values and message cooldowns are saved. */
bool qa_bot_runtime_assets_capture(const qa_bot_runtime *, qa_buffer *, qa_bot_saved_assets **, qa_error *);
bool qa_bot_runtime_assets_restore(qa_bot_runtime *, qa_bytes, qa_bot_saved_assets **, qa_error *);
bool qa_bot_saved_asset_id(const qa_bot_saved_assets *, qa_bot_saved_asset_kind, const void *, uint64_t *, qa_error *);
bool qa_bot_saved_asset_resolve(const qa_bot_saved_assets *, qa_bot_saved_asset_kind, uint64_t, const void **, qa_error *);
void qa_bot_saved_assets_free(qa_bot_saved_assets *);

#endif
