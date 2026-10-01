#ifndef QA_AUDIO_DEVICE_SAVE_H
#define QA_AUDIO_DEVICE_SAVE_H
#include "qa/audio.h"
typedef struct qa_audio_device_restore_guard qa_audio_device_restore_guard;
/* Captures the genuine device PCM/converter/pump continuation. Temporarily
 * freezes native consumption, observes the actual consumed prefix, then
 * resumes the same endpoint without counting the capture pause as playback.
 * The enclosing frontend keeps this device and its engine alive and idle. */
bool qa_audio_device_checkpoint(qa_audio_device *, const qa_audio_engine *, qa_buffer *, qa_error *);
/* Pure detached reconstruction, including the exact saved conversion phase.
 * Native output stays solely active-owned until the qualified nofail handoff.
 * A guard borrows both devices through publication or failed-candidate cleanup. */
bool qa_audio_device_restore(qa_bytes, const qa_audio_device *active,
    qa_audio_engine *candidate_engine, qa_audio_device **,
    qa_audio_device_restore_guard **, qa_error *);
/* Fresh PCM/converter/pump owner with the genuine active native format and
 * endpoint authority. It submits no old PCM and opens no device until prepare. */
bool qa_audio_device_create_detached(const qa_audio_device *,qa_audio_engine *,
    qa_audio_device **,qa_audio_device_restore_guard **,qa_error *);
bool qa_audio_device_handoff_ready(const qa_audio_device_restore_guard *, qa_error *);
/* Last fallible native preparation: opens a distinct paused endpoint and
 * submits exactly the saved native PCM prefix. Failure leaves active intact. */
bool qa_audio_device_handoff_prepare(qa_audio_device_restore_guard *, qa_error *);
/* Encodes the restored private owner with the guard's qualified saved native
 * cut. The candidate remains detached; no queue or playback clock is queried. */
bool qa_audio_device_restore_checkpoint(const qa_audio_device_restore_guard *, qa_buffer *, qa_error *);
/* Pauses the displaced endpoint and starts the prepared candidate endpoint.
 * Displaced destruction closes its own old endpoint later.
 * This operation allocates nothing and calls no source or mixer callback. */
void qa_audio_device_handoff(qa_audio_device_restore_guard *);
void qa_audio_device_restore_guard_destroy(qa_audio_device_restore_guard *);
#endif
