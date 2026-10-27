/* Port of src/render/scene/source-sort.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_SOURCE_SORT_H
#define QA_RENDER_SCENE_SOURCE_SORT_H
#include <stddef.h>
#include <stdint.h>
uint32_t qa_pack_source_draw_sort(double shader, double entity, double fog, double dlight);
/* TS SourceDrawSortRange as fn+ctx; sorts are unsigned words. */
typedef struct qa_source_draw_sort_range {
  void *ctx; size_t length;
  uint32_t (*get_sort)(void *ctx, size_t index);
  void (*set_sort)(void *ctx, size_t index, uint32_t sort);
  void (*swap)(void *ctx, size_t first, size_t second);
} qa_source_draw_sort_range;
void qa_sort_draw_surfs(qa_source_draw_sort_range range);
#endif
