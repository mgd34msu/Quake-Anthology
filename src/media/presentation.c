/* Port of src/media/presentation.ts. GPL-2.0-or-later. */
#include "qa/media/presentation.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static noreturn void media_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

static qa_media_input *dimension_open(void *ctx) {
  return qa_cinematic_source_open((const qa_cinematic_source *)ctx);
}

bool qa_cinematic_dimensions(const qa_cinematic_source *source,
                             int64_t *width, int64_t *height) {
  if (source->format == QA_CINEMATIC_FORMAT_OGV) {
    qa_media_input *input = qa_cinematic_source_open(source);
    if (input == NULL) return false;
    uint8_t *bytes = qa_read_media(input, 0, input->byte_length);
    size_t length = (size_t)input->byte_length;
    qa_media_input_close(input);
    if (bytes == NULL && length != 0) return false;
    qa_ogg_movie movie;
    if (!qa_decode_ogg_movie(bytes, length, &movie)) {
      free(bytes);
      return false;
    }
    free(bytes);
    qa_platform_ogg_packet headers[3];
    for (int i = 0; i < 3; i++) {
      headers[i].data = movie.video[i].data;
      headers[i].length = movie.video[i].length;
      headers[i].first = movie.video[i].first;
      headers[i].last = movie.video[i].last;
      headers[i].granule = movie.video[i].granule;
      headers[i].index = movie.video[i].index;
    }
    qa_theora_decoder decoder;
    memset(&decoder, 0, sizeof decoder);
    char error[256];
    error[0] = '\0';
    if (!qa_theora_decoder_init(&decoder, headers, 3, error,
                                sizeof error)) {
      qa_ogg_movie_dispose(&movie);
      media_error("%s", error[0] == '\0' ? "Theora decoder failed" : error);
    }
    *width = decoder.width;
    *height = decoder.height;
    qa_theora_decoder_close(&decoder);
    qa_ogg_movie_dispose(&movie);
    return true;
  }
  if (source->format == QA_CINEMATIC_FORMAT_IMAGE) {
    *width = source->width;
    *height = source->height;
    return true;
  }
  if (source->format == QA_CINEMATIC_FORMAT_CIN) {
    qa_media_input *input = qa_cinematic_source_open(source);
    if (input == NULL) return false;
    uint8_t *bytes = qa_read_media(input, 0, 8);
    qa_media_input_close(input);
    if (bytes == NULL) return false;
    qa_binary_reader reader;
    if (!qa_binary_reader_init(&reader, bytes, 8, source->source)) {
      free(bytes);
      return false;
    }
    int64_t w = qa_binary_reader_i32(&reader);
    int64_t h = qa_binary_reader_i32(&reader);
    qa_binary_reader_destroy(&reader);
    free(bytes);
    if (w <= 0 || h <= 0 || w * h > 0x1000000) {
      qa_range_error("Invalid CIN dimensions");
    }
    *width = w;
    *height = h;
    return true;
  }
  qa_roq_scratch scratch;
  if (!qa_roq_scratch_init(&scratch)) return false;
  qa_roq_stream *stream = qa_roq_stream_open(
      dimension_open, (void *)source, source->source, scratch.file,
      QA_ROQ_SCRATCH_FILE_LENGTH);
  if (stream == NULL) {
    qa_roq_scratch_destroy(&scratch);
    media_error("Empty cinematic: %s", source->source);
  }
  qa_roq_decoder decoder;
  qa_roq_decoder_options options;
  memset(&options, 0, sizeof options);
  options.silent = true;
  options.scratch = &scratch;
  if (!qa_roq_decoder_init_stream(&decoder, stream, source->source,
                                  &options)) {
    qa_roq_stream_destroy(stream);
    qa_roq_scratch_destroy(&scratch);
    return false;
  }
  bool found = false;
  bool ok = true;
  while (!found) {
    qa_roq_event event;
    if (!qa_roq_decoder_next_chunk(&decoder, NULL, NULL, NULL, NULL, NULL,
                                   NULL, &event)) {
      ok = false;
      break;
    }
    if (event.kind == QA_ROQ_EVENT_INFO) {
      *width = event.width;
      *height = event.height;
      found = true;
    } else if (event.kind == QA_ROQ_EVENT_END) {
      qa_roq_event_dispose(&event);
      qa_roq_decoder_destroy(&decoder);
      qa_roq_stream_destroy(stream);
      qa_roq_scratch_destroy(&scratch);
      media_error("Cinematic contains no video info: %s", source->source);
    }
    qa_roq_event_dispose(&event);
  }
  qa_roq_decoder_destroy(&decoder);
  qa_roq_stream_destroy(stream);
  qa_roq_scratch_destroy(&scratch);
  return ok;
}

