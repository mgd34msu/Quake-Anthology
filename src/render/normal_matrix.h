#ifndef QA_RENDER_NORMAL_MATRIX_H
#define QA_RENDER_NORMAL_MATRIX_H

#include "qa/scene.h"

static inline void qa_render_normal_matrix(const qa_scene_matrix *model,
                                          double result[9])
{
    const float *m = model->m;
    result[0] = (double)m[5] * m[10] - (double)m[9] * m[6];
    result[1] = (double)m[9] * m[2] - (double)m[1] * m[10];
    result[2] = (double)m[1] * m[6] - (double)m[5] * m[2];
    result[3] = (double)m[8] * m[6] - (double)m[4] * m[10];
    result[4] = (double)m[0] * m[10] - (double)m[8] * m[2];
    result[5] = (double)m[4] * m[2] - (double)m[0] * m[6];
    result[6] = (double)m[4] * m[9] - (double)m[8] * m[5];
    result[7] = (double)m[8] * m[1] - (double)m[0] * m[9];
    result[8] = (double)m[0] * m[5] - (double)m[4] * m[1];
    double determinant = m[0] * result[0] + m[4] * result[1] +
                         m[8] * result[2];
    if (determinant != 0)
        for (size_t i = 0; i < 9; ++i) result[i] /= determinant;
}

#endif
