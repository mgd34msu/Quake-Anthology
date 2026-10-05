#ifndef QA_AUDIO_SAVE_H
#define QA_AUDIO_SAVE_H
#include "qa/audio.h"
#include "qa/audio_acoustics_prepare.h"

typedef enum qa_audio_reference_kind {
    QA_AUDIO_REFERENCE_ACTOR, QA_AUDIO_REFERENCE_OWNER, QA_AUDIO_REFERENCE_RESOURCE,
    QA_AUDIO_REFERENCE_BUS, QA_AUDIO_REFERENCE_KEY
} qa_audio_reference_kind;
/* The application supplies portable actor/provider/resource identities. Zero
 * and UINT64_MAX denote the audio API's empty/shared identities. Descriptors
 * returned by encode are owned buffers. decode resolves only candidate owners.
 * Asset descriptors preserve actual retained bank/voice heap aliases, beyond
 * names or content digests. Decode returns one owned exact candidate asset. */
typedef struct qa_audio_checkpoint_refs {
    void *context;
    bool (*encode)(void *, qa_audio_reference_kind, uint64_t, qa_buffer *, qa_error *);
    bool (*decode)(void *, qa_audio_reference_kind, qa_bytes, uint64_t *, qa_error *);
    bool (*asset_encode)(void *, const qa_audio_asset *, qa_buffer *, qa_error *);
    bool (*asset_decode)(void *, qa_bytes, qa_audio_asset **, qa_error *);
    /* Retained encoded stream sources belong to the content graph. Decode
     * returns a borrowed candidate resource; the stream retains its own lease. */
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *, uint64_t *, qa_error *);
    bool (*resource_decode)(void *, uint64_t, uint64_t, const qa_resource **, qa_error *);
    qa_audio_transmission_fn geometry;
    void *geometry_context;
    qa_audio_transmission_checked_fn geometry_checked;
    /* Exact retained scene topology. Decode creates one owned candidate
     * source reference, output unchanged on failure. No trace/native query. */
    bool (*acoustics_encode)(void *, const qa_audio_acoustics_source *, qa_buffer *, qa_error *);
    bool (*acoustics_decode)(void *, qa_bytes, qa_audio_acoustics_source *, qa_error *);
    bool (*environment)(void *, uint32_t seat, qa_audio_trace_fn *, void **, qa_error *);
} qa_audio_checkpoint_refs;

/* Borrowed installed bus owners. */
qa_audio_music *qa_audio_engine_bus_music(qa_audio_engine *, uint64_t bus);
qa_audio_raw_stream *qa_audio_engine_bus_stream(qa_audio_engine *, uint64_t bus);
/* Pure qualification of an actual installed music bus and its exact route.
 * The engine retains the music continuation; no decoder or callback runs. */
bool qa_audio_engine_music_ready(const qa_audio_engine *, uint64_t bus,
    uint32_t audience, float gain);

/* Read-only qualification of the actual restored cinematic queue and route. */
bool qa_audio_engine_raw_ready(const qa_audio_engine *, uint64_t bus,
    uint32_t audience, float gain, bool present, qa_error *);

#endif
