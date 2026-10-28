/* Port of src/content/q3/presentation/scene.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/scene.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/render/scene/view.h"
const char *qa_q3_pres_scene_admission_origin_name(qa_q3_pres_scene_admission_origin o) {
  return o == QA_Q3_PRES_SCENE_ORIGIN_MIXED ? "mixed" : "native";
}
bool qa_q3_pres_scene_admission_id_equals(qa_q3_pres_scene_admission_id a, qa_q3_pres_scene_admission_id b) {
  return a.token == b.token;
}
void qa_q3_pres_admitted_poly_destroy(qa_q3_pres_admitted_poly *poly) {
  if (!poly) return;
  qa_q3_pres_ref_poly_destroy(&poly->poly);
  poly->fog = NULL;
}
void qa_q3_pres_scene_admission_destroy(qa_q3_pres_scene_admission *a) {
  if (!a) return;
  for (size_t i = 0; i < a->polygon_count; i++) qa_q3_pres_admitted_poly_destroy(&a->polygons[i]);
  free(a->polygons);
  free(a->entities);
  free(a->token_storage);
  memset(a, 0, sizeof *a);
}
bool qa_q3_pres_snapshot_scene_admission(qa_q3_pres_scene_admission_origin origin,
    const qa_q3_pres_admitted_ref_entity *entities, size_t entity_count,
    const qa_q3_pres_admitted_poly *polygons, size_t polygon_count, qa_q3_pres_scene_admission *out) {
  memset(out, 0, sizeof *out);
  out->token_storage = malloc(1);
  if (!out->token_storage) return false;
  out->id = (qa_q3_pres_scene_admission_id){origin, out->token_storage};
  if (entity_count) {
    out->entities = malloc(entity_count * sizeof *out->entities);
    if (!out->entities) goto oom;
    for (size_t i = 0; i < entity_count; i++)
      out->entities[i] = qa_q3_pres_copy_admitted_ref_entity(&entities[i]);
    out->entity_count = entity_count;
  }
  if (polygon_count) {
    out->polygons = calloc(polygon_count, sizeof *out->polygons);
    if (!out->polygons) goto oom;
    out->polygon_count = polygon_count;
    for (size_t i = 0; i < polygon_count; i++) {
      out->polygons[i].fog = polygons[i].fog;
      if (!qa_q3_pres_copy_ref_poly(&polygons[i].poly, &out->polygons[i].poly)) goto oom;
    }
  }
  return true;
oom:
  qa_q3_pres_scene_admission_destroy(out);
  return false;
}
bool qa_q3_pres_admit_poly(const qa_q3_pres_ref_poly *poly, const qa_q3_pres_fog_selection *fogs,
                           size_t fog_count, qa_q3_pres_admitted_poly *out) {
  memset(out, 0, sizeof *out);
  if (!qa_q3_pres_copy_ref_poly(poly, &out->poly)) return false;
  if (fog_count == 0) return true;
  if (out->poly.vertex_count == 0) qa_range_error("Source polygon fog requires its first admitted vertex");
  qa_vec3 mn = out->poly.vertices[0].position, mx = mn;
  for (size_t i = 0; i < out->poly.vertex_count; i++) {
    qa_vec3 p = out->poly.vertices[i].position;
    if (p.x < mn.x) mn.x = p.x;
    if (p.y < mn.y) mn.y = p.y;
    if (p.z < mn.z) mn.z = p.z;
    if (p.x > mx.x) mx.x = p.x;
    if (p.y > mx.y) mx.y = p.y;
    if (p.z > mx.z) mx.z = p.z;
  }
  for (size_t i = 0; i < fog_count; i++) {
    qa_bounds b = fogs[i].bounds;
    if (mx.x >= b.min.x && mx.y >= b.min.y && mx.z >= b.min.z && mn.x <= b.max.x && mn.y <= b.max.y && mn.z <= b.max.z) {
      out->fog = &fogs[i];
      break;
    }
  }
  return true;
}
const qa_q3_pres_fog_selection *qa_q3_pres_procedural_fog(qa_vec3 origin, double radius,
    const qa_q3_pres_fog_selection *fogs, size_t fog_count) {
  for (size_t i = 0; i < fog_count; i++) {
    qa_bounds b = fogs[i].bounds;
    if (origin.x - radius < b.max.x && origin.x + radius > b.min.x && origin.y - radius < b.max.y &&
        origin.y + radius > b.min.y && origin.z - radius < b.max.z && origin.z + radius > b.min.z)
      return &fogs[i];
  }
  return NULL;
}
bool qa_q3_pres_prepare_model(const qa_q3_pres_ref_model_entity *entity, const qa_actor_id *actor,
                              double entity_index, qa_q3_pres_presented_model *out) {
  memset(out, 0, sizeof *out);
  if (entity->model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) return false;
  out->entity_index = entity_index;
  out->source = entity;
  out->custom_shader = entity->shading.custom_shader ? entity->shading.custom_shader->name : NULL;
  if (entity->custom_skin) {
    out->custom_skin = entity->custom_skin->surfaces;
    out->custom_skin_count = entity->custom_skin->surface_count;
  }
  out->non_normalized_axes = entity->non_normalized_axes;
  qa_scene_entity *e = &out->entity;
  if (actor) {
    out->actor_storage = *actor;
    e->actor = &out->actor_storage;
  }
  e->resource = entity->model.resource;
  e->model = entity->model.model;
  e->transform = (qa_model_transform){entity->origin, entity->axis, {1, 1, 1}};
  e->previous_origin = entity->old_origin;
  e->pose.is_frame = true;
  e->pose.frame = entity->frame;
  e->pose.previous_frame = entity->old_frame;
  e->pose.back_lerp = entity->back_lerp;
  e->skin = entity->skin_num;
  e->color = (qa_vec4){entity->shading.shader_rgba.x / 255, entity->shading.shader_rgba.y / 255,
                       entity->shading.shader_rgba.z / 255, entity->shading.shader_rgba.w / 255};
  e->shader_time.kind = QA_SOURCE_TIME_SECONDS;
  e->shader_time.value = entity->shading.shader_time;
  e->flags_kind = "q3";
  e->flags_bits = entity->shading.render_flags;
  e->lighting_origin = entity->lighting_origin;
  e->shadow_plane = entity->shadow_plane;
  return true;
}
void qa_q3_pres_scene_content_destroy(qa_q3_pres_scene_content *c) {
  if (!c) return;
  qa_q3_pres_scene_admission_destroy(&c->admission);
  free(c->models); free(c->effects); free(c->special_entities); free(c->portals); free(c->lights);
  memset(c, 0, sizeof *c);
}
void qa_q3_pres_presented_scene_destroy(qa_q3_pres_presented_scene *s) {
  if (!s) return;
  qa_q3_pres_scene_content_destroy(&s->content);
  memset(s, 0, sizeof *s);
}
struct qa_q3_pres_scene_recorder {
  qa_q3_pres_scene_target target;
  qa_q3_pres_admitted_ref_entity *entities; size_t entity_count, entity_cap;
  qa_q3_pres_admitted_poly *polygons; size_t polygon_count, polygon_cap;
  qa_scene_light *lights; size_t light_count, light_cap;
};
static bool grow(void **arr, size_t *cap, size_t need, size_t sz) {
  if (*cap >= need) return true;
  size_t nc = *cap ? *cap * 2 : 8;
  while (nc < need) nc *= 2;
  void *np = realloc(*arr, nc * sz);
  if (!np) return false;
  *arr = np;
  *cap = nc;
  return true;
}
static noreturn void oom_abort(void) { qa_range_error("Q3 scene recorder allocation failed"); }
qa_q3_pres_scene_recorder *qa_q3_pres_scene_recorder_create(qa_q3_pres_scene_target target) {
  qa_q3_pres_scene_recorder *r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->target = target;
  return r;
}
void qa_q3_pres_scene_recorder_destroy(qa_q3_pres_scene_recorder *r) {
  if (!r) return;
  qa_q3_pres_scene_recorder_clear_scene(r);
  free(r->entities); free(r->polygons); free(r->lights); free(r);
}
qa_q3_pres_scene_target qa_q3_pres_scene_recorder_target(qa_q3_pres_scene_recorder *r) { return r->target; }
void qa_q3_pres_scene_recorder_clear_scene(qa_q3_pres_scene_recorder *r) {
  for (size_t i = 0; i < r->polygon_count; i++) qa_q3_pres_admitted_poly_destroy(&r->polygons[i]);
  r->entity_count = 0;
  r->polygon_count = 0;
  r->light_count = 0;
}
void qa_q3_pres_scene_recorder_add_ref_entity(qa_q3_pres_scene_recorder *r,
                                              const qa_q3_pres_admitted_ref_entity *entity) {
  if (!grow((void **)&r->entities, &r->entity_cap, r->entity_count + 1, sizeof *r->entities)) oom_abort();
  r->entities[r->entity_count++] = qa_q3_pres_copy_admitted_ref_entity(entity);
}
void qa_q3_pres_scene_recorder_add_poly(qa_q3_pres_scene_recorder *r, const qa_q3_pres_ref_poly *poly) {
  if (poly->shader == NULL) {
    r->target.print(r->target.ctx, "^3WARNING: RE_AddPolyToScene: NULL poly shader\n");
    return;
  }
  size_t fog_count = 0;
  const qa_q3_pres_fog_selection *fogs = r->target.fog_selections(r->target.ctx, &fog_count);
  if (!grow((void **)&r->polygons, &r->polygon_cap, r->polygon_count + 1, sizeof *r->polygons)) oom_abort();
  if (!qa_q3_pres_admit_poly(poly, fogs, fog_count, &r->polygons[r->polygon_count])) oom_abort();
  r->polygon_count++;
}
void qa_q3_pres_scene_recorder_add_light(qa_q3_pres_scene_recorder *r, qa_q3_pres_dynamic_light light) {
  if (!grow((void **)&r->lights, &r->light_cap, r->light_count + 1, sizeof *r->lights)) oom_abort();
  qa_scene_light *entry = &r->lights[r->light_count++];
  memset(entry, 0, sizeof *entry);
  entry->origin = light.origin;
  entry->color = light.color;
  entry->radius = light.radius;
  entry->additive = light.additive;
  entry->profile.kind = "q3";
}
bool qa_q3_pres_scene_recorder_capture(qa_q3_pres_scene_recorder *r, qa_q3_pres_scene_content *out) {
  memset(out, 0, sizeof *out);
  if (!qa_q3_pres_snapshot_scene_admission(QA_Q3_PRES_SCENE_ORIGIN_NATIVE, r->entities, r->entity_count,
                                           r->polygons, r->polygon_count, &out->admission))
    return false;
  size_t mcap = 0, ecap = 0, scap = 0, pcap = 0;
  for (size_t i = 0; i < out->admission.entity_count; i++) {
    const qa_q3_pres_admitted_ref_entity *entity = &out->admission.entities[i];
    if (entity->is_poly_record) continue;
    qa_q3_pres_ref_entity_kind kind = entity->entity.kind;
    if (kind == QA_Q3_PRES_REF_PORTAL) {
      if (!grow((void **)&out->portals, &pcap, out->portal_count + 1, sizeof *out->portals)) goto oom;
      out->portals[out->portal_count++] = (qa_q3_pres_presented_portal){(double)i, entity};
      continue;
    }
    if (kind == QA_Q3_PRES_REF_BEAM) {
      if (!grow((void **)&out->special_entities, &scap, out->special_count + 1, sizeof *out->special_entities)) goto oom;
      out->special_entities[out->special_count++] = (qa_q3_pres_presented_special){(double)i, entity};
      continue;
    }
    if (kind == QA_Q3_PRES_REF_MODEL) {
      if (entity->entity.as.model.model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
        if (!grow((void **)&out->special_entities, &scap, out->special_count + 1, sizeof *out->special_entities)) goto oom;
        out->special_entities[out->special_count++] = (qa_q3_pres_presented_special){(double)i, entity};
        continue;
      }
      qa_q3_pres_presented_model prepared;
      const qa_actor_id *actor = r->target.actor(r->target.ctx, &entity->entity.as.model);
      if (qa_q3_pres_prepare_model(&entity->entity.as.model, actor, (double)i, &prepared)) {
        if (!grow((void **)&out->models, &mcap, out->model_count + 1, sizeof *out->models)) goto oom;
        out->models[out->model_count] = prepared;
        /* Fix self-pointers after the move: actor and source. */
        qa_q3_pres_presented_model *stored = &out->models[out->model_count];
        if (stored->entity.actor) stored->entity.actor = &stored->actor_storage;
        stored->source = &out->admission.entities[i].entity.as.model;
        out->model_count++;
      }
      continue;
    }
    if (!grow((void **)&out->effects, &ecap, out->effect_count + 1, sizeof *out->effects)) goto oom;
    const qa_q3_pres_scene_shader *shader = NULL;
    if (kind == QA_Q3_PRES_REF_SPRITE) shader = entity->entity.as.sprite.shading.custom_shader;
    else if (kind == QA_Q3_PRES_REF_RAIL_CORE || kind == QA_Q3_PRES_REF_RAIL_RINGS || kind == QA_Q3_PRES_REF_LIGHTNING)
      shader = entity->entity.as.rail.shading.custom_shader;
    out->effects[out->effect_count++] = (qa_q3_pres_presented_geometry){
        {QA_Q3_PRES_GEOMETRY_REFENTITY, (double)i}, shader, entity, NULL};
  }
  for (size_t i = 0; i < out->admission.polygon_count; i++) {
    if (!grow((void **)&out->effects, &ecap, out->effect_count + 1, sizeof *out->effects)) goto oom;
    out->effects[out->effect_count++] = (qa_q3_pres_presented_geometry){
        {QA_Q3_PRES_GEOMETRY_POLYGON, (double)i}, out->admission.polygons[i].poly.shader, NULL,
        &out->admission.polygons[i]};
  }
  if (r->light_count) {
    out->lights = malloc(r->light_count * sizeof *out->lights);
    if (!out->lights) goto oom;
    memcpy(out->lights, r->lights, r->light_count * sizeof *out->lights);
    out->light_count = r->light_count;
  }
  return true;
