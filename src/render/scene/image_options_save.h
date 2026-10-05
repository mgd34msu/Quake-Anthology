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
static inline bool qa_scene_image_options_fields(qa_source_save_io *io, qa_scene_image_options *options,
    uint8_t palette_storage[768], uint8_t translation_storage[256], bool *exact_file)
{
    uint32_t family = options->family, wrap = options->wrap, filter = options->filter, usage = options->usage;
    int32_t transparent_index = options->transparent_index;
    size_t palette = options->palette_rgb.size, translation = options->translation.size;
    bool ok = qa_source_save_u32(io, &family) && family <= QA_SCENE_Q3 &&
        qa_source_save_u32(io, &wrap) && wrap <= QA_SCENE_CLAMP &&
        qa_source_save_u32(io, &filter) && filter <= QA_SCENE_LINEAR_MIPMAP_LINEAR &&
        qa_source_save_u32(io, &usage) && usage <= QA_IMAGE_USAGE_SKY &&
        qa_source_save_bool(io, &options->mipmap) && qa_source_save_bool(io, &options->transparent) &&
        qa_source_save_bool(io, &options->fullbright_only) && qa_source_save_i32(io, &transparent_index) &&
        qa_source_save_count(io, &palette, 768) && (!palette || palette == 768) &&
        qa_source_save_count(io, &translation, 256) && (!translation || translation == 256) &&
        qa_source_save_bytes(io, palette_storage, palette) && qa_source_save_bytes(io, translation_storage, translation) &&
        qa_source_save_bool(io, exact_file);
    if (ok) {
        options->family = (qa_scene_family)family; options->wrap = (qa_scene_wrap)wrap;
        options->filter = (qa_scene_filter)filter; options->usage = (qa_scene_image_usage)usage;
        options->transparent_index = transparent_index;
        options->palette_rgb = (qa_bytes){palette_storage, palette};
        options->translation = (qa_bytes){translation_storage, translation};
    }
    return ok && qa_scene_source_upload_precision_fields(io, options);
}
#endif
