#ifndef QA_FRONTEND_REMOTE_UNIFIED_MEDIA_SAVE_H
#define QA_FRONTEND_REMOTE_UNIFIED_MEDIA_SAVE_H
#include "remote_unified_media.h"
#include "world_inventory.h"
#include "qa/audio_bank_graph_save.h"

typedef struct frontend_unified_media_refs {
    qa_application_content_graph *content;
    frontend_scene_namespace *scene;
    frontend_model_inventory *models;
    frontend_world_inventory *roots;
    const qa_audio_asset_inventory *audio;
    /* Genuine physical media parent ordinal plus one, never a pointer key. */
    uint64_t owner;
} frontend_unified_media_refs;

/* Banks participate in the parent's real shared image/material/font/audio
 * dictionaries. This leaf preserves their physical topology, issued model
 * receipts, owning scene roots and Q3 numeric handle registries. */
bool frontend_unified_media_checkpoint(frontend_unified_media *,
    const frontend_unified_media_refs *, qa_buffer *, qa_error *);
/* Actual recipe catalog/views/resource holders precede this prefix. It creates
 * empty detached bank owners only; no product open, resource lookup, parser,
 * script load, image policy producer or Source constructor is replayed. */
bool frontend_unified_media_restore_prepare(qa_frontend *, qa_executable_recipe *,
    const frontend_unified_media_refs *, qa_bytes, frontend_unified_media **, qa_error *);
/* Complete imported models and QWON/QMON roots precede attachment. All unique
 * destructor edges qualify before any no-fail ownership transfer. */
bool frontend_unified_media_roots_attach(frontend_unified_media *,
    const frontend_unified_media_refs *, qa_error *);
/* Shared dictionaries and roots precede Q3AS; family continuation follows it.
 * Failure leaves an isolated candidate owned by ordinary media destruction. */
bool frontend_unified_media_restore_finish(frontend_unified_media *,
    const frontend_unified_media_refs *, qa_error *);
/* Stable row domain for a bank registry's actual model destructor edges. */
bool frontend_unified_media_q3_row(size_t bank, size_t model, uint64_t *);
#endif
