/* Port of src/content/q3/base/bot-debug.ts (interface only). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_BOT_DEBUG_H
#define QA_CONTENT_Q3_BASE_BOT_DEBUG_H
#include <stddef.h>
#include "qa/contracts/math.h"
typedef struct qa_q3_base_bot_debug_polygons {
  void *ctx;
  double (*create)(void *ctx, double color, double count, const qa_vec3 *points, size_t point_count);
} qa_q3_base_bot_debug_polygons;
#endif
