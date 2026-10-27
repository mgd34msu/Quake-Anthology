/* Port of src/render/scene/models/image-path.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_MODELS_IMAGE_PATH_H
#define QA_RENDER_SCENE_MODELS_IMAGE_PATH_H

/* Malloc'd normalized path; NULL only on OOM. Invalid paths abort
 * with the exact TS RangeError. Name is a NUL-terminated byte string,
 * so the TS embedded-NUL case scans as ending at the NUL. */
char *qa_model_image_path(const char *name);

#endif
