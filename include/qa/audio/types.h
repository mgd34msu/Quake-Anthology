/*
 * Port of src/audio/types.ts. Optional fields use has_* flags;
 * `X | null` handles use a bool. qa_sound_family mirrors the TS
 * SoundFamily literals (same values as contracts GameFamily, kept
 * distinct as in TS). GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_TYPES_H
#define QA_AUDIO_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/audio/wav.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

typedef enum qa_sound_family {
  QA_SOUND_FAMILY_Q1,
  QA_SOUND_FAMILY_Q2,
  QA_SOUND_FAMILY_Q3
} qa_sound_family;
const char *qa_sound_family_name(qa_sound_family family);
bool qa_sound_family_parse(const char *text, qa_sound_family *out);

typedef enum qa_audio_audience_kind {
  QA_AUDIO_AUDIENCE_WORLD,
  QA_AUDIO_AUDIENCE_SEAT
} qa_audio_audience_kind;
typedef struct qa_audio_audience {
  qa_audio_audience_kind kind;
  qa_seat_id seat; /* seat kind */
} qa_audio_audience;

typedef struct qa_audio_listener {
  qa_seat_id seat;
  bool has_actor;
  qa_actor_id actor;
  qa_vec3 origin;
  qa_axis axis;
  double gain;
  bool underwater;
} qa_audio_listener;

typedef struct qa_sound_asset {
  const qa_resolved_resource_reference *reference; /* NULL = absent */
  qa_resource_id resource;
  const char *name;
  qa_pcm_sound pcm;
} qa_sound_asset;

typedef enum qa_sound_origin_kind {
  QA_SOUND_ORIGIN_LOCAL,
  QA_SOUND_ORIGIN_FIXED,
  QA_SOUND_ORIGIN_ACTOR
} qa_sound_origin_kind;
typedef struct qa_sound_origin {
  qa_sound_origin_kind kind;
  qa_vec3 position;   /* fixed */
  qa_actor_id actor;  /* actor */
} qa_sound_origin;

typedef struct qa_play_sound {
  qa_provider_id owner; /* NULL = absent */
  const qa_sound_asset *sound;
  qa_sound_family family;
  bool has_actor;
  qa_actor_id actor;
  qa_sound_origin origin;
  qa_audio_audience audience;
  int64_t channel;
  double volume;
  double attenuation;
  bool has_delay_seconds;
  double delay_seconds;
  bool has_server_milliseconds;
  double server_milliseconds;
} qa_play_sound;

typedef enum qa_loop_lifetime {
  QA_LOOP_LIFETIME_FRAME,
  QA_LOOP_LIFETIME_PERSISTENT
} qa_loop_lifetime;

typedef struct qa_loop_sound {
  qa_provider_id owner; /* NULL = absent */
  const qa_sound_asset *sound;
  qa_sound_family family;
  qa_actor_id actor;
  qa_sound_origin origin;
  qa_audio_audience audience;
  double volume;
  double attenuation;
  qa_vec3 velocity;
  double frame_number;
  qa_loop_lifetime lifetime;
} qa_loop_sound;

/* TS StreamPcm: samples are Int16Array unless is_u8, then Uint8Array. */
typedef struct qa_stream_pcm {
  bool is_u8;
  const int16_t *samples_s16;
  const uint8_t *samples_u8;
  size_t sample_count;
  int channels; /* 1 or 2 */
  int64_t sample_rate;
  int64_t source_sample;
  bool reset_stream;
} qa_stream_pcm;

typedef struct qa_audio_stream_target {
  const char *id;
  qa_audio_audience audience;
  double gain;
} qa_audio_stream_target;

typedef enum qa_sound_channel_command_kind {
  QA_SOUND_CHANNEL_AUTO,
  QA_SOUND_CHANNEL_REPLACE_ACTOR,
  QA_SOUND_CHANNEL_CHANNEL
} qa_sound_channel_command_kind;
typedef struct qa_sound_channel_command {
  qa_sound_channel_command_kind kind;
  /* TS SharedSoundChannel text; extension names are formatted here. */
  char channel[64];
} qa_sound_channel_command;

/* Aborts with the TS RangeError on an invalid channel. */
qa_sound_channel_command qa_source_sound_channel(qa_sound_family family,
                                                 int64_t channel);

typedef enum qa_audio_voice_event_kind {
  QA_AUDIO_VOICE_EVENT_START,
  QA_AUDIO_VOICE_EVENT_STOP
} qa_audio_voice_event_kind;
typedef enum qa_audio_voice_stop_reason {
  QA_AUDIO_VOICE_STOP_ENDED,
  QA_AUDIO_VOICE_STOP_STOPPED,
  QA_AUDIO_VOICE_STOP_REPLACED
} qa_audio_voice_stop_reason;
typedef struct qa_audio_voice_event {
  qa_audio_voice_event_kind kind;
  qa_seat_id seat;
  double voice_id;
  const qa_sound_asset *sound; /* start */
  double output_sample;
  double sample_rate;              /* start */
  double source_offset_seconds;    /* start */
  qa_audio_voice_stop_reason reason; /* stop */
} qa_audio_voice_event;

typedef struct qa_audio_voice_clock {
  double output_sample;
  double sample_rate;
  bool paused;
} qa_audio_voice_clock;

#endif
