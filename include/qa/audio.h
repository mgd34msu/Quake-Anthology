/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_AUDIO_H
#define QA_AUDIO_H

#include "qa/common.h"
#include "qa/math.h"
#include "qa/vfs.h"
#include <stdatomic.h>

#define QA_AUDIO_NO_LOOP UINT64_MAX
#define QA_AUDIO_WORLD UINT32_MAX
#define QA_AUDIO_NO_ACTOR UINT64_MAX
#define QA_AUDIO_NO_OWNER UINT64_MAX
#define QA_AUDIO_RAW_CAPACITY 16384

typedef enum qa_audio_family { QA_AUDIO_Q1, QA_AUDIO_Q2, QA_AUDIO_Q3 } qa_audio_family;
typedef enum qa_audio_wav_policy { QA_WAV_FORMAT, QA_WAV_QUAKE, QA_WAV_Q3 } qa_audio_wav_policy;
/* PCM storage is immutable after publication and shared by atomic references.
 * Voices may share a retained prepared resource; streams retain their source.
 * samples are interleaved signed16. */
typedef struct qa_audio_sample {
    uint32_t sample_rate;
    unsigned channels, source_bytes_per_sample;
    uint64_t frame_count, loop_start;
    const int16_t *samples;
    atomic_uint references;
} qa_audio_sample;
qa_audio_sample *qa_audio_sample_retain(qa_audio_sample *sample);
void qa_audio_sample_release(qa_audio_sample *sample);
bool qa_audio_sample_copy(const int16_t *samples, uint64_t frames, unsigned channels, uint32_t rate,
                          uint64_t loop_start, qa_audio_sample **out, qa_error *error);
bool qa_audio_decode_wav(qa_bytes bytes, qa_audio_wav_policy policy, qa_audio_sample **out,
                         qa_error *error);
bool qa_audio_decode(qa_bytes bytes, qa_audio_wav_policy policy, qa_audio_sample **out,
                     qa_error *error);
/* Compressed compatibility blocks are decoded once into shared sample
 * resources. Wavelet calls operate on one source block, at most 2048 samples.
 */
typedef struct qa_audio_adpcm_state {
    int predictor, step_index;
} qa_audio_adpcm_state;
bool qa_audio_adpcm_decode(qa_bytes bytes, size_t samples, qa_audio_adpcm_state *state,
                           int16_t *out, size_t capacity, qa_error *error);
bool qa_audio_adpcm_encode(const int16_t *samples, size_t count, qa_audio_adpcm_state *state,
                           qa_buffer *out, qa_error *error);
bool qa_audio_wavelet_decode(qa_bytes bytes, size_t samples, int16_t *out, size_t capacity,
                             qa_error *error);
bool qa_audio_wavelet_encode(const int16_t *samples, size_t count, qa_buffer *out, qa_error *error);
int16_t qa_audio_mulaw_decode(uint8_t byte);
uint8_t qa_audio_mulaw_encode(int16_t sample);
bool qa_audio_resample_source(const qa_audio_sample *sample, uint32_t output_rate,
                              qa_audio_family family, qa_audio_sample **out, qa_error *error);

typedef struct qa_audio_stream qa_audio_stream;
/* Open from bytes borrows the input until close; callers retain mount
 * resources. read writes caller-owned PCM; zero frames denotes EOF, errors
 * return false. */
bool qa_audio_stream_open(qa_bytes bytes, qa_audio_wav_policy policy, qa_audio_stream **out,
                          qa_error *error);
/* On success takes the resource lease; release(owner) runs exactly once at
 * close. On failure the caller still owns the lease. Source bytes are never
 * recopied. */
bool qa_audio_stream_open_retained(qa_bytes bytes, qa_audio_wav_policy policy,
                                   void (*release)(void *owner), void *owner, qa_audio_stream **out,
                                   qa_error *error);
bool qa_audio_stream_from_sample(qa_audio_sample *sample, qa_audio_stream **out, qa_error *error);
uint32_t qa_audio_stream_rate(const qa_audio_stream *stream);
unsigned qa_audio_stream_channels(const qa_audio_stream *stream);
uint64_t qa_audio_stream_frames(const qa_audio_stream *stream);
uint64_t qa_audio_stream_position(const qa_audio_stream *stream);
bool qa_audio_stream_read(qa_audio_stream *stream, int16_t *out, size_t capacity_frames,
                          size_t *frames, qa_error *error);
