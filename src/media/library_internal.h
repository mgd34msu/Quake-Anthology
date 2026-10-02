#ifndef QA_MEDIA_LIBRARY_INTERNAL_H
#define QA_MEDIA_LIBRARY_INTERNAL_H
#include "cinematic_internal.h"

struct qa_cinematic_asset {
    size_t references;
    qa_cinematic_source source;
    qa_resource *source_record;
    qa_sha256_digest digest;
    uint32_t width, height;
    char *name;
    struct qa_cinematic_asset *next;
};
struct qa_media_library {
    qa_scene_resources *resources;
    qa_cinematic_asset *assets;
    struct qa_media_library_stage *pending;
    const struct qa_media_library *parent;
    bool stage_sealed;
};
bool qa_media_asset_format(const char *, qa_cinematic_format *, qa_error *);
bool qa_media_asset_load(qa_media_library *, qa_resource *, qa_cinematic_asset *, qa_error *);
#endif
