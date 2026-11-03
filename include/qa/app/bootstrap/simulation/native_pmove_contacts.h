/* Port of src/app/bootstrap/simulation/native-pmove-contacts.ts: the Pmove
 * touch list and ground plane a selected foreign movement provider reports
 * back into a native Q2 guest pmove_t. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_PMOVE_CONTACTS_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_PMOVE_CONTACTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

/* TS NativeSliceContact: one contact the selected movement provider reported
 * during a native Pmove slice. has_actor false is TS null (static world
 * geometry); has_plane false is TS null. */
typedef struct qa_app_sim_native_slice_contact { bool has_actor; qa_actor_id actor; bool has_plane; qa_plane plane; } qa_app_sim_native_slice_contact;
/* TS NativePmoveTouch: a pmove_t touch entry, the guest edict slot and the plane it was hit on. */
typedef struct qa_app_sim_native_pmove_touch { double slot; qa_plane plane; } qa_app_sim_native_pmove_touch;
/* One overlapped solid of pmoveTouchList's second argument. */
typedef struct qa_app_sim_native_overlap { qa_actor_id actor; qa_plane plane; } qa_app_sim_native_overlap;

/* TS WALKABLE_NORMAL_Z: Quake's walkable-slope threshold. */
#define QA_APP_SIM_WALKABLE_NORMAL_Z 0.7

/* TS reportedGroundPlane: the most recent reported contact a player could
 * stand on; false is TS null. */
bool qa_app_sim_reported_ground_plane(const qa_app_sim_native_slice_contact *contacts, size_t count, qa_plane *out);
/* TS overlapPlane(origin, bounds, other): the plane of the face of `other`
 * the player is pressed against, from the axis of least penetration. */
qa_plane qa_app_sim_overlap_plane(qa_vec3 origin, qa_bounds bounds, qa_bounds other);
/* TS slotOf: false is TS undefined. */
typedef bool (*qa_app_sim_native_slot_of_fn)(void *ctx, qa_actor_id actor, double *out_slot);
/* TS pmoveTouchList(reported, overlapped, slotOf): malloc'd (free()), count
 * in *out_count; NULL only when the list is empty. */
qa_app_sim_native_pmove_touch *qa_app_sim_pmove_touch_list(const qa_app_sim_native_slice_contact *reported, size_t reported_count,
    const qa_app_sim_native_overlap *overlapped, size_t overlapped_count, qa_app_sim_native_slot_of_fn slot_of, void *ctx,
    size_t *out_count);
#endif
