/* Port of src/content/q3/presentation/model-access.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/model_access.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/formats/q3_model/md3.h"
#include "qa/formats/q3_model/md5.h"
#include "qa/formats/q3_model/scene.h"
static const qa_bounds ZERO = {{0, 0, 0}, {0, 0, 0}};
/* TS models[index] with a non-integer / negative / out-of-range index is undefined. */
static bool model_index(double index, size_t count, size_t *out) {
  if (!(index >= 0) || index != floor(index) || index >= (double)count) return false;
  *out = (size_t)index;
  return true;
}
qa_bounds qa_q3_pres_model_bounds(const qa_q3_pres_scene_model *source) {
  if (source->kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) return ZERO;
  if (source->kind == QA_Q3_PRES_SCENE_MODEL_INLINE) return source->bounds;
  const qa_decoded_model *model = &source->model;
  /* TS `"bounds" in model`: Q1/Q2 alias and sprite models carry top-level bounds. */
  switch (model->kind) {
    case QA_DECODED_MODEL_Q1_ALIAS: return model->as.q1_alias->bounds;
    case QA_DECODED_MODEL_Q2_ALIAS: return model->as.q2_alias->bounds;
    case QA_DECODED_MODEL_Q1_SPRITE: return model->as.q1_sprite->bounds;
    case QA_DECODED_MODEL_Q2_SPRITE: return model->as.q2_sprite->bounds;
    case QA_DECODED_MODEL_BRUSH_MODEL: {
      size_t index = 0;
      const qa_decoded_world *world = &model->brush_world;
      if (world->kind == QA_DECODED_WORLD_Q1 && model_index(model->brush_model_index, world->as.q1->model_count, &index))
        return world->as.q1->models[index].bounds;
      if (world->kind == QA_DECODED_WORLD_Q2 && model_index(model->brush_model_index, world->as.q2->model_count, &index))
        return world->as.q2->models[index].bounds;
      if (world->kind == QA_DECODED_WORLD_Q3 && model_index(model->brush_model_index, world->as.q3->model_count, &index))
        return world->as.q3->models[index].bounds;
      return ZERO;
    }
    case QA_DECODED_MODEL_Q3_MESH:
      return model->as.q3_mesh->frame_count > 0 ? model->as.q3_mesh->frames[0].bounds : ZERO;
    case QA_DECODED_MODEL_MD4:
      return model->as.md4->frame_count > 0 ? model->as.md4->frames[0].bounds : ZERO;
    case QA_DECODED_MODEL_MD5:
      return model->as.md5->frame_count > 0 ? model->as.md5->frames[0].bounds : ZERO;
  }
  return ZERO;
}
/* TS tags[Math.min(frame, frames.length - 1)]?.find(tag => tag.name === name). */
static const qa_model_tag *find_tag(const qa_q3_mesh_model *model, const char *name, double frame) {
  double clamped = frame < (double)model->frame_count - 1 ? frame : (double)model->frame_count - 1;
  size_t row = 0;
  if (!model_index(clamped, model->tag_row_count, &row)) return NULL;
  for (size_t i = 0; i < model->tag_row_counts[row]; i++) {
    if (strcmp(model->tag_rows[row][i].name, name) == 0) return &model->tag_rows[row][i];
  }
  return NULL;
}
bool qa_q3_pres_lerp_model_tag(const qa_q3_pres_scene_model *source, const char *name, double start, double end,
                               double fraction, qa_q3_pres_model_tag *out) {
  if (source->kind != QA_Q3_PRES_SCENE_MODEL_LOADED) return false;
  const qa_decoded_model *model = &source->model;
  if (model->kind == QA_DECODED_MODEL_Q3_MESH) {
    const qa_model_tag *first = find_tag(model->as.q3_mesh, name, start);
    const qa_model_tag *second = find_tag(model->as.q3_mesh, name, end);
    if (first == NULL || second == NULL) return false;
    /* TS adapts ModelTag{axis} to Md3Tag{axes} before interpolateMd3Tags. */
    qa_md3_tag from = {(char *)first->name, first->origin, first->axis};
    qa_md3_tag to = {(char *)second->name, second->origin, second->axis};
    qa_md3_tag mixed = {NULL, {0, 0, 0}, {{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}};
    qa_md3_interpolate_tags(&from, &to, name, fraction, &mixed);
    out->origin = mixed.origin;
    out->axes = mixed.axes;
    free(mixed.name); /* interpolate_tags stores an owned copy of name */
    return true;
  }
  if (model->kind == QA_DECODED_MODEL_MD5) {
    const qa_md5_model *md5 = model->as.md5;
    long index = -1;
    for (size_t i = 0; i < md5->joint_count; i++) {
      if (strcmp(md5->joints[i].name, name) == 0) {
        index = (long)i;
        break;
      }
    }
    bool owned = false;
    size_t count = 0;
    const qa_skeleton_joint_pose *poses = qa_sample_md5_pose(md5, end, start, 1 - fraction, &owned, &count);
    if (index < 0 || (size_t)index >= count) {
      if (owned) free((void *)poses);
      return false;
    }
    qa_joint_attachment_pose tag = qa_joint_attachment_tag(name, poses[index]);
    if (owned) free((void *)poses);
    out->origin = tag.tag.origin;
    out->axes = tag.tag.axis;
    return true;
  }
  return false;
}
