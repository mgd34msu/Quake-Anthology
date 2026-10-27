/*
 * Port of key cases from tests/audio/playback.test.ts (mixer voice
 * sharing, Q1/Q2/Q3 source policies, Doppler, UnifiedAudio seats,
 * output selection), undecodable.test.ts (SoundBank quiet decode),
 * and startup-queue.test.ts (device pump lookahead/handoff, under
 * SDL_AUDIODRIVER=dummy like the TS subprocess). Archive-corpus
 * cases need ~/qfiles retail files and stay in the TS suite.
 * GPL-2.0-or-later.
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
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

static double clock_value = 100;
static double milliseconds(void *ctx) {
  (void)ctx;
  return clock_value;
}
static double zero_random(void *ctx) {
  (void)ctx;
  return 0;
}

static qa_identity_owner identity;
static const qa_axis axis_x = {.vectors = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
static const qa_vec3 origin_zero = {0, 0, 0};

static int16_t tone_samples[2048];
static qa_pcm_sound tone;
static qa_sound_asset asset;

static void init_fixtures(void) {
  for (size_t i = 0; i < 2048; i++) tone_samples[i] = 12000;
  memset(&tone, 0, sizeof tone);
  tone.sample_rate = 22050;
  tone.channels = 1;
  tone.samples = tone_samples;
  tone.sample_count = 2048;
  tone.frame_count = 2048;
  tone.loop_start = -1;
  memset(&asset, 0, sizeof asset);
  asset.resource = "resource:audio-smoke";
  asset.name = "sound/test";
  asset.pcm = tone;
  CHECK(qa_identity_owner_create(&identity, "audio-smoke"));
}

static qa_audio_mixer *new_mixer(double rate, double capacity) {
  qa_audio_mixer *mixer =
      qa_audio_mixer_create(rate, milliseconds, NULL, capacity, 2, 1024);
  CHECK(mixer != NULL);
  return mixer;
}

static qa_voice_origin local_origin(void) {
  qa_voice_origin origin = {0};
  origin.kind = QA_VOICE_ORIGIN_LOCAL;
  return origin;
}
static qa_voice_origin fixed_origin(qa_vec3 position) {
  qa_voice_origin origin = {0};
  origin.kind = QA_VOICE_ORIGIN_FIXED;
  origin.position = position;
  return origin;
}
static qa_sound_channel_command channel_named(const char *name) {
  qa_sound_channel_command command = {0};
  command.kind = QA_SOUND_CHANNEL_CHANNEL;
  snprintf(command.channel, sizeof command.channel, "%s", name);
  return command;
}
static qa_sound_channel_command auto_command(void) {
  qa_sound_channel_command command = {0};
  command.kind = QA_SOUND_CHANNEL_AUTO;
  return command;
}

static void channel_sounds(qa_audio_mixer *mixer, const qa_pcm_sound **out,
                           size_t *count) {
  qa_mixer_channel_volume *volumes = qa_audio_mixer_channel_volumes(mixer, count);
  for (size_t i = 0; i < *count; i++) out[i] = volumes[i].sound;
  free(volumes);
}

static bool all_zero(const int16_t *samples, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (samples[i] != 0) return false;
  return true;
}
static bool all_positive(const int16_t *samples, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (samples[i] <= 0) return false;
  return true;
}

/* playback.test.ts: shared voices retain rejected sounds... */
static void test_shared_voices(void) {
  clock_value = 100;
  qa_audio_mixer *mixer = new_mixer(22050, 96);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  qa_start_sound_options options = {0};
  options.entity = 1;
  options.channel = 2;
  options.origin = local_origin();
  options.volume = 127;
  CHECK(qa_audio_mixer_start_sound(mixer, &tone, options, NULL));
  int16_t out[2];
  qa_audio_mixer_mix(mixer, 1, out);
  clock_value = 101;
  CHECK(!qa_audio_mixer_start_sound(mixer, &tone, options, NULL));
  qa_audio_mixer_mix(mixer, 1, out);
  CHECK(out[0] > 0);
  static int16_t other_samples[2048];
  for (size_t i = 0; i < 2048; i++) other_samples[i] = 1000;
  qa_pcm_sound other = tone;
  other.samples = other_samples;
  options.channel = 1;
  CHECK(qa_audio_mixer_start_sound(mixer, &other, options, NULL));
  qa_source_sound_options source = {0};
  source.entity = 1;
  source.origin = local_origin();
  source.volume = 1;
  source.attenuation = 0;
  CHECK(qa_audio_mixer_start_q1_sound(mixer, &other, source,
                                      channel_named("weapon"), zero_random,
                                      NULL, NULL));
  const qa_pcm_sound *sounds[8];
  size_t count = 0;
  channel_sounds(mixer, sounds, &count);
  CHECK(count == 2 && sounds[0] == &other && sounds[1] == &other);
  qa_audio_mixer_stop_shared_channel(mixer, 1, "weapon");
  channel_sounds(mixer, sounds, &count);
  CHECK(count == 1 && sounds[0] == &other);
  qa_audio_mixer_stop_shared_channel(mixer, 1, "local");
  qa_audio_mixer_mix(mixer, 1, out);
  CHECK(all_zero(out, 2));
  qa_audio_mixer_destroy(mixer);
}

/* playback.test.ts: Q1 effect, static, ambient and entity loops... */
static void test_q1_shared_clock(void) {
  clock_value = 100;
  qa_audio_mixer *mixer = new_mixer(22050, 96);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  static int16_t loop_samples[4] = {1000, 2000, 3000, 4000};
  qa_pcm_sound loop = {0};
  loop.sample_rate = 22050;
  loop.channels = 1;
  loop.samples = loop_samples;
  loop.sample_count = 4;
  loop.frame_count = 4;
  loop.loop_start = 2;
  qa_source_sound_options source = {0};
  source.entity = 1;
  source.origin = local_origin();
  source.volume = 0.1;
  source.attenuation = 0;
  qa_audio_mixer_start_q1_sound(mixer, &tone, source, auto_command(), zero_random,
                                NULL, NULL);
  qa_audio_mixer_add_static_sound(mixer, &loop, origin_zero, 100, 0, 0);
  const qa_pcm_sound *ambient_sounds[1] = {&loop};
  const double levels[1] = {100};
  qa_audio_mixer_update_ambient(mixer, ambient_sounds, 1, levels, 1, 1, 0.3,
                                100);
  qa_source_loop_sound entries[1] = {{0}};
  entries[0].family = QA_SOUND_FAMILY_Q1;
  entries[0].entity = 2;
  entries[0].sound = &loop;
  entries[0].origin = origin_zero;
  entries[0].volume = 0.1;
  entries[0].has_attenuation = false;
  qa_audio_mixer_set_source_loop_sounds(mixer, entries, 1);
  qa_start_sound_options start = {0};
  start.entity = 3;
  start.channel = 2;
  start.origin = local_origin();
  start.volume = 10;
  qa_audio_mixer_start_sound(mixer, &tone, start, NULL);
  qa_audio_mixer_clear_looping_sounds(mixer, true);
  const qa_pcm_sound *sounds[16];
  size_t count = 0;
  channel_sounds(mixer, sounds, &count);
  CHECK(count == 5);
  int16_t out[20];
  qa_audio_mixer_mix(mixer, 10, out);
  CHECK(all_positive(out, 20));
  CHECK(qa_audio_mixer_sample_clock(mixer) == 10);
  qa_audio_mixer_set_source_loop_sounds(mixer, NULL, 0);
  qa_audio_mixer_update_ambient(mixer, NULL, 0, NULL, 0, 0, 0.3, 100);
  source.entity = -1;
  source.volume = 0.1;
  qa_sound_channel_command replace = {0};
  replace.kind = QA_SOUND_CHANNEL_REPLACE_ACTOR;
  qa_audio_mixer_start_q1_sound(mixer, &loop, source, replace, zero_random,
                                NULL, NULL);
  channel_sounds(mixer, sounds, &count);
  CHECK(count == 4);
  qa_audio_mixer_stop_all(mixer);
  qa_audio_mixer_add_static_sound(mixer, &loop, origin_zero, 255, 0, 0);
  int16_t pcm[16];
  qa_audio_mixer_mix(mixer, 8, pcm);
  CHECK(pcm[0] < pcm[2]);
  CHECK(pcm[4] == pcm[8]);
  CHECK(pcm[6] == pcm[10]);
  CHECK(pcm[8] == pcm[12]);
  CHECK(qa_audio_mixer_sample_clock(mixer) == 18);
  qa_audio_mixer_destroy(mixer);
}

typedef struct counting_random {
  int calls;
  double value;
} counting_random;
static double count_random(void *ctx) {
  counting_random *random = ctx;
  random->calls++;
  return random->value;
}

