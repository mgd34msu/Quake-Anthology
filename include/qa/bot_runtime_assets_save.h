#ifndef QA_BOT_RUNTIME_ASSETS_SAVE_H
#define QA_BOT_RUNTIME_ASSETS_SAVE_H

#include "qa/bot_runtime.h"

typedef enum qa_bot_saved_asset_kind {
    QA_BOT_SAVED_WEIGHTS, QA_BOT_SAVED_CHARACTER, QA_BOT_SAVED_WEAPONS,
    QA_BOT_SAVED_ITEMS, QA_BOT_SAVED_CHAT
} qa_bot_saved_asset_kind;
typedef struct qa_bot_saved_assets qa_bot_saved_assets;

/* Registry owns one retained reference per actual distinct asset, including
 * cache entries and assets referenced only by live runtime owners. Capture
 * returns this registry for encoding exact owner references. Restore preserves
 * source cache order and actual weight/weapon allocation aliases after MEMORY import,
 * and requires an empty detached library cache. Other private owners import
 * their references afterward. The native weight binding list is separate from
 * the genuine fuzzy store cache. The held weapon resource list includes
 * reached failed loads with their actual PC readers and pending acquisitions. */
bool qa_bot_runtime_assets_capture(const qa_bot_runtime *, qa_buffer *, qa_bot_saved_assets **, qa_error *);
bool qa_bot_runtime_assets_restore(qa_bot_runtime *, qa_bytes, qa_bot_saved_assets **, qa_error *);
/* Standalone goal owners capture their actual retained item/configuration
 * assets. Decode creates the registry without installing library caches. */
bool qa_bot_goals_assets_capture(const qa_bot_goals *, qa_buffer *, qa_bot_saved_assets **, qa_error *);
bool qa_bot_saved_assets_decode(qa_bytes, qa_bot_saved_assets **, qa_error *);
bool qa_bot_saved_asset_id(const qa_bot_saved_assets *, qa_bot_saved_asset_kind, const void *, uint64_t *, qa_error *);
bool qa_bot_saved_asset_resolve(const qa_bot_saved_assets *, qa_bot_saved_asset_kind, uint64_t, const void **, qa_error *);
void qa_bot_saved_assets_free(qa_bot_saved_assets *);

#endif
