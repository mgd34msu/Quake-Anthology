#ifndef QA_AUDIO_ENGINE_INTERNAL_H
#define QA_AUDIO_ENGINE_INTERNAL_H
#include "qa/audio.h"
#include "qa/audio_acoustics_prepare.h"
#include "qa/audio_music_engine.h"
typedef struct audio_seat {
    qa_audio_listener listener;
    qa_audio_mixer *mixer;
    qa_audio_reverb *reverb;
    qa_audio_underwater underwater;
    qa_audio_environment *environment;
} audio_seat;
typedef struct audio_position { uint64_t actor; qa_vec3 position; } audio_position;
typedef struct audio_round_mixer { uint32_t seat; qa_audio_mixer *mixer; } audio_round_mixer;
typedef struct audio_bus {
    uint64_t id;
    uint32_t audience;
    float gain;
    qa_audio_raw_stream *raw;
    qa_audio_music *music;
    qa_audio_music_lifetime lifetime;
    bool active;
} audio_bus;
struct qa_audio_engine {
    qa_audio_engine_options options;
    audio_seat **seats;
    size_t seat_count;
    audio_round_mixer *round_mixers;
    size_t round_mixer_count, round_mixer_capacity;
    audio_position *positions;
    size_t position_count, position_capacity;
    audio_bus *buses;
    size_t bus_count, bus_capacity;
    qa_audio_q3_operation *q3_operations;
    size_t q3_first, q3_count, q3_capacity;
    float *sum, *seat_scratch;
    uint64_t clock, next_voice;
    double milliseconds;
    float effects_gain, music_gain;
    bool paused, doppler, destroy_pending, destroying;
    bool round_resetting, round_destroy_requested;
    unsigned operation_depth, callback_depth;
    unsigned acoustics_readers;
    qa_audio_acoustics_source acoustics;
    bool acoustics_enabled;
    qa_audio_engine_gains *gains;
    qa_audio_transmission_fn geometry;
    void *geometry_user;
};
qa_audio_mixer_options qa_audio_engine_mixer_options(qa_audio_engine *engine);
bool qa_audio_engine_acoustics_transmit(void *, const qa_audio_listener *, qa_vec3,
    float *, qa_error *);
void qa_audio_engine_acoustics_rebind(qa_audio_engine *);
bool qa_audio_q3_operation_valid(const qa_audio_q3_operation *);
#endif
