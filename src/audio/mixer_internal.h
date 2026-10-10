#ifndef QA_MIXER_INTERNAL_H
#define QA_MIXER_INTERNAL_H

#include "codec_internal.h"
#include "qa/pool.h"

/* These are source DMA/chunk dimensions, not limits on actors or voices. */
#define QA_MIXER_RAW_FRAMES QA_AUDIO_RAW_CAPACITY
#define QA_MIXER_PAINT_FRAMES 4096u
#define QA_MIXER_CHUNK_FRAMES 1024u

typedef struct qa_mixer_gain {
    double left, right;
} qa_mixer_gain;
typedef struct qa_mixer_prepared {
    struct qa_mixer_prepared *next;
    qa_audio_sample *sample, *pcm;
    qa_audio_asset *asset;
    bool q3;
    size_t references, slot;
    double *doppler_sums;
    size_t doppler_period;
} qa_mixer_prepared;
bool qa_mixer_prepared_doppler(qa_mixer_prepared *, qa_error *);

typedef enum qa_mixer_voice_state {
    QA_MIXER_FREE,
    QA_MIXER_PENDING,
    QA_MIXER_SCHEDULED,
    QA_MIXER_STARTED
} qa_mixer_voice_state;
typedef enum qa_mixer_role { QA_MIXER_EFFECT, QA_MIXER_STATIC, QA_MIXER_AMBIENT } qa_mixer_role;
typedef enum qa_mixer_notification {
    QA_MIXER_UNANNOUNCED,
    QA_MIXER_ANNOUNCED,
    QA_MIXER_FINISHED
} qa_mixer_notification;

typedef struct qa_mixer_voice {
    qa_mixer_voice_state state;
    qa_mixer_role role;
    qa_mixer_notification notification;
    qa_mixer_prepared *prepared;
    qa_audio_play sound;
    uint64_t id, channel, key, order, loop_start;
    int64_t start;
    int32_t allocated_at;
    double volume, attenuation, distance_offset, stereo_scale;
    bool unattenuated_mono;
    qa_mixer_gain gain;
    size_t next_free;
    size_t start_event, stop_event;
} qa_mixer_voice;

typedef struct qa_mixer_event {
    qa_audio_voice_event event;
    qa_mixer_prepared *prepared;
    size_t next;
} qa_mixer_event;

typedef struct qa_mixer_loop {
    qa_audio_loop request;
    qa_mixer_prepared *prepared;
    bool active, doppler, merged;
    qa_mixer_gain gain;
    float doppler_scale, old_doppler_scale;
} qa_mixer_loop;
typedef struct qa_mixer_loop_mix {
    qa_mixer_prepared *prepared;
    qa_mixer_gain gain;
    qa_game_family family;
    bool doppler;
    float doppler_scale, old_doppler_scale;
} qa_mixer_loop_mix;
typedef struct qa_mixer_position {
    uint64_t actor, owner;
    qa_vec3 origin;
    bool scoped;
} qa_mixer_position;
typedef struct qa_mixer_transmission {
    qa_vec3 origin;
    float gain;
} qa_mixer_transmission;

struct qa_audio_mixer {
    qa_audio_mixer_options options;
    qa_audio_listener listener;
    qa_audio_transmission_fn transmission;
    qa_audio_transmission_checked_fn transmission_checked;
    void *transmission_user;
    qa_mixer_voice *voices;
    size_t voice_count, voice_capacity, free_head;
    qa_arena prepared_storage;
    qa_pool prepared_records;
    qa_mixer_prepared **prepared_index;
    size_t prepared_index_capacity;
    qa_mixer_loop *loops;
    size_t loop_count, loop_capacity;
    qa_mixer_loop_mix *loop_mixes;
    size_t loop_mix_count, loop_mix_capacity;
    qa_mixer_position *positions;
    size_t position_count, position_capacity;
    qa_mixer_transmission *transmissions;
    size_t transmission_count, transmission_capacity;
    qa_mixer_event *events;
    size_t event_count, event_capacity, event_free_count, event_free, event_head, event_tail;
    char *diagnostic_message;
    size_t diagnostic_capacity;
    union {
        int64_t integer[QA_MIXER_PAINT_FRAMES * 2];
        double wide[QA_MIXER_PAINT_FRAMES * 2];
    } paint;
    int32_t raw[QA_MIXER_RAW_FRAMES * 2];
    uint64_t next_voice, schedule_order;
    int64_t paint_time, sound_time, raw_end;
    double source_begin_offset;
    float effects_gain;
    bool doppler_enabled, callback_active, enabled;
    bool dispatching, destroy_requested, destroying;
    bool round_locked, round_destroy_requested;
    uint32_t random_state;
};

/* Float destination for the engine DSP, with the same Source shift/clipping as PCM. */
bool qa_audio_mixer_mix_float(qa_audio_mixer *, float *, size_t, qa_error *);

bool qa_audio_mixer_round_lock(qa_audio_mixer *, qa_error *);
void qa_audio_mixer_round_stop(qa_audio_mixer *);
bool qa_audio_mixer_round_unlock(qa_audio_mixer *);

#endif
