#ifndef QA_FRONTEND_REMOTE_Q3_CLIENT_H
#define QA_FRONTEND_REMOTE_Q3_CLIENT_H

#include "internal.h"
#include "network_presentation.h"
#include "client_registry.h"
#include "qa/application_native_q3_remote_client.h"

typedef struct frontend_remote_q3 frontend_remote_q3;
typedef struct frontend_remote_q3_modules frontend_remote_q3_modules;
typedef struct frontend_remote_q3_runtime frontend_remote_q3_runtime;
typedef struct frontend_remote_q3_transport frontend_remote_q3_transport;
typedef struct frontend_remote_q3_frame frontend_remote_q3_frame;
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
/* The claimed graph view transfers only after the partial parent is retained.
 * This creates detached empty heaps and immutable map collision, then imports
 * saved portals. The world dictionary supplies its decoded owned root later. */
bool frontend_remote_q3_resources_prepare_restored(qa_frontend *,const frontend_network_client_domain *,
    uint64_t identity,uint32_t physical_seat,qa_vfs **claimed_mounts,qa_bytes portals,
    frontend_remote_q3 **,qa_error *);
bool frontend_remote_q3_resources_import_read(const frontend_remote_q3 *,frontend_remote_q3_resources *,qa_error *);
bool frontend_remote_q3_resources_import_current(const frontend_remote_q3_resources *);
bool frontend_remote_q3_resources_world_adopt_ready(frontend_remote_q3 *,qa_scene_world *,qa_error *);
void frontend_remote_q3_resources_world_adopt(frontend_remote_q3 *,qa_scene_world *);
bool frontend_remote_q3_resources_finish_import(frontend_remote_q3 *,qa_error *);
bool frontend_remote_q3_resources_read(const frontend_remote_q3 *,frontend_remote_q3_resources *,qa_error *);
bool frontend_remote_q3_resources_current(const frontend_remote_q3_resources *);
/* Pure structural observations for the installed metadata inventory only;
 * prepared resource holds need no active connection or media callbacks. */
bool frontend_remote_q3_resources_metadata_read(const frontend_remote_q3 *,frontend_remote_q3_resources *,qa_error *);
bool frontend_remote_q3_resources_metadata_current(const frontend_remote_q3_resources *);
bool frontend_remote_q3_basis_read(const frontend_remote_q3 *,qa_native_q3_remote_client_basis *,qa_error *);
/* The exact compiled video ticket permits structural Source observations
 * while its graphics banks are closed or partly reconstructed. */
bool frontend_remote_q3_resources_compiled_video_read(const frontend_remote_q3 *,frontend_remote_q3_resources *,qa_error *);
bool frontend_remote_q3_resources_compiled_video_refresh(frontend_remote_q3 *,qa_error *);
bool frontend_remote_q3_resources_borrow(frontend_remote_q3 *,frontend_remote_q3_resources *,qa_error *);
void frontend_remote_q3_resources_release(frontend_remote_q3 *);
/* Structural acquired-host children remain attached while callbacks return.
 * They retire before the compiled service and constructor media parents. */
bool frontend_remote_q3_modules_attach(frontend_remote_q3 *,frontend_remote_q3_modules *,qa_error *);
frontend_remote_q3_modules *frontend_remote_q3_modules_read(const frontend_remote_q3 *);
bool frontend_remote_q3_modules_detach(frontend_remote_q3 *,frontend_remote_q3_modules *,qa_error *);
bool frontend_remote_q3_transport_attach(frontend_remote_q3 *,frontend_remote_q3_transport *,qa_error *);
frontend_remote_q3_transport *frontend_remote_q3_transport_read(const frontend_remote_q3 *);
bool frontend_remote_q3_transport_detach(frontend_remote_q3 *,frontend_remote_q3_transport *,qa_error *);
qa_frontend *frontend_remote_q3_frontend(const frontend_remote_q3 *);
frontend_remote_q3_frame *frontend_remote_q3_frames_read(const frontend_remote_q3 *);
bool frontend_remote_q3_runtime_attach(frontend_remote_q3 *,frontend_remote_q3_runtime *,qa_error *);
frontend_remote_q3_runtime *frontend_remote_q3_runtime_read(const frontend_remote_q3 *);
bool frontend_remote_q3_runtime_detach(frontend_remote_q3 *,frontend_remote_q3_runtime *,qa_error *);
bool frontend_remote_q3_idle(const qa_frontend *);
bool frontend_remote_q3_capture_current(const qa_frontend *,const frontend_capture *);
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
