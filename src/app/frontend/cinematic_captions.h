#ifndef QA_FRONTEND_CINEMATIC_CAPTIONS_H
#define QA_FRONTEND_CINEMATIC_CAPTIONS_H
#include "ui_features.h"
#include "qa/source_save.h"
typedef struct frontend_cinematic_captions frontend_cinematic_captions;
typedef struct frontend_cinematic_language frontend_cinematic_language;
/* A current movie retains its actual subtitle resource view and compiled
 * pools while the requested language is prepared privately. No movie or
 * prepared subtitle view returns success with NULL. Publish transfers only
 * admitted pointers; finish/abort retain parents until checked cleanup. */
bool frontend_ui_cinematic_language_prepare(qa_frontend *,uint32_t,const char *,
    frontend_cinematic_language **,qa_error *);
bool frontend_ui_cinematic_language_ready(const frontend_cinematic_language *,qa_error *);
/* Pure retained movie/caption identity proof; does not sample movie time. */
bool frontend_ui_cinematic_language_ready_is(const frontend_cinematic_language *);
void frontend_ui_cinematic_language_publish(frontend_cinematic_language *);
/* Successful ready at the same held boundary permits publication plus idle
 * compiled-owner disposal without allocation, callbacks or device setters. */
void frontend_ui_cinematic_language_commit(frontend_cinematic_language *);
bool frontend_ui_cinematic_language_finish(frontend_cinematic_language **,qa_error *);
bool frontend_ui_cinematic_language_abort(frontend_cinematic_language **,qa_error *);
bool frontend_ui_cinematic_init(qa_frontend *,qa_error *);
bool frontend_ui_cinematic_idle(const qa_frontend *);
void frontend_ui_cinematic_destroy(qa_frontend *);
void frontend_ui_cinematic_clear(qa_frontend *,uint32_t seat);
bool frontend_ui_cinematic_prepare(qa_frontend *,qa_vfs *,const char *,uint32_t,qa_error *);
bool frontend_ui_cinematic_draw(qa_frontend *,qa_vfs *,const char *,uint32_t,double elapsed_ms,
    double source_ms,uint64_t loop,qa_media_status,qa_scene_rect,qa_scene_frame *,qa_error *);
bool frontend_ui_cinematic_content_visit(const qa_frontend *,const qa_application_content_visitor *,qa_error *);
bool frontend_ui_cinematic_fields(qa_source_save_io *,qa_frontend *);
#endif
