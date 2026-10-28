/* Port of src/app/bootstrap/remote-seat-identities.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_seat_identities.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef enum phase { PREPARED, PUBLISHED, DISCARDED } phase;
struct qa_app_remote_seat_identities {
  qa_engine_session *session;
  qa_app_remote_seat_identity *selected; size_t selected_count;
  qa_app_remote_seat_identity *added; size_t added_count;
  phase state;
};
static const char *phase_name(phase p) { return p == PUBLISHED ? "published" : p == DISCARDED ? "discarded" : "prepared"; }
static void discard_added(qa_app_remote_seat_identity *added, size_t count) {
  for (size_t i = count; i-- > 0;) qa_session_client_close(added[i].client);
}
qa_app_remote_seat_identities *qa_app_prepare_remote_seat_identities(qa_engine_session *session,
    const qa_app_remote_seat_identity *retained, size_t retained_count, double count_d) {
  if (count_d != trunc(count_d) || count_d < 1 || count_d > 4)
    qa_range_error("Remote play requires one to four local players");
  size_t count = (size_t)count_d;
  qa_app_remote_seat_identities *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->session = session;
  p->selected = calloc(count, sizeof *p->selected);
  p->added = calloc(count, sizeof *p->added);
  if (!p->selected || !p->added) { free(p->selected); free(p->added); free(p); return NULL; }
  size_t keep = retained_count < count ? retained_count : count;
  memcpy(p->selected, retained, keep * sizeof *p->selected);
  p->selected_count = keep;
  while (p->selected_count < count) {
    int64_t slot = 0;
    for (;;) {
      bool used = qa_engine_session_client_at(session, slot) != NULL;
      for (size_t i = 0; i < p->added_count; i++)
        if (qa_session_client_id(p->added[i].client).slot == slot) used = true;
      if (!used) break;
      slot++;
    }
    int64_t index = 0;
    for (;;) {
      bool used = false;
      for (size_t i = 0; i < retained_count; i++)
        if (qa_session_seat_id(retained[i].seat).index == index) used = true;
      for (size_t i = 0; i < p->added_count; i++)
        if (qa_session_seat_id(p->added[i].seat).index == index) used = true;
      if (!used) break;
      index++;
    }
    qa_session_client *client = qa_engine_session_prepare_client(session, slot, NULL);
    qa_session_seat *seat = qa_engine_session_prepare_seat(session, index, client, NULL);
    qa_app_remote_seat_identity entry = {client, seat};
    p->added[p->added_count++] = entry;
    p->selected[p->selected_count++] = entry;
  }
  p->state = PREPARED;
  return p;
}
void qa_app_remote_seat_identities_destroy(qa_app_remote_seat_identities *p) {
  if (!p) return;
  free(p->selected); free(p->added); free(p);
}
bool qa_app_remote_seat_identities_published(const qa_app_remote_seat_identities *p) { return p->state == PUBLISHED; }
const qa_app_remote_seat_identity *qa_app_remote_seat_identities_selected(const qa_app_remote_seat_identities *p, size_t *out_count) {
  *out_count = p->selected_count; return p->selected;
}
const qa_app_remote_seat_identity *qa_app_remote_seat_identities_added(const qa_app_remote_seat_identities *p, size_t *out_count) {
  *out_count = p->added_count; return p->added;
}
static void changes(qa_app_remote_seat_identities *p, qa_session_client_change *clients, qa_session_seat_change *seats,
                    qa_session_client ***cbuf, qa_session_seat ***sbuf) {
  *cbuf = calloc(p->added_count ? p->added_count : 1, sizeof **cbuf);
  *sbuf = calloc(p->added_count ? p->added_count : 1, sizeof **sbuf);
  if (!*cbuf || !*sbuf) qa_range_error("Out of memory");
  for (size_t i = 0; i < p->added_count; i++) { (*cbuf)[i] = p->added[i].client; (*sbuf)[i] = p->added[i].seat; }
  clients->added = *cbuf; clients->added_count = p->added_count; clients->removed = NULL; clients->removed_count = 0;
  seats->added = *sbuf; seats->added_count = p->added_count; seats->removed = NULL; seats->removed_count = 0;
}
void qa_app_remote_seat_identities_validate(qa_app_remote_seat_identities *p) {
  if (p->state != PREPARED) qa_range_error("Remote seat identities are %s", phase_name(p->state));
  qa_session_client_change clients; qa_session_seat_change seats;
  qa_session_client **cbuf; qa_session_seat **sbuf;
  changes(p, &clients, &seats, &cbuf, &sbuf);
  qa_engine_session_validate_local_seats(p->session, NULL, 0, clients, seats);
  free(cbuf); free(sbuf);
}
void qa_app_remote_seat_identities_publish(qa_app_remote_seat_identities *p) {
  qa_app_remote_seat_identities_validate(p);
  qa_session_client_change clients; qa_session_seat_change seats;
  qa_session_client **cbuf; qa_session_seat **sbuf;
  changes(p, &clients, &seats, &cbuf, &sbuf);
  qa_session_resource retired = qa_engine_session_publish_local_seats(p->session, NULL, 0, clients, seats);
  free(cbuf); free(sbuf);
  if (retired.close) retired.close(retired.ctx);
  p->state = PUBLISHED;
}
void qa_app_remote_seat_identities_discard(qa_app_remote_seat_identities *p) {
  if (p->state != PREPARED) return;
  p->state = DISCARDED;
  discard_added(p->added, p->added_count);
}
