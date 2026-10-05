#ifndef QA_FRONTEND_REMOTE_UNIFIED_MATERIAL_MOVIES_BRIDGE_H
#define QA_FRONTEND_REMOTE_UNIFIED_MATERIAL_MOVIES_BRIDGE_H
#include "remote_unified_media.h"
#include "material_movies.h"

bool frontend_unified_material_movies_create(frontend_unified_media *, size_t bank, qa_error *);
bool frontend_unified_material_cinematic_namespace_read(const frontend_unified_media *, size_t bank,
    uint32_t *seat, uint64_t *bus, bool *present, qa_error *);
bool frontend_unified_material_movies_prepare_restored(frontend_unified_media *, size_t bank, qa_error *);
bool frontend_unified_material_movies_clear(frontend_unified_media *, size_t bank, qa_error *);
bool frontend_unified_material_movies_idle(const frontend_unified_media *);
bool frontend_unified_material_movies_frame(frontend_unified_media *, qa_scene_frame *, qa_error *);
size_t frontend_unified_material_movie_count(const frontend_unified_media *);
bool frontend_unified_material_movie_at(const frontend_unified_media *, size_t filtered_ordinal,
    size_t *bank_ordinal);
bool frontend_unified_material_movie_source_read(frontend_unified_media *, size_t bank,
    frontend_material_movie_source *, qa_error *);
bool frontend_unified_material_movies_restore_ready(frontend_unified_media *, qa_error *);
#endif