bool qa_audio_stream_seek(qa_audio_stream *stream, uint64_t frame, qa_error *error);
void qa_audio_stream_close(qa_audio_stream *stream);

typedef struct qa_audio_bank qa_audio_bank;
typedef struct qa_audio_asset qa_audio_asset;
/* A bank borrows its live content view. Assets retain source leases and PCM,
 * surviving bank pruning/destruction. Register returns an owned asset
 * reference; a missing optional asset succeeds with *out == NULL. Calls are
 * serialized; final asset/stream release follows the VFS owner-thread contract. */
bool qa_audio_bank_create(qa_vfs *view, qa_audio_bank **out, qa_error *error);
void qa_audio_bank_destroy(qa_audio_bank *bank);
void qa_audio_bank_begin(qa_audio_bank *bank);
void qa_audio_bank_end(qa_audio_bank *bank);
void qa_audio_bank_clear(qa_audio_bank *bank);
bool qa_audio_bank_register(qa_audio_bank *bank, const char *name, qa_audio_family family,
                            qa_audio_asset **out, qa_error *error);
bool qa_audio_bank_sexed(qa_audio_bank *bank, const char *base, const char *model,
                         qa_audio_asset **out, qa_error *error);
/* Borrowed cached entry, or NULL; acquiring ownership requires retain. */
qa_audio_asset *qa_audio_bank_get(const qa_audio_bank *bank, uint64_t resource_id,
                                  qa_audio_family family);
qa_audio_asset *qa_audio_asset_retain(qa_audio_asset *asset);
void qa_audio_asset_release(qa_audio_asset *asset);
qa_audio_sample *qa_audio_asset_sample(const qa_audio_asset *asset);
qa_resource *qa_audio_asset_resource(const qa_audio_asset *asset);
qa_mount_id qa_audio_asset_mount(const qa_audio_asset *asset);
const char *qa_audio_asset_name(const qa_audio_asset *asset);
qa_audio_family qa_audio_asset_family(const qa_audio_asset *asset);
/* accept checks the selected resource's mount after resolution, never changing
 * search precedence. NULL accepts all. Returned stream owns its resource lease.
 */
bool qa_audio_bank_music(qa_audio_bank *bank, const char *path, qa_vfs_accept_mount accept,
                         void *context, qa_audio_stream **out, qa_error *error);

typedef struct qa_audio_raw_stream qa_audio_raw_stream;
bool qa_audio_raw_create(uint32_t output_rate, qa_audio_raw_stream **out, qa_error *error);
void qa_audio_raw_destroy(qa_audio_raw_stream *stream);
/* queue copies immutable source chunks, retaining fractional phase across them.
 */
bool qa_audio_raw_queue(qa_audio_raw_stream *stream, const int16_t *samples, size_t frames,
                        unsigned channels, uint32_t rate, uint64_t source_frame, bool reset,
                        qa_error *error);
bool qa_audio_raw_set_rate(qa_audio_raw_stream *stream, uint32_t rate, qa_error *error);
void qa_audio_raw_pause(qa_audio_raw_stream *stream, bool paused);
uint64_t qa_audio_raw_position(const qa_audio_raw_stream *stream);
uint64_t qa_audio_raw_queued(const qa_audio_raw_stream *stream);
uint32_t qa_audio_raw_rate(const qa_audio_raw_stream *stream);
bool qa_audio_raw_checkpoint(const qa_audio_raw_stream *stream, qa_buffer *out, qa_error *error);
bool qa_audio_raw_restore(qa_bytes checkpoint, uint32_t output_rate, qa_audio_raw_stream **out,
                          qa_error *error);
/* Adds to a zero-initialized or existing stereo float bus. Returns frames
 * mixed. */
size_t qa_audio_raw_mix(qa_audio_raw_stream *stream, float *stereo, size_t frames, float gain);

typedef struct qa_audio_music qa_audio_music;
typedef bool (*qa_audio_open_track_fn)(void *user, const char *path, qa_audio_stream **out,
                                       qa_error *error);
bool qa_audio_music_create(uint32_t rate, qa_audio_family family, bool source_volume,
                           qa_audio_music **out, qa_error *error);
