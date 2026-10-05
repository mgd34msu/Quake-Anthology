#ifndef QA_FRONTEND_IMAGE_INVENTORY_H
#define QA_FRONTEND_IMAGE_INVENTORY_H
#include "internal.h"
#include "qa/scene_save.h"
#include "scene_identity.h"
/* Whole actual owner graph; prepared physical owner order and exact content
 * views qualify immutable image namespace/animation aliases before holders. */
bool frontend_images_checkpoint(qa_frontend *, qa_buffer *, qa_error *);
/* A genuinely empty owner inventory succeeds with a NULL image set. */
bool frontend_images_restore(qa_frontend *, qa_bytes, const qa_q3_image_upload_options *,
    qa_scene_image_set **, qa_error *);
/* Read-only ordinal matching the whole-image codec's actual traversal. */
bool frontend_image_index(qa_frontend *, const qa_scene_image *, uint64_t *, qa_error *);
/* Full QARS owner continuation in the identical physical QFIM roster. QAIM and
 * the shared image namespace precede import; all resolver lookups are pure. */
bool frontend_image_owners_checkpoint(qa_frontend *, frontend_scene_namespace *, qa_buffer *, qa_error *);
bool frontend_image_owners_restore(qa_frontend *, frontend_scene_namespace *, qa_bytes, qa_error *);
#endif
