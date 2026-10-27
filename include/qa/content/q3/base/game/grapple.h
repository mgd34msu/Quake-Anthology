/* Port of src/content/q3/base/game/grapple.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_GRAPPLE_H
#define QA_CONTENT_Q3_BASE_GAME_GRAPPLE_H
#include <stdbool.h>
#include "qa/contracts/math.h"
enum { QA_Q3_GRAPPLE_SPEED = 800, QA_Q3_GRAPPLE_LIFETIME = 10000, QA_Q3_GRAPPLE_THINK_INTERVAL = 100 };
typedef struct qa_q3_grapple_cable { qa_vec3 start; qa_vec3 end; } qa_q3_grapple_cable;
/* Natural name for TS q3GrappleVelocity; movement/q3/move.c keeps a static mirror to replace later. */
qa_vec3 qa_q3_grapple_velocity(qa_vec3 origin, qa_vec3 point, qa_vec3 forward);
qa_vec3 qa_q3_grapple_target(qa_vec3 origin, qa_bounds bounds);
/* Returns false for TS null. view_height default 26: pass QA_Q3_GRAPPLE_DEFAULT_VIEW_HEIGHT. */
enum { QA_Q3_GRAPPLE_DEFAULT_VIEW_HEIGHT = 26 };
bool qa_q3_get_grapple_cable(qa_vec3 origin, qa_vec3 up, qa_vec3 point, double view_height, qa_q3_grapple_cable *out);
#endif
