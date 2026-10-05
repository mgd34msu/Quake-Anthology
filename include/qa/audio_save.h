#ifndef QA_AUDIO_SAVE_H
#define QA_AUDIO_SAVE_H
#include "qa/audio.h"
#include "qa/audio_acoustics_prepare.h"

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
