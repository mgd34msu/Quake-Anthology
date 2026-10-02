#ifndef QA_FRONTEND_CAPTURE_H
#define QA_FRONTEND_CAPTURE_H
#include "internal.h"
#include "qa/application_client_prepare.h"
#include "video_guests.h"
#include "qa/q3_cinematic_handles.h"

typedef struct frontend_resource_inventory frontend_resource_inventory;
/* Owns metadata and the frontend's structural roster fence, not child capture
 * tokens. Collect before preparing any bank/font/material/model destination.
 * A nonnull candidate must be the actual application's resource-phase ticket;
 * NULL names an ordinary scalar edit or the actual source-free ENGINE resource
 * phase, whose canonical console and cvar heap remain retained. The caller keeps frontend/application and
 * every observed owner alive through checked release, including retired roots.
 * This grants no source dispatch, capture, publication or destruction authority. */
bool frontend_resource_inventory_collect(qa_frontend *, qa_application *, const qa_launch_snapshot *,
    frontend_resource_inventory **, qa_error *);
bool frontend_resource_inventory_collect_client(qa_frontend *,const qa_application_client_preparation *,
    frontend_resource_inventory **,qa_error *);
bool frontend_resource_inventory_collect_video(qa_frontend *,const frontend_video_guests *,
    frontend_resource_inventory **,qa_error *);
/* Requalifies the installed parent tuple and the complete physical roster.
 * Actual child preparation/capture holds do not invalidate this observation. */
bool frontend_resource_inventory_current(const frontend_resource_inventory *);
/* Bind the exact shared global CIN child while its real bank policy is held.
 * Clear only after checked child finish/abort has returned the pool. */
bool frontend_resource_inventory_cinematics(frontend_resource_inventory *,
    qa_q3_cinematic_handles_stage *,qa_error *);
/* Seal while the full resource-phase admission is still available. Final
 * readiness replays only retained physical observations and exact callback-free
 * phase association; it allocates nothing and grants no publication authority. */
bool frontend_resource_inventory_seal(frontend_resource_inventory *, qa_error *);
bool frontend_resource_inventory_ready_is(const frontend_resource_inventory *);
/* Only the genuine synchronous resource consume callback admits the installed
 * candidate and one configuration generation advance; source-free ENGINE
 * consumption preserves its launch and generation. The retained physical
 * roster, world, frame and seat identities still require the same pure proof. */
bool frontend_resource_inventory_consume_ready_is(const frontend_resource_inventory *);
/* End all child preparation/capture holds first. A rejected release retains
 * metadata and the enclosing roster fence for checked cleanup retry. */
bool frontend_resource_inventory_release(frontend_resource_inventory **, qa_error *);
const qa_scene_resources *frontend_resource_inventory_images_at(const frontend_resource_inventory *, size_t);
const qa_material_library *frontend_resource_inventory_library_at(const frontend_resource_inventory *, size_t);
const qa_font_library *frontend_resource_inventory_fonts_at(const frontend_resource_inventory *, size_t);
const qa_material_order *frontend_resource_inventory_order_at(const frontend_resource_inventory *, size_t);
qa_q3_presentation_assets *frontend_resource_inventory_assets_at(const frontend_resource_inventory *, size_t);
const qa_scene_world *frontend_resource_inventory_world_at(const frontend_resource_inventory *, size_t);
const qa_scene_model *frontend_resource_inventory_model_at(const frontend_resource_inventory *, size_t);

/* Pure admission for the source, scene and immutable-content child owners.
 * The persistent local UI can remain in its map-selection action during travel. */
bool frontend_owners_idle(const qa_frontend *);
/* ENGINE shutdown separately qualifies its installed native settings ticket
 * and every physical release scope. This checks every other actual owner;
 * it supplies no ticket, release-history or detach authority by itself. */
bool frontend_owners_returned(const qa_frontend *);
/* The whole frontend cannot be captured, stepped or destroyed until its real
 * seat callbacks have returned, including HUD codecs and wheel callbacks. */
bool frontend_seat_callbacks_idle(const qa_frontend *);
/* A retained input release can advance after callbacks return. Capture and
 * retirement use the stricter idle predicate above, which also fences every
 * actual physical seat's source release history. */
bool frontend_seat_callbacks_returned(const qa_frontend *);
bool frontend_seat_callbacks_checkpoint_ready(const qa_frontend *,qa_error *);
bool frontend_sources_idle(const qa_frontend *);
/* The application opens its own persistence/content lease while this actual
 * frontend lease is held. Collection opens unique Q3 registries before their
 * child root tokens, and closes every token in reverse order on all paths. */
bool frontend_capture_begin(qa_frontend *, frontend_capture **, qa_error *);
void frontend_capture_end(frontend_capture *);
const qa_scene_resources *frontend_capture_images_at(const frontend_capture *, size_t);
const qa_material_library *frontend_capture_library_at(const frontend_capture *, size_t);
const qa_font_library *frontend_capture_fonts_at(const frontend_capture *, size_t);
qa_q3_presentation_assets *frontend_capture_assets_at(const frontend_capture *, size_t);
const qa_scene_world *frontend_capture_world_at(const frontend_capture *, size_t);
const qa_scene_model *frontend_capture_model_at(const frontend_capture *, size_t);
#endif
