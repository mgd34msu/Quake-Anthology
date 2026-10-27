/*
 * Port of src/media/roq-presentation.ts (source
 * CIN_DrawCinematic/CIN_UploadCinematic buffer and resampling
 * rules). The returned level's pixels are an owned malloc'd copy;
 * free level.pixels.bytes. False only on OOM.
 * GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_ROQ_PRESENTATION_H
#define QA_MEDIA_ROQ_PRESENTATION_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/contracts/render.h"
#include "qa/media/roq.h"
#include "qa/media/roq_playback.h"

/* Q3 wall movies read a fixed 256-square region of linbuf,
 * including retained neighboring bytes. */
bool qa_source_roq_shader_pixels(qa_roq_scratch *scratch,
                                 qa_roq_frame_pointer pointer,
                                 qa_image_level *out);
/* Invoke after the queued rendering barrier to capture the
 * source's live linbuf bytes. */
bool qa_source_roq_ui_pixels(qa_roq_scratch *scratch,
                             qa_roq_frame_pointer pointer, int64_t width,
                             int64_t height, int64_t draw_width,
                             int64_t draw_height, bool dirty,
                             qa_image_level *out);

#endif
