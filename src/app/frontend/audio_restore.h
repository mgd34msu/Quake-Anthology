#ifndef QA_FRONTEND_AUDIO_RESTORE_H
#define QA_FRONTEND_AUDIO_RESTORE_H
#include "audio_inventory.h"
#include "qa/persistence_content.h"
bool frontend_audio_content_visit(const qa_frontend *, const qa_application_content_visitor *, qa_error *);
/* Complete real bank roster plus outside asset holders. Capture returns a
 * borrowed cut for subsequent asset descriptor writers. Restore returns owned
 * construction refs for exact downstream engine/event/source handle imports.
 * Callers finish all holders and qualify inventory_ready before destroying the
 * inventory, then recapture or publish. Content resolvers remain read-only. */
bool frontend_audio_banks_checkpoint(qa_frontend *, const qa_audio_bank_checkpoint_refs *,
    qa_audio_asset_inventory **, qa_buffer *, qa_error *);
bool frontend_audio_banks_restore(qa_frontend *, const qa_audio_bank_checkpoint_refs *,
    qa_bytes, qa_audio_asset_inventory **, qa_error *);
/* Callback context is the actual inventory. Decode returns one retained ref. */
bool frontend_audio_asset_encode(void *, const qa_audio_asset *, qa_buffer *, qa_error *);
bool frontend_audio_asset_decode(void *, qa_bytes, qa_audio_asset **, qa_error *);
#endif
