#ifndef QA_MATERIAL_IMAGE_H
#define QA_MATERIAL_IMAGE_H
#include "cinematic_internal.h"

bool cinematic_material_initial(qa_cinematic *, qa_scene_resources *, qa_scene_frame *,
    const qa_scene_image **, qa_error *);

static inline bool cinematic_initial_dimensions(const qa_cinematic *movie,
    uint32_t *width, uint32_t *height, size_t *bytes, qa_error *error)
{
    *width = *height = 1;
    if (movie->options.target.kind == QA_CINEMATIC_MATERIAL) {
        if (!movie->asset) return cinematic_fail(error, "Shader movie has no retained asset dimensions");
        qa_cinematic_asset_dimensions(movie->asset, width, height);
    }
    if (!*width || !*height || (size_t)*height > SIZE_MAX / 4 / *width)
        return cinematic_fail(error, "Initial cinematic image dimensions exceed their allocation");
    *bytes = (size_t)*width * *height * 4;
    return true;
}
#endif