void qa_audio_music_destroy(qa_audio_music *music);
/* Takes ownership of intro and loop; intro==loop is valid. */
void qa_audio_music_start(qa_audio_music *music, qa_audio_stream *intro, qa_audio_stream *loop);
void qa_audio_music_stop(qa_audio_music *music);
void qa_audio_music_pause(qa_audio_music *music, bool paused);
void qa_audio_music_enable(qa_audio_music *music, bool enabled);
bool qa_audio_music_volume(qa_audio_music *music, float volume, qa_error *error);
void qa_audio_music_update(qa_audio_music *music);
bool qa_audio_music_mix(qa_audio_music *music, float *stereo, size_t frames, qa_error *error);
bool qa_audio_music_playing(const qa_audio_music *music);
uint32_t qa_audio_music_rate(const qa_audio_music *music);
uint64_t qa_audio_music_completions(const qa_audio_music *music);
bool qa_audio_music_remap(qa_audio_music *music, const uint8_t *tracks, size_t count,
                          qa_error *error);
void qa_audio_music_reset(qa_audio_music *music);
bool qa_audio_music_cd_play(qa_audio_music *music, unsigned track, bool loop,
                            qa_audio_open_track_fn open, void *user, qa_error *error);
unsigned qa_audio_music_q2_track(unsigned track, const char *campaign, bool remastered);

typedef enum qa_audio_origin_kind {
    QA_AUDIO_LOCAL,
    QA_AUDIO_FIXED,
    QA_AUDIO_ACTOR
} qa_audio_origin_kind;
typedef struct qa_audio_listener {
    uint32_t seat;
    uint64_t actor;
    qa_vec3 origin, axis[3];
    float gain;
    bool underwater;
} qa_audio_listener;
typedef struct qa_audio_play {
    qa_audio_sample *sample;
    qa_audio_asset *asset; /* Optional retained provenance; its PCM must match sample. */
    uint64_t resource_id;  /* Optional shared resource identity for observers. */
    const char *name;      /* Optional diagnostic name, borrowed only during play. */
    qa_audio_family family;
    uint64_t actor, owner; /* owner isolates providers; NO_OWNER is shared/unowned. */
    uint32_t audience;     /* QA_AUDIO_WORLD or one listener seat. */
    qa_audio_origin_kind origin_kind;
    uint64_t origin_actor; /* Actor supplying position when origin_kind is ACTOR. */
    qa_vec3 origin;
    int32_t channel;
    float volume, attenuation;
    double delay_seconds, server_milliseconds;
    bool has_server_time;
} qa_audio_play;
typedef struct qa_audio_loop {
    qa_audio_play sound;
    qa_vec3 velocity;
    int32_t frame_number;
    bool persistent;
} qa_audio_loop;
typedef enum qa_audio_stop_reason {
    QA_AUDIO_ENDED,
    QA_AUDIO_STOPPED,
    QA_AUDIO_REPLACED
} qa_audio_stop_reason;
typedef struct qa_audio_voice_event {
    bool started;
    uint32_t seat;
    uint64_t voice_id;
    uint64_t resource_id;
    int64_t output_frame;
    uint32_t sample_rate;
    double source_offset_seconds;
    qa_audio_sample *sample; /* Borrowed for callback duration. */
    qa_audio_asset *asset;   /* Retained provenance, borrowed for callback duration. */
    qa_audio_stop_reason reason;
} qa_audio_voice_event;
typedef void (*qa_audio_voice_observer)(void *user, const qa_audio_voice_event *event);
/* Voice notifications may schedule or stop sounds. Notifications are queued
 * until mixer mutation completes; event.sample lives through the callback.
 * Engine observers must defer listener replacement and recursive mixing.
 * Destroy during a notification is deferred until the active operation ends.
 * Query/random/geometry/diagnostic callbacks are read-only for their engine. */
typedef uint32_t (*qa_audio_random_fn)(void *user);
typedef int32_t (*qa_audio_milliseconds_fn)(void *user);
typedef int (*qa_audio_setting_fn)(void *user, const char *name);
typedef void (*qa_audio_log_fn)(void *user, const char *message);
typedef float (*qa_audio_transmission_fn)(void *user, const qa_audio_listener *listener,
                                          qa_vec3 source);