/* playback.test.ts: Q1 same-frame dephasing... (mixer half) */
static void test_q1_dephase(void) {
  clock_value = 100;
  qa_audio_mixer *mixer = new_mixer(22050, 96);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  static int16_t ramp_samples[5] = {1000, 2000, 3000, 4000, 5000};
  qa_pcm_sound ramp = {0};
  ramp.sample_rate = 22050;
  ramp.channels = 1;
  ramp.samples = ramp_samples;
  ramp.sample_count = 5;
  ramp.frame_count = 5;
  ramp.loop_start = -1;
  counting_random random = {0, 2};
  qa_source_sound_options source = {0};
  source.entity = 1;
  source.origin = local_origin();
  source.volume = 1;
  source.attenuation = 0;
  qa_audio_mixer_start_q1_sound(mixer, &ramp, source, channel_named("weapon"),
                                count_random, &random, NULL);
  CHECK(random.calls == 0);
  qa_audio_mixer_start_q1_sound(mixer, &ramp, source, channel_named("voice"),
                                count_random, &random, NULL);
  CHECK(random.calls == 1);
  qa_audio_mixer_stop_shared_channel(mixer, 1, "weapon");
  int16_t shifted[8];
  qa_audio_mixer_mix(mixer, 4, shifted);
  CHECK(shifted[0] > shifted[6]);
  CHECK(shifted[6] == 0);
  CHECK(qa_audio_mixer_sample_clock(mixer) == 4);
  qa_audio_mixer_destroy(mixer);
}

/* playback.test.ts: a full shared pool of protected source loops... */
static void test_full_pool(void) {
  clock_value = 100;
  qa_audio_mixer *mixer = new_mixer(22050, 2);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  qa_pcm_sound loop = tone;
  loop.loop_start = 0;
  qa_audio_mixer_add_static_sound(mixer, &loop, origin_zero, 20, 0, 0);
  const qa_pcm_sound *ambient_sounds[1] = {&loop};
  const double levels[1] = {100};
  qa_audio_mixer_update_ambient(mixer, ambient_sounds, 1, levels, 1, 1, 0.3,
                                100);
  const qa_pcm_sound *sounds[8];
  size_t count = 0;
  channel_sounds(mixer, sounds, &count);
  CHECK(count == 2);
  qa_start_sound_options start = {0};
  start.entity = 1;
  start.channel = 2;
  start.origin = local_origin();
  start.volume = 20;
  CHECK(qa_audio_mixer_start_sound(mixer, &tone, start, NULL));
  channel_sounds(mixer, sounds, &count);
  CHECK(count == 3);
  int16_t out[16];
  qa_audio_mixer_mix(mixer, 8, out);
  CHECK(all_positive(out, 16));
  CHECK(qa_audio_mixer_sample_clock(mixer) == 8);
  qa_audio_mixer_destroy(mixer);
}

/* playback.test.ts: dense mixed voices clip once... */
static void test_dense_clip(void) {
  const int16_t extremes[2] = {32767, -32768};
  for (int e = 0; e < 2; e++) {
    clock_value = 100;
    qa_audio_mixer *mixer = new_mixer(22050, 96);
    qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
    int16_t sample = extremes[e];
    qa_pcm_sound full = {0};
    full.sample_rate = 22050;
    full.channels = 1;
    full.samples = &sample;
    full.sample_count = 1;
    full.frame_count = 1;
    full.loop_start = -1;
    qa_source_sound_options source = {0};
    source.entity = 1;
    source.origin = local_origin();
    source.volume = 1;
    source.attenuation = 0;
    for (int i = 0; i < 600; i++)
      qa_audio_mixer_start_q1_sound(mixer, &full, source, auto_command(),
                                    zero_random, NULL, NULL);
    qa_start_sound_options start = {0};
    start.entity = 1;
    start.channel = 2;
    start.origin = local_origin();
    start.volume = 127;
    CHECK(qa_audio_mixer_start_sound(mixer, &full, start, NULL));
    int16_t out[2];
    qa_audio_mixer_mix(mixer, 1, out);
    CHECK(out[0] == sample && out[1] == sample);
    CHECK(qa_audio_mixer_sample_clock(mixer) == 1);
    qa_audio_mixer_destroy(mixer);
  }
}

/* playback.test.ts: full-pan Q1 voice products... */
static void test_full_pan(void) {
  const int16_t extremes[2] = {32767, -32768};
  for (int e = 0; e < 2; e++) {
    clock_value = 100;
    qa_audio_mixer *mixer = new_mixer(22050, 96);
    qa_audio_mixer_set_effects_volume(mixer, 1);
    qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
    int16_t sample = extremes[e];
    qa_pcm_sound full = {0};
    full.sample_rate = 22050;
    full.channels = 1;
    full.samples = &sample;
    full.sample_count = 1;
    full.frame_count = 1;
    full.loop_start = -1;
    qa_source_sound_options source = {0};
    source.entity = 2;
    qa_vec3 side = {0, 100, 0};
    source.origin = fixed_origin(side);
    source.volume = 1;
    source.attenuation = 0;
    CHECK(qa_audio_mixer_start_q1_sound(mixer, &full, source, auto_command(),
                                        zero_random, NULL, NULL));
    size_t count = 0;
    qa_mixer_channel_volume *volumes =
        qa_audio_mixer_channel_volumes(mixer, &count);
    CHECK(count == 1 && volumes[0].left == 510 && volumes[0].right == 0);
    free(volumes);
    int16_t out[2];
    qa_audio_mixer_mix(mixer, 1, out);
    CHECK(out[0] == sample && out[1] == 0);
    qa_audio_mixer_destroy(mixer);
  }
}

/* playback.test.ts: Q2 deadlines replace shared weapon voices... */
static void test_q2_deadlines(void) {
  clock_value = 100;
  qa_audio_mixer *mixer = new_mixer(1000, 96);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  static int16_t old_samples[100], next_samples[100];
  for (size_t i = 0; i < 100; i++) {
    old_samples[i] = 1000;
    next_samples[i] = -1000;
  }
  qa_pcm_sound old_sound = {0}, next_sound = {0};
  old_sound.sample_rate = 1000;
  old_sound.channels = 1;
  old_sound.samples = old_samples;
  old_sound.sample_count = 100;
  old_sound.frame_count = 100;
  old_sound.loop_start = -1;
  next_sound = old_sound;
  next_sound.samples = next_samples;
  qa_source_sound_options source = {0};
  source.entity = 1;
  source.origin = local_origin();
  source.volume = 1;
  source.attenuation = 0;
  qa_audio_mixer_start_q1_sound(mixer, &old_sound, source,
                                channel_named("weapon"), zero_random, NULL,
                                NULL);
  source.has_delay_seconds = true;
  source.delay_seconds = 0.005;
  qa_audio_mixer_start_q2_sound(mixer, &next_sound, source,
                                channel_named("weapon"), NULL);
  const qa_pcm_sound *sounds[8];
  size_t count = 0;
  channel_sounds(mixer, sounds, &count);
  CHECK(count == 1 && sounds[0] == &old_sound);
  int16_t crossing[16];
  qa_audio_mixer_mix(mixer, 8, crossing);
  CHECK(crossing[8] > 0);
  CHECK(crossing[10] < 0);
  channel_sounds(mixer, sounds, &count);
  CHECK(count == 1 && sounds[0] == &next_sound);
  qa_audio_mixer_start_q2_sound(mixer, &old_sound, source,
                                channel_named("weapon"), NULL);
  qa_audio_mixer_stop_shared_channel(mixer, 1, "weapon");
  int16_t silent[40];
  qa_audio_mixer_mix(mixer, 20, silent);
  CHECK(all_zero(silent, 40));
  CHECK(qa_audio_mixer_sample_clock(mixer) == 28);
  qa_audio_mixer_destroy(mixer);
}

/* playback.test.ts: Q2 equal deadlines issue newest first... */
static void test_q2_equal_deadlines(void) {
  clock_value = 100;
  qa_audio_mixer *mixer = new_mixer(1000, 96);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  static int16_t first_samples[4] = {1000, 2000, 3000, 4000};
  static int16_t second_samples[4] = {-1000, -1000, -1000, -1000};
  qa_pcm_sound first = {0}, second = {0};
  first.sample_rate = 1000;
  first.channels = 1;
  first.samples = first_samples;
  first.sample_count = 4;
  first.frame_count = 4;
  first.loop_start = -1;
  second = first;
  second.samples = second_samples;
  qa_source_sound_options source = {0};
  source.entity = 1;
  source.origin = local_origin();
  source.volume = 1;
  source.attenuation = 0;
  source.has_delay_seconds = true;
  source.delay_seconds = 0.005;
  qa_audio_mixer_start_q2_sound(mixer, &first, source, channel_named("weapon"),
                                NULL);
  qa_audio_mixer_start_q2_sound(mixer, &second, source,
                                channel_named("weapon"), NULL);
  int16_t out[10];
  qa_audio_mixer_mix(mixer, 5, out);
  CHECK(all_zero(out, 10));
  int16_t one[2];
  qa_audio_mixer_mix(mixer, 1, one);
  CHECK(one[0] > 0);
  const qa_pcm_sound *sounds[8];
  size_t count = 0;
  channel_sounds(mixer, sounds, &count);
  CHECK(count == 1 && sounds[0] == &first);
  qa_audio_mixer_stop_all(mixer);
  source.delay_seconds = -0.1;
  qa_audio_mixer_start_q2_sound(mixer, &first, source, auto_command(), NULL);
  int16_t late[4];
  qa_audio_mixer_mix(mixer, 2, late);
  CHECK(late[0] > 0);
  CHECK(late[2] > late[0]);
  qa_audio_mixer_destroy(mixer);
}

