/* pres_contracts part: render. Included by tests/app/test_pres_contracts.c.
 * ModelLightSampler and SceneModelRenderer on the real WorldScene,
 * WorldViewInput, SceneTextureLoader and ModelRenderProvider types. The
 * worlds are hand-built WorldScene values (map, surfaces, options, shaders.sun:
 * what the sampler reads); the bun oracle passes the same objects to the TS
 * classes. Expected values: build/agents/pres-contracts-render/tmp/oracle.ts. */
#include "qa/core/math.h"
#include "qa/render/scene/models/light_sampler.h"
#include "qa/render/scene/models/renderer.h"
#include "qa/render/scene/world.h"

typedef struct pres_render_sample {
  qa_vec3 color;
  bool has_floor;
  qa_vec3 floor_point;
} pres_render_sample;

static const pres_render_sample pres_render_q1_samples[8] = {
  {{0.5586397051811218, 0.2852022051811218, 0.14848345518112183}, true, {8.0, 8.0, 0.0}},
  {{0.011764705882352941, 0.011764705882352941, 0.011764705882352941}, true, {8.0, 8.0, 0.0}},
  {{0.978959321975708, 0.7247737646102905, 0.5976809859275818}, true, {40.0, 24.0, 0.0}},
  {{0.47058823529411764, 0.47058823529411764, 0.47058823529411764}, true, {40.0, 24.0, 0.0}},
  {{0.22157829999923706, 0.11078914999961853, 0.055394574999809265}, false, {0, 0, 0}},
  {{0.0, 0.0, 0.0}, false, {0, 0, 0}},
  {{0.5859375, 0.29296875, 0.146484375}, false, {0, 0, 0}},
  {{0.0, 0.0, 0.0}, false, {0, 0, 0}}};
static const qa_entity_lighting pres_render_q1_entity[3] = {
  {{127.5, 127.5, 127.5}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 4286545791u},
  {{91.0, 91.0, 91.0}, {634.0062255859375, 634.0062255859375, 634.0062255859375}, {-0.7313336730003357, -0.7024237513542175, 0.5846376419067383}, 4284177243u},
  {{91.0, 91.0, 91.0}, {634.0062255859375, 634.0062255859375, 634.0062255859375}, {-0.691563606262207, -0.8165385127067566, 0.5958656668663025}, 4284177243u}};
static const pres_render_sample pres_render_q2_samples[8] = {
  {{0.5586397051811218, 0.3518688678741455, 0.33671873807907104}, true, {8.0, 8.0, 0.0}},
  {{0.0117647061124444, 0.0784313753247261, 0.20000000298023224}, true, {8.0, 8.0, 0.0}},
  {{1.9985672235488892, 2.3169307708740234, 2.817288875579834}, true, {40.0, 24.0, 0.0}},
  {{1.4901961088180542, 2.0627450942993164, 2.6901960372924805}, true, {40.0, 24.0, 0.0}},
  {{0.22157829999923706, 0.11078914999961853, 0.055394574999809265}, false, {0, 0, 0}},
  {{0.0, 0.0, 0.0}, false, {0, 0, 0}},
  {{0.5859375, 0.29296875, 0.146484375}, false, {0, 0, 0}},
  {{0.0, 0.0, 0.0}, false, {0, 0, 0}}};
static const qa_entity_lighting pres_render_q2_entity[3] = {
  {{127.5, 127.5, 127.5}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 4286545791u},
  {{91.0, 91.0, 91.0}, {634.0062255859375, 634.0062255859375, 634.0062255859375}, {-0.7313336730003357, -0.7024237513542175, 0.5846376419067383}, 4284177243u},
  {{91.0, 91.0, 91.0}, {634.0062255859375, 634.0062255859375, 634.0062255859375}, {-0.691563606262207, -0.8165385127067566, 0.5958656668663025}, 4284177243u}};

