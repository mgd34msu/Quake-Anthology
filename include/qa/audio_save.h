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
    bool (*environment)(void *, uint32_t seat, qa_audio_trace_fn *, void **, qa_error *);
} qa_audio_checkpoint_refs;

bool qa_audio_mixer_checkpoint(const qa_audio_mixer *, const qa_audio_checkpoint_refs *,
                                qa_buffer *, qa_error *);
/* Restores an isolated mixer with borrowed candidate callbacks. No observer,
 * geometry, diagnostics, random or clock callback executes during restoration. */
bool qa_audio_mixer_restore(qa_bytes, const qa_audio_mixer_options *,
                            const qa_audio_checkpoint_refs *, qa_audio_mixer **, qa_error *);
bool qa_audio_reverb_checkpoint(const qa_audio_reverb *, qa_buffer *, qa_error *);
bool qa_audio_reverb_restore(qa_bytes, qa_audio_reverb **, qa_error *);
uint32_t qa_audio_reverb_rate(const qa_audio_reverb *);
bool qa_audio_environment_definition_checkpoint(const qa_audio_environment *, qa_buffer *, qa_error *);
bool qa_audio_environments_restore(qa_bytes, qa_audio_environments **, qa_error *);
bool qa_audio_environment_checkpoint(const qa_audio_environment *, qa_buffer *, qa_error *);
bool qa_audio_environment_restore(qa_bytes, const qa_audio_environments *, qa_audio_trace_fn,
                                   void *, qa_audio_environment **, qa_error *);
bool qa_audio_engine_checkpoint(const qa_audio_engine *, const qa_audio_checkpoint_refs *,
                                 qa_buffer *, qa_error *);
bool qa_audio_engine_restore(qa_bytes, const qa_audio_engine_options *,
                             const qa_audio_checkpoint_refs *, qa_audio_engine **, qa_error *);
/* Import into the actual empty candidate heap used by source presentation
 * factories. Keeps its address and options bindings; nested engine allocator
 * and observer contexts follow that heap. No callback or source action runs. */
bool qa_audio_engine_restore_into(qa_audio_engine *, qa_bytes,
    const qa_audio_checkpoint_refs *, qa_error *);
/* Borrowed bus owners allow candidate source leases to reconnect without
 * replacing the restored queues or decoder phase. */
qa_audio_music *qa_audio_engine_bus_music(qa_audio_engine *, uint64_t bus);
qa_audio_raw_stream *qa_audio_engine_bus_stream(qa_audio_engine *, uint64_t bus);

/* Read-only qualification of a separately restored cinematic queue and route.
 * An empty descriptor qualifies the actual absence of a raw queue. */
bool qa_audio_engine_raw_checkpoint_ready(const qa_audio_engine *, uint64_t bus,
    uint32_t audience, float gain, qa_bytes checkpoint, qa_error *);

#endif