/* playback.test.ts: Q2 server drift correction... */
static void test_q2_drift(void) {
  clock_value = 100;
  qa_audio_mixer *mixer = new_mixer(1000, 96);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  static int16_t short_samples[2] = {1000, 2000};
  qa_pcm_sound short_sound = {0};
  short_sound.sample_rate = 1000;
  short_sound.channels = 1;
  short_sound.samples = short_samples;
  short_sound.sample_count = 2;
  short_sound.frame_count = 2;
  short_sound.loop_start = -1;
  qa_source_sound_options source = {0};
  source.entity = 1;
  source.origin = local_origin();
  source.volume = 1;
  source.attenuation = 0;
  source.has_server_milliseconds = true;
  source.server_milliseconds = 10000;
  source.has_delay_seconds = true;
  source.delay_seconds = 0.005;
  qa_audio_mixer_start_q2_sound(mixer, &short_sound, source, auto_command(),
                                NULL);
  int16_t *out = malloc(210 * sizeof *out);
  CHECK(out != NULL);
  qa_audio_mixer_mix(mixer, 105, out);
  CHECK(all_zero(out, 210));
  int16_t one[2];
  qa_audio_mixer_mix(mixer, 1, one);
  CHECK(one[0] > 0);
  qa_audio_mixer_stop_all(mixer);
  source.server_milliseconds = -10000;
  qa_audio_mixer_start_q2_sound(mixer, &short_sound, source, auto_command(),
                                NULL);
  qa_audio_mixer_mix(mixer, 5, out);
  CHECK(all_zero(out, 10));
  qa_audio_mixer_mix(mixer, 1, one);
  CHECK(one[0] > 0);
  qa_audio_mixer_stop_all(mixer);
  source.server_milliseconds = 10000;
  source.has_delay_seconds = false;
  qa_audio_mixer_start_q2_sound(mixer, &short_sound, source, auto_command(),
                                NULL);
  qa_audio_mixer_mix(mixer, 1, one);
  CHECK(one[0] > 0);
  CHECK(qa_audio_mixer_sample_clock(mixer) == 113);
  free(out);
  qa_audio_mixer_destroy(mixer);
}

/* playback.test.ts: shared Q1/Q2 entity loops retain phase... */
static void test_source_loops_phase(void) {
  clock_value = 100;
  qa_audio_mixer *mixer = new_mixer(1000, 96);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  static int16_t loop_samples[4] = {1000, 2000, 3000, 4000};
  qa_pcm_sound loop = {0};
  loop.sample_rate = 1000;
  loop.channels = 1;
  loop.samples = loop_samples;
  loop.sample_count = 4;
  loop.frame_count = 4;
  loop.loop_start = -1;
  qa_source_loop_sound entries[2] = {{0}, {0}};
  entries[0].family = QA_SOUND_FAMILY_Q1;
  entries[0].entity = 2;
  entries[0].sound = &loop;
  entries[0].origin = origin_zero;
  entries[0].volume = 0.1;
  entries[1] = entries[0];
  entries[1].family = QA_SOUND_FAMILY_Q2;
  entries[1].entity = 3;
  qa_audio_mixer_set_source_loop_sounds(mixer, entries, 2);
  const qa_pcm_sound *sounds[8];
  size_t count = 0;
  channel_sounds(mixer, sounds, &count);
  CHECK(count == 2);
  int16_t first[6], refreshed[4];
  qa_audio_mixer_mix(mixer, 3, first);
  qa_audio_mixer_set_source_loop_sounds(mixer, entries, 2);
  qa_audio_mixer_mix(mixer, 2, refreshed);
  CHECK(refreshed[0] > first[4]);
  CHECK(refreshed[2] == first[0]);
  qa_audio_mixer_set_source_loop_sounds(mixer, NULL, 0);
  qa_source_sound_options source = {0};
  source.entity = 2;
  qa_vec3 side = {0, 80, 0};
  source.origin = fixed_origin(side);
  source.volume = 1;
  source.attenuation = 1;
  qa_audio_mixer_start_q2_sound(mixer, &loop, source, auto_command(), NULL);
  int16_t pan[2];
  qa_audio_mixer_mix(mixer, 1, pan);
  CHECK(pan[0] > 0);
  CHECK(pan[1] == 0);
  size_t volumes_count = 0;
  qa_mixer_channel_volume *volumes =
      qa_audio_mixer_channel_volumes(mixer, &volumes_count);
  CHECK(volumes_count == 1 && volumes[0].left == 255 && volumes[0].right == 0);
  free(volumes);
  CHECK(qa_audio_mixer_sample_clock(mixer) == 6);
  qa_audio_mixer_destroy(mixer);
}

/* playback.test.ts: ordinary Q3 Doppler exact PCM + wide Doppler. */
static void test_doppler(void) {
  clock_value = 0;
  qa_audio_mixer *mixer = new_mixer(1000, 96);
  static int16_t samples[2048];
  for (size_t i = 0; i < 2048; i++)
    samples[i] = (int16_t)(((int)(i % 13) - 6) * 3000);
  qa_pcm_sound sound = {0};
  sound.sample_rate = 1000;
  sound.channels = 1;
  sound.samples = samples;
  sound.sample_count = 2048;
  sound.frame_count = 2048;
  sound.loop_start = -1;
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  qa_frame_looping_sound_options options = {0};
  options.entity = 2;
  options.origin = (qa_vec3){100, 0, 0};
  options.velocity = (qa_vec3){1000, 0, 0};
  options.frame_number = 1;
  qa_audio_mixer_update_looping_sound(mixer, &sound, options);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  static const int16_t expected[24] = {
      -3474, -3474, -2895, -2895, -2316, -2316, -1737, -1737,
      -869,  -869,  0,     0,     578,   578,   1157,  1157,
      1736,  1736,  2605,  2605,  3473,  3473,  -3474, -3474};
  int16_t out[24];
  qa_audio_mixer_mix(mixer, 12, out);
  CHECK(memcmp(out, expected, sizeof expected) == 0);
  qa_audio_mixer_destroy(mixer);

  /* (1 + 639)^2 / 100 = 4096: two complete cycles average to zero. */
  mixer = new_mixer(1000, 96);
  static int16_t split_samples[2048];
  for (size_t i = 0; i < 2048; i++) split_samples[i] = i < 1024 ? 12000 : -12000;
  sound.samples = split_samples;
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  options.origin = (qa_vec3){1, 0, 0};
  options.velocity = (qa_vec3){639, 0, 0};
  qa_audio_mixer_update_looping_sound(mixer, &sound, options);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  int16_t wide[64];
  qa_audio_mixer_mix(mixer, 32, wide);
  CHECK(all_zero(wide, 64));
  qa_audio_mixer_set_doppler_enabled(mixer, false);
  options.frame_number = 2;
  qa_audio_mixer_update_looping_sound(mixer, &sound, options);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  qa_audio_mixer_mix(mixer, 32, wide);
  CHECK(all_positive(wide, 64));
  qa_audio_mixer_destroy(mixer);
}

