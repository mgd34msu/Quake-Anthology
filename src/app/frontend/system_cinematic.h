#ifndef QA_FRONTEND_SYSTEM_CINEMATIC_H
#define QA_FRONTEND_SYSTEM_CINEMATIC_H
#include "campaign_cinematic.h"
#include "qa/q3_host.h"
#include "qa/persistence_content.h"

typedef struct frontend_system_cinematic frontend_system_cinematic;
typedef struct frontend_system_cinematic_identity {
    uint64_t source_group, service_owner, audio_bus;
    uint64_t source_owner;
    qa_qvm_role role;
    uint32_t physical_seat, launch_seat;
} frontend_system_cinematic_identity;
typedef struct frontend_system_cinematic_source {
    frontend_system_cinematic_identity identity;
    qa_vfs *files;
    qa_media_library *movies;
    qa_cvars *cvars;
    struct qa_q3_cinematic_source *cinematics;
    void *context;
    /* The constructor transfers one real role lease. Current is pure and
     * proves that same source/CLIENT namespace and physical recipient. */
    bool (*current)(void *,const struct frontend_system_cinematic_source *);
    bool (*append)(void *,const char *,qa_error *);
    void (*release)(void *);
} frontend_system_cinematic_source;

/* Success transfers source's held lease and one real handle to the caller.
 * Failure leaves the incoming lease with the caller. Request flags are exact;
 * campaign-command hold defaults are not applied. */
bool frontend_system_cinematic_open(qa_frontend *,const frontend_system_cinematic_source *,
    const qa_q3_movie_request *,qa_q3_system_movie *,qa_error *);
bool frontend_system_cinematic_running(const qa_frontend *);
bool frontend_system_cinematic_idle(const qa_frontend *);
bool frontend_system_cinematic_capture_ready(const qa_frontend *);
bool frontend_system_cinematic_drain(qa_frontend *,qa_error *);
bool frontend_system_cinematic_command(qa_frontend *,const qa_command_invocation *,bool *handled,qa_error *);
bool frontend_system_cinematic_frame(qa_frontend *,uint64_t elapsed_ns,bool *rendered,qa_error *);
bool frontend_system_cinematic_input(qa_frontend *,uint32_t,qa_input_focus,const qa_input_event *,bool *,qa_error *);
bool frontend_system_cinematic_view_read(qa_frontend *,frontend_cinematic_view *,bool *,qa_error *);
bool frontend_system_cinematic_view_current(const qa_frontend *,const qa_vfs *,const char *,uint32_t);
/* Stop actual screen owners before lower slot retirement. Release of each
 * lower handle removes its retained row; checked teardown requires no rows. */
bool frontend_system_cinematic_stop_all(qa_frontend *,qa_error *);
bool frontend_system_cinematic_destroy(qa_frontend *,qa_error *);
bool frontend_system_cinematic_rebind_ready(const qa_frontend *,const qa_frontend *,qa_error *);
void frontend_system_cinematic_rebind(qa_frontend *,qa_frontend *);
bool frontend_system_cinematic_content_visit(const qa_frontend *,const qa_application_content_visitor *,qa_error *);
#endif
