#ifndef QA_FRONTEND_SHARED_VIDEO_H
#define QA_FRONTEND_SHARED_VIDEO_H
#include "input_settings.h"
#include "qa/console_cvars_prepare.h"

typedef struct frontend_shared_video frontend_shared_video;
bool frontend_shared_video_resolve(void *,const qa_cvar_video_query *,qa_cvar_video_mode *,qa_error *);
bool frontend_shared_video_dimensions(const qa_cvars_edit *,uint32_t fallback_width,
    uint32_t fallback_height,uint32_t *width,uint32_t *height,qa_error *);
/* Project the actual canonical display rows against the returned native
 * window before any resource preparation. This checked read may query SDL. */
bool frontend_shared_video_settings(qa_frontend *,const qa_cvars_edit *,
    qa_display_settings *,bool *changed,qa_error *);
/* Settings are the enclosing canonical owner's admitted typed values. Every
 * physical ALL release must already be completed; preparation retains both
 * native windows, the same renderer, and its actual presentation resources.
 * A failed preparation may retain *out for checked cleanup. */
bool frontend_shared_video_prepare(qa_frontend *,const qa_display_settings *,float gamma,
    frontend_input_settings *,frontend_shared_video **,qa_error *);
bool frontend_shared_video_ready(const frontend_shared_video *,qa_error *);
/* Pure exact child and surface receipts after successful checked readiness. */
bool frontend_shared_video_ready_is(const frontend_shared_video *);
/* Borrow the actual staged endpoint while its complete native handoff is
 * retained. This reads only the prepared owner and its pure child receipts. */
qa_display *frontend_shared_video_candidate(const frontend_shared_video *);
/* Re-present retained CPU pixels after an actual native gamma child. */
bool frontend_shared_video_refresh(frontend_shared_video *,qa_error *);
/* Actual prepared native dimensions/fullscreen for the enclosing canonical
 * scalar owner, before it seals observed window settings. */
bool frontend_shared_video_configuration(const frontend_shared_video *,qa_display_info *,qa_error *);
/* Publish input first, retaining its terminal coordinator until this call.
 * This publication only transfers already prepared pointers and dimensions. */
void frontend_shared_video_publish(frontend_shared_video *);
/* Retire the actual renderer backups before destroying the old display. A
 * refusal retains the child and both display parents for cleanup retry. */
bool frontend_shared_video_finish(frontend_shared_video **,qa_error *);
/* Restore the old surface and renderer before checked input cleanup. Keep
 * both windows alive until input abort/retirement has become terminal. */
bool frontend_shared_video_rollback(frontend_shared_video *,qa_error *);
/* After rollback and terminal input cleanup, release the candidate window. */
bool frontend_shared_video_abort(frontend_shared_video **,qa_error *);
#endif
