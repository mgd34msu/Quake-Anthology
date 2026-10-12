#ifndef QA_FRONTEND_QC_RERELEASE_EVENTS_H
#define QA_FRONTEND_QC_RERELEASE_EVENTS_H
#include "qa/frontend.h"
#include "qa/builtin.h"
#include "qa/audio.h"
/* World text uses EFFECT/q1:rerelease-debug with the WORLDTEXT builtin code,
 * text ID, origin, size and numeric palette/lifetime/depth tuple. Shape lines
 * are admitted by the source through the common application DEBUG ring. */
bool frontend_qc_rerelease_event(qa_frontend *,const qa_builtin_event *,bool *,qa_error *);
bool frontend_qc_rerelease_draw(qa_frontend *,uint32_t,const qa_scene_view *,qa_error *);
bool frontend_qc_rerelease_idle(const qa_frontend *);
void frontend_qc_rerelease_retire_world(qa_frontend *);
void frontend_qc_rerelease_destroy(qa_frontend *);
/* Borrow the ordinary event resource owners; their existing inventories retain
 * every actual image, palette and sound-bank registration. */
bool frontend_event_qc_resources(qa_frontend *,qa_actor_owner,qa_scene_resources **,qa_audio_bank **,qa_error *);
#endif
