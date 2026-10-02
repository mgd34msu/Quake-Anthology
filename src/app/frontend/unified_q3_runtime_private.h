#ifndef QA_FRONTEND_UNIFIED_Q3_RUNTIME_PRIVATE_H
#define QA_FRONTEND_UNIFIED_Q3_RUNTIME_PRIVATE_H
#include "unified_q3_runtime.h"
struct frontend_unified_q3_runtime {
    frontend_unified_q3_runtime_options options;
    frontend_unified_q3_runtime_owners children;
    q3n_native_frame_options settings;
    const q3n_compiled_frame *entered;
    const frontend_unified_q3_command *command;
    const frontend_unified_q3_client_frame *rebind;
    qa_q3_refdef refdef;
    qa_vec3 view_angles;
    int32_t old_time, frame_milliseconds, client_frame;
    uint32_t stereo;
    bool complete, initialized, busy, faulted, retiring, restoring;
    bool prepared, prediction_prepared, information_prepared, rendered;
    struct frontend_unified_q3_runtime_video *video;
    uint64_t video_generation;
    bool video_constructor;
};
bool frontend_unified_q3_runtime_build_children(frontend_unified_q3_runtime *, qa_error *);
bool frontend_unified_q3_runtime_close_children(frontend_unified_q3_runtime *, qa_error *);
frontend_unified_q3_snapshots_options frontend_unified_q3_runtime_cache_options(frontend_unified_q3_runtime *);
#endif
