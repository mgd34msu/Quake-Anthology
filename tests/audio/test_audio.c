/*
 * Port of key cases from tests/audio/geometry.test.ts,
 * q3-wav.test.ts and playback.test.ts (RawAudioStream, MusicPlayer,
 * StereoReverb), plus oracle values for adpcm/wavelet/types produced
 * by the TypeScript implementation. Mixer/engine/bank cases land
 * with those ports. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/audio.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

typedef struct trace_state {
  double wall_width; /* >=0: wall; -1: clear; -2: solid; -3: count calls */
  int calls;
} trace_state;

static qa_audio_geometry_hit trace_fn(qa_vec3 from, qa_vec3 to, void *ctx) {
  trace_state *state = ctx;
  state->calls++;
  qa_audio_geometry_hit hit = {1, false, false};
  if (state->wall_width == -2) {
    hit.fraction = 0;
    hit.start_solid = true;
    hit.all_solid = true;
  } else if (state->wall_width >= 0) {
    double edge = from.x < to.x ? 128 - state->wall_width / 2
                                : 128 + state->wall_width / 2;
    hit.fraction = fabs(edge - from.x) / fabs(to.x - from.x);
  }
  return hit;
}

static void test_geometry(void) {
  qa_vec3 start = {0, 0, 0}, end = {256, 0, 0};
  trace_state clear = {-1, 0};
  CHECK(qa_geometry_transmission(start, end, trace_fn, &clear) == 1);
  trace_state untraced = {-3, 0};
  CHECK(qa_geometry_transmission(start, start, trace_fn, &untraced) == 1);
  CHECK(untraced.calls == 0);
  trace_state wall0 = {0, 0};
  CHECK(qa_geometry_transmission(start, end, trace_fn, &wall0) == 0.5);
  trace_state wall64 = {64, 0};
  CHECK(qa_geometry_transmission(start, end, trace_fn, &wall64) == 0.25);
  trace_state wall128 = {128, 0};
  CHECK(qa_geometry_transmission(start, end, trace_fn, &wall128) == 0.125);
  trace_state solid = {-2, 0};
  CHECK(qa_geometry_transmission(start, end, trace_fn, &solid) == 0.03125);
}

static void put_text(uint8_t *bytes, size_t offset, const char *text) {
  memcpy(bytes + offset, text, strlen(text));
}
static void put_u32(uint8_t *bytes, size_t offset, uint32_t value) {
  bytes[offset] = (uint8_t)value;
  bytes[offset + 1] = (uint8_t)(value >> 8);
  bytes[offset + 2] = (uint8_t)(value >> 16);
  bytes[offset + 3] = (uint8_t)(value >> 24);
}
static void put_u16(uint8_t *bytes, size_t offset, uint16_t value) {
  bytes[offset] = (uint8_t)value;
  bytes[offset + 1] = (uint8_t)(value >> 8);
}

static void test_q3_wav(void) {
  uint8_t bytes[82] = {0};
  put_text(bytes, 0, "RIFF");
  put_u32(bytes, 4, 74);
  put_text(bytes, 8, "WAVEfmt ");
  put_u32(bytes, 16, 16);
  put_u16(bytes, 20, 1);
  put_u16(bytes, 22, 1);
  put_u32(bytes, 24, 22050);
  put_u32(bytes, 28, 44100);
  put_u16(bytes, 32, 2);
  put_u16(bytes, 34, 16);
  put_text(bytes, 36, "data");
  put_u32(bytes, 40, 2);
  put_u16(bytes, 44, 1234);
  put_text(bytes, 46, "cue ");
  put_u32(bytes, 50, 28);
  put_u32(bytes, 54, 1);
  put_text(bytes, 66, "data");
  put_u32(bytes, 78, 59404);
  qa_decoded_wav decoded;
  /* TS: decodeWav throws "loop start"; the C port aborts. */
  CHECK_ABORTS((void)qa_decode_wav(bytes, sizeof bytes, NULL, &decoded));
  CHECK(qa_decode_q3_wav(bytes, sizeof bytes, NULL, &decoded));
  CHECK(decoded.pcm.sample_count == 1);
  CHECK(decoded.pcm.samples[0] == 1234);
  CHECK(decoded.pcm.frame_count == 1);
  CHECK(decoded.pcm.loop_start == -1);
  CHECK(decoded.pcm.sample_rate == 22050);
  qa_decoded_wav_dispose(&decoded);
}

