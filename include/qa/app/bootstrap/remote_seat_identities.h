/* Port of src/app/bootstrap/remote-seat-identities.ts over the landed
 * world/session types. AggregateError cleanup paths abort with the
 * aggregate message (landed session convention). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_SEAT_IDENTITIES_H
#define QA_APP_BOOTSTRAP_REMOTE_SEAT_IDENTITIES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/world/session/session.h"
typedef struct qa_app_remote_seat_identity { qa_session_client *client; qa_session_seat *seat; } qa_app_remote_seat_identity;
typedef struct qa_app_remote_seat_identities qa_app_remote_seat_identities;
qa_app_remote_seat_identities *qa_app_prepare_remote_seat_identities(qa_engine_session *session,
    const qa_app_remote_seat_identity *retained, size_t retained_count, double count);
void qa_app_remote_seat_identities_destroy(qa_app_remote_seat_identities *p); /* frees wrapper only; call discard first if unpublished */
bool qa_app_remote_seat_identities_published(const qa_app_remote_seat_identities *p);
const qa_app_remote_seat_identity *qa_app_remote_seat_identities_selected(const qa_app_remote_seat_identities *p, size_t *out_count);
const qa_app_remote_seat_identity *qa_app_remote_seat_identities_added(const qa_app_remote_seat_identities *p, size_t *out_count);
void qa_app_remote_seat_identities_validate(qa_app_remote_seat_identities *p);
void qa_app_remote_seat_identities_publish(qa_app_remote_seat_identities *p);
void qa_app_remote_seat_identities_discard(qa_app_remote_seat_identities *p);
#endif
