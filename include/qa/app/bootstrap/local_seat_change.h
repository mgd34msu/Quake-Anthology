/* Port of src/app/bootstrap/local-seat-change.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_LOCAL_SEAT_CHANGE_H
#define QA_APP_BOOTSTRAP_LOCAL_SEAT_CHANGE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/world/session/session.h"
/* TS LocalSeatIdentity. */
typedef struct qa_app_local_seat_identity {
  qa_session_client *client; /* borrowed */
  qa_session_seat *seat;     /* borrowed */
} qa_app_local_seat_identity;
/* TS LocalSeatRequest union on kind: "join" | "drop"(seat). */
typedef struct qa_app_local_seat_request {
  const char *kind;
  qa_seat_id seat; /* drop */
} qa_app_local_seat_request;
typedef struct qa_app_local_seat_capacity { double local_seats, clients; } qa_app_local_seat_capacity;
typedef struct qa_app_prepared_local_seat_change qa_app_prepared_local_seat_change;
/* Aborts with the TS Error text on misuse. Caller owns the result
 * and frees it with qa_app_prepared_local_seat_change_destroy. */
qa_app_prepared_local_seat_change *qa_app_prepare_local_seat_change(qa_engine_session *session,
    const qa_app_local_seat_identity *current, size_t current_count,
    qa_app_local_seat_request request, qa_app_local_seat_capacity capacity);
void qa_app_prepared_local_seat_change_destroy(qa_app_prepared_local_seat_change *change);
/* Borrowed (NULL when none). */
const qa_app_local_seat_identity *qa_app_prepared_local_seat_change_added(const qa_app_prepared_local_seat_change *change);
const qa_app_local_seat_identity *qa_app_prepared_local_seat_change_removed(const qa_app_prepared_local_seat_change *change);
const qa_app_local_seat_identity *qa_app_prepared_local_seat_change_next(
    const qa_app_prepared_local_seat_change *change, size_t *out_count);
bool qa_app_prepared_local_seat_change_published(const qa_app_prepared_local_seat_change *change);
void qa_app_prepared_local_seat_change_validate(qa_app_prepared_local_seat_change *change,
    const qa_session_presentation_entry *presentations, size_t presentation_count);
qa_session_resource qa_app_prepared_local_seat_change_publish(qa_app_prepared_local_seat_change *change,
    const qa_session_presentation_entry *presentations, size_t presentation_count);
void qa_app_prepared_local_seat_change_discard(qa_app_prepared_local_seat_change *change);
#endif