/* playback.test.ts: pure Q3 effects/loops grow past allocation. */
static void test_growth(void) {
  clock_value = 100;
  qa_audio_mixer *small = new_mixer(1000, 2);
  qa_audio_mixer *roomy = new_mixer(1000, 8);
  static int16_t buffers[3][8];
  qa_pcm_sound sounds[3];
  for (int s = 0; s < 3; s++) {
    for (int i = 0; i < 8; i++) buffers[s][i] = (int16_t)((s + 1) * 1000);
    memset(&sounds[s], 0, sizeof sounds[s]);
    sounds[s].sample_rate = 1000;
    sounds[s].channels = 1;
    sounds[s].samples = buffers[s];
    sounds[s].sample_count = 8;
    sounds[s].frame_count = 8;
    sounds[s].loop_start = -1;
  }
  qa_audio_mixer *mixers[2] = {small, roomy};
  for (int m = 0; m < 2; m++) {
    qa_audio_mixer_set_listener(mixers[m], 1, origin_zero, axis_x);
    for (int s = 0; s < 3; s++) {
      qa_start_sound_options start = {0};
      start.entity = s + 2;
      start.channel = 0;
      start.origin = local_origin();
      start.volume = 127;
      CHECK(qa_audio_mixer_start_sound(mixers[m], &sounds[s], start, NULL));
    }
    const qa_pcm_sound *found[8];
    size_t count = 0;
    channel_sounds(mixers[m], found, &count);
    CHECK(count == 3);
  }
  int16_t small_out[2], roomy_out[2];
  qa_audio_mixer_mix(small, 1, small_out);
  qa_audio_mixer_mix(roomy, 1, roomy_out);
  CHECK(small_out[0] > 0);
  CHECK(memcmp(small_out, roomy_out, sizeof small_out) == 0);
  for (int m = 0; m < 2; m++) {
    qa_audio_mixer_stop_all(mixers[m]);
    for (int s = 0; s < 3; s++) {
      qa_real_looping_sound_options options = {0};
      options.entity = s + 2;
      options.origin = origin_zero;
      options.velocity = origin_zero;
      qa_audio_mixer_update_real_looping_sound(mixers[m], &sounds[s], options);
    }
    qa_audio_mixer_set_listener(mixers[m], 1, origin_zero, axis_x);
  }
  qa_audio_mixer_mix(small, 1, small_out);
  qa_audio_mixer_mix(roomy, 1, roomy_out);
  CHECK(small_out[0] > 0);
  CHECK(memcmp(small_out, roomy_out, sizeof small_out) == 0);
  CHECK(qa_audio_mixer_sample_clock(small) == 2);
  qa_audio_mixer_destroy(small);
  qa_audio_mixer_destroy(roomy);
}

static qa_unified_audio *new_engine(double rate) {
  qa_unified_audio_options options = {0};
  options.has_sample_rate = rate != 0;
  options.sample_rate = rate;
  options.milliseconds = milliseconds;
  options.random = zero_random;
  qa_unified_audio *audio = qa_unified_audio_create(&options);
  CHECK(audio != NULL);
  return audio;
}

static qa_audio_listener listener_for(qa_seat_id seat, bool has_actor,
                                      qa_actor_id actor, double gain) {
  qa_audio_listener listener = {0};
  listener.seat = seat;
  listener.has_actor = has_actor;
  listener.actor = actor;
  listener.origin = origin_zero;
  listener.axis = axis_x;
  listener.gain = gain;
  listener.underwater = false;
  return listener;
}

/* playback.test.ts: seat routing, replacement and pause. */
static void test_engine_seats(void) {
  clock_value = 100;
  qa_seat_id seat0 = qa_identity_owner_seat(&identity, 0);
  qa_seat_id seat1 = qa_identity_owner_seat(&identity, 1);
  qa_actor_id actor0 = qa_identity_owner_actor(&identity, 1, 0);
  qa_actor_id actor1 = qa_identity_owner_actor(&identity, 2, 0);
  qa_unified_audio *audio = new_engine(22050);
  qa_audio_listener listeners[2] = {listener_for(seat0, true, actor0, 1),
                                    listener_for(seat1, true, actor1, 0)};
  qa_unified_audio_set_listeners(audio, listeners, 2);
  qa_play_sound play = {0};
  play.sound = &asset;
  play.family = QA_SOUND_FAMILY_Q1;
  play.has_actor = true;
  play.actor = actor0;
  play.origin.kind = QA_SOUND_ORIGIN_LOCAL;
  play.audience.kind = QA_AUDIO_AUDIENCE_SEAT;
  play.audience.seat = seat0;
  play.channel = 1;
  play.volume = 1;
  play.attenuation = 1;
  CHECK(qa_unified_audio_play(audio, &play) == 1);
  int16_t first[64];
  qa_unified_audio_mix(audio, 32, first);
  CHECK(first[0] > 0);
  qa_unified_audio_pause(audio, true);
  qa_unified_audio_mix(audio, 32, first);
  CHECK(all_zero(first, 64));
  CHECK(qa_unified_audio_sample_clock(audio) == 32);
  qa_unified_audio_pause(audio, false);
  qa_audio_audience world = {0};
  world.kind = QA_AUDIO_AUDIENCE_WORLD;
  qa_unified_audio_stop_sound(audio, actor0, "weapon", world);
  qa_unified_audio_mix(audio, 32, first);
  CHECK(all_zero(first, 64));
  play.actor = actor1;
  play.has_actor = true;
  play.audience.seat = seat1;
  qa_unified_audio_play(audio, &play);
  qa_unified_audio_mix(audio, 32, first);
  CHECK(all_zero(first, 64));
  qa_unified_audio_destroy(audio);
}

/* playback.test.ts: component Q3 loops/channels stay independent. */
static void test_engine_q3_components(void) {
  clock_value = 100;
  qa_seat_id seat = qa_identity_owner_seat(&identity, 0);
  qa_actor_id listener = qa_identity_owner_actor(&identity, 40, 0);
  qa_actor_id actor = qa_identity_owner_actor(&identity, 41, 0);
  qa_unified_audio *audio = new_engine(22050);
  qa_audio_listener listeners[1] = {listener_for(seat, true, listener, 1)};
  qa_unified_audio_set_listeners(audio, listeners, 1);
  qa_loop_sound loop = {0};
  loop.sound = &asset;
  loop.family = QA_SOUND_FAMILY_Q3;
  loop.actor = actor;
  loop.origin.kind = QA_SOUND_ORIGIN_FIXED;
  loop.origin.position = origin_zero;
  loop.audience.kind = QA_AUDIO_AUDIENCE_SEAT;
  loop.audience.seat = seat;
  loop.volume = 0.1;
  loop.attenuation = 0;
  loop.velocity = origin_zero;
  loop.frame_number = 1;
  loop.lifetime = QA_LOOP_LIFETIME_PERSISTENT;
  qa_unified_audio_loop(audio, &loop);
  loop.owner = "mod:first";
  qa_unified_audio_loop(audio, &loop);
  loop.owner = "mod:second";
  qa_unified_audio_loop(audio, &loop);
  qa_unified_audio_end_loop_frame(audio);
  int16_t sample[2];
  qa_unified_audio_mix(audio, 1, sample);
  int16_t three = sample[0];
  qa_unified_audio_clear_q3_seat_loops(audio, seat, true, "mod:first");
  qa_unified_audio_end_loop_frame(audio);
  qa_unified_audio_mix(audio, 1, sample);
  int16_t two = sample[0];
  qa_unified_audio_stop_q3_seat_loop(audio, seat, actor, NULL);
  qa_unified_audio_end_loop_frame(audio);
  qa_unified_audio_mix(audio, 1, sample);
  int16_t one = sample[0];
  CHECK(one > 0);
  CHECK(two > one);
  CHECK(three > two);
  qa_unified_audio_release_q3_seat_owner(audio, seat, "mod:second");
  qa_unified_audio_end_loop_frame(audio);
  qa_unified_audio_mix(audio, 1, sample);
  CHECK(all_zero(sample, 2));
  qa_unified_audio_update_actor(audio, actor, origin_zero);
  qa_play_sound shot = {0};
  shot.sound = &asset;
  shot.family = QA_SOUND_FAMILY_Q3;
  shot.has_actor = true;
  shot.actor = actor;
  shot.origin.kind = QA_SOUND_ORIGIN_ACTOR;
  shot.origin.actor = actor;
  shot.audience.kind = QA_AUDIO_AUDIENCE_SEAT;
  shot.audience.seat = seat;
  shot.volume = 0.1;
  shot.attenuation = 1;
  shot.channel = 4;
  qa_unified_audio_play(audio, &shot);
  shot.owner = "mod:first";
  qa_unified_audio_play(audio, &shot);
  shot.owner = "mod:second";
  qa_unified_audio_play(audio, &shot);
  qa_unified_audio_mix(audio, 1, sample);
  int16_t all_shots = sample[0];
  qa_unified_audio_release_q3_seat_owner(audio, seat, "mod:first");
  qa_unified_audio_mix(audio, 1, sample);
  CHECK(all_shots > sample[0]);
  CHECK(sample[0] > 0);
  qa_unified_audio_update_actor(audio, actor, (qa_vec3){10000, 0, 0});
  qa_unified_audio_end_loop_frame(audio);
  qa_unified_audio_mix(audio, 1, sample);
  CHECK(all_zero(sample, 2));
  qa_unified_audio_destroy(audio);
}

