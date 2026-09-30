#ifndef QA_AUDIO_SAVE_H
#define QA_AUDIO_SAVE_H
#include "qa/audio.h"

typedef enum qa_audio_reference_kind {
    QA_AUDIO_REFERENCE_ACTOR, QA_AUDIO_REFERENCE_OWNER, QA_AUDIO_REFERENCE_RESOURCE,
    QA_AUDIO_REFERENCE_BUS, QA_AUDIO_REFERENCE_KEY
} qa_audio_reference_kind;
/* The application supplies portable actor/provider/resource identities. Zero
 * and UINT64_MAX denote the audio API's empty/shared identities. Descriptors
 * returned by encode are owned buffers. decode resolves only candidate owners.
 * asset returns an owned exact-content asset, checked against the recorded PCM. */
typedef struct qa_audio_checkpoint_refs {
    void *context;
    bool (*encode)(void *, qa_audio_reference_kind, uint64_t, qa_buffer *, qa_error *);
    bool (*decode)(void *, qa_audio_reference_kind, qa_bytes, uint64_t *, qa_error *);
    bool (*asset)(void *, qa_audio_family, const char *, const qa_sha256_digest *,
                   qa_audio_asset **, qa_error *);
    qa_audio_transmission_fn geometry;
    void *geometry_context;
} qa_audio_checkpoint_refs;

bool qa_audio_mixer_checkpoint(const qa_audio_mixer *, const qa_audio_checkpoint_refs *,
                                qa_buffer *, qa_error *);
/* Restores an isolated mixer with borrowed candidate callbacks. No observer,
 * geometry, diagnostics, random or clock callback executes during restoration. */
bool qa_audio_mixer_restore(qa_bytes, const qa_audio_mixer_options *,
                            const qa_audio_checkpoint_refs *, qa_audio_mixer **, qa_error *);
bool qa_audio_reverb_checkpoint(const qa_audio_reverb *, qa_buffer *, qa_error *);
bool qa_audio_reverb_restore(qa_bytes, qa_audio_reverb **, qa_error *);

#endif
