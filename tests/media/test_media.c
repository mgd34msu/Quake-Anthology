/*
 * Port of tests/media/cinematic.test.ts (the corpus-free cases:
 * RoQ stereo/hold, CIN boundaries and prefetch order, fullscreen
 * pause/transition, material upload acknowledgement, timeline
 * freezing, Ogg demux, and the CIN checkpoint continuation).
 * Retail-corpus and renderer-execution cases stay with the
 * TypeScript oracle. GPL-2.0-or-later.
 */
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

#include "qa/core/binary/index.h"
#include "qa/media.h"

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

typedef struct test_clock {
  double time;
} test_clock;

static double test_clock_sample(void *ctx) {
  return ((test_clock *)ctx)->time;
}

static qa_media_clock clock_of(test_clock *clock) {
  qa_media_clock media = {clock, test_clock_sample};
  return media;
}

static void finish_writer(qa_binary_writer *writer, uint8_t **out,
                          size_t *out_length) {
  *out = qa_binary_writer_finish(writer, out_length);
  qa_binary_writer_destroy(writer);
}

/* The TS shortCin() fixture. Caller frees. */
static uint8_t *short_cin(size_t *out_length) {
  qa_binary_writer writer;
  qa_binary_writer_init(&writer, 100000);
  const int header[5] = {2, 1, 22051, 1, 1};
  for (int i = 0; i < 5; i++) qa_binary_writer_i32(&writer, header[i]);
  uint8_t counts[65536];
  memset(counts, 0, sizeof counts);
  for (int context = 0; context < 256; context++) {
    counts[context * 256] = 1;
    counts[context * 256 + 1] = 1;
  }
  qa_binary_writer_bytes(&writer, counts, sizeof counts);
  for (int frame = 0; frame < 4; frame++) {
    qa_binary_writer_i32(&writer, frame == 0 ? 1 : 0);
    if (frame == 0) {
      uint8_t palette[768];
      memset(palette, 0, sizeof palette);
      palette[0] = 255;
      palette[4] = 255;
      qa_binary_writer_bytes(&writer, palette, sizeof palette);
    }
    qa_binary_writer_i32(&writer, 5);
    qa_binary_writer_i32(&writer, 2);
    qa_binary_writer_u8(&writer, frame % 2 == 0 ? 2 : 1);
    int64_t start = 0, end = 0;
    qa_cin_sample_range(frame, 22051, &start, &end);
    uint8_t pcm[4096];
    memset(pcm, 128 + frame, (size_t)(end - start));
    qa_binary_writer_bytes(&writer, pcm, (size_t)(end - start));
  }
  qa_binary_writer_i32(&writer, 2);
  uint8_t *bytes = NULL;
  finish_writer(&writer, &bytes, out_length);
  return bytes;
}

/* The TS synthetic RoQ fixture. Caller frees. */
static uint8_t *short_roq(size_t *out_length) {
  qa_binary_writer writer;
  qa_binary_writer_init(&writer, 256);
  qa_binary_writer_u16(&writer, 0x1084);
  qa_binary_writer_u32(&writer, 0xffffffffLL);
  qa_binary_writer_u16(&writer, 30);
  const struct {
    int id;
    uint8_t data[10];
    int length;
    int flags;
  } chunks[5] = {
      {0x1021, {1, 2, 3, 4}, 4, 0x0102},
      {0x1001, {8, 0, 8, 0, 8, 0, 4, 0}, 8, 0},
      {0x1002, {0, 0, 0, 0, 128, 128, 0, 0, 0, 0}, 10, 0x0101},
      {0x1011, {0, 128, 0}, 3, 0},
      {0x1013, {0}, 0, 0},
  };
  for (int i = 0; i < 5; i++) {
    qa_binary_writer_u16(&writer, chunks[i].id);
    qa_binary_writer_u32(&writer, chunks[i].length);
    qa_binary_writer_u16(&writer, chunks[i].flags);
    qa_binary_writer_bytes(&writer, chunks[i].data,
                           (size_t)chunks[i].length);
  }
  uint8_t *bytes = NULL;
  finish_writer(&writer, &bytes, out_length);
  return bytes;
}

static void noop_print(void *ctx, const char *text) {
  (void)ctx;
  (void)text;
}
static void noop_roq_audio(void *ctx, const qa_roq_playback_audio *audio) {
  (void)ctx;
  (void)audio;
}

