#ifndef QA_SCENE_MARKS_H
#define QA_SCENE_MARKS_H

#include "qa/scene.h"

typedef struct qa_scene_mark_fragment {
    size_t first_point, point_count;
} qa_scene_mark_fragment;
typedef struct qa_scene_mark_result {
    size_t point_count, fragment_count;
} qa_scene_mark_result;

/* Synchronous readonly projection against this actual world's prepared faces
 * and stitched Bezier grids, before view-dependent LOD. The caller admits the
 * current source generation and keeps the world alive and unchanged throughout
 * the call. This does not admit an actor, load resources, alter visibility, or
 * emit draws. Input/output spans must be disjoint and owned by the caller.
 *
 * Bounds consume every input point; the first 64 construct the clip planes.
 * Four input points with 384 output points and 128 fragments implement the Q3
 * impact-mark contract. Capacities may be smaller or larger. A polygon that
 * cannot fit the remaining point capacity is skipped, without partial writes.
 * Zero output capacity succeeds with an empty result. Only populated prefixes
 * are written. Invalid arguments leave both buffers and result unchanged. */
bool qa_scene_world_mark_fragments(const qa_scene_world *,
    const qa_vec3 *points, size_t point_count, qa_vec3 projection,
    qa_vec3 *point_buffer, size_t max_points,
    qa_scene_mark_fragment *fragment_buffer, size_t max_fragments,
    qa_scene_mark_result *, qa_error *);

#endif
