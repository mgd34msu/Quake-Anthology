/*
 * Port of src/formats/q3-model/scene.ts. The scene model borrows the
 * source MD3 storage; destroy frees only the wrapper rows.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q3_MODEL_SCENE_H
#define QA_FORMATS_Q3_MODEL_SCENE_H

#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/formats/q3_model/md3.h"

typedef struct qa_decoded_scene_md3 {
  qa_q3_mesh_model model;
  const qa_md3_model *source_model; /* borrowed */
} qa_decoded_scene_md3;

void qa_to_scene_md3(const qa_md3_model *model, qa_decoded_scene_md3 *out);
void qa_decoded_scene_md3_destroy(qa_decoded_scene_md3 *model);

/* anchor.kind: "md3-tag" (tag) | "skeleton-joint" (joint) | "model-origin". */
typedef struct qa_character_attachment {
  const char *name;
  const char *model_part;
  qa_model_transform transform;
  const char *anchor_kind;
  const char *anchor_tag;
  const char *anchor_joint;
} qa_character_attachment;

typedef struct qa_character_part {
  const char *name;
  const char *model_path;
  const char *skin_path; /* nullable */
} qa_character_part;

typedef struct qa_character_animation_ref {
  const char *action;
  const char *part;
  const char *clip;
} qa_character_animation_ref;

typedef struct qa_character_model_metadata {
  const char *source_family; /* "q1" | "q2" | "q3" */
  const qa_character_part *parts;
  size_t part_count;
  const qa_character_attachment *attachments;
  size_t attachment_count;
  const qa_character_animation_ref *animations;
  size_t animation_count;
  qa_vec3 visual_scale;
} qa_character_model_metadata;

typedef struct qa_joint_attachment_pose {
  qa_model_tag tag;
  double scale;
} qa_joint_attachment_pose;

qa_joint_attachment_pose qa_joint_attachment_tag(const char *name, qa_skeleton_joint_pose pose);

#endif