typedef struct qa_audio_mixer qa_audio_mixer;
typedef struct qa_audio_mixer_options {
    uint32_t sample_rate;
    unsigned output_channels;
    size_t initial_voices;
    qa_audio_random_fn random;
    void *random_user;
    qa_audio_milliseconds_fn milliseconds;
    void *milliseconds_user;
    qa_audio_voice_observer observer;
    void *observer_user;
    qa_audio_setting_fn setting;
    qa_audio_log_fn log;
    void *diagnostic_user;
    uint64_t (*allocate_voice_id)(void *user);
    void *voice_id_user;
} qa_audio_mixer_options;
bool qa_audio_mixer_create(const qa_audio_mixer_options *options, qa_audio_mixer **out,
                           qa_error *error);
void qa_audio_mixer_destroy(qa_audio_mixer *mixer);
bool qa_audio_mixer_listener(qa_audio_mixer *mixer, const qa_audio_listener *listener,
                             qa_error *error);
bool qa_audio_mixer_position(qa_audio_mixer *mixer, uint64_t actor, qa_vec3 origin,
                             qa_error *error);
/* Q3 providers may override positions per seat without changing shared actors.
 */
bool qa_audio_mixer_position_owner(qa_audio_mixer *mixer, uint64_t actor, uint64_t owner,
                                   qa_vec3 origin, qa_error *error);
void qa_audio_mixer_geometry(qa_audio_mixer *mixer, qa_audio_transmission_fn fn, void *user);
void qa_audio_mixer_effects_gain(qa_audio_mixer *mixer, float gain);
void qa_audio_mixer_doppler(qa_audio_mixer *mixer, bool enabled);
/* Accepted=false is normal source-policy suppression, not an API error. */
bool qa_audio_mixer_play(qa_audio_mixer *mixer, const qa_audio_play *sound, int32_t milliseconds,
                         bool *accepted, qa_error *error);
bool qa_audio_mixer_loop(qa_audio_mixer *mixer, const qa_audio_loop *loop, qa_error *error);
bool qa_audio_mixer_end_loop_frame(qa_audio_mixer *mixer, qa_error *error);
void qa_audio_mixer_clear_loops(qa_audio_mixer *mixer, bool all);
void qa_audio_mixer_stop_loop(qa_audio_mixer *mixer, uint64_t actor, uint64_t owner);
void qa_audio_mixer_clear_seat_loops(qa_audio_mixer *mixer, uint64_t owner, bool all);
void qa_audio_mixer_stop_seat_loop(qa_audio_mixer *mixer, uint64_t actor, uint64_t owner);
void qa_audio_mixer_stop_channel(qa_audio_mixer *mixer, uint64_t actor, uint64_t owner,
                                 qa_audio_family family, int32_t channel);
void qa_audio_mixer_stop_actor(qa_audio_mixer *mixer, uint64_t actor, uint64_t owner);
void qa_audio_mixer_stop_owner(qa_audio_mixer *mixer, uint64_t owner);
void qa_audio_mixer_stop_all(qa_audio_mixer *mixer);
bool qa_audio_mixer_static(qa_audio_mixer *mixer, uint64_t key, qa_audio_sample *sample,
                           qa_vec3 origin, float volume, float attenuation, qa_error *error);
void qa_audio_mixer_remove_static(qa_audio_mixer *mixer, uint64_t key);
bool qa_audio_mixer_ambient(qa_audio_mixer *mixer, qa_audio_sample *sounds[2],
                            const uint8_t levels[2], float elapsed_seconds, float level, float fade,
                            qa_error *error);
/* Source raw path preserves Q3 byte signedness and circular DMA policies. */
bool qa_audio_mixer_raw(qa_audio_mixer *mixer, qa_bytes bytes, size_t frames, unsigned sample_bytes,
                        unsigned channels, uint32_t rate, float volume, qa_error *error);
void qa_audio_mixer_clear_raw(qa_audio_mixer *mixer);
bool qa_audio_mixer_select_time(qa_audio_mixer *mixer, uint64_t delivered_frame,
                                int64_t paint_frame, qa_error *error);
int64_t qa_audio_mixer_clock(const qa_audio_mixer *mixer);
/* Reuses internal paint storage, writes caller-owned stereo PCM, advances time.
 */
bool qa_audio_mixer_mix(qa_audio_mixer *mixer, int16_t *stereo, size_t frames, qa_error *error);
/* Guest/source adapters may drive paint and delivery clocks independently. */
bool qa_audio_mixer_paint(qa_audio_mixer *mixer, int64_t start_frame, int16_t *stereo,
                          size_t frames, qa_error *error);
