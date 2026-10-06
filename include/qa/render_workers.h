#ifndef QA_RENDER_WORKERS_H
#define QA_RENDER_WORKERS_H
#include "qa/scene.h"

typedef struct qa_render_workers qa_render_workers;
typedef struct qa_render_model_job {
    qa_model_md5_view view;
    const qa_scene_skin_pose *pose;
    qa_model_vertex *vertices;
    size_t vertex_count;
    int rounding;
    qa_error error;
    bool completed;
} qa_render_model_job;

/* One control thread owns publication and lifetime. Both render endpoints and
 * callers may retain the same workers; CPU queues drain before another endpoint
 * uses their retained raster storage. Unavailable threads use scalar work. */
qa_render_workers *qa_render_workers_create(qa_error *);
bool qa_render_workers_retain(qa_render_workers *, qa_error *);
void qa_render_workers_release(qa_render_workers *);
/* Borrow immutable geometry/poses and disjoint output spans until return. No
 * allocation or callbacks run. Each job uses its admitted FE_* rounding mode;
 * caller/thread environments are restored with generated exceptions retained.
 * Completed jobs retain their individual error;
 * false reports a batch admission failure before any job was changed. NULL
 * workers execute the same jobs synchronously on the calling thread. */
bool qa_render_workers_skin_batch(qa_render_workers *, qa_render_model_job *,
                                  size_t count, qa_error *);
#endif
