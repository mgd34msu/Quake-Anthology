#ifndef QA_FRONTEND_SHARED_AUDIO_H
#define QA_FRONTEND_SHARED_AUDIO_H
#include "internal.h"
#include "qa/console_cvars_prepare.h"

typedef struct frontend_shared_audio frontend_shared_audio;
/* Checked projection for actual initial native constructors before their
 * engine/device owners exist. It borrows the returned canonical edit. */
bool frontend_shared_audio_configuration(const qa_cvars_edit *,qa_audio_output_format *,
    float *effects,float *music,qa_error *);
/* Prepare only after actual source callbacks and held release programmes have
 * completed. The real engine/device leases exclude ordinary audio work until
 * publication or checked abort. Failed preparation may retain *out. */
bool frontend_shared_audio_prepare(qa_frontend *,const qa_cvars_edit *,
    frontend_shared_audio **,qa_error *);
/* The native device may pause consumption and prequeue its true retained PCM.
 * Requalify the complete enclosing bundle before its nofail publication. */
bool frontend_shared_audio_ready(frontend_shared_audio *,qa_error *);
/* Pure proof of these exact admitted engine and device children. */
bool frontend_shared_audio_ready_is(const frontend_shared_audio *);
/* Borrow only for an admitted child of this same retained gains lease. The
 * child must be consumed before this parent's publish or checked abort. */
qa_audio_engine_gains *frontend_shared_audio_gains(const frontend_shared_audio *);
/* Actual prepared music target, including for a policy-held detached player. */
bool frontend_shared_audio_music_gain(const frontend_shared_audio *,float *);
bool frontend_shared_audio_parent_is(const frontend_shared_audio *,const qa_frontend *,
    const qa_audio_engine *);
void frontend_shared_audio_publish(frontend_shared_audio **);
bool frontend_shared_audio_abort(frontend_shared_audio **,qa_error *);
#endif