bool qa_audio_mixer_scan_starts(qa_audio_mixer *mixer);
bool qa_audio_mixer_rebase(qa_audio_mixer *mixer, uint64_t delivered_frame, qa_error *error);
void qa_audio_mixer_clear_buffer(qa_audio_mixer *mixer);
int64_t qa_audio_mixer_raw_end(const qa_audio_mixer *mixer);
int32_t *qa_audio_mixer_raw_samples(qa_audio_mixer *mixer); /* 2*RAW_CAPACITY, borrowed. */
void qa_audio_mixer_reset_raw(qa_audio_mixer *mixer, bool stopped);
void qa_audio_mixer_enable(qa_audio_mixer *mixer, bool enabled);
uint64_t qa_audio_mixer_sound_clock(const qa_audio_mixer *mixer);
typedef struct qa_audio_channel_volume {
    const qa_audio_sample *sample;
    double left, right;
} qa_audio_channel_volume;
/* Returns total matching channels; fills at most capacity entries. */
size_t qa_audio_mixer_channel_volumes(const qa_audio_mixer *mixer, qa_audio_channel_volume *out,
                                      size_t capacity);

typedef struct qa_audio_reverb_params {
    float density, diffusion, gain, gain_hf, gain_lf;
    float decay_time, decay_hf_ratio, decay_lf_ratio;
    float reflections_gain, reflections_delay, late_gain, late_delay;
    float echo_time, echo_depth, modulation_time, modulation_depth;
    float air_absorption_gain_hf, hf_reference, lf_reference, room_rolloff;
    bool decay_hf_limit;
} qa_audio_reverb_params;
typedef struct qa_audio_reverb qa_audio_reverb;
bool qa_audio_reverb_create(uint32_t rate, qa_audio_reverb **out, qa_error *error);
void qa_audio_reverb_destroy(qa_audio_reverb *reverb);
void qa_audio_reverb_reset(qa_audio_reverb *reverb);
void qa_audio_reverb_process(qa_audio_reverb *reverb, float *stereo, size_t frames,
                             const qa_audio_reverb_params *params);
typedef struct qa_audio_underwater {
    float z1[2], z2[2];
} qa_audio_underwater;
void qa_audio_underwater_process(qa_audio_underwater *state, uint32_t rate, float *stereo,
                                 size_t frames, float hf_gain);
typedef struct qa_audio_trace_hit {
    float fraction;
    bool start_solid, all_solid, sky;
    qa_vec3 end;
    const char *material;
} qa_audio_trace_hit;
typedef qa_audio_trace_hit (*qa_audio_trace_fn)(void *user, qa_vec3 start, qa_vec3 end,
                                                qa_vec3 mins, qa_vec3 maxs);
float qa_audio_geometry_transmission(qa_vec3 start, qa_vec3 end, qa_audio_trace_fn trace,
                                     void *user);
size_t qa_audio_reverb_preset_count(void);
const char *qa_audio_reverb_preset_name(size_t index);
const qa_audio_reverb_params *qa_audio_reverb_preset(size_t index);
typedef struct qa_audio_environments qa_audio_environments;
typedef struct qa_audio_environment qa_audio_environment;
bool qa_audio_environments_parse(qa_bytes json, qa_audio_environments **out, qa_error *error);
bool qa_audio_environments_parse_ex(qa_bytes json, qa_audio_log_fn warning, void *warning_user,
                                    qa_audio_environments **out, qa_error *error);
void qa_audio_environments_destroy(qa_audio_environments *environments);
/* Selectors retain immutable configuration until destroyed. */
bool qa_audio_environment_create(const qa_audio_environments *environments, qa_audio_trace_fn trace,
                                 void *user, qa_audio_environment **out, qa_error *error);
void qa_audio_environment_destroy(qa_audio_environment *environment);
void qa_audio_environment_update(qa_audio_environment *environment, qa_vec3 origin,
                                 double milliseconds);
void qa_audio_environment_enable(qa_audio_environment *environment, bool enabled);
void qa_audio_environment_lerp(qa_audio_environment *environment, float seconds);
const qa_audio_reverb_params *qa_audio_environment_params(const qa_audio_environment *environment);

/* A single producer and single consumer exchange interleaved stereo frames.
 * No allocations or locks are needed by read/write; only reset requires
 * quiescence. Capacity is rounded up to a power of two and is at least
 * capacity_frames. */
