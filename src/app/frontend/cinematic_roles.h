#ifndef QA_FRONTEND_CINEMATIC_ROLES_H
#define QA_FRONTEND_CINEMATIC_ROLES_H
#include "material_movies.h"

typedef struct frontend_cinematic_roles frontend_cinematic_roles;
typedef struct frontend_cinematic_role_view {
    frontend_material_movies *parent;
    frontend_material_movie_source source;
    qa_q3_cinematic_source *cinematics;
    uint32_t seat;
    uint64_t bus;
} frontend_cinematic_role_view;

/* Transfer only a returned role's numeric playback custody. The retired
 * presentation's diagnostic context is removed before its lease can die. */
bool frontend_cinematic_roles_adopt(qa_frontend *, qa_q3_cinematic_source **,
    void *diagnostic_context, qa_error *);
size_t frontend_cinematic_roles_count(const qa_frontend *);
bool frontend_cinematic_roles_read(const qa_frontend *, size_t,
    frontend_cinematic_role_view *, qa_error *);
bool frontend_cinematic_roles_parent_destroy(qa_frontend *, frontend_material_movies *, qa_error *);
void frontend_cinematic_roles_parent_rebind(qa_frontend *, frontend_material_movies *);
bool frontend_cinematic_roles_destroy(qa_frontend *, qa_error *);
bool frontend_cinematic_roles_prune(qa_frontend *, qa_error *);

/* The aggregate decodes real physical movie rows before this namespace prefix.
 * It allocates candidate bus identities before audio-engine references import.
 * Parent playback sources bind later, after their own media/cache import. */
bool frontend_cinematic_roles_restore_add(qa_frontend *,
    const frontend_material_movie_source *, uint32_t seat, size_t index, qa_error *);
bool frontend_cinematic_roles_restore_bind(qa_frontend *, size_t,
    frontend_material_movies *, qa_error *);
#endif
