/*
 * Port of src/formats/images/limits.ts: game-scale decode limits shared by
 * every image decoder. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_LIMITS_H
#define QA_FORMATS_IMAGES_LIMITS_H

#include <stdbool.h>

#define QA_MAX_IMAGE_DIMENSION 16384
#define QA_MAX_IMAGE_PIXELS (8192.0 * 8192.0)
/* Decoded RGBA bytes a multi-frame image may retain across all its frames. */
#define QA_MAX_ANIMATION_BYTES (256.0 * 1024.0 * 1024.0)
#define QA_MAX_ANIMATION_FRAMES 4096

/* TS imageSizeAllowed: safe positive integers within the dimension and
 * pixel-count limits (doubles, so any TS number can be passed). */
bool qa_image_size_allowed(double width, double height);

#endif