static void test_roq(void) {
  size_t length = 0;
  uint8_t *bytes = short_roq(&length);
  qa_roq_decoder decoder;
  CHECK(qa_roq_decoder_init_bytes(&decoder, bytes, length, NULL, NULL));
  qa_roq_event event;
  CHECK(qa_roq_decoder_next(&decoder, &event));
  CHECK(event.kind == QA_ROQ_EVENT_AUDIO);
  CHECK(event.channels == 2);
  CHECK(event.sample_count == 4);
  const int16_t expected[4] = {257, 516, 266, 532};
  for (int i = 0; i < 4; i++) CHECK(event.samples[i] == expected[i]);
  qa_roq_event_dispose(&event);
  qa_roq_decoder_destroy(&decoder);

  test_clock clock = {0};
  qa_roq_playback_options options;
  memset(&options, 0, sizeof options);
  options.clock = clock_of(&clock);
  options.hold = true;
  options.on_audio = noop_roq_audio;
  options.developer_print = noop_print;
  qa_roq_playback playback;
  CHECK(qa_roq_playback_init_bytes(&playback, bytes, length, &options,
                                   NULL));
  qa_roq_playback_tick tick;
  CHECK(qa_roq_playback_run(&playback, clock_of(&clock), &tick));
  qa_roq_playback_tick_dispose(&tick);
  clock.time = 34;
  CHECK(qa_roq_playback_run(&playback, clock_of(&clock), &tick));
  CHECK(tick.has_frame);
  qa_roq_playback_tick_dispose(&tick);
  clock.time = 67;
  CHECK(qa_roq_playback_run(&playback, clock_of(&clock), &tick));
  CHECK(tick.status == QA_ROQ_PLAYBACK_HELD);
  qa_roq_playback_tick_dispose(&tick);
  qa_roq_playback_frame current;
  CHECK(qa_roq_playback_current_frame(&playback, &current));
  CHECK(current.rgba_length == 8 * 8 * 4);
  qa_cinematic_frame_dispose(&current);
  qa_roq_playback_destroy(&playback);
  free(bytes);
}

typedef struct audio_log {
  int64_t source_samples[16];
  int count;
} audio_log;

static void log_cin_audio(void *ctx, const qa_cinematic_audio *audio) {
  audio_log *log = (audio_log *)ctx;
  if (log->count < 16) log->source_samples[log->count] = audio->source_sample;
  log->count++;
}

static void test_cin(void) {
  int64_t start = 0, end = 0;
  qa_cin_sample_range(13, 22051, &start, &end);
  CHECK(start == 20475 && end == 22051);
  size_t length = 0;
  uint8_t *bytes = short_cin(&length);
  qa_cin_decoder decoder;
  CHECK(qa_cin_decoder_init_bytes(&decoder, bytes, length, NULL));
  qa_cin_frame first;
  CHECK(qa_cin_decoder_next(&decoder, &first));
  CHECK(!first.is_end);
  uint8_t *rgba = qa_cin_rgba(first.pixels, first.pixels_length,
                              first.palette, 768);
  const uint8_t expected[8] = {255, 0, 0, 255, 0, 255, 0, 255};
  CHECK(rgba != NULL && memcmp(rgba, expected, 8) == 0);
  free(rgba);
  CHECK(first.has_audio && first.audio.source_sample == 0);
  CHECK(first.audio.sample_count == 1575);
  qa_cin_frame_dispose(&first);
  qa_cin_decoder_destroy(&decoder);

  test_clock clock = {0};
  audio_log log;
  memset(&log, 0, sizeof log);
  qa_cin_playback_options options;
  memset(&options, 0, sizeof options);
  options.clock = clock_of(&clock);
  options.on_audio_ctx = &log;
  options.on_audio = log_cin_audio;
  qa_cin_playback playback;
  CHECK(qa_cin_playback_init_bytes(&playback, bytes, length, &options,
                                   NULL));
  qa_cin_playback_tick tick;
  clock.time = 142;
  CHECK(qa_cin_playback_run(&playback, clock_of(&clock),
                            QA_MEDIA_FOCUS_GAME, &tick));
  CHECK(tick.update_kind == QA_CIN_UPDATE_UNCHANGED);
  qa_cin_playback_tick_dispose(&tick);
  clock.time = 143;
  CHECK(qa_cin_playback_run(&playback, clock_of(&clock),
                            QA_MEDIA_FOCUS_GAME, &tick));
  CHECK(tick.update_kind == QA_CIN_UPDATE_FRAME && !tick.has_frame);
  qa_cin_playback_tick_dispose(&tick);
  CHECK(log.count == 2 && log.source_samples[0] == 0 &&
        log.source_samples[1] == 1575);
  clock.time = 215;
  CHECK(qa_cin_playback_run(&playback, clock_of(&clock),
                            QA_MEDIA_FOCUS_GAME, &tick));
  CHECK(tick.update_kind == QA_CIN_UPDATE_FRAME && tick.has_frame &&
        tick.frame.index == 1);
  qa_cin_playback_tick_dispose(&tick);
  qa_cin_playback_destroy(&playback);
  free(bytes);
}

