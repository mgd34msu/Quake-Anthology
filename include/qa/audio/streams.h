/*
 * Port of src/audio/streams.ts. The TS PcmStream interface becomes a
 * heap object with an ops table plus state; MemoryPcmStream borrows
 * its qa_pcm_sound (read results borrow its samples: samples_owned
 * is false), VorbisPcmStream owns a platform decoder (read results
 * own their samples). Vorbis failures abort printing the decoder's
 * error text, like the TS throw. RawAudioStream mix writes into a
 * caller buffer of frames*2 doubles (TS returns a Float64Array).
 * Checkpoint capture returns an owned qa_save_value tree.
 * Constructors return NULL/false only on OOM. GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_STREAMS_H
#define QA_AUDIO_STREAMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/audio/types.h"
#include "qa/audio/wav.h"
#include "qa/persistence/value.h"

typedef struct qa_pcm_stream qa_pcm_stream;

typedef struct qa_pcm_stream_ops {
  int64_t (*sample_rate)(void *state);
  int (*channels)(void *state);
  int64_t (*frame_count)(void *state);
  int64_t (*position_frames)(void *state);
  /* False only on OOM; *has false at end of stream (TS null). */
  bool (*read)(void *state, int64_t max_frames, qa_pcm_sound *out, bool *has);
  void (*seek)(void *state, int64_t frame);
  void (*close)(void *state); /* frees state */
} qa_pcm_stream_ops;

struct qa_pcm_stream {
  void *state;
  const qa_pcm_stream_ops *ops;
};

/* Wraps caller state (taken over on success). */
qa_pcm_stream *qa_pcm_stream_create(void *state, const qa_pcm_stream_ops *ops);
int64_t qa_pcm_stream_sample_rate(const qa_pcm_stream *stream);
int qa_pcm_stream_channels(const qa_pcm_stream *stream);
int64_t qa_pcm_stream_frame_count(const qa_pcm_stream *stream);
int64_t qa_pcm_stream_position_frames(const qa_pcm_stream *stream);
bool qa_pcm_stream_read(qa_pcm_stream *stream, int64_t max_frames,
                        qa_pcm_sound *out, bool *has);
void qa_pcm_stream_seek(qa_pcm_stream *stream, int64_t frame);
/* TS close(): releases state and frees the stream object. */
void qa_pcm_stream_close(qa_pcm_stream *stream);

/* Borrows `pcm`; the sound must outlive the stream. */
qa_pcm_stream *qa_memory_pcm_stream_create(const qa_pcm_sound *pcm);
qa_pcm_stream *qa_vorbis_pcm_stream_open(const char *path);
qa_pcm_stream *qa_vorbis_pcm_stream_from_bytes(const uint8_t *bytes,
                                               size_t length);

/* `source` NULL means "<sound>". */
qa_pcm_stream *qa_open_pcm_bytes(const uint8_t *bytes, size_t length,
                                 const char *source);
bool qa_decode_sound_bytes(const uint8_t *bytes, size_t length,
                           const char *source, qa_pcm_sound *out);

/*
 * Quiet counterparts for TS callers that catch decode failures
 * (only src/audio/bank.ts: register/openMusic catch -> null). The
 * aborting entry points above remain for callers that do not catch.
 * A stream returned by qa_try_open_pcm_bytes is an ordinary
 * (aborting) stream: only its opening is quiet, exactly where the
 * TS try block ends.
 */
typedef enum qa_pcm_try_result {
  QA_PCM_TRY_OK,
  QA_PCM_TRY_INVALID,
  QA_PCM_TRY_OOM
} qa_pcm_try_result;

qa_pcm_try_result qa_try_open_pcm_bytes(const uint8_t *bytes, size_t length,
                                        const char *source,
                                        qa_pcm_stream **out);
qa_pcm_try_result qa_try_decode_sound_bytes(const uint8_t *bytes,
                                            size_t length, const char *source,
                                            qa_pcm_sound *out);

typedef struct qa_raw_segment {
  int64_t begin;
  int64_t end;
  int16_t *samples; /* owned */
  size_t sample_count;
} qa_raw_segment;

typedef struct qa_raw_audio_stream {
  int64_t output_rate;
  qa_raw_segment *segments;
  size_t segment_count;
  size_t segment_capacity;
  int64_t input_rate;
  int channels; /* 1 or 2 */
  double origin;
  int64_t output_frames;
  int64_t end;
  bool paused;
} qa_raw_audio_stream;

/* TS refill closure: true fills *out (samples borrowed for the call). */
typedef bool (*qa_raw_refill_fn)(void *ctx, qa_stream_pcm *out);

void qa_raw_audio_stream_init(qa_raw_audio_stream *stream, int64_t output_rate);
void qa_raw_audio_stream_destroy(qa_raw_audio_stream *stream);
/* Owned checkpoint tree; NULL only on OOM. Caller frees. */
qa_save_value *qa_raw_audio_stream_capture_checkpoint(
    const qa_raw_audio_stream *stream);
bool qa_raw_audio_stream_restore_checkpoint(qa_raw_audio_stream *out,
                                            const qa_save_value *value,
                                            int64_t output_rate);
bool qa_raw_audio_stream_initialized(const qa_raw_audio_stream *stream);
int64_t qa_raw_audio_stream_source_position(const qa_raw_audio_stream *stream);
int64_t qa_raw_audio_stream_queued_source_frames(
    const qa_raw_audio_stream *stream);
/* Deep-copies segments (TS shares them; not observable). */
bool qa_raw_audio_stream_with_output_rate(const qa_raw_audio_stream *stream,
                                          int64_t output_rate,
                                          qa_raw_audio_stream *out);
bool qa_raw_audio_stream_queue(qa_raw_audio_stream *stream,
                               const qa_stream_pcm *chunk);
void qa_raw_audio_stream_mix(qa_raw_audio_stream *stream, int64_t frames,
                             double gain, qa_raw_refill_fn refill,
                             void *refill_ctx, double *output);

#endif
