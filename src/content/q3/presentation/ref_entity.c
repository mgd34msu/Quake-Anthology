/* Port of src/content/q3/presentation/ref-entity.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/ref_entity.h"
#include <stdlib.h>
#include <string.h>
const qa_q3_pres_scene_model qa_q3_pres_default_model = {QA_Q3_PRES_SCENE_MODEL_DEFAULT, "*default",
                                                         {0}, 0, {0}, {{0, 0, 0}, {0, 0, 0}}, {0}};
const char *qa_q3_pres_ref_entity_kind_name(qa_q3_pres_ref_entity_kind kind) {
  switch (kind) {
    case QA_Q3_PRES_REF_MODEL: return "model";
    case QA_Q3_PRES_REF_SPRITE: return "sprite";
    case QA_Q3_PRES_REF_BEAM: return "beam";
    case QA_Q3_PRES_REF_RAIL_CORE: return "rail-core";
    case QA_Q3_PRES_REF_RAIL_RINGS: return "rail-rings";
    case QA_Q3_PRES_REF_LIGHTNING: return "lightning";
    case QA_Q3_PRES_REF_PORTAL: return "portal-surface";
    case QA_Q3_PRES_REF_POLY: return "poly";
  }
  return "model";
}
static qa_q3_pres_shading shading(void) {
  qa_q3_pres_shading value = {0, NULL, {0, 0, 0, 0}, {0, 0}, 0};
  return value;
}
qa_q3_pres_ref_model_entity qa_q3_pres_create_model_entity(qa_q3_pres_scene_model model) {
  qa_q3_pres_ref_model_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.shading = shading();
  entity.model = model;
  return entity;
}
qa_q3_pres_ref_model_entity qa_q3_pres_create_model_entity_default(void) {
  return qa_q3_pres_create_model_entity(qa_q3_pres_default_model);
}
qa_q3_pres_ref_sprite_entity qa_q3_pres_create_sprite_entity(void) {
  qa_q3_pres_ref_sprite_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.shading = shading();
  return entity;
}
qa_q3_pres_ref_beam_entity qa_q3_pres_create_beam_entity(void) {
  qa_q3_pres_ref_beam_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.shading = shading();
  return entity;
}
static qa_q3_pres_ref_rail_entity create_rail(void) {
  qa_q3_pres_ref_rail_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.shading = shading();
  return entity;
}
qa_q3_pres_ref_rail_entity qa_q3_pres_create_rail_core_entity(void) { return create_rail(); }
qa_q3_pres_ref_rail_entity qa_q3_pres_create_rail_rings_entity(void) { return create_rail(); }
qa_q3_pres_ref_rail_entity qa_q3_pres_create_lightning_entity(void) { return create_rail(); }
qa_q3_pres_ref_portal_entity qa_q3_pres_create_portal_entity(void) {
  qa_q3_pres_ref_portal_entity entity;
  memset(&entity, 0, sizeof entity);
  return entity;
}
qa_q3_pres_ref_entity qa_q3_pres_ref_entity_from_model(qa_q3_pres_ref_model_entity entity) {
  qa_q3_pres_ref_entity tagged;
  memset(&tagged, 0, sizeof tagged);
  tagged.kind = QA_Q3_PRES_REF_MODEL;
  tagged.as.model = entity;
  return tagged;
}
qa_q3_pres_ref_entity qa_q3_pres_ref_entity_from_sprite(qa_q3_pres_ref_sprite_entity entity) {
  qa_q3_pres_ref_entity tagged;
  memset(&tagged, 0, sizeof tagged);
  tagged.kind = QA_Q3_PRES_REF_SPRITE;
  tagged.as.sprite = entity;
  return tagged;
}
qa_q3_pres_ref_entity qa_q3_pres_ref_entity_from_beam(qa_q3_pres_ref_beam_entity entity) {
  qa_q3_pres_ref_entity tagged;
  memset(&tagged, 0, sizeof tagged);
  tagged.kind = QA_Q3_PRES_REF_BEAM;
  tagged.as.beam = entity;
  return tagged;
}
qa_q3_pres_ref_entity qa_q3_pres_ref_entity_from_rail(qa_q3_pres_ref_entity_kind kind, qa_q3_pres_ref_rail_entity entity) {
  qa_q3_pres_ref_entity tagged;
  memset(&tagged, 0, sizeof tagged);
  tagged.kind = kind;
  tagged.as.rail = entity;
  return tagged;
}
qa_q3_pres_ref_entity qa_q3_pres_ref_entity_from_portal(qa_q3_pres_ref_portal_entity entity) {
  qa_q3_pres_ref_entity tagged;
  memset(&tagged, 0, sizeof tagged);
  tagged.kind = QA_Q3_PRES_REF_PORTAL;
  tagged.as.portal = entity;
  return tagged;
}
/* Every nested TS object (Vec3/Vec4/Vec2/Axis rows) is a C value struct, so
 * each TS branch below -- spread plus fresh nested objects -- is exactly a
 * struct assignment; model/shader/skin handles keep identity. */
qa_q3_pres_ref_entity qa_q3_pres_copy_ref_entity(const qa_q3_pres_ref_entity *entity) { return *entity; }
qa_q3_pres_admitted_ref_entity qa_q3_pres_copy_admitted_ref_entity(const qa_q3_pres_admitted_ref_entity *entity) {
  return *entity; /* poly-record and per-kind branches: same value copy */
}
qa_q3_pres_source_ref_entity qa_q3_pres_copy_source_ref_entity(const qa_q3_pres_source_ref_entity *entity) {
  return *entity; /* lightingOrigin/portal/sprite/beam/other branches: same value copy */
}
bool qa_q3_pres_copy_ref_poly(const qa_q3_pres_ref_poly *poly, qa_q3_pres_ref_poly *out) {
  out->shader = poly->shader;
  out->vertex_count = poly->vertex_count;
  out->vertices = NULL;
  if (poly->vertex_count == 0) return true;
  out->vertices = (qa_q3_pres_ref_poly_vertex *)malloc(poly->vertex_count * sizeof *out->vertices);
  if (out->vertices == NULL) return false;
  memcpy(out->vertices, poly->vertices, poly->vertex_count * sizeof *out->vertices);
  return true;
}
void qa_q3_pres_ref_poly_destroy(qa_q3_pres_ref_poly *poly) {
  free(poly->vertices);
  poly->vertices = NULL;
  poly->vertex_count = 0;
}
