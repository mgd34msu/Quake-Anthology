#ifndef QA_FRONTEND_Q1_SKY_SAVE_H
#define QA_FRONTEND_Q1_SKY_SAVE_H
#include "q1_sky.h"
#include "scene_identity.h"
/* Import the actual global map and event image banks first. Image references
 * resolve against that one saved namespace; restoration never loads a face. */
bool frontend_q1_sky_checkpoint(const frontend_q1_sky *, frontend_scene_namespace *, qa_buffer *, qa_error *);
bool frontend_q1_sky_restore(qa_frontend *, frontend_scene_namespace *, qa_bytes, frontend_q1_sky **, qa_error *);
bool frontend_q1_sky_publish_ready(const frontend_q1_sky *, qa_error *);
#endif
