#ifndef QA_FRONTEND_EVENT_RESTORE_H
#define QA_FRONTEND_EVENT_RESTORE_H
#include "internal.h"
#include "scene_identity.h"
/* Empty real event heaps claim their exact saved private VFS owners before
 * global image/audio import. The whole metadata stream is qualified first;
 * partial claims remain attached for ordinary failed-candidate retirement. */
bool frontend_event_topology_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
bool frontend_event_prepare_restored(qa_frontend *, qa_bytes, qa_error *);
/* After actual providers have bound, qualify their real event identities. */
bool frontend_event_topology_ready(const qa_frontend *, qa_error *);
/* Uses the genuine fixed Q1 light cells, linked Q2 lights, and static-sound
 * traversal. These producer rows precede saved frame and engine references. */
bool frontend_event_capture_namespace(qa_frontend *, frontend_scene_identity_scope *, qa_error *);
#endif
