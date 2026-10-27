/* Port of src/render/scene/skin.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_SKIN_H
#define QA_RENDER_SCENE_SKIN_H
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
/* Result bytes malloc'd, caller frees; NULL only on OOM. Length is width*height. */
uint8_t *qa_flood_skin(const uint8_t *indices, size_t length, double width, double height,
                       qa_palette palette, size_t *out_length);
#endif