void qa_cinematic_image_init(qa_cinematic_image *image,
                             qa_renderer_image current,
                             qa_cinematic_image_allocator_fn allocate,
                             void *allocate_ctx) {
  memset(image, 0, sizeof *image);
  image->current = current;
  image->allocate = allocate;
  image->allocate_ctx = allocate_ctx;
  image->uploaded_revision = -1;
}

void qa_cinematic_image_destroy(qa_cinematic_image *image) {
  free(image->pending.pixels);
  memset(image, 0, sizeof *image);
}

qa_renderer_image qa_cinematic_image_image(
    const qa_cinematic_image *image) {
  return image->has_pending ? image->pending.target : image->current;
}

static void clear_pending(qa_cinematic_image *image) {
  /* Keep the owner/completed markers so a stale upload pointer
   * still reports the TS completion errors instead of dangling. */
  bool completed = image->pending.completed;
  free(image->pending.pixels);
  memset(&image->pending, 0, sizeof image->pending);
  image->pending.image = image;
  image->pending.completed = completed;
  image->has_pending = false;
}

void qa_cinematic_image_upload_complete(qa_cinematic_image_upload *upload) {
  qa_cinematic_image *image = upload->image;
  if (upload->completed) {
    media_error("Cinematic upload completion was already used");
  }
  if (image->closed) {
    media_error("Cinematic upload completed after image release");
  }
  upload->completed = true;
  image->current = upload->target;
  image->uploaded = true;
  image->uploaded_revision = image->pending_revision;
  clear_pending(image);
}

qa_cinematic_image_upload *qa_cinematic_image_prepare(
    qa_cinematic_image *image, const qa_cinematic_frame *frame,
    int64_t revision) {
  if (image->closed) media_error("Cinematic image is closed");
  if (image->has_pending) return &image->pending;
  if (revision == image->uploaded_revision) return NULL;
  bool resized = (double)frame->width != image->current.width ||
                 (double)frame->height != image->current.height;
  qa_renderer_image target =
      resized ? image->allocate(image->allocate_ctx, frame->width,
                                frame->height,
                                image->current.source_is_resource,
                                image->current.source_resource,
                                image->current.source_name)
              : image->current;
  uint8_t *pixels = (uint8_t *)malloc(
      frame->rgba_length == 0 ? 1 : frame->rgba_length);
  if (pixels == NULL) return NULL;
  memcpy(pixels, frame->rgba, frame->rgba_length);
  qa_cinematic_image_upload *upload = &image->pending;
  memset(upload, 0, sizeof *upload);
  upload->image = image;
  upload->target = target;
  upload->pixels = pixels;
  upload->content.width = (double)frame->width;
  upload->content.height = (double)frame->height;
  upload->content.pixels.bytes = pixels;
  upload->content.pixels.length = frame->rgba_length;
  upload->create_level = upload->content;
  size_t count = 0;
  if (resized && image->uploaded) {
    qa_image_resource_operation *op = &upload->operations[count++];
    memset(op, 0, sizeof *op);
    op->kind = QA_IMAGE_OP_RELEASE;
    op->image = image->current;
  }
  qa_image_resource_operation *op = &upload->operations[count++];
  memset(op, 0, sizeof *op);
  op->image = target;
  if (image->uploaded && !resized) {
    op->kind = QA_IMAGE_OP_UPDATE;
    op->level = 0;
    op->update_level = &upload->content;
  } else {
    op->kind = QA_IMAGE_OP_CREATE;
    op->content.kind = QA_RENDER_IMAGE_RGBA8;
    op->content.levels = &upload->create_level;
    op->content.level_count = 1;
    op->content.border_color = (qa_vec4){0, 0, 0, 1};
    op->sampling.filter = "linear";
    op->sampling.wrap = "clamp";
  }
  upload->operation_count = count;
  image->pending_revision = revision;
  image->has_pending = true;
  return upload;
}

