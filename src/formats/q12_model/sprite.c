/* Port of src/formats/q12-model/sprite.ts. GPL-2.0-or-later. */
#include "qa/formats/q12_model/sprite.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/formats/q12_model/common.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

/* Zeroed, so a caught BinaryError can release a partly read sprite. */
static void *xmalloc(size_t size) {
  void *p = calloc(size == 0 ? 1 : size, 1);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static int64_t read_orientation(qa_binary_reader *reader) {
  int32_t value = qa_binary_reader_i32(reader);
  switch (value) {
    case 0: case 1: case 2: case 3: case 4: return value;
    default:
      qa_binary_error(reader->source, (int64_t)reader->cursor, "invalid sprite orientation %d", (int)value);
  }
}

typedef struct {
  qa_binary_reader *reader;
} sprite_ctx;

static void *read_sprite_frame(void *ctx) {
  qa_binary_reader *reader = ((sprite_ctx *)ctx)->reader;
  qa_sprite_frame read;
  read.origin_x = qa_binary_reader_i32(reader);
  read.origin_y = qa_binary_reader_i32(reader);
  read.width = qa_q12_count(reader, "sprite width", 1);
  read.height = qa_q12_count(reader, "sprite height", 1);
  int64_t length = (int64_t)(read.width * read.height);
  read.pixels.bytes = qa_binary_reader_bytes(reader, length);
  read.pixels.length = (size_t)length;
  qa_sprite_frame *frame = xmalloc(sizeof *frame);
  *frame = read;
  return frame;
}

typedef struct spr_parse_state {
  qa_binary_reader reader;
  qa_q1_sprite_model *out;
} spr_parse_state;

static void spr_parse_release(void *raw) {
  spr_parse_state *state = raw;
  qa_binary_reader_destroy(&state->reader);
  qa_q1_sprite_model_destroy(state->out);
}

void qa_parse_spr(const uint8_t *data, size_t data_length, const char *source, qa_q1_sprite_model *out) {
  memset(out, 0, sizeof *out);
  spr_parse_state state;
  state.out = out;
  if (!qa_binary_reader_init(&state.reader, (uint8_t *)data, data_length, source != NULL ? source : "<spr>"))
    qa_range_error("Out of memory");
  qa_cleanup node;
  qa_cleanup_push(&node, spr_parse_release, &state);
  qa_binary_reader *reader = &state.reader;
  qa_binary_reader_expect_magic(reader, "IDSP");
  qa_q12_version(reader, 1);
  out->orientation = read_orientation(reader);
  out->bounding_radius = qa_binary_reader_finite_f32(reader);
  out->max_width = qa_q12_count(reader, "sprite maximum width", 1);
  out->max_height = qa_q12_count(reader, "sprite maximum height", 1);
  double frame_count = qa_q12_count(reader, "sprite frames", 1);
  out->beam_length = qa_binary_reader_finite_f32(reader);
  out->sync = qa_q12_sync_type(reader);
  if (out->bounding_radius < 0) qa_q12_fail(reader, "negative sprite radius");
  (void)qa_binary_reader_data_view(reader, (int64_t)reader->cursor, (int64_t)(frame_count * 21));
  qa_timed_frames *frames = xmalloc((size_t)frame_count * sizeof *frames);
  out->frames = frames;
  out->frame_count = (size_t)frame_count;
  sprite_ctx ctx = {reader};
  for (size_t i = 0; i < (size_t)frame_count; i++) qa_q12_read_timed(reader, read_sprite_frame, &ctx, &frames[i]);
  out->bounds.min.x = -out->max_width / 2;
  out->bounds.min.y = -out->max_width / 2;
  out->bounds.min.z = -out->max_height / 2;
  out->bounds.max.x = out->max_width / 2;
  out->bounds.max.y = out->max_width / 2;
  out->bounds.max.z = out->max_height / 2;
  qa_cleanup_pop(&node);
  qa_binary_reader_destroy(&state.reader);
}

void qa_q1_sprite_model_destroy(qa_q1_sprite_model *model) {
  if (model == NULL) return;
  for (size_t i = 0; i < model->frame_count; i++) {
    qa_timed_frames *frames = (qa_timed_frames *)&model->frames[i];
    if (!frames->is_group) {
      qa_sprite_frame *frame = (qa_sprite_frame *)frames->single_frame;
      if (frame != NULL) { free((void *)frame->pixels.bytes); free(frame); }
    } else {
      for (size_t j = 0; j < frames->frame_count; j++) {
        qa_sprite_frame *frame = (qa_sprite_frame *)frames->frames[j].frame;
        if (frame != NULL) { free((void *)frame->pixels.bytes); free(frame); }
      }
      free((void *)frames->frames);
    }
  }
  free((void *)model->frames);
  memset(model, 0, sizeof *model);
}

static qa_bounds sprite_bounds(const qa_q2_sprite_frame *frame) {
  double right = qa_js_max(fabs(frame->origin_x), fabs(frame->width - frame->origin_x));
  double up = qa_js_max(fabs(frame->origin_y), fabs(frame->height - frame->origin_y));
  double radius = qa_js_hypot2(right, up);
  qa_bounds bounds = {{-radius, -radius, -radius}, {radius, radius, radius}};
  return bounds;
}

typedef struct sp2_parse_state {
  qa_binary_reader reader;
  qa_q2_sprite_model *out;
} sp2_parse_state;

static void sp2_parse_release(void *raw) {
  sp2_parse_state *state = raw;
  qa_binary_reader_destroy(&state->reader);
  qa_q2_sprite_model_destroy(state->out);
}

void qa_parse_sp2(const uint8_t *data, size_t data_length, const char *source, qa_q2_sprite_model *out) {
  memset(out, 0, sizeof *out);
  sp2_parse_state state;
  state.out = out;
  if (!qa_binary_reader_init(&state.reader, (uint8_t *)data, data_length, source != NULL ? source : "<sp2>"))
    qa_range_error("Out of memory");
  qa_cleanup node;
  qa_cleanup_push(&node, sp2_parse_release, &state);
  qa_binary_reader *reader = &state.reader;
  qa_binary_reader_expect_magic(reader, "IDS2");
  qa_q12_version(reader, 2);
  double frame_count = qa_q12_count(reader, "sprite frames", 1);
  (void)qa_binary_reader_data_view(reader, (int64_t)reader->cursor, (int64_t)(frame_count * 80));
  qa_q2_sprite_frame *frames = xmalloc((size_t)frame_count * sizeof *frames);
  out->frames = frames;
  out->frame_count = (size_t)frame_count;
  for (size_t i = 0; i < (size_t)frame_count; i++) {
    frames[i].width = qa_q12_count(reader, "sprite width", 1);
    frames[i].height = qa_q12_count(reader, "sprite height", 1);
    frames[i].origin_x = qa_binary_reader_i32(reader);
    frames[i].origin_y = qa_binary_reader_i32(reader);
    frames[i].image = qa_binary_reader_fixed_byte_string(reader, 64);
  }
  qa_bounds *all = xmalloc((size_t)frame_count * sizeof *all);
  for (size_t i = 0; i < (size_t)frame_count; i++) all[i] = sprite_bounds(&frames[i]);
  out->bounds = qa_q12_union_bounds(all, (size_t)frame_count);
  free(all);
  qa_cleanup_pop(&node);
  qa_binary_reader_destroy(&state.reader);
}

void qa_q2_sprite_model_destroy(qa_q2_sprite_model *model) {
  if (model == NULL) return;
  for (size_t i = 0; i < model->frame_count; i++) free((void *)model->frames[i].image);
  free((void *)model->frames);
  memset(model, 0, sizeof *model);
}