static void test_types_channels(void) {
  qa_sound_channel_command command =
      qa_source_sound_channel(QA_SOUND_FAMILY_Q3, 1);
  CHECK(command.kind == QA_SOUND_CHANNEL_CHANNEL);
  CHECK(strcmp(command.channel, "local") == 0);
  command = qa_source_sound_channel(QA_SOUND_FAMILY_Q1, 1);
  CHECK(strcmp(command.channel, "weapon") == 0);
  command = qa_source_sound_channel(QA_SOUND_FAMILY_Q2, 9);
  CHECK(strcmp(command.channel, "q2:extension:9") == 0);
  command = qa_source_sound_channel(QA_SOUND_FAMILY_Q1, -1);
  CHECK(command.kind == QA_SOUND_CHANNEL_REPLACE_ACTOR);
  command = qa_source_sound_channel(QA_SOUND_FAMILY_Q3, 0);
  CHECK(command.kind == QA_SOUND_CHANNEL_AUTO);
  CHECK_ABORTS((void)qa_source_sound_channel(QA_SOUND_FAMILY_Q3, -1));
  CHECK_ABORTS((void)qa_source_sound_channel(QA_SOUND_FAMILY_Q1, -2));
}

static void test_adpcm_wavelet(void) {
  const int16_t input[8] = {0, 1000, -1000, 5000, 32767, -32768, 0, 123};
  const uint8_t expected_bytes[4] = {7, 247, 127, 17};
  const int16_t expected_decoded[8] = {0, 11, -19, 44, 180, -113, 13, 127};
  uint8_t encoded[4] = {0};
  qa_adpcm_state state = {0, 0};
  qa_encode_adpcm(input, 8, encoded, sizeof encoded, &state);
  CHECK(memcmp(encoded, expected_bytes, 4) == 0);
  CHECK(state.sample == 127);
  CHECK(state.index == 38);
  int16_t decoded[8] = {0};
  qa_adpcm_state state2 = {0, 0};
  qa_decode_adpcm(encoded, sizeof encoded, decoded, 8, &state2);
  CHECK(memcmp(decoded, expected_decoded, sizeof expected_decoded) == 0);
  CHECK(state2.sample == 127 && state2.index == 38);
  CHECK(qa_adpcm_memory_needed(4096, 22050, 22050) == 2052);
  CHECK(qa_adpcm_memory_needed(44100, 44100, 22050) == 11049);
  CHECK_ABORTS(qa_encode_adpcm(input, 8, encoded, 2, &(qa_adpcm_state){0, 0}));

  CHECK(qa_mu_law_encode(0) == 127);
  CHECK(qa_mu_law_decode(127) == -4);
  CHECK(qa_mu_law_encode(1000) == 78);
  CHECK(qa_mu_law_decode(78) == 956);
  CHECK(qa_mu_law_encode(-1000) == 206);
  CHECK_ABORTS((void)qa_mu_law_encode(32768));
}

static void test_raw_stream(void) {
  qa_raw_audio_stream stream;
  qa_raw_audio_stream_init(&stream, 48000);
  int16_t first[2] = {100, 200}, second[2] = {300, 400};
  qa_stream_pcm chunk = {0};
  chunk.samples_s16 = first;
  chunk.sample_count = 2;
  chunk.channels = 1;
  chunk.sample_rate = 22050;
  chunk.source_sample = 0;
  chunk.reset_stream = true;
  CHECK(qa_raw_audio_stream_queue(&stream, &chunk));
  chunk.samples_s16 = second;
  chunk.source_sample = 2;
  chunk.reset_stream = false;
  CHECK(qa_raw_audio_stream_queue(&stream, &chunk));
  double output[18];
  stream.paused = true;
  qa_raw_audio_stream_mix(&stream, 3, 1, NULL, NULL, output);
  bool silent = true;
  for (int i = 0; i < 6; i++) silent = silent && output[i] == 0;
  CHECK(silent);
  stream.paused = false;
  qa_raw_audio_stream_mix(&stream, 9, 1, NULL, NULL, output);
  const double expected[9] = {100, 100, 100, 200, 200, 300, 300, 400, 400};
  for (int i = 0; i < 9; i++) CHECK(output[i * 2] == expected[i]);
  qa_raw_audio_stream_destroy(&stream);
}

static void test_music_player(void) {
  int16_t intro_samples[2] = {100, 200}, loop_samples[2] = {300, 400};
  qa_pcm_sound intro_pcm = {0}, loop_pcm = {0};
  intro_pcm.sample_rate = 22050;
  intro_pcm.channels = 1;
  intro_pcm.samples = intro_samples;
  intro_pcm.sample_count = 2;
  intro_pcm.frame_count = 2;
  intro_pcm.loop_start = -1;
  loop_pcm = intro_pcm;
  loop_pcm.samples = loop_samples;
  qa_music_player player;
  qa_music_player_init(&player, 22050, QA_SOUND_FAMILY_Q1,
                       QA_MUSIC_VOLUME_SOURCE, NULL);
  qa_music_player_set_volume(&player, 1);
  qa_pcm_stream *intro = qa_memory_pcm_stream_create(&intro_pcm);
  qa_pcm_stream *loop = qa_memory_pcm_stream_create(&loop_pcm);
  CHECK(intro != NULL && loop != NULL);
  qa_music_player_start(&player, intro, loop);
  double output[12];
  qa_music_player_mix(&player, 6, output);
  const double expected[6] = {100, 200, 300, 400, 300, 400};
  for (int i = 0; i < 6; i++) CHECK(output[i * 2] == expected[i]);
  player.paused = true;
  int64_t position = qa_music_player_source_position(&player);
  qa_music_player_mix(&player, 4, output);
  CHECK(qa_music_player_source_position(&player) == position);
  qa_music_player_close(&player);
  qa_music_player_destroy(&player);
}