/* playback.test.ts: Q1 frame/persistent loops through seat reset. */
static void test_engine_q1_loops(void) {
  clock_value = 100;
  qa_seat_id seat = qa_identity_owner_seat(&identity, 0);
  qa_actor_id listener = qa_identity_owner_actor(&identity, 20, 0);
  qa_actor_id frame_actor = qa_identity_owner_actor(&identity, 21, 0);
  qa_actor_id persistent_actor = qa_identity_owner_actor(&identity, 22, 0);
  qa_unified_audio *audio = new_engine(22050);
  qa_audio_listener listeners[1] = {listener_for(seat, true, listener, 1)};
  qa_unified_audio_set_listeners(audio, listeners, 1);
  qa_loop_sound loop = {0};
  loop.sound = &asset;
  loop.family = QA_SOUND_FAMILY_Q1;
  loop.origin.kind = QA_SOUND_ORIGIN_FIXED;
  loop.origin.position = (qa_vec3){0, 100, 0};
  loop.audience.kind = QA_AUDIO_AUDIENCE_SEAT;
  loop.audience.seat = seat;
  loop.volume = 0.1;
  loop.attenuation = 1;
  loop.velocity = origin_zero;
  loop.frame_number = 1;
  loop.actor = frame_actor;
  loop.lifetime = QA_LOOP_LIFETIME_FRAME;
  qa_unified_audio_loop(audio, &loop);
  loop.actor = persistent_actor;
  loop.lifetime = QA_LOOP_LIFETIME_PERSISTENT;
  qa_unified_audio_loop(audio, &loop);
  qa_unified_audio_end_loop_frame(audio);
  qa_unified_audio_clear_q3_seat_loops(audio, seat, true, NULL);
  int16_t both[2];
  qa_unified_audio_mix(audio, 1, both);
  CHECK(both[0] > 0);
  CHECK(both[1] == 0);
  qa_unified_audio_begin_loop_frame(audio);
  qa_unified_audio_end_loop_frame(audio);
  int16_t persistent[2];
  qa_unified_audio_mix(audio, 1, persistent);
  CHECK(persistent[0] > 0);
  CHECK(persistent[0] < both[0]);
  qa_audio_audience world = {0};
  world.kind = QA_AUDIO_AUDIENCE_WORLD;
  qa_unified_audio_stop_loop(audio, persistent_actor, world, NULL);
  int16_t silent[2];
  qa_unified_audio_mix(audio, 1, silent);
  CHECK(all_zero(silent, 2));
  CHECK(qa_unified_audio_sample_clock(audio) == 3);
  qa_unified_audio_destroy(audio);
}

/* Voice observer delivery through the engine (mixer events + seat). */
typedef struct recorded_events {
  int starts;
  int stops;
  qa_audio_voice_event last;
} recorded_events;
static void record_event(void *ctx, const qa_audio_voice_event *event) {
  recorded_events *recorded = ctx;
  if (event->kind == QA_AUDIO_VOICE_EVENT_START) recorded->starts++;
  if (event->kind == QA_AUDIO_VOICE_EVENT_STOP) recorded->stops++;
  recorded->last = *event;
}

static void test_engine_voice_events(void) {
  clock_value = 100;
  qa_seat_id seat = qa_identity_owner_seat(&identity, 0);
  qa_actor_id actor = qa_identity_owner_actor(&identity, 1, 0);
  qa_unified_audio *audio = new_engine(22050);
  recorded_events recorded = {0};
  int64_t subscription =
      qa_unified_audio_observe_voices(audio, record_event, &recorded);
  CHECK(subscription >= 1);
  qa_audio_listener listeners[1] = {listener_for(seat, true, actor, 1)};
  qa_unified_audio_set_listeners(audio, listeners, 1);
  qa_play_sound play = {0};
  play.sound = &asset;
  play.family = QA_SOUND_FAMILY_Q1;
  play.has_actor = true;
  play.actor = actor;
  play.origin.kind = QA_SOUND_ORIGIN_LOCAL;
  play.audience.kind = QA_AUDIO_AUDIENCE_WORLD;
  play.channel = 1;
  play.volume = 1;
  play.attenuation = 0;
  qa_unified_audio_play(audio, &play);
  CHECK(recorded.starts == 1);
  CHECK(qa_same_seat(recorded.last.seat, seat));
  CHECK(recorded.last.sound == &asset);
  int16_t *out = malloc(2048 * sizeof *out);
  CHECK(out != NULL);
  qa_unified_audio_mix(audio, 1024, out);
  /* The 2048-frame tone at 22050->22050 ends within two mixes. */
  qa_unified_audio_mix(audio, 1024, out);
  qa_unified_audio_mix(audio, 1, out);
  CHECK(recorded.stops == 1);
  CHECK(recorded.last.reason == QA_AUDIO_VOICE_STOP_ENDED);
  free(out);
  qa_unified_audio_unobserve_voices(audio, subscription);
  qa_unified_audio_destroy(audio);
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
/* Minimal valid mono WAV: 16-bit, 4 frames. */
static size_t make_wav(uint8_t *bytes) {
  memset(bytes, 0, 52);
  put_text(bytes, 0, "RIFF");
  put_u32(bytes, 4, 44);
  put_text(bytes, 8, "WAVEfmt ");
  put_u32(bytes, 16, 16);
  put_u16(bytes, 20, 1);
  put_u16(bytes, 22, 1);
  put_u32(bytes, 24, 11025);
  put_u32(bytes, 28, 22050);
  put_u16(bytes, 32, 2);
  put_u16(bytes, 34, 16);
  put_text(bytes, 36, "data");
  put_u32(bytes, 40, 8);
  put_u16(bytes, 44, 100);
  put_u16(bytes, 46, 200);
  put_u16(bytes, 48, 300);
  put_u16(bytes, 50, 400);
  return 52;
}

/* The aborting decoders keep aborting; only the try variants (and
 * the bank over them) are quiet. */
static void test_try_decode(void) {
  uint8_t garbage[12] = {82, 73, 70, 70, 1, 2, 3, 4, 5, 6, 7, 8};
  qa_decoded_wav decoded;
  CHECK(qa_try_decode_wav(garbage, sizeof garbage, "bad", &decoded) ==
        QA_WAV_TRY_INVALID);
  CHECK(qa_try_decode_quake_wav(garbage, sizeof garbage, "bad", &decoded) ==
        QA_WAV_TRY_INVALID);
  qa_pcm_sound pcm;
  CHECK(qa_try_decode_sound_bytes(garbage, sizeof garbage, "bad", &pcm) ==
        QA_PCM_TRY_INVALID);
  qa_pcm_stream *stream = NULL;
  CHECK(qa_try_open_pcm_bytes(garbage, sizeof garbage, "bad", &stream) ==
            QA_PCM_TRY_INVALID &&
        stream == NULL);
  uint8_t wav[52];
  size_t count = make_wav(wav);
  CHECK(qa_try_decode_wav(wav, count, "good", &decoded) == QA_WAV_TRY_OK);
  CHECK(decoded.pcm.frame_count == 4 && decoded.pcm.samples[3] == 400);
  qa_decoded_wav_dispose(&decoded);
  CHECK(qa_try_decode_quake_wav(wav, count, "good", &decoded) ==
        QA_WAV_TRY_OK);
  qa_decoded_wav_dispose(&decoded);
  CHECK(qa_try_open_pcm_bytes(wav, count, "good", &stream) == QA_PCM_TRY_OK);
  CHECK(stream != NULL && qa_pcm_stream_frame_count(stream) == 4);
  qa_pcm_stream_close(stream);
  CHECK_ABORTS((void)qa_decode_wav(garbage, sizeof garbage, "bad", &decoded));
}

static bool write_file(const char *path, const uint8_t *bytes, size_t count) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) return false;
  bool ok = fwrite(bytes, 1, count, file) == count;
  fclose(file);
  return ok;
}

/* undecodable.test.ts, through a real loose mount (the TS test
 * subclasses MountedContent in memory). */
