#ifndef QA_FRONTEND_REMOTE_UNIFIED_PREDICTION_PRIVATE_H
#define QA_FRONTEND_REMOTE_UNIFIED_PREDICTION_PRIVATE_H
#include "remote_unified_prediction.h"
typedef struct prediction_command {
    qa_unified_movement raw;
    uint64_t sequence;
    double time_ms;
} prediction_command;
typedef struct prediction_snapshot {
    qa_movement_input input;
    qa_vec3 angles, offset, pml;
    qa_movement_ground ground;
    float height;
    int32_t water_level, water_type;
    double time_ms;
    int64_t sequence;
} prediction_snapshot;
struct frontend_remote_unified_prediction {
    frontend_remote_unified *replica;
    qa_executable_recipe *recipe;
    qa_actor_registry *registry;
    qa_collision_geometry *geometry;
    qa_world *scene;
    qa_unified_document *snapshot_document;
    prediction_snapshot snapshot;
    prediction_command commands[64];
    size_t command_count;
    int64_t discarded;
    uint32_t epoch;
    int rounding;
    bool received, busy;
};
#endif
