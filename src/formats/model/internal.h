#ifndef QA_MODEL_INTERNAL_H
#define QA_MODEL_INTERNAL_H
#include "qa/binary.h"
#include "qa/model.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct model_reader {
    qa_bytes bytes;
    size_t pos;
    qa_error *error;
    bool ok;
} model_reader;
bool model_fail(model_reader *r, const char *message);
bool model_range(model_reader *r, size_t offset, size_t count, size_t stride, size_t minimum,
                 size_t end);
void *model_alloc(model_reader *r, size_t count, size_t size);
void *model_grow(model_reader *r, void *ptr, size_t old_count, size_t new_count, size_t size);
const uint8_t *model_take(model_reader *r, size_t size);
int32_t model_i32(model_reader *r);
uint32_t model_count(model_reader *r, uint32_t minimum, uint32_t maximum);
float model_float(model_reader *r);
void model_vec(model_reader *r, float *out, size_t count);
void model_string(model_reader *r, char *out, size_t width);
void model_bounds_clear(qa_model_bounds *bounds);
void model_bounds_add(qa_model_bounds *bounds, const float point[3]);
void model_bounds_union(qa_model_bounds *bounds, const qa_model_bounds *other);
void model_normalize(float value[3]);
bool model_mdl(model_reader *r, qa_model *m);
bool model_md2(model_reader *r, qa_model *m);
bool model_md3(model_reader *r, qa_model *m);
bool model_md4(model_reader *r, qa_model *m);
bool model_is_md5(qa_bytes bytes);
bool model_md5(model_reader *r, qa_model *m);
bool model_sprite(model_reader *r, qa_model *m, bool q2);
qa_bytes model_token_next(model_reader *r);
bool model_expect(model_reader *r, const char *expected);
double model_number(model_reader *r);
int32_t model_integer(model_reader *r, int32_t minimum, int32_t maximum);
float model_scalar(model_reader *r);
void model_vector(model_reader *r, float out[3]);
static inline qa_vec3 model_v3(const float v[3]) { return qa_v3(v[0], v[1], v[2]); }
static inline void model_v3_store(float out[3], qa_vec3 v) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}
#endif