static void test_bank(void) {
  char root_template[] = "/tmp/qa-bank-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  if (root == NULL) return;
  char path[1024];
  snprintf(path, sizeof path, "%s/sound", root);
  CHECK(mkdir(path, 0777) == 0);
  snprintf(path, sizeof path, "%s/sound/misc", root);
  CHECK(mkdir(path, 0777) == 0);
  snprintf(path, sizeof path, "%s/music", root);
  CHECK(mkdir(path, 0777) == 0);
  snprintf(path, sizeof path, "%s/sound/player", root);
  CHECK(mkdir(path, 0777) == 0);
  snprintf(path, sizeof path, "%s/sound/player/male", root);
  CHECK(mkdir(path, 0777) == 0);
  uint8_t garbage[12] = {82, 73, 70, 70, 1, 2, 3, 4, 5, 6, 7, 8};
  snprintf(path, sizeof path, "%s/sound/misc/bad.wav", root);
  CHECK(write_file(path, garbage, sizeof garbage));
  snprintf(path, sizeof path, "%s/music/track02.ogg", root);
  CHECK(write_file(path, garbage, sizeof garbage));
  uint8_t wav[52];
  size_t wav_count = make_wav(wav);
  snprintf(path, sizeof path, "%s/sound/misc/good.wav", root);
  CHECK(write_file(path, wav, wav_count));
  snprintf(path, sizeof path, "%s/music/track.wav", root);
  CHECK(write_file(path, wav, wav_count));
  snprintf(path, sizeof path, "%s/sound/player/male/jump.wav", root);
  CHECK(write_file(path, wav, wav_count));

  qa_content_identity content_identity = {QA_GAME_FAMILY_Q1, "test", "sound",
                                          "1"};
  char *content_id = qa_create_content_id(content_identity);
  char *mount_id = qa_create_mount_id("sound-memory", "main");
  char *plan_id = qa_create_mount_plan_id("sound-memory", "r1");
  CHECK(content_id != NULL && mount_id != NULL && plan_id != NULL);
  qa_mount_identity mount_identity =
      qa_create_mount_identity(mount_id, content_id, 0);
  qa_content_mount mounts[1] = {{0}};
  mounts[0].kind = QA_CONTENT_MOUNT_LOOSE;
  mounts[0].as.loose.identity = mount_identity;
  mounts[0].as.loose.root_path = root;
  const qa_mount_id order[1] = {mount_id};
  qa_resolved_mount_plan plan = {0};
  plan.id = plan_id;
  plan.mounts = mounts;
  plan.mount_count = 1;
  plan.default_order = order;
  plan.default_order_count = 1;
  qa_open_mount_options options = {0};
  qa_content_mounted *mounted = qa_open_mount_plan(&plan, &options);
  CHECK(mounted != NULL);
  qa_sound_bank *bank = qa_sound_bank_create(mounted);
  CHECK(bank != NULL);

  const qa_sound_asset *sound_asset = NULL;
  CHECK(qa_sound_bank_register(bank, "misc/bad.wav", QA_SOUND_FAMILY_Q1,
                               &sound_asset));
  CHECK(sound_asset == NULL);
  qa_pcm_stream *music = NULL;
  CHECK(qa_sound_bank_open_music(bank, "music/track02.ogg", NULL, &music));
  CHECK(music == NULL);
  CHECK(qa_sound_bank_register(bank, "misc/missing.wav", QA_SOUND_FAMILY_Q1,
                               &sound_asset));
  CHECK(sound_asset == NULL);

  CHECK(qa_sound_bank_register(bank, "misc/good.wav", QA_SOUND_FAMILY_Q1,
                               &sound_asset));
  CHECK(sound_asset != NULL);
  if (sound_asset != NULL) {
    CHECK(sound_asset->pcm.frame_count == 4);
    CHECK(strcmp(sound_asset->name, "sound/misc/good.wav") == 0);
    CHECK(qa_sound_bank_get(bank, sound_asset->resource,
                            QA_SOUND_FAMILY_Q1) == sound_asset);
    CHECK(qa_sound_bank_get(bank, sound_asset->resource,
                            QA_SOUND_FAMILY_Q3) == NULL);
    const qa_sound_asset *again = NULL;
    CHECK(qa_sound_bank_register(bank, "sound/misc/good.wav",
                                 QA_SOUND_FAMILY_Q1, &again));
    CHECK(again == sound_asset);
    qa_resource_id resource = sound_asset->resource;
    char *resource_copy = malloc(strlen(resource) + 1);
    CHECK(resource_copy != NULL);
    strcpy(resource_copy, resource);
    qa_sound_bank_begin_registration(bank);
    qa_sound_bank_end_registration(bank);
    CHECK(qa_sound_bank_get(bank, resource_copy, QA_SOUND_FAMILY_Q1) == NULL);
    free(resource_copy);
  }

  /* Sexed fallback: only sound/player/male/jump.wav exists. */
  const qa_sound_asset *sexed = NULL;
  CHECK(qa_sound_bank_register_sexed_sound(bank, "*jump.wav", "female/x",
                                           &sexed));
  CHECK(sexed != NULL);
  if (sexed != NULL)
    CHECK(strcmp(sexed->name, "sound/player/male/jump.wav") == 0);
  const qa_sound_asset *plain = NULL;
  CHECK(qa_sound_bank_register_sexed_sound(bank, "misc/good.wav", "female",
                                           &plain));
  CHECK(plain != NULL);

  CHECK(qa_sound_bank_open_music(bank, "music/track.wav", content_id,
                                 &music));
  CHECK(music != NULL);
  if (music != NULL) {
    CHECK(qa_pcm_stream_frame_count(music) == 4);
    qa_pcm_stream_close(music);
    music = NULL;
  }
  qa_content_identity other_identity = {QA_GAME_FAMILY_Q2, "test", "sound",
                                        "1"};
  char *other_id = qa_create_content_id(other_identity);
  CHECK(other_id != NULL);
  CHECK(qa_sound_bank_open_music(bank, "music/track.wav", other_id, &music));
  CHECK(music == NULL);

  qa_sound_bank_destroy(bank);
  qa_content_mounted_destroy(mounted);
  free(other_id);
  free(content_id);
  free(mount_id);
  free(plan_id);
  snprintf(path, sizeof path, "%s/sound/misc/bad.wav", root);
  unlink(path);
  snprintf(path, sizeof path, "%s/sound/misc/good.wav", root);
  unlink(path);
  snprintf(path, sizeof path, "%s/music/track02.ogg", root);
  unlink(path);
  snprintf(path, sizeof path, "%s/music/track.wav", root);
  unlink(path);
  snprintf(path, sizeof path, "%s/sound/player/male/jump.wav", root);
  unlink(path);
  snprintf(path, sizeof path, "%s/sound/player/male", root);
  rmdir(path);
  snprintf(path, sizeof path, "%s/sound/player", root);
  rmdir(path);
  snprintf(path, sizeof path, "%s/sound/misc", root);
  rmdir(path);
  snprintf(path, sizeof path, "%s/sound", root);
  rmdir(path);
  snprintf(path, sizeof path, "%s/music", root);
  rmdir(path);
  rmdir(root);
}

static bool dummy_driver(void) {
  const char *driver = getenv("SDL_AUDIODRIVER");
  return driver != NULL && strcmp(driver, "dummy") == 0;
}

/* startup-queue.test.ts child cases (need the dummy device). */
static void test_startup_queue(void) {
  if (!dummy_driver()) {
    printf("audio-engine: startup-queue cases skipped (no dummy driver)\n");
    return;
  }
  clock_value = 0;
  /* Cold fill: 200 ms at 44100 without entering running history. */
  {
    qa_unified_audio *audio = new_engine(0);
    static int16_t samples[10000];
    for (int i = 0; i < 10000; i++) samples[i] = (int16_t)(i + 1);
    qa_audio_stream_target target = {0};
    target.id = "startup";
    target.audience.kind = QA_AUDIO_AUDIENCE_WORLD;
    target.gain = 1;
    qa_stream_pcm chunk = {0};
    chunk.samples_s16 = samples;
    chunk.sample_count = 10000;
    chunk.channels = 1;
    chunk.sample_rate = 44100;
    chunk.source_sample = 0;
    chunk.reset_stream = true;
    qa_unified_audio_queue_stream(audio, target, &chunk);
    CHECK(qa_unified_audio_open_device(audio, NULL, 0) ==
          QA_SDL_AUDIO_OPEN_OK);
    CHECK(qa_unified_audio_output_state_of(audio) == QA_UNIFIED_AUDIO_PAUSED);
    CHECK(qa_unified_audio_pump(audio, false, 0, 2485.7373) == 8820);
    CHECK(qa_unified_audio_output_state_of(audio) == QA_UNIFIED_AUDIO_PLAYING);
    qa_unified_audio_pause(audio, true);
    size_t pending_count = 0;
    int16_t *pending = qa_unified_audio_pending_output(audio, &pending_count);
    CHECK(pending_count > 0 && pending_count <= 17640);
    bool matches = true;
    for (size_t i = 0; i < pending_count; i++) {
      int16_t expected =
          (int16_t)(8821 - (int)pending_count / 2 + (int)(i / 2));
      if (pending[i] != expected) matches = false;
    }
    CHECK(matches);
    free(pending);
    qa_unified_audio_destroy(audio);
  }
  /* Explicit lookahead (including zero), then the running default. */
  const int64_t lookaheads[3] = {0, 400, 88200};
  for (int i = 0; i < 3; i++) {
    qa_unified_audio *audio = new_engine(0);
    CHECK(qa_unified_audio_open_device(audio, NULL, 0) ==
          QA_SDL_AUDIO_OPEN_OK);
    CHECK(qa_unified_audio_pump(audio, true, (double)lookaheads[i], 5000) ==
          lookaheads[i]);
    CHECK(qa_unified_audio_sample_clock(audio) == lookaheads[i]);
    CHECK(qa_unified_audio_output_state_of(audio) == QA_UNIFIED_AUDIO_PLAYING);
    qa_unified_audio_stop_all(audio);
    CHECK(qa_unified_audio_pump(audio, false, 0, 0) == 3528);
    qa_unified_audio_destroy(audio);
  }
  /* The second pump measures running work. */
  {
    qa_unified_audio *audio = new_engine(0);
    CHECK(qa_unified_audio_open_device(audio, NULL, 0) ==
          QA_SDL_AUDIO_OPEN_OK);
    CHECK(qa_unified_audio_pump(audio, false, 0, 5000) == 8820);
    qa_unified_audio_stop_all(audio);
    int64_t expected = (int64_t)ceil(151.261312 * 44.1) + 2048;
    CHECK(qa_unified_audio_pump(audio, false, 0, 151.261312) == expected);
    qa_unified_audio_stop_all(audio);
    CHECK(qa_unified_audio_pump(audio, false, 0, 5000) == 88200);
    qa_unified_audio_destroy(audio);
  }
  /* Handoff: paused PCM survives, invalid lookahead aborts. */
  {
    qa_unified_audio *previous = new_engine(0);
    qa_unified_audio *next = new_engine(0);
    static int16_t samples[10000];
    for (int i = 0; i < 10000; i++) samples[i] = 321;
    qa_audio_stream_target target = {0};
    target.id = "handoff";
    target.audience.kind = QA_AUDIO_AUDIENCE_WORLD;
    target.gain = 1;
    qa_stream_pcm chunk = {0};
    chunk.samples_s16 = samples;
    chunk.sample_count = 10000;
    chunk.channels = 1;
    chunk.sample_rate = 44100;
    chunk.reset_stream = true;
    qa_unified_audio_queue_stream(previous, target, &chunk);
    CHECK(qa_unified_audio_open_device(previous, NULL, 0) ==
          QA_SDL_AUDIO_OPEN_OK);
    qa_unified_audio_pump(previous, false, 0, 0);
    qa_unified_audio_pause(previous, true);
    size_t pending_count = 0;
    int16_t *pending =
        qa_unified_audio_pending_output(previous, &pending_count);
    int64_t painted = qa_unified_audio_sample_clock(previous);
    qa_unified_audio_transfer transfer =
        qa_unified_audio_prepare_output_transfer(previous, next);
    size_t before_count = 0;
    int16_t *before = qa_unified_audio_pending_output(previous, &before_count);
    CHECK(before_count == pending_count &&
          (pending_count == 0 ||
           memcmp(before, pending, pending_count * sizeof *pending) == 0));
    free(before);
    CHECK(qa_unified_audio_output_state_of(previous) ==
          QA_UNIFIED_AUDIO_PAUSED);
    CHECK(qa_unified_audio_output_state_of(next) == QA_UNIFIED_AUDIO_DETACHED);
    qa_unified_audio_transfer_commit(transfer);
    size_t next_count = 0;
    int16_t *moved = qa_unified_audio_pending_output(next, &next_count);
    CHECK(next_count == pending_count &&
          (pending_count == 0 ||
           memcmp(moved, pending, pending_count * sizeof *pending) == 0));
    free(moved);
    CHECK(qa_unified_audio_sample_clock(previous) == painted);
    CHECK(qa_unified_audio_sample_clock(next) == 0);
    CHECK(qa_unified_audio_pump(next, false, 0, 8416.735532) == 0);
    qa_unified_audio_pause(next, false);
    CHECK(qa_unified_audio_pump(next, false, 0, 8416.735532) <= 8820);
    CHECK(qa_unified_audio_sample_clock(next) <= 8820);
    CHECK_ABORTS(qa_unified_audio_pump(next, true, -1, 0));
    free(pending);
    qa_unified_audio_destroy(previous);
    qa_unified_audio_destroy(next);
  }
}

