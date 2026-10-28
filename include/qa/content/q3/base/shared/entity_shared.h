/* Port of src/content/q3/base/shared/entity-shared.ts. GPL-2.0-or-later.
 * Body fields forward to the landed contracts qa_body_state/qa_linked_body
 * through an EntityBodyBinding fn table (linked() false is TS null). */
#ifndef QA_CONTENT_Q3_BASE_SHARED_ENTITY_SHARED_H
#define QA_CONTENT_Q3_BASE_SHARED_ENTITY_SHARED_H
#include <stdbool.h>
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/contracts/math.h"
#include "qa/contracts/world.h"

/* TS EntityCollisionModel: "inline" carries index; "box"|"capsule" do not. */
typedef enum qa_q3_base_entity_collision_model_kind {
  QA_Q3_BASE_MODEL_INLINE, QA_Q3_BASE_MODEL_BOX, QA_Q3_BASE_MODEL_CAPSULE
} qa_q3_base_entity_collision_model_kind;
typedef struct qa_q3_base_entity_collision_model {
  qa_q3_base_entity_collision_model_kind kind; double index;
} qa_q3_base_entity_collision_model;

enum {
  QA_Q3_BASE_SVF_NOCLIENT = 1, QA_Q3_BASE_SVF_CLIENTMASK = 2, QA_Q3_BASE_SVF_BOT = 8,
  QA_Q3_BASE_SVF_BROADCAST = 32, QA_Q3_BASE_SVF_PORTAL = 64, QA_Q3_BASE_SVF_USE_CURRENT_ORIGIN = 128,
  QA_Q3_BASE_SVF_SINGLECLIENT = 256, QA_Q3_BASE_SVF_NOSERVERINFO = 512, QA_Q3_BASE_SVF_NOTSINGLECLIENT = 2048
};

typedef struct qa_q3_base_entity_body_binding {
  void *ctx;
  qa_body_state (*read)(void *ctx);
  void (*write)(void *ctx, qa_body_state value);
  bool (*linked)(void *ctx, qa_linked_body *out); /* false = TS null */
} qa_q3_base_entity_body_binding;

typedef struct qa_q3_base_entity_shared {
  double sv_flags, single_client;
  qa_q3_base_entity_collision_model model;
  double contents, owner_num;
  qa_q3_base_entity_body_binding body;
  bool has_previous_link; qa_linked_body previous_link;
  bool has_current_origin_view; qa_vec3 current_origin_view;
  bool has_abs_min_override; qa_vec3 abs_min_override;
  bool has_abs_max_override; qa_vec3 abs_max_override;
} qa_q3_base_entity_shared;

void qa_q3_base_entity_shared_init(qa_q3_base_entity_shared *r, qa_q3_base_entity_body_binding body);
bool qa_q3_base_entity_shared_linked(const qa_q3_base_entity_shared *r);
void qa_q3_base_entity_shared_capture_link(qa_q3_base_entity_shared *r);
double qa_q3_base_entity_shared_linkcount(const qa_q3_base_entity_shared *r);
qa_vec3 qa_q3_base_entity_shared_mins(const qa_q3_base_entity_shared *r);
void qa_q3_base_entity_shared_set_mins(qa_q3_base_entity_shared *r, qa_vec3 value);
qa_vec3 qa_q3_base_entity_shared_maxs(const qa_q3_base_entity_shared *r);
void qa_q3_base_entity_shared_set_maxs(qa_q3_base_entity_shared *r, qa_vec3 value);
qa_vec3 qa_q3_base_entity_shared_current_origin(const qa_q3_base_entity_shared *r);
void qa_q3_base_entity_shared_set_current_origin(qa_q3_base_entity_shared *r, qa_vec3 value);
void qa_q3_base_entity_shared_with_current_origin(qa_q3_base_entity_shared *r, qa_vec3 origin,
    void *call_ctx, void (*call)(void *ctx));
qa_vec3 qa_q3_base_entity_shared_current_angles(const qa_q3_base_entity_shared *r);
void qa_q3_base_entity_shared_set_current_angles(qa_q3_base_entity_shared *r, qa_vec3 value);
/* TS capturePrivateState/restorePrivateState state shape. */
typedef struct qa_q3_base_entity_shared_private_state {
  bool has_previous_link; qa_linked_body previous_link;
  bool has_abs_min_override; qa_vec3 abs_min_override;
  bool has_abs_max_override; qa_vec3 abs_max_override;
} qa_q3_base_entity_shared_private_state;
qa_q3_base_entity_shared_private_state qa_q3_base_entity_shared_capture_private_state(const qa_q3_base_entity_shared *r);
void qa_q3_base_entity_shared_restore_private_state(qa_q3_base_entity_shared *r,
    qa_q3_base_entity_shared_private_state state);
void qa_q3_base_entity_shared_clear_bounds_overrides(qa_q3_base_entity_shared *r);
void qa_q3_base_entity_shared_set_absmin(qa_q3_base_entity_shared *r, qa_vec3 value);
void qa_q3_base_entity_shared_set_absmax(qa_q3_base_entity_shared *r, qa_vec3 value);
qa_vec3 qa_q3_base_entity_shared_absmin(const qa_q3_base_entity_shared *r);
qa_vec3 qa_q3_base_entity_shared_absmax(const qa_q3_base_entity_shared *r);

/* TS SharedEntity. s is the landed EntityState record (number/solid/
 * modelindex mutable, other fields read-only by convention). */
typedef struct qa_q3_base_shared_entity {
  qa_q3_entity_state_record *s;
  qa_q3_base_entity_shared *r;
} qa_q3_base_shared_entity;
#endif
