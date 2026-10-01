#ifndef QA_FRONTEND_REMOTE_Q3_CLIENT_H
#define QA_FRONTEND_REMOTE_Q3_CLIENT_H

#include "internal.h"
#include "network_presentation.h"
#include "client_registry.h"
#include "qa/application_native_q3_remote_client.h"

typedef struct frontend_remote_q3 frontend_remote_q3;
/* Constructor resources are distinct from completed CGAME media. These
 * owners exist before the actual service and lower media Init are entered. */
typedef struct frontend_remote_q3_resources {
    const frontend_remote_q3 *owner;
    frontend_network_client_domain domain;
    uint64_t identity;
    uint32_t physical_seat;
    const qa_launch_instance *descriptor;
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_media_library *movies;
    qa_q3_presentation_assets *assets;
    qa_scene_world *world;
    qa_collision_geometry *geometry;
    const qa_resource *map;
    frontend_client_registry *registry;
    qa_input_seat *input;
} frontend_remote_q3_resources;

/* An admitted output immediately owns every subsequent allocation. Failed
 * construction retains *out for checked cleanup; it never reports CG_Init. */
bool frontend_remote_q3_resources_create(qa_frontend *,const frontend_network_client_domain *,
    frontend_remote_q3 **,qa_error *);
bool frontend_remote_q3_resources_read(const frontend_remote_q3 *,frontend_remote_q3_resources *,qa_error *);
bool frontend_remote_q3_resources_current(const frontend_remote_q3_resources *);
bool frontend_remote_q3_basis_read(const frontend_remote_q3 *,qa_native_q3_remote_client_basis *,qa_error *);
bool frontend_remote_q3_resources_borrow(frontend_remote_q3 *,frontend_remote_q3_resources *,qa_error *);
void frontend_remote_q3_resources_release(frontend_remote_q3 *);
bool frontend_remote_q3_idle(const qa_frontend *);
bool frontend_remote_q3_resources_destroy(frontend_remote_q3 **,qa_error *);
bool frontend_remote_q3_destroy(qa_frontend *,qa_error *);
size_t frontend_remote_q3_count(const qa_frontend *);
frontend_remote_q3 *frontend_remote_q3_at(const qa_frontend *,size_t);
/* Both callback scopes supply the actual physical CLIENT receiver tuple.
 * No source host, local world, or initialized-media inference is involved. */
bool frontend_remote_q3_geometry_read(const qa_frontend *,const qa_application_q3_client_context *,
    const qa_resource **,const qa_collision_geometry **,bool *,qa_error *);
bool frontend_remote_q3_content_visit(const qa_frontend *,const qa_application_content_visitor *,qa_error *);

#endif
