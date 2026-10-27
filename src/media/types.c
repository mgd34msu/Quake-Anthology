/* Port of src/media/types.ts. GPL-2.0-or-later. */
#include "qa/media/types.h"

#include <stdlib.h>
#include <string.h>

const char *qa_cinematic_status_name(qa_cinematic_status status) {
  switch (status) {
    case QA_CINEMATIC_PLAYING: return "playing";
    case QA_CINEMATIC_PAUSED: return "paused";
    case QA_CINEMATIC_HELD: return "held";
    case QA_CINEMATIC_ENDED: return "ended";
    case QA_CINEMATIC_STOPPED: return "stopped";
  }
  return "playing";
}

const char *qa_cinematic_end_reason_name(qa_cinematic_end_reason reason) {
  switch (reason) {
    case QA_CINEMATIC_END_FINISHED: return "finished";
    case QA_CINEMATIC_END_SKIPPED: return "skipped";
    case QA_CINEMATIC_END_STOPPED: return "stopped";
  }
  return "finished";
}

void qa_cinematic_frame_dispose(qa_cinematic_frame *frame) {
  free(frame->rgba);
  memset(frame, 0, sizeof *frame);
}

bool qa_copy_cinematic_frame(const qa_cinematic_frame *frame,
                             qa_cinematic_frame *out) {
  memset(out, 0, sizeof *out);
  if (frame == NULL) return false;
  *out = *frame;
  out->rgba = NULL;
  if (frame->rgba_length != 0) {
    out->rgba = (uint8_t *)malloc(frame->rgba_length);
    if (out->rgba == NULL) {
      memset(out, 0, sizeof *out);
      return false;
    }
    memcpy(out->rgba, frame->rgba, frame->rgba_length);
  }
  return true;
}

void qa_cinematic_tick_dispose(qa_cinematic_tick *tick) {
  if (tick->has_frame) qa_cinematic_frame_dispose(&tick->frame);
  memset(tick, 0, sizeof *tick);
}

static bool set_number(qa_save_value *object, const char *key, double value) {
  qa_save_value *child = qa_save_value_number(value);
  return child != NULL && qa_save_value_set(object, key, child);
}

qa_save_value *qa_cinematic_frame_to_save(const qa_cinematic_frame *frame) {
  if (frame == NULL) return qa_save_value_null();
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  qa_save_value *rgba =
      qa_save_value_bytes(frame->rgba, frame->rgba_length);
  bool ok = rgba != NULL && qa_save_value_set(object, "rgba", rgba) &&
            set_number(object, "width", (double)frame->width) &&
            set_number(object, "height", (double)frame->height) &&
            set_number(object, "index", (double)frame->index) &&
            set_number(object, "loop", (double)frame->loop) &&
            set_number(object, "sourceTime", frame->source_time) &&
            set_number(object, "time", frame->time);
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

bool qa_read_cinematic_frame(const qa_save_reader *reader,
                             qa_cinematic_frame *out) {
  memset(out, 0, sizeof *out);
  if (qa_save_reader_is_null(reader)) return false;
  qa_save_reader width_r, height_r, rgba_r, index_r, loop_r, source_r, time_r;
  if (!qa_save_reader_field(reader, "width", &width_r)) return false;
  int64_t width = (int64_t)qa_save_reader_integer_min(&width_r, 1);
  qa_save_reader_destroy(&width_r);
  if (!qa_save_reader_field(reader, "height", &height_r)) return false;
  int64_t height = (int64_t)qa_save_reader_integer_min(&height_r, 1);
  qa_save_reader_destroy(&height_r);
  if (!qa_save_reader_field(reader, "rgba", &rgba_r)) return false;
  size_t rgba_length = 0;
  const uint8_t *rgba = qa_save_reader_bytes(&rgba_r, &rgba_length);
  uint8_t *copy = NULL;
  if (rgba_length != 0) {
    copy = (uint8_t *)malloc(rgba_length);
    if (copy == NULL) {
      qa_save_reader_destroy(&rgba_r);
      return false;
    }
    memcpy(copy, rgba, rgba_length);
  }
  qa_save_reader_destroy(&rgba_r);
  if ((double)width * (double)height * 4.0 != (double)rgba_length) {
    free(copy);
    qa_save_reader_fail(reader, "frame dimensions differ from pixels");
  }
  qa_cinematic_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.rgba = copy;
  frame.rgba_length = rgba_length;
  frame.width = width;
  frame.height = height;
  if (!qa_save_reader_field(reader, "index", &index_r)) goto oom;
  frame.index = (int64_t)qa_save_reader_integer_min(&index_r, 0);
  qa_save_reader_destroy(&index_r);
  if (!qa_save_reader_field(reader, "loop", &loop_r)) goto oom;
  frame.loop = (int64_t)qa_save_reader_integer_min(&loop_r, 0);
  qa_save_reader_destroy(&loop_r);
  if (!qa_save_reader_field(reader, "sourceTime", &source_r)) goto oom;
  frame.source_time = qa_save_reader_finite(&source_r);
  qa_save_reader_destroy(&source_r);
  if (!qa_save_reader_field(reader, "time", &time_r)) goto oom;
  frame.time = qa_save_reader_finite(&time_r);
  qa_save_reader_destroy(&time_r);
  *out = frame;
  return true;
oom:
  qa_cinematic_frame_dispose(&frame);
  return false;
}