typedef struct playback_events {
  qa_cinematic_end_reason completed[4];
  int completed_count;
  bool paused[4];
  int paused_count;
  int audio_count;
  int reset_count;
} playback_events;

static void events_audio(void *ctx, const qa_cinematic_audio *audio,
                         qa_cinematic_target target) {
  (void)audio;
  (void)target;
  ((playback_events *)ctx)->audio_count++;
}
static void events_reset(void *ctx, qa_cinematic_target target) {
  (void)target;
  ((playback_events *)ctx)->reset_count++;
}
static void events_pause(void *ctx, bool paused, qa_cinematic_target target) {
  (void)target;
  playback_events *events = (playback_events *)ctx;
  if (events->paused_count < 4) events->paused[events->paused_count] = paused;
  events->paused_count++;
}
static void events_complete(void *ctx, qa_cinematic_end_reason reason,
                            qa_cinematic_target target) {
  (void)target;
  playback_events *events = (playback_events *)ctx;
  if (events->completed_count < 4) {
    events->completed[events->completed_count] = reason;
  }
  events->completed_count++;
}

static qa_cinematic_options seat_options(test_clock *clock, qa_seat_id seat,
                                         playback_events *events) {
  qa_cinematic_options options;
  memset(&options, 0, sizeof options);
  options.clock = clock_of(clock);
  options.target.kind = QA_CINEMATIC_TARGET_SEAT;
  options.target.seat = seat;
  options.on_audio_ctx = events;
  options.on_audio = events_audio;
  options.on_audio_reset_ctx = events;
  options.on_audio_reset = events_reset;
  options.on_audio_pause_ctx = events;
  options.on_audio_pause = events_pause;
  options.on_complete_ctx = events;
  options.on_complete = events_complete;
  return options;
}

typedef struct image_factory {
  qa_renderer_image base;
  int next;
} image_factory;

static qa_renderer_image allocate_image(void *ctx, int64_t width,
                                        int64_t height,
                                        bool source_is_resource,
                                        qa_resolved_resource_reference resource,
                                        const char *source_name) {
  image_factory *factory = (image_factory *)ctx;
  qa_renderer_image image = factory->base;
  image.ordinal = factory->next++;
  image.width = (double)width;
  image.height = (double)height;
  image.source_is_resource = source_is_resource;
  image.source_resource = resource;
  image.source_name = source_name;
  return image;
}

static qa_renderer_image make_image(qa_identity_owner *owner, double ordinal,
                                    const char *name, double width,
                                    double height) {
  qa_renderer_image image;
  memset(&image, 0, sizeof image);
  image.owner.identity = owner;
  image.owner.session = owner->session;
  image.owner.generation = 0;
  image.ordinal = ordinal;
  image.source_is_resource = false;
  image.source_name = name;
  image.width = width;
  image.height = height;
  return image;
}

typedef struct transition_log {
  qa_seat_id seat;
  char commands[2][64];
  int command_count;
  int leave_count;
} transition_log;

static void host_send(void *ctx, qa_seat_id seat, const char *command) {
  transition_log *log = (transition_log *)ctx;
  CHECK(qa_same_seat(seat, log->seat));
  snprintf(log->commands[log->command_count], 64, "%s", command);
  log->command_count++;
}
static void host_append(void *ctx, qa_seat_id seat, const char *command) {
  (void)ctx;
  (void)seat;
  (void)command;
  CHECK(false);
}
static void host_leave(void *ctx, qa_seat_id seat) {
  transition_log *log = (transition_log *)ctx;
  CHECK(qa_same_seat(seat, log->seat));
  log->leave_count++;
}

