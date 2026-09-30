#ifndef QA_AUDIO_BANK_GRAPH_SAVE_H
#define QA_AUDIO_BANK_GRAPH_SAVE_H
#include "qa/audio_bank_save.h"
typedef struct qa_audio_asset_inventory qa_audio_asset_inventory;
/* Captures all actual banks and every outside owned asset reference in one
 * graph. Inventory borrows the qualified cut; it does not retain live assets.
 * Pruned assets and shared PCM across banks remain distinct where the source
 * retained distinct heaps. Bank order is qualified by the frontend topology. */
bool qa_audio_asset_inventory_capture(qa_audio_bank *const *, size_t,
    qa_audio_asset *const *, size_t, qa_audio_asset_inventory **, qa_error *);
bool qa_audio_bank_graph_checkpoint(const qa_audio_asset_inventory *,
    const qa_audio_bank_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_audio_asset_inventory_index(const qa_audio_asset_inventory *, const qa_audio_asset *, uint64_t *);
qa_audio_asset *qa_audio_asset_inventory_at(const qa_audio_asset_inventory *, uint64_t);
/* Bank heaps must be empty. Restored inventory owns one construction reference
 * per asset; each real downstream holder retains its looked-up asset. ready
 * requires exact original total holder counts plus those construction refs.
 * Destroy after complete read-only qualification, before final recapture or
 * publication. Failed candidate teardown can destroy it independently. */
bool qa_audio_bank_graph_restore(qa_audio_bank *const *, size_t,
    const qa_audio_bank_checkpoint_refs *, qa_bytes, qa_audio_asset_inventory **, qa_error *);
bool qa_audio_asset_inventory_ready(const qa_audio_asset_inventory *, qa_error *);
void qa_audio_asset_inventory_destroy(qa_audio_asset_inventory *);
#endif
