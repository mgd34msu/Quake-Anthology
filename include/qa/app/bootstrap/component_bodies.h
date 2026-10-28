/* Port of src/app/bootstrap/component-bodies.ts. RefModelEntity
 * material fields become the value struct below (landed ref-entity
 * owners map onto it). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_COMPONENT_BODIES_H
#define QA_APP_BOOTSTRAP_COMPONENT_BODIES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/presentation.h"
/* TS QvmBodyPart text: "body"|"head"|"legs"|"torso"|... kept as text. */
/* TS BodyMaterial = Pick<RefModelEntity, ...>. custom_shader_name NULL
 * mirrors customShader === null; has_custom_skin + skin text mirror
 * customSkin?.surfaces identity used only for the dedup key. */
typedef struct qa_app_body_material {
  bool has_custom_shader; const char *custom_shader_name;
  bool has_custom_skin; const char *custom_skin_surfaces;
  qa_vec4 shader_rgba;
  qa_vec2 shader_tex_coord;
  double shader_time, render_flags, shadow_plane;
  qa_vec3 lighting_origin;
  bool non_normalized_axes;
} qa_app_body_material;
typedef struct qa_app_component_body_part {
  const char *part; bool base;
  const qa_app_body_material *passes; size_t pass_count;
} qa_app_component_body_part;
typedef struct qa_app_component_body {
  qa_presentation_owner owner;
  qa_actor_id actor;
  qa_content_id content;
  double time;
  const qa_app_component_body_part *parts; size_t part_count;
} qa_app_component_body;
/* TS PreparedPrimaryBody (entity/options stay opaque borrowed rows). */
typedef struct qa_app_prepared_primary_body {
  qa_actor_id actor;
  const char *part;
  qa_content_id content;
  const void *entity, *options;
  bool base;
  qa_content_id shader_content;
  double time;
} qa_app_prepared_primary_body;
/* Borrowed passes collected into caller storage; returns count. */
size_t qa_app_body_materials(const qa_app_component_body *body, const char *part,
                             const qa_app_body_material **out, size_t capacity);
bool qa_app_body_base_visible(const qa_app_component_body *bodies, size_t body_count, qa_actor_id actor, const char *part);
#endif