static void test_fullscreen(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "media-smoke"));
  qa_seat_id seat = qa_identity_owner_seat(&identity, 1);
  test_clock clock = {0};
  playback_events events;
  memset(&events, 0, sizeof events);
  transition_log tlog;
  memset(&tlog, 0, sizeof tlog);
  tlog.seat = seat;
  qa_cinematic_transition transition;
  memset(&transition, 0, sizeof transition);
  transition.kind = QA_CINEMATIC_TRANSITION_Q2_NEXTSERVER;
  transition.server_count = 7;
  qa_cinematic_transition_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &tlog;
  host.send_client_command = host_send;
  host.append_command = host_append;
  host.leave_cinematic = host_leave;
  qa_cinematic_transition_binding binding =
      qa_cinematic_transition_make(seat, transition, host);

  qa_cinematic_options options = seat_options(&clock, seat, &events);
  /* Route completions through the transition, as the TS test does. */
  options.on_complete = events_complete;
  size_t length = 0;
  uint8_t *bytes = short_cin(&length);
  qa_cinematic_source source =
      qa_cinematic_bytes(QA_CINEMATIC_FORMAT_CIN, bytes, length, NULL);
  int64_t width = 0, height = 0;
  CHECK(qa_cinematic_dimensions(&source, &width, &height));
  CHECK(width == 2 && height == 1);
  qa_cinematic_playback playback;
  CHECK(qa_cinematic_playback_init(&playback, &source, &options, NULL));
  qa_renderer_image image = make_image(&identity, 1, "cinematic:test", 2, 1);
  image_factory factory = {image, 2};
  qa_fullscreen_cinematic fullscreen;
  qa_fullscreen_cinematic_init(&fullscreen, &playback, image, allocate_image,
                               &factory);
  qa_rect viewport = {400, 0, 400, 300};
  qa_fullscreen_cinematic_draw draw;
  CHECK(qa_fullscreen_cinematic_prepare(&fullscreen, viewport,
                                        QA_MEDIA_FOCUS_GAME, &draw));
  CHECK(qa_same_seat(draw.seat, seat));
  CHECK(draw.command_count == 3);
  CHECK(draw.commands[0].kind == QA_RENDER_COMMAND_IMAGE_RESOURCE);
  CHECK(draw.commands[1].kind == QA_RENDER_COMMAND_SET_COLOR);
  CHECK(draw.commands[2].kind == QA_RENDER_COMMAND_STRETCH_PIC);
  qa_fullscreen_cinematic_draw_complete(&draw);
  qa_fullscreen_cinematic_draw_dispose(&draw);
  clock.time = 100;
  qa_cinematic_playback_pause(&playback, true);
  clock.time = 1000;
  CHECK(qa_cinematic_playback_time_milliseconds(&playback) == 100);
  qa_cinematic_tick tick;
  CHECK(qa_cinematic_playback_tick(&playback, &tick));
  CHECK(tick.status == QA_CINEMATIC_PAUSED);
  qa_cinematic_tick_dispose(&tick);
  qa_cinematic_playback_pause(&playback, false);
  clock.time = 1050;
  CHECK(qa_cinematic_playback_time_milliseconds(&playback) == 150);
  qa_cinematic_playback_skip(&playback);
  qa_cinematic_playback_skip(&playback);
  if (events.completed_count > 0) {
    qa_cinematic_transition_fire(&binding, events.completed[0]);
    qa_cinematic_transition_fire(&binding, events.completed[0]);
  }
  CHECK(events.paused_count == 2 && events.paused[0] && !events.paused[1]);
  CHECK(events.completed_count == 1 &&
        events.completed[0] == QA_CINEMATIC_END_SKIPPED);
  CHECK(tlog.command_count == 1 &&
        strcmp(tlog.commands[0], "nextserver 7\n") == 0);
  CHECK(tlog.leave_count == 1);
  qa_image_resource_operation release;
  CHECK(qa_fullscreen_cinematic_close(&fullscreen, &release));
  CHECK(release.kind == QA_IMAGE_OP_RELEASE);
  qa_fullscreen_cinematic_destroy(&fullscreen);
  qa_cinematic_playback_destroy(&playback);
  qa_identity_owner_destroy(&identity);
  free(bytes);
}