/* playback.test.ts output-selection case (dummy driver only),
 * reduced to the retained-PCM and failure-recovery spine. */
static void test_output_selection(void) {
  if (!dummy_driver()) return;
  clock_value = 100;
  qa_seat_id seat = qa_identity_owner_seat(&identity, 0);
  qa_actor_id actor = qa_identity_owner_actor(&identity, 1, 0);
  qa_unified_audio *audio = new_engine(0);
  qa_audio_listener listeners[1] = {listener_for(seat, true, actor, 0.5)};
  qa_unified_audio_set_listeners(audio, listeners, 1);
  qa_unified_audio_set_effects_volume(audio, 0.3);
  qa_play_sound play = {0};
  play.sound = &asset;
  play.family = QA_SOUND_FAMILY_Q1;
  play.has_actor = true;
  play.actor = actor;
  play.origin.kind = QA_SOUND_ORIGIN_LOCAL;
  play.audience.kind = QA_AUDIO_AUDIENCE_SEAT;
  play.audience.seat = seat;
  play.channel = 1;
  play.volume = 1;
  play.attenuation = 1;
  qa_unified_audio_play(audio, &play);
  CHECK(qa_unified_audio_open_device(audio, NULL, 0) == QA_SDL_AUDIO_OPEN_OK);
  size_t name_count = 0;
  char **names = qa_unified_audio_output_device_names(audio, &name_count);
  CHECK(name_count >= 1);
  if (name_count == 0) {
    qa_sdl_audio_device_names_free(names, name_count);
    qa_unified_audio_destroy(audio);
    return;
  }
  qa_unified_audio_pump(audio, true, 2048, 0);
  qa_unified_audio_pause(audio, true);
  int64_t clock = qa_unified_audio_sample_clock(audio);
  size_t pending_count = 0;
  int16_t *pending = qa_unified_audio_pending_output(audio, &pending_count);
  bool nonzero = false;
  for (size_t i = 0; i < pending_count; i++)
    if (pending[i] != 0) nonzero = true;
  CHECK(nonzero);
  qa_unified_audio_detach_output(audio);
  CHECK(qa_unified_audio_output_state_of(audio) == QA_UNIFIED_AUDIO_DETACHED);
  size_t after_count = 0;
  int16_t *after = qa_unified_audio_pending_output(audio, &after_count);
  CHECK(after_count == pending_count &&
        (pending_count == 0 ||
         memcmp(after, pending, pending_count * sizeof *pending) == 0));
  free(after);
  CHECK(qa_unified_audio_select_output(audio, names[0], NULL, false) ==
        QA_SDL_AUDIO_OPEN_OK);
  qa_unified_audio_output_configuration config;
  CHECK(qa_unified_audio_output_configuration_get(audio, &config));
  CHECK(config.device_name != NULL &&
        strcmp(config.device_name, names[0]) == 0);
  CHECK(config.sample_rate == 44100);
  CHECK(qa_unified_audio_output_state_of(audio) == QA_UNIFIED_AUDIO_PAUSED);
  CHECK(qa_unified_audio_sample_clock(audio) == clock);
  CHECK(qa_unified_audio_select_output(audio,
                                       "nonexistent-quake-audio-output", NULL,
                                       false) == QA_SDL_AUDIO_OPEN_UNAVAILABLE);
  CHECK(qa_unified_audio_output_configuration_get(audio, &config));
  CHECK(config.device_name != NULL &&
        strcmp(config.device_name, names[0]) == 0);
  CHECK(qa_unified_audio_output_state_of(audio) == QA_UNIFIED_AUDIO_PAUSED);
  after = qa_unified_audio_pending_output(audio, &after_count);
  CHECK(after_count == pending_count &&
        (pending_count == 0 ||
         memcmp(after, pending, pending_count * sizeof *pending) == 0));
  free(after);
  free(pending);
  qa_sdl_audio_device_names_free(names, name_count);
  qa_unified_audio_destroy(audio);
}

/* MixerSoundMemory preparation: sample reads go through memory. */
typedef struct counting_memory {
  int reads;
  int touches;
  bool out_of_bounds;
} counting_memory;
static double memory_frames(void *ctx, const qa_pcm_sound *sound) {
  (void)ctx;
  return (double)sound->frame_count;
}
static bool memory_has_data(void *ctx, const qa_pcm_sound *sound) {
  (void)ctx;
  (void)sound;
  return true;
}
static double memory_sample(void *ctx, const qa_pcm_sound *sound, double frame) {
  counting_memory *memory = ctx;
  memory->reads++;
  if (frame < 0 || frame >= (double)sound->sample_count) {
    memory->out_of_bounds = true;
    return 0;
  }
  return sound->samples[(size_t)frame];
}
static void memory_touch(void *ctx, const qa_pcm_sound *sound, double ms) {
  counting_memory *memory = ctx;
  (void)sound;
  (void)ms;
  memory->touches++;
}

