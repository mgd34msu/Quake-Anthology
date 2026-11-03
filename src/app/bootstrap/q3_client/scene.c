/* Port of src/app/bootstrap/q3-client/scene.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client/scene.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q3_client/view.h"
#include "qa/core/common_error.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/materials/evaluate.h"
#include "qa/render/scene/models/renderer.h"
#include "qa/render/scene/shaders.h"
#include "qa/render/scene/view.h"

/* One SceneModelRenderer per ProviderSceneAssets (TS Map keyed by the
 * provider object; the provider identity here is its shader registry). */
typedef struct provider_renderer {
  const void *provider;
  qa_scene_model_renderer renderer;
} provider_renderer;
struct qa_app_application_q3_scene_renderer {
  qa_app_application_q3_assets *media;
  qa_q3_pres_q3_renderer_resources *resources;
  provider_renderer **renderers; size_t renderer_count, renderer_capacity;
  bool closed;
};

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void assert_current(const qa_app_application_q3_scene_renderer *r) {
  if (r->closed) qa_raise("Error", 0, 0, "Q3 scene renderer is closed");
}
/* TS media.assets (ApplicationAssets). */
static qa_app_application_assets *application(const qa_app_application_q3_scene_renderer *r) {
  qa_app_application_assets *assets = qa_app_application_q3_assets_application(r->media);
  if (assets == NULL) qa_raise("Error", 0, 0, "Q3 scene renderer media has no application assets");
  return assets;
}
/* TS media.provider: ApplicationAssets.provider(media.content). */
static qa_app_provider_scene_assets media_provider(const qa_app_application_q3_scene_renderer *r) {
  return qa_app_application_assets_provider(application(r), qa_app_application_q3_assets_content(r->media));
}

qa_app_application_q3_scene_renderer *qa_app_application_q3_scene_renderer_create(
    qa_app_application_q3_assets *media, qa_q3_pres_q3_renderer_resources *resources) {
  qa_app_application_q3_scene_renderer *r = xcalloc(1, sizeof *r);
  r->media = media;
  r->resources = resources;
  return r;
}
static void clear_renderers(qa_app_application_q3_scene_renderer *r) {
  for (size_t i = 0; i < r->renderer_count; i++) {
    qa_scene_model_renderer_destroy(&r->renderers[i]->renderer);
    free(r->renderers[i]);
  }
  r->renderer_count = 0;
}
void qa_app_application_q3_scene_renderer_destroy(qa_app_application_q3_scene_renderer *renderer) {
  if (renderer == NULL) return;
  qa_app_application_q3_scene_renderer_close(renderer);
  free(renderer->renderers);
  free(renderer);
}
qa_q3_pres_q3_renderer_resources *qa_app_application_q3_scene_renderer_resources(qa_app_application_q3_scene_renderer *renderer) {
  return renderer->resources;
}
void qa_app_application_q3_scene_renderer_close(qa_app_application_q3_scene_renderer *renderer) {
  renderer->closed = true;
  clear_renderers(renderer);
}

/* TS renderer(provider): the provider's SceneModelRenderer over media.assets.world,
 * created on first use. model is one the provider was selected for. */