typedef struct op_log {
  qa_image_resource_operation_kind kinds[8];
  int count;
} op_log;

static void log_operation(void *ctx,
                          const qa_image_resource_operation *operation) {
  op_log *log = (op_log *)ctx;
  if (log->count < 8) log->kinds[log->count] = operation->kind;
  log->count++;
}

static qa_cinematic_options material_options(test_clock *clock,
                                             playback_events *events) {
  qa_cinematic_options options;
  memset(&options, 0, sizeof options);
  options.clock = clock_of(clock);
  options.target.kind = QA_CINEMATIC_TARGET_MATERIAL;
  options.target.id = "wall";
  options.on_audio_ctx = events;
  options.on_audio = events_audio;
  options.on_audio_reset_ctx = events;
  options.on_audio_reset = events_reset;
  options.on_audio_pause_ctx = events;
  options.on_audio_pause = events_pause;
  options.on_complete_ctx = events;
  options.on_complete = events_complete;
  return options;
}

static void test_material(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "material-media"));
  test_clock clock = {0};
  playback_events events;
  memset(&events, 0, sizeof events);
  qa_cinematic_options options = material_options(&clock, &events);
  size_t length = 0;
  uint8_t *bytes = short_cin(&length);
  qa_cinematic_source source =
      qa_cinematic_bytes(QA_CINEMATIC_FORMAT_CIN, bytes, length, NULL);
  qa_cinematic_playback playback;
  CHECK(qa_cinematic_playback_init(&playback, &source, &options, NULL));
  qa_renderer_image image = make_image(&identity, 0, "wall", 2, 1);
  image_factory factory = {image, 1};
  qa_material_cinematic material;
  qa_material_cinematic_init(&material, &playback, image, allocate_image,
                             &factory, NULL, NULL);
  op_log log;
  memset(&log, 0, sizeof log);
  qa_renderer_image resolved;
  CHECK(qa_material_cinematic_resolve(&material, log_operation, &log,
                                      &resolved));
  CHECK(log.count == 1 && log.kinds[0] == QA_IMAGE_OP_CREATE);
  CHECK(qa_material_cinematic_resolve(&material, log_operation, &log,
                                      &resolved));
  CHECK(log.count == 1);
  qa_image_resource_operation release;
  CHECK(qa_material_cinematic_close(&material, &release));
  CHECK(release.kind == QA_IMAGE_OP_RELEASE);
  qa_material_cinematic_destroy(&material);
  qa_cinematic_playback_destroy(&playback);

  /* Malformed OGV fails before native admission. */
  uint8_t bad = 0;
  qa_cinematic_source bad_source =
      qa_cinematic_bytes(QA_CINEMATIC_FORMAT_OGV, &bad, 1, "intro.ogv");
  CHECK_ABORTS({
    qa_cinematic_playback bad_playback;
    qa_cinematic_playback_init(&bad_playback, &bad_source, &options, NULL);
  });
  qa_identity_owner_destroy(&identity);
  free(bytes);
}

static bool timelines_equal(qa_cinematic_timeline a, qa_cinematic_timeline b) {
  return strcmp(a.source, b.source) == 0 &&
         a.source_time_milliseconds == b.source_time_milliseconds &&
         a.elapsed_milliseconds == b.elapsed_milliseconds &&
         a.loop == b.loop && a.status == b.status;
}