static bool pres_render_vec3_eq(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

/* Synthetic BSP shared by the Q1 and Q2 worlds: one node on z = 0 whose two
 * faces are surface 0 (lightmapped, 2x2 samples) and surface 1 (no lightmap,
 * an ordinary 64x64 quad the sampler prepares itself). */
typedef struct pres_render_fixture {
  uint8_t samples[256];
  qa_bsp_plane planes[1];
  qa_vec3 vertices[4];
  qa_bsp_edge edges[4];
  double surface_edges[4];
  qa_bsp_node nodes[1];
  qa_q1_texture_info q1_info[1];
  qa_q2_texture_info q2_info[1];
  double face0_styles[2], face1_styles[4];
  qa_bsp_face faces[2];
  qa_q1_world_geometry q1;
  qa_q2_world_geometry q2;
  qa_world_surface_lightmap lightmap0;
  qa_world_surface surfaces[2];
  qa_scene_shader_registry shaders;
  qa_world_scene world;
} pres_render_fixture;

static void pres_render_fixture_init(pres_render_fixture *f, bool q2, bool sun) {
  memset(f, 0, sizeof *f);
  for (int i = 0; i < 256; i++) f->samples[i] = (uint8_t)((i * 7 + 3) & 255);
  f->planes[0].plane = (qa_plane){{0, 0, 1}, 0};
  f->planes[0].type = 2;
  f->vertices[0] = (qa_vec3){0, 0, 0};
  f->vertices[1] = (qa_vec3){64, 0, 0};
  f->vertices[2] = (qa_vec3){64, 64, 0};
  f->vertices[3] = (qa_vec3){0, 64, 0};
  for (int i = 0; i < 4; i++) {
    f->edges[i].vertices[0] = i;
    f->edges[i].vertices[1] = (i + 1) % 4;
    f->surface_edges[i] = i;
  }
  f->nodes[0].plane = 0;
  f->nodes[0].children[0] = (qa_bsp_child){false, 0};
  f->nodes[0].children[1] = (qa_bsp_child){false, 1};
  f->nodes[0].bounds = (qa_bounds){{0, 0, 0}, {64, 64, 0}};
  f->nodes[0].faces = (qa_index_range){0, 2};
  qa_texture_projection projection = {{1, 0, 0, 0}, {0, 1, 0, 0}};
  f->q1_info[0].projection = projection;
  f->q2_info[0].projection = projection;
  f->q2_info[0].surface.name = "e1u1/floor";
  f->q2_info[0].surface.material = "";
  f->face0_styles[0] = 0; f->face0_styles[1] = 255;
  f->face1_styles[0] = 0; f->face1_styles[1] = 1; f->face1_styles[2] = 255; f->face1_styles[3] = 255;
  for (int i = 0; i < 2; i++) {
    f->faces[i].plane = 0;
    f->faces[i].edges = (qa_index_range){0, 4};
    f->faces[i].texture_info = 0;
    f->faces[i].has_lighting_offset = true;
  }
  f->faces[0].styles = (qa_double_view){f->face0_styles, 2};
  f->faces[1].styles = (qa_double_view){f->face1_styles, 4};
  f->faces[1].lighting_offset = 32;
  qa_bsp_lighting lighting = {q2, {f->samples, 256}, q2 ? "bsp" : NULL};
  if (q2) {
    f->q2.planes = f->planes; f->q2.plane_count = 1;
    f->q2.vertices = f->vertices; f->q2.vertex_count = 4;
    f->q2.edges = f->edges; f->q2.edge_count = 4;
    f->q2.surface_edges = (qa_double_view){f->surface_edges, 4};
    f->q2.nodes = f->nodes; f->q2.node_count = 1;
    f->q2.texture_info = f->q2_info; f->q2.texture_info_count = 1;
    f->q2.faces = f->faces; f->q2.face_count = 2;
    f->q2.lighting = lighting;
    f->world.map.kind = QA_DECODED_WORLD_Q2;
    f->world.map.as.q2 = &f->q2;
    f->world.options.has_q2_light_modulate = true;
    f->world.options.q2_light_modulate = 2;
  } else {
    f->q1.planes = f->planes; f->q1.plane_count = 1;
    f->q1.vertices = f->vertices; f->q1.vertex_count = 4;
    f->q1.edges = f->edges; f->q1.edge_count = 4;
    f->q1.surface_edges = (qa_double_view){f->surface_edges, 4};
    f->q1.nodes = f->nodes; f->q1.node_count = 1;
    f->q1.texture_info = f->q1_info; f->q1.texture_info_count = 1;
    f->q1.faces = f->faces; f->q1.face_count = 2;
    f->q1.lighting = lighting;
    f->world.map.kind = QA_DECODED_WORLD_Q1;
    f->world.map.as.q1 = &f->q1;
  }
  const qa_bsp_lighting *map_lighting = q2 ? &f->q2.lighting : &f->q1.lighting;
  qa_lightmap_face *face0 = &f->lightmap0.face;
  face0->width = 2; face0->height = 2;
  face0->lighting = map_lighting;
  face0->styles = f->face0_styles; face0->style_count = 2;
  face0->plane = f->planes[0].plane;
  face0->projection.kind = "classic";
  face0->projection.texture = projection;
  for (int i = 0; i < 2; i++) {
    qa_world_surface *surface = &f->surfaces[i];
    surface->index = i;
    surface->material.kind = q2 ? "q2" : "q1";
    surface->material.surface = q2 ? NULL : "ordinary";
  }
  f->surfaces[0].legacy_lightmap = &f->lightmap0;
  f->shaders.has_sun = sun;
  f->shaders.sun_value.direction = qa_normalize3((qa_vec3){1, 2, 3});
  f->world.surfaces = f->surfaces;
  f->world.surface_count = 2;
  f->world.shaders = &f->shaders;
}

static void pres_render_sampler_checks(bool q2, const pres_render_sample *expected,
                                       const qa_entity_lighting *expected_entity) {
  static double q1_styles[256];
  static qa_q2_light_style q2_styles[256];
  for (int i = 0; i < 256; i++) {
    q1_styles[i] = i == 0 ? 300 : i == 1 ? 128 : 256;
    q2_styles[i] = (qa_q2_light_style){i == 0 ? (qa_vec3){0.5, 1, 1.5} : (qa_vec3){1, 1, 1}, 3};
  }
  const qa_surface_dynamic_light lights[1] = {{{8, 8, 40}, 200, 0, {1, 0.5, 0.25}}};
  const qa_q3_dynamic_light q3_lights[1] = {{{0, 0, 120}, 300, {1, 1, 1}, false}};
  qa_world_view_input input;
  memset(&input, 0, sizeof input);
  input.target_is_seat = false; input.target_preview_id = "t";
  input.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0};
  if (q2) { input.q2_styles = q2_styles; input.q2_style_count = 256; }
  else { input.q1_styles = q1_styles; input.q1_style_count = 256; }
  input.lights = lights; input.light_count = 1; input.has_lights = true;
  input.visibility.q3_lights = q3_lights; input.visibility.q3_light_count = 1;
  input.has_identity_light = true; input.identity_light = 0.5;

  pres_render_fixture *f = calloc(1, sizeof *f);
  CHECK(f != NULL);
  if (f == NULL) return;
  pres_render_fixture_init(f, q2, true);
  qa_model_light_sampler sampler;
  CHECK(qa_model_light_sampler_init(&sampler, &f->world));
  CHECK(sampler.world == &f->world);
  CHECK(!sampler.has_grid);
  /* Surface 1 has no lightmap and an ordinary material: TS unlitFaces. */
  CHECK(sampler.unlit_count == 1 && sampler.unlit_indices[0] == 1);
  const qa_vec3 points[4] = {{8, 8, 100}, {40, 24, 100}, {100, 100, 100}, {8, 8, -10}};
  for (int p = 0; p < 4; p++) {
    for (int dynamic = 0; dynamic < 2; dynamic++) {
      const pres_render_sample *want = &expected[p * 2 + dynamic];
      qa_model_light_sample got = qa_model_light_sampler_sample(&sampler, points[p], &input, dynamic == 0);
      CHECK(pres_render_vec3_eq(got.color, want->color));
      CHECK(got.has_floor == want->has_floor);
      if (want->has_floor) {
        CHECK(pres_render_vec3_eq(got.floor_point, want->floor_point));
        CHECK(pres_render_vec3_eq(got.floor_plane.normal, (qa_vec3){0, 0, 1}) && got.floor_plane.distance == 0);
      }
    }
  }
  qa_scene_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.transform.origin = (qa_vec3){8, 8, 100};
  entity.transform.axis = (qa_axis){{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  entity.transform.scale = (qa_vec3){1, 2, 1};
  entity.lighting_origin = (qa_vec3){40, 24, 100};
  entity.flags_kind = "q3";
  entity.flags_bits = 128;
  for (int i = 0; i < 2; i++) {
    qa_entity_lighting got = qa_model_light_sampler_entity_lighting(&sampler, &entity, &input, i == 1);
    CHECK(pres_render_vec3_eq(got.ambient_light, expected_entity[i].ambient_light));
    CHECK(pres_render_vec3_eq(got.directed_light, expected_entity[i].directed_light));
    CHECK(pres_render_vec3_eq(got.light_dir, expected_entity[i].light_dir));
    CHECK(got.ambient_light_int == expected_entity[i].ambient_light_int);
  }
  /* world.shaders.sun is read on each call: without it the default sun. */
  f->shaders.has_sun = false;
  qa_entity_lighting no_sun = qa_model_light_sampler_entity_lighting(&sampler, &entity, &input, true);
  CHECK(pres_render_vec3_eq(no_sun.light_dir, expected_entity[2].light_dir));
  CHECK(pres_render_vec3_eq(no_sun.directed_light, expected_entity[2].directed_light));
  CHECK(no_sun.ambient_light_int == expected_entity[2].ambient_light_int);
  qa_model_light_sampler_destroy(&sampler);
  free(f);
}

/* ModelRenderProvider whose textures and modelPolicy are getters, as TS
 * ProviderSceneAssets: the renderer re-reads textures before each preload
 * and drops its materials when the loader changed. */
typedef struct pres_render_getters {
  qa_scene_texture_loader *loaders[2];
  int current, texture_reads, policy_reads;
} pres_render_getters;
static pres_render_getters pres_render_getter_state;
static qa_scene_texture_loader *pres_render_textures_getter(void *ctx) {
  CHECK_SENTINEL(ctx);
  pres_render_getter_state.texture_reads++;
  return pres_render_getter_state.loaders[pres_render_getter_state.current];
}
static qa_model_replacement_policy pres_render_policy_getter(void *ctx) {
  CHECK_SENTINEL(ctx);
  pres_render_getter_state.policy_reads++;
  qa_model_replacement_policy policy;
  memset(&policy, 0, sizeof policy);
  return policy;
}

static void pres_render_provider_checks(void) {
  qa_scene_texture white[2], missing[2];
  memset(white, 0, sizeof white);
  memset(missing, 0, sizeof missing);
  white[0].name = "*white"; missing[0].name = "*missing";
  white[1].name = "*white2"; missing[1].name = "*missing2";
  qa_scene_texture_loader loaders[2];
  memset(loaders, 0, sizeof loaders);
  for (int i = 0; i < 2; i++) { loaders[i].white = &white[i]; loaders[i].missing = &missing[i]; }
  memset(&pres_render_getter_state, 0, sizeof pres_render_getter_state);
  pres_render_getter_state.loaders[0] = &loaders[0];
  pres_render_getter_state.loaders[1] = &loaders[1];

  pres_render_fixture *f = calloc(1, sizeof *f);
  CHECK(f != NULL);
  if (f == NULL) return;
  pres_render_fixture_init(f, false, false);
  qa_scene_shader_registry shaders;
  memset(&shaders, 0, sizeof shaders);
  qa_model_render_provider provider;
  memset(&provider, 0, sizeof provider);
  provider.family = QA_GAME_FAMILY_Q1;
  provider.palette = NULL;
  provider.shaders = &shaders;
  provider.getter_ctx = PRES_SENTINEL;
  provider.textures_getter = pres_render_textures_getter;
  provider.model_policy_getter = pres_render_policy_getter;
  CHECK(qa_model_render_provider_textures(&provider) == &loaders[0]);
  pres_render_getter_state.texture_reads = 0;

  qa_scene_model_renderer renderer;
  CHECK(qa_scene_model_renderer_init(&renderer, provider, &f->world));
  CHECK(renderer.world == &f->world && renderer.lighting.world == &f->world);
  CHECK(pres_render_getter_state.texture_reads == 1 && pres_render_getter_state.policy_reads == 0);
  qa_resolved_resource_reference resource;
  memset(&resource, 0, sizeof resource);
  resource.id = "q1:test/brush.bsp";
  qa_decoded_model model;
  memset(&model, 0, sizeof model);
  model.kind = QA_DECODED_MODEL_BRUSH_MODEL;
  qa_model_source_options options;
  memset(&options, 0, sizeof options);
  qa_scene_model_renderer_preload_model(&renderer, &resource, &model, options, true);
  CHECK(pres_render_getter_state.texture_reads == 5);
  CHECK(renderer.material_count == 3);
  CHECK(renderer.textures == &loaders[0]);
  pres_render_getter_state.current = 1;
  qa_scene_model_renderer_preload_model(&renderer, &resource, &model, options, true);
  CHECK(pres_render_getter_state.texture_reads == 10);
  CHECK(renderer.material_count == 3);
  CHECK(renderer.textures == &loaders[1]);
  qa_world_view_input input;
  memset(&input, 0, sizeof input);
  input.target_is_seat = false; input.target_preview_id = "t";
  input.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0};
  input.camera.axis = (qa_axis){{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  size_t group_count = 1;
  qa_scene_model_group *groups = qa_scene_model_renderer_prepare(&renderer, NULL, 0, &input, NULL, NULL, NULL, &group_count);
  CHECK(group_count == 0);
  free(groups);
  /* TS reads provider.modelPolicy only per prepared entity. */
  CHECK(pres_render_getter_state.texture_reads == 10 && pres_render_getter_state.policy_reads == 0);
  qa_scene_model_renderer_destroy(&renderer);
  free(f);
}

static void pres_render_tests(void) {
  pres_render_sampler_checks(false, pres_render_q1_samples, pres_render_q1_entity);
  pres_render_sampler_checks(true, pres_render_q2_samples, pres_render_q2_entity);
  pres_render_provider_checks();
}
