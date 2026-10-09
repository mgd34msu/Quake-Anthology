#ifndef QA_SOURCE_FRAME_TIME_H
#define QA_SOURCE_FRAME_TIME_H

#include "qa/console.h"

typedef struct qa_source_frame_time_controls {
    double timescale, fixedtime, host_framerate, camera_mode, maximum_fps, rate;
    double server_minimum_seconds, server_maximum_seconds;
} qa_source_frame_time_controls;

typedef struct qa_source_frame_time_binding {
    const qa_cvars *cvars;
    qa_cvar_handle timescale, fixedtime, host_framerate, camera_mode, maximum_fps, rate;
    qa_cvar_handle server_minimum_seconds, server_maximum_seconds, dedicated, capture_fps;
} qa_source_frame_time_binding;

/* Original integer wall timer delta. The caller's absolute clock retains
 * sub-millisecond time across host renders and restored continuations. */
static inline uint64_t qa_source_frame_time_host_delta(uint64_t boundary_ns, uint64_t elapsed_ns)
{
    return (boundary_ns / UINT64_C(1000000) -
        (boundary_ns - elapsed_ns) / UINT64_C(1000000)) * UINT64_C(1000000);
}

/* Bind after declarations on the actual source/client owner. The shared store
 * owns values and projections; the binding owns only indexed references. */
bool qa_source_frame_time_register(qa_cvars *, uint64_t owner, qa_error *);
void qa_source_frame_time_bind(const qa_cvars *, qa_source_frame_time_binding *);
bool qa_source_frame_time_controls_read(const qa_source_frame_time_binding *, qa_source_frame_time_controls *, qa_error *);
bool qa_source_frame_time_transform(qa_console_dialect, double supplied_milliseconds,
    const qa_source_frame_time_controls *, bool dedicated, bool local_server,
    double *source_milliseconds, qa_error *);
bool qa_source_frame_time_sample(const qa_source_frame_time_binding *, double supplied_milliseconds,
    bool dedicated, bool local_server, double *source_milliseconds, qa_error *);
/* Host_FilterTime, QW client Host_Frame and QW server SV_Physics admit their
 * own accumulated host delta. The caller owns that pending delta. */
bool qa_source_frame_time_admit(const qa_source_frame_time_binding *, uint64_t pending_ns,
    bool server, bool *accepted, uint64_t *source_ns, qa_error *);

#endif
