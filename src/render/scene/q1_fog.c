/* Port of src/render/scene/q1-fog.ts. GPL-2.0-or-later. */
#include "qa/render/scene/q1_fog.h"
#include <stdlib.h>
#include <string.h>
void qa_scene_fogged_operations_destroy(qa_scene_fogged_operations *ops) {
  if (!ops || !ops->owned) { if (ops) memset(ops, 0, sizeof *ops); return; }
  if (ops->batches) for (size_t i = 0; i < ops->count; i++) free(ops->batches[i]);
  if (ops->fogs) for (size_t i = 0; i < ops->count; i++) free(ops->fogs[i]);
  free(ops->batches); free(ops->fogs); free(ops->operations); memset(ops, 0, sizeof *ops);
}
const qa_render_operation *qa_fog_scene_operations(const qa_render_operation *operations, size_t count,
                                                   const qa_scene_fog *fog, qa_scene_fogged_operations *out,
                                                   size_t *out_count) {
  memset(out, 0, sizeof *out);
  if (!fog || fog->density <= 0) {
    out->operations = (qa_render_operation *)operations; out->count = count; out->owned = 0;
    if (out_count) *out_count = count;
    return operations;
  }
  out->operations = malloc((count ? count : 1) * sizeof *out->operations);
  out->batches = calloc(count ? count : 1, sizeof *out->batches);
  out->fogs = calloc(count ? count : 1, sizeof *out->fogs);
  if (!out->operations || !out->batches || !out->fogs) { out->owned = 1; out->count = count; qa_scene_fogged_operations_destroy(out); return NULL; }
  out->owned = 1; out->count = count;
  for (size_t i = 0; i < count; i++) {
    out->operations[i] = operations[i];
    if (operations[i].kind != QA_RENDER_OP_DRAW && operations[i].kind != QA_RENDER_OP_OBJECT_OPACITY) continue;
    size_t nb = operations[i].batch_count;
    qa_draw_batch *nbatches = malloc((nb ? nb : 1) * sizeof *nbatches);
    qa_batch_fog *backing = malloc((nb ? nb : 1) * sizeof *backing);
    if (!nbatches || !backing) { free(nbatches); free(backing); qa_scene_fogged_operations_destroy(out); return NULL; }
    for (size_t k = 0; k < nb; k++) {
      nbatches[k] = operations[i].batches[k];
      if (operations[i].batches[k].fog) continue;
      int additive = operations[i].batches[k].state.blend.destination
        && !strcmp(operations[i].batches[k].state.blend.destination, "one");
      memset(&backing[k], 0, sizeof backing[k]);
      backing[k].kind = "exp2"; backing[k].density = fog->density;
      backing[k].color = additive ? (qa_vec3){ 0, 0, 0 } : fog->color;
      nbatches[k].fog = &backing[k];
    }
    out->batches[i] = nbatches; out->fogs[i] = backing;
    out->operations[i].batches = nbatches;
  }
  if (out_count) *out_count = count;
  return out->operations;
}
