#ifndef QA_SCENE_IMAGE_OPTIONS_SAVE_H
#define QA_SCENE_IMAGE_OPTIONS_SAVE_H
#include "qa/scene.h"
#include "qa/source_save.h"

static inline bool qa_scene_source_upload_precision_fields(qa_source_save_io *io, qa_scene_image_options *image)
{
    if (!qa_source_save_bool(io, &image->source_q3)) return false;
    return !image->source_q3 || (qa_q3_image_upload_options_precision_codec(io, &image->source_upload) &&
        image->family == QA_SCENE_Q3 && image->mipmap == image->source_upload.mipmap);
}
#endif
