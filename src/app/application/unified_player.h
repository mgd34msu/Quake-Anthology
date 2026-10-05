#ifndef QA_APPLICATION_UNIFIED_PLAYER_H
#define QA_APPLICATION_UNIFIED_PLAYER_H

#include "network_unified.h"
#include "qa/application_qc_presentation.h"
#include "qa/unified_frame_player.h"

typedef struct application_unified_player_camera {
    qa_application_qc_message_source source;
    qa_actor_id recipient, view_entity;
    uint32_t source_slot, intermission;
    qa_vec3 angles;
    bool has_angles;
} application_unified_player_camera;

/* The actual recipient decoder supplies its full-generation camera and optional
 * received STAT3 ammo override. current proves that retained decoder receipt;
 * declared weapons and timers are read from the physical Source below.
 * Declared client presentation borrows the first actual configured QC output,
 * independently of the physical Source family. */
typedef struct application_unified_player_external {
    void *context;
    bool (*current)(void *, qa_application *, const application_unified_source *,
        qa_net_client_id, const qa_unified_session_player *);
    const application_unified_player_camera *camera;
    const qa_application_qc_client_presentation *declared_vitals, *declared_view;
    const qa_application_camera_view *declared_camera;
    int32_t qc_ammo;
    bool has_qc_ammo;
} application_unified_player_external;

/* Typed view/UI observation for the exact physical Source recipient.
 * Allocations belong to the completed frame owner. */
bool application_unified_player_values(qa_application *,
    const application_unified_source *, qa_net_client_id,
    const qa_unified_session_player *, const application_unified_player_external *,
    qa_unified_frame *, const qa_inventory_entry *, size_t, qa_error *);

typedef struct application_unified_player_selection {
    qa_actor_id actor;
    qa_item_id item, ammo;
    qa_actor_owner primary, arsenal, visible_source;
    uint64_t publication, map_revision, frame_revision, actors_revision;
} application_unified_player_selection;
/* Actual selected item/ammo for Source composition callbacks. This observes
 * the admitted physical player independently of any network transport seat.
 * current rereads the genuine selected owner, including active gear. */
bool application_unified_player_selection_read(qa_application *, qa_actor_id,
    application_unified_player_selection *, qa_error *);
bool application_unified_player_selection_current(qa_application *,
    const application_unified_player_selection *);

#endif
