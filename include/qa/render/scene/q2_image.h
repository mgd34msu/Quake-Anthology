/* Port of src/render/scene/q2-image.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_Q2_IMAGE_H
#define QA_RENDER_SCENE_Q2_IMAGE_H
#include <stddef.h>
#include "qa/contracts/render.h"
typedef struct qa_q2_mipmapped_image {
  qa_render_image image;
  qa_image_level *levels; size_t level_count; /* owned chain (level 0 = gamma result) */
  void *_chain; void *_expanded; void *_scaled;
} qa_q2_mipmapped_image;
void qa_q2_mipmapped_image_destroy(qa_q2_mipmapped_image *image);
qa_render_image qa_q2_mipmapped_image_new(qa_render_image content, qa_q2_mipmapped_image *out);
#endif