static void test_timeline(void) {
  test_clock clock = {0};
  playback_events events;
  memset(&events, 0, sizeof events);
  qa_cinematic_options options = material_options(&clock, &events);
  options.target.id = "timeline";
  options.hold = true;
  size_t length = 0;
  uint8_t *bytes = short_cin(&length);
  qa_cinematic_source source = qa_cinematic_bytes(
      QA_CINEMATIC_FORMAT_CIN, bytes, length, "video/unit.cin");
  qa_cinematic_playback playback;
  CHECK(qa_cinematic_playback_init(&playback, &source, &options, NULL));
  qa_cinematic_tick tick;
  clock.time = 100;
  CHECK(qa_cinematic_playback_tick(&playback, &tick));
  qa_cinematic_tick_dispose(&tick);
  qa_cinematic_playback_pause(&playback, true);
  qa_cinematic_timeline paused = qa_cinematic_playback_timeline(&playback);
  clock.time = 9000;
  CHECK(timelines_equal(qa_cinematic_playback_timeline(&playback), paused));
  qa_cinematic_playback_pause(&playback, false);
  clock.time = 9100;
  CHECK(qa_cinematic_playback_tick(&playback, &tick));
  qa_cinematic_tick_dispose(&tick);
  CHECK(qa_cinematic_playback_timeline(&playback).elapsed_milliseconds ==
        200);
  for (int i = 0; i < 20 &&
                  qa_cinematic_playback_status(&playback) !=
                      QA_CINEMATIC_HELD;
       i++) {
    clock.time += 72;
    CHECK(qa_cinematic_playback_tick(&playback, &tick));
    qa_cinematic_tick_dispose(&tick);
  }
  CHECK(qa_cinematic_playback_status(&playback) == QA_CINEMATIC_HELD);
  qa_cinematic_timeline held = qa_cinematic_playback_timeline(&playback);
  clock.time += 10000;
  CHECK(qa_cinematic_playback_tick(&playback, &tick));
  qa_cinematic_tick_dispose(&tick);
  CHECK(timelines_equal(qa_cinematic_playback_timeline(&playback), held));
  CHECK(strcmp(qa_cinematic_playback_timeline(&playback).source,
               "video/unit.cin") == 0);
  qa_cinematic_playback_skip(&playback);
  qa_cinematic_playback_skip(&playback);
  qa_cinematic_playback_close(&playback);
  CHECK(events.completed_count == 1 &&
        events.completed[0] == QA_CINEMATIC_END_SKIPPED);
  qa_cinematic_playback_destroy(&playback);
  free(bytes);
}

static void write_u32_le(uint8_t *bytes, size_t offset, uint32_t value) {
  bytes[offset] = (uint8_t)value;
  bytes[offset + 1] = (uint8_t)(value >> 8);
  bytes[offset + 2] = (uint8_t)(value >> 16);
  bytes[offset + 3] = (uint8_t)(value >> 24);
}

static uint8_t *make_ogg_page(uint32_t serial, uint32_t sequence, uint8_t flags,
                         const uint8_t *lacing, size_t segment_count,
                         const uint8_t *payload, size_t payload_length,
                         size_t *out_length) {
  size_t length = 27 + segment_count + payload_length;
  uint8_t *bytes = (uint8_t *)calloc(length, 1);
  bytes[0] = 79;
  bytes[1] = 103;
  bytes[2] = 103;
  bytes[3] = 83;
  bytes[5] = flags;
  for (int i = 0; i < 8; i++) bytes[6 + i] = 0xff; /* granule -1 */
  write_u32_le(bytes, 14, serial);
  write_u32_le(bytes, 18, sequence);
  bytes[26] = (uint8_t)segment_count;
  memcpy(bytes + 27, lacing, segment_count);
  memcpy(bytes + 27 + segment_count, payload, payload_length);
  uint32_t crc = 0;
  for (size_t i = 0; i < length; i++) {
    crc ^= (uint32_t)bytes[i] << 24;
    for (int bit = 0; bit < 8; bit++) {
      crc = crc & 0x80000000u ? (crc << 1) ^ 0x04c11db7u : crc << 1;
    }
  }
  write_u32_le(bytes, 22, crc);
  *out_length = length;
  return bytes;
}

