/* Port of src/render/scene/textures.ts. GPL-2.0-or-later.
 * async reads are synchronous fn+ctx (PORTING.md). SceneTexture boxes are
 * loader-owned; free on loader destroy. */
#ifndef QA_RENDER_SCENE_TEXTURES_H
#define QA_RENDER_SCENE_TEXTURES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/render/scene/image_policy.h"
#include "qa/render/scene/resources.h"
typedef struct qa_scene_asset {
  const uint8_t *bytes; size_t length;
  qa_renderer_image source_sample; /* source fields copied from this image */
} qa_scene_asset;
typedef struct qa_scene_asset_reader {
  void *ctx;
  /* read bytes are caller-freed (malloc'd) when true. */
  bool (*read)(void *ctx, const char *path, qa_scene_asset *out, uint8_t **owned_bytes);
  bool (*read_original)(void *ctx, const char *path, qa_scene_asset *out, uint8_t **owned_bytes); /* nullable */
} qa_scene_asset_reader;
typedef struct qa_scene_texture {
  const char *name; double width, height;
  qa_renderer_image image; qa_render_image content;
  const qa_renderer_image *fullbright; qa_renderer_image fullbright_value;
} qa_scene_texture;
typedef struct qa_scene_texture_load_options {
  bool has_mipmap; bool mipmap;
  const char *wrap;       /* NULL is TS undefined ("repeat" default in load) */
  const char *family;     /* NULL | "q1" | "q2" | "q3" */
  const char *usage;      /* NULL | ImageUsage */
} qa_scene_texture_load_options;
typedef struct qa_scene_texture_loader qa_scene_texture_loader;
struct qa_scene_texture_loader {
  qa_scene_image_registry *images;
  qa_scene_asset_reader reader;
  const qa_palette *palette;
  qa_palette palette_value;
  double fullbright_first;
  const qa_scene_texture *white, *missing;
  void *_policy, *_loaded, *_requests, *_owned, *_animations;
  bool strict, closed, _init_failed;
};
typedef struct qa_scene_texture_loader_options {
  bool has_policy; qa_scene_image_policy policy;
  bool has_fullbright_first; double fullbright_first;
  bool strict;
} qa_scene_texture_loader_options;
/* Returns false only on OOM (palette NULL is TS null). */
bool qa_scene_texture_loader_init(qa_scene_texture_loader *l, qa_scene_image_registry *images, qa_scene_asset_reader reader, const qa_palette *palette, const qa_scene_texture_loader_options *options);
void qa_scene_texture_loader_destroy(qa_scene_texture_loader *l);
const qa_scene_texture *qa_scene_texture_loader_register(qa_scene_texture_loader *l, const char *name, qa_render_image content, const qa_texture_sampling *sampling, const qa_renderer_image *source, bool has_logical, double logical_width, double logical_height);
/* NULL is TS null. */
const qa_scene_texture *qa_scene_texture_loader_q1_embedded(qa_scene_texture_loader *l, qa_q1_mip_texture texture);
typedef struct qa_scene_surface_options { bool mipmap; const char *wrap; } qa_scene_surface_options;
const qa_scene_texture *qa_scene_texture_loader_sample_surface(qa_scene_texture_loader *l, const qa_scene_texture *texture, qa_scene_surface_options options);
const qa_scene_texture *qa_scene_texture_loader_load(qa_scene_texture_loader *l, const char *name, const qa_scene_texture_load_options *options);
void qa_scene_texture_loader_prepare_replacement(qa_scene_texture_loader *l, qa_scene_texture_loader *replacement);
void qa_scene_texture_loader_dispose_images(qa_scene_texture_loader *l);
void qa_scene_texture_loader_close(qa_scene_texture_loader *l);
#endif