static qa_scene_model_renderer *model_renderer(qa_app_application_q3_scene_renderer *r,
                                               const qa_app_q3_provider_scene_assets *provider,
                                               const qa_q3_pres_scene_model *model) {
  assert_current(r);
  for (size_t i = 0; i < r->renderer_count; i++)
    if (r->renderers[i]->provider == provider->ctx) return &r->renderers[i]->renderer;
  qa_content_id content;
  if (!qa_app_application_q3_assets_model_content(r->media, model, &content))
    qa_raise("Error", 0, 0, "Cgame model lost its selected asset provider");
  qa_app_application_assets *assets = application(r);
  if (r->renderer_count == r->renderer_capacity) {
    size_t capacity = r->renderer_capacity == 0 ? 4 : r->renderer_capacity * 2;
    provider_renderer **next = realloc(r->renderers, capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    r->renderers = next;
    r->renderer_capacity = capacity;
  }
  qa_model_render_provider provider_value = qa_app_application_assets_render_provider(assets, content);
  qa_world_scene *world = qa_app_application_assets_world(assets);
  provider_renderer *entry = xcalloc(1, sizeof *entry);
  entry->provider = provider->ctx;
  if (!qa_scene_model_renderer_init(&entry->renderer, provider_value, world)) {
    free(entry);
    qa_range_error("Out of memory");
  }
  r->renderers[r->renderer_count++] = entry;
  return &entry->renderer;
}

/* PresentedModel.options ({ customShader, customSkin, nonNormalizedAxes }) as
 * ModelSourceOptions; the skin entries are allocated into storage when given. */
static qa_model_source_options presented_options(const qa_q3_pres_presented_model *model, qa_app_effect_storage *storage) {
  qa_model_source_options options;
  memset(&options, 0, sizeof options);
  options.custom_shader = model->custom_shader;
  if (model->custom_skin != NULL) {
    qa_model_custom_skin_entry *skin = xcalloc(model->custom_skin_count, sizeof *skin);
    for (size_t i = 0; i < model->custom_skin_count; i++)
      skin[i] = (qa_model_custom_skin_entry){model->custom_skin[i].name, model->custom_skin[i].shader};
    qa_app_effect_storage_add(storage, free, skin);
    options.custom_skin = skin;
    options.custom_skin_count = model->custom_skin_count;
  }
  options.has_non_normalized_axes = true;
  options.non_normalized_axes = model->non_normalized_axes;
  return options;
}

/* ---- preload ---- */
typedef struct preload_group {
  qa_app_q3_provider_scene_assets provider;
  const qa_q3_pres_presented_model **models; size_t count, capacity;
} preload_group;
typedef struct preload_state {
  const preload_group *group;
  const qa_scene_entity *entities;
  qa_app_effect_storage *storage;
} preload_state;
/* entity => models.find(model => model.entity === entity)?.options ?? {} */
static qa_model_source_options preload_options(void *ctx, const qa_scene_entity *entity) {
  preload_state *state = ctx;
  qa_model_source_options empty;
  memset(&empty, 0, sizeof empty);
  if (entity < state->entities || entity >= state->entities + state->group->count) return empty;
  return presented_options(state->group->models[entity - state->entities], state->storage);
}
static void free_preload_groups(preload_group *groups, size_t count) {
  for (size_t i = 0; i < count; i++) free(groups[i].models);
  free(groups);
}
typedef struct preload_scope { preload_group *groups; size_t count; qa_scene_entity *entities; qa_app_effect_storage storage; } preload_scope;
static void release_preload_scope(void *ctx) {
  preload_scope *scope = ctx;
  free_preload_groups(scope->groups, scope->count);
  free(scope->entities);
  qa_app_effect_storage_release(&scope->storage);
  free(scope->storage.entries);
}
/* TS models(scene): non-brush models grouped by their selected provider, in first-seen order. */
static void group_models(qa_app_application_q3_scene_renderer *r, const qa_q3_pres_scene_content *scene, preload_scope *scope) {
  size_t capacity = 0;
  for (size_t i = 0; i < scene->model_count; i++) {
    const qa_q3_pres_presented_model *model = &scene->models[i];
    if (model->entity.model.kind == QA_DECODED_MODEL_BRUSH_MODEL) continue;
    qa_app_q3_provider_scene_assets provider;
    if (model->source == NULL || !qa_app_application_q3_assets_model_provider(r->media, &model->source->model, &provider))
      qa_raise("Error", 0, 0, "Cgame model lost its selected asset provider");
    preload_group *group = NULL;
    for (size_t g = 0; g < scope->count && group == NULL; g++)
      if (scope->groups[g].provider.ctx == provider.ctx) group = &scope->groups[g];
    if (group == NULL) {
      if (scope->count == capacity) {
        capacity = capacity == 0 ? 4 : capacity * 2;
        preload_group *next = realloc(scope->groups, capacity * sizeof *next);
        if (next == NULL) qa_range_error("Out of memory");
        scope->groups = next;
      }
      group = &scope->groups[scope->count++];
      memset(group, 0, sizeof *group);
      group->provider = provider;
    }
    if (group->count == group->capacity) {
      group->capacity = group->capacity == 0 ? 4 : group->capacity * 2;
      const qa_q3_pres_presented_model **next = realloc(group->models, group->capacity * sizeof *next);
      if (next == NULL) qa_range_error("Out of memory");
      group->models = next;
    }
    group->models[group->count++] = model;
  }
}
void qa_app_application_q3_scene_renderer_preload(qa_app_application_q3_scene_renderer *renderer,
    const qa_q3_pres_scene_content *const *contents, size_t content_count) {
  assert_current(renderer);
  for (size_t i = 0; i < content_count; i++) {
    preload_scope scope;
    memset(&scope, 0, sizeof scope);
    qa_cleanup node;
    qa_cleanup_push(&node, release_preload_scope, &scope);
    group_models(renderer, contents[i], &scope);
    for (size_t g = 0; g < scope.count; g++) {
      const preload_group *group = &scope.groups[g];
      free(scope.entities);
      scope.entities = xcalloc(group->count, sizeof *scope.entities);
      for (size_t m = 0; m < group->count; m++) scope.entities[m] = group->models[m]->entity;
      preload_state state = {group, scope.entities, &scope.storage};
      qa_scene_model_renderer *model = model_renderer(renderer, &group->provider, &group->models[0]->source->model);
      qa_scene_model_renderer_preload(model, scope.entities, group->count, preload_options, &state);
      assert_current(renderer);
    }
    qa_cleanup_pop(&node);
    release_preload_scope(&scope);
  }
}

/* ---- operations ---- */
void qa_app_q3_scene_operations_release(qa_app_q3_scene_operations *operations) {
  if (operations == NULL) return;
  qa_app_effect_storage_release(&operations->storage);
  free(operations->storage.entries);
  free(operations->items);
  memset(operations, 0, sizeof *operations);
}
static void push(qa_app_q3_scene_operations *out, qa_scene_operation operation) {
  if (out->count == out->capacity) {
    out->capacity = out->capacity == 0 ? 16 : out->capacity * 2;
    qa_scene_operation *next = realloc(out->items, out->capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    out->items = next;
  }
  out->items[out->count++] = operation;
}
/* One owned source draw group: the group, its batches, the geometry and
 * the material context the batches were prepared from. */
typedef struct owned_group {
  qa_scene_group group;
  qa_draw_batch *batches; size_t batch_count;
  qa_entity_geometry geometry; bool has_geometry;
  qa_particle_draw_batch particle; bool has_particle;
  qa_world_material_context *context;
} owned_group;
static void release_owned_group(void *value) {
  owned_group *g = value;
  qa_scene_group_destroy(&g->group);
  if (g->batches != NULL) qa_mat_draw_batches_destroy(g->batches, g->batch_count);
  if (g->has_particle) qa_particle_draw_batch_destroy(&g->particle);
  if (g->has_geometry) qa_entity_geometry_destroy(&g->geometry);
  free(g->context);
  free(g);
}
static owned_group *owned(qa_app_q3_scene_operations *out) {
  owned_group *g = xcalloc(1, sizeof *g);
  qa_app_effect_storage_add(&out->storage, release_owned_group, g);
  return g;
}
static void push_group(qa_app_q3_scene_operations *out, const qa_scene_group *group) {
  qa_scene_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.is_group = true;
  operation.group = *group;
  push(out, operation);
}
static void release_scene_group(void *value) {
  qa_scene_group_destroy(value);
  free(value);
}
static qa_sequence_phase phase_named(const char *name) {
  for (int i = QA_SEQUENCE_SKY; i <= QA_SEQUENCE_TRANSLUCENT; i++)
    if (name != NULL && strcmp(qa_sequence_phase_name((qa_sequence_phase)i), name) == 0) return (qa_sequence_phase)i;
  return QA_SEQUENCE_TRANSLUCENT;
}
/* A SceneModelGroup is a SceneGroup in TS; the C group wraps its batches. */
static void push_model_group(qa_app_q3_scene_operations *out, const qa_scene_model_group *group) {
  qa_scene_group *wrapped = xcalloc(1, sizeof *wrapped);
  if (strcmp(group->kind, "compiled") == 0) *wrapped = qa_compiled_draw_group(group->material, group->batches, group->batch_count);
  else if (strcmp(group->kind, "source") == 0) {
    qa_source_surface_order order = {group->source_view, group->source_entity, group->surface, group->fog, group->dlight};
    *wrapped = qa_source_draw_group(group->source_material, order, group->batches, group->batch_count);
  } else *wrapped = qa_sequence_draw_group(phase_named(group->sequence), group->batches, group->batch_count);
  qa_app_effect_storage_add(&out->storage, release_scene_group, wrapped);
  push_group(out, wrapped);
}
typedef struct model_groups { qa_scene_model_group *groups; size_t count; } model_groups;
static void release_model_groups(void *value) {
  model_groups *groups = value;
  qa_scene_model_groups_destroy(groups->groups, groups->count);
  free(groups);
}
static void release_world_operations(void *value) {
  qa_world_operations_destroy(value);
  free(value);
}
static bool is_polygon(const qa_scene_operation *operation) {
  return operation->is_group && operation->group.order.kind == QA_GROUP_SOURCE
    && !operation->group.order.source.entity.is_refentity;
}
static double render_flags(const qa_q3_pres_admitted_ref_entity *entity) {
  if (entity->is_poly_record) return entity->record.render_flags;
  switch (entity->entity.kind) {
    case QA_Q3_PRES_REF_MODEL: return entity->entity.as.model.shading.render_flags;
    case QA_Q3_PRES_REF_SPRITE: return entity->entity.as.sprite.shading.render_flags;
    case QA_Q3_PRES_REF_BEAM: return entity->entity.as.beam.shading.render_flags;
    case QA_Q3_PRES_REF_PORTAL: return entity->entity.as.portal.render_flags;
    default: return entity->entity.as.rail.shading.render_flags;
  }
}
/* TS media.assets.materialRegistrations.requireMaterial(picture.material.compiled):
 * only a MaterialHandle published in this scene's registrations is accepted. */
static qa_registered_scene_material require_picture(qa_app_application_q3_scene_renderer *r, const qa_q3_pres_scene_shader *shader) {
  qa_material_picture picture = qa_q3_pres_q3_renderer_resources_picture(r->resources, shader);
  if (!picture.compiled_is_handle) qa_raise("Error", 0, 0, "Material is not registered in this scene");
  const qa_scene_material_handle *handle = picture.compiled;
  qa_registered_scene_material material = {handle->registration, (qa_scene_material_handle *)handle, handle->current};
  qa_scene_material_registrations *registrations = qa_app_application_assets_material_registrations(application(r));
  if (handle->registration == NULL || handle->registration->provider == NULL || handle->registration->provider->owner != registrations)
    qa_raise("Error", 0, 0, "Material is not registered in this scene");
  return qa_scene_material_registrations_require_material(registrations, material);
}
/* TS fog?.volume over world.fogSelections by the selection's index. */
static const qa_fog_volume *fog_volume(const qa_world_scene *world, const qa_q3_pres_fog_selection *fog) {
  if (fog == NULL) return NULL;
  for (size_t i = 0; i < world->fog_selection_count; i++)
    if (world->fog_selections[i].index == fog->index) return &world->fog_selections[i].volume;
  return NULL;
}
static qa_vec4 project_point(void *ctx, qa_vec3 point) {
  return qa_scene_projector_project(*(const qa_scene_view_projector *)ctx, point);
}
static qa_material_geometry geometry_of(const qa_entity_geometry *geometry) {
  qa_material_geometry out = {geometry->vertices, geometry->vertex_count, geometry->indices, geometry->index_count};
  return out;
}
/* TS state. */
static const qa_render_state default_state = {{"one", "zero"}, "less-equal", true, "none", "back", {0, 1}, NULL};
typedef struct model_options_state { qa_model_source_options options; } model_options_state;
static qa_model_source_options model_options(void *ctx, const qa_scene_entity *entity) {
  (void)entity; /* TS () => ({ ...model.options, noWorldModel, shaderTexCoord, source }) */
  return ((const model_options_state *)ctx)->options;
}
typedef struct operation_scope { qa_q3_pres_fog_selection *fogs; qa_scene_entity *rendered; } operation_scope;
static void release_operation_scope(void *ctx) {
  operation_scope *scope = ctx;
  free(scope->fogs);
  qa_app_offset_q3_view_entity_free(scope->rendered);
  scope->rendered = NULL;
}
void qa_app_application_q3_scene_renderer_operations(
    qa_app_application_q3_scene_renderer *renderer, const qa_q3_pres_scene_content *scene,
    const qa_world_view_input *input, double first_entity, qa_app_q3_scene_render_options options,
    const qa_scene_operation *additions, size_t addition_count, qa_app_q3_scene_operations *out) {
  memset(out, 0, sizeof *out);
  assert_current(renderer);
  const qa_world_surface_admission *source = input->source;
  if (source == NULL) qa_raise("Error", 0, 0, "Q3 view has no source admission");
  qa_app_application_assets *assets = application(renderer);
  qa_world_scene *world = qa_app_application_assets_world(assets);
  bool no_world = options.no_world_model;
  qa_world_view_input weapon_input = *input;
  weapon_input.camera = qa_app_q3_weapon_camera(input->camera, options.split_screen && !no_world);
  operation_scope scope = {NULL, NULL};
  qa_cleanup node;
  qa_cleanup_push(&node, release_operation_scope, &scope);
  for (size_t index = 0; index < scene->admission.polygon_count; index++) {
    const qa_q3_pres_admitted_poly *poly = &scene->admission.polygons[index];
    qa_registered_scene_material compiled = require_picture(renderer, poly->poly.shader);
    qa_scene_material_remap remap;
    bool has_remap = qa_scene_material_registrations_current_remap(
        qa_app_application_assets_material_registrations(assets), compiled, &remap);
    owned_group *g = owned(out);
    g->context = xcalloc(1, sizeof *g->context);
    qa_world_scene_material_context(world, input, NULL, fog_volume(world, poly->fog), g->context);
    qa_poly_input_vertex *vertices = xcalloc(poly->poly.vertex_count, sizeof *vertices);
    for (size_t v = 0; v < poly->poly.vertex_count; v++)
      vertices[v] = (qa_poly_input_vertex){poly->poly.vertices[v].position, poly->poly.vertices[v].tex_coord,
                                           poly->poly.vertices[v].color};
    g->geometry = qa_poly_geometry(vertices, poly->poly.vertex_count);
    g->has_geometry = true;
    free(vertices);
    g->context->context.time_offset = g->context->context.time_offset + (has_remap ? remap.time_offset : 0);
    g->batches = qa_prepare_material_batches(qa_registered_scene_material_compiled(has_remap ? remap.material : compiled),
                                             geometry_of(&g->geometry), &g->context->context, &g->batch_count);
    qa_source_surface_order order = {source->view, {false, 0}, (double)index,
                                     poly->fog == NULL ? 0 : poly->fog->index + 1, 0};
    g->group = qa_source_draw_group(compiled, order, g->batches, g->batch_count);
    push_group(out, &g->group);
  }
  for (size_t i = 0; i < addition_count; i++)
    if (is_polygon(&additions[i])) push(out, additions[i]);
  qa_scene_view_projector *projector = xcalloc(1, sizeof *projector);
  qa_app_effect_storage_add(&out->storage, free, projector);
  *projector = qa_scene_create_view_projector(input->camera);
  qa_app_provider_scene_assets provider = media_provider(renderer);
  qa_renderer_image white = provider.textures->white->image;
  qa_source_scene_materials source_materials = qa_scene_shader_registry_source_materials(provider.shaders);
  scope.fogs = xcalloc(world->fog_selection_count, sizeof *scope.fogs);
  for (size_t i = 0; i < world->fog_selection_count; i++)
    scope.fogs[i] = (qa_q3_pres_fog_selection){world->fog_selections[i].index, world->fog_selections[i].volume.bounds};
  for (size_t index = 0; index < scene->admission.entity_count; index++) {
    const qa_q3_pres_admitted_ref_entity *original = &scene->admission.entities[index];
    qa_q3_pres_admitted_ref_entity offset;
    const qa_q3_pres_admitted_ref_entity *entity = original;
    if (!no_world && (qa_js_to_int32(render_flags(original)) & 4) != 0) {
      offset = qa_app_offset_q3_view_reference(*original, options.view_offset);
      entity = &offset;
    }
    int32_t flags = qa_js_to_int32(render_flags(entity));
    qa_source_entity_order entity_order = {true, first_entity + (double)index};
    if (options.supplemental_view_weapon && !no_world && (flags & 4) != 0) continue;
    if (input->camera.clip_is_portal && (flags & 4) != 0) continue;
    if (entity->is_poly_record) qa_common_error(QA_COMMON_ERROR_DROP, "R_AddEntitySurfaces: Bad reType");
    if (entity->entity.kind == QA_Q3_PRES_REF_PORTAL) continue;
    if (entity->entity.kind == QA_Q3_PRES_REF_MODEL) {
      const qa_q3_pres_ref_model_entity *model_entity = &entity->entity.as.model;
      if (model_entity->model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
        if (!input->camera.clip_is_portal && (flags & 2) != 0) continue;
        owned_group *g = owned(out);
        qa_model_transform transform = {model_entity->origin, model_entity->axis, {1, 1, 1}};
        g->particle = qa_default_model_batch(transform, project_point, projector, default_state, white);
        g->has_particle = true;
        qa_source_surface_order order = {source->view, entity_order, 0, 0, 0};
        g->group = qa_source_draw_group(source_materials.default_material, order, &g->particle.batch, 1);
        push_group(out, &g->group);
        continue;
      }
      const qa_q3_pres_presented_model *model = NULL;
      for (size_t m = 0; m < scene->model_count; m++)
        if (scene->models[m].entity_index == (double)index) model = &scene->models[m];
      if (model == NULL) qa_raise("Error", 0, 0, "Admitted Q3 model lost its prepared descriptor");
      bool weapon = (flags & 4) != 0;
      const qa_world_view_input *selected = weapon ? &weapon_input : input;
      const qa_scene_entity *rendered = &model->entity;
      if (weapon && !no_world) {
        scope.rendered = qa_app_offset_q3_view_entity(&model->entity, options.view_offset);
        rendered = scope.rendered;
      }
      if (model->entity.model.kind == QA_DECODED_MODEL_BRUSH_MODEL) {
        qa_world_view_input brush = *selected;
        brush.has_animation_frame = true;
        brush.animation_frame = model_entity->frame;
        brush.has_material_entity_rgba = true;
        brush.material_entity_rgba = model_entity->shading.shader_rgba;
        qa_scene_model_transform transform;
        memset(&transform, 0, sizeof transform);
        transform.origin = rendered->transform.origin;
        transform.axis = model_entity->axis;
        qa_world_operations *prepared = xcalloc(1, sizeof *prepared);
        qa_app_effect_storage_add(&out->storage, release_world_operations, prepared);
        qa_world_scene_prepare_model(world, model->entity.model.brush_model_index, transform, &brush, &entity_order, prepared);
        for (size_t i = 0; i < prepared->count; i++) push(out, prepared->operations[i]);
      } else {
        qa_app_q3_provider_scene_assets selected_provider;
        if (!qa_app_application_q3_assets_model_provider(renderer->media, &model_entity->model, &selected_provider))
          qa_raise("Error", 0, 0, "Cgame model lost its selected asset provider");
        qa_scene_model_renderer *models = model_renderer(renderer, &selected_provider, &model_entity->model);
        model_options_state state;
        state.options = presented_options(model, &out->storage);
        state.options.has_no_world_model = true;
        state.options.no_world_model = no_world;
        state.options.has_shader_tex_coord = true;
        state.options.shader_tex_coord = model_entity->shading.shader_tex_coord;
        state.options.has_source = true;
        state.options.source_view = source->view;
        state.options.source_entity = entity_order;
        model_groups *groups = xcalloc(1, sizeof *groups);
        qa_app_effect_storage_add(&out->storage, release_model_groups, groups);
        groups->groups = qa_scene_model_renderer_prepare(models, rendered, 1, selected, model_options, &state, NULL, &groups->count);
        for (size_t i = 0; i < groups->count; i++) push_model_group(out, &groups->groups[i]);
      }
      qa_app_offset_q3_view_entity_free(scope.rendered);
      scope.rendered = NULL;
      continue;
    }
    if (!input->camera.clip_is_portal && (flags & 2) != 0) continue;
    const qa_q3_pres_ref_entity *ref = &entity->entity;
    const qa_q3_pres_shading *shading = ref->kind == QA_Q3_PRES_REF_SPRITE ? &ref->as.sprite.shading
        : ref->kind == QA_Q3_PRES_REF_BEAM ? &ref->as.beam.shading : &ref->as.rail.shading;
    qa_vec3 origin = ref->kind == QA_Q3_PRES_REF_SPRITE ? ref->as.sprite.origin
        : ref->kind == QA_Q3_PRES_REF_BEAM ? ref->as.beam.origin : ref->as.rail.origin;
    double radius = ref->kind == QA_Q3_PRES_REF_SPRITE ? ref->as.sprite.radius
        : ref->kind == QA_Q3_PRES_REF_BEAM ? ref->as.beam.radius : ref->as.rail.radius;
    qa_registered_scene_material compiled = shading->custom_shader == NULL ? source_materials.default_material
        : require_picture(renderer, shading->custom_shader);
    qa_scene_material_remap remap;
    bool has_remap = qa_scene_material_registrations_current_remap(
        qa_app_application_assets_material_registrations(assets), compiled, &remap);
    const qa_q3_pres_fog_selection *fog = no_world ? NULL
        : qa_q3_pres_procedural_fog(origin, radius, scope.fogs, world->fog_selection_count);
    qa_source_surface_order order = {source->view, entity_order, 0, fog == NULL ? 0 : fog->index + 1, 0};
    owned_group *g = owned(out);
    if (ref->kind == QA_Q3_PRES_REF_BEAM) {
      qa_beam_pose pose = {ref->as.beam.origin, ref->as.beam.old_origin};
      g->particle = qa_beam_batch(pose, project_point, projector, default_state, white);
      g->has_particle = true;
      g->group = qa_source_draw_group(compiled, order, &g->particle.batch, 1);
      push_group(out, &g->group);
      continue;
    }
    if (ref->kind == QA_Q3_PRES_REF_SPRITE) {
      qa_sprite_pose pose = {ref->as.sprite.origin, ref->as.sprite.radius, ref->as.sprite.rotation, shading->shader_rgba};
      g->geometry = qa_sprite_geometry(pose, input->camera.axis, input->camera.clip_is_portal && input->camera.clip_mirror);
    } else {
      const char *kind = ref->kind == QA_Q3_PRES_REF_RAIL_CORE ? "rail-core"
          : ref->kind == QA_Q3_PRES_REF_RAIL_RINGS ? "rail-rings" : "lightning";
      qa_rail_pose pose = {kind, ref->as.rail.origin, ref->as.rail.old_origin, shading->shader_rgba};
      g->geometry = qa_rail_geometry(pose, input->camera.origin, options.rail != NULL ? options.rail : &qa_default_rail_settings);
    }
    g->has_geometry = true;
    g->context = xcalloc(1, sizeof *g->context);
    qa_world_scene_material_context(world, input, NULL, fog == NULL ? NULL : fog_volume(world, fog), g->context);
    g->context->context.entity_rgba = shading->shader_rgba;
    g->context->context.shader_tex_coord = shading->shader_tex_coord;
    g->context->context.time_offset = shading->shader_time + (has_remap ? remap.time_offset : 0);
    g->batches = qa_prepare_material_batches(qa_registered_scene_material_compiled(has_remap ? remap.material : compiled),
                                             geometry_of(&g->geometry), &g->context->context, &g->batch_count);
    g->group = qa_source_draw_group(compiled, order, g->batches, g->batch_count);
    push_group(out, &g->group);
  }
  for (size_t i = 0; i < addition_count; i++)
    if (!is_polygon(&additions[i])) push(out, additions[i]);
  qa_cleanup_pop(&node);
  release_operation_scope(&scope);
}
