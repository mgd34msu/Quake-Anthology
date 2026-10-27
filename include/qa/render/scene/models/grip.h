/*
 * Port of src/render/scene/models/grip.ts. The TS returned closure
 * becomes a retained qa_model_grip evaluated with
 * qa_model_grip_transform; the copied MD2 frame rows it captures are
 * owned by the grip. GPL-2.0-or-later.
 */
#ifndef QA_RENDER_SCENE_MODELS_GRIP_H
#define QA_RENDER_SCENE_MODELS_GRIP_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/model_attachment.h"
#include "qa/contracts/scene.h"

typedef struct qa_model_grip {
  bool is_joint;
  qa_resource_id reference_id; /* borrowed */
  /* joint */
  const char *name; /* borrowed */
  qa_model_transform grip;
  /* mesh: owned reduced model copy */
  qa_q2_alias_model model;
  qa_q2_alias_frame *owned_frames;
  qa_model_vertex *owned_vertices;        /* 3 per frame */
  qa_packed_alias_vertex *owned_packed;   /* 3 per frame */
  qa_model_transform relative;
} qa_model_grip;

/* Aborts with the exact TS Error messages on mismatch/degeneracy.
 * False only on OOM. */
bool qa_create_model_grip(const qa_scene_entity *reference,
                          qa_model_attachment_definition definition,
                          qa_model_grip *out);
void qa_model_grip_destroy(qa_model_grip *grip);
/* The TS closure. False is TS null; mismatches abort as in the TS. */
bool qa_model_grip_transform(const qa_model_grip *grip,
                             const qa_scene_entity *entity,
                             qa_model_transform *out);

#endif
