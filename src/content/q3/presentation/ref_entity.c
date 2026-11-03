/* Port of src/content/q3/presentation/ref-entity.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/ref_entity.h"
#include <math.h>
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
enum { SCENE_COORDINATE_LIMIT = 1000000000 };
static bool is_finite_number(double value) { return isfinite(value); }
static bool scene_coordinate(double value) { return isfinite(value) && fabs(value) <= SCENE_COORDINATE_LIMIT; }
static bool scene_vector(qa_vec3 v) { return scene_coordinate(v.x) && scene_coordinate(v.y) && scene_coordinate(v.z); }
static bool scene_axis(const qa_axis *axis) {
  return scene_vector(axis->vectors[0]) && scene_vector(axis->vectors[1]) && scene_vector(axis->vectors[2]);
}
static bool shading_admissible(const qa_q3_pres_shading *s) {
  return is_finite_number(s->shader_time) && is_finite_number(s->shader_tex_coord.x) && is_finite_number(s->shader_tex_coord.y);
}
/* Field presence follows the TS `"field" in entity` tests per kind; checks run in the TS order. */
bool qa_q3_pres_ref_entity_is_admissible(const qa_q3_pres_admitted_ref_entity *admitted) {
  if (admitted->is_poly_record) {
    const qa_q3_pres_source_ref_entity_record *r = &admitted->record;
    if (!scene_vector(r->origin) || !scene_vector(r->old_origin)) return false;
    if (!scene_axis(&r->axis)) return false;
    if (!scene_vector(r->lighting_origin)) return false;
    if (!is_finite_number(r->radius) || !is_finite_number(r->rotation) || !is_finite_number(r->shadow_plane) || !is_finite_number(r->back_lerp)) return false;
    return is_finite_number(r->shader_time) && is_finite_number(r->shader_tex_coord.x) && is_finite_number(r->shader_tex_coord.y);
  }
  const qa_q3_pres_ref_entity *e = &admitted->entity;
  switch (e->kind) {
    case QA_Q3_PRES_REF_MODEL: {
      const qa_q3_pres_ref_model_entity *m = &e->as.model;
      if (!scene_vector(m->origin) || !scene_vector(m->old_origin)) return false;
      if (!scene_axis(&m->axis) || !scene_vector(m->lighting_origin)) return false;
      if (!is_finite_number(m->shadow_plane) || !is_finite_number(m->back_lerp)) return false;
      return shading_admissible(&m->shading);
    }
    case QA_Q3_PRES_REF_SPRITE: {
      const qa_q3_pres_ref_sprite_entity *s = &e->as.sprite;
      if (!scene_vector(s->origin)) return false;
      if (!is_finite_number(s->radius) || !is_finite_number(s->rotation)) return false;
      return shading_admissible(&s->shading);
    }
    case QA_Q3_PRES_REF_BEAM: {
      const qa_q3_pres_ref_beam_entity *b = &e->as.beam;
      if (!scene_vector(b->origin) || !scene_vector(b->old_origin)) return false;
      if (!scene_axis(&b->axis) || !is_finite_number(b->radius)) return false;
      return shading_admissible(&b->shading);
    }
    case QA_Q3_PRES_REF_RAIL_CORE: case QA_Q3_PRES_REF_RAIL_RINGS: case QA_Q3_PRES_REF_LIGHTNING: {
      const qa_q3_pres_ref_rail_entity *r = &e->as.rail;
      if (!scene_vector(r->origin) || !scene_vector(r->old_origin) || !is_finite_number(r->radius)) return false;
      return shading_admissible(&r->shading);
    }
    case QA_Q3_PRES_REF_PORTAL: {
      const qa_q3_pres_ref_portal_entity *p = &e->as.portal;
      return scene_vector(p->origin) && scene_vector(p->old_origin) && scene_axis(&p->axis);
    }
    default: return true;
  }
}
bool qa_q3_pres_ref_poly_is_admissible(const qa_q3_pres_ref_poly *poly) {
  for (size_t i = 0; i < poly->vertex_count; i++) {
    const qa_q3_pres_ref_poly_vertex *v = &poly->vertices[i];
    if (!scene_vector(v->position) || !is_finite_number(v->tex_coord.x) || !is_finite_number(v->tex_coord.y)) return false;
  }
  return true;
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
