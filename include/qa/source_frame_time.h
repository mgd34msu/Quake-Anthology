#ifndef QA_SOURCE_FRAME_TIME_H
#define QA_SOURCE_FRAME_TIME_H

#include "qa/console.h"

typedef struct qa_source_frame_time_controls {
    double timescale, fixedtime, host_framerate, camera_mode;
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

#endif
