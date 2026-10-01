#ifndef QA_Q3_NATIVE_MARKS_H
#define QA_Q3_NATIVE_MARKS_H
#include "frame.h"
typedef struct q3n_impact_mark {
    int32_t shader;
    qa_vec3 origin, direction;
    float orientation, color[4], radius;
    bool alpha_fade, temporary;
} q3n_impact_mark;
bool q3n_marks_impact(const q3n_frame *, const q3n_impact_mark *, qa_error *);
bool q3n_marks_submit(const q3n_frame *, qa_error *);
#endif
