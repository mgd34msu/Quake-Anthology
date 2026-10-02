#ifndef QA_FRONTEND_ROOT_RESOURCES_H
#define QA_FRONTEND_ROOT_RESOURCES_H
#include "internal.h"
#include "material_movies_save.h"
typedef struct frontend_root_resources frontend_root_resources;
bool frontend_root_resources_sync(qa_frontend *,qa_error *);
bool frontend_root_resources_destroy(qa_frontend *,qa_error *);
bool frontend_root_resources_prepare_restored(qa_frontend *,qa_error *);
bool frontend_root_sidecars_bind_restored(qa_frontend *,qa_error *);
bool frontend_root_movie_source_read(qa_frontend *,frontend_material_movie_source *,qa_error *);
bool frontend_root_movies_restore(qa_frontend *,const frontend_material_movies_refs *,qa_bytes,qa_error *);
#endif