typedef struct qa_audio_ring qa_audio_ring;
bool qa_audio_ring_create(size_t capacity_frames, qa_audio_ring **out, qa_error *error);
void qa_audio_ring_destroy(qa_audio_ring *ring);
void qa_audio_ring_reset(qa_audio_ring *ring);
size_t qa_audio_ring_queued(const qa_audio_ring *ring);
size_t qa_audio_ring_write(qa_audio_ring *ring, const int16_t *stereo, size_t frames);
size_t qa_audio_ring_read(qa_audio_ring *ring, int16_t *stereo, size_t frames);
typedef struct qa_audio_output_format {
    uint32_t sample_rate;
    unsigned channels, sample_bits;
} qa_audio_output_format;
bool qa_audio_output_encode(const int16_t *stereo, size_t frames, qa_audio_output_format format,
                            void *out, size_t capacity_bytes, size_t *written, qa_error *error);

/* Stateful engine, mixer, music and stream operations require application
 * serialization. Sample ownership and PCM ring exchange support separate threads.
 */
typedef struct qa_audio_engine qa_audio_engine;
typedef struct qa_audio_engine_options {
    uint32_t sample_rate;
    unsigned output_channels;
    size_t mix_frames, initial_voices;
    qa_audio_random_fn random;
    void *random_user;
    qa_audio_milliseconds_fn milliseconds;
    void *milliseconds_user;
    qa_audio_voice_observer observer;
    void *observer_user;
    qa_audio_setting_fn setting;
    qa_audio_log_fn log;
    void *diagnostic_user;
} qa_audio_engine_options;
bool qa_audio_engine_create(const qa_audio_engine_options *options, qa_audio_engine **out,
                            qa_error *error);
void qa_audio_engine_destroy(qa_audio_engine *engine);
bool qa_audio_engine_listeners(qa_audio_engine *engine, const qa_audio_listener *listeners,
                               size_t count, qa_error *error);
bool qa_audio_engine_position(qa_audio_engine *engine, uint64_t actor, qa_vec3 origin,
                              qa_error *error);
void qa_audio_engine_geometry(qa_audio_engine *engine, qa_audio_transmission_fn fn, void *user);
void qa_audio_engine_pause(qa_audio_engine *engine, bool paused);
void qa_audio_engine_gain(qa_audio_engine *engine, float gain);
void qa_audio_engine_doppler(qa_audio_engine *engine, bool enabled);
bool qa_audio_engine_play(qa_audio_engine *engine, const qa_audio_play *sound, int32_t milliseconds,
                          qa_error *error);
bool qa_audio_engine_loop(qa_audio_engine *engine, const qa_audio_loop *loop, qa_error *error);
void qa_audio_engine_clear_loops(qa_audio_engine *engine, bool all);
void qa_audio_engine_stop_channel(qa_audio_engine *engine, uint64_t actor, uint64_t owner,
                                  qa_audio_family family, int32_t channel);
bool qa_audio_engine_stop_actor(qa_audio_engine *engine, uint64_t actor, uint64_t owner,
                                qa_error *error);
bool qa_audio_engine_stop_loop(qa_audio_engine *engine, uint64_t actor, uint64_t owner,
                               uint32_t audience, qa_error *error);
bool qa_audio_engine_stop_owner(qa_audio_engine *engine, uint64_t owner, uint32_t audience,
                                qa_error *error);
void qa_audio_engine_stop_all(qa_audio_engine *engine);
/* Bus attachment transfers ownership; replacing or removing a bus releases it.
 * Music and raw streams have separate ID namespaces; remove_bus removes both.
 */
bool qa_audio_engine_stream(qa_audio_engine *engine, uint64_t id, uint32_t audience, float gain,
                            qa_audio_raw_stream *stream, qa_error *error);
bool qa_audio_engine_music(qa_audio_engine *engine, uint64_t id, uint32_t audience, float gain,
                           qa_audio_music *music, qa_error *error);
void qa_audio_engine_remove_bus(qa_audio_engine *engine, uint64_t id);
void qa_audio_engine_remove_stream(qa_audio_engine *engine, uint64_t id);
void qa_audio_engine_remove_music(qa_audio_engine *engine, uint64_t id);
/* Clears round-specific listeners, source positions, voices and buses;
 * preserves clock. */
