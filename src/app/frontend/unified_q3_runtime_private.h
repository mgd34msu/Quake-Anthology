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
    int32_t old_time, frame_milliseconds, client_frame, presentation_time;
    uint32_t stereo;
    bool complete, initialized, busy, faulted, retiring, restoring, restored, passive;
    bool prepared, prediction_prepared, prediction_applied, information_prepared, rendered;
    qa_q3_supplement *supplement;
    struct qa_q3_source_scene_bank *scene_bank;
    qa_scene_light scene_lights[32];
    uint32_t scene_light_first;
    size_t scene_light_count;
    bool camera_prepared, scene_prepared, scene_submitted, scene_third_person;
    bool view_weapon_handled, view_weapon_replaced;
    bool packet_handled[1022];
    qa_actor_id packet_actors[1022];
    struct frontend_unified_q3_runtime_video *video;
    uint64_t video_generation;
    bool video_constructor;
    qa_buffer import_bytes;
    unsigned imported_children;
};
bool frontend_unified_q3_runtime_build_children(frontend_unified_q3_runtime *, qa_error *);
bool frontend_unified_q3_runtime_close_children(frontend_unified_q3_runtime *, qa_error *);
frontend_unified_q3_snapshots_options frontend_unified_q3_runtime_cache_options(frontend_unified_q3_runtime *);
#endif
