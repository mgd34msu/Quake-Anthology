/*
 * Port of src/contracts/mod-client-outputs.ts (types only).
 * ModClientOutputDeclaration<Scalar,Vector> is generic over the
 * source's field references; the C form holds them as opaque pointers
 * whose concrete type is the instantiating module's field type
 * (documented at each use). GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_MOD_CLIENT_OUTPUTS_H
#define QA_CONTRACTS_MOD_CLIENT_OUTPUTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"

/* TS ModClientMovementMode = "normal" | "noclip" | "freeze". */
typedef enum qa_mod_client_movement_mode {
  QA_MOD_CLIENT_MOVEMENT_NORMAL, QA_MOD_CLIENT_MOVEMENT_NOCLIP, QA_MOD_CLIENT_MOVEMENT_FREEZE
} qa_mod_client_movement_mode;

/* TS ModClientOutput union on kind (ModClientOutputChannel is its
 * kind set): "view-offset"|"movement-mode"|"stance"|"body-shape". */
typedef enum qa_mod_client_output_kind {
  QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET, QA_MOD_CLIENT_OUTPUT_MOVEMENT_MODE,
  QA_MOD_CLIENT_OUTPUT_STANCE, QA_MOD_CLIENT_OUTPUT_BODY_SHAPE
} qa_mod_client_output_kind;
typedef qa_mod_client_output_kind qa_mod_client_output_channel;
typedef struct qa_mod_client_output {
  qa_mod_client_output_kind kind;
  qa_vec3 view_offset;
  qa_mod_client_movement_mode mode;
  bool stance;
  qa_bounds body_shape;
} qa_mod_client_output;

typedef struct qa_mod_client_mode_value { double value; qa_mod_client_movement_mode mode; } qa_mod_client_mode_value;
typedef struct qa_mod_client_stance_value { double value; bool crouched; } qa_mod_client_stance_value;

/* Generic declaration; scalar/vector fields are opaque per source. */
typedef struct qa_mod_client_output_declaration {
  qa_mod_client_output_kind kind;
  const void *min_vector;  /* body-shape */
  const void *max_vector;  /* body-shape */
  const void *field;       /* view-offset/movement-mode/stance */
  const void *height;      /* view-offset (scalar form) */
  bool has_mask;
  double mask;
  const qa_mod_client_mode_value *mode_values; size_t mode_value_count;
  const qa_mod_client_stance_value *stance_values; size_t stance_value_count;
} qa_mod_client_output_declaration;

typedef struct qa_mod_client_movement_outputs {
  const qa_vec3 *view_offset;              /* optional */
  bool has_mode;
  qa_mod_client_movement_mode mode;
  bool has_stance;
  bool stance;
  const qa_bounds *body_bounds;            /* optional */
} qa_mod_client_movement_outputs;

#endif
