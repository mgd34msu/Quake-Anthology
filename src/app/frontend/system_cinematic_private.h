#ifndef QA_FRONTEND_SYSTEM_CINEMATIC_PRIVATE_H
#define QA_FRONTEND_SYSTEM_CINEMATIC_PRIVATE_H
#include "system_cinematic.h"
#include "internal.h"
#include "qa/cinematic_restore.h"
#include "qa/q3_cinematic_handles.h"
typedef enum system_cinematic_phase { SYSTEM_PLAYING, SYSTEM_COMPLETED, SYSTEM_STOPPED } system_cinematic_phase;
struct frontend_system_cinematic {
    frontend_system_cinematic *next;
    qa_frontend *frontend;
    frontend_system_cinematic_source source;
    qa_cinematic_asset *asset;
    qa_cinematic *movie;
    qa_q3_cinematic_source *numeric_source;
    int32_t numeric_handle;
    char *path,*nextmap;
    double clock_ms;
    system_cinematic_phase phase;
    qa_cinematic_end ending_reason;
    bool loop,hold,silent,screen,busy,restore_pending,ending,appended,focus_paused;
};
bool frontend_system_cinematic_source_current(const frontend_system_cinematic *);
void frontend_system_cinematic_handle(frontend_system_cinematic *,qa_q3_system_movie *);
void frontend_system_cinematic_row_free(frontend_system_cinematic *,bool cold);
qa_cinematic_options frontend_system_cinematic_options(frontend_system_cinematic *,qa_audio_engine *);
bool frontend_system_cinematic_source_valid(const qa_frontend *,const frontend_system_cinematic_source *);
bool frontend_system_cinematic_path_valid(const char *);
bool frontend_system_cinematic_restore_attach(frontend_system_cinematic *,qa_error *);
#endif
