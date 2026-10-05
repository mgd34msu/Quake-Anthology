#ifndef QA_FRONTEND_REMOTE_Q2_MATERIAL_MOVIES_BRIDGE_H
#define QA_FRONTEND_REMOTE_Q2_MATERIAL_MOVIES_BRIDGE_H
#include "remote_q2_client.h"
#include "material_movies.h"

/* The actual receiver installs this producer before its first world/model
 * material registration. Its retained CLIENT content owns the media cache. */
bool remote_q2_material_movies_create(frontend_remote_q2 *, qa_error *);
bool remote_q2_material_movies_prepare_restored(frontend_remote_q2 *, qa_error *);
bool remote_q2_material_movies_clear(frontend_remote_q2 *, qa_error *);
bool remote_q2_material_movies_idle(const frontend_remote_q2 *);

/* Empty pending receivers have no media provider. The ordinal follows only
 * actual retained cache holders, including the isolated import holders. */
size_t frontend_remote_q2_material_movie_count(const qa_frontend *);
frontend_remote_q2 *frontend_remote_q2_material_movie_at(const qa_frontend *, size_t);
bool frontend_remote_q2_movie_source_read(frontend_remote_q2 *,
    frontend_material_movie_source *, qa_error *);
#endif
