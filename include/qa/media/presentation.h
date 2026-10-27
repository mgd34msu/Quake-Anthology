/*
 * Port of src/media/presentation.ts: dimension probing, the
 * upload-acknowledged CinematicImage, and FullscreenCinematic.
 * C accommodations: the allocator and apply callbacks are function
 * pointers plus ctx; a pending upload lives inside the image (its
 * operations borrow its pixel copy until completion); apply has no
 * error channel, so a failed TS apply (which leaves the upload
 * pending for retry) is fatal in C — re-calling resolve resumes at
 * the same operation. Draw commands are an owned array.
 * GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_PRESENTATION_H
#define QA_MEDIA_PRESENTATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/render.h"
#include "qa/media/playback.h"

/* Reads movie metadata without retaining another decoder or
 * output device. False only on OOM. */
bool qa_cinematic_dimensions(const qa_cinematic_source *source,
                             int64_t *width, int64_t *height);

typedef qa_renderer_image (*qa_cinematic_image_allocator_fn)(
    void *ctx, int64_t width, int64_t height, bool source_is_resource,
    qa_resolved_resource_reference source_resource,
    const char *source_name);

typedef struct qa_cinematic_image qa_cinematic_image;

typedef struct qa_cinematic_image_upload {
  qa_cinematic_image *image; /* owner */
  qa_image_resource_operation operations[2];
  size_t operation_count;
  /* Storage the operations view until completion. */
  uint8_t *pixels; /* owned */
  qa_image_level content;
  qa_image_level create_level;
  qa_renderer_image target;
  size_t next;
  bool completed;
} qa_cinematic_image_upload;
/* TS upload.complete(). */
void qa_cinematic_image_upload_complete(qa_cinematic_image_upload *upload);

/* Backend success advances uploads. */
struct qa_cinematic_image {
  qa_renderer_image current;
  qa_cinematic_image_allocator_fn allocate;
  void *allocate_ctx;
  bool uploaded;
  int64_t uploaded_revision;
  bool closed;
  bool executing;
  bool has_pending;
  qa_cinematic_image_upload pending;
  int64_t pending_revision;
};

void qa_cinematic_image_init(qa_cinematic_image *image,
                             qa_renderer_image current,
                             qa_cinematic_image_allocator_fn allocate,
                             void *allocate_ctx);
void qa_cinematic_image_destroy(qa_cinematic_image *image);
qa_renderer_image qa_cinematic_image_image(
    const qa_cinematic_image *image);
/* Pending upload, the same one while pending, or NULL when the
 * revision is already uploaded (or on OOM). */
qa_cinematic_image_upload *qa_cinematic_image_prepare(
    qa_cinematic_image *image, const qa_cinematic_frame *frame,
    int64_t revision);
qa_renderer_image qa_cinematic_image_resolve(
    qa_cinematic_image *image, const qa_cinematic_frame *frame,
    int64_t revision,
    void (*apply)(void *ctx, const qa_image_resource_operation *operation),
    void *apply_ctx);
/* The release-image operation, when one is owed. */
bool qa_cinematic_image_release(qa_cinematic_image *image,
                                qa_image_resource_operation *out);

typedef struct qa_fullscreen_cinematic_draw {
  qa_seat_id seat;
  qa_rect viewport;
  bool blank;
  qa_render_command *commands; /* owned */
  size_t command_count;
  qa_cinematic_image_upload *upload; /* borrowed; nullable */
} qa_fullscreen_cinematic_draw;
void qa_fullscreen_cinematic_draw_dispose(
    qa_fullscreen_cinematic_draw *draw);
/* TS draw.complete(). */
void qa_fullscreen_cinematic_draw_complete(
    qa_fullscreen_cinematic_draw *draw);

/* One seat owns its focus and viewport; callers clear only this
 * viewport for blank frames. */
typedef struct qa_fullscreen_cinematic {
  qa_cinematic_playback *playback; /* borrowed */
  qa_seat_id seat;
  qa_cinematic_image image;
  bool focus_paused;
} qa_fullscreen_cinematic;

void qa_fullscreen_cinematic_init(qa_fullscreen_cinematic *cinematic,
                                  qa_cinematic_playback *playback,
                                  qa_renderer_image image,
                                  qa_cinematic_image_allocator_fn allocate,
                                  void *allocate_ctx);
void qa_fullscreen_cinematic_destroy(qa_fullscreen_cinematic *cinematic);
/* False only on OOM; dispose the draw. */
bool qa_fullscreen_cinematic_prepare(qa_fullscreen_cinematic *cinematic,
                                     qa_rect viewport, qa_media_focus focus,
                                     qa_fullscreen_cinematic_draw *out);
qa_save_value *qa_fullscreen_cinematic_capture_checkpoint(
    qa_fullscreen_cinematic *cinematic);
void qa_fullscreen_cinematic_restore_checkpoint(
    qa_fullscreen_cinematic *cinematic, const qa_save_value *value);
bool qa_fullscreen_cinematic_close(qa_fullscreen_cinematic *cinematic,
                                   qa_image_resource_operation *out);

/* Q3's source SCR_AdjustFrom640 stretches within the selected
 * seat's viewport. */
qa_rect qa_cinematic_pixel_rect(qa_rect rect, qa_rect viewport);

#endif
