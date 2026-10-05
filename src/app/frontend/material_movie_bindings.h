#ifndef QA_FRONTEND_MATERIAL_MOVIE_BINDINGS_H
#define QA_FRONTEND_MATERIAL_MOVIE_BINDINGS_H
#include "material_movies.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
bool frontend_source_movie_source_read(qa_frontend *,size_t,frontend_material_movie_source *,qa_error *);
bool frontend_native_q3_movie_source_read(qa_frontend *,size_t,frontend_material_movie_source *,qa_error *);
bool frontend_remote_q3_movie_source_read(frontend_remote_q3 *,frontend_material_movie_source *,qa_error *);
bool frontend_remote_q3_initial_movie_source_read(frontend_remote_q3_initial *,frontend_material_movie_source *,qa_error *);
bool frontend_source_material_bindings_restore(qa_frontend *,qa_error *);
bool frontend_remote_q3_material_bindings_restore(qa_frontend *,qa_error *);
bool frontend_remote_q3_initial_material_bindings_restore(qa_frontend *,qa_error *);
#endif
