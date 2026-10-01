#ifndef QA_FRONTEND_CINEMATIC_CAPTIONS_H
#define QA_FRONTEND_CINEMATIC_CAPTIONS_H
#include "ui_features.h"
#include "qa/source_save.h"
typedef struct frontend_cinematic_captions frontend_cinematic_captions;
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
