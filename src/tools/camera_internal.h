#ifndef QA_CAMERA_INTERNAL_H
#define QA_CAMERA_INTERNAL_H
#include "qa/camera.h"

typedef struct camera_curve { qa_vec3 *points; double *distances, distance; size_t count; } camera_curve;
struct qa_camera_document {
    size_t references;
    qa_arena storage;
    qa_camera_definition definition;
    camera_curve position, *targets;
};
bool camera_fail(qa_error *, const char *);
void *camera_array(qa_arena *, size_t, size_t, size_t alignment, qa_error *);
char *camera_text(qa_arena *, const char *, qa_error *);
bool camera_number(qa_bytes, bool allow_empty, double *, qa_error *);
#endif
