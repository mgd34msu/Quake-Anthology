/* Port of src/app/bootstrap/q3-client/view.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client/view.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static double fr(double v) { return (double)(float)v; }
static qa_vec3 offset(qa_vec3 value, qa_vec3 delta) {
  qa_vec3 out = {value.x + delta.x, value.y + delta.y, value.z + delta.z};
  return out;
}
qa_scene_camera qa_app_q3_weapon_camera(qa_scene_camera camera, bool split_screen) {
  double aspect = camera.viewport.width / camera.viewport.height;
  if (!split_screen || aspect <= 4.0 / 3.0 || camera.clip_is_portal) return camera;
  double scale = (4.0 / 3.0) / aspect;
  camera.projection.values[0] = fr(camera.projection.values[0] * scale);
  camera.projection.values[5] = fr(camera.projection.values[5] * scale);
  return camera;
}
static qa_scene_entity *copy_entity(const qa_scene_entity *entity, const qa_vec3 *delta) {
  qa_scene_entity *out = malloc(sizeof *out);
  if (out == NULL) qa_range_error("Out of memory");
  *out = *entity;
  /* Attachment entities are copied into owned storage. */
  qa_scene_entity_attachment *attachments = NULL;
  if (entity->attachment_count > 0) {
    attachments = malloc(entity->attachment_count * sizeof *attachments);
    if (attachments == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < entity->attachment_count; i++) {
      attachments[i].tag = entity->attachments[i].tag;
      attachments[i].entity = copy_entity(entity->attachments[i].entity, delta);
    }
  }
  out->attachments = attachments;
  if (delta != NULL) {
    out->transform.origin = offset(entity->transform.origin, *delta);
    out->previous_origin = offset(entity->previous_origin, *delta);
    out->lighting_origin = offset(entity->lighting_origin, *delta);
    out->shadow_plane = entity->shadow_plane + delta->z;
  }
  return out;
}
qa_scene_entity *qa_app_offset_q3_view_entity(const qa_scene_entity *entity, const qa_vec3 *delta) {
  return copy_entity(entity, delta);
}
void qa_app_offset_q3_view_entity_free(qa_scene_entity *entity) {
  if (entity == NULL) return;
  for (size_t i = 0; i < entity->attachment_count; i++)
    qa_app_offset_q3_view_entity_free((qa_scene_entity *)entity->attachments[i].entity);
  free((void *)entity->attachments);
  free(entity);
}
qa_q3_pres_admitted_ref_entity qa_app_offset_q3_view_reference(
    qa_q3_pres_admitted_ref_entity entity, const qa_vec3 *delta) {
  if (delta == NULL || entity.is_poly_record) return entity;
  if (entity.entity.kind == QA_Q3_PRES_REF_PORTAL) return entity;
  switch (entity.entity.kind) {
    case QA_Q3_PRES_REF_MODEL:
      entity.entity.as.model.origin = offset(entity.entity.as.model.origin, *delta);
      entity.entity.as.model.old_origin = offset(entity.entity.as.model.old_origin, *delta);
      entity.entity.as.model.lighting_origin = offset(entity.entity.as.model.lighting_origin, *delta);
      entity.entity.as.model.shadow_plane += delta->z;
      return entity;
    case QA_Q3_PRES_REF_SPRITE:
      entity.entity.as.sprite.origin = offset(entity.entity.as.sprite.origin, *delta);
      return entity;
    case QA_Q3_PRES_REF_BEAM:
      entity.entity.as.beam.origin = offset(entity.entity.as.beam.origin, *delta);
      entity.entity.as.beam.old_origin = offset(entity.entity.as.beam.old_origin, *delta);
      return entity;
    default: /* rail-core / rail-rings / lightning */
      entity.entity.as.rail.origin = offset(entity.entity.as.rail.origin, *delta);
      entity.entity.as.rail.old_origin = offset(entity.entity.as.rail.old_origin, *delta);
      return entity;
  }
}
