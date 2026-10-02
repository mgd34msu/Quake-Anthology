#ifndef QA_SCENE_IMAGE_OPTIONS_SAVE_H
#define QA_SCENE_IMAGE_OPTIONS_SAVE_H
#include "qa/scene.h"
#include "qa/source_save.h"

static inline bool qa_scene_source_upload_fields(qa_source_save_io *io, qa_scene_image_options *image)
{
    if (!qa_source_save_bool(io, &image->source_q3)) return false;
    if (!image->source_q3) return true;
    qa_q3_image_upload_options *upload = &image->source_upload;
    return qa_q3_image_upload_options_codec(io, upload) && image->family == QA_SCENE_Q3 &&
        image->mipmap == upload->mipmap;
}
#endif
