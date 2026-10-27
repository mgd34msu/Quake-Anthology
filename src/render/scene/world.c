/* Port of src/render/scene/world.ts. GPL-2.0-or-later. */
#include "qa/render/scene/world.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/materials/dlight.h"
#include "qa/materials/legacy.h"
#include "qa/render/scene/geometry.h"
#include "qa/render/scene/q1_fog.h"
#include "qa/render/scene/resources.h"
#include "qa/render/scene/shadow_geometry.h"
static float fr(double v) { return (float)v; }
static void *xalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Allocation failed"); return p; }
static void *xg(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) qa_range_error("Allocation failed"); return q; }
static char *xdup(const char *s) { size_t n = strlen(s) + 1; char *p = xalloc(n); memcpy(p, s, n); return p; }
static int is_str(const char *a, const char *b) { return a && !strcmp(a, b); }
static int32_t i32d(double v) { if (!isfinite(v)) return 0; double m = fmod(trunc(v), 4294967296.0); if (m < 0) m += 4294967296.0; if (m >= 2147483648.0) m -= 4294967296.0; return (int32_t)m; }
static char *material_name(const char *name) { char *s = qa_strip_shader_extension(name); char *n = qa_normalize_shader_name(s); free(s); return n; }
/* owned operation storage */
typedef struct op_storage {
  qa_scene_operation *ops; size_t count, cap;
  qa_draw_batch **batch_lists; size_t *batch_counts_f; size_t nlists, clists; /* qa_mat_draw_batches_destroy targets */
  qa_material_vertex **geom_vertices; double **geom_indices; size_t ngeom, cgeom;
  qa_render_vertex *sky_vertices; qa_render_operation *extra_ops; size_t nextra;
  void **scratch; size_t nscratch, cscratch;
  qa_sky_geometry *sky_geoms; size_t nsky;
  qa_q2_sky_operations *sky_ops; size_t nsky_ops, csky_ops;
  qa_scene_group *groups; size_t ngroups; /* borrowed type tags only */
  qa_patch_mesh *lod_meshes; size_t nlod, clod;
  qa_batch_lighting *lightings; size_t nlight, clight;
  qa_vec3 **world_arrays; size_t nwa, cwa;
} op_storage;
static op_storage *op_storage_new(void) { op_storage *s = xalloc(sizeof *s); memset(s, 0, sizeof *s); return s; }
static void op_storage_destroy(op_storage *s) {
  if (!s) return;
  for (size_t i = 0; i < s->nlists; i++) qa_mat_draw_batches_destroy(s->batch_lists[i], s->batch_counts_f[i]);
  free(s->batch_lists); free(s->batch_counts_f);
  for (size_t i = 0; i < s->ngeom; i++) { free(s->geom_vertices[i]); free(s->geom_indices[i]); }
  free(s->geom_vertices); free(s->geom_indices);
  free(s->sky_vertices); free(s->extra_ops);
  for (size_t i = 0; i < s->nscratch; i++) free(s->scratch[i]);
  free(s->scratch);
  for (size_t i = 0; i < s->nsky; i++) qa_sky_geometry_destroy(&s->sky_geoms[i]);
  free(s->sky_geoms);
  for (size_t i = 0; i < s->nsky_ops; i++) qa_q2_sky_operations_destroy(&s->sky_ops[i]);
  free(s->sky_ops); free(s->groups);
  for (size_t i = 0; i < s->nlod; i++) qa_patch_mesh_destroy(&s->lod_meshes[i]);
  free(s->lod_meshes); free(s->lightings);
  for (size_t i = 0; i < s->nwa; i++) free(s->world_arrays[i]);
  free(s->world_arrays);
  for (size_t i = 0; i < s->count; i++) if (s->ops[i].is_group) qa_scene_group_destroy(&s->ops[i].group);
  free(s->ops); free(s);
}
static void op_push(op_storage *s, qa_scene_operation op) {
  if (s->count == s->cap) { s->cap = s->cap ? s->cap * 2 : 16; s->ops = xg(s->ops, s->cap * sizeof *s->ops); }
  s->ops[s->count++] = op;
}
static void op_push_group(op_storage *s, qa_scene_group group) { qa_scene_operation o; memset(&o, 0, sizeof o); o.is_group = 1; o.group = group; op_push(s, o); }
static void op_push_plain(op_storage *s, qa_render_operation operation) { qa_scene_operation o; memset(&o, 0, sizeof o); o.operation = operation; op_push(s, o); }
static void track_batches(op_storage *s, qa_draw_batch *batches, size_t count) {
  if (!batches && !count) return;
  if (s->nlists == s->clists) { s->clists = s->clists ? s->clists * 2 : 8; s->batch_lists = xg(s->batch_lists, s->clists * sizeof(void *)); s->batch_counts_f = xg(s->batch_counts_f, s->clists * sizeof(size_t)); }
  s->batch_lists[s->nlists] = batches; s->batch_counts_f[s->nlists] = count; s->nlists++;
}
static void track_scratch(op_storage *s, void *p) { if (s->nscratch == s->cscratch) { s->cscratch = s->cscratch ? s->cscratch * 2 : 16; s->scratch = xg(s->scratch, s->cscratch * sizeof(void *)); } s->scratch[s->nscratch++] = p; }
void qa_world_operations_destroy(qa_world_operations *o) { if (!o) return; op_storage_destroy(o->_storage); memset(o, 0, sizeof *o); }
typedef struct wimpl {
  qa_q2_shadow_scene shadow_scene;
  qa_renderer_image *owned; size_t nowned, cowned;
  qa_renderer_image *q2_sky; size_t q2_sky_count;
  struct { double ordinal; const qa_renderer_image *fullbright; } *fb_map; size_t nfb, cfb;
  /* raw remaps by surface index */
  struct { double index; qa_scene_material_remap remap; } *raw; size_t nraw, craw;
  int closed; double image_generation;
  int has_prepared_generation; double prepared_generation;
  int has_prepared_revision; double prepared_revision;
  double shadow_material_revision;
  /* static light styles per surface */
  struct style_entry { double index; double *styles; size_t count; } *styles; size_t nstyles, cstyles;
  /* caches */
  struct cached_world_arrays { const qa_material_vertex *key; qa_vec3 *positions; qa_vec3 *normals; } *warrays; size_t nwa, cwa;
  struct batch_cache { double index; char *key; op_storage *storage; } *legacy_cache; size_t nlc, clc;
  struct frag_cache { double index; char *key; qa_draw_batch *batches; size_t count; op_storage *storage; } *frag_cache; size_t nfc, cfc;
  const qa_q2_fragment_light *frag_lights_key; qa_q2_fragment_light *frag_all, *frag_shadow; size_t frag_n;
  struct { const qa_world_surface *surfaces; double first, count; qa_static_shadow_world world; int valid; } static_shadow;
  /* load-time owned geometry */
  qa_material_vertex **owned_verts; double **owned_indices; size_t nov, cov;
  qa_patch_mesh *patch_meshes; size_t npatch, cpatch;
  qa_patch_grid *grids; size_t ngrid_store, cgrid_store;
  qa_prepared_brush_face *brush_faces; size_t nbrush, cbrush;
  qa_world_surface_lightmap *lightmaps; size_t nlm, clm;
  char **names; size_t nname, cname;
  qa_q1_texture_animations *animations; size_t nanim, canim;
  qa_built_lightmap *built; size_t nbuilt, cbuilt;
  qa_split_sky_texture *splits; size_t nsplit, csplit;
  qa_fog_volume *fog_volumes; size_t nfog, cfog;
  void **scratch; size_t nscratch, cscratch;
  qa_world_scene *self;
} wimpl;
static wimpl *wi(qa_world_scene *s) { return (wimpl *)s->_impl; }
static void w_scratch(wimpl *o, void *p) { if (o->nscratch == o->cscratch) { o->cscratch = o->cscratch ? o->cscratch * 2 : 16; o->scratch = xg(o->scratch, o->cscratch * sizeof(void *)); } o->scratch[o->nscratch++] = p; }
static void w_owned(wimpl *o, qa_renderer_image image) { if (o->nowned == o->cowned) { o->cowned = o->cowned ? o->cowned * 2 : 16; o->owned = xg(o->owned, o->cowned * sizeof *o->owned); } o->owned[o->nowned++] = image; }
static double q2mod(qa_world_scene *s) { return s->options.has_q2_light_modulate ? s->options.q2_light_modulate : 1; }
static const qa_renderer_image *fb_lookup(qa_world_scene *s, qa_renderer_image key, int *present) {
  wimpl *o = wi(s); *present = 0;
  for (size_t i = 0; i < o->nfb; i++) if (o->fb_map[i].ordinal == key.ordinal) { *present = 1; return o->fb_map[i].fullbright; }
  return NULL;
}
static const qa_q2_fragment_light *fragment_lights(qa_world_scene *s, const qa_q2_fragment_light *lights, size_t count, int shadow_only, size_t *out_count) {
  wimpl *o = wi(s);
  if (o->frag_lights_key != lights) {
    free(o->frag_all); free(o->frag_shadow); o->frag_all = NULL; o->frag_shadow = NULL;
    o->frag_lights_key = lights; o->frag_n = count;
  }
  double modulate = q2mod(s);
  if (shadow_only) {
    if (!o->frag_shadow && count) {
      o->frag_shadow = xalloc(count * sizeof *o->frag_shadow); size_t n = 0;
      for (size_t i = 0; i < count; i++) if (lights[i].cone || !is_str(lights[i].shadow.kind, "none")) { o->frag_shadow[n] = lights[i]; o->frag_shadow[n].scale = lights[i].scale * modulate; n++; }
      o->frag_n = n;
      *out_count = n; return o->frag_shadow;
    }
    *out_count = o->frag_shadow ? o->frag_n : 0; return o->frag_shadow;
  }
  if (!o->frag_all && count) {
    o->frag_all = xalloc(count * sizeof *o->frag_all);
    for (size_t i = 0; i < count; i++) { o->frag_all[i] = lights[i]; o->frag_all[i].scale = lights[i].scale * modulate; }
  }
  *out_count = count; return o->frag_all;
}
typedef struct world_arrays { qa_vec3 *positions; qa_vec3 *normals; size_t count; } world_arrays;
static void build_world_arrays(qa_world_scene *s, qa_material_geometry geometry, const qa_scene_model_transform *model, op_storage *store, world_arrays *out) {
  wimpl *o = wi(s);
  out->count = geometry.vertex_count;
  if (model) {
    qa_vec3 *pos = xalloc((out->count ? out->count : 1) * sizeof *pos), *nor = xalloc((out->count ? out->count : 1) * sizeof *nor);
    if (store) { track_scratch(store, pos); track_scratch(store, nor); }
    for (size_t i = 0; i < out->count; i++) { pos[i] = qa_scene_world_point(geometry.vertices[i].position, *model); nor[i] = qa_normalize3(qa_scene_world_vector(geometry.vertices[i].normal, *model)); }
    out->positions = pos; out->normals = nor; return;
  }
  for (size_t i = 0; i < o->nwa; i++) if (o->warrays[i].key == geometry.vertices) { out->positions = o->warrays[i].positions; out->normals = o->warrays[i].normals; return; }
  qa_vec3 *pos = xalloc((out->count ? out->count : 1) * sizeof *pos), *nor = xalloc((out->count ? out->count : 1) * sizeof *nor);
  for (size_t i = 0; i < out->count; i++) { pos[i] = geometry.vertices[i].position; nor[i] = qa_normalize3(geometry.vertices[i].normal); }
  if (o->nwa == o->cwa) { o->cwa = o->cwa ? o->cwa * 2 : 8; o->warrays = xg(o->warrays, o->cwa * sizeof *o->warrays); }
  o->warrays[o->nwa].key = geometry.vertices; o->warrays[o->nwa].positions = pos; o->warrays[o->nwa].normals = nor; o->nwa++;
  out->positions = pos; out->normals = nor;
}
static qa_image_level q3_lightmap(wimpl *o, qa_byte_view bytes, int shift) {
  if (bytes.length != 128 * 128 * 3) qa_range_error("Q3 lightmap must contain 128x128 RGB samples");
  uint8_t *pixels = xalloc(128 * 128 * 4); w_scratch(o, pixels);
  for (int pixel = 0; pixel < 128 * 128; pixel++) {
    int r = bytes.bytes[pixel*3] << shift, g = bytes.bytes[pixel*3+1] << shift, b = bytes.bytes[pixel*3+2] << shift;
    int m = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (m > 255) { r = (int)(r * 255 / m); g = (int)(g * 255 / m); b = (int)(b * 255 / m); }
    pixels[pixel*4] = (uint8_t)r; pixels[pixel*4+1] = (uint8_t)g; pixels[pixel*4+2] = (uint8_t)b; pixels[pixel*4+3] = 255;
  }
  return (qa_image_level){ 128, 128, { pixels, 128 * 128 * 4 } };
}
static void scene_ctor(qa_world_scene *scene, qa_decoded_world map, qa_scene_shader_registry *shaders, qa_world_scene_options options) {
  scene->map = map; scene->shaders = shaders; scene->options = options;
  wimpl *o = xalloc(sizeof *o); memset(o, 0, sizeof *o); o->self = scene; o->shadow_material_revision = -1;
  scene->_impl = o;
  scene->material_world = qa_scene_material_registrations_world(shaders->registrations->owner, map);
  qa_q2_shadow_scene_init(&o->shadow_scene, shaders->textures->images);
  qa_world_visibility_cache_init(&scene->visibility_cache);
  /* bounds: models[0] or vertex bounds */
  int have = 0;
  if (map.kind == QA_DECODED_WORLD_Q3) {
    if (map.as.q3->model_count) { scene->bounds = map.as.q3->models[0].bounds; have = 1; }
    else scene->bounds = qa_scene_geometry_bounds_vertices(map.as.q3->vertices, map.as.q3->vertex_count);
  } else if (map.kind == QA_DECODED_WORLD_Q1) {
    if (map.as.q1->model_count) { scene->bounds = map.as.q1->models[0].bounds; have = 1; }
    else scene->bounds = qa_scene_geometry_bounds(map.as.q1->vertices, map.as.q1->vertex_count);
  } else {
    if (map.as.q2->model_count) { scene->bounds = map.as.q2->models[0].bounds; have = 1; }
    else scene->bounds = qa_scene_geometry_bounds(map.as.q2->vertices, map.as.q2->vertex_count);
  }
  (void)have;
  qa_renderer_noise_init(&scene->noise);
  qa_image_level fog_level = qa_create_fog_texture(NULL, 0);
  w_scratch(o, (void *)fog_level.pixels.bytes);
  qa_image_level *fog_levels = xalloc(sizeof *fog_levels); *fog_levels = fog_level; w_scratch(o, fog_levels);
  qa_render_image fog_content; memset(&fog_content, 0, sizeof fog_content);
  fog_content.kind = QA_RENDER_IMAGE_RGBA8; fog_content.levels = fog_levels; fog_content.level_count = 1;
  qa_texture_sampling clamp_linear = { "clamp", "linear" };
  scene->fog_image = qa_scene_image_registry_register_generated(shaders->textures->images, "*fog", fog_content, clamp_linear)->image;
  w_owned(o, scene->fog_image);
  uint8_t *dlight = xalloc(16 * 16 * 4); w_scratch(o, dlight);
  for (int x = 0; x < 16; x++) for (int y = 0; y < 16; y++) {
    double distance = (7.5 - x) * (7.5 - x) + (7.5 - y) * (7.5 - y);
    int brightness = (int)fr(4000 / distance);
    if (brightness > 255) brightness = 255; else if (brightness < 75) brightness = 0;
    uint8_t *dst = dlight + (y * 16 + x) * 4;
    dst[0] = (uint8_t)brightness; dst[1] = (uint8_t)brightness; dst[2] = (uint8_t)brightness; dst[3] = 255;
  }
  qa_image_level *dl_levels = xalloc(sizeof *dl_levels); w_scratch(o, dl_levels);
  *dl_levels = (qa_image_level){ 16, 16, { dlight, 16 * 16 * 4 } };
  qa_render_image dl_content; memset(&dl_content, 0, sizeof dl_content);
  dl_content.kind = QA_RENDER_IMAGE_RGBA8; dl_content.levels = dl_levels; dl_content.level_count = 1;
  scene->dlight_image = qa_scene_image_registry_register_generated(shaders->textures->images, "*dlight", dl_content, clamp_linear)->image;
  w_owned(o, scene->dlight_image);
}
static qa_renderer_image generated_image(qa_world_scene *scene, wimpl *o, const char *name, qa_image_level level, const char *wrap) {
  qa_image_level *levels = xalloc(sizeof *levels); *levels = level; w_scratch(o, levels);
  qa_render_image content; memset(&content, 0, sizeof content);
  content.kind = QA_RENDER_IMAGE_RGBA8; content.levels = levels; content.level_count = 1;
  qa_texture_sampling samp = { wrap, "linear" };
  qa_renderer_image image = qa_scene_image_registry_register_generated(scene->shaders->textures->images, name, content, samp)->image;
  w_owned(o, image);
  return image;
}
static const double Q1_STYLES[256] = { 0 };
static void fill_defaults(void) { /* initialized lazily below */ (void)Q1_STYLES; }
static const double *q1_defaults(void) { static double values[256]; static int done = 0; if (!done) { for (int i = 0; i < 256; i++) values[i] = 256; done = 1; } return values; }
static const qa_q2_light_style *q2_defaults(void) { static qa_q2_light_style values[256]; static int done = 0; if (!done) { for (int i = 0; i < 256; i++) values[i] = (qa_q2_light_style){ {1,1,1}, 3 }; done = 1; } return values; }
void qa_world_scene_load(qa_decoded_world map, qa_scene_shader_registry *shaders, const qa_world_scene_options *options_in, qa_world_scene *out) {
  (void)fill_defaults;
  memset(out, 0, sizeof *out);
  qa_world_scene_options options; memset(&options, 0, sizeof options);
  if (options_in) options = *options_in;
  if (!options.q1_lightmap_encoding) options.q1_lightmap_encoding = "rgb";
  scene_ctor(out, map, shaders, options);
  wimpl *o = wi(out);
  qa_scene_image_registry *images = shaders->textures->images; (void)images;
  qa_shader_world_identity *material_world = out->material_world;
  qa_world_surface *surfaces = NULL; size_t nsurf = 0, csurf = 0;
  #define PUSH_SURF(s) do { if (nsurf == csurf) { csurf = csurf ? csurf * 2 : 16; surfaces = xg(surfaces, csurf * sizeof *surfaces); } surfaces[nsurf++] = (s); } while (0)
  if (map.kind == QA_DECODED_WORLD_Q3) {
    const qa_q3_world_geometry *q3 = map.as.q3;
    qa_renderer_image *lightmaps = xalloc((q3->lightmap_count ? q3->lightmap_count : 1) * sizeof *lightmaps); w_scratch(o, lightmaps);
    for (size_t i = 0; i < q3->lightmap_count; i++) {
      char name[64]; snprintf(name, sizeof name, "*q3-lightmap-%zu", i);
      char *ncopy = xdup(name); w_scratch(o, ncopy);
      lightmaps[i] = generated_image(out, o, ncopy, q3_lightmap(o, q3->lightmaps[i], options.has_q3_lightmap_overbright ? (int)options.q3_lightmap_overbright : 2), "clamp");
    }
    const qa_fog_volume **fogs = xalloc((q3->fog_count ? q3->fog_count : 1) * sizeof(void *)); w_scratch(o, (void *)fogs);
    for (size_t i = 0; i < q3->fog_count; i++) {
      qa_registered_scene_material material = qa_scene_shader_registry_register(shaders, q3->fogs[i].shader, NULL);
      const qa_render_material *mat = qa_registered_scene_material_material(material);
      if (q3->fogs[i].brush < 0 || !mat->fog) { fogs[i] = NULL; continue; }
      qa_fog_brush_map fmap; memset(&fmap, 0, sizeof fmap);
      fmap.fogs = (const qa_fog_brush_map_entry *)q3->fogs;
      fmap.fog_count = q3->fog_count;
      qa_fog_brush_entry *brushes = xalloc((q3->brush_count ? q3->brush_count : 1) * sizeof *brushes); w_scratch(o, brushes);
      for (size_t k = 0; k < q3->brush_count; k++) brushes[k] = (qa_fog_brush_entry){ q3->brushes[k].sides.first, q3->brushes[k].sides.count };
      fmap.brushes = brushes; fmap.brush_count = q3->brush_count;
      qa_fog_brush_side_entry *sides = xalloc((q3->brush_side_count ? q3->brush_side_count : 1) * sizeof *sides); w_scratch(o, sides);
      for (size_t k = 0; k < q3->brush_side_count; k++) sides[k] = (qa_fog_brush_side_entry){ q3->brush_sides[k].plane };
      fmap.brush_sides = sides; fmap.brush_side_count = q3->brush_side_count;
      qa_plane *planes = xalloc((q3->plane_count ? q3->plane_count : 1) * sizeof *planes); w_scratch(o, planes);
      for (size_t k = 0; k < q3->plane_count; k++) planes[k] = q3->planes[k].plane;
      fmap.planes = planes; fmap.plane_count = q3->plane_count;
      if (o->nfog == o->cfog) { o->cfog = o->cfog ? o->cfog * 2 : 4; o->fog_volumes = xg(o->fog_volumes, o->cfog * sizeof *o->fog_volumes); }
      o->fog_volumes[o->nfog] = qa_prepare_fog_volume(fmap, (double)i, *mat->fog);
      fogs[i] = &o->fog_volumes[o->nfog++];
    }
    out->fog_selections = xalloc((q3->fog_count ? q3->fog_count : 1) * sizeof *out->fog_selections);
    size_t nf = 0;
    for (size_t i = 0; i < q3->fog_count; i++) if (fogs[i]) out->fog_selections[nf++] = (qa_fog_selection){ (double)i, *fogs[i] };
    out->fog_selection_count = nf;
    size_t *patch_ordinals = NULL; size_t npatch_ord = 0, cpatch_ord = 0;
    for (size_t index = 0; index < q3->surface_count; index++) {
      const qa_q3_bsp_surface *surface = &q3->surfaces[index];
      long shi = (long)surface->shader;
      if (surface->shader != (double)shi || shi < 0 || (size_t)shi >= q3->shader_count) qa_range_error("Scene world index %g outside %zu", surface->shader, q3->shader_count);
      const qa_q3_bsp_shader *shader = &q3->shaders[shi];
      size_t vfirst = (size_t)surface->vertices.first, vcount = (size_t)surface->vertices.count;
      qa_material_vertex *vertices = xalloc((vcount ? vcount : 1) * sizeof *vertices);
      int shift = options.has_q3_lightmap_overbright ? (int)options.q3_lightmap_overbright : 2;
      for (size_t k = 0; k < vcount; k++) {
        vertices[k] = q3->vertices[vfirst + k];
        int r = (int)vertices[k].color.x << shift, g = (int)vertices[k].color.y << shift, b = (int)vertices[k].color.z << shift;
        int m = r > g ? (r > b ? r : b) : (g > b ? g : b);
        vertices[k].color = m > 255 ? qa_math_vec4((int)(r * 255 / m), (int)(g * 255 / m), (int)(b * 255 / m), vertices[k].color.w)
                                    : qa_math_vec4(r, g, b, vertices[k].color.w);
      }
      size_t ifirst = (size_t)surface->indices.first, icount = (size_t)surface->indices.count;
      double *indices = xalloc((icount ? icount : 1) * sizeof *indices);
      memcpy(indices, q3->indices.values + ifirst, icount * sizeof(double));
      if (o->nov == o->cov) { o->cov = o->cov ? o->cov * 2 : 16; o->owned_verts = xg(o->owned_verts, o->cov * sizeof(void *)); o->owned_indices = xg(o->owned_indices, o->cov * sizeof(void *)); }
      o->owned_verts[o->nov] = vertices; o->owned_indices[o->nov] = indices; o->nov++;
      qa_material_geometry geometry = { vertices, vcount, indices, icount };
      int has_plane = 0; qa_plane plane; memset(&plane, 0, sizeof plane);
      qa_patch_grid *grid = NULL;
      if (is_str(surface->kind, "patch") && (i32d(shader->surface_flags) & 0x80) == 0) {
        if (o->npatch == o->cpatch) { o->cpatch = o->cpatch ? o->cpatch * 2 : 8; o->patch_meshes = xg(o->patch_meshes, o->cpatch * sizeof *o->patch_meshes); }
        qa_patch_mesh *mesh = &o->patch_meshes[o->npatch++];
        qa_tessellate_patch(vertices, vcount, surface->width, surface->height, options.has_subdivisions ? options.subdivisions : 4, mesh);
        qa_vec3 bounds2[2] = { surface->lightmap.vectors[0], surface->lightmap.vectors[1] };
        if (o->ngrid_store == o->cgrid_store) { o->cgrid_store = o->cgrid_store ? o->cgrid_store * 2 : 8; o->grids = xg(o->grids, o->cgrid_store * sizeof *o->grids); }
        grid = &o->grids[o->ngrid_store++];
        *grid = qa_create_patch_grid(*mesh, bounds2);
        if (npatch_ord == cpatch_ord) { cpatch_ord = cpatch_ord ? cpatch_ord * 2 : 8; patch_ordinals = xg(patch_ordinals, cpatch_ord * sizeof(size_t)); }
        patch_ordinals[npatch_ord++] = index;
      } else if (is_str(surface->kind, "planar")) {
        has_plane = 1; plane.normal = surface->lightmap.vectors[2];
        plane.distance = vcount ? qa_dot3(vertices[0].position, surface->lightmap.vectors[2]) : 0;
      } else if (vcount >= 3 && icount >= 3) {
        qa_vec3 a = vertices[(size_t)indices[0]].position;
        qa_vec3 normal = qa_normalize3(qa_cross3(qa_sub3(vertices[(size_t)indices[2]].position, a), qa_sub3(vertices[(size_t)indices[1]].position, a)));
        has_plane = 1; plane = (qa_plane){ normal, qa_dot3(a, normal) };
      }
      double lightmap_index = (is_str(surface->kind, "planar") || is_str(surface->kind, "patch")) ? surface->lightmap.image : -3;
      qa_scene_shader_binding binding; memset(&binding, 0, sizeof binding);
      binding.is_world = true; binding.world = material_world; binding.lightmap_index = lightmap_index;
      long lmi = (long)lightmap_index;
      qa_renderer_image lm_image; const qa_renderer_image *lm_ptr = NULL;
      if (lightmap_index == (double)lmi && lmi >= 0 && (size_t)lmi < q3->lightmap_count) { lm_image = lightmaps[lmi]; lm_ptr = NULL; binding.has_lightmap = true; binding.lightmap = lm_image; }
      qa_registered_scene_material material = qa_scene_shader_registry_source_world_material(shaders,
        qa_scene_shader_registry_register(shaders, shader->name, &binding));
      if (binding.has_lightmap) lm_ptr = (void *)0; /* marker; store pointer below */
      qa_material_geometry actual = grid ? (qa_material_geometry){ grid->mesh.vertices, grid->mesh.vertex_count, grid->mesh.indices, grid->mesh.index_count } : geometry;
      qa_world_surface surf; memset(&surf, 0, sizeof surf);
      char *sname = xdup(shader->name); w_scratch(o, sname);
      surf.shader_name = sname; surf.index = (double)index;
      surf.bounds = qa_scene_geometry_bounds_vertices(actual.vertices, actual.vertex_count);
      surf.has_plane = has_plane; surf.plane = plane; surf.geometry = actual;
      surf.is_q3 = true; surf.shader = material; surf.has_shader = true;
      if (binding.has_lightmap) {
        qa_renderer_image *store = xalloc(sizeof *store); *store = lm_image; w_scratch(o, store);
        surf.lightmap_image = store;
      }
      (void)lm_ptr;
      surf.grid = grid;
      surf.fog = surface->fog < 0 ? NULL : ((size_t)surface->fog < q3->fog_count ? fogs[(size_t)surface->fog] : NULL);
      surf.fog_index = surface->fog;
      surf.flare = is_str(surface->kind, "flare");
      surf.skip = is_str(surface->kind, "patch") && (i32d(shader->surface_flags) & 0x80) != 0;
      PUSH_SURF(surf);
    }
    /* stitch patches (grids are contiguous in grid store order of use) */
    if (npatch_ord) {
      qa_patch_grid *patches = xalloc(npatch_ord * sizeof *patches);
      for (size_t i = 0; i < npatch_ord; i++) patches[i] = *surfaces[patch_ordinals[i]].grid;
      qa_prepare_patch_grids(patches, npatch_ord, NULL, NULL, NULL, NULL);
      for (size_t i = 0; i < npatch_ord; i++) {
        qa_world_surface *surf = &surfaces[patch_ordinals[i]];
        *surf->grid = patches[i];
        surf->geometry = (qa_material_geometry){ surf->grid->mesh.vertices, surf->grid->mesh.vertex_count, surf->grid->mesh.indices, surf->grid->mesh.index_count };
        surf->bounds = qa_scene_geometry_bounds_vertices(surf->geometry.vertices, surf->geometry.vertex_count);
      }
      free(patches);
    }
    free(patch_ordinals);
  } else {
    /* legacy Q1/Q2 */
    int is_q1 = map.kind == QA_DECODED_WORLD_Q1;
    size_t face_count = is_q1 ? map.as.q1->face_count : map.as.q2->face_count;
    size_t info_count = is_q1 ? map.as.q1->texture_count : map.as.q2->texture_info_count;
    /* textures: Q1 by texture slot, Q2 by textureInfo slot */
    size_t ntexslots = is_q1 ? map.as.q1->texture_count : map.as.q2->texture_info_count;
    const qa_scene_texture **textures = xalloc((ntexslots ? ntexslots : 1) * sizeof(void *)); w_scratch(o, (void *)textures);
    qa_legacy_texture_entry *entries = xalloc((ntexslots ? ntexslots : 1) * sizeof *entries); w_scratch(o, entries);
    if (is_q1) {
      for (size_t i = 0; i < ntexslots; i++) {
        const qa_q1_mip_texture *texture = map.as.q1->textures[i];
        if (!texture) { textures[i] = shaders->textures->missing; }
        else {
          char *path = xalloc(strlen(texture->name) + 10); sprintf(path, "textures/%s", texture->name); w_scratch(o, path);
          qa_scene_texture_load_options lo; memset(&lo, 0, sizeof lo); lo.family = "q1";
          const qa_scene_texture *t = qa_scene_texture_loader_load(shaders->textures, path, &lo);
          if (!t) t = qa_scene_texture_loader_q1_embedded(shaders->textures, *texture);
          textures[i] = t ? t : shaders->textures->missing;
        }
        entries[i].name = textures[i]->name; entries[i].image = textures[i]->image;
      }
    } else {
      for (size_t i = 0; i < ntexslots; i++) {
        const char *nm = map.as.q2->texture_info[i].surface.name;
        char *path = xalloc(strlen(nm) + 10); sprintf(path, "textures/%s", nm); w_scratch(o, path);
        qa_scene_texture_load_options lo; memset(&lo, 0, sizeof lo); lo.family = "q2"; lo.usage = "wall";
        const qa_scene_texture *t = qa_scene_texture_loader_load(shaders->textures, path, &lo);
        textures[i] = t ? t : shaders->textures->missing;
        entries[i].name = textures[i]->name; entries[i].image = textures[i]->image;
      }
    }
    (void)info_count;
    for (size_t i = 0; i < ntexslots; i++) {
      if (o->nfb == o->cfb) { o->cfb = o->cfb ? o->cfb * 2 : 16; o->fb_map = xg(o->fb_map, o->cfb * sizeof *o->fb_map); }
      o->fb_map[o->nfb].ordinal = textures[i]->image.ordinal;
      o->fb_map[o->nfb].fullbright = textures[i]->fullbright; o->nfb++;
    }
    /* sky layers keyed by image ordinal */
    struct sky_pair { double ordinal; qa_renderer_image solid, overlay; } *sky_pairs = NULL; size_t nsky = 0, csky = 0;
    for (size_t index = 0; index < face_count; index++) {
      qa_bsp_face face = is_q1 ? map.as.q1->faces[index] : map.as.q2->faces[index];
      long tii = (long)face.texture_info;
      const qa_scene_texture *texture;
      const char *name;
      double info_flags = 0; int has_tex_index = 0; double tex_index = 0;
      if (is_q1) {
        qa_q1_texture_info info = map.as.q1->texture_info[tii];
        info_flags = info.flags; has_tex_index = 1; tex_index = info.texture;
        texture = textures[(size_t)tex_index];
        const qa_q1_mip_texture *orig = map.as.q1->textures[(size_t)tex_index];
        name = orig ? orig->name : texture->name;
      } else {
        qa_q2_texture_info info = map.as.q2->texture_info[tii];
        info_flags = info.surface.flags;
        texture = textures[tii];
        name = info.surface.name;
      }
      int warp = is_q1 ? (name[0] == '*') : (i32d(info_flags) & 8) != 0;
      int sky = is_q1 ? (!strncmp(name, "sky", 3)) : (i32d(info_flags) & 4) != 0;
      const qa_q1_mip_texture *original = (is_q1 && has_tex_index) ? map.as.q1->textures[(size_t)tex_index] : NULL;
      double texture_scale = (original && original->has_quake64_shift && original->quake64_shift > 0) ? 2 * original->quake64_shift : 1;
      qa_vec2 image_size = { original ? original->width : texture->width, original ? original->height : texture->height };
      if (o->nbrush == o->cbrush) { o->cbrush = o->cbrush ? o->cbrush * 2 : 16; o->brush_faces = xg(o->brush_faces, o->cbrush * sizeof *o->brush_faces); }
      qa_prepared_brush_face *prepared = &o->brush_faces[o->nbrush];
      qa_prepare_brush_face(is_q1, is_q1 ? map.as.q1 : NULL, is_q1 ? NULL : map.as.q2, face, (double)index, image_size, warp, is_q1 ? 128 : 64, prepared);
      o->nbrush++;
      qa_material_geometry geometry = prepared->geometry;
      qa_material_vertex *scaled_vertices = NULL;
      if (!warp && !sky && texture_scale != 1) {
        scaled_vertices = xalloc((geometry.vertex_count ? geometry.vertex_count : 1) * sizeof *scaled_vertices); w_scratch(o, scaled_vertices);
        for (size_t k = 0; k < geometry.vertex_count; k++) { scaled_vertices[k] = geometry.vertices[k]; scaled_vertices[k].tex_coord.x /= texture_scale; scaled_vertices[k].tex_coord.y /= texture_scale; }
        geometry.vertices = scaled_vertices;
      }
      qa_world_surface_lightmap *lightmap = NULL;
      qa_surface_lighting lighting; memset(&lighting, 0, sizeof lighting); lighting.kind = "unlit";
      if (!sky && !warp && prepared->lightmap.lighting) {
        qa_built_lightmap built;
        if (is_q1) { qa_build_q1_options bo; memset(&bo, 0, sizeof bo); bo.encoding = options.q1_lightmap_encoding; built = qa_build_q1_lightmap(prepared->lightmap, q1_defaults(), 256, bo); }
        else { qa_build_q2_options bo; memset(&bo, 0, sizeof bo); bo.has_modulate = true; bo.modulate = options.has_q2_light_modulate ? options.q2_light_modulate : 1; built = qa_build_q2_lightmap(prepared->lightmap, q2_defaults(), 256, bo); }
        char n1[80], n2[80];
        snprintf(n1, sizeof n1, "*%s-lightmap-%zu", is_q1 ? "q1-bsp" : "q2-bsp", index);
        snprintf(n2, sizeof n2, "*%s-direct-lightmap-%zu", is_q1 ? "q1-bsp" : "q2-bsp", index);
        char *name1 = xdup(n1), *name2 = xdup(n2); w_scratch(o, name1); w_scratch(o, name2);
        qa_image_level built_image = built.image;
        qa_image_level direct_level = qa_direct_lightmap_pixels(built);
        qa_renderer_image image = generated_image(out, o, name1, built_image, "clamp");
        qa_renderer_image direct = generated_image(out, o, name2, direct_level, "clamp");
        if (o->nlm == o->clm) { o->clm = o->clm ? o->clm * 2 : 16; o->lightmaps = xg(o->lightmaps, o->clm * sizeof *o->lightmaps); }
        lightmap = &o->lightmaps[o->nlm++];
        lightmap->face = prepared->lightmap; lightmap->image = image; lightmap->direct = direct;
        lightmap->encoding = is_q1 ? options.q1_lightmap_encoding : "rgb";
        const qa_decoupled_lightmap *dec = is_q1 ? map.as.q1->decoupled_lightmaps : map.as.q2->decoupled_lightmaps;
        size_t ndec = is_q1 ? map.as.q1->decoupled_lightmap_count : map.as.q2->decoupled_lightmap_count;
        if (dec && index < ndec) { lighting.kind = "decoupled-lightmap"; lighting.image = image; lighting.styles = face.styles; lighting.mapping = dec[index]; }
        else { lighting.kind = "lightmap"; lighting.image = image; lighting.styles = face.styles; }
      }
      qa_render_material material; int has_q1_sky = 0; qa_renderer_image sky_solid, sky_overlay; memset(&sky_solid, 0, sizeof sky_solid); memset(&sky_overlay, 0, sizeof sky_overlay);
      if (is_q1) {
        qa_q1_texture_animations anim = qa_q1_texture_animations_build(name, entries, ntexslots);
        if (o->nanim == o->canim) { o->canim = o->canim ? o->canim * 2 : 8; o->animations = xg(o->animations, o->canim * sizeof *o->animations); }
        o->animations[o->nanim++] = anim;
        qa_create_q1_options co; memset(&co, 0, sizeof co);
        co.has_alpha = true; co.alpha = warp ? (options.has_q1_water_alpha ? options.q1_water_alpha : 1) : 1;
        co.animation = anim.animation; co.animation_count = anim.animation_count;
        co.alternate_animation = anim.alternate_animation; co.alternate_animation_count = anim.alternate_animation_count;
        material = qa_create_q1_material(name, texture->image, lighting, co);
        if (!strcmp(qa_q1_surface_kind(name), "sky")) {
          has_q1_sky = 1;
          int found = 0;
          for (size_t k = 0; k < nsky; k++) if (sky_pairs[k].ordinal == texture->image.ordinal) { sky_solid = sky_pairs[k].solid; sky_overlay = sky_pairs[k].overlay; found = 1; }
          if (!found) {
            const qa_image_level *level = texture->content.level_count ? &texture->content.levels[0] : NULL;
            int can_split = (original && original->has_quake64_shift) ? texture->content.kind == QA_RENDER_IMAGE_INDEXED8
              : (level && level->width == 256 && level->height == 128);
            qa_split_sky_texture *split = NULL;
            if (can_split) {
              if (o->nsplit == o->csplit) { o->csplit = o->csplit ? o->csplit * 2 : 4; o->splits = xg(o->splits, o->csplit * sizeof *o->splits); }
              split = &o->splits[o->nsplit++];
              *split = (original && !original->has_quake64_shift) || !original ? qa_split_q1_sky_texture(texture->content) : qa_split_q64_sky_texture(texture->content);
            }
            char *sn1 = xalloc(strlen(name) + 16), *sn2 = xalloc(strlen(name) + 16);
            sprintf(sn1, "%s:solid", name); sprintf(sn2, "%s:overlay", name); w_scratch(o, sn1); w_scratch(o, sn2);
            if (!split) {
              sky_solid = texture->image;
              uint8_t *px = xalloc(4); memset(px, 0, 4); w_scratch(o, px);
              sky_overlay = generated_image(out, o, sn2, (qa_image_level){ 1, 1, { px, 4 } }, "repeat");
            } else {
              sky_solid = generated_image(out, o, sn1, split->solid, "repeat");
              sky_overlay = generated_image(out, o, sn2, split->overlay, "repeat");
            }
            if (nsky == csky) { csky = csky ? csky * 2 : 4; sky_pairs = xg(sky_pairs, csky * sizeof *sky_pairs); }
            struct sky_pair pairv; pairv.ordinal = texture->image.ordinal; pairv.solid = sky_solid; pairv.overlay = sky_overlay;
            sky_pairs[nsky++] = pairv;
          }
        }
      } else {
        qa_renderer_image *frames = xalloc((ntexslots ? ntexslots : 1) * sizeof *frames); w_scratch(o, frames);
        size_t nf2 = 0;
        int *visited = xalloc((ntexslots ? ntexslots : 1) * sizeof(int)); w_scratch(o, visited); memset(visited, 0, (ntexslots ? ntexslots : 1) * sizeof(int));
        long next = (long)face.texture_info; int has_next = 1;
        while (has_next && next >= 0 && (size_t)next < ntexslots && !visited[next]) {
          visited[next] = 1; frames[nf2++] = textures[next]->image;
          qa_q2_texture_info ti = map.as.q2->texture_info[next];
          has_next = ti.has_next; next = (long)ti.next;
        }
        qa_q2_texture_info info = map.as.q2->texture_info[tii];
        material = qa_create_q2_material(name, frames, nf2, lighting, info.surface.flags, info.surface.material);
      }
      char *shader_name_str = xalloc(strlen(name) + 10); sprintf(shader_name_str, "textures/%s", name); w_scratch(o, shader_name_str);
      qa_registered_scene_material shader; int has_shader = 0;
      memset(&shader, 0, sizeof shader);
      if (qa_scene_shader_registry_has_authored(shaders, shader_name_str)) {
        qa_scene_shader_binding binding; memset(&binding, 0, sizeof binding);
        binding.is_world = true; binding.world = material_world;
        binding.has_lightmap = lightmap != NULL; if (lightmap) binding.lightmap = lightmap->image;
        binding.lightmap_index = lightmap ? (double)index : -1;
        binding.base_texture = texture;
        shader = qa_scene_shader_registry_register(shaders, shader_name_str, &binding);
        has_shader = 1;
      }
      qa_world_surface surf; memset(&surf, 0, sizeof surf);
      surf.shader_name = shader_name_str; surf.base_texture = texture;
      surf.shader = shader; surf.has_shader = has_shader;
      surf.index = (double)index;
      surf.bounds = qa_scene_geometry_bounds_vertices(geometry.vertices, geometry.vertex_count);
      surf.has_plane = true; surf.plane = prepared->plane; surf.geometry = geometry;
      surf.is_q3 = false; surf.material = material; surf.fullbright = texture->fullbright;
      surf.legacy_lightmap = lightmap;
      surf.has_q1_sky = has_q1_sky; surf.q1_sky_solid = sky_solid; surf.q1_sky_overlay = sky_overlay;
      PUSH_SURF(surf);
    }
    free(sky_pairs);
  }
  out->surfaces = surfaces; out->surface_count = nsurf;
  if (map.kind == QA_DECODED_WORLD_Q2 && options.q2_sky_name) {
    o->q2_sky = xalloc(6 * sizeof *o->q2_sky); o->q2_sky_count = 6;
    for (int i = 0; i < 6; i++) {
      char *path = xalloc(strlen(options.q2_sky_name) + 16);
      sprintf(path, "env/%s%s", options.q2_sky_name, qa_sky_face_suffixes[i]); w_scratch(o, path);
      qa_scene_texture_load_options lo; memset(&lo, 0, sizeof lo);
      lo.family = "q2"; lo.usage = "sky"; lo.wrap = "clamp"; lo.has_mipmap = true; lo.mipmap = false;
      const qa_scene_texture *t = qa_scene_texture_loader_load(shaders->textures, path, &lo);
      o->q2_sky[i] = t ? t->image : shaders->textures->missing->image;
    }
  }
  if (!qa_scene_shader_registry_preparing_replacement(shaders))
    qa_scene_material_registrations_bind_world(shaders->registrations->owner, qa_world_scene_world_service(out));
  #undef PUSH_SURF
}
/* ---- material context ---- */
static qa_vec4 project_cb(void *ctx, qa_vec3 point) { return qa_scene_projector_project(*(qa_scene_view_projector *)ctx, point); }
typedef struct fog_ctx { qa_fog_coordinates_state state; qa_scene_model_transform model; int has_model; } fog_ctx;
static qa_vec2 fog_coords_cb(void *ctx, qa_vec3 position) {
  fog_ctx *c = ctx;
  return qa_fog_coordinates_at(c->state, c->has_model ? qa_scene_world_point(position, c->model) : position);
}
static fog_ctx fog_storage; /* single live context at a time, as TS builds one per call */
void qa_world_scene_material_context(qa_world_scene *scene, const qa_world_view_input *input, const qa_scene_model_transform *model, const qa_fog_volume *fog, qa_world_material_context *out) {
  memset(out, 0, sizeof *out);
  double time = input->time.kind == QA_SOURCE_TIME_SECONDS ? input->time.value : input->time.value / 1000;
  qa_material_draw_context *c = &out->context;
  memset(c, 0, sizeof *c);
  if (input->has_q1_fog) {
    c->has_q1_fog = true; c->q1_fog.density = input->q1_fog.density; c->q1_fog.color = input->q1_fog.color;
    c->q1_fog.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; c->q1_fog.texture.image = scene->shaders->textures->white->image;
  }
  c->time = time; c->time_offset = 0; c->refdef_time = trunc(time * 1000);
  c->identity_light = input->has_identity_light ? input->identity_light : 1;
  c->entity_rgba = input->has_material_entity_rgba ? input->material_entity_rgba : qa_math_vec4(255, 255, 255, 255);
  if (input->material_lighting) c->lighting = input->material_lighting;
  else { out->lighting_value = (qa_entity_lighting){{0,0,0},{0,0,0},{0,0,0}, 0}; c->lighting = &out->lighting_value; }
  c->view_origin = input->camera.origin;
  c->local_view_origin = model ? qa_scene_local_point(input->camera.origin, *model) : input->camera.origin;
  c->noise = &scene->noise;
  c->shader_tex_coord = qa_math_vec2(0, 0);
  c->deform_view.axis = input->camera.axis;
  c->deform_view.mirror = input->camera.clip_is_portal && input->camera.clip_mirror;
  c->deform_view.entity_axis = NULL; /* set below through model storage */
  static qa_axis entity_axis_storage;
  if (model) { entity_axis_storage = model->axis; c->deform_view.entity_axis = &entity_axis_storage; out->model = *model; out->has_model = true; }
  c->deform_view.non_normalized_axis = NULL;
  c->projection_shadow = input->material_projection_shadow;
  c->render_text = input->render_text; c->render_text_count = input->render_text_count;
  c->depth_range[0] = 0; c->depth_range[1] = 1;
  out->polygon_offset_value = (qa_polygon_offset){ -1, -2 }; c->polygon_offset = &out->polygon_offset_value;
  if (fog) {
    out->fog_state = qa_fog_coordinates(*fog, input->camera.origin, input->camera.axis.vectors[0]);
    fog_storage.state = out->fog_state; fog_storage.has_model = model != NULL;
    if (model) fog_storage.model = *model;
    out->fog_value.ctx = &fog_storage; out->fog_value.coordinates = fog_coords_cb;
    out->fog_value.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; out->fog_value.texture.image = scene->fog_image;
    out->fog_value.color = fog->color;
    c->fog = &out->fog_value;
  }
  out->projector = model ? qa_scene_create_model_projector(input->camera, *model) : qa_scene_create_view_projector(input->camera);
  c->project = (qa_mat_project_fn){ &out->projector, project_cb };
}
/* ---- remap helpers ---- */
static int surface_remap(qa_world_scene *scene, const qa_world_surface *surface, qa_scene_material_remap *out) {
  wimpl *o = wi(scene);
  if (!surface->has_shader) {
    for (size_t i = 0; i < o->nraw; i++) if (o->raw[i].index == surface->index) { *out = o->raw[i].remap; return 1; }
    return 0;
  }
  qa_scene_material_remap found;
  if (qa_scene_material_registrations_current_remap(scene->shaders->registrations->owner, surface->shader, &found)) { *out = found; return 1; }
  return 0;
}
static qa_world_surface *surface_at(qa_world_scene *scene, double index) {
  long i = (long)index;
  if (index != (double)i || i < 0 || (size_t)i >= scene->surface_count) qa_range_error("Scene world index %g outside %zu", index, scene->surface_count);
  return &scene->surfaces[i];
}
static int local_box_culled(qa_bounds bounds, qa_scene_camera camera, const qa_scene_model_transform *model) {
  qa_vec3 corners[8];
  for (int i = 0; i < 8; i++) {
    double x = (i & 1) == 0 ? bounds.min.x : bounds.max.x;
    double y = (i & 2) == 0 ? bounds.min.y : bounds.max.y;
    double z = (i & 4) == 0 ? bounds.min.z : bounds.max.z;
    if (!model) corners[i] = qa_math_vec3(x, y, z);
    else {
      double scale = qa_scene_model_scale(*model);
      corners[i] = qa_add3(qa_add3(qa_add3(model->origin, qa_scale3(model->axis.vectors[0], fr(x * scale))),
        qa_scale3(model->axis.vectors[1], fr(y * scale))), qa_scale3(model->axis.vectors[2], fr(z * scale)));
    }
  }
  qa_plane planes[5]; size_t np = qa_scene_camera_frustum(camera, planes); if (np > 4) np = 4;
  for (size_t p = 0; p < np; p++) {
    int any = 0;
    for (int i = 0; i < 8; i++) if (qa_dot3(corners[i], planes[p].normal) > planes[p].distance) any = 1;
    if (!any) return 1;
  }
  return 0;
}
static int surface_culled(qa_world_scene *scene, qa_world_surface *surface, const qa_world_view_input *input, const qa_material_draw_context *context, const qa_scene_model_transform *model) {
  if (input->visibility.no_cull || surface->flare || surface->skip) return 0;
  if (scene->map.kind != QA_DECODED_WORLD_Q3) qa_range_error("Q3 surface requires its BSP source");
  const qa_q3_bsp_surface *source = &scene->map.as.q3->surfaces[(size_t)surface->index];
  if (is_str(source->kind, "triangles")) return local_box_culled(surface->bounds, input->camera, model);
  if (is_str(source->kind, "patch")) {
    qa_vec3 center = qa_scale3(qa_add3(surface->bounds.min, surface->bounds.max), 0.5);
    double radius = fr(qa_length3(qa_sub3(surface->bounds.min, center)) * (model ? fabs(qa_scene_model_scale(*model)) : 1));
    qa_vec3 origin = model ? qa_scene_world_point(center, *model) : center;
    qa_plane planes[5]; size_t np = qa_scene_camera_frustum(input->camera, planes); if (np > 4) np = 4;
    int clipped = 0;
    for (size_t p = 0; p < np; p++) {
      double distance = fr(qa_dot3(origin, planes[p].normal) - planes[p].distance);
      if (distance < -radius) return 1;
      if (distance <= radius) clipped = 1;
    }
    return clipped && local_box_culled(surface->bounds, input->camera, model);
  }
  qa_scene_material_remap rem;
  qa_registered_scene_material shader = surface_remap(scene, surface, &rem) ? rem.material : surface->shader;
  if (!is_str(source->kind, "planar") || !surface->has_plane) return 0;
  const char *cull = qa_registered_scene_material_material(shader)->cull;
  if (is_str(cull, "none")) return 0;
  double viewer = qa_dot3(context->local_view_origin, surface->plane.normal), distance = surface->plane.distance;
  return is_str(cull, "front") ? viewer < fr(distance - 8) : viewer > fr(distance + 8);
}
static int frontend_light_mask(qa_world_scene *scene, qa_world_surface *surface, int incoming, const qa_q3_dynamic_light *lights, size_t nlights) {
  if (incoming == 0) return 0;
  if (surface->is_q3) {
    if (scene->map.kind != QA_DECODED_WORLD_Q3) qa_range_error("Q3 surface requires its BSP source");
    const qa_q3_bsp_surface *source = &scene->map.as.q3->surfaces[(size_t)surface->index];
    if (is_str(source->kind, "flare") || surface->skip) return 0;
    if (is_str(source->kind, "triangles")) return incoming;
    if (is_str(source->kind, "patch")) return qa_grid_dlight_mask(lights, nlights, incoming, surface->bounds);
  }
  return !surface->has_plane ? qa_grid_dlight_mask(lights, nlights, incoming, surface->bounds)
                              : qa_face_dlight_mask(lights, nlights, incoming, surface->plane);
}
/* admission */
typedef struct adm_impl {
  struct { qa_world_scene *scene; double *surfaces; size_t n, cap; op_storage *ops_storage; int has_ops; } *entries;
  size_t count, cap;
} adm_impl;
void qa_create_world_surface_admission(qa_source_scene_order *view, qa_world_surface_admission *out) {
  memset(out, 0, sizeof *out); out->view = view;
  adm_impl *a = xalloc(sizeof *a); memset(a, 0, sizeof *a); out->_impl = a;
}
static adm_impl *adm_get(qa_world_surface_admission *a, qa_world_scene *scene, int create) {
  (void)scene; (void)create;
  return (adm_impl *)a->_impl;
}
static int admit_surface(qa_world_scene *scene, qa_world_surface_admission *admission, double index) {
  adm_impl *a = adm_get(admission, scene, 1);
  size_t slot = a->count;
  for (size_t i = 0; i < a->count; i++) if (a->entries[i].scene == scene) slot = i;
  if (slot == a->count) {
    if (a->count == a->cap) { a->cap = a->cap ? a->cap * 2 : 4; a->entries = xg(a->entries, a->cap * sizeof *a->entries); }
    a->entries[a->count].scene = scene; a->entries[a->count].surfaces = NULL; a->entries[a->count].n = 0; a->entries[a->count].cap = 0; a->entries[a->count].ops_storage = NULL; a->entries[a->count].has_ops = 0;
    slot = a->count++;
  }
  for (size_t i = 0; i < a->entries[slot].n; i++) if (a->entries[slot].surfaces[i] == index) return 0;
  if (a->entries[slot].n == a->entries[slot].cap) { a->entries[slot].cap = a->entries[slot].cap ? a->entries[slot].cap * 2 : 16; a->entries[slot].surfaces = xg(a->entries[slot].surfaces, a->entries[slot].cap * sizeof(double)); }
  a->entries[slot].surfaces[a->entries[slot].n++] = index;
  return 1;
}
void qa_world_surface_admission_destroy(qa_world_surface_admission *a) {
  if (!a || !a->_impl) return;
  adm_impl *impl = a->_impl;
  for (size_t i = 0; i < impl->count; i++) { free(impl->entries[i].surfaces); op_storage_destroy(impl->entries[i].ops_storage); }
  free(impl->entries); free(impl); memset(a, 0, sizeof *a);
}
static qa_source_surface_order source_order_for(qa_world_surface *surface, qa_world_surface_admission *source, qa_source_entity_order entity, int mask) {
  qa_source_surface_order o; memset(&o, 0, sizeof o);
  o.view = source->view; o.entity = entity; o.surface = surface->index;
  o.fog = surface->is_q3 ? surface->fog_index + 1 : 0; o.dlight = mask != 0 ? 1 : 0;
  return o;
}
static void shader_operations(qa_world_scene *scene, qa_world_surface *surface, qa_registered_scene_material source_shader, const qa_world_view_input *input, qa_world_material_context *ctx, const qa_scene_model_transform *model, int has_mask, int mask_in, const qa_q3_dynamic_light *lights_in, size_t nlights_in, int has_order, qa_source_surface_order order, op_storage *store);
static void q2_sky_operations(qa_world_scene *scene, qa_material_geometry geometry, const qa_world_view_input *input, qa_world_material_context *ctx, op_storage *store, int wrap_depth) {
  qa_q2_sky_view fallback; memset(&fallback, 0, sizeof fallback);
  wimpl *o = wi(scene);
  fallback.images = o->q2_sky; fallback.image_count = o->q2_sky_count; fallback.rotation = 0; fallback.auto_rotate = false; fallback.axis = qa_math_vec3(0, 0, 1);
  const qa_q2_sky_view *sky = input->source_sky ? input->source_sky : input->q2_sky ? input->q2_sky : &fallback;
  double seconds = input->time.kind == QA_SOURCE_TIME_SECONDS ? input->time.value : input->time.value / 1000;
  if (wrap_depth) { qa_render_operation d; memset(&d, 0, sizeof d); d.kind = QA_RENDER_OP_DEPTH_RANGE; d.range[0] = 1; d.range[1] = 1; op_push_plain(store, d); }
  if (store->nsky_ops == store->csky_ops) { store->csky_ops = store->csky_ops ? store->csky_ops * 2 : 4; store->sky_ops = xg(store->sky_ops, store->csky_ops * sizeof *store->sky_ops); }
  qa_q2_sky_operations *res = &store->sky_ops[store->nsky_ops++];
  size_t n = 0;
  qa_render_operation *ops = qa_q2_sky_sides(geometry, input->camera.origin, *sky, seconds, ctx->context.project.project, ctx->context.project.ctx, res, &n);
  for (size_t i = 0; i < n; i++) op_push_plain(store, ops[i]);
  if (wrap_depth) { qa_render_operation d; memset(&d, 0, sizeof d); d.kind = QA_RENDER_OP_DEPTH_RANGE; d.range[0] = ctx->context.depth_range[0]; d.range[1] = ctx->context.depth_range[1]; op_push_plain(store, d); }
}
static void sky_operations(qa_world_scene *scene, qa_registered_scene_material shader, qa_material_geometry geometry, const qa_world_view_input *input, qa_world_material_context *ctx, qa_scene_shader_registry *source, op_storage *store) {
  if (input->source_sky) { q2_sky_operations(scene, geometry, input, ctx, store, 1); return; }
  qa_deform_geometry dgeom = geometry;
  qa_sky_builder_clip(&source->sky, &dgeom, 1, input->camera.origin);
  double far_v = qa_scene_far_clip(input->camera.origin, scene->bounds); if (far_v < 2048) far_v = 2048;
  if (!store->sky_geoms) { store->sky_geoms = xalloc(sizeof *store->sky_geoms); store->nsky = 0; }
  qa_sky_geometry *built = &store->sky_geoms[store->nsky];
  *built = qa_sky_builder_build(&source->sky, input->camera.origin, far_v);
  store->nsky = 1;
  qa_render_operation depth; memset(&depth, 0, sizeof depth); depth.kind = QA_RENDER_OP_DEPTH_RANGE; depth.range[0] = 1; depth.range[1] = 1;
  op_push_plain(store, depth);
  const qa_mat_compiled_material *compiled = qa_registered_scene_material_compiled(shader);
  const qa_mat_registered_sky *sky = compiled->registered.sky;
  if (sky && sky->outer) {
    static const char *SUFFIXES[6] = { "rt", "lf", "bk", "ft", "up", "dn" };
    for (size_t fi = 0; fi < built->face_count; fi++) {
      qa_sky_face *face = &built->box[fi];
      long fi_l = (long)face->face;
      if (face->face != (double)fi_l || fi_l < 0 || fi_l >= 6) qa_range_error("Sky face is outside the cube");
      qa_render_operation op; memset(&op, 0, sizeof op);
      op.kind = QA_RENDER_OP_SKY_SIDE;
      op.image = sky->outer->image(sky->outer->ctx, SUFFIXES[fi_l]).image;
      op.color = qa_math_vec4(ctx->context.identity_light, ctx->context.identity_light, ctx->context.identity_light, 1);
      qa_sky_strip *strips = xalloc((face->strip_count ? face->strip_count : 1) * sizeof *strips); track_scratch(store, strips);
      for (size_t si = 0; si < face->strip_count; si++) {
        size_t nv = face->strips[si].index_count;
        qa_vertex_position_tex_coord *vs = xalloc((nv ? nv : 1) * sizeof *vs); track_scratch(store, vs);
        for (size_t k = 0; k < nv; k++) {
          long vi = (long)face->strips[si].indices[k];
          vs[k].position = ctx->context.project.project(ctx->context.project.ctx, face->geometry.vertices[vi].position);
          vs[k].tex_coord = face->geometry.vertices[vi].tex_coord;
        }
        strips[si].vertices = vs; strips[si].vertex_count = nv;
      }
      op.strips = strips; op.strip_count = face->strip_count;
      op_push_plain(store, op);
    }
  }
  if (built->clouds.index_count) {
    qa_world_material_context ccopy = *ctx; ccopy.context.depth_range[0] = 1; ccopy.context.depth_range[1] = 1;
    size_t nb = 0;
    qa_draw_batch *batches = qa_evaluate_material_passes(compiled, built->clouds, &ccopy.context, &nb);
    track_batches(store, batches, nb);
    qa_render_operation draw; memset(&draw, 0, sizeof draw); draw.kind = QA_RENDER_OP_DRAW; draw.batches = batches; draw.batch_count = nb;
    op_push_plain(store, draw);
  }
  qa_render_operation restore; memset(&restore, 0, sizeof restore); restore.kind = QA_RENDER_OP_DEPTH_RANGE;
  restore.range[0] = ctx->context.depth_range[0]; restore.range[1] = ctx->context.depth_range[1];
  op_push_plain(store, restore);
}
typedef struct dlight_ctx {
  qa_world_scene *scene; qa_registered_scene_material shader; int mask;
  const qa_q3_dynamic_light *lights; size_t count; qa_mat_project_fn project; op_storage *store;
} dlight_ctx;
static qa_draw_batch *dlight_batches(void *vctx, qa_material_geometry geometry, size_t *out_count) {
  dlight_ctx *c = vctx;
  if (!qa_receives_projected_dlights(qa_registered_scene_material_compiled(c->shader)) || c->mask == 0) { *out_count = 0; return NULL; }
  size_t n = 0;
  qa_draw_batch *batches = qa_project_dlight_texture(geometry, c->mask, c->lights, c->count, c->scene->dlight_image, c->project, qa_registered_scene_material_material(c->shader)->cull, &n);
  track_batches(c->store, batches, n);
  *out_count = n; return batches;
}
typedef struct lightmap_ctx {
  qa_world_scene *scene; const qa_world_view_input *input; const qa_scene_model_transform *model; op_storage *store; int has_model;
} lightmap_ctx;
static qa_batch_lighting lightmap_lighting_cb(void *vctx, qa_material_geometry geometry) {
  lightmap_ctx *c = vctx;
  world_arrays arrays; build_world_arrays(c->scene, geometry, c->has_model ? c->model : NULL, c->store, &arrays);
  size_t nlights = 0;
  const qa_q2_fragment_light *lights = fragment_lights(c->scene, c->input->fragment_lights, c->input->fragment_light_count, 0, &nlights);
  qa_batch_lighting lighting; memset(&lighting, 0, sizeof lighting);
  lighting.kind = "q2-world"; lighting.pass = "material-lightmap";
  lighting.world_positions = arrays.positions; lighting.world_position_count = arrays.count;
  lighting.normals = arrays.normals; lighting.normal_count = arrays.count;
  lighting.lights = lights; lighting.light_count = nlights; lighting.atlas = c->input->fragment_atlas;
  return lighting;
}
static void shader_operations(qa_world_scene *scene, qa_world_surface *surface, qa_registered_scene_material source_shader, const qa_world_view_input *input, qa_world_material_context *ctx, const qa_scene_model_transform *model, int has_mask, int mask_in, const qa_q3_dynamic_light *lights_in, size_t nlights_in, int has_order, qa_source_surface_order order, op_storage *store) {
  qa_scene_material_remap rem; int has_rem = surface_remap(scene, surface, &rem);
  qa_registered_scene_material shader = has_rem ? rem.material : source_shader;
  qa_patch_grid *grid = surface->is_q3 ? surface->grid : NULL;
  const qa_fog_volume *fog_vol = surface->is_q3 ? surface->fog : NULL;
  if (surface->is_q3 && (surface->flare || surface->skip)) {
    op_storage *sub = op_storage_new(); track_scratch(store, sub);
    if (surface->flare && input->prepare_flare) {
      size_t n = 0; const qa_render_operation *ops = input->prepare_flare(input->prepare_flare_ctx, surface, &ctx->context, &n);
      for (size_t i = 0; i < n; i++) op_push_plain(sub, ops[i]);
    }
    if (!has_order) for (size_t i = 0; i < sub->count; i++) op_push(store, sub->ops[i]);
    else {
      qa_scene_group g = qa_source_draw_group(source_shader, order, NULL, 0);
      /* replace operations with the flare operations */
      free(g.owned_operation); g.owned_operation = NULL;
      g.operations = sub->ops ? (qa_render_operation *)sub->ops : NULL;
      /* sub holds SceneOperations of plain kind: extract */
      qa_render_operation *plain = xalloc((sub->count ? sub->count : 1) * sizeof *plain); track_scratch(store, plain);
      for (size_t i = 0; i < sub->count; i++) plain[i] = sub->ops[i].operation;
      g.operations = plain; g.operation_count = sub->count;
      op_push_group(store, g);
    }
    sub->count = 0; sub->ops = NULL; /* ownership moved */
    return;
  }
  const qa_render_material *rmat = qa_registered_scene_material_material(shader);
  int nodraw = 0;
  for (size_t i = 0; i < rmat->surface_parameters.count; i++) if (is_str(rmat->surface_parameters.values[i], "nodraw")) nodraw = 1;
  if (nodraw) { if (has_order) op_push_group(store, qa_source_draw_group(source_shader, order, NULL, 0)); return; }
  qa_material_geometry geometry = surface->geometry;
  if (grid) {
    if (store->nlod == store->clod) { store->clod = store->clod ? store->clod * 2 : 4; store->lod_meshes = xg(store->lod_meshes, store->clod * sizeof *store->lod_meshes); }
    qa_patch_mesh *lod = &store->lod_meshes[store->nlod++];
    qa_vec3 lod_origin = model ? qa_scene_world_point(grid->lod_origin, *model) : grid->lod_origin;
    qa_select_patch_lod(grid, lod_origin, input->camera.origin, input->camera.axis.vectors[0], input->has_curve_error ? input->curve_error : 250, lod);
    geometry = (qa_material_geometry){ lod->vertices, lod->vertex_count, lod->indices, lod->index_count };
  }
  size_t nlights = lights_in ? nlights_in : input->visibility.q3_light_count;
  const qa_q3_dynamic_light *source_lights = lights_in ? lights_in : input->visibility.q3_lights;
  if (input->visibility.q3_light_count > 32) qa_range_error("Q3 projected lighting supports the source 32-light mask");
  qa_q3_dynamic_light *local_lights = NULL;
  const qa_q3_dynamic_light *lights = source_lights;
  size_t count_l = nlights;
  if (!lights_in && model) {
    local_lights = xalloc((count_l ? count_l : 1) * sizeof *local_lights); track_scratch(store, local_lights);
    for (size_t i = 0; i < count_l; i++) { local_lights[i] = source_lights[i]; local_lights[i].origin = qa_scene_local_point(source_lights[i].origin, *model); local_lights[i].radius = source_lights[i].radius / fabs(qa_scene_model_scale(*model)); }
    lights = local_lights;
  }
  int incoming = count_l == 32 ? -1 : (int)(((size_t)1 << count_l) - 1);
  int mask = has_mask ? mask_in : frontend_light_mask(scene, surface, incoming, lights, count_l);
  qa_world_material_context draw_ctx = *ctx;
  if (fog_vol) qa_world_scene_material_context(scene, input, model, fog_vol, &draw_ctx);
  draw_ctx.context.time_offset = has_rem ? rem.time_offset : 0;
  dlight_ctx *dctx = xalloc(sizeof *dctx); track_scratch(store, dctx);
  *dctx = (dlight_ctx){ scene, shader, mask, lights, count_l, draw_ctx.context.project, store };
  draw_ctx.context.dynamic_light_batches_ctx = dctx; draw_ctx.context.dynamic_light_batches = dlight_batches;
  draw_ctx.context.has_dynamic_lights = true; draw_ctx.context.dynamic_lights = lights; draw_ctx.context.dynamic_light_count = count_l;
  draw_ctx.context.dynamic_light_mask = mask; draw_ctx.context.dynamic_light_image = scene->dlight_image;
  lightmap_ctx *lctx = NULL;
  if (input->has_q2_fragment_lighting) {
    lctx = xalloc(sizeof *lctx); track_scratch(store, lctx);
    *lctx = (lightmap_ctx){ scene, input, model, store, model != NULL };
    draw_ctx.context.lightmap_lighting_ctx = lctx; draw_ctx.context.lightmap_lighting = lightmap_lighting_cb;
  }
  const qa_mat_compiled_material *compiled = qa_registered_scene_material_compiled(shader);
  if (is_str(compiled->finished.iterator.kind, "sky")) {
    op_storage *sub = op_storage_new(); track_scratch(store, sub);
    sky_operations(scene, shader, geometry, input, &draw_ctx, has_rem ? (qa_scene_shader_registry *)rem.source : scene->shaders, sub);
    qa_scene_group g; memset(&g, 0, sizeof g);
    if (has_order) { qa_scene_group tmp = qa_source_draw_group(source_shader, order, NULL, 0); g.order = tmp.order; free(tmp.owned_operation); }
    else { g.order.kind = QA_GROUP_COMPILED; g.order.material = compiled; }
    qa_render_operation *plain = xalloc((sub->count ? sub->count : 1) * sizeof *plain); track_scratch(store, plain);
    for (size_t i = 0; i < sub->count; i++) plain[i] = sub->ops[i].operation;
    /* move sub storage lists into store by merging tracking */
    for (size_t i = 0; i < sub->nlists; i++) track_batches(store, sub->batch_lists[i], sub->batch_counts_f[i]);
    for (size_t i = 0; i < sub->nscratch; i++) track_scratch(store, sub->scratch[i]);
    for (size_t i = 0; i < sub->nsky_ops; i++) { if (store->nsky_ops == store->csky_ops) { store->csky_ops = store->csky_ops ? store->csky_ops * 2 : 4; store->sky_ops = xg(store->sky_ops, store->csky_ops * sizeof *store->sky_ops); } store->sky_ops[store->nsky_ops++] = sub->sky_ops[i]; }
    if (sub->sky_geoms) { if (!store->sky_geoms) { store->sky_geoms = sub->sky_geoms; store->nsky = sub->nsky; } else track_scratch(store, sub->sky_geoms); }
    sub->nlists = 0; sub->nscratch = 0; sub->nsky_ops = 0; sub->sky_geoms = NULL; sub->nsky = 0;
    g.operations = plain; g.operation_count = sub->count;
    op_push_group(store, g);
    return;
  }
  size_t nb = 0;
  qa_draw_batch *batches = qa_prepare_material_batches(compiled, geometry, &draw_ctx.context, &nb);
  track_batches(store, batches, nb);
  int flip = (draw_ctx.context.deform_view.mirror != (model && qa_scene_model_scale(*model) < 0));
  qa_draw_batch *final_batches = batches;
  if (flip && nb) {
    qa_draw_batch *flipped = xalloc(nb * sizeof *flipped); track_scratch(store, flipped);
    for (size_t i = 0; i < nb; i++) {
      flipped[i] = batches[i];
      const char *cull = flipped[i].state.cull;
      flipped[i].state.cull = is_str(cull, "none") ? "none" : is_str(cull, "front") ? "back" : "front";
    }
    final_batches = flipped;
  }
  qa_scene_group group = has_order ? qa_source_draw_group(source_shader, order, final_batches, nb)
                                   : qa_compiled_draw_group(compiled, final_batches, nb);
  op_push_group(store, group);
}
/* ---- surface operations ---- */
typedef void (*world_op_sink)(op_storage *);
static void q1_sky_batches(qa_world_scene *scene, qa_world_surface *surface, const qa_world_view_input *input, qa_world_material_context *ctx, op_storage *store) {
  (void)scene;
  static const qa_batch_fog *fog_dummy = NULL; (void)fog_dummy;
  qa_render_vertex *all_verts = xalloc((surface->geometry.vertex_count ? surface->geometry.vertex_count * 2 : 1) * sizeof *all_verts);
  track_scratch(store, all_verts);
  qa_draw_batch *batches = xalloc(2 * sizeof *batches); track_scratch(store, batches);
  qa_batch_fog *fogs = xalloc(2 * sizeof *fogs); track_scratch(store, fogs);
  const char *layers[2] = { "solid", "overlay" };
  for (int li = 0; li < 2; li++) {
    qa_draw_batch *b = &batches[li]; memset(b, 0, sizeof *b);
    if (input->has_q1_fog && input->q1_fog.density > 0) {
      fogs[li] = (qa_batch_fog){ "constant", NULL, input->q1_fog.color, 0, input->q1_fog.sky_factor };
      b->fog = &fogs[li];
    }
    b->primitive_is_lines = false; b->texturing_is_pair = false; b->lighting.kind = "vertex";
    b->indices.values = surface->geometry.indices; b->indices.count = surface->geometry.index_count;
    b->texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
    b->texture.image = li ? surface->q1_sky_overlay : surface->q1_sky_solid;
    qa_render_vertex *vs = all_verts + li * surface->geometry.vertex_count;
    for (size_t k = 0; k < surface->geometry.vertex_count; k++) {
      const qa_material_vertex *v = &surface->geometry.vertices[k];
      vs[k].position = ctx->context.project.project(ctx->context.project.ctx, v->position);
      vs[k].tex_coord = qa_q1_sky_tex_coords(v->position, input->camera.origin, ctx->context.time, layers[li]);
      vs[k].color = qa_math_vec4(1, 1, 1, 1);
    }
    b->vertices = vs; b->vertex_count = surface->geometry.vertex_count;
    b->state.blend.source = li == 0 ? "one" : "src-alpha";
    b->state.blend.destination = li == 0 ? "zero" : "one-minus-src-alpha";
    b->state.depth_test = "less-equal"; b->state.depth_write = true; b->state.alpha_test = "none"; b->state.cull = "none";
    b->state.depth_range[0] = 0; b->state.depth_range[1] = 1; b->state.polygon_offset = NULL;
  }
  op_push_group(store, qa_sequence_draw_group(QA_SEQUENCE_SKY, batches, 2));
}
static qa_vec4 world_point_project(void *ctx, qa_vec3 point) {
  qa_scene_model_transform *m = ctx;
  qa_vec3 world = m ? qa_scene_world_point(point, *m) : point;
  return qa_math_vec4(world.x, world.y, world.z, 1);
}
static void surface_operations(qa_world_scene *scene, qa_world_surface *surface, const qa_world_view_input *input, qa_world_material_context *ctx, const qa_scene_model_transform *model, int has_lighting, int mask, const qa_q3_dynamic_light *lights_arr, size_t nlights_arr, int has_order, qa_source_surface_order order, op_storage *store) {
  wimpl *o = wi(scene);
  if (surface->is_q3) { shader_operations(scene, surface, surface->shader, input, ctx, model, has_lighting, mask, lights_arr, nlights_arr, has_order, order, store); return; }
  const qa_render_material *material = &surface->material;
  int has_raw = 0; for (size_t i = 0; i < o->nraw; i++) if (o->raw[i].index == surface->index) has_raw = 1;
  if (!surface->has_shader && !has_raw) {
    if (is_str(material->kind, "q2") && (i32d(material->surface_flags) & 128) && !(i32d(material->surface_flags) & 4)) return;
    if (surface->has_plane && qa_dot3(ctx->context.local_view_origin, surface->plane.normal) - surface->plane.distance < -0.01) return;
    if (surface->has_q1_sky && input->source_sky) {
      op_storage *sub = op_storage_new(); track_scratch(store, sub);
      q2_sky_operations(scene, surface->geometry, input, ctx, sub, 1);
      qa_scene_group g; memset(&g, 0, sizeof g); g.order.kind = QA_GROUP_SEQUENCE; g.order.phase = QA_SEQUENCE_SKY;
      qa_render_operation *plain = xalloc((sub->count ? sub->count : 1) * sizeof *plain); track_scratch(store, plain);
      for (size_t i = 0; i < sub->count; i++) plain[i] = sub->ops[i].operation;
      for (size_t i = 0; i < sub->nsky_ops; i++) { if (store->nsky_ops == store->csky_ops) { store->csky_ops = store->csky_ops ? store->csky_ops * 2 : 4; store->sky_ops = xg(store->sky_ops, store->csky_ops * sizeof *store->sky_ops); } store->sky_ops[store->nsky_ops++] = sub->sky_ops[i]; }
      sub->nsky_ops = 0;
      g.operations = plain; g.operation_count = sub->count;
      op_push_group(store, g);
      return;
    }
    if (surface->has_q1_sky) { q1_sky_batches(scene, surface, input, ctx, store); return; }
    if (is_str(material->kind, "q2") && (i32d(material->surface_flags) & 4)) {
      op_storage *sub = op_storage_new(); track_scratch(store, sub);
      q2_sky_operations(scene, surface->geometry, input, ctx, sub, 1);
      qa_scene_group g; memset(&g, 0, sizeof g); g.order.kind = QA_GROUP_SEQUENCE; g.order.phase = QA_SEQUENCE_SKY;
      qa_render_operation *plain = xalloc((sub->count ? sub->count : 1) * sizeof *plain); track_scratch(store, plain);
      for (size_t i = 0; i < sub->count; i++) plain[i] = sub->ops[i].operation;
      for (size_t i = 0; i < sub->nsky_ops; i++) { if (store->nsky_ops == store->csky_ops) { store->csky_ops = store->csky_ops ? store->csky_ops * 2 : 4; store->sky_ops = xg(store->sky_ops, store->csky_ops * sizeof *store->sky_ops); } store->sky_ops[store->nsky_ops++] = sub->sky_ops[i]; }
      sub->nsky_ops = 0;
      g.operations = plain; g.operation_count = sub->count;
      op_push_group(store, g);
      return;
    }
  }
  if (surface->legacy_lightmap) {
    const qa_world_surface_lightmap *lightmap = surface->legacy_lightmap;
    int use_source_lights = !(is_str(material->kind, "q2") && input->has_q2_fragment_lighting);
    size_t nsl = use_source_lights ? input->light_count : 0;
    const qa_surface_dynamic_light *source_lights = use_source_lights ? input->lights : NULL;
    qa_surface_dynamic_light *transformed = NULL;
    if (model && nsl) {
      transformed = xalloc(nsl * sizeof *transformed); track_scratch(store, transformed);
      double scale = fabs(qa_scene_model_scale(*model));
      for (size_t i = 0; i < nsl; i++) {
        transformed[i] = source_lights[i];
        transformed[i].origin = qa_scene_local_point(source_lights[i].origin, *model);
        transformed[i].radius = source_lights[i].radius / scale;
        transformed[i].minimum = source_lights[i].minimum / scale;
        transformed[i].color = qa_math_vec3(source_lights[i].color.x * scale, source_lights[i].color.y * scale, source_lights[i].color.z * scale);
      }
      source_lights = transformed;
    }
    double styles_list[1024]; size_t nslist = 0;
    styles_list[nslist++] = q2mod(scene);
    const qa_lightmap_face *face = &lightmap->face;
    for (size_t i = 0; i < face->style_count; i++) {
      double index = face->styles[i];
      if (index == 255) break;
      if (is_str(material->kind, "q1")) {
        const double *src = input->q1_styles ? input->q1_styles : q1_defaults();
        size_t sc = input->q1_styles ? input->q1_style_count : 256;
        long si = (long)index;
        if (index != (double)si || si < 0 || (size_t)si >= sc) qa_range_error("Scene world index %g outside %zu", index, sc);
        styles_list[nslist++] = src[si];
      } else {
        const qa_q2_light_style *src = input->q2_styles ? input->q2_styles : q2_defaults();
        size_t sc = input->q2_styles ? input->q2_style_count : 256;
        long si = (long)index;
        if (index != (double)si || si < 0 || (size_t)si >= sc) qa_range_error("Scene world index %g outside %zu", index, sc);
        styles_list[nslist++] = src[si].rgb.x; styles_list[nslist++] = src[si].rgb.y; styles_list[nslist++] = src[si].rgb.z;
      }
    }
    /* static styles cache */
    int slot = -1; for (size_t i = 0; i < o->nstyles; i++) if (o->styles[i].index == surface->index) slot = (int)i;
    int reusable = nsl == 0;
    int same = reusable && slot >= 0 && o->styles[slot].count == nslist;
    if (same) for (size_t i = 0; i < nslist; i++) if (o->styles[slot].styles[i] != styles_list[i]) same = 0;
    if (!same) {
      if (slot >= 0) { free(o->styles[slot].styles); memmove(o->styles + slot, o->styles + slot + 1, (o->nstyles - (size_t)slot - 1) * sizeof *o->styles); o->nstyles--; }
      qa_built_lightmap built;
      if (is_str(material->kind, "q1")) {
        qa_build_q1_options bo; memset(&bo, 0, sizeof bo); bo.encoding = lightmap->encoding; bo.dynamic_lights = source_lights; bo.dynamic_light_count = nsl;
        built = qa_build_q1_lightmap(*face, input->q1_styles ? input->q1_styles : q1_defaults(), input->q1_styles ? input->q1_style_count : 256, bo);
      } else {
        qa_build_q2_options bo; memset(&bo, 0, sizeof bo); bo.has_modulate = true; bo.modulate = q2mod(scene); bo.dynamic_lights = source_lights; bo.dynamic_light_count = nsl;
        built = qa_build_q2_lightmap(*face, input->q2_styles ? input->q2_styles : q2_defaults(), input->q2_styles ? input->q2_style_count : 256, bo);
      }
      qa_scene_image_registry_update(scene->shaders->textures->images, lightmap->image, 0, built.image);
      qa_image_level direct = qa_direct_lightmap_pixels(built);
      qa_scene_image_registry_update(scene->shaders->textures->images, lightmap->direct, 0, direct);
      if (reusable) {
        if (o->nstyles == o->cstyles) { o->cstyles = o->cstyles ? o->cstyles * 2 : 8; o->styles = xg(o->styles, o->cstyles * sizeof *o->styles); }
        double *copy = xalloc((nslist ? nslist : 1) * sizeof(double)); memcpy(copy, styles_list, nslist * sizeof(double));
        o->styles[o->nstyles++] = (struct style_entry){ surface->index, copy, nslist };
      }
    } else {
      qa_scene_image_registry_require(scene->shaders->textures->images, lightmap->image);
      qa_scene_image_registry_require(scene->shaders->textures->images, lightmap->direct);
    }
  }
  int has_selected = surface->has_shader || has_raw;
  if (has_selected) {
    qa_scene_material_remap rem;
    qa_registered_scene_material selected = surface->has_shader ? surface->shader : (surface_remap(scene, surface, &rem) ? rem.material : surface->shader);
    /* TS: shader ?? rawRemaps.get; shaderOperations also applies currentRemap */
    shader_operations(scene, surface, selected, input, ctx, model, has_lighting, mask, lights_arr, nlights_arr, has_order, order, store);
    return;
  }
  int world_space = input->world_space_batches && !input->camera.clip_is_portal;
  int liquid = is_str(material->kind, "q1")
    ? (is_str(material->surface, "water") || is_str(material->surface, "slime") || is_str(material->surface, "lava") || is_str(material->surface, "teleport"))
    : (material->warp || material->flowing);
  int cacheable = world_space && !model && !liquid;
  double anim_bucket = !cacheable ? 0 : is_str(material->kind, "q1")
    ? (material->animation_count || material->alternate_animation_count ? trunc(ctx->context.time * 10) : 0)
    : (input->has_animation_frame ? input->animation_frame : trunc(ctx->context.time * 2));
  int fb_present = 0; const qa_renderer_image *fb_for_anim = NULL;
  if (is_str(material->kind, "q1")) {
    qa_renderer_image animated = qa_q1_animated_texture(material, ctx->context.time, input->has_alternate_animation && input->alternate_animation);
    fb_for_anim = fb_lookup(scene, animated, &fb_present);
  }
  char cache_key[256] = "";
  if (cacheable) snprintf(cache_key, sizeof cache_key, "%g|%d|%g|%g|%g|%g|%d|%d|%d|%g|%g",
    anim_bucket, (input->has_alternate_animation && input->alternate_animation) ? 1 : 0,
    ctx->context.entity_rgba.x, ctx->context.entity_rgba.y, ctx->context.entity_rgba.z, ctx->context.entity_rgba.w,
    (input->has_q1_fog && input->q1_fog.density > 0) ? 1 : 0,
    is_str(material->kind, "q1") ? (fb_present ? 1 : 0) : 0,
    ctx->context.deform_view.mirror ? 1 : 0, ctx->context.depth_range[0], ctx->context.depth_range[1]);
  if (cacheable && !input->has_q2_fragment_lighting) {
    for (size_t i = 0; i < o->nlc; i++) if (o->legacy_cache[i].index == surface->index && !strcmp(o->legacy_cache[i].key, cache_key)) {
      op_storage *cached = o->legacy_cache[i].storage;
      for (size_t k = 0; k < cached->count; k++) op_push(store, cached->ops[k]);
      return;
    }
  }
  qa_scene_model_transform model_copy = model ? *model : (qa_scene_model_transform){{0,0,0}, {{{0,0,0},{0,0,0},{0,0,0}}}, false, 1};
  qa_mat_project_fn project_fn = ctx->context.project;
  if (world_space) { project_fn.ctx = model ? &model_copy : NULL; project_fn.project = world_point_project; }
  qa_legacy_material_draw_context lctx; memset(&lctx, 0, sizeof lctx);
  lctx.has_entity_rgba = true; lctx.entity_rgba = ctx->context.entity_rgba;
  lctx.time = ctx->context.time;
  lctx.animation_frame = input->has_animation_frame ? input->animation_frame : trunc(ctx->context.time * 2);
  lctx.alternate_animation = input->has_alternate_animation && input->alternate_animation;
  lctx.fullbright = is_str(material->kind, "q1") ? fb_for_anim : surface->fullbright;
  lctx.q1_fog_active = input->has_q1_fog && input->q1_fog.density > 0;
  lctx.q1_lightmap_encoding = surface->legacy_lightmap ? surface->legacy_lightmap->encoding : "rgb";
  qa_batch_lighting fragment; memset(&fragment, 0, sizeof fragment);
  world_arrays arrays; memset(&arrays, 0, sizeof arrays);
  if (input->has_q2_fragment_lighting) {
    size_t nl = 0;
    const qa_q2_fragment_light *fl = fragment_lights(scene, input->fragment_lights, input->fragment_light_count, is_str(material->kind, "q1"), &nl);
    build_world_arrays(scene, surface->geometry, model, store, &arrays);
    fragment.kind = "q2-world"; fragment.pass = "texture";
    fragment.world_positions = arrays.positions; fragment.world_position_count = arrays.count;
    fragment.normals = arrays.normals; fragment.normal_count = arrays.count;
    fragment.lights = fl; fragment.light_count = nl; fragment.atlas = input->fragment_atlas;
    lctx.fragment_lighting = &fragment;
  }
  if (surface->legacy_lightmap) { lctx.has_translucent_lightmap = true; lctx.translucent_lightmap = surface->legacy_lightmap->direct; }
  int mirror_flip = ctx->context.deform_view.mirror != (model && qa_scene_model_scale(*model) < 0);
  lctx.cull = mirror_flip ? "back" : "front";
  lctx.depth_range[0] = ctx->context.depth_range[0]; lctx.depth_range[1] = ctx->context.depth_range[1];
  lctx.project = project_fn;
  qa_sequence_phase phase = material->alpha * ctx->context.entity_rgba.w / 255 < 1 ? QA_SEQUENCE_TRANSLUCENT : QA_SEQUENCE_OPAQUE;
  if (cacheable && input->has_q2_fragment_lighting) {
    int slot = -1; for (size_t i = 0; i < o->nfc; i++) if (o->frag_cache[i].index == surface->index) slot = (int)i;
    if (slot < 0 || strcmp(o->frag_cache[slot].key, cache_key)) {
      qa_legacy_material_draw_context empty_ctx = lctx;
      qa_batch_lighting empty_frag = fragment; empty_frag.lights = NULL; empty_frag.light_count = 0;
      empty_ctx.fragment_lighting = &empty_frag;
      op_storage *tstore = op_storage_new();
      size_t nb = 0;
      qa_draw_batch *batches = qa_prepare_legacy_material_batches(material, surface->geometry, &empty_ctx, &nb);
      track_batches(tstore, batches, nb);
      qa_draw_batch *world_batches = xalloc((nb ? nb : 1) * sizeof *world_batches); track_scratch(tstore, world_batches);
      for (size_t i = 0; i < nb; i++) { world_batches[i] = batches[i]; world_batches[i].space = "world"; }
      if (slot >= 0) { op_storage_destroy(o->frag_cache[slot].storage); free(o->frag_cache[slot].key); }
      else { if (o->nfc == o->cfc) { o->cfc = o->cfc ? o->cfc * 2 : 8; o->frag_cache = xg(o->frag_cache, o->cfc * sizeof *o->frag_cache); } slot = (int)o->nfc++; }
      o->frag_cache[slot] = (struct frag_cache){ surface->index, xdup(cache_key), world_batches, nb, tstore };
    }
    size_t nl = 0;
    const qa_q2_fragment_light *fl = fragment_lights(scene, input->fragment_lights, input->fragment_light_count, is_str(material->kind, "q1"), &nl);
    qa_draw_batch *batches = xalloc((o->frag_cache[slot].count ? o->frag_cache[slot].count : 1) * sizeof *batches); track_scratch(store, batches);
    for (size_t i = 0; i < o->frag_cache[slot].count; i++) {
      batches[i] = o->frag_cache[slot].batches[i];
      if (is_str(batches[i].lighting.kind, "q2-world") && !is_str(batches[i].lighting.pass, "model")) {
        batches[i].lighting.lights = fl; batches[i].lighting.light_count = nl; batches[i].lighting.atlas = input->fragment_atlas;
      }
    }
    op_push_group(store, qa_sequence_draw_group(phase, batches, o->frag_cache[slot].count));
    return;
  }
  size_t nb = 0;
  qa_draw_batch *batches = qa_prepare_legacy_material_batches(material, surface->geometry, &lctx, &nb);
  track_batches(store, batches, nb);
  qa_draw_batch *final_batches = batches;
  if (world_space && nb) {
    final_batches = xalloc(nb * sizeof *final_batches); track_scratch(store, final_batches);
    for (size_t i = 0; i < nb; i++) { final_batches[i] = batches[i]; final_batches[i].space = "world"; }
  }
  if (cacheable) {
    /* cache under scene storage: move a copy of the group into a scene-owned store */
    op_storage *cstore = op_storage_new();
    op_push_group(cstore, qa_sequence_draw_group(phase, final_batches, nb));
    int slot = -1; for (size_t i = 0; i < o->nlc; i++) if (o->legacy_cache[i].index == surface->index) slot = (int)i;
    if (slot >= 0) { op_storage_destroy(o->legacy_cache[slot].storage); free(o->legacy_cache[slot].key); o->legacy_cache[slot].key = xdup(cache_key); o->legacy_cache[slot].storage = cstore; }
    else { if (o->nlc == o->clc) { o->clc = o->clc ? o->clc * 2 : 8; o->legacy_cache = xg(o->legacy_cache, o->clc * sizeof *o->legacy_cache); } o->legacy_cache[o->nlc++] = (struct batch_cache){ surface->index, xdup(cache_key), cstore }; }
    for (size_t i = 0; i < cstore->count; i++) op_push(store, cstore->ops[i]);
    return;
  }
  op_push_group(store, qa_sequence_draw_group(phase, final_batches, nb));
}
/* ---- prepare model / world / view ---- */
typedef struct range_info { double first, count; qa_bounds bounds; } range_info;
static range_info model_range(qa_world_scene *scene, double model_index) {
  qa_decoded_world map = scene->map;
  long i = (long)model_index;
  size_t count = map.kind == QA_DECODED_WORLD_Q1 ? map.as.q1->model_count : map.kind == QA_DECODED_WORLD_Q2 ? map.as.q2->model_count : map.as.q3->model_count;
  if (model_index != (double)i || i < 0 || (size_t)i >= count) qa_range_error("Scene world index %g outside %zu", model_index, count);
  if (map.kind == QA_DECODED_WORLD_Q3) { const qa_q3_bsp_model *m = &map.as.q3->models[i]; return (range_info){ m->surfaces.first, m->surfaces.count, m->bounds }; }
  if (map.kind == QA_DECODED_WORLD_Q1) { const qa_q1_world_model *m = &map.as.q1->models[i]; return (range_info){ m->faces.first, m->faces.count, m->bounds }; }
  const qa_q2_world_model *m = &map.as.q2->models[i]; return (range_info){ m->faces.first, m->faces.count, m->bounds };
}
void qa_world_scene_prepare_model(qa_world_scene *scene, double model_index, qa_scene_model_transform transform, const qa_world_view_input *input, const qa_source_entity_order *entity, qa_world_operations *out) {
  memset(out, 0, sizeof *out);
  op_storage *store = op_storage_new(); out->_storage = store;
  range_info range = model_range(scene, model_index);
  qa_world_material_context ctx; qa_world_scene_material_context(scene, input, &transform, NULL, &ctx);
  const qa_q3_dynamic_light *lights = input->visibility.q3_lights; size_t nlights = input->visibility.q3_light_count;
  if (nlights > 32) qa_range_error("Q3 world lighting supports the source 32-light mask");
  double scale = qa_scene_model_scale(transform);
  int source_inline = scene->map.kind == QA_DECODED_WORLD_Q3 && scale == 1 && input->source && entity;
  qa_q3_dynamic_light *transformed = NULL;
  if (source_inline) { transformed = qa_transform_dlights(lights, nlights, transform.origin, transform.axis); track_scratch(store, transformed); }
  else {
    transformed = xalloc((nlights ? nlights : 1) * sizeof *transformed); track_scratch(store, transformed);
    for (size_t i = 0; i < nlights; i++) { transformed[i] = lights[i]; transformed[i].origin = qa_scene_local_point(lights[i].origin, transform); transformed[i].radius = lights[i].radius / fabs(scale); }
  }
  int incoming = source_inline ? (qa_bmodel_dlight_mask(transformed, nlights, range.bounds) != 0 ? 1 : 0)
                               : (nlights == 32 ? -1 : (int)(((size_t)1 << nlights) - 1));
  if (scene->map.kind == QA_DECODED_WORLD_Q3 && !input->visibility.no_cull && local_box_culled(range.bounds, input->camera, &transform)) { out->operations = NULL; out->count = 0; return; }
  for (long offset = 0; offset < (long)range.count; offset++) {
    qa_world_surface *surface = surface_at(scene, range.first + offset);
    if (input->source && entity && !admit_surface(scene, input->source, surface->index)) continue;
    if (surface->is_q3 && surface_culled(scene, surface, input, &ctx.context, &transform)) continue;
    int mask = frontend_light_mask(scene, surface, incoming, source_inline ? lights : transformed, nlights);
    int has_order = input->source && entity;
    qa_source_surface_order order = has_order ? source_order_for(surface, input->source, *entity, mask) : (qa_source_surface_order){ 0 };
    /* TS passes transformed lights unless sourceInline (then original incoming semantics via mask fn input lights arg) */
    surface_operations(scene, surface, input, &ctx, &transform, 1, mask, transformed, nlights, has_order, order, store);
  }
  out->operations = store->ops; out->count = store->count;
}
void qa_world_scene_prepare_world_operations(qa_world_scene *scene, const qa_world_view_input *input, const qa_visible_world *visibility_in, qa_world_operations *out) {
  memset(out, 0, sizeof *out);
  op_storage *store = op_storage_new(); out->_storage = store;
  if (input->no_world_model) { out->count = 0; return; }
  qa_visible_world computed; const qa_visible_world *visibility = visibility_in;
  if (!visibility) {
    qa_world_view_input vin = *input;
    qa_visible_world_new(scene->map, input->camera, &vin.visibility, &scene->visibility_cache, &computed);
    visibility = &computed;
  }
  if (input->source) {
    adm_impl *a = (adm_impl *)input->source->_impl;
    for (size_t i = 0; i < a->count; i++) if (a->entries[i].scene == scene && a->entries[i].has_ops) {
      op_storage_destroy(store);
      out->_storage = NULL; out->operations = a->entries[i].ops_storage->ops; out->count = a->entries[i].ops_storage->count;
      if (!visibility_in) qa_visible_world_destroy(&computed);
      return;
    }
  }
  qa_world_material_context ctx; qa_world_scene_material_context(scene, input, NULL, NULL, &ctx);
  range_info world_range;
  if ((scene->map.kind == QA_DECODED_WORLD_Q3 ? scene->map.as.q3->model_count : scene->map.kind == QA_DECODED_WORLD_Q1 ? scene->map.as.q1->model_count : scene->map.as.q2->model_count) == 0)
    world_range = (range_info){ 0, (double)scene->surface_count, scene->bounds };
  else world_range = model_range(scene, 0);
  qa_world_surface **surfaces = xalloc((visibility->surface_count ? visibility->surface_count : 1) * sizeof(void *));
  size_t ns = 0;
  for (size_t i = 0; i < visibility->surface_count; i++) {
    qa_world_surface *surface = surface_at(scene, visibility->surfaces[i]);
    if (scene->map.kind == QA_DECODED_WORLD_Q3 || (surface->index >= world_range.first && surface->index < world_range.first + world_range.count)) surfaces[ns++] = surface;
  }
  int any_null_shader = 0; for (size_t i = 0; i < ns; i++) if (!surfaces[i]->has_shader) any_null_shader = 1;
  if (any_null_shader) {
    /* stable sort by order */
    for (size_t i = 1; i < ns; i++) {
      qa_world_surface *t = surfaces[i]; size_t j = i;
      while (j) {
        qa_scene_material_remap r1, r2;
        qa_world_surface *a = surfaces[j-1], *b = t;
        double oa, ob;
        qa_registered_scene_material sa = surface_remap(scene, a, &r1) ? r1.material : a->shader;
        qa_registered_scene_material sb = surface_remap(scene, b, &r2) ? r2.material : b->shader;
        oa = (a->has_shader || surface_remap(scene, a, &r1)) ? qa_registered_scene_material_finished(sa)->sort
           : (a->has_q1_sky || (is_str(a->material.kind, "q2") && (i32d(a->material.surface_flags) & 4)) ? 2 : a->material.alpha < 1 ? 9 : 3);
        ob = (b->has_shader || surface_remap(scene, b, &r2)) ? qa_registered_scene_material_finished(sb)->sort
           : (b->has_q1_sky || (is_str(b->material.kind, "q2") && (i32d(b->material.surface_flags) & 4)) ? 2 : b->material.alpha < 1 ? 9 : 3);
        if (oa <= ob) break;
        surfaces[j] = surfaces[j-1]; j--;
      }
      surfaces[j] = t;
    }
  }
  qa_plane frustum[5]; size_t nfrustum = qa_scene_camera_frustum(input->camera, frustum);
  const qa_q3_dynamic_light *lights = input->visibility.q3_lights; size_t nlights = input->visibility.q3_light_count;
  for (size_t i = 0; i < ns; i++) {
    qa_world_surface *surface = surfaces[i];
    if (input->source && !admit_surface(scene, input->source, surface->index)) continue;
    if (surface->is_q3 ? surface_culled(scene, surface, input, &ctx.context, NULL)
                        : (!input->visibility.no_cull && !qa_scene_bounds_in_frustum(surface->bounds, frustum, nfrustum))) continue;
    int have_mask = 0, incoming = 0;
    for (size_t k = 0; k < visibility->mask_count; k++) if (visibility->mask_surfaces[k] == surface->index) { have_mask = 1; incoming = visibility->mask_values[k]; }
    if (!have_mask) qa_range_error("Visible surface lost its first-encounter light mask");
    int mask = frontend_light_mask(scene, surface, incoming, lights, nlights);
    int has_order = input->source != NULL;
    qa_source_surface_order ord = has_order ? source_order_for(surface, input->source, (qa_source_entity_order){ false, 0 }, mask) : (qa_source_surface_order){ 0 };
    surface_operations(scene, surface, input, &ctx, NULL, 1, mask, lights, nlights, has_order, ord, store);
  }
  free(surfaces);
  if (!visibility_in) qa_visible_world_destroy(&computed);
  if (input->source) {
    adm_impl *a = (adm_impl *)input->source->_impl;
    size_t slot = a->count;
    for (size_t i = 0; i < a->count; i++) if (a->entries[i].scene == scene) slot = i;
    if (slot == a->count) {
      if (a->count == a->cap) { a->cap = a->cap ? a->cap * 2 : 4; a->entries = xg(a->entries, a->cap * sizeof *a->entries); }
      memset(&a->entries[a->count], 0, sizeof a->entries[0]); a->entries[a->count].scene = scene; slot = a->count++;
    }
    a->entries[slot].ops_storage = store; a->entries[slot].has_ops = 1;
    out->_storage = NULL; /* admission owns */
  }
  out->operations = store->ops; out->count = store->count;
}
void qa_prepared_world_view_destroy(qa_prepared_world_view *v) {
  if (!v) return;
  qa_visible_world_destroy(&v->visibility);
  free(v->image_operations);
  /* storage variants crammed into _storage list */
  void **list = v->_storage;
  if (list) {
    qa_world_operations *parts = list[0]; size_t n = (size_t)(uintptr_t)list[1];
    if (parts) for (size_t i = 0; i < n; i++) qa_world_operations_destroy(&parts[i]);
    free(parts);
    if (list[2]) { qa_merged_operations *m = list[2]; qa_merged_operations_destroy(m); free(m); }
    if (list[3]) { qa_finished_operations *f = list[3]; qa_finished_operations_destroy(f); free(f); }
    if (list[4]) { qa_scene_fogged_operations *g = list[4]; qa_scene_fogged_operations_destroy(g); free(g); }
    free(list[5]); /* sky images view storage */
    free(list);
  }
  memset(v, 0, sizeof *v);
}
void qa_world_scene_prepare_view(qa_world_scene *scene, const qa_world_view_input *input, qa_prepared_world_view *out) {
  memset(out, 0, sizeof *out);
  if (input->no_world_model) {
    out->visibility.leaf = -1;
  } else {
    qa_world_visibility_options vopts = input->visibility;
    qa_visible_world_new(scene->map, input->camera, &vopts, &scene->visibility_cache, &out->visibility);
  }
  /* gather scene operations */
  qa_world_operations *parts = xalloc((2 + input->inline_model_count) * sizeof *parts);
  memset(parts, 0, (2 + input->inline_model_count) * sizeof *parts);
  size_t nparts = 0;
  qa_world_scene_prepare_world_operations(scene, input, &out->visibility, &parts[nparts++]);
  for (size_t i = 0; i < input->inline_model_count; i++) {
    qa_world_view_input child = *input;
    const qa_world_inline_model *model = &input->inline_models[i];
    child.has_animation_frame = true;
    child.animation_frame = model->has_animation_frame ? model->animation_frame : input->has_animation_frame ? input->animation_frame : 0;
    if (model->has_entity_rgba) { child.has_material_entity_rgba = true; child.material_entity_rgba = model->entity_rgba; }
    if (model->has_alternate_animation && !child.has_alternate_animation) { child.has_alternate_animation = true; child.alternate_animation = model->alternate_animation; }
    if (!child.has_alternate_animation) { child.has_alternate_animation = true; child.alternate_animation = false; }
    qa_world_scene_prepare_model(scene, model->model, model->transform, &child, NULL, &parts[nparts++]);
  }
  size_t total = input->operation_count;
  for (size_t i = 0; i < nparts; i++) total += parts[i].count;
  int has_q2fog = !input->no_world_model && input->has_q2_fog;
  qa_scene_operation *combined = xalloc(((total + 1) ? total + 1 : 1) * sizeof *combined);
  size_t w = 0;
  for (size_t i = 0; i < nparts; i++) { memcpy(combined + w, parts[i].operations, parts[i].count * sizeof *combined); w += parts[i].count; }
  for (size_t i = 0; i < input->operation_count; i++) combined[w++] = input->operations[i];
  if (has_q2fog) {
    int sky_drawn = 0;
    for (size_t i = 0; i < out->visibility.surface_count; i++) {
      qa_world_surface *surface = surface_at(scene, out->visibility.surfaces[i]);
      qa_scene_material_remap rem;
      int has_shader = surface->has_shader || surface_remap(scene, surface, &rem);
      qa_registered_scene_material shader = surface_remap(scene, surface, &rem) ? rem.material : surface->shader;
      if (has_shader) { if (is_str(qa_registered_scene_material_finished(shader)->iterator.kind, "sky")) sky_drawn = 1; }
      else if (!surface->is_q3 && is_str(surface->material.kind, "q2") && (i32d(surface->material.surface_flags) & 4)) sky_drawn = 1;
    }
    qa_scene_operation op; memset(&op, 0, sizeof op);
    op.operation.kind = QA_RENDER_OP_Q2_FOG;
    op.operation.fog.camera = input->camera; op.operation.fog.fog = input->q2_fog;
    op.operation.fog.far_depth = 1 - 1e-6; op.operation.fog.sky_drawn = sky_drawn;
    combined[w++] = op;
  }
  qa_finished_operations *finished = xalloc(sizeof *finished); memset(finished, 0, sizeof *finished);
  qa_finish_scene_operations(combined, w, finished);
  qa_scene_fogged_operations *fogged = xalloc(sizeof *fogged); memset(fogged, 0, sizeof *fogged);
  size_t fogged_count = 0;
  const qa_render_operation *fogged_ops = qa_fog_scene_operations(finished->operations, finished->count,
    (input->no_world_model || !input->has_q1_fog) ? NULL : &input->q1_fog, fogged, &fogged_count);
  qa_merged_operations *merged = xalloc(sizeof *merged); memset(merged, 0, sizeof *merged);
  qa_merge_draw_operations(fogged_ops, fogged_count, merged);
  out->image_operations = qa_scene_image_registry_drain_operations(scene->shaders->textures->images, &out->image_operation_count);
  qa_render_view *view = &out->view; memset(view, 0, sizeof *view);
  view->target_is_seat = input->target_is_seat; view->target_seat = input->target_seat; view->target_preview_id = input->target_preview_id;
  view->time = input->time;
  view->state.viewport = input->camera.viewport;
  qa_render_view_clear default_clear; memset(&default_clear, 0, sizeof default_clear); default_clear.depth = 1;
  static qa_render_view_clear stored_clear;
  stored_clear = input->clear ? *input->clear : default_clear;
  view->state.clear = &stored_clear;
  qa_vec4 clip; int has_clip = qa_scene_portal_clip_plane(input->camera, &clip);
  static qa_vec4 clip_storage;
  if (has_clip) { clip_storage = clip; view->state.clip_plane = &clip_storage; }
  view->state.has_view_projection = true; view->state.view_projection = qa_scene_view_projection_matrix(input->camera);
  view->before_view = input->before_view; view->before_view_count = input->before_view_count;
  view->operations = merged->operations; view->operation_count = merged->count;
  void **list = xalloc(6 * sizeof(void *));
  list[0] = parts; list[1] = (void *)(uintptr_t)nparts; list[2] = merged; list[3] = finished; list[4] = fogged; list[5] = combined;
  out->_storage = list;
}
void qa_world_scene_prepare_views(qa_world_scene *scene, const qa_world_view_input *input, const qa_portal_entity *portals, size_t portal_count, qa_prepared_world_view *out_views, size_t *out_count) {
  size_t n = 0;
  if (input->no_world_model) { qa_world_scene_prepare_view(scene, input, &out_views[n++]); *out_count = n; return; }
  if (!input->camera.clip_is_portal && portal_count) {
    qa_visible_world visible; qa_world_visibility_options vopts = input->visibility;
    qa_visible_world_new(scene->map, input->camera, &vopts, &scene->visibility_cache, &visible);
    for (size_t i = 0; i < visible.surface_count; i++) {
      qa_world_surface *surface = surface_at(scene, visible.surfaces[i]);
      qa_scene_material_remap rem;
      int has_shader = surface->has_shader || surface_remap(scene, surface, &rem);
      if (!has_shader) continue;
      qa_registered_scene_material shader = surface_remap(scene, surface, &rem) ? rem.material : surface->shader;
      if (qa_registered_scene_material_finished(shader)->sort != 1 || !surface->has_plane) continue;
      double milliseconds = input->time.kind == QA_SOURCE_TIME_SECONDS ? input->time.value * 1000 : input->time.value;
      qa_portal_camera child;
      if (!qa_portal_camera_new(surface->plane, portals, portal_count, input->camera, milliseconds, NULL, &child)) continue;
      if (qa_portal_surface_offscreen(surface->geometry, input->camera, qa_registered_scene_material_material(shader)->portal_range, child.mirror)) continue;
      qa_world_view_input child_input = *input;
      child_input.camera = child.camera;
      child_input.visibility.has_pvs_origin = true; child_input.visibility.pvs_origin = child.pvs_origin;
      child_input.visibility.has_visible_areas = false; child_input.visibility.visible_areas = NULL; child_input.visibility.visible_area_count = 0;
      child_input.operations = NULL; child_input.operation_count = 0;
      child_input.before_view = NULL; child_input.before_view_count = 0;
      qa_source_scene_order *child_order = NULL; qa_world_surface_admission *child_adm = NULL;
      if (input->source) {
        child_order = xalloc(sizeof *child_order); qa_source_scene_order_init(child_order, input->source->view->registrations);
        child_adm = xalloc(sizeof *child_adm); qa_create_world_surface_admission(child_order, child_adm);
        child_input.source = child_adm;
      }
      qa_world_scene_prepare_view(scene, &child_input, &out_views[n++]);
      /* child admission/order leak with the returned view (TS retains them in the view build) */
      (void)child_order; (void)child_adm;
      break;
    }
    qa_visible_world_destroy(&visible);
  }
  qa_world_view_input parent = *input;
  qa_render_view_clear parent_clear;
  if (n && input->clear) {
    parent_clear = *input->clear; parent_clear.color = NULL;
    parent.clear = &parent_clear;
  }
  qa_world_scene_prepare_view(scene, &parent, &out_views[n++]);
  *out_count = n;
}
/* ---- shadows ---- */
static const qa_material_geometry *shadow_geometry_for(qa_world_scene *scene, qa_world_surface *surface, qa_world_material_context *ctx, int entity, qa_scene_shadow_geometry *holder, int *uses_holder) {
  *uses_holder = 0;
  wimpl *o = wi(scene);
  int has_raw = 0; for (size_t i = 0; i < o->nraw; i++) if (o->raw[i].index == surface->index) has_raw = 1;
  if (surface->has_shader || has_raw) {
    if (surface->is_q3 && surface->flare) return NULL;
    qa_scene_material_remap rem; int has_rem = surface_remap(scene, surface, &rem);
    if (!has_rem && !surface->has_shader) qa_range_error("Remapped shadow material is absent");
    qa_registered_scene_material shader = has_rem ? rem.material : surface->shader;
    qa_world_material_context copy = *ctx; copy.context.time_offset = has_rem ? rem.time_offset : 0;
    qa_shadow_material_geometry(qa_registered_scene_material_compiled(shader), surface->geometry, &copy.context, holder);
    if (!holder->has_geometry) return NULL;
    *uses_holder = 1;
    return &holder->geometry;
  }
  if (surface->is_q3) qa_range_error("Compiled surface lost its shader");
  const qa_render_material *material = &surface->material;
  if (is_str(material->kind, "q1"))
    return ((!is_str(material->surface, "ordinary") && !is_str(material->surface, "fence")) || material->alpha < 1) ? NULL : &surface->geometry;
  if ((i32d(material->surface_flags) & (4 | 8 | 128)) || (entity && (i32d(material->surface_flags) & (16 | 32)))) return NULL;
  return &surface->geometry;
}
void qa_world_shadow_model_destroy(qa_world_shadow_model *m) {
  if (!m) return;
  if (m->owned_positions) for (size_t i = 0; i < m->mesh_count; i++) free(m->owned_positions[i]);
  free(m->owned_positions); free(m->meshes);
  op_storage_destroy(m->_storage);
  memset(m, 0, sizeof *m);
}
void qa_world_scene_shadow_model(qa_world_scene *scene, double model_index, qa_scene_model_transform transform, const qa_world_view_input *input, qa_world_shadow_model *out) {
  memset(out, 0, sizeof *out);
  op_storage *store = op_storage_new(); out->_storage = store;
  range_info range = model_range(scene, model_index);
  qa_world_material_context ctx; qa_world_scene_material_context(scene, input, &transform, NULL, &ctx);
  qa_shadow_mesh *meshes = xalloc(((size_t)range.count ? (size_t)range.count : 1) * sizeof *meshes);
  qa_vec3 **positions = xalloc(((size_t)range.count ? (size_t)range.count : 1) * sizeof(void *));
  size_t n = 0;
  for (long i = (long)range.first; i < (long)range.first + (long)range.count; i++) {
    qa_world_surface *surface = surface_at(scene, (double)i);
    qa_scene_shadow_geometry holder; int uses = 0;
    const qa_material_geometry *geometry = shadow_geometry_for(scene, surface, &ctx, 1, &holder, &uses);
    if (geometry) {
      qa_vec3 *pos = xalloc((geometry->vertex_count ? geometry->vertex_count : 1) * sizeof *pos);
      for (size_t k = 0; k < geometry->vertex_count; k++) pos[k] = qa_scene_world_point(geometry->vertices[k].position, transform);
      positions[n] = pos;
      meshes[n] = (qa_shadow_mesh){ pos, geometry->vertex_count, geometry->indices, geometry->index_count };
      n++;
    }
    if (uses) qa_scene_shadow_geometry_destroy(&holder);
  }
  out->meshes = meshes; out->owned_positions = positions; out->mesh_count = n;
  out->caster = qa_shadow_caster_new(transform.origin, meshes, n);
}
bool qa_world_scene_prepare_shadows(qa_world_scene *scene, const qa_scene_light *lights, size_t light_count, const qa_world_view_input *input, const qa_shadow_caster *casters, size_t caster_count, const qa_shadow_atlas_options *options, qa_prepared_shadows *out) {
  wimpl *o = wi(scene);
  double revision = qa_scene_material_registrations_revision(scene->shaders->registrations->owner);
  if (revision != o->shadow_material_revision) { o->static_shadow.valid = 0; o->shadow_material_revision = revision; }
  range_info range;
  size_t model_count = scene->map.kind == QA_DECODED_WORLD_Q3 ? scene->map.as.q3->model_count : scene->map.kind == QA_DECODED_WORLD_Q1 ? scene->map.as.q1->model_count : scene->map.as.q2->model_count;
  range = model_count ? model_range(scene, 0) : (range_info){ 0, (double)scene->surface_count, scene->bounds };
  int enabled = !options || !options->has_enabled || options->enabled;
  int any_light = 0;
  for (size_t i = 0; i < light_count; i++) if (is_str(lights[i].profile.kind, "q2") && lights[i].radius > 0 && (lights[i].profile.has_cone || lights[i].profile.shadow_cast)) any_light = 1;
  qa_shadow_mesh *meshes = NULL; size_t nmeshes = 0;
  qa_scene_shadow_mesh_owned *owned_meshes = NULL;
  const qa_shadow_mesh *world_meshes = NULL; size_t world_count = 0;
  qa_static_shadow_world *static_world = NULL;
  if (enabled && any_light) {
    if (o->static_shadow.valid && o->static_shadow.surfaces == scene->surfaces && o->static_shadow.first == range.first && o->static_shadow.count == range.count) {
      static_world = &o->static_shadow.world;
    } else {
      o->static_shadow.valid = 0;
      qa_world_material_context ctx; qa_world_scene_material_context(scene, input, NULL, NULL, &ctx);
      meshes = xalloc(((size_t)range.count ? (size_t)range.count : 1) * sizeof *meshes);
      owned_meshes = xalloc(((size_t)range.count ? (size_t)range.count : 1) * sizeof *owned_meshes);
      int static_ok = 1;
      for (long i = (long)range.first; i < (long)range.first + (long)range.count; i++) {
        qa_world_surface *surface = surface_at(scene, (double)i);
        qa_scene_material_remap rem;
        qa_registered_scene_material shader = surface_remap(scene, surface, &rem) ? rem.material : surface->shader;
        if (surface->has_shader || surface_remap(scene, surface, &rem))
          if (qa_registered_scene_material_registered(shader)->definition.deform_count != 0) static_ok = 0;
        qa_scene_shadow_geometry holder; int uses = 0;
        const qa_material_geometry *geometry = shadow_geometry_for(scene, surface, &ctx, 0, &holder, &uses);
        if (geometry) { qa_shadow_mesh_new(*geometry, &owned_meshes[nmeshes]); meshes[nmeshes] = owned_meshes[nmeshes].mesh; nmeshes++; }
        if (uses) qa_scene_shadow_geometry_destroy(&holder);
      }
      if (static_ok) {
        if (o->static_shadow.world.meshes) qa_static_shadow_world_destroy(&o->static_shadow.world);
        qa_static_shadow_world_init(&o->static_shadow.world, meshes, nmeshes);
        o->static_shadow.surfaces = scene->surfaces; o->static_shadow.first = range.first; o->static_shadow.count = range.count; o->static_shadow.valid = 1;
        static_world = &o->static_shadow.world;
      } else { world_meshes = meshes; world_count = nmeshes; }
    }
  }
  /* inline casters */
  size_t total_casters = caster_count;
  qa_world_shadow_model *inline_models = xalloc((input->inline_model_count ? input->inline_model_count : 1) * sizeof *inline_models);
  size_t ninline = 0;
  for (size_t i = 0; i < input->inline_model_count; i++) {
    if (input->inline_models[i].has_casts_shadow && !input->inline_models[i].casts_shadow) continue;
    qa_world_scene_shadow_model(scene, input->inline_models[i].model, input->inline_models[i].transform, input, &inline_models[ninline++]);
  }
  qa_shadow_caster *all_casters = xalloc(((total_casters + ninline) ? total_casters + ninline : 1) * sizeof *all_casters);
  memcpy(all_casters, casters, caster_count * sizeof *all_casters);
  for (size_t i = 0; i < ninline; i++) all_casters[caster_count++] = inline_models[i].caster;
  bool ok = qa_q2_shadow_scene_prepare(&o->shadow_scene, lights, light_count, world_meshes, world_count, static_world, all_casters, caster_count, options, out);
  for (size_t i = 0; i < ninline; i++) qa_world_shadow_model_destroy(&inline_models[i]);
  free(inline_models); free(all_casters);
  if (owned_meshes) { for (size_t i = 0; i < nmeshes; i++) qa_scene_shadow_mesh_owned_destroy(&owned_meshes[i]); free(owned_meshes); }
  free(meshes);
  return ok;
}
/* ---- close / images / remap ---- */
static void release_images(qa_world_scene *scene) {
  wimpl *o = wi(scene);
  o->static_shadow.valid = 0;
  for (size_t i = 0; i < o->nstyles; i++) free(o->styles[i].styles);
  o->nstyles = 0;
  qa_q2_shadow_scene_close(&o->shadow_scene);
  for (size_t i = 0; i < o->nowned; i++) qa_scene_image_registry_release(scene->shaders->textures->images, o->owned[i]);
  o->nowned = 0;
}
void qa_world_scene_close(qa_world_scene *scene) {
  wimpl *o = wi(scene);
  if (!o || o->closed) return;
  o->closed = 1; o->image_generation++;
  qa_scene_material_registrations_release_world(scene->shaders->registrations->owner, qa_world_scene_world_service(scene));
  release_images(scene);
}
void qa_world_scene_destroy(qa_world_scene *scene) {
  if (!scene || !scene->_impl) return;
  if (!wi(scene)->closed) qa_world_scene_close(scene);
  wimpl *o = wi(scene);
  qa_q2_shadow_scene_destroy(&o->shadow_scene);
  qa_world_visibility_cache_destroy(&scene->visibility_cache);
  free(scene->surfaces); free(scene->fog_selections);
  free(o->owned); free(o->q2_sky); free(o->fb_map); free(o->raw);
  for (size_t i = 0; i < o->nstyles; i++) free(o->styles[i].styles);
  free(o->styles);
  for (size_t i = 0; i < o->nwa; i++) { free(o->warrays[i].positions); free(o->warrays[i].normals); }
  free(o->warrays);
  for (size_t i = 0; i < o->nlc; i++) { free(o->legacy_cache[i].key); op_storage_destroy(o->legacy_cache[i].storage); }
  free(o->legacy_cache);
  for (size_t i = 0; i < o->nfc; i++) { free(o->frag_cache[i].key); op_storage_destroy(o->frag_cache[i].storage); }
  free(o->frag_cache); free(o->frag_all); free(o->frag_shadow);
  if (o->static_shadow.world.meshes) qa_static_shadow_world_destroy(&o->static_shadow.world);
  for (size_t i = 0; i < o->nov; i++) { free(o->owned_verts[i]); free(o->owned_indices[i]); }
  free(o->owned_verts); free(o->owned_indices);
  for (size_t i = 0; i < o->npatch; i++) qa_patch_mesh_destroy(&o->patch_meshes[i]);
  free(o->patch_meshes); free(o->grids);
  for (size_t i = 0; i < o->nbrush; i++) qa_prepared_brush_face_destroy(&o->brush_faces[i]);
  free(o->brush_faces); free(o->lightmaps);
  for (size_t i = 0; i < o->nname; i++) free(o->names[i]);
  free(o->names);
  for (size_t i = 0; i < o->nanim; i++) qa_q1_texture_animations_destroy(&o->animations[i]);
  free(o->animations); free(o->built);
  for (size_t i = 0; i < o->nsplit; i++) qa_split_sky_texture_destroy(&o->splits[i]);
  free(o->splits); free(o->fog_volumes);
  for (size_t i = 0; i < o->nscratch; i++) free(o->scratch[i]);
  free(o->scratch);
  free(o);
  memset(scene, 0, sizeof *scene);
}
static qa_scene_shader_binding remap_binding_for(qa_world_scene *scene, qa_world_surface *surface) {
  qa_scene_shader_binding b; memset(&b, 0, sizeof b);
  b.is_world = true; b.world = scene->material_world;
  const qa_renderer_image *lightmap = surface->is_q3 ? surface->lightmap_image
    : (surface->legacy_lightmap ? (const qa_renderer_image *)&surface->legacy_lightmap->image : NULL);
  if (lightmap) { b.has_lightmap = true; b.lightmap = *lightmap; }
  b.lightmap_index = surface->has_shader ? qa_registered_scene_material_finished(surface->shader)->lightmap_index
    : (lightmap ? surface->index : -1);
  b.base_texture = surface->base_texture;
  return b;
}
qa_scene_raw_remap_binding *qa_world_scene_raw_remap_bindings(qa_world_scene *scene, const char *name, size_t *out_count) {
  char *key = material_name(name);
  qa_scene_raw_remap_binding *result = xalloc((scene->surface_count ? scene->surface_count : 1) * sizeof *result);
  size_t n = 0;
  for (size_t i = 0; i < scene->surface_count; i++) {
    qa_world_surface *surface = &scene->surfaces[i];
    if (surface->has_shader) continue;
    char *k = material_name(surface->shader_name);
    if (!strcmp(k, key)) result[n++] = (qa_scene_raw_remap_binding){ surface->index, remap_binding_for(scene, surface) };
    free(k);
  }
  free(key);
  if (out_count) *out_count = n;
  return result;
}
void qa_world_scene_publish_raw_remap(qa_world_scene *scene, const char *name, const qa_registered_scene_material *materials, size_t count, qa_scene_shader_registry *source, double time_offset) {
  wimpl *o = wi(scene);
  size_t nb = 0; qa_scene_raw_remap_binding *bindings = qa_world_scene_raw_remap_bindings(scene, name, &nb);
  for (size_t i = 0; i < nb; i++)
    for (size_t k = 0; k < o->nraw; k++) if (o->raw[k].index == bindings[i].surface) { memmove(o->raw + k, o->raw + k + 1, (o->nraw - k - 1) * sizeof *o->raw); o->nraw--; break; }
  for (size_t i = 0; i < count; i++) {
    if (i >= nb) qa_range_error("Prepared remap lost its surface");
    if (o->nraw == o->craw) { o->craw = o->craw ? o->craw * 2 : 8; o->raw = xg(o->raw, o->craw * sizeof *o->raw); }
    o->raw[o->nraw].index = bindings[i].surface;
    o->raw[o->nraw].remap = (qa_scene_material_remap){ materials[i], time_offset, source };
    o->nraw++;
  }
  free(bindings);
  o->static_shadow.valid = 0;
}
typedef struct remap_current_ctx { qa_world_scene *scene; double generation; qa_scene_current_fn inner; void *inner_ctx; } remap_current_ctx;
static bool remap_current_cb(void *v) {
  remap_current_ctx *c = v; wimpl *o = wi(c->scene);
  return !o->closed && c->generation == o->image_generation && (!c->inner || c->inner(c->inner_ctx));
}
qa_shader_remap_result qa_world_scene_remap_shader(qa_world_scene *scene, const char *original, const char *replacement, double time_offset, qa_scene_shader_registry *source, qa_scene_current_fn current, void *current_ctx) {
  remap_current_ctx c = { scene, wi(scene)->image_generation, current, current_ctx };
  qa_scene_shader_registry *src = source ? source : scene->shaders;
  qa_scene_remap_request request;
  memset(&request, 0, sizeof request);
  request.original = original; request.replacement = replacement; request.time_offset = time_offset;
  request.source = qa_scene_shader_registry_remap_source(src);
  request.current = remap_current_cb; request.current_ctx = &c;
  return qa_scene_material_registrations_remap(scene->shaders->registrations->owner, request);
}
void qa_world_scene_validate_images(qa_world_scene *scene, qa_world_scene *replacement) {
  wimpl *o = wi(scene), *ro = wi(replacement);
  double revision = qa_scene_material_registrations_revision(scene->shaders->registrations->owner);
  if (o->closed || !ro->has_prepared_generation || ro->prepared_generation != o->image_generation
      || !ro->has_prepared_revision || ro->prepared_revision != revision)
    qa_range_error("World images or remaps changed during replacement");
}
void qa_world_scene_prepare_images(qa_world_scene *scene, qa_scene_shader_registry *shaders, qa_world_source_for_fn source_for, void *source_for_ctx, qa_world_scene *replacement) {
  wimpl *o = wi(scene);
  double generation = o->image_generation;
  double revision = qa_scene_material_registrations_revision(scene->shaders->registrations->owner);
  qa_world_scene_load(scene->map, shaders, &scene->options, replacement);
  wimpl *ro = wi(replacement);
  ro->has_prepared_generation = 1; ro->prepared_generation = generation;
  ro->has_prepared_revision = 1; ro->prepared_revision = revision;
  /* names of raw-remapped surfaces */
  for (size_t ni = 0; ni < o->nraw; ni++) {
    const char *name = surface_at(scene, o->raw[ni].index)->shader_name;
    int seen = 0; for (size_t k = 0; k < ni; k++) if (!strcmp(surface_at(scene, o->raw[k].index)->shader_name, name)) seen = 1;
    if (seen) continue;
    qa_scene_remap_definition def;
    if (!qa_scene_material_registrations_remap_definition(scene->shaders->registrations->owner, name, &def)) continue;
    qa_scene_shader_registry *source = source_for ? source_for(source_for_ctx, (qa_scene_shader_registry *)def.source) : shaders;
    /* replacement surfaces without shaders matching name */
    size_t count = 0;
    qa_scene_shader_binding *bindings = xalloc((replacement->surface_count ? replacement->surface_count : 1) * sizeof *bindings);
    qa_world_surface **matched = xalloc((replacement->surface_count ? replacement->surface_count : 1) * sizeof(void *));
    char *target = material_name(name);
    for (size_t i = 0; i < replacement->surface_count; i++) {
      qa_world_surface *surface = &replacement->surfaces[i];
      if (surface->has_shader) continue;
      char *k = material_name(surface->shader_name);
      if (!strcmp(k, target)) { bindings[count] = remap_binding_for(replacement, surface); matched[count] = surface; count++; }
      free(k);
    }
    free(target);
    qa_prepared_scene_remap prepared;
    qa_scene_shader_registry_prepare_remap_materials(source, def.replacement, bindings, count, &prepared);
    free(bindings);
    if (!prepared.accepted) { prepared.discard(prepared.ctx); qa_range_error("Current surface remap could not be rebuilt from its source images"); }
    prepared.commit(prepared.ctx);
    for (size_t i = 0; i < count; i++) {
      if (i >= prepared.material_count) qa_range_error("Refreshed surface remap is absent");
      if (ro->nraw == ro->craw) { ro->craw = ro->craw ? ro->craw * 2 : 8; ro->raw = xg(ro->raw, ro->craw * sizeof *ro->raw); }
      ro->raw[ro->nraw].index = matched[i]->index;
      ro->raw[ro->nraw].remap = (qa_scene_material_remap){ prepared.materials[i], def.time_offset, def.source };
      ro->nraw++;
    }
    free(matched);
  }
  qa_world_scene_validate_images(scene, replacement);
}
void qa_world_scene_commit_images(qa_world_scene *scene, qa_world_scene *replacement) {
  qa_world_scene_validate_images(scene, replacement);
  wimpl *o = wi(scene), *ro = wi(replacement);
  release_images(scene);
  o->image_generation++;
  /* adopt replacement surfaces/shader-retained materials and images */
  free(scene->surfaces);
  scene->surfaces = replacement->surfaces; scene->surface_count = replacement->surface_count;
  replacement->surfaces = NULL; replacement->surface_count = 0;
  qa_scene_material_registrations *owner = scene->shaders->registrations->owner;
  for (size_t i = 0; i < scene->surface_count; i++)
    if (scene->surfaces[i].has_shader) scene->surfaces[i].shader = qa_scene_material_registrations_retained(owner, scene->surfaces[i].shader.registration);
  free(scene->fog_selections);
  scene->fog_selections = replacement->fog_selections; scene->fog_selection_count = replacement->fog_selection_count;
  replacement->fog_selections = NULL; replacement->fog_selection_count = 0;
  scene->fog_image = replacement->fog_image; scene->dlight_image = replacement->dlight_image;
  /* move shadow scene + images ownership */
  qa_q2_shadow_scene_destroy(&o->shadow_scene);
  o->shadow_scene = ro->shadow_scene;
  memset(&ro->shadow_scene, 0, sizeof ro->shadow_scene);
  free(o->q2_sky); o->q2_sky = ro->q2_sky; o->q2_sky_count = ro->q2_sky_count; ro->q2_sky = NULL; ro->q2_sky_count = 0;
  free(o->fb_map); o->fb_map = ro->fb_map; o->nfb = ro->nfb; o->cfb = ro->cfb; ro->fb_map = NULL; ro->nfb = 0;
  for (size_t i = 0; i < ro->nowned; i++) w_owned(o, ro->owned[i]);
  ro->nowned = 0;
  /* move load-owned geometry/backing by adopting replacement impl lists */
  #define MOVE_LIST(field, n, c) do { free(o->field); o->field = ro->field; o->n = ro->n; o->c = ro->c; ro->field = NULL; ro->n = 0; ro->c = 0; } while (0)
  MOVE_LIST(owned_verts, nov, cov);
  o->owned_indices = ro->owned_indices; ro->owned_indices = NULL;
  MOVE_LIST(patch_meshes, npatch, cpatch);
  MOVE_LIST(grids, ngrid_store, cgrid_store);
  MOVE_LIST(brush_faces, nbrush, cbrush);
  MOVE_LIST(lightmaps, nlm, clm);
  MOVE_LIST(animations, nanim, canim);
  MOVE_LIST(splits, nsplit, csplit);
  MOVE_LIST(fog_volumes, nfog, cfog);
  MOVE_LIST(scratch, nscratch, cscratch);
  #undef MOVE_LIST
  o->nraw = 0;
  for (size_t i = 0; i < ro->nraw; i++) {
    if (o->nraw == o->craw) { o->craw = o->craw ? o->craw * 2 : 8; o->raw = xg(o->raw, o->craw * sizeof *o->raw); }
    o->raw[o->nraw] = ro->raw[i];
    o->raw[o->nraw].remap.material = qa_scene_material_registrations_retained(owner, ro->raw[i].remap.material.registration);
    o->nraw++;
  }
  /* Adopt replacement registries' shader registry for future operations. */
  scene->shaders = replacement->shaders;
}
/* world service */
static qa_scene_raw_remap_binding *svc_raw_bindings(void *ctx, const char *name, size_t *out_count) {
  return qa_world_scene_raw_remap_bindings((qa_world_scene *)ctx, name, out_count);
}
static void svc_publish(void *ctx, const char *name, const qa_registered_scene_material *materials, size_t count, void *source, double time_offset) {
  qa_world_scene_publish_raw_remap((qa_world_scene *)ctx, name, materials, count, (qa_scene_shader_registry *)source, time_offset);
}
qa_scene_world_service qa_world_scene_world_service(qa_world_scene *scene) {
  return (qa_scene_world_service){ scene, svc_raw_bindings, svc_publish };
}