qa_renderer_image qa_cinematic_image_resolve(
    qa_cinematic_image *image, const qa_cinematic_frame *frame,
    int64_t revision,
    void (*apply)(void *ctx, const qa_image_resource_operation *operation),
    void *apply_ctx) {
  if (image->executing) media_error("Cinematic upload cannot reenter");
  image->executing = true;
  qa_cinematic_image_prepare(image, frame, revision);
  if (image->has_pending) {
    qa_cinematic_image_upload *pending = &image->pending;
    for (; pending->next < pending->operation_count; pending->next++) {
      const qa_image_resource_operation *operation =
          &pending->operations[pending->next];
      apply(apply_ctx, operation);
      if (operation->kind == QA_IMAGE_OP_RELEASE) image->uploaded = false;
    }
    qa_cinematic_image_upload_complete(pending);
  }
  image->executing = false;
  return image->current;
}

bool qa_cinematic_image_release(qa_cinematic_image *image,
                                qa_image_resource_operation *out) {
  if (image->executing) {
    media_error("Cannot release a cinematic during upload");
  }
  if (image->closed) return false;
  image->closed = true;
  clear_pending(image);
  if (!image->uploaded) return false;
  memset(out, 0, sizeof *out);
  out->kind = QA_IMAGE_OP_RELEASE;
  out->image = image->current;
  return true;
}

void qa_fullscreen_cinematic_draw_dispose(
    qa_fullscreen_cinematic_draw *draw) {
  free(draw->commands);
  memset(draw, 0, sizeof *draw);
}

void qa_fullscreen_cinematic_draw_complete(
    qa_fullscreen_cinematic_draw *draw) {
  if (draw->upload != NULL) qa_cinematic_image_upload_complete(draw->upload);
}

void qa_fullscreen_cinematic_init(qa_fullscreen_cinematic *cinematic,
                                  qa_cinematic_playback *playback,
                                  qa_renderer_image image,
                                  qa_cinematic_image_allocator_fn allocate,
                                  void *allocate_ctx) {
  memset(cinematic, 0, sizeof *cinematic);
  if (playback->target.kind != QA_CINEMATIC_TARGET_SEAT) {
    media_error("Fullscreen cinematic requires a seat target");
  }
  cinematic->playback = playback;
  cinematic->seat = playback->target.seat;
  qa_cinematic_image_init(&cinematic->image, image, allocate, allocate_ctx);
}

void qa_fullscreen_cinematic_destroy(qa_fullscreen_cinematic *cinematic) {
  qa_cinematic_image_destroy(&cinematic->image);
  memset(cinematic, 0, sizeof *cinematic);
}

