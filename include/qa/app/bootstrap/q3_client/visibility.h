/* Port of src/app/bootstrap/q3-client/visibility.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_VISIBILITY_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_VISIBILITY_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/network/q3/state/entity.h"
#include "qa/network/q3/state/player.h"
#include "qa/network/q3/visibility.h"
#include "qa/world/collision/index.h"
/* One TS source.entities row. */
typedef struct qa_app_q3_snapshot_source_entity {
  const qa_q3_entity_state_record *state;
  bool linked;
  double server_flags;
  double single_client;
} qa_app_q3_snapshot_source_entity;
/* Owned backing for the selection (copied records, links, and the
 * fallback records the TS entity() closure allocates on demand). */
typedef struct qa_app_q3_snapshot_selection qa_app_q3_snapshot_selection;
/* TS selectApplicationQ3Snapshot. bounds: false is the TS null
 * (aborts with the TS Error for linked rows). The returned
 * selection's visible state pointers borrow its records; free
 * with qa_app_q3_snapshot_selection_destroy. */
qa_app_q3_snapshot_selection *qa_app_select_application_q3_snapshot(
    const qa_q3_player_state_record *player,
    const qa_app_q3_snapshot_source_entity *entities, size_t entity_row_count,
    qa_shared_scene_queries *queries,
    bool (*bounds)(void *ctx, double number, qa_bounds *out), void *bounds_ctx,
    double leaf_count, void (*print)(void *ctx, const char *text), void *print_ctx);
const qa_q3_visible_entities *qa_app_q3_snapshot_selection_visible(const qa_app_q3_snapshot_selection *selection);
void qa_app_q3_snapshot_selection_destroy(qa_app_q3_snapshot_selection *selection);
#endif
