#ifndef QA_AUDIO_ACOUSTICS_PREPARE_H
#define QA_AUDIO_ACOUSTICS_PREPARE_H
#include "qa/audio.h"

/* One owned structural scene reference. current and release are pure owner
 * bookkeeping: no clocks, callbacks, native queries or world mutation. trace
 * uses the actual listener scene and full pass-actor provenance, point/world
 * Q2 merged contents mask 3, including its real linked actor collision rows.
 * A source must outlive every trace; each successful transfer owns one release.
 * The portable owner descriptor is supplied by the actual scene constructor. */
typedef struct qa_audio_acoustics_source {
    void *context;
    bool (*current)(const void *);
    bool (*trace)(void *, const qa_audio_listener *, qa_vec3 start, qa_vec3 end,
        qa_audio_trace_hit *, qa_error *);
    void (*release)(void *);
} qa_audio_acoustics_source;
typedef struct qa_audio_engine_acoustics qa_audio_engine_acoustics;
/* Admits a child of this exact live gains ticket, sharing its locked engine
 * and all actual active/round mixers. Transfers source only on success.
 * A NULL source requires disabled; an enabled source cannot be fabricated. */
bool qa_audio_engine_acoustics_prepare(qa_audio_engine_gains *, bool enabled,
    const qa_audio_acoustics_source *, qa_audio_engine_acoustics **, qa_error *);
bool qa_audio_engine_acoustics_ready(const qa_audio_engine_acoustics *, qa_error *);
bool qa_audio_engine_acoustics_ready_is(const qa_audio_engine_acoustics *,
    const qa_audio_engine_gains *);
/* Requires ready; consumes this child before parent gains publication. */
void qa_audio_engine_acoustics_publish(qa_audio_engine_acoustics *);
/* A refusal retains both the source child and its actual gains parent. */
bool qa_audio_engine_acoustics_abort(qa_audio_engine_acoustics *, qa_error *);
/* Checked map retirement unbinds actual active/round mixers before dropping
 * the structural source. Desired enabled state remains for true map rebinding.
 * Ordinary source publication must install a new real receipt before playback. */
bool qa_audio_engine_acoustics_release(qa_audio_engine *, qa_error *);
bool qa_audio_engine_acoustics_enabled(const qa_audio_engine *);
/* Real runtime source binding at an idle audio boundary, after scene admission.
 * Transfers one reference only on success; no trace or playback runs. NULL
 * requires disabled and releases the old scene after mixer rebinding. */
bool qa_audio_engine_acoustics_bind(qa_audio_engine *, bool enabled,
    const qa_audio_acoustics_source *, qa_error *);

#endif