oom:
  qa_q3_pres_scene_content_destroy(out);
  return false;
}
void qa_q3_pres_scene_recorder_render_scene(qa_q3_pres_scene_recorder *r, const qa_q3_pres_refdef *input) {
  qa_q3_pres_presented_scene scene;
  memset(&scene, 0, sizeof scene);
  scene.source = qa_q3_pres_copy_refdef(input);
  if (!qa_q3_pres_scene_recorder_capture(r, &scene.content)) oom_abort();
  scene.seat = r->target.seat;
  scene.viewport = (qa_rect){r->target.viewport.x + scene.source.x, r->target.viewport.y + scene.source.y,
                             scene.source.width, scene.source.height};
  memset(&scene.camera, 0, sizeof scene.camera);
  scene.camera.viewport = scene.viewport;
  scene.camera.origin = scene.source.view_origin;
  scene.camera.axis = scene.source.view_axis;
  scene.camera.projection = qa_scene_perspective_projection(scene.source.fov_x, scene.source.fov_y,
                                                            r->target.far_clip, r->target.near_clip);
  scene.camera.clip_is_portal = false;
  r->target.publish(r->target.ctx, &scene);
}
static void view_clear(void *ctx) { qa_q3_pres_scene_recorder_clear_scene(ctx); }
static void view_add_entity(void *ctx, const qa_q3_pres_admitted_ref_entity *e) {
  qa_q3_pres_scene_recorder_add_ref_entity(ctx, e);
}
static void view_add_poly(void *ctx, const qa_q3_pres_ref_poly *p) { qa_q3_pres_scene_recorder_add_poly(ctx, p); }
static void view_add_light(void *ctx, qa_q3_pres_dynamic_light l) { qa_q3_pres_scene_recorder_add_light(ctx, l); }
static void view_render(void *ctx, const qa_q3_pres_refdef *rd) { qa_q3_pres_scene_recorder_render_scene(ctx, rd); }
qa_q3_pres_scene_recorder_view qa_q3_pres_scene_recorder_as_view(qa_q3_pres_scene_recorder *r) {
  qa_q3_pres_scene_recorder_view view = {r, view_clear, view_add_entity, view_add_poly, view_add_light, view_render};
  return view;
}
