/*
 * Port of src/formats/q3-model/md5.ts. Mesh/animation records use the
 * contracts/scene.h Md5 shapes where they match. The optional scale
 * JSON is parsed by a minimal object parser covering the TS-observed
 * shape (object of objects with number/boolean values); anything else
 * takes the TS diagnostic paths. Decoded models borrow the mesh file
 * and animation, as the TS shares them. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q3_MODEL_MD5_H
#define QA_FORMATS_Q3_MODEL_MD5_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/scene.h"

typedef struct qa_md5_string_list {
  char **values; /* owned */
  size_t count;
} qa_md5_string_list;

typedef struct qa_md5_mesh_file {
  char *source;        /* owned */
  char *command_line;  /* owned */
  qa_md5_joint *joints;
  size_t joint_count;
  qa_skeleton_joint_pose *bind_pose;
  size_t bind_pose_count;
  qa_md5_mesh *meshes;
  size_t mesh_count;
} qa_md5_mesh_file;

typedef struct qa_md5_hierarchy_joint {
  qa_md5_joint joint;
  double flags;
  double start_index;
} qa_md5_hierarchy_joint;

typedef struct qa_md5_animation_frame {
  qa_bounds bounds;
  double *components;
  size_t component_count;
  qa_skeleton_joint_pose *local_joints;
  size_t local_joint_count;
  qa_skeleton_joint_pose *joints;
  size_t joint_count;
} qa_md5_animation_frame;

typedef struct qa_md5_animation {
  char *source;       /* owned */
  char *command_line; /* owned */
  double frame_rate;
  qa_md5_hierarchy_joint *hierarchy;
  size_t hierarchy_count;
  qa_skeleton_joint_pose *base_frame;
  size_t base_frame_count;
  qa_md5_animation_frame *frames;
  size_t frame_count;
  char *scale_source; /* owned, NULL is TS null */
  qa_md5_string_list diagnostics;
} qa_md5_animation;

typedef struct qa_md5_scale_source {
  const char *source;
  const char *text;
} qa_md5_scale_source;

typedef struct qa_decoded_md5_model {
  qa_md5_model model;
  const qa_md5_mesh_file *mesh_file;   /* borrowed */
  const qa_md5_animation *animation;   /* borrowed */
  qa_md5_model_frame *owned_frames;    /* view rows over animation */
  qa_md5_joint *owned_joints;          /* view over hierarchy */
} qa_decoded_md5_model;

/* `source` NULL means "<md5mesh>" / "<md5anim>". */
void qa_parse_md5_mesh(const char *text, const char *source, qa_md5_mesh_file *out);
void qa_md5_mesh_file_destroy(qa_md5_mesh_file *file);
void qa_parse_md5_anim(const char *text, const char *source, const qa_md5_scale_source *scale_source,
                       qa_md5_animation *out);
void qa_md5_animation_destroy(qa_md5_animation *animation);
/* skin_selection NULL is the TS default {kind:"mesh-shaders"}. */
void qa_create_md5_model(const qa_md5_mesh_file *mesh, const qa_md5_animation *animation,
                         const qa_md5_skin_selection *skin_selection, qa_decoded_md5_model *out);
void qa_decoded_md5_model_destroy(qa_decoded_md5_model *model);
/* Returned poses are borrowed (*out_owned false) or malloc'd. */
const qa_skeleton_joint_pose *qa_sample_md5_pose(const qa_md5_model *model, double frame,
                                                 double previous_frame, double back_lerp, bool *out_owned,
                                                 size_t *out_count);
/* Malloc'd skinned vertices; caller frees. */
qa_model_vertex *qa_skin_md5_mesh(const qa_md5_mesh *mesh, const qa_skeleton_joint_pose *joints,
                                  size_t joint_count, size_t *out_count);

#endif
