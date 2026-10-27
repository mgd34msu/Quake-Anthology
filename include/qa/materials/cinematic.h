/*
 * Port of src/materials/cinematic.ts: a dynamic image source that also
 * carries its currently bound renderer image. GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_CINEMATIC_H
#define QA_MATERIALS_CINEMATIC_H

#include "qa/contracts/render.h"

/* TS ShaderCinematicSource extends DynamicImageSource with image. */
typedef struct qa_shader_cinematic_source {
  qa_dynamic_image_source source;
  qa_renderer_image image;
} qa_shader_cinematic_source;

#endif