static void test_ogg(void) {
  uint8_t header[267];
  memset(header, 0, sizeof header);
  const uint8_t theora[7] = {128, 116, 104, 101, 111, 114, 97};
  memcpy(header, theora, 7);
  const uint8_t lacing_first[1] = {255};
  size_t first_length = 0;
  uint8_t *first = make_ogg_page(1, 0, 2, lacing_first, 1, header, 255,
                            &first_length);
  uint8_t tail[27];
  memset(tail, 0, sizeof tail);
  memcpy(tail, header + 255, 12);
  const uint8_t comment[7] = {129, 116, 104, 101, 111, 114, 97};
  const uint8_t setup[7] = {130, 116, 104, 101, 111, 114, 97};
  memcpy(tail + 12, comment, 7);
  memcpy(tail + 19, setup, 7);
  const uint8_t lacing_last[4] = {12, 7, 7, 1};
  size_t last_length = 0;
  uint8_t *last = make_ogg_page(1, 1, 5, lacing_last, 4, tail, sizeof tail,
                           &last_length);
  const uint8_t vorbis[7] = {1, 118, 111, 114, 98, 105, 115};
  const uint8_t lacing_audio[1] = {7};
  size_t audio_length = 0;
  uint8_t *audio = make_ogg_page(2, 0, 6, lacing_audio, 1, vorbis, 7,
                            &audio_length);
  size_t total = first_length + audio_length + last_length;
  uint8_t *bytes = (uint8_t *)malloc(total);
  memcpy(bytes, first, first_length);
  memcpy(bytes + first_length, audio, audio_length);
  memcpy(bytes + first_length + audio_length, last, last_length);
  qa_ogg_movie movie;
  CHECK(qa_decode_ogg_movie(bytes, total, &movie));
  CHECK(movie.video_count == 4);
  CHECK(movie.video[0].length == sizeof header &&
        memcmp(movie.video[0].data, header, sizeof header) == 0);
  CHECK(movie.video[3].last);
  CHECK(movie.audio != NULL && movie.audio_length == audio_length &&
        memcmp(movie.audio, audio, audio_length) == 0);
  qa_ogg_movie_dispose(&movie);
  uint8_t *corrupt = (uint8_t *)malloc(total);
  memcpy(corrupt, bytes, total);
  corrupt[total - 1] = 1;
  CHECK_ABORTS({
    qa_ogg_movie bad;
    qa_decode_ogg_movie(corrupt, total, &bad);
  });
  CHECK_ABORTS({
    qa_ogg_movie bad;
    qa_decode_ogg_movie(bytes, total - 1, &bad);
  });
  free(corrupt);
  free(bytes);
  free(first);
  free(last);
  free(audio);
}

static bool frames_equal(const qa_cinematic_frame *a,
                         const qa_cinematic_frame *b) {
  return a->width == b->width && a->height == b->height &&
         a->index == b->index && a->loop == b->loop &&
         a->source_time == b->source_time && a->time == b->time &&
         a->rgba_length == b->rgba_length &&
         (a->rgba_length == 0 ||
          memcmp(a->rgba, b->rgba, a->rgba_length) == 0);
}

static bool captures_equal(qa_cinematic_playback *a,
                           qa_cinematic_playback *b) {
  qa_save_value *left = qa_cinematic_playback_capture_checkpoint(a);
  qa_save_value *right = qa_cinematic_playback_capture_checkpoint(b);
  if (left == NULL || right == NULL) return false;
  size_t left_length = 0, right_length = 0;
  uint8_t *left_bytes = qa_encode_checkpoint_value(left, &left_length);
  uint8_t *right_bytes = qa_encode_checkpoint_value(right, &right_length);
  bool equal = left_bytes != NULL && right_bytes != NULL &&
               left_length == right_length &&
               memcmp(left_bytes, right_bytes, left_length) == 0;
  free(left_bytes);
  free(right_bytes);
  qa_save_value_free(left);
  qa_save_value_free(right);
  return equal;
}

static qa_save_value *roundtrip(qa_save_value *value) {
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  free(bytes);
  qa_save_value_free(value);
  return decoded;
}