bool qa_audio_engine_reset_round(qa_audio_engine *engine, qa_error *error);
/* Successful attachment transfers selector ownership to exactly one seat. */
bool qa_audio_engine_environment(qa_audio_engine *engine, uint32_t seat,
                                 qa_audio_environment *environment, qa_error *error);
/* Mixes one shared output with caller-selected listener gains and private DSP.
 */
bool qa_audio_engine_mix(qa_audio_engine *engine, int16_t *stereo, size_t frames, qa_error *error);
uint64_t qa_audio_engine_clock(const qa_audio_engine *engine);
uint32_t qa_audio_engine_rate(const qa_audio_engine *engine);
/* Borrowed until that seat is removed or the engine is destroyed. */
qa_audio_mixer *qa_audio_engine_seat_mixer(qa_audio_engine *engine, uint32_t seat);
bool qa_audio_engine_end_loop_frame(qa_audio_engine *engine, qa_error *error);
void qa_audio_engine_update(qa_audio_engine *engine, double milliseconds);

/* SDL2 subsystem lifetime belongs to the platform. These functions open/close
 * devices only; the caller initializes SDL_INIT_AUDIO before using them.
 * Device calls are serialized by the application, including close and pump. */
typedef struct qa_audio_device qa_audio_device;
typedef struct qa_audio_device_options {
    qa_audio_output_format format;
    const char *name;             /* NULL selects system default; copied by open/select. */
    unsigned buffer_frames;       /* Zero selects 1024; native advisory buffer size. */
    size_t maximum_queued_frames; /* Zero uses the native queue/address-space
                                     limit. */
} qa_audio_device_options;
typedef void (*qa_audio_device_name_fn)(void *user, const char *name);
typedef enum qa_audio_device_state {
    QA_AUDIO_DEVICE_CLOSED, /* NULL device handle. */
    QA_AUDIO_DEVICE_DETACHED,
    QA_AUDIO_DEVICE_PAUSED,
    QA_AUDIO_DEVICE_PLAYING
} qa_audio_device_state;
bool qa_audio_device_names(qa_audio_device_name_fn callback, void *user, qa_error *error);
/* Opens paused; pump starts output automatically unless explicitly paused. */
bool qa_audio_device_open(const qa_audio_device_options *options, qa_audio_device **out,
                          qa_error *error);
void qa_audio_device_close(qa_audio_device *device);
void qa_audio_device_pause(qa_audio_device *device, bool paused);
void qa_audio_device_clear(qa_audio_device *device);
void qa_audio_device_detach(qa_audio_device *device);
/* Selection retains pending PCM, resampling it if the device rate changes.
 * Failure retains/restores the previous output or its detached queued state. */
bool qa_audio_device_select(qa_audio_device *device, const qa_audio_device_options *options,
                            qa_error *error);
qa_audio_output_format qa_audio_device_format(const qa_audio_device *device);
const char *qa_audio_device_name(const qa_audio_device *device);
qa_audio_device_state qa_audio_device_get_state(const qa_audio_device *device);
/* The returned name remains borrowed until selection or close. */
qa_audio_device_options qa_audio_device_configuration(const qa_audio_device *device);
/* Includes PCM retained after output failure or detachment. */
size_t qa_audio_device_queued(qa_audio_device *device);
/* Logical unpaused playback time expressed at the currently selected rate. */
uint64_t qa_audio_device_playback(qa_audio_device *device);
/* Caller PCM is stereo at the selected device rate, retained until consumed. */
bool qa_audio_device_queue(qa_audio_device *device, const int16_t *stereo, size_t frames,
                           qa_error *error);
/* Mix/refill preserves conversion phase; target is queued device-rate frames.
 * Device and engine must remain alive for the complete pump call, including
 * notifications. Schedule owner destruction after pump returns. */
bool qa_audio_device_pump(qa_audio_device *device, qa_audio_engine *engine, size_t target_frames,
                          qa_error *error);
/* Uses the recent eight refill intervals and measured work to select lookahead.
 * Reports produced device-rate frames, including those retained on failure. */
bool qa_audio_device_pump_auto(qa_audio_device *device, qa_audio_engine *engine,
                               double measured_work_ms, size_t *mixed_frames, qa_error *error);
bool qa_audio_device_pending(qa_audio_device *device, qa_buffer *stereo_s16, qa_error *error);

#endif
