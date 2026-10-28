/* Port of src/app/bootstrap/q3-client/scene.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client/scene.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q3_client/view.h"
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"
struct qa_app_application_q3_scene_renderer {
  qa_app_application_q3_assets *media;
  qa_q3_pres_q3_renderer_resources *resources;
  qa_app_q3_scene_renderer_host host;
  bool closed;
};
static void assert_current(const qa_app_application_q3_scene_renderer *r) {
  if (r->closed) qa_range_error("Q3 scene renderer is closed");
}
qa_app_application_q3_scene_renderer *qa_app_application_q3_scene_renderer_create(
    qa_app_application_q3_assets *media, qa_q3_pres_q3_renderer_resources *resources,
    qa_app_q3_scene_renderer_host host) {
  qa_app_application_q3_scene_renderer *r = calloc(1, sizeof *r);
  if (r == NULL) qa_range_error("Out of memory");
  r->media = media;
  r->resources = resources;
  r->host = host;
  return r;
}
void qa_app_application_q3_scene_renderer_destroy(qa_app_application_q3_scene_renderer *renderer) {
  free(renderer);
}
qa_q3_pres_q3_renderer_resources *qa_app_application_q3_scene_renderer_resources(qa_app_application_q3_scene_renderer *renderer) {
  return renderer->resources;
}
void qa_app_q3_scene_operation_sink_push(qa_app_q3_scene_operation_sink *sink, qa_scene_operation operation) {
  if (sink->count == sink->capacity) {
    size_t cap = sink->capacity == 0 ? 8 : sink->capacity * 2;
    qa_scene_operation *next = realloc(sink->operations, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    sink->operations = next;
    sink->capacity = cap;
  }
  sink->operations[sink->count++] = operation;
}
typedef struct provider_group {
  qa_app_q3_provider_scene_assets provider;
  bool has_provider;
  void *provider_ctx;
  const qa_q3_pres_presented_model **models;
  size_t count, capacity;
} provider_group;
static provider_group *group_models(qa_app_application_q3_scene_renderer *r,
                                    const qa_q3_pres_scene_content *scene, size_t *out_count) {
  provider_group *groups = NULL;
  size_t group_count = 0, group_capacity = 0;
  for (size_t i = 0; i < scene->model_count; i++) {
    const qa_q3_pres_presented_model *model = &scene->models[i];
    /* TS model.entity.model.kind === "brush-model" (decoded kind). */
    if (model->entity.model.kind == QA_DECODED_MODEL_BRUSH_MODEL) continue;
    qa_q3_pres_scene_model source_model;
    memset(&source_model, 0, sizeof source_model);
    source_model = model->source != NULL ? model->source->model : source_model;
    qa_app_q3_provider_scene_assets provider;
    if (!qa_app_application_q3_assets_model_provider(r->media, &source_model, &provider))
      qa_range_error("Cgame model lost its selected asset provider");
    provider_group *group = NULL;
    for (size_t g = 0; g < group_count; g++)
      if (groups[g].provider_ctx == provider.ctx && groups[g].provider.mounts == provider.mounts) group = &groups[g];
    if (group == NULL) {
      if (group_count == group_capacity) {
        size_t cap = group_capacity == 0 ? 4 : group_capacity * 2;
        provider_group *next = realloc(groups, cap * sizeof *next);
        if (next == NULL) qa_range_error("Out of memory");
        groups = next; group_capacity = cap;
      }
      group = &groups[group_count++];
      memset(group, 0, sizeof *group);
      group->provider = provider;
      group->has_provider = true;
      group->provider_ctx = provider.ctx;
    }
    if (group->count == group->capacity) {
      size_t cap = group->capacity == 0 ? 4 : group->capacity * 2;
      const qa_q3_pres_presented_model **next = realloc(group->models, cap * sizeof *next);
      if (next == NULL) qa_range_error("Out of memory");
      group->models = next; group->capacity = cap;
    }
    group->models[group->count++] = model;
  }
  *out_count = group_count;
  return groups;
}
static void free_groups(provider_group *groups, size_t count) {
  for (size_t i = 0; i < count; i++) free(groups[i].models);
  free(groups);
}
void qa_app_application_q3_scene_renderer_preload(qa_app_application_q3_scene_renderer *renderer,
    const qa_q3_pres_scene_content *const *contents, size_t content_count) {
  assert_current(renderer);
  for (size_t i = 0; i < content_count; i++) {
    size_t group_count = 0;
    provider_group *groups = group_models(renderer, contents[i], &group_count);
    for (size_t g = 0; g < group_count; g++) {
      /* Host preload takes the presented models directly. */
      qa_q3_pres_presented_model *flat = malloc((groups[g].count == 0 ? 1 : groups[g].count) * sizeof *flat);
      if (flat == NULL) qa_range_error("Out of memory");
      for (size_t m = 0; m < groups[g].count; m++) flat[m] = *groups[g].models[m];
      renderer->host.preload_models(renderer->host.ctx, &groups[g].provider, flat, groups[g].count);
      free(flat);
      assert_current(renderer);
    }
    free_groups(groups, group_count);
  }
}
static bool is_polygon_addition(const qa_scene_operation *operation) {
  return operation->is_group
    && operation->group.order.kind == QA_GROUP_SOURCE
    && !operation->group.order.source.entity.is_refentity;
}
static double entity_render_flags(const qa_q3_pres_admitted_ref_entity *entity) {
  if (entity->is_poly_record) return entity->record.render_flags;
  switch (entity->entity.kind) {
    case QA_Q3_PRES_REF_MODEL: return entity->entity.as.model.shading.render_flags;
    case QA_Q3_PRES_REF_SPRITE: return entity->entity.as.sprite.shading.render_flags;
    case QA_Q3_PRES_REF_BEAM: return entity->entity.as.beam.shading.render_flags;
    case QA_Q3_PRES_REF_PORTAL: return entity->entity.as.portal.render_flags;
    default: return entity->entity.as.rail.shading.render_flags;
  }
}
qa_scene_operation *qa_app_application_q3_scene_renderer_operations(
    qa_app_application_q3_scene_renderer *renderer, const qa_q3_pres_scene_content *scene,
    const qa_world_view_input *input, double first_entity, qa_app_q3_scene_render_options options,
    const qa_scene_operation *additions, size_t addition_count, size_t *out_count) {
  assert_current(renderer);
  if (input->source == NULL) qa_range_error("Q3 view has no source admission");
  qa_app_q3_scene_operation_sink sink = {NULL, 0, 0};
  bool no_world = options.no_world_model;
  qa_world_view_input weapon_input = *input;
  weapon_input.camera = qa_app_q3_weapon_camera(input->camera, options.split_screen && !no_world);
  for (size_t i = 0; i < scene->admission.polygon_count; i++)
    renderer->host.poly_operation(renderer->host.ctx, &scene->admission.polygons[i], i, input, &sink);
  for (size_t i = 0; i < addition_count; i++)
    if (is_polygon_addition(&additions[i])) qa_app_q3_scene_operation_sink_push(&sink, additions[i]);
  for (size_t index = 0; index < scene->admission.entity_count; index++) {
    const qa_q3_pres_admitted_ref_entity *original = &scene->admission.entities[index];
    qa_q3_pres_admitted_ref_entity offset = *original;
    const qa_q3_pres_admitted_ref_entity *entity = original;
    double flags = entity_render_flags(original);
    if (!no_world && ((int64_t)flags & 4) != 0) {
      offset = qa_app_offset_q3_view_reference(*original, options.view_offset);
      entity = &offset;
      flags = entity_render_flags(entity);
    }
    double entity_index = first_entity + (double)index;
    if (options.supplemental_view_weapon && !no_world && ((int64_t)flags & 4) != 0) continue;
    if (input->camera.clip_is_portal && ((int64_t)flags & 4) != 0) continue;
    if (entity->is_poly_record) qa_common_error(QA_COMMON_ERROR_DROP, "R_AddEntitySurfaces: Bad reType");
    if (!entity->is_poly_record && entity->entity.kind == QA_Q3_PRES_REF_PORTAL) continue;
    if (!entity->is_poly_record && entity->entity.kind == QA_Q3_PRES_REF_POLY) continue;
    if (!entity->is_poly_record && entity->entity.kind == QA_Q3_PRES_REF_MODEL) {
      const qa_q3_pres_ref_model_entity *model_entity = &entity->entity.as.model;
      if (model_entity->model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
        if (!input->camera.clip_is_portal && ((int64_t)flags & 2) != 0) continue;
        renderer->host.default_model_operation(renderer->host.ctx, model_entity, entity_index, input, &sink);
        continue;
      }
      const qa_q3_pres_presented_model *model = NULL;
      for (size_t m = 0; m < scene->model_count; m++)
        if (scene->models[m].entity_index == (double)index) model = &scene->models[m];
      if (model == NULL) qa_range_error("Admitted Q3 model lost its prepared descriptor");
      bool weapon = ((int64_t)flags & 4) != 0;
      const qa_world_view_input *selected = weapon ? &weapon_input : input;
      qa_scene_entity *rendered_copy = NULL;
      const qa_scene_entity *rendered = &model->entity;
      if (weapon && !no_world) {
        rendered_copy = qa_app_offset_q3_view_entity(&model->entity, options.view_offset);
        rendered = rendered_copy;
      }
      if (model->entity.model.kind == QA_DECODED_MODEL_BRUSH_MODEL) {
        renderer->host.brush_model_operations(renderer->host.ctx, model, rendered, model_entity,
                                              entity_index, selected, weapon, &sink);
      } else {
        qa_app_q3_provider_scene_assets provider;
        if (!qa_app_application_q3_assets_model_provider(renderer->media, &model_entity->model, &provider))
          qa_range_error("Cgame model lost its selected asset provider");
        renderer->host.provider_model_operations(renderer->host.ctx, &provider, model, rendered,
                                                 model_entity, entity_index, selected, weapon, no_world, &sink);
      }
      qa_app_offset_q3_view_entity_free(rendered_copy);
      continue;
    }
    if (!input->camera.clip_is_portal && ((int64_t)flags & 2) != 0) continue;
    renderer->host.procedural_operation(renderer->host.ctx, entity, entity_index, input, no_world, &sink);
  }
  for (size_t i = 0; i < addition_count; i++)
    if (!is_polygon_addition(&additions[i])) qa_app_q3_scene_operation_sink_push(&sink, additions[i]);
  if (sink.operations == NULL) {
    sink.operations = malloc(sizeof *sink.operations);
    if (sink.operations == NULL) qa_range_error("Out of memory");
  }
  *out_count = sink.count;
  return sink.operations;
}
void qa_app_application_q3_scene_renderer_close(qa_app_application_q3_scene_renderer *renderer) {
  renderer->closed = true;
}