static void test_checkpoint(void) {
  size_t length = 0;
  uint8_t *bytes = short_cin(&length);
  qa_cinematic_source source = qa_cinematic_bytes(
      QA_CINEMATIC_FORMAT_CIN, bytes, length, "continuation.cin");
  test_clock wall = {0};
  playback_events events_a, events_b, events_c;
  memset(&events_a, 0, sizeof events_a);
  memset(&events_b, 0, sizeof events_b);
  memset(&events_c, 0, sizeof events_c);
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "saved-movie"));
  qa_seat_id seat = qa_identity_owner_seat(&identity, 0);
  qa_cinematic_options options_a = seat_options(&wall, seat, &events_a);
  options_a.hold = true;
  qa_cinematic_playback uninterrupted;
  CHECK(qa_cinematic_playback_init(&uninterrupted, &source, &options_a,
                                   NULL));
  qa_cinematic_tick tick;
  const double before[3] = {0, 143, 215};
  for (int i = 0; i < 3; i++) {
    wall.time = before[i];
    CHECK(qa_cinematic_playback_tick(&uninterrupted, &tick));
    qa_cinematic_tick_dispose(&tick);
  }
  qa_cinematic_playback_pause(&uninterrupted, true);
  double elapsed = qa_cinematic_playback_time_milliseconds(&uninterrupted);
  qa_save_value *saved = roundtrip(
      qa_cinematic_playback_capture_checkpoint(&uninterrupted));
  CHECK(saved != NULL);
  test_clock restored_wall = {25000};
  qa_cinematic_options options_b =
      seat_options(&restored_wall, seat, &events_b);
  options_b.hold = true;
  qa_cinematic_playback resumed;
  CHECK(qa_cinematic_playback_init(&resumed, &source, &options_b, saved));
  CHECK(events_b.audio_count == 0);
  qa_cinematic_frame frame_a, frame_b;
  bool has_a = qa_cinematic_playback_current_frame(&uninterrupted, &frame_a);
  bool has_b = qa_cinematic_playback_current_frame(&resumed, &frame_b);
  CHECK(has_a == has_b && (!has_a || frames_equal(&frame_a, &frame_b)));
  if (has_a) qa_cinematic_frame_dispose(&frame_a);
  if (has_b) qa_cinematic_frame_dispose(&frame_b);
  CHECK(captures_equal(&resumed, &uninterrupted));
  qa_cinematic_playback_pause(&uninterrupted, false);
  qa_cinematic_playback_pause(&resumed, false);
  const double deltas[6] = {0, 30, 72, 144, 216, 1000};
  for (int i = 0; i < 6; i++) {
    wall.time = elapsed + deltas[i];
    restored_wall.time = 25000 + deltas[i];
    qa_cinematic_tick next_a, next_b;
    CHECK(qa_cinematic_playback_tick(&uninterrupted, &next_a));
    CHECK(qa_cinematic_playback_tick(&resumed, &next_b));
    CHECK(next_a.status == next_b.status &&
          next_a.changed == next_b.changed &&
          next_a.has_frame == next_b.has_frame &&
          (!next_a.has_frame || frames_equal(&next_a.frame, &next_b.frame)));
    qa_cinematic_tick_dispose(&next_a);
    qa_cinematic_tick_dispose(&next_b);
  }
  qa_cinematic_playback_skip(&uninterrupted);
  qa_cinematic_playback_skip(&resumed);
  CHECK(events_a.completed_count == events_b.completed_count &&
        events_a.completed_count == 1 &&
        events_a.completed[0] == events_b.completed[0]);
  qa_save_value *terminal_saved = roundtrip(
      qa_cinematic_playback_capture_checkpoint(&resumed));
  CHECK(terminal_saved != NULL);
  test_clock terminal_wall = {90000};
  qa_cinematic_options options_c =
      seat_options(&terminal_wall, seat, &events_c);
  options_c.hold = true;
  qa_cinematic_playback terminal;
  CHECK(qa_cinematic_playback_init(&terminal, &source, &options_c,
                                   terminal_saved));
  CHECK(qa_cinematic_playback_tick(&terminal, &tick));
  qa_cinematic_tick_dispose(&tick);
  qa_cinematic_playback_skip(&terminal);
  CHECK(events_c.completed_count == 0 && events_c.audio_count == 0);
  qa_cinematic_playback_close(&terminal);
  qa_cinematic_playback_destroy(&terminal);
  qa_save_value_free(terminal_saved);
  qa_cinematic_playback_close(&uninterrupted);
  qa_cinematic_playback_close(&resumed);
  qa_cinematic_playback_destroy(&uninterrupted);
  qa_cinematic_playback_destroy(&resumed);
  qa_save_value_free(saved);
  qa_identity_owner_destroy(&identity);
  free(bytes);
}

static void test_source(void) {
  const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  qa_media_input *input = qa_media_bytes(data, 8, "<test>");
  CHECK(input != NULL && input->byte_length == 8);
  uint8_t *bytes = qa_read_media(input, 6, 2);
  CHECK(bytes != NULL && bytes[0] == 7 && bytes[1] == 8);
  free(bytes);
  CHECK_ABORTS(qa_read_media(input, -1, 1));
  qa_media_input_close(input);
}

int main(void) {
  test_roq();
  test_cin();
  test_fullscreen();
  test_material();
  test_timeline();
  test_ogg();
  test_checkpoint();
  test_source();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