static void test_reverb(void) {
  qa_stereo_reverb reverb;
  CHECK(qa_stereo_reverb_init(&reverb, 22050));
  size_t count = 22050 * 2;
  double *samples = calloc(count, sizeof *samples);
  CHECK(samples != NULL);
  samples[0] = 32767;
  samples[1] = 32767;
  qa_efx_reverb_params preset = qa_reverb_preset(5);
  qa_stereo_reverb_process(&reverb, samples, count, &preset);
  bool tail = false;
  for (size_t i = 1000; i < count; i++)
    if (fabs(samples[i]) > 0.01) tail = true;
  CHECK(tail);
  free(samples);
  qa_stereo_reverb_destroy(&reverb);
  CHECK_ABORTS((void)qa_reverb_preset(26));
  CHECK(qa_reverb_preset_count == 26);
  CHECK(strcmp(qa_reverb_preset_names[QA_REVERB_PRESET_PLAIN], "plain") == 0);
}

static char warned[512];
static void capture_warn(const char *message, void *ctx) {
  (void)ctx;
  snprintf(warned, sizeof warned, "%s", message);
}

static void test_environments(void) {
  qa_reverb_environment *environments = NULL;
  size_t count = 0;
  warned[0] = '\0';
  CHECK(qa_parse_environments(
      "{\"environments\":[{\"dimension\":100,\"reverbs\":["
      "{\"materials\":[\"stone\"],\"preset\":\"cave\"},"
      "{\"preset\":\"missing-one\"}]}]}",
      capture_warn, NULL, &environments, &count));
  CHECK(count == 1);
  CHECK(environments[0].dimension == 100);
  CHECK(environments[0].reverb_count == 2);
  CHECK(environments[0].reverbs[0].preset_index == 8);
  CHECK(environments[0].reverbs[0].material_count == 1);
  CHECK(strcmp(environments[0].reverbs[0].materials[0], "stone") == 0);
  CHECK(environments[0].reverbs[1].materials == NULL);
  CHECK(environments[0].reverbs[1].preset_index == QA_REVERB_PRESET_PLAIN);
  CHECK(strcmp(warned, "Missing sound environment preset missing-one") == 0);
  qa_reverb_environments_free(environments, count);
}

static void test_output(void) {
  int64_t rate = 0;
  CHECK(qa_audio_khz_rate("44", &rate) && rate == 44100);
  CHECK(qa_audio_khz_rate(" 11 ", &rate) && rate == 11025);
  CHECK(!qa_audio_khz_rate("", &rate));
  CHECK(!qa_audio_khz_rate("45", &rate));
  CHECK(!qa_audio_khz_rate("junk", &rate));
  qa_audio_output_format format = qa_audio_output_format_validated(48000, 1, 8);
  CHECK(format.sample_rate == 48000 && format.channels == 1 &&
        format.sample_bits == 8);
  CHECK_ABORTS((void)qa_audio_output_format_validated(100, 2, 16));
  int16_t stereo[4] = {1000, 3000, -32768, 32767};
  qa_encoded_output_pcm encoded;
  CHECK(qa_encode_output_pcm(stereo, 4, format, &encoded));
  CHECK(encoded.is_u8 && encoded.sample_count == 2);
  /* (2000+32768)>>8 = 135; (0 avg of -32768,32767 -> 0 truncated
   * toward zero from -0.5) -> (0+32768)>>8 = 128. */
  CHECK(encoded.samples_u8[0] == 135);
  CHECK(encoded.samples_u8[1] == 128);
  qa_encoded_output_pcm_dispose(&encoded);
  qa_audio_output_format stereo16 = qa_default_audio_output_format;
  CHECK(qa_encode_output_pcm(stereo, 4, stereo16, &encoded));
  CHECK(encoded.borrowed && encoded.samples_s16 == stereo);
  qa_encoded_output_pcm_dispose(&encoded);
}

int main(void) {
  test_geometry();
  test_q3_wav();
  test_types_channels();
  test_adpcm_wavelet();
  test_raw_stream();
  test_music_player();
  test_reverb();
  test_environments();
  test_output();
  printf("audio: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
