/* Port of src/content/q2/foundation/fields.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_FIELDS_H
#define QA_CONTENT_Q2_FOUNDATION_FIELDS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q2/foundation/host.h"
#include "qa/core/math.h"
double qa_q2_number_field(const qa_q2_spawn_fields *f, const char *key, double fallback);
double qa_q2_integer_field(const qa_q2_spawn_fields *f, const char *key, double fallback);
qa_vec3 qa_q2_vector_field(const qa_q2_spawn_fields *f, const char *key);
/* Malloc'd array; free with qa_q2_entities_free. Aborts on TS Errors. */
bool qa_q2_parse_entities(const char *source, qa_q2_edition edition, qa_q2_spawn_fields **out, size_t *out_count);
void qa_q2_entities_free(qa_q2_spawn_fields *entities, size_t count);
bool qa_q2_inhibit_spawn(const qa_q2_spawn_fields *f, qa_q2_game_options options);
qa_vec3 qa_q2_movedir(qa_vec3 angles);
/* TS re-exports add/subtract/scale/dot/length/normalize/zero. */
#define qa_q2_add qa_add3_double
#define qa_q2_subtract qa_sub3_double
#define qa_q2_scale qa_scale3_double
#define qa_q2_dot qa_dot3_double
#define qa_q2_length qa_hypot3
#define qa_q2_normalize qa_normalize3_double_hypot
extern const qa_vec3 QA_Q2_ZERO;
#endif
