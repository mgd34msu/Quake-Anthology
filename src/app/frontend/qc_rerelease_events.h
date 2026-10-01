#ifndef QA_FRONTEND_QC_RERELEASE_EVENTS_H
#define QA_FRONTEND_QC_RERELEASE_EVENTS_H
#include "qa/frontend.h"
#include "qa/builtin.h"
#include "qa/audio.h"
/* The actual source emits EFFECT/q1:rerelease-debug with its EX_DRAW builtin
 * code and three numeric arguments: palette index, lifetime seconds, depth.
 * origin/end/direction carry geometry; value is size, volume radius/length,
 * attenuation cylinder half-height. WORLDTEXT carries the actual text ID. */
bool frontend_qc_rerelease_event(qa_frontend *,const qa_builtin_event *,bool *,qa_error *);
bool frontend_qc_rerelease_draw(qa_frontend *,uint32_t,const qa_scene_view *,qa_error *);
bool frontend_qc_rerelease_idle(const qa_frontend *);
void frontend_qc_rerelease_retire_world(qa_frontend *);
void frontend_qc_rerelease_destroy(qa_frontend *);
bool frontend_qc_rerelease_checkpoint(qa_frontend *,qa_buffer *,qa_error *);
bool frontend_qc_rerelease_restore(qa_frontend *,qa_bytes,qa_error *);
/* Borrow the ordinary event resource owners; their existing inventories retain
 * every actual image, palette and sound-bank registration. */
bool frontend_event_qc_resources(qa_frontend *,qa_actor_owner,qa_scene_resources **,qa_audio_bank **,qa_error *);
#endif