bool qa_fullscreen_cinematic_prepare(qa_fullscreen_cinematic *cinematic,
                                     qa_rect viewport, qa_media_focus focus,
                                     qa_fullscreen_cinematic_draw *out) {
  memset(out, 0, sizeof *out);
  qa_cinematic_playback *playback = cinematic->playback;
  if (focus != QA_MEDIA_FOCUS_GAME &&
      qa_cinematic_playback_status(playback) == QA_CINEMATIC_PLAYING) {
    cinematic->focus_paused = true;
    qa_cinematic_playback_pause(playback, true);
  } else if (focus == QA_MEDIA_FOCUS_GAME && cinematic->focus_paused) {
    cinematic->focus_paused = false;
    qa_cinematic_playback_pause(playback, false);
  }
  qa_cinematic_tick tick;
  if (!qa_cinematic_playback_tick(playback, &tick)) return false;
  bool visible = focus != QA_MEDIA_FOCUS_MENU &&
                 tick.status != QA_CINEMATIC_ENDED &&
                 tick.status != QA_CINEMATIC_STOPPED && tick.has_frame;
  qa_cinematic_image_upload *upload = NULL;
  if (visible && tick.has_frame) {
    upload = qa_cinematic_image_prepare(
        &cinematic->image, &tick.frame,
        qa_cinematic_playback_revision(playback));
  }
  size_t capacity = (upload == NULL ? 0 : upload->operation_count) +
                    (visible ? 2 : 0);
  qa_render_command *commands = NULL;
  if (capacity != 0) {
    commands =
        (qa_render_command *)calloc(capacity, sizeof(qa_render_command));
    if (commands == NULL) {
      qa_cinematic_tick_dispose(&tick);
      return false;
    }
  }
  size_t count = 0;
  if (upload != NULL) {
    for (size_t i = 0; i < upload->operation_count; i++) {
      commands[count].kind = QA_RENDER_COMMAND_IMAGE_RESOURCE;
      commands[count].operation = upload->operations[i];
      count++;
    }
  }
  if (visible) {
    commands[count].kind = QA_RENDER_COMMAND_SET_COLOR;
    commands[count].color = (qa_vec4){1, 1, 1, 1};
    count++;
    commands[count].kind = QA_RENDER_COMMAND_STRETCH_PIC;
    commands[count].rect = viewport;
    commands[count].uv = (qa_texture_rect){0, 0, 1, 1};
    commands[count].image = qa_cinematic_image_image(&cinematic->image);
    count++;
  }
  qa_cinematic_tick_dispose(&tick);
  out->seat = cinematic->seat;
  out->viewport = viewport;
  out->blank = !visible;
  out->commands = commands;
  out->command_count = count;
  out->upload = upload;
  return true;
}

qa_save_value *qa_fullscreen_cinematic_capture_checkpoint(
    qa_fullscreen_cinematic *cinematic) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  qa_save_value *value = qa_save_value_boolean(cinematic->focus_paused);
  if (value == NULL || !qa_save_value_set(object, "focusPaused", value)) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

void qa_fullscreen_cinematic_restore_checkpoint(
    qa_fullscreen_cinematic *cinematic, const qa_save_value *value) {
  qa_save_reader r;
  if (!qa_save_reader_init(&r, value, "fullscreen-cinematic")) {
    media_error("out of memory");
  }
  qa_save_reader f;
  if (!qa_save_reader_field(&r, "focusPaused", &f)) {
    media_error("out of memory");
  }
  cinematic->focus_paused = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&r);
}

bool qa_fullscreen_cinematic_close(qa_fullscreen_cinematic *cinematic,
                                   qa_image_resource_operation *out) {
  bool has = qa_cinematic_image_release(&cinematic->image, out);
  qa_cinematic_playback_close(cinematic->playback);
  return has;
}

qa_rect qa_cinematic_pixel_rect(qa_rect rect, qa_rect viewport) {
  /* Math.fround staging: double products of frounded factors. */
  double scale_x = (float)(viewport.width / 640.0);
  double scale_y = (float)(viewport.height / 480.0);
  qa_rect out;
  out.x = viewport.x + trunc((float)((double)(float)rect.x * scale_x));
  out.y = viewport.y + trunc((float)((double)(float)rect.y * scale_y));
  out.width = trunc((float)((double)(float)rect.width * scale_x));
  out.height = trunc((float)((double)(float)rect.height * scale_y));
  return out;
}
