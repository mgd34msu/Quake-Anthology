#ifndef QA_SOURCE_FRAME_TIME_H
#define QA_SOURCE_FRAME_TIME_H

#include "qa/console.h"

typedef struct qa_source_frame_time_controls {
    double timescale, fixedtime, host_framerate, camera_mode, maximum_fps, rate;
} qa_source_frame_time_controls;

/* The selected source registry owns these values. These functions introduce
 * no numeric cache or host-clock accumulator. */
bool qa_source_frame_time_register(qa_cvars *, uint64_t owner, qa_error *);
bool qa_source_frame_time_controls_read(const qa_cvars *, qa_source_frame_time_controls *, qa_error *);
bool qa_source_frame_time_transform(qa_console_dialect, double supplied_milliseconds,
    const qa_source_frame_time_controls *, bool dedicated, bool local_server,
    double *source_milliseconds, qa_error *);
bool qa_source_frame_time_sample(const qa_cvars *, double supplied_milliseconds,
    bool dedicated, bool local_server, double *source_milliseconds, qa_error *);
/* Host_FilterTime and QW's client Host_Frame admit the accumulated host delta
 * before transforming simulation time. The caller owns that pending delta. */
bool qa_source_frame_time_admit(const qa_cvars *, uint64_t pending_ns,
    bool dedicated, bool *accepted, uint64_t *source_ns, qa_error *);

#endif