static void test_sound_memory(void) {
  clock_value = 0;
  qa_audio_mixer *mixer = new_mixer(1000, 96);
  counting_memory counting = {0};
  qa_mixer_sound_memory memory = {0};
  memory.ctx = &counting;
  memory.frame_count = memory_frames;
  memory.has_data = memory_has_data;
  memory.sample = memory_sample;
  memory.touch = memory_touch;
  qa_audio_mixer_bind_sound_memory(mixer, memory);
  static int16_t samples[2048];
  for (size_t i = 0; i < 2048; i++)
    samples[i] = (int16_t)(((int)(i % 13) - 6) * 3000);
  qa_pcm_sound sound = {0};
  sound.sample_rate = 1000;
  sound.channels = 1;
  sound.samples = samples;
  sound.sample_count = 2048;
  sound.frame_count = 2048;
  sound.loop_start = -1;
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  qa_frame_looping_sound_options options = {0};
  options.entity = 2;
  options.origin = (qa_vec3){100, 0, 0};
  options.velocity = (qa_vec3){1000, 0, 0};
  options.frame_number = 1;
  qa_audio_mixer_update_looping_sound(mixer, &sound, options);
  qa_audio_mixer_set_listener(mixer, 1, origin_zero, axis_x);
  static const int16_t expected[24] = {
      -3474, -3474, -2895, -2895, -2316, -2316, -1737, -1737,
      -869,  -869,  0,     0,     578,   578,   1157,  1157,
      1736,  1736,  2605,  2605,  3473,  3473,  -3474, -3474};
  int16_t out[24];
  qa_audio_mixer_mix(mixer, 12, out);
  CHECK(memcmp(out, expected, sizeof expected) == 0);
  CHECK(counting.reads > 0);
  CHECK(!counting.out_of_bounds);
  CHECK(counting.touches > 0);
  qa_audio_mixer_destroy(mixer);
}

/* queueRaw/queueMusic paint through the raw ring at unity gain. */
static void test_raw_queue(void) {
  clock_value = 100;
  qa_audio_mixer *mixer = new_mixer(1000, 96);
  static int16_t samples[2] = {1000, -1000};
  qa_pcm_sound sound = {0};
  sound.sample_rate = 1000;
  sound.channels = 1;
  sound.samples = samples;
  sound.sample_count = 2;
  sound.frame_count = 2;
  sound.loop_start = -1;
  qa_audio_mixer_queue_raw(mixer, &sound, 1);
  CHECK(qa_audio_mixer_raw_end(mixer) == 2);
  int32_t *raw = qa_audio_mixer_raw_sample_pointer(mixer);
  CHECK(raw[0] == 256000 && raw[2] == -256000);
  int16_t out[4];
  qa_audio_mixer_mix(mixer, 2, out);
  CHECK(out[0] == 1000 && out[2] == -1000);
  qa_audio_mixer_set_music_volume(mixer, 1);
  qa_audio_mixer_queue_music(mixer, &sound);
  qa_audio_mixer_mix(mixer, 2, out);
  CHECK(out[0] == 1000 && out[2] == -1000);
  /* Signed-byte stereo through the DataView entry point. */
  qa_audio_mixer_stop_all(mixer);
  const uint8_t bytes[4] = {16, 240, 32, 224}; /* +16,-16,+32,-32 */
  qa_audio_mixer_queue_raw_bytes(mixer, 2, 1000, 1, 2, bytes, sizeof bytes,
                                 1);
  qa_audio_mixer_mix(mixer, 2, out);
  CHECK(out[0] == 4096 && out[1] == -4096);
  CHECK(out[2] == 8192 && out[3] == -8192);
  qa_audio_mixer_destroy(mixer);
}

/* Engine stream lanes, checkpoints, music attach, and resetRound. */
static void test_engine_streams_music(void) {
  clock_value = 100;
  qa_unified_audio *audio = new_engine(22050);
  qa_audio_stream_target target = {0};
  target.id = "movie";
  target.audience.kind = QA_AUDIO_AUDIENCE_WORLD;
  target.gain = 0.5;
  static int16_t samples[4] = {1000, 2000, 3000, 4000};
  qa_stream_pcm chunk = {0};
  chunk.samples_s16 = samples;
  chunk.sample_count = 4;
  chunk.channels = 1;
  chunk.sample_rate = 22050;
  chunk.reset_stream = true;
  qa_unified_audio_queue_stream(audio, target, &chunk);
  int16_t out[4];
  qa_unified_audio_mix(audio, 2, out);
  CHECK(out[0] == 500 && out[2] == 1000);
  qa_save_value *checkpoint =
      qa_unified_audio_capture_stream_checkpoint(audio, "movie");
  CHECK(checkpoint != NULL);
  qa_save_value *missing =
      qa_unified_audio_capture_stream_checkpoint(audio, "absent");
  CHECK(missing == NULL);
  qa_unified_audio_stop_stream(audio, "movie");
  qa_unified_audio_restore_stream_checkpoint(audio, target, checkpoint);
  qa_save_value_free(checkpoint);
  qa_unified_audio_mix(audio, 2, out);
  CHECK(out[0] == 1500 && out[2] == 2000);
  qa_unified_audio_pause_stream(audio, "movie", true);
  qa_unified_audio_mix(audio, 2, out);
  CHECK(all_zero(out, 4));

  qa_music_player player;
  qa_music_player_init(&player, 22050, QA_SOUND_FAMILY_Q1,
                       QA_MUSIC_VOLUME_SOURCE, NULL);
  qa_music_player_set_volume(&player, 1);
  static int16_t music_samples[2] = {700, -700};
  qa_pcm_sound music_pcm = {0};
  music_pcm.sample_rate = 22050;
  music_pcm.channels = 1;
  music_pcm.samples = music_samples;
  music_pcm.sample_count = 2;
  music_pcm.frame_count = 2;
  music_pcm.loop_start = -1;
  qa_music_player_start(&player, qa_memory_pcm_stream_create(&music_pcm),
                        qa_memory_pcm_stream_create(&music_pcm));
  qa_audio_stream_target music_target = {0};
  music_target.id = "music";
  music_target.audience.kind = QA_AUDIO_AUDIENCE_WORLD;
  music_target.gain = 1;
  qa_unified_audio_attach_music(audio, music_target, &player);
  qa_unified_audio_update_music(audio);
  qa_unified_audio_mix(audio, 2, out);
  CHECK(out[0] == 700 && out[2] == -700);
  qa_unified_audio_stop_music(audio, "music");
  qa_music_player_destroy(&player);

  /* resetRound retains the seat mixer for the next listener set. */
  qa_seat_id seat = qa_identity_owner_seat(&identity, 0);
  qa_actor_id actor = qa_identity_owner_actor(&identity, 1, 0);
  qa_audio_listener listeners[1] = {listener_for(seat, true, actor, 1)};
  qa_unified_audio_set_listeners(audio, listeners, 1);
  qa_unified_audio_reset_round(audio);
  qa_unified_audio_set_listeners(audio, listeners, 1);
  qa_play_sound play = {0};
  play.sound = &asset;
  play.family = QA_SOUND_FAMILY_Q1;
  play.has_actor = true;
  play.actor = actor;
  play.origin.kind = QA_SOUND_ORIGIN_LOCAL;
  play.audience.kind = QA_AUDIO_AUDIENCE_WORLD;
  play.channel = 1;
  play.volume = 1;
  play.attenuation = 0;
  CHECK(qa_unified_audio_play(audio, &play) == 1);
  qa_unified_audio_mix(audio, 1, out);
  CHECK(out[0] > 0);
  qa_unified_audio_destroy(audio);
}

static void test_aborts(void) {
  clock_value = 100;
  CHECK_ABORTS((void)qa_audio_mixer_create(0, milliseconds, NULL, 96, 2, 1024));
  qa_audio_mixer *mixer = new_mixer(22050, 96);
  int16_t out[2];
  CHECK_ABORTS(qa_audio_mixer_mix(mixer, -1, out));
  qa_pcm_sound stereo = tone;
  stereo.channels = 2;
  stereo.sample_count = 4096;
  stereo.frame_count = 2048;
  qa_start_sound_options start = {0};
  start.entity = 1;
  start.channel = 1;
  start.origin = local_origin();
  start.volume = 127;
  CHECK_ABORTS((void)qa_audio_mixer_start_sound(mixer, &stereo, start, NULL));
  qa_audio_mixer_destroy(mixer);

  qa_unified_audio *audio = new_engine(22050);
  qa_play_sound play = {0};
  play.sound = &asset;
  play.family = QA_SOUND_FAMILY_Q1;
  play.origin.kind = QA_SOUND_ORIGIN_LOCAL;
  play.audience.kind = QA_AUDIO_AUDIENCE_WORLD;
  play.channel = 1;
  play.volume = 2;
  play.attenuation = 0;
  CHECK_ABORTS((void)qa_unified_audio_play(audio, &play));
  qa_unified_audio_close(audio);
  play.volume = 1;
  CHECK_ABORTS((void)qa_unified_audio_play(audio, &play));
  qa_unified_audio_destroy(audio);
}

int main(void) {
  init_fixtures();
  test_shared_voices();
  test_q1_shared_clock();
  test_q1_dephase();
  test_full_pool();
  test_dense_clip();
  test_full_pan();
  test_q2_deadlines();
  test_q2_equal_deadlines();
  test_q2_drift();
  test_source_loops_phase();
  test_doppler();
  test_growth();
  test_engine_seats();
  test_engine_q3_components();
  test_engine_q1_loops();
  test_engine_voice_events();
  test_sound_memory();
  test_raw_queue();
  test_engine_streams_music();
  test_try_decode();
  test_bank();
  test_startup_queue();
  test_output_selection();
  test_aborts();
  qa_identity_owner_destroy(&identity);
  printf("audio-engine: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

