#ifndef QA_FRONTEND_REMOTE_Q3_INITIAL_H
#define QA_FRONTEND_REMOTE_Q3_INITIAL_H

#include "remote_q3_client.h"

typedef struct frontend_remote_q3_initial frontend_remote_q3_initial;
/* Connecting UI media has no decoded map, gamestate or CGAME Init counters. */
typedef struct frontend_remote_q3_initial_view {
    const frontend_remote_q3_initial *owner;
    frontend_network_client_attempt attempt;
    uint64_t identity;
    uint32_t physical_seat;
    qa_q3_product product;
    const qa_launch_instance *descriptor;
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_media_library *movies;
    qa_q3_presentation_assets *assets;
    frontend_client_registry *registry;
    qa_input_seat *input;
} frontend_remote_q3_initial_view;

/* The caller retains out before the first media acquisition. Failed creation
 * leaves that exact owner reachable for checked disposal. */
bool frontend_remote_q3_initial_create(qa_frontend *,const frontend_network_client_attempt *,
    frontend_remote_q3_initial **,qa_error *);
bool frontend_remote_q3_initial_read(const frontend_remote_q3_initial *,
    frontend_remote_q3_initial_view *,qa_error *);
bool frontend_remote_q3_initial_current(const frontend_remote_q3_initial_view *);
/* A host media child retains a structural reference without holding an
 * entered-callback borrow. Parent disposal waits for every such reference. */
bool frontend_remote_q3_initial_child_retain(frontend_remote_q3_initial *,
    frontend_remote_q3_initial **,qa_error *);
bool frontend_remote_q3_initial_child_release(frontend_remote_q3_initial **,qa_error *);
bool frontend_remote_q3_initial_borrow(frontend_remote_q3_initial *,
    frontend_remote_q3_initial_view *,qa_error *);
void frontend_remote_q3_initial_release(frontend_remote_q3_initial *);
bool frontend_remote_q3_initial_idle(const frontend_remote_q3_initial *);
bool frontend_remote_q3_initial_destroy(frontend_remote_q3_initial **,qa_error *);

#endif
