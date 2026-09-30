#ifndef QA_FRONTEND_EVENT_RESTORE_H
#define QA_FRONTEND_EVENT_RESTORE_H
#include "internal.h"
/* Empty real event heaps claim their exact saved private VFS owners before
 * global image/audio import. The whole metadata stream is qualified first;
 * partial claims remain attached for ordinary failed-candidate retirement. */
bool frontend_event_topology_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
bool frontend_event_prepare_restored(qa_frontend *, qa_bytes, qa_error *);
/* After actual providers have bound, qualify their real event identities. */
bool frontend_event_topology_ready(const qa_frontend *, qa_error *);
#endif
