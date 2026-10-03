#ifndef QA_FRONTEND_CAMPAIGN_CINEMATIC_H
#define QA_FRONTEND_CAMPAIGN_CINEMATIC_H
#include "qa/frontend.h"
#include "qa/cinematic.h"
#include "qa/input.h"

typedef struct frontend_cinematic_view {
    qa_vfs *files;
    const char *path;
    uint32_t seat;
    qa_scene_rect viewport;
    qa_media_status status;
    double elapsed_ms, source_ms;
    uint64_t loop;
} frontend_cinematic_view;

bool frontend_cinematic_command(qa_frontend *,const qa_command_invocation *,qa_error *);
bool frontend_cinematic_travel(qa_frontend *,const qa_application_travel_view *,qa_error *);
/* Call after command, input and playback callbacks have returned. Preparation
 * owns its real decoder and PCM privately until activation. */
bool frontend_cinematic_drain(qa_frontend *,qa_error *);
bool frontend_cinematic_frame(qa_frontend *,uint64_t elapsed_ns,bool *rendered,qa_error *);
bool frontend_cinematic_input(qa_frontend *,uint32_t seat,qa_input_focus,const qa_input_event *,bool *,qa_error *);
bool frontend_cinematic_view_read(qa_frontend *,frontend_cinematic_view *,bool *found,qa_error *);
bool frontend_cinematic_view_current(const qa_frontend *,const qa_vfs *,const char *path,uint32_t physical_seat);
bool frontend_cinematic_running(const qa_frontend *);
bool frontend_cinematic_idle(const qa_frontend *);
/* Standalone screen playback follows the source save/load rejection. */
bool frontend_cinematic_capture_ready(const qa_frontend *);
bool frontend_cinematic_destroy(qa_frontend *,qa_error *);
#endif
