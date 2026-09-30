#ifndef QA_CAMERA_H
#define QA_CAMERA_H
#include "qa/scene.h"

typedef enum qa_camera_path_kind { QA_CAMERA_FIXED, QA_CAMERA_INTERPOLATED, QA_CAMERA_SPLINE } qa_camera_path_kind;
typedef struct qa_camera_velocity { double start_ms, duration_ms, speed; } qa_camera_velocity;
typedef struct qa_camera_path {
    qa_camera_path_kind kind;
    const char *name;
    double time_ms, base_velocity;
    const qa_camera_velocity *velocities;
    size_t velocity_count;
    union {
        qa_vec3 fixed;
        struct { qa_vec3 start, end; } interpolated;
        struct { float granularity; const qa_vec3 *points; size_t count; } spline;
    } position;
} qa_camera_path;
typedef struct qa_camera_event { uint32_t type; const char *parameter; double time_ms; } qa_camera_event;
typedef struct qa_camera_definition {
    double seconds;
    qa_camera_path position;
    const qa_camera_path *targets;
    size_t target_count;
    const qa_camera_event *events;
    size_t event_count;
    struct { float value, start, end; double time_ms; } fov;
} qa_camera_definition;
typedef struct qa_camera_document qa_camera_document;
typedef struct qa_camera_playback qa_camera_playback;
typedef struct qa_camera_sample {
    qa_vec3 origin, direction;
    float fov;
    const qa_camera_event *events;
    size_t event_count;
} qa_camera_sample;
/* Immutable documents copy authored data and retain compiled curves/distances.
 * Editing creates a replacement document; active playbacks retain their source. */
bool qa_camera_create(const qa_camera_definition *, qa_camera_document **, qa_error *);
bool qa_camera_decode(qa_bytes, qa_camera_document **, qa_error *);
bool qa_camera_encode(const qa_camera_document *, qa_buffer *, qa_error *);
/* Renderer-independent authoring adapters; outputs are owned text. */
bool qa_camera_normalize(qa_bytes, qa_buffer *, qa_error *);
bool qa_camera_sample_json(qa_camera_document *, double step_ms, qa_buffer *, qa_error *);
void qa_camera_retain(qa_camera_document *);
void qa_camera_release(qa_camera_document *);
const qa_camera_definition *qa_camera_describe(const qa_camera_document *);
bool qa_camera_playback_create(qa_camera_document *, double start_ms, qa_camera_playback **, qa_error *);
void qa_camera_playback_destroy(qa_camera_playback *);
/* Event descriptors live in scratch; parameter strings borrow the retained
 * document. Time is monotonic; output is unchanged on admission failure. */
bool qa_camera_playback_sample(qa_camera_playback *, double now_ms, qa_arena *scratch,
                               qa_camera_sample *, bool *active, qa_error *);
bool qa_camera_sample_view(const qa_camera_sample *, const qa_scene_view *, qa_scene_view *, qa_error *);
#endif
