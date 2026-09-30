#ifndef QA_AUDIO_ENGINE_INTERNAL_H
#define QA_AUDIO_ENGINE_INTERNAL_H
#include "qa/audio.h"
typedef struct audio_seat {
    qa_audio_listener listener;
    qa_audio_mixer *mixer;
    qa_audio_reverb *reverb;
    qa_audio_underwater underwater;
    qa_audio_environment *environment;
} audio_seat;
typedef struct audio_position { uint64_t actor; qa_vec3 position; } audio_position;
typedef struct audio_bus {
    uint64_t id;
    uint32_t audience;
    float gain;
    qa_audio_raw_stream *raw;
    qa_audio_music *music;
} audio_bus;
struct qa_audio_engine {
    qa_audio_engine_options options;
    audio_seat **seats;
    size_t seat_count;
    audio_position *positions;
    size_t position_count, position_capacity;
    audio_bus *buses;
    size_t bus_count, bus_capacity;
    float *sum, *seat_scratch;
    int16_t *pcm_scratch;
    uint64_t clock, next_voice;
    double milliseconds;
    float effects_gain;
    bool paused, doppler, destroy_pending, destroying;
    unsigned operation_depth, callback_depth;
    qa_audio_transmission_fn geometry;
    void *geometry_user;
};
qa_audio_mixer_options qa_audio_engine_mixer_options(qa_audio_engine *engine);
void qa_audio_engine_discard(qa_audio_engine *engine);
#endif
