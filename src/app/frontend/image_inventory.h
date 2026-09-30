#ifndef QA_FRONTEND_IMAGE_INVENTORY_H
#define QA_FRONTEND_IMAGE_INVENTORY_H
#include "internal.h"
#include "qa/scene_save.h"
/* Whole actual owner graph; prepared physical owner order and exact content
 * views qualify immutable image namespace/animation aliases before holders. */
bool frontend_images_checkpoint(qa_frontend *, qa_buffer *, qa_error *);
/* A genuinely empty owner inventory succeeds with a NULL image set. */
bool frontend_images_restore(qa_frontend *, qa_bytes, qa_scene_image_set **, qa_error *);
/* Read-only ordinal matching the whole-image codec's actual traversal. */
bool frontend_image_index(qa_frontend *, const qa_scene_image *, uint64_t *, qa_error *);
#endif
