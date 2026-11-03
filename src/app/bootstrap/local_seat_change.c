/* Port of src/app/bootstrap/local-seat-change.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/local_seat_change.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
struct qa_app_prepared_local_seat_change {
  qa_engine_session *session;
  qa_world_lifetime *world;
  bool has_added; qa_app_local_seat_identity added;
  bool has_removed; qa_app_local_seat_identity removed;
  qa_app_local_seat_identity *next; size_t next_count;
  const char *phase; /* "prepared"|"published"|"discarded" */
  qa_session_client *added_client;
  qa_session_client *removed_client;
  qa_session_seat *added_seat;
  qa_session_seat *removed_seat;
};
static int compare_identity(const void *a, const void *b) {
  int64_t left = qa_session_seat_id(((const qa_app_local_seat_identity *)a)->seat).index;
  int64_t right = qa_session_seat_id(((const qa_app_local_seat_identity *)b)->seat).index;
  return left < right ? -1 : left > right ? 1 : 0;
}
qa_app_prepared_local_seat_change *qa_app_prepare_local_seat_change(qa_engine_session *session,
    const qa_app_local_seat_identity *current, size_t current_count,
    qa_app_local_seat_request request, qa_app_local_seat_capacity capacity) {
  qa_world_lifetime *world = qa_engine_session_world(session);
  if (world == NULL || qa_world_lifetime_is_closed(world))
    qa_range_error("Local player changes require an active world");
  qa_app_prepared_local_seat_change *change = calloc(1, sizeof *change);
  if (change == NULL) qa_range_error("Out of memory");
  change->session = session; change->world = world; change->phase = "prepared";
  if (strcmp(request.kind, "join") == 0) {
    int64_t index = 0;
    for (;;) {
      bool taken = false;
      for (size_t i = 0; i < current_count; i++)
        if (qa_session_seat_id(current[i].seat).index == index) taken = true;
      if (!taken) break;
      index++;
    }
    if ((double)index >= capacity.local_seats) { free(change); qa_range_error("All local player slots are occupied"); }
    int64_t slot = 0;
    while ((double)slot < capacity.clients && qa_engine_session_client_at(session, slot) != NULL) slot++;
    if ((double)slot >= capacity.clients) { free(change); qa_range_error("The server has no free player slots"); }
    qa_session_client *client = qa_engine_session_prepare_client(session, slot, NULL);
    /* TS: prepareSeat failure closes the client and rethrows. The
     * landed prepare_seat aborts on misuse instead of throwing, so
     * the AggregateError path cannot be reached here. */
    qa_session_seat *seat = qa_engine_session_prepare_seat(session, index, client, NULL);
    change->has_added = true;
    change->added.client = client; change->added.seat = seat;
    change->added_client = client; change->added_seat = seat;
  } else {
    bool found = false;
    for (size_t i = 0; i < current_count; i++) {
      if (qa_same_seat(qa_session_seat_id(current[i].seat), request.seat)) {
        change->removed = current[i]; found = true;
      }
    }
    if (!found) { free(change); qa_range_error("Local player is no longer active"); }
    change->has_removed = true;
    change->removed_client = change->removed.client;
    change->removed_seat = change->removed.seat;
  }
  size_t count = 0;
  for (size_t i = 0; i < current_count; i++)
    if (!(change->has_removed && current[i].seat == change->removed.seat)) count++;
  if (change->has_added) count++;
  change->next = calloc(count == 0 ? 1 : count, sizeof *change->next);
  if (change->next == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (size_t i = 0; i < current_count; i++)
    if (!(change->has_removed && current[i].seat == change->removed.seat)) change->next[at++] = current[i];
  if (change->has_added) change->next[at++] = change->added;
  change->next_count = at;
  qa_js_sort(change->next, change->next_count, sizeof *change->next, compare_identity);
  return change;
}
void qa_app_prepared_local_seat_change_destroy(qa_app_prepared_local_seat_change *change) {
  if (change == NULL) return;
  free(change->next); free(change);
}
const qa_app_local_seat_identity *qa_app_prepared_local_seat_change_added(const qa_app_prepared_local_seat_change *change) {
  return change->has_added ? &change->added : NULL;
}
const qa_app_local_seat_identity *qa_app_prepared_local_seat_change_removed(const qa_app_prepared_local_seat_change *change) {
  return change->has_removed ? &change->removed : NULL;
}
const qa_app_local_seat_identity *qa_app_prepared_local_seat_change_next(
    const qa_app_prepared_local_seat_change *change, size_t *out_count) {
  *out_count = change->next_count;
  return change->next;
}
bool qa_app_prepared_local_seat_change_published(const qa_app_prepared_local_seat_change *change) {
  return strcmp(change->phase, "published") == 0;
}
static qa_session_client_change clients_of(qa_app_prepared_local_seat_change *change) {
  qa_session_client_change clients = {0};
  clients.added = change->has_added ? &change->added_client : NULL;
  clients.added_count = change->has_added ? 1 : 0;
  clients.removed = change->has_removed ? &change->removed_client : NULL;
  clients.removed_count = change->has_removed ? 1 : 0;
  return clients;
}
static qa_session_seat_change seats_of(qa_app_prepared_local_seat_change *change) {
  qa_session_seat_change seats = {0};
  seats.added = change->has_added ? &change->added_seat : NULL;
  seats.added_count = change->has_added ? 1 : 0;
  seats.removed = change->has_removed ? &change->removed_seat : NULL;
  seats.removed_count = change->has_removed ? 1 : 0;
  return seats;
}
void qa_app_prepared_local_seat_change_validate(qa_app_prepared_local_seat_change *change,
    const qa_session_presentation_entry *presentations, size_t presentation_count) {
  if (strcmp(change->phase, "prepared") != 0)
    qa_range_error("Local seat change is %s", change->phase);
  if (qa_engine_session_world(change->session) != change->world || qa_world_lifetime_is_closed(change->world))
    qa_range_error("Local player world changed during preparation");
  qa_engine_session_validate_local_seats(change->session, presentations, presentation_count,
      clients_of(change), seats_of(change));
}
qa_session_resource qa_app_prepared_local_seat_change_publish(qa_app_prepared_local_seat_change *change,
    const qa_session_presentation_entry *presentations, size_t presentation_count) {
  qa_app_prepared_local_seat_change_validate(change, presentations, presentation_count);
  qa_session_resource retired = qa_engine_session_publish_local_seats(change->session,
      presentations, presentation_count, clients_of(change), seats_of(change));
  change->phase = "published";
  return retired;
}
void qa_app_prepared_local_seat_change_discard(qa_app_prepared_local_seat_change *change) {
  if (strcmp(change->phase, "prepared") != 0) return;
  change->phase = "discarded";
  if (change->has_added) qa_session_client_close(change->added.client);
}
