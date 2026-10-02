#ifndef QA_FRONTEND_REMOTE_Q3_TRANSPORT_H
#define QA_FRONTEND_REMOTE_Q3_TRANSPORT_H
#include "remote_q3_client.h"

/* Pure acquired CGAME has a physical transport without a compiled service.
 * The decoded resource parent retains this child through checked teardown. */
bool frontend_remote_q3_transport_create(frontend_remote_q3 *,frontend_remote_q3_transport **,qa_error *);
bool frontend_remote_q3_transport_create_restored(frontend_remote_q3 *,frontend_remote_q3_transport **,qa_error *);
frontend_remote_q3 *frontend_remote_q3_transport_parent(const frontend_remote_q3_transport *);
bool frontend_remote_q3_transport_current(const frontend_remote_q3_transport *);
bool frontend_remote_q3_transport_idle(const frontend_remote_q3_transport *);
bool frontend_remote_q3_transport_retired(const frontend_remote_q3_transport *);
bool frontend_remote_q3_transport_destroy(frontend_remote_q3_transport **,qa_error *);
#endif
