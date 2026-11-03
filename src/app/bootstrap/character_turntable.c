/* Port of src/app/bootstrap/character-turntable.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/character_turntable.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/foundation/assets.h"
#include "qa/formats/images/bmp.h"
#include "qa/formats/images/gif.h"
#include "qa/formats/images/indexed.h"
#include "qa/formats/images/jpeg.h"
#include "qa/formats/images/mip.h"
#include "qa/formats/images/palette.h"
#include "qa/formats/images/png.h"
#include "qa/formats/images/q3_tga.h"
#include "qa/formats/images/tga.h"
#include "qa/formats/q12_model/animation.h"
#include "qa/formats/q12_model/md2.h"
#include "qa/formats/q12_model/mdl.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/materials/material.h"
#include "qa/movement/q3/constants.h"
#include "qa/network/common/js_number.h"
#include "qa/render/scene/models/transform.h"
#include "qa/render/scene/skin.h"

#define PI 3.141592653589793

static void *xmalloc(size_t size) {
  void *out = malloc(size == 0 ? 1 : size);
  if (out == NULL) qa_range_error("Out of memory");
  return out;
}
static void *xcalloc(size_t count, size_t size) {
  void *out = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (out == NULL) qa_range_error("Out of memory");
  return out;
}
static void *xrealloc(void *pointer, size_t size) {
  void *out = realloc(pointer, size == 0 ? 1 : size);
  if (out == NULL) qa_range_error("Out of memory");
  return out;
}
static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *out = xmalloc(length);
  memcpy(out, text, length);
  return out;
}
static char *xformat(const char *format, ...) __attribute__((format(printf, 1, 2)));
static char *xformat(const char *format, ...) {
  va_list args, copy;
  char small[256];
  va_start(args, format);
  va_copy(copy, args);
  int length = vsnprintf(small, sizeof small, format, copy);
  va_end(copy);
  if (length < 0) qa_range_error("Out of memory");
  char *out = xmalloc((size_t)length + 1);
  if ((size_t)length < sizeof small) memcpy(out, small, (size_t)length + 1);
  else vsnprintf(out, (size_t)length + 1, format, args);
  va_end(args);
  return out;
}
static bool starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}
static void lower_in_place(char *text) {
  for (; *text != '\0'; text++) *text = (char)tolower((unsigned char)*text);
}

/* TS at(values, index, label): RangeError for a missing row. */
static noreturn void missing(const char *label, double index) {
  qa_range_error("Missing %s %s", label, QA_JS_NUMBER(index));
}

/* ---- scene construction ---------------------------------------------------- */

typedef struct scene_builder {
  qa_app_turntable_vertex *vertices; size_t vertex_count, vertex_capacity;
  double *indices; size_t index_count, index_capacity;
  qa_app_turntable_texture *textures; size_t texture_count, texture_capacity;
} scene_builder;
static void builder_vertex(scene_builder *builder, qa_app_turntable_vertex vertex) {
  if (builder->vertex_count == builder->vertex_capacity) {
    builder->vertex_capacity = builder->vertex_capacity == 0 ? 256 : builder->vertex_capacity * 2;
    builder->vertices = xrealloc(builder->vertices, builder->vertex_capacity * sizeof *builder->vertices);
  }
  builder->vertices[builder->vertex_count++] = vertex;
}
static void builder_index(scene_builder *builder, double index) {
  if (builder->index_count == builder->index_capacity) {
    builder->index_capacity = builder->index_capacity == 0 ? 256 : builder->index_capacity * 2;
    builder->indices = xrealloc(builder->indices, builder->index_capacity * sizeof *builder->indices);
  }
  builder->indices[builder->index_count++] = index;
}
static size_t builder_texture(scene_builder *builder, qa_app_turntable_texture texture) {
  if (builder->texture_count == builder->texture_capacity) {
    builder->texture_capacity = builder->texture_capacity == 0 ? 8 : builder->texture_capacity * 2;
    builder->textures = xrealloc(builder->textures, builder->texture_capacity * sizeof *builder->textures);
  }
  builder->textures[builder->texture_count] = texture;
  return builder->texture_count++;
}
static void builder_free(void *arg) {
  scene_builder *builder = arg;
  for (size_t i = 0; i < builder->texture_count; i++) free(builder->textures[i].pixels);
  free(builder->vertices); free(builder->indices); free(builder->textures);
  memset(builder, 0, sizeof *builder);
}

static qa_app_turntable_texture missing_texture(void) {
  double width = 16, height = 16;
  uint8_t *pixels = xmalloc(16 * 16 * 4);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      uint8_t *pixel = pixels + ((size_t)y * 16 + (size_t)x) * 4;
      bool even = ((x + y) & 1) == 0;
      pixel[0] = even ? 72 : 38; pixel[1] = even ? 72 : 38; pixel[2] = even ? 78 : 44; pixel[3] = 255;
    }
  return (qa_app_turntable_texture){width, height, pixels, 16 * 16 * 4, "none"};
}

/* TS finishScene: takes the builder's arrays. */
static qa_app_character_turntable_scene *finish_scene(scene_builder *builder, double initial_yaw) {
  if (builder->vertex_count == 0 || builder->index_count < 3)
    qa_raise("Error", 0, 0, "Character model has no preview geometry");
  double min_x = INFINITY, min_y = INFINITY, min_z = INFINITY, max_x = -INFINITY, max_y = -INFINITY, max_z = -INFINITY;
  for (size_t i = 0; i < builder->vertex_count; i++) {
    qa_vec3 p = builder->vertices[i].position;
    min_x = qa_js_min(min_x, p.x); min_y = qa_js_min(min_y, p.y); min_z = qa_js_min(min_z, p.z);
    max_x = qa_js_max(max_x, p.x); max_y = qa_js_max(max_y, p.y); max_z = qa_js_max(max_z, p.z);
  }
  qa_vec3 center = {(min_x + max_x) / 2, (min_y + max_y) / 2, (min_z + max_z) / 2};
  double radius = 1;
  for (size_t i = 0; i < builder->vertex_count; i++) {
    double dx = builder->vertices[i].position.x - center.x, dy = builder->vertices[i].position.y - center.y;
    radius = qa_js_max(radius, qa_js_hypot2(dx, dy));
  }
  qa_app_character_turntable_scene *scene = xcalloc(1, sizeof *scene);
  scene->vertices = builder->vertices; scene->vertex_count = builder->vertex_count;
  scene->indices = builder->indices; scene->index_count = builder->index_count;
  scene->textures = builder->textures; scene->texture_count = builder->texture_count;
  scene->center = center;
  scene->radius = radius;
  scene->half_height = qa_js_max(1, (max_z - min_z) / 2);
  scene->floor_z = min_z;
  scene->initial_yaw = initial_yaw;
  scene->references = 1;
  memset(builder, 0, sizeof *builder);
  return scene;
}

void qa_app_character_turntable_scene_retain(qa_app_character_turntable_scene *scene) {
  if (scene != NULL) scene->references++;
}
void qa_app_character_turntable_scene_release(qa_app_character_turntable_scene *scene) {
  if (scene == NULL || --scene->references != 0) return;
  for (size_t i = 0; i < scene->texture_count; i++) free(scene->textures[i].pixels);
  free(scene->textures); free(scene->vertices); free(scene->indices); free(scene);
}

/* ---- resource access ----------------------------------------------------- */

typedef struct opened_resource {
  const qa_resolved_resource_reference *reference;
  uint8_t *bytes; size_t length;
} opened_resource;
static void opened_free(void *arg) {
  opened_resource *opened = arg;
  free(opened->bytes); opened->bytes = NULL;
}

/* TS firstOpen: the first non-empty resource, or false (TS null). */
static bool first_open(qa_content_mounted *mounts, const char *const *paths, size_t count, opened_resource *out) {
  for (size_t i = 0; i < count; i++) {
    qa_content_opened_resource opened;
    memset(&opened, 0, sizeof opened);
    if (!qa_content_mounted_open(mounts, paths[i], NULL, NULL, &opened)) continue;
    if (opened.byte_length != 0) {
      out->reference = opened.reference; out->bytes = opened.bytes; out->length = opened.byte_length;
      return true;
    }
    free(opened.bytes);
  }
  return false;
}
/* TS requiredOpen. */
static void required_open(qa_content_mounted *mounts, const char *const *paths, size_t count, opened_resource *out) {
  if (first_open(mounts, paths, count, out)) return;
  size_t length = 1;
  for (size_t i = 0; i < count; i++) length += strlen(paths[i]) + 2;
  char *joined = xmalloc(length);
  joined[0] = '\0';
  for (size_t i = 0; i < count; i++) {
    if (i != 0) strcat(joined, ", ");
    strcat(joined, paths[i]);
  }
  char message[QA_CAUGHT_MESSAGE_CAPACITY];
  snprintf(message, sizeof message, "Character preview resource missing: %s", joined);
  free(joined);
  qa_raise("Error", 0, 0, "%s", message);
}

/* TS loadPalette. colors are malloc'd (free with free(palette->colors.bytes)). */
typedef struct owned_palette { qa_palette palette; uint8_t *colors; } owned_palette;
static void owned_palette_free(void *arg) {
  owned_palette *palette = arg;
  free(palette->colors); palette->colors = NULL;
}
static void load_palette(qa_content_mounted *mounts, qa_game_family family, owned_palette *out) {
  const char *path = family == QA_GAME_FAMILY_Q1 ? "gfx/palette.lmp" : "pics/colormap.pcx";
  opened_resource opened = {0};
  required_open(mounts, &path, 1, &opened);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, opened_free, &opened);
  if (family == QA_GAME_FAMILY_Q1) {
    qa_palette decoded;
    memset(&decoded, 0, sizeof decoded);
    if (!qa_decode_palette(opened.bytes, opened.length, *opened.reference, &decoded)) qa_range_error("Out of memory");
    out->colors = xmalloc(decoded.colors.length);
    memcpy(out->colors, decoded.colors.bytes, decoded.colors.length);
    out->palette.colors.bytes = out->colors; out->palette.colors.length = decoded.colors.length;
    out->palette.source = decoded.source;
    qa_palette_destroy(&decoded);
  } else {
    qa_pcx_image pcx;
    memset(&pcx, 0, sizeof pcx);
    if (!qa_decode_pcx(opened.bytes, opened.length, path, &pcx)) qa_range_error("Out of memory");
    if (pcx.palette == NULL) { qa_pcx_image_destroy(&pcx); qa_raise("Error", 0, 0, "Quake II colormap has no palette"); }
    out->colors = xmalloc(pcx.palette_length);
    memcpy(out->colors, pcx.palette, pcx.palette_length);
    out->palette.colors.bytes = out->colors; out->palette.colors.length = pcx.palette_length;
    out->palette.source = *opened.reference;
    qa_pcx_image_destroy(&pcx);
  }
  qa_cleanup_pop(&cleanup);
  opened_free(&opened);
}

/* ---- textures ---------------------------------------------------------- */

typedef struct decode_job {
  const uint8_t *bytes; size_t length; const char *path; qa_game_family family; const char *alpha_test;
  qa_app_turntable_texture result;
} decode_job;
static uint8_t *copy_pixels(qa_byte_view pixels) {
  uint8_t *out = xmalloc(pixels.length);
  if (pixels.length != 0) memcpy(out, pixels.bytes, pixels.length);
  return out;
}
/* TS decodeImage. */
static void decode_image(void *arg) {
  decode_job *job = arg;
  const char *dot = strrchr(job->path, '.');
  char *suffix = xstrdup(dot == NULL ? job->path + strlen(job->path) : dot);
  /* TS path.slice(path.lastIndexOf(".")): -1 slices the last character. */
  if (dot == NULL && job->path[0] != '\0') { free(suffix); suffix = xstrdup(job->path + strlen(job->path) - 1); }
  lower_in_place(suffix);
  qa_cleanup suffix_cleanup;
  qa_cleanup_push(&suffix_cleanup, free, suffix);
  if (strcmp(suffix, ".pcx") == 0) {
    qa_pcx_image pcx;
    memset(&pcx, 0, sizeof pcx);
    if (!qa_decode_pcx(job->bytes, job->length, job->path, &pcx)) qa_range_error("Out of memory");
    if (pcx.palette == NULL) { qa_pcx_image_destroy(&pcx); qa_raise("Error", 0, 0, "PCX has no palette: %s", job->path); }
    size_t count = (size_t)(pcx.width * pcx.height);
    uint8_t *pixels = xcalloc(count * 4, 1);
    for (size_t offset = 0; offset < pcx.indices_length; offset++) {
      size_t index = pcx.indices[offset];
      /* TS pixels.set at offset*4: a write past the end would throw; indices
       * are width*height long. */
      if (offset >= count) { free(pixels); qa_pcx_image_destroy(&pcx); qa_range_error("offset is out of bounds"); }
      uint8_t *pixel = pixels + offset * 4;
      pixel[0] = index * 3 < pcx.palette_length ? pcx.palette[index * 3] : 0;
      pixel[1] = index * 3 + 1 < pcx.palette_length ? pcx.palette[index * 3 + 1] : 0;
      pixel[2] = index * 3 + 2 < pcx.palette_length ? pcx.palette[index * 3 + 2] : 0;
      pixel[3] = job->family != QA_GAME_FAMILY_Q3 && index == 255 ? 0 : 255;
    }
    job->result = (qa_app_turntable_texture){(double)pcx.width, (double)pcx.height, pixels, count * 4, job->alpha_test};
    qa_pcx_image_destroy(&pcx);
    qa_cleanup_pop(&suffix_cleanup); free(suffix);
    return;
  }
  double width = 0, height = 0;
  uint8_t *pixels = NULL; size_t pixel_length = 0;
  bool decoded = true;
  if (strcmp(suffix, ".tga") == 0) {
    if (job->family == QA_GAME_FAMILY_Q3) {
      qa_image_data image;
      memset(&image, 0, sizeof image);
      if (!qa_decode_q3_tga(job->bytes, job->length, job->path, NULL, NULL, &image)) qa_range_error("Out of memory");
      width = (double)image.width; height = (double)image.height; pixels = image.pixels; pixel_length = image.pixels_length;
    } else {
      qa_tga_image image;
      memset(&image, 0, sizeof image);
      if (!qa_decode_tga(job->bytes, job->length, job->path, &image)) qa_range_error("Out of memory");
      width = image.level.width; height = image.level.height; pixels = copy_pixels(image.level.pixels);
      pixel_length = image.level.pixels.length;
      qa_tga_image_destroy(&image);
    }
  } else if (strcmp(suffix, ".jpg") == 0 || strcmp(suffix, ".jpeg") == 0) {
    qa_jpeg_image image;
    memset(&image, 0, sizeof image);
    if (!qa_decode_jpeg(job->bytes, job->length, job->path, NULL, NULL, &image)) qa_range_error("Out of memory");
    width = image.width; height = image.height; pixels = copy_pixels(image.pixels); pixel_length = image.pixels.length;
    qa_image_level_destroy(&image);
  } else if (strcmp(suffix, ".png") == 0) {
    qa_png_image image;
    memset(&image, 0, sizeof image);
    if (!qa_decode_png(job->bytes, job->length, job->path, &image)) qa_range_error("Out of memory");
    width = image.level.width; height = image.level.height; pixels = copy_pixels(image.level.pixels);
    pixel_length = image.level.pixels.length;
    qa_png_image_destroy(&image);
  } else if (strcmp(suffix, ".bmp") == 0) {
    qa_bmp_image image;
    memset(&image, 0, sizeof image);
    if (!qa_decode_bmp(job->bytes, job->length, job->path, &image)) qa_range_error("Out of memory");
    width = image.level.width; height = image.level.height; pixels = copy_pixels(image.level.pixels);
    pixel_length = image.level.pixels.length;
    qa_bmp_image_destroy(&image);
  } else if (strcmp(suffix, ".gif") == 0) {
    qa_gif_image image;
    memset(&image, 0, sizeof image);
    if (!qa_decode_gif(job->bytes, job->length, job->path, &image)) qa_range_error("Out of memory");
    if (image.frame_count == 0) {
      qa_gif_image_destroy(&image);
      qa_raise("TypeError", 0, 0, "undefined is not an object (evaluating 'decodeGif(bytes, path).frames[0].image')");
    }
    width = image.frames[0].image.width; height = image.frames[0].image.height;
    pixels = copy_pixels(image.frames[0].image.pixels); pixel_length = image.frames[0].image.pixels.length;
    qa_gif_image_destroy(&image);
  } else decoded = false;
  if (!decoded) qa_raise("Error", 0, 0, "Unsupported character texture format: %s", job->path);
  qa_cleanup_pop(&suffix_cleanup); free(suffix);
  if (job->family == QA_GAME_FAMILY_Q3 && strcmp(job->alpha_test, "none") == 0) {
    /* Q3 implicit diffuse shaders do not alpha-test; diffuse alpha is ancillary. */
    for (size_t offset = 3; offset < pixel_length; offset += 4) pixels[offset] = 255;
  }
  job->result = (qa_app_turntable_texture){width, height, pixels, pixel_length, job->alpha_test};
}

/* shader.replaceAll("\\", "/").replace(/^\/+/, "") */
static char *normalize_path(const char *shader) {
  char *out = xstrdup(shader);
  for (char *cursor = out; *cursor != '\0'; cursor++) if (*cursor == '\\') *cursor = '/';
  size_t skip = 0;
  while (out[skip] == '/') skip++;
  memmove(out, out + skip, strlen(out + skip) + 1);
  return out;
}

static const char *const image_extensions[] = {".tga", ".jpg", ".png", ".jpeg", ".pcx", ".bmp", ".gif"};
enum { IMAGE_EXTENSION_COUNT = 7 };

typedef struct string_list { char **values; size_t count, capacity; } string_list;
static void list_push(string_list *list, char *value) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
    list->values = xrealloc(list->values, list->capacity * sizeof *list->values);
  }
  list->values[list->count++] = value;
}
static void list_free(void *arg) {
  string_list *list = arg;
  for (size_t i = 0; i < list->count; i++) free(list->values[i]);
  free(list->values);
  memset(list, 0, sizeof *list);
}

/* TS loadDirectTexture. */
static qa_app_turntable_texture load_direct_texture(qa_content_mounted *mounts, const char *shader,
    qa_game_family family, const char *alpha_test) {
  char *normalized = normalize_path(shader);
  if (normalized[0] == '\0') { free(normalized); return missing_texture(); }
  string_list variants = {0}, candidates = {0};
  qa_cleanup variant_cleanup, candidate_cleanup;
  qa_cleanup_push(&variant_cleanup, list_free, &variants);
  qa_cleanup_push(&candidate_cleanup, list_free, &candidates);
  list_push(&variants, normalized);
  /* Retail Tim team skins point at an older medium/ directory whose textures
   * shipped beside the final model instead. */
  const char *medium = "models/players/medium/";
  if (starts_with(normalized, medium))
    list_push(&variants, xformat("models/players/%s", normalized + strlen(medium)));
  for (size_t v = 0; v < variants.count; v++) {
    const char *path = variants.values[v];
    const char *dot = strrchr(path, '.'), *slash = strrchr(path, '/');
    long dot_index = dot == NULL ? -1 : (long)(dot - path), slash_index = slash == NULL ? -1 : (long)(slash - path);
    if (!(dot_index > slash_index)) {
      for (size_t e = 0; e < IMAGE_EXTENSION_COUNT; e++) list_push(&candidates, xformat("%s%s", path, image_extensions[e]));
      continue;
    }
    /* Q3 image lookup substitutes supported extensions for shader map names. */
    list_push(&candidates, xstrdup(path));
    for (size_t e = 0; e < IMAGE_EXTENSION_COUNT; e++) {
      char *candidate = xformat("%.*s%s", (int)dot_index, path, image_extensions[e]);
      if (strcmp(candidate, path) == 0) free(candidate); else list_push(&candidates, candidate);
    }
  }
  opened_resource opened = {0};
  bool found = first_open(mounts, (const char *const *)candidates.values, candidates.count, &opened);
  qa_cleanup_pop(&candidate_cleanup); list_free(&candidates);
  qa_cleanup_pop(&variant_cleanup); list_free(&variants);
  if (!found) return missing_texture();
  decode_job job = {.bytes = opened.bytes, .length = opened.length, .path = opened.reference->requested_path, .family = family, .alpha_test = alpha_test};
  qa_caught_error caught;
  bool ok = qa_try(decode_image, &job, &caught);
  free(opened.bytes);
  if (!ok) return missing_texture();
  return job.result;
}

/* TS shaderKey. */
static char *shader_key(const char *name) {
  char *normalized = normalize_path(name);
  char *stripped = qa_strip_shader_extension(normalized);
  free(normalized);
  if (stripped == NULL) qa_range_error("Out of memory");
  lower_in_place(stripped);
  return stripped;
}

/* TS q3ShaderCatalogs: WeakMap<MountedContent, Map<key, ShaderDefinition>>. */
typedef struct shader_entry { char *key; const qa_shader_definition *definition; } shader_entry;
typedef struct shader_catalog {
  const qa_content_mounted *mounts;
  shader_entry *entries; size_t count, capacity;
  qa_shader_script_inspection *inspections; size_t inspection_count;
  struct shader_catalog *next;
} shader_catalog;
static shader_catalog *shader_catalogs;

static void shader_catalog_free(shader_catalog *catalog) {
  for (size_t i = 0; i < catalog->count; i++) free(catalog->entries[i].key);
  free(catalog->entries);
  for (size_t i = 0; i < catalog->inspection_count; i++) qa_shader_script_inspection_free(&catalog->inspections[i]);
  free(catalog->inspections);
  free(catalog);
}
void qa_app_turntable_forget_mounts(const qa_content_mounted *mounts) {
  for (shader_catalog **link = &shader_catalogs; *link != NULL; link = &(*link)->next)
    if ((*link)->mounts == mounts) {
      shader_catalog *found = *link;
      *link = found->next;
      shader_catalog_free(found);
      return;
    }
}
static const qa_shader_definition *catalog_get(const shader_catalog *catalog, const char *key) {
  for (size_t i = 0; i < catalog->count; i++) if (strcmp(catalog->entries[i].key, key) == 0) return catalog->entries[i].definition;
  return NULL;
}
typedef struct inspect_job { const char *text; const char *source; qa_shader_script_inspection result; } inspect_job;
static void inspect_script(void *arg) {
  inspect_job *job = arg;
  qa_inspect_shader_script(job->text, job->source, NULL, &job->result);
}
static void free_name_list(void *arg) {
  string_list *list = arg;
  list_free(list);
}
static const shader_catalog *q3_shaders(qa_content_mounted *mounts) {
  for (shader_catalog *catalog = shader_catalogs; catalog != NULL; catalog = catalog->next)
    if (catalog->mounts == mounts) return catalog;
  shader_catalog *catalog = xcalloc(1, sizeof *catalog);
  catalog->mounts = mounts;
  size_t file_count = 0;
  /* TS listFiles(...).catch(() => []): a failed listing is empty. */
  char **files = qa_content_mounted_list_files(mounts, "scripts", ".shader", &file_count);
  string_list names = {files, files == NULL ? 0 : file_count, files == NULL ? 0 : file_count};
  qa_cleanup names_cleanup;
  qa_cleanup_push(&names_cleanup, free_name_list, &names);
  for (size_t f = 0; f < names.count; f++) {
    char *relative = xstrdup(names.values[f]);
    for (char *cursor = relative; *cursor != '\0'; cursor++) if (*cursor == '\\') *cursor = '/';
    char *path = xformat("scripts/%s", relative);
    free(relative);
    qa_content_opened_resource opened;
    memset(&opened, 0, sizeof opened);
    bool found = qa_content_mounted_open(mounts, path, NULL, NULL, &opened);
    free(path);
    if (!found) continue;
    /* TS byteText maps each byte to one UTF-16 unit; a NUL byte ends the C text. */
    char *text = xmalloc(opened.byte_length + 1);
    if (opened.byte_length != 0) memcpy(text, opened.bytes, opened.byte_length);
    text[opened.byte_length] = '\0';
    free(opened.bytes);
    inspect_job job = {.text = text, .source = opened.reference->requested_path};
    qa_caught_error caught;
    bool ok = qa_try(inspect_script, &job, &caught);
    free(text);
    if (!ok) continue;
    catalog->inspections = xrealloc(catalog->inspections, (catalog->inspection_count + 1) * sizeof *catalog->inspections);
    catalog->inspections[catalog->inspection_count++] = job.result;
    qa_shader_script_inspection *inspection = &catalog->inspections[catalog->inspection_count - 1];
    for (size_t e = 0; e < inspection->count; e++) {
      const qa_shader_definition *definition = &inspection->entries[e].definition;
      char *key = shader_key(definition->name);
      if (catalog_get(catalog, key) != NULL) { free(key); continue; }
      if (catalog->count == catalog->capacity) {
        catalog->capacity = catalog->capacity == 0 ? 64 : catalog->capacity * 2;
        catalog->entries = xrealloc(catalog->entries, catalog->capacity * sizeof *catalog->entries);
      }
      catalog->entries[catalog->count++] = (shader_entry){key, definition};
    }
  }
  qa_cleanup_pop(&names_cleanup);
  list_free(&names);
  catalog->next = shader_catalogs;
  shader_catalogs = catalog;
  return catalog;
}

/* TS stageImageName. */
static const char *stage_image_name(const qa_mat_parsed_stage *stage) {
  if (strcmp(stage->stage.map.kind, "image") == 0) return stage->stage.map.name;
  if (strcmp(stage->stage.map.kind, "animation") == 0)
    return stage->stage.map.frames.count == 0 ? NULL : stage->stage.map.frames.values[0];
  return NULL;
}
static bool additive(const qa_mat_parsed_stage *stage) {
  return strcmp(stage->stage.blend.destination, "one") == 0
      && (strcmp(stage->stage.blend.source, "one") == 0 || strcmp(stage->stage.blend.source, "src-alpha") == 0);
}

/* TS loadTexture; false is TS null (nodraw). */
static bool load_texture(qa_content_mounted *mounts, const char *shader, qa_game_family family,
    qa_app_turntable_texture *out) {
  if (family != QA_GAME_FAMILY_Q3) { *out = load_direct_texture(mounts, shader, family, "none"); return true; }
  char *normalized = normalize_path(shader);
  if (normalized[0] == '\0') { free(normalized); *out = missing_texture(); return true; }
  qa_cleanup normalized_cleanup;
  qa_cleanup_push(&normalized_cleanup, free, normalized);
  const shader_catalog *catalog = q3_shaders(mounts);
  char *key = shader_key(normalized);
  const qa_shader_definition *definition = catalog_get(catalog, key);
  free(key);
  if (definition == NULL) {
    *out = load_direct_texture(mounts, normalized, family, "none");
    qa_cleanup_pop(&normalized_cleanup); free(normalized);
    return true;
  }
  qa_cleanup_pop(&normalized_cleanup); free(normalized);
  for (size_t i = 0; i < definition->surface_parms.count; i++)
    if (strcmp(definition->surface_parms.values[i], "nodraw") == 0) return false;
  const qa_mat_parsed_stage *first_image = NULL, *last_base = NULL;
  const char *first_name = NULL, *last_base_name = NULL;
  for (size_t i = 0; i < definition->stage_count; i++) {
    const qa_mat_parsed_stage *stage = &definition->stages[i];
    const char *name = stage_image_name(stage);
    if (name == NULL) continue;
    if (first_image == NULL) { first_image = stage; first_name = name; }
    if (strcmp(stage->stage.tc_gen.kind, "texture") == 0 && !additive(stage)) { last_base = stage; last_base_name = name; }
  }
  const qa_mat_parsed_stage *chosen = last_base != NULL ? last_base : first_image;
  const char *chosen_name = last_base != NULL ? last_base_name : first_name;
  if (chosen != NULL) {
    *out = load_direct_texture(mounts, chosen_name, family, chosen->stage.alpha_func);
    return true;
  }
  for (size_t i = 0; i < definition->stage_count; i++)
    if (strcmp(definition->stages[i].stage.map.kind, "whiteimage") == 0) {
      uint8_t *pixels = xmalloc(4);
      pixels[0] = pixels[1] = pixels[2] = pixels[3] = 255;
      *out = (qa_app_turntable_texture){1, 1, pixels, 4, "none"};
      return true;
    }
  *out = missing_texture();
  return true;
}

/* ---- Q1 ------------------------------------------------------------- */

static bool name_starts_with_stand(const char *name) {
  if (name == NULL) return false;
  char *lower = xstrdup(name);
  lower_in_place(lower);
  bool result = starts_with(lower, "stand");
  free(lower);
  return result;
}
/* TS q1Frame. */
static const qa_model_frame *q1_frame(const qa_q1_alias_model *model) {
  const qa_model_frame *first = NULL;
  for (size_t s = 0; s < model->frame_count; s++) {
    const qa_q1_alias_frame_set *set = &model->frames[s];
    if (!set->is_group) {
      const qa_model_frame *frame = &set->single_frame->frame;
      if (first == NULL) first = frame;
      if (name_starts_with_stand(frame->name)) return frame;
      continue;
    }
    for (size_t i = 0; i < set->frame_count; i++) {
      const qa_model_frame *frame = &((const qa_q1_alias_frame *)set->frames[i].frame)->frame;
      if (first == NULL) first = frame;
      if (name_starts_with_stand(frame->name)) return frame;
    }
  }
  if (first == NULL) missing("Q1 frame", 0);
  return first;
}

typedef struct model_state {
  opened_resource model_asset;
  owned_palette palette;
  qa_q1_alias_model q1; bool has_q1;
  qa_q2_alias_model q2; bool has_q2;
  opened_resource skin_asset;
  qa_alias_geometry geometry; bool has_geometry;
  qa_pcx_image pcx; bool has_pcx;
  uint8_t *flooded; uint8_t *translation;
  qa_image_level expanded; bool has_expanded;
  scene_builder builder;
} model_state;
static void model_state_free(void *arg) {
  model_state *state = arg;
  opened_free(&state->model_asset);
  owned_palette_free(&state->palette);
  if (state->has_q1) qa_q1_alias_model_destroy(&state->q1);
  if (state->has_q2) qa_q2_alias_model_destroy(&state->q2);
  opened_free(&state->skin_asset);
  if (state->has_geometry) qa_alias_geometry_destroy(&state->geometry);
  if (state->has_pcx) qa_pcx_image_destroy(&state->pcx);
  free(state->flooded); free(state->translation);
  if (state->has_expanded) qa_image_level_destroy(&state->expanded);
  builder_free(&state->builder);
  memset(state, 0, sizeof *state);
}

static void geometry_to_builder(model_state *state, qa_image_level texture) {
  for (size_t i = 0; i < state->geometry.vertex_count; i++) {
    const qa_alias_mesh_vertex *vertex = &state->geometry.vertices[i];
    builder_vertex(&state->builder, (qa_app_turntable_vertex){vertex->vertex.position, vertex->vertex.normal, vertex->tex_coord, 0});
  }
  for (size_t i = 0; i < state->geometry.index_count; i++) builder_index(&state->builder, state->geometry.indices[i]);
  builder_texture(&state->builder, (qa_app_turntable_texture){texture.width, texture.height,
      (uint8_t *)texture.pixels.bytes, texture.pixels.length, "gt0"});
}

static qa_app_character_turntable_scene *load_q1_scene(qa_content_mounted *mounts, const qa_app_turntable_request *request) {
  model_state *state = xcalloc(1, sizeof *state);
  qa_cleanup cleanup, state_cleanup;
  qa_cleanup_push(&state_cleanup, free, state);
  qa_cleanup_push(&cleanup, model_state_free, state);
  const char *model_path = "progs/player.mdl";
  required_open(mounts, &model_path, 1, &state->model_asset);
  load_palette(mounts, QA_GAME_FAMILY_Q1, &state->palette);
  qa_parse_mdl(state->model_asset.bytes, state->model_asset.length, state->model_asset.reference->requested_path, &state->q1);
  state->has_q1 = true;
  const qa_model_frame *frame = q1_frame(&state->q1);
  qa_build_mdl_geometry(&state->q1, frame->vertices, frame->vertex_count, &state->geometry);
  state->has_geometry = true;
  if (state->q1.skin_count == 0) missing("Q1 skin", 0);
  const qa_byte_view *skin = qa_sample_timed_frame(&state->q1.skins[0], 0, 0);
  size_t flooded_length = 0;
  state->flooded = qa_flood_skin(skin->bytes, skin->length, state->q1.skin_width, state->q1.skin_height,
      state->palette.palette, &flooded_length);
  /* q1PlayerTranslation's Number.isInteger guard, ahead of the int64 cast. */
  if (!(request->top_color == trunc(request->top_color) && fabs(request->top_color) <= 13)
      || !(request->bottom_color == trunc(request->bottom_color) && fabs(request->bottom_color) <= 13))
    qa_range_error("Q1 player colors must be in 0..13");
  state->translation = qa_q1_player_translation((int64_t)request->top_color, (int64_t)request->bottom_color);
  if (state->translation == NULL) qa_range_error("Out of memory");
  qa_image_level level = {state->q1.skin_width, state->q1.skin_height, {state->flooded, flooded_length}};
  qa_byte_view translation = {state->translation, 256};
  qa_palette_transparency opaque = {"opaque", 0};
  qa_indexed_render_image image = qa_indexed_render_image_make(&level, 1, state->palette.palette, opaque, NULL, &translation);
  if (!qa_expand_indexed_image(&image, 0, QA_PALETTE_LAYER_COMBINED, &state->expanded)) qa_range_error("Out of memory");
  geometry_to_builder(state, state->expanded);
  state->expanded.pixels.bytes = NULL; /* now owned by the builder */
  qa_app_character_turntable_scene *scene = finish_scene(&state->builder, -0.55);
  qa_cleanup_pop(&cleanup);
  model_state_free(state);
  qa_cleanup_pop(&state_cleanup);
  free(state);
  return scene;
}

/* ---- Q2 ------------------------------------------------------------- */

static int compare_strings(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static bool lower_equals(const char *text, const char *expected) {
  char *lower = xstrdup(text);
  lower_in_place(lower);
  bool result = strcmp(lower, expected) == 0;
  free(lower);
  return result;
}
static bool lower_ends_with(const char *text, const char *suffix) {
  char *lower = xstrdup(text);
  lower_in_place(lower);
  size_t length = strlen(lower), suffix_length = strlen(suffix);
  bool result = length >= suffix_length && strcmp(lower + length - suffix_length, suffix) == 0;
  free(lower);
  return result;
}
/* TS text.trim() !== "" for ASCII and JS white space. */
static bool blank(const char *text) {
  while (*text != '\0') {
    size_t space = qa_js_white_space_length(text);
    if (space == 0) return false;
    text += space;
  }
  return true;
}

/* TS q2Skin. */
static void q2_skin(qa_content_mounted *mounts, const char *model, const char *skin, qa_string_view_list model_skins,
    opened_resource *out) {
  const char *known = lower_equals(model, "female") ? "athena" : lower_equals(model, "cyborg") ? "oni911" : "grunt";
  string_list candidates = {0};
  qa_cleanup candidate_cleanup;
  qa_cleanup_push(&candidate_cleanup, list_free, &candidates);
  if (skin[0] != '\0') list_push(&candidates, xformat("players/%s/%s.pcx", model, skin));
  else {
    for (size_t i = 0; i < model_skins.count; i++) if (!blank(model_skins.values[i])) list_push(&candidates, xstrdup(model_skins.values[i]));
    list_push(&candidates, xformat("players/%s/%s.pcx", model, known));
    list_push(&candidates, xformat("players/%s/grunt.pcx", model));
    list_push(&candidates, xformat("players/%s/skin.pcx", model));
  }
  if (first_open(mounts, (const char *const *)candidates.values, candidates.count, out)) {
    qa_cleanup_pop(&candidate_cleanup); list_free(&candidates);
    return;
  }
  char *directory = xformat("players/%s", model);
  size_t count = 0;
  char **files = qa_content_mounted_list_files(mounts, directory, ".pcx", &count);
  free(directory);
  if (files == NULL) qa_range_error("Out of memory");
  string_list listing = {files, count, count}, listed = {0};
  qa_cleanup listing_cleanup, listed_cleanup;
  qa_cleanup_push(&listing_cleanup, list_free, &listing);
  qa_cleanup_push(&listed_cleanup, list_free, &listed);
  for (size_t i = 0; i < listing.count; i++) {
    const char *name = listing.values[i];
    const char *slash = strrchr(name, '/'), *backslash = strrchr(name, '\\');
    const char *last = slash > backslash ? slash : backslash;
    const char *base = last == NULL ? name : last + 1;
    if (lower_ends_with(base, "_i.pcx") || lower_equals(base, "weapon.pcx")) continue;
    list_push(&listed, xstrdup(base));
  }
  /* TS Array.prototype.sort default: UTF-16 code-unit order (byte order for ASCII). */
  qa_js_sort(listed.values, listed.count, sizeof *listed.values, compare_strings);
  bool opened = false;
  if (listed.count != 0) {
    char *path = xformat("players/%s/%s", model, listed.values[0]);
    qa_content_opened_resource resource;
    memset(&resource, 0, sizeof resource);
    opened = qa_content_mounted_open(mounts, path, NULL, NULL, &resource);
    free(path);
    if (opened) { out->reference = resource.reference; out->bytes = resource.bytes; out->length = resource.byte_length; }
  }
  qa_cleanup_pop(&listed_cleanup); list_free(&listed);
  qa_cleanup_pop(&listing_cleanup); list_free(&listing);
  if (!opened) required_open(mounts, (const char *const *)candidates.values, candidates.count, out);
  qa_cleanup_pop(&candidate_cleanup); list_free(&candidates);
}

static qa_app_character_turntable_scene *load_q2_scene(qa_content_mounted *mounts, const qa_app_turntable_request *request) {
  model_state *state = xcalloc(1, sizeof *state);
  qa_cleanup cleanup, state_cleanup;
  qa_cleanup_push(&state_cleanup, free, state);
  qa_cleanup_push(&cleanup, model_state_free, state);
  char *model_path = xformat("players/%s/tris.md2", request->model);
  qa_cleanup path_cleanup;
  qa_cleanup_push(&path_cleanup, free, model_path);
  required_open(mounts, (const char *const *)&model_path, 1, &state->model_asset);
  qa_cleanup_pop(&path_cleanup); free(model_path);
  qa_parse_md2(state->model_asset.bytes, state->model_asset.length, state->model_asset.reference->requested_path, &state->q2);
  state->has_q2 = true;
  load_palette(mounts, QA_GAME_FAMILY_Q2, &state->palette);
  q2_skin(mounts, request->model, request->skin, state->q2.skins, &state->skin_asset);
  const qa_model_frame *frame = NULL;
  for (size_t i = 0; i < state->q2.frame_count && frame == NULL; i++)
    if (name_starts_with_stand(state->q2.frames[i].frame.name)) frame = &state->q2.frames[i].frame;
  if (frame == NULL) {
    if (state->q2.frame_count == 0) missing("Q2 frame", 0);
    frame = &state->q2.frames[0].frame;
  }
  qa_build_md2_geometry(&state->q2, frame->vertices, frame->vertex_count, &state->geometry);
  state->has_geometry = true;
  if (!qa_decode_pcx(state->skin_asset.bytes, state->skin_asset.length, state->skin_asset.reference->requested_path, &state->pcx))
    qa_range_error("Out of memory");
  state->has_pcx = true;
  size_t flooded_length = 0;
  state->flooded = qa_flood_skin(state->pcx.indices, state->pcx.indices_length, (double)state->pcx.width,
      (double)state->pcx.height, state->palette.palette, &flooded_length);
  qa_image_level level = {(double)state->pcx.width, (double)state->pcx.height, {state->flooded, flooded_length}};
  qa_palette_transparency transparency = {"index", 255};
  qa_indexed_render_image image = qa_indexed_render_image_make(&level, 1, state->palette.palette, transparency, NULL, NULL);
  if (!qa_expand_indexed_image(&image, 0, QA_PALETTE_LAYER_COMBINED, &state->expanded)) qa_range_error("Out of memory");
  geometry_to_builder(state, state->expanded);
  state->expanded.pixels.bytes = NULL;
  qa_app_character_turntable_scene *scene = finish_scene(&state->builder, 0.85);
  qa_cleanup_pop(&cleanup);
  model_state_free(state);
  qa_cleanup_pop(&state_cleanup);
  free(state);
  return scene;
}

/* ---- Q3 ------------------------------------------------------------- */

/* TS animationFrame. */
static size_t animation_frame(const qa_q3_mesh_model *model, bool present, double first_frame) {
  if (model->frame_count == 0) qa_raise("Error", 0, 0, "Q3 character part has no frames");
  double value = qa_js_max(0, qa_js_min((double)model->frame_count - 1, trunc(present ? first_frame : 0)));
  return (size_t)value;
}

typedef struct texture_cache_entry { char *shader; bool has_index; size_t index; } texture_cache_entry;
typedef struct q3_state {
  qa_q3_character_assets assets; bool has_assets;
  texture_cache_entry *cache; size_t cache_count;
  scene_builder builder;
  qa_content_mounted *mounts;
} q3_state;
static void q3_state_free(void *arg) {
  q3_state *state = arg;
  if (state->has_assets) qa_q3_character_assets_destroy(&state->assets);
  for (size_t i = 0; i < state->cache_count; i++) free(state->cache[i].shader);
  free(state->cache);
  builder_free(&state->builder);
  memset(state, 0, sizeof *state);
}
/* textureIndex: false is TS null. */
static bool texture_index(q3_state *state, const char *shader, size_t *out) {
  for (size_t i = 0; i < state->cache_count; i++)
    if (strcmp(state->cache[i].shader, shader) == 0) { *out = state->cache[i].index; return state->cache[i].has_index; }
  qa_app_turntable_texture loaded;
  memset(&loaded, 0, sizeof loaded);
  bool drawn = load_texture(state->mounts, shader, QA_GAME_FAMILY_Q3, &loaded);
  state->cache = xrealloc(state->cache, (state->cache_count + 1) * sizeof *state->cache);
  texture_cache_entry *entry = &state->cache[state->cache_count++];
  entry->shader = xstrdup(shader); entry->has_index = drawn; entry->index = 0;
  if (!drawn) return false;
  entry->index = builder_texture(&state->builder, loaded);
  *out = entry->index;
  return true;
}
static const char *part_shader(const qa_q3_character_part *part, const char *name) {
  for (size_t i = 0; i < part->surface_count; i++) if (strcmp(part->surfaces[i].name, name) == 0) return part->surfaces[i].shader;
  return NULL;
}
static void append_part(q3_state *state, const qa_q3_character_part *part, size_t frame, qa_model_transform transform) {
  for (size_t s = 0; s < part->model.surface_count; s++) {
    const qa_q3_mesh_surface *surface = &part->model.surfaces[s];
    char *with_t = xformat("%s_t", surface->name);
    const char *shader = part_shader(part, surface->name);
    if (shader == NULL) shader = part_shader(part, with_t);
    free(with_t);
    if (shader == NULL) {
      size_t length = strlen(surface->name);
      char *without_t = xstrdup(surface->name);
      if (length >= 2 && strcmp(surface->name + length - 2, "_t") == 0) without_t[length - 2] = '\0';
      shader = part_shader(part, without_t);
      free(without_t);
    }
    if (shader == NULL) shader = surface->shaders.count != 0 ? surface->shaders.values[0] : "";
    size_t texture;
    if (!texture_index(state, shader, &texture)) continue;
    double base = (double)state->builder.vertex_count;
    if (frame >= surface->frame_count) missing("Q3 surface frame", (double)frame);
    const qa_model_vertex *vertices = surface->frame_vertices[frame];
    size_t vertex_count = surface->frame_vertex_counts[frame];
    for (size_t index = 0; index < vertex_count; index++) {
      if (index >= surface->texture_coordinate_count) missing("Q3 UV", (double)index);
      builder_vertex(&state->builder, (qa_app_turntable_vertex){qa_model_world_point(transform, vertices[index].position),
          qa_model_world_direction(transform, vertices[index].normal), surface->texture_coordinates[index], (double)texture});
    }
    for (size_t i = 0; i < surface->indices.count; i++) builder_index(&state->builder, base + surface->indices.values[i]);
  }
}
static const qa_model_tag *find_tag(const qa_q3_mesh_model *model, size_t frame, const char *name) {
  if (frame >= model->tag_row_count) return NULL;
  for (size_t i = 0; i < model->tag_row_counts[frame]; i++)
    if (strcmp(model->tag_rows[frame][i].name, name) == 0) return &model->tag_rows[frame][i];
  return NULL;
}
static bool mounted_resources_open(void *ctx, const char *path, qa_q3_character_opened_resource *out) {
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(ctx, path, NULL, NULL, &opened)) return false;
  out->reference = *opened.reference; out->bytes = opened.bytes; out->byte_length = opened.byte_length;
  return true;
}
static const qa_model_transform identity_transform = {{0, 0, 0}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {1, 1, 1}};

static qa_app_character_turntable_scene *load_q3_scene(qa_content_mounted *mounts, const qa_app_turntable_request *request) {
  q3_state *state = xcalloc(1, sizeof *state);
  qa_cleanup cleanup, state_cleanup;
  qa_cleanup_push(&state_cleanup, free, state);
  qa_cleanup_push(&cleanup, q3_state_free, state);
  state->mounts = mounts;
  const char *skin = request->skin[0] != '\0' ? request->skin : "default";
  qa_q3_character_selection selection = {request->model, skin, "", skin, NULL, ""};
  qa_q3_character_resources resources = {mounts, mounted_resources_open};
  qa_q3_load_character(resources, selection, &state->assets);
  state->has_assets = true;
  const qa_q3_character_assets *assets = &state->assets;
  size_t legs_frame = animation_frame(&assets->lower.model, assets->animation.animation_present[QA_Q3_ANIM_LEGS_IDLE],
      assets->animation.animations[QA_Q3_ANIM_LEGS_IDLE].first_frame);
  size_t torso_frame = animation_frame(&assets->upper.model, assets->animation.animation_present[QA_Q3_ANIM_TORSO_STAND],
      assets->animation.animations[QA_Q3_ANIM_TORSO_STAND].first_frame);
  append_part(state, &assets->lower, legs_frame, identity_transform);
  const qa_model_tag *torso_tag = find_tag(&assets->lower.model, legs_frame, "tag_torso");
  qa_model_transform torso_transform = torso_tag == NULL ? identity_transform
      : (qa_model_transform){torso_tag->origin, torso_tag->axis, {1, 1, 1}};
  append_part(state, &assets->upper, torso_frame, torso_transform);
  const qa_model_tag *head_tag = find_tag(&assets->upper.model, torso_frame, "tag_head");
  qa_model_transform head_transform = head_tag == NULL ? torso_transform
      : qa_compose_model_transform(torso_transform, (qa_model_transform){head_tag->origin, head_tag->axis, {1, 1, 1}});
  append_part(state, &assets->head, 0, head_transform);
  qa_app_character_turntable_scene *scene = finish_scene(&state->builder, 0.85);
  qa_cleanup_pop(&cleanup);
  q3_state_free(state);
  qa_cleanup_pop(&state_cleanup);
  free(state);
  return scene;
}

qa_app_character_turntable_scene *qa_app_load_character_turntable_scene(qa_content_mounted *mounts,
    const qa_app_turntable_request *request) {
  if (blank(request->model)) qa_raise("Error", 0, 0, "Character preview has no model");
  return request->family == QA_GAME_FAMILY_Q1 ? load_q1_scene(mounts, request)
      : request->family == QA_GAME_FAMILY_Q2 ? load_q2_scene(mounts, request)
      : load_q3_scene(mounts, request);
}

/* ---- software rasterizer ----------------------------------------------- */

typedef struct projected_vertex { double x, y, q, u, v, nx, ny, nz, texture; } projected_vertex;
typedef struct projected_point { double x, y, q, perspective; } projected_point;
typedef struct projection {
  qa_vec3 center; double cos, sin, cos_pitch, sin_pitch, camera_distance, scale, width, height;
} projection;
static projected_point project_point(const projection *p, qa_vec3 position) {
  double dx = position.x - p->center.x, dy = position.y - p->center.y, dz = position.z - p->center.z;
  double forward = dx * p->cos - dy * p->sin, right = dx * p->sin + dy * p->cos;
  double up = dz * p->cos_pitch - forward * p->sin_pitch, toward = forward * p->cos_pitch + dz * p->sin_pitch;
  double perspective = p->camera_distance / qa_js_max(1, p->camera_distance - toward);
  return (projected_point){p->width / 2 + right * p->scale * perspective, p->height * 0.52 - up * p->scale * perspective,
      perspective / p->camera_distance, perspective};
}

/* JS `a % b` on finite doubles. */
static double js_mod(double a, double b) { return fmod(a, b); }

/* TS sampleTexture. */
static void sample_texture(const qa_app_turntable_texture *texture, double u, double v, double output[4]) {
  double wrapped_u = u - floor(u), wrapped_v = v - floor(v);
  double px = wrapped_u * texture->width - 0.5, py = wrapped_v * texture->height - 0.5;
  double x0 = floor(px), y0 = floor(py), fx = px - x0, fy = py - y0;
  double xa = js_mod(js_mod(x0, texture->width) + texture->width, texture->width);
  double xb = js_mod(js_mod(x0 + 1, texture->width) + texture->width, texture->width);
  double ya = js_mod(js_mod(y0, texture->height) + texture->height, texture->height);
  double yb = js_mod(js_mod(y0 + 1, texture->height) + texture->height, texture->height);
  output[0] = 0; output[1] = 0; output[2] = 0; output[3] = 0;
  for (int row = 0; row < 2; row++)
    for (int column = 0; column < 2; column++) {
      double weight = (column == 0 ? 1 - fx : fx) * (row == 0 ? 1 - fy : fy);
      double offset = ((row == 0 ? ya : yb) * texture->width + (column == 0 ? xa : xb)) * 4;
      for (int channel = 0; channel < 4; channel++) {
        double at = offset + channel;
        /* pixels[at] ?? 0: an absent element reads as 0. */
        double value = at >= 0 && at < (double)texture->pixel_length && at == trunc(at) ? texture->pixels[(size_t)at] : 0;
        output[channel] = output[channel] + value * weight;
      }
    }
}

static uint8_t clamp_byte(double value) {
  return qa_js_to_uint8(qa_js_max(0, qa_js_min(255, trunc(value))));
}

void qa_app_render_character_turntable(const qa_app_character_turntable_scene *scene, double yaw_radians,
    double width, double height, qa_image_level *out) {
  if (!(width == trunc(width) && fabs(width) <= (double)QA_MAX_SAFE_INTEGER) || !(height == trunc(height) && fabs(height) <= (double)QA_MAX_SAFE_INTEGER)
      || width <= 0 || height <= 0) qa_range_error("Invalid turntable dimensions");
  size_t pixel_count = (size_t)width * (size_t)height;
  uint8_t *pixels = xcalloc(pixel_count * 4, 1);
  float *depths = xmalloc(pixel_count * sizeof *depths);
  for (size_t i = 0; i < pixel_count; i++) depths[i] = -INFINITY;
  qa_cleanup pixel_cleanup, depth_cleanup;
  qa_cleanup_push(&pixel_cleanup, free, pixels);
  qa_cleanup_push(&depth_cleanup, free, depths);
  projection p;
  p.center = scene->center; p.width = width; p.height = height;
  p.cos = cos(yaw_radians); p.sin = sin(yaw_radians);
  double pitch = 0.18;
  p.cos_pitch = cos(pitch); p.sin_pitch = sin(pitch);
  double vertical_extent = scene->half_height * p.cos_pitch + scene->radius * p.sin_pitch;
  p.scale = qa_js_min(width * 0.44 / scene->radius, height * 0.47 / vertical_extent);
  p.camera_distance = qa_js_max(qa_js_max(80, scene->radius * 4.5), scene->half_height * 3);
  projected_point floor_point = project_point(&p, (qa_vec3){scene->center.x, scene->center.y, scene->floor_z});
  double floor_radius_x = scene->radius * 1.10 * p.scale * floor_point.perspective;
  double floor_radius_y = qa_js_max(5, floor_radius_x * 0.24);
  for (double y = qa_js_max(0, floor(floor_point.y - floor_radius_y)); y <= qa_js_min(height - 1, ceil(floor_point.y + floor_radius_y)); y++)
    for (double x = qa_js_max(0, floor(floor_point.x - floor_radius_x)); x <= qa_js_min(width - 1, ceil(floor_point.x + floor_radius_x)); x++) {
      double ellipse = qa_js_pow((x + 0.5 - floor_point.x) / floor_radius_x, 2) + qa_js_pow((y + 0.5 - floor_point.y) / floor_radius_y, 2);
      if (ellipse > 1) continue;
      uint8_t *pixel = pixels + ((size_t)y * (size_t)width + (size_t)x) * 4;
      if (ellipse > 0.90) { pixel[0] = 176; pixel[1] = 118; pixel[2] = 52; pixel[3] = 150; }
      else { pixel[0] = 5; pixel[1] = 8; pixel[2] = 12; pixel[3] = qa_js_to_uint8(trunc(105 * (1 - ellipse * 0.45))); }
    }
  projected_vertex *projected = xmalloc((scene->vertex_count == 0 ? 1 : scene->vertex_count) * sizeof *projected);
  qa_cleanup projected_cleanup;
  qa_cleanup_push(&projected_cleanup, free, projected);
  for (size_t i = 0; i < scene->vertex_count; i++) {
    const qa_app_turntable_vertex *vertex = &scene->vertices[i];
    projected_point point = project_point(&p, vertex->position);
    double dx = vertex->normal.x, dy = vertex->normal.y, dz = vertex->normal.z;
    double forward = dx * p.cos - dy * p.sin, right = dx * p.sin + dy * p.cos;
    projected[i] = (projected_vertex){point.x, point.y, point.q, vertex->tex_coord.x, vertex->tex_coord.y,
        right, dz * p.cos_pitch - forward * p.sin_pitch, forward * p.cos_pitch + dz * p.sin_pitch, vertex->texture};
  }
  qa_app_turntable_texture missing = {0};
  bool has_missing = false;
  double sample[4] = {0, 0, 0, 0};
  for (size_t index = 0; index + 2 < scene->index_count; index += 3) {
    const projected_vertex *corners[3];
    bool absent = false;
    for (int k = 0; k < 3; k++) {
      double at = scene->indices[index + (size_t)k];
      /* projected[at] is undefined for a non-row index. */
      if (!(at >= 0 && at < (double)scene->vertex_count && at == trunc(at))) { absent = true; break; }
      corners[k] = &projected[(size_t)at];
    }
    if (absent) continue;
    const projected_vertex *a = corners[0], *b = corners[1], *c = corners[2];
    double denominator = (b->y - c->y) * (a->x - c->x) + (c->x - b->x) * (a->y - c->y);
    if (fabs(denominator) < 1e-9) continue;
    double min_x = qa_js_max(0, floor(qa_js_min(qa_js_min(a->x, b->x), c->x)));
    double max_x = qa_js_min(width - 1, ceil(qa_js_max(qa_js_max(a->x, b->x), c->x)));
    double min_y = qa_js_max(0, floor(qa_js_min(qa_js_min(a->y, b->y), c->y)));
    double max_y = qa_js_min(height - 1, ceil(qa_js_max(qa_js_max(a->y, b->y), c->y)));
    const qa_app_turntable_texture *texture;
    if (a->texture >= 0 && a->texture < (double)scene->texture_count && a->texture == trunc(a->texture))
      texture = &scene->textures[(size_t)a->texture];
    else {
      if (!has_missing) { missing = missing_texture(); has_missing = true; }
      texture = &missing;
    }
    for (double y = min_y; y <= max_y; y++)
      for (double x = min_x; x <= max_x; x++) {
        double px = x + 0.5, py = y + 0.5;
        double wa = ((b->y - c->y) * (px - c->x) + (c->x - b->x) * (py - c->y)) / denominator;
        double wb = ((c->y - a->y) * (px - c->x) + (a->x - c->x) * (py - c->y)) / denominator;
        double wc = 1 - wa - wb;
        if (wa < -0.0001 || wb < -0.0001 || wc < -0.0001) continue;
        double q = wa * a->q + wb * b->q + wc * c->q;
        if (q <= 0) continue;
        size_t pixel_index = (size_t)y * (size_t)width + (size_t)x;
        if (q <= (double)depths[pixel_index]) continue;
        double u = (wa * a->u * a->q + wb * b->u * b->q + wc * c->u * c->q) / q;
        double v = (wa * a->v * a->q + wb * b->v * b->q + wc * c->v * c->q) / q;
        sample_texture(texture, u, v, sample);
        if (strcmp(texture->alpha_test, "gt0") == 0 && sample[3] <= 0) continue;
        if (strcmp(texture->alpha_test, "ge128") == 0 && sample[3] < 128) continue;
        if (strcmp(texture->alpha_test, "lt128") == 0 && sample[3] >= 128) continue;
        double nx = (wa * a->nx * a->q + wb * b->nx * b->q + wc * c->nx * c->q) / q;
        double ny = (wa * a->ny * a->q + wb * b->ny * b->q + wc * c->ny * c->q) / q;
        double nz = (wa * a->nz * a->q + wb * b->nz * b->q + wc * c->nz * c->q) / q;
        double normal_length = qa_js_hypot3(nx, ny, nz);
        if (normal_length == 0 || isnan(normal_length)) normal_length = 1; /* || 1 */
        nx /= normal_length; ny /= normal_length; nz /= normal_length;
        double key = qa_js_max(0, nx * -0.35 + ny * 0.72 + nz * 0.60);
        double fill = qa_js_max(0, nx * 0.45 + ny * 0.20 + nz * 0.75);
        double shade = qa_js_min(1.18, 0.42 + key * 0.62 + fill * 0.28);
        depths[pixel_index] = (float)q; /* Float32Array store */
        uint8_t *pixel = pixels + pixel_index * 4;
        pixel[0] = clamp_byte(sample[0] * shade);
        pixel[1] = clamp_byte(sample[1] * shade);
        pixel[2] = clamp_byte(sample[2] * shade);
        pixel[3] = clamp_byte(sample[3]);
      }
  }
  if (has_missing) free(missing.pixels);
  qa_cleanup_pop(&projected_cleanup); free(projected);
  qa_cleanup_pop(&depth_cleanup); free(depths);
  qa_cleanup_pop(&pixel_cleanup);
  out->width = width; out->height = height; out->pixels.bytes = pixels; out->pixels.length = pixel_count * 4;
}

/* ---- CharacterTurntable ----------------------------------------------- */

const char qa_app_character_turntable_resource[] = "resource:character-turntable:viewer";

typedef struct mount_entry {
  char *key;
  qa_catalog_mount_list list;
  qa_mount_id *order;
  char *plan_id;
  qa_content_mounted *mounted;
} mount_entry;
typedef struct scene_entry { char *key; qa_app_character_turntable_scene *scene; } scene_entry;

struct qa_app_character_turntable {
  qa_app_character_turntable_options options;
  const qa_scene_image_handle *image;
  qa_image_level initial_level;
  qa_image_picture image_picture;
  mount_entry *mounts; size_t mount_count;
  scene_entry *scenes; size_t scene_count;
  char **failures; size_t failure_count;
  bool has_stop; qa_scene_animation_token stop;
  char *desired_key;
  qa_app_character_turntable_scene *scene;
  char *scene_key;
  double last_requested_at, last_rendered_at;
  bool has_rotation_started_at; double rotation_started_at;
  uint8_t *frames[QA_APP_TURNTABLE_FRAME_RING]; size_t next_frame;
  bool closed;
};

static void print_text(qa_app_character_turntable *turntable, const char *text) {
  turntable->options.print(turntable->options.print_ctx, text);
}

typedef struct render_job {
  const qa_app_character_turntable_scene *scene; double yaw; qa_image_level level;
} render_job;
static void render_job_run(void *arg) {
  render_job *job = arg;
  qa_app_render_character_turntable(job->scene, job->yaw, QA_APP_TURNTABLE_WIDTH, QA_APP_TURNTABLE_HEIGHT, &job->level);
}
typedef struct update_job { qa_app_character_turntable *turntable; qa_image_level level; } update_job;
static void update_job_run(void *arg) {
  update_job *job = arg;
  qa_scene_image_registry_update(job->turntable->options.images, job->turntable->image->image, 0, job->level);
}

/* TS tick(milliseconds), driven by the image registry animation pump. */
static void tick(void *ctx, double milliseconds) {
  qa_app_character_turntable *turntable = ctx;
  if (turntable->closed || turntable->scene == NULL || turntable->scene_key == NULL
      || strcmp(turntable->scene_key, turntable->desired_key) != 0) return;
  if (milliseconds - turntable->last_requested_at > 300) return;
  if (milliseconds - turntable->last_rendered_at < QA_APP_TURNTABLE_FRAME_MILLISECONDS) return;
  if (!turntable->has_rotation_started_at) { turntable->rotation_started_at = milliseconds; turntable->has_rotation_started_at = true; }
  double elapsed = qa_js_max(0, milliseconds - turntable->rotation_started_at);
  double yaw = turntable->scene->initial_yaw + js_mod(elapsed, QA_APP_TURNTABLE_ROTATION_MILLISECONDS)
      / QA_APP_TURNTABLE_ROTATION_MILLISECONDS * PI * 2;
  render_job job = {.scene = turntable->scene, .yaw = yaw};
  qa_caught_error caught;
  bool ok = qa_try(render_job_run, &job, &caught);
  if (ok) {
    update_job update = {turntable, job.level};
    ok = qa_try(update_job_run, &update, &caught);
    if (ok) {
      size_t slot = turntable->next_frame;
      free(turntable->frames[slot]);
      turntable->frames[slot] = (uint8_t *)job.level.pixels.bytes;
      turntable->next_frame = (slot + 1) % QA_APP_TURNTABLE_FRAME_RING;
      turntable->last_rendered_at = milliseconds;
      return;
    }
    free((void *)job.level.pixels.bytes);
  }
  qa_app_character_turntable_scene_release(turntable->scene);
  turntable->scene = NULL;
  char *text = xformat("Character preview failed: %s\n", caught.message);
  print_text(turntable, text);
  free(text);
}
static void animation_stopped(void *ctx) {
  qa_app_character_turntable *turntable = ctx;
  turntable->has_stop = false;
}

qa_app_character_turntable *qa_app_character_turntable_create(qa_app_character_turntable_options options) {
  qa_app_character_turntable *turntable = xcalloc(1, sizeof *turntable);
  turntable->options = options;
  turntable->desired_key = xstrdup("");
  turntable->last_requested_at = -INFINITY;
  turntable->last_rendered_at = -INFINITY;
  uint8_t *pixels = xcalloc((size_t)QA_APP_TURNTABLE_WIDTH * QA_APP_TURNTABLE_HEIGHT * 4, 1);
  turntable->initial_level = (qa_image_level){QA_APP_TURNTABLE_WIDTH, QA_APP_TURNTABLE_HEIGHT,
      {pixels, (size_t)QA_APP_TURNTABLE_WIDTH * QA_APP_TURNTABLE_HEIGHT * 4}};
  qa_image_level storage;
  qa_render_image content = qa_scene_rgba_image(turntable->initial_level, &storage);
  content.levels = &turntable->initial_level;
  qa_texture_sampling sampling = {"clamp", "linear"};
  turntable->image = qa_scene_image_registry_register_generated(options.images, "character-turntable", content, sampling);
  turntable->image_picture = (qa_image_picture){"character-turntable", turntable->image->image};
  turntable->stop = qa_scene_image_registry_track_animation(options.images, turntable->image->image, tick, turntable,
      animation_stopped, turntable);
  turntable->has_stop = true;
  return turntable;
}

static mount_entry *find_mount(qa_app_character_turntable *turntable, const char *key) {
  for (size_t i = 0; i < turntable->mount_count; i++) if (strcmp(turntable->mounts[i].key, key) == 0) return &turntable->mounts[i];
  return NULL;
}
static void mount_entry_free(mount_entry *entry) {
  if (entry->mounted != NULL) {
    qa_app_turntable_forget_mounts(entry->mounted);
    qa_content_mounted_destroy(entry->mounted);
  }
  qa_content_catalog_mount_list_free(&entry->list);
  free(entry->order); free(entry->plan_id); free(entry->key);
}
static void mount_entry_cleanup(void *arg) {
  mount_entry_free(arg);
  free(arg);
}

/* TS mounted(catalog, product); a failed open is not cached
 * (cacheEvictingRejection). */
static qa_content_mounted *mounted_for(qa_app_character_turntable *turntable, qa_content_catalog *catalog, qa_content_id product) {
  char *key = xformat("%s:%s", QA_JS_NUMBER((double)catalog->generation), product);
  mount_entry *existing = find_mount(turntable, key);
  if (existing != NULL) { free(key); return existing->mounted; }
  mount_entry *entry = xcalloc(1, sizeof *entry);
  entry->key = key;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, mount_entry_cleanup, entry);
  if (!qa_content_catalog_mounts_for(catalog, product, &entry->list)) qa_range_error("Out of memory");
  char *sanitized = xmalloc(strlen(product) + 1);
  size_t length = 0;
  for (const char *cursor = product; *cursor != '\0';) {
    unsigned char ch = (unsigned char)*cursor;
    if (isalnum(ch) || ch == '.' || ch == '_' || ch == '-') { sanitized[length++] = (char)ch; cursor++; continue; }
    /* /[^a-zA-Z0-9._-]+/g: one "-" per run (a multi-byte character is one run). */
    while (*cursor != '\0' && !(isalnum((unsigned char)*cursor) || *cursor == '.' || *cursor == '_' || *cursor == '-')) cursor++;
    sanitized[length++] = '-';
  }
  sanitized[length] = '\0';
  char *revision = xformat("character-%s-%s", QA_JS_NUMBER((double)catalog->generation), sanitized);
  free(sanitized);
  entry->plan_id = qa_create_mount_plan_id("startup", revision);
  free(revision);
  if (entry->plan_id == NULL) qa_range_error("Out of memory");
  entry->order = xcalloc(entry->list.count, sizeof *entry->order);
  for (size_t i = 0; i < entry->list.count; i++)
    entry->order[i] = entry->list.mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE
        ? entry->list.mounts[i].as.archive.identity.id : entry->list.mounts[i].as.loose.identity.id;
  qa_resolved_mount_plan plan = {entry->plan_id, entry->list.mounts, entry->list.count, entry->order, entry->list.count, NULL, 0};
  qa_open_mount_options mount_options;
  memset(&mount_options, 0, sizeof mount_options);
  entry->mounted = qa_open_mount_plan(&plan, &mount_options);
  if (entry->mounted == NULL) qa_range_error("Out of memory");
  qa_cleanup_pop(&cleanup);
  turntable->mounts = xrealloc(turntable->mounts, (turntable->mount_count + 1) * sizeof *turntable->mounts);
  turntable->mounts[turntable->mount_count++] = *entry;
  free(entry);
  return turntable->mounts[turntable->mount_count - 1].mounted;
}

typedef struct load_job {
  qa_app_character_turntable *turntable; qa_content_catalog *catalog;
  const qa_app_turntable_request *preview;
  qa_app_character_turntable_scene *result;
} load_job;
static void load_job_run(void *arg) {
  load_job *job = arg;
  qa_content_mounted *mounts = mounted_for(job->turntable, job->catalog, job->preview->product);
  job->result = qa_app_load_character_turntable_scene(mounts, job->preview);
}

static void clear_scenes(qa_app_character_turntable *turntable) {
  for (size_t i = 0; i < turntable->scene_count; i++) {
    free(turntable->scenes[i].key);
    qa_app_character_turntable_scene_release(turntable->scenes[i].scene);
  }
  free(turntable->scenes);
  turntable->scenes = NULL; turntable->scene_count = 0;
}

/* TS load(key, catalog, preview). */
static void load(qa_app_character_turntable *turntable, const char *key, qa_content_catalog *catalog,
    const qa_app_turntable_request *preview) {
  scene_entry *pending = NULL;
  for (size_t i = 0; i < turntable->scene_count; i++) if (strcmp(turntable->scenes[i].key, key) == 0) pending = &turntable->scenes[i];
  if (pending == NULL) {
    if (turntable->scene_count >= 24) clear_scenes(turntable);
    load_job job = {turntable, catalog, preview, NULL};
    qa_caught_error caught;
    if (!qa_try(load_job_run, &job, &caught)) {
      bool seen = false;
      for (size_t i = 0; i < turntable->failure_count; i++) if (strcmp(turntable->failures[i], key) == 0) seen = true;
      if (!seen) {
        turntable->failures = xrealloc(turntable->failures, (turntable->failure_count + 1) * sizeof *turntable->failures);
        turntable->failures[turntable->failure_count++] = xstrdup(key);
        char *text = xformat("Character preview unavailable: %s\n", caught.message);
        print_text(turntable, text);
        free(text);
      }
      job.result = NULL;
    }
    turntable->scenes = xrealloc(turntable->scenes, (turntable->scene_count + 1) * sizeof *turntable->scenes);
    pending = &turntable->scenes[turntable->scene_count++];
    pending->key = xstrdup(key); pending->scene = job.result;
  }
  if (turntable->closed || strcmp(turntable->desired_key, key) != 0) return;
  qa_app_character_turntable_scene_retain(pending->scene);
  qa_app_character_turntable_scene_release(turntable->scene);
  turntable->scene = pending->scene;
  free(turntable->scene_key);
  turntable->scene_key = xstrdup(key);
  turntable->has_rotation_started_at = false;
  turntable->last_rendered_at = -INFINITY;
}

typedef struct catalog_job { qa_app_character_turntable *turntable; qa_content_catalog *catalog; } catalog_job;
static void catalog_job_run(void *arg) {
  catalog_job *job = arg;
  job->catalog = job->turntable->options.catalog(job->turntable->options.catalog_ctx);
}

const char *qa_app_character_turntable_request(qa_app_character_turntable *turntable,
    const qa_app_turntable_request *preview, double now_milliseconds) {
  if (turntable->closed) return qa_app_character_turntable_resource;
  turntable->last_requested_at = now_milliseconds;
  catalog_job job = {turntable, NULL};
  qa_caught_error caught;
  if (!qa_try(catalog_job_run, &job, &caught)) return qa_app_character_turntable_resource;
  char generation[40], top[40], bottom[40];
  qa_net_js_number((double)job.catalog->generation, generation, sizeof generation);
  qa_net_js_number(preview->top_color, top, sizeof top);
  qa_net_js_number(preview->bottom_color, bottom, sizeof bottom);
  const char *parts[7] = {generation, preview->product, qa_game_family_name(preview->family), preview->model,
      preview->skin, top, bottom};
  size_t length = 0;
  for (int i = 0; i < 7; i++) length += strlen(parts[i]) + 1;
  /* [..].join("\0"): the separator is a NUL character, so the key is kept
   * with an explicit length-free encoding: "\x01" stands for "\0" (no part
   * contains either byte in a valid preview). */
  char *key = xmalloc(length + 1);
  key[0] = '\0';
  for (int i = 0; i < 7; i++) { if (i != 0) strcat(key, "\x01"); strcat(key, parts[i]); }
  if (strcmp(key, turntable->desired_key) != 0) {
    free(turntable->desired_key);
    turntable->desired_key = key;
    turntable->has_rotation_started_at = false;
    load(turntable, key, job.catalog, preview);
  } else free(key);
  return qa_app_character_turntable_resource;
}

const qa_image_picture *qa_app_character_turntable_picture(const qa_app_character_turntable *turntable, const char *resource) {
  return strcmp(resource, qa_app_character_turntable_resource) == 0 ? &turntable->image_picture : NULL;
}

void qa_app_character_turntable_close(qa_app_character_turntable *turntable) {
  if (turntable->closed) return;
  turntable->closed = true;
  if (turntable->has_stop) qa_scene_animation_token_stop(turntable->stop);
  turntable->has_stop = false;
  /* TS closes each pending mount asynchronously, ignoring failures. */
  for (size_t i = 0; i < turntable->mount_count; i++) mount_entry_free(&turntable->mounts[i]);
  free(turntable->mounts);
  turntable->mounts = NULL; turntable->mount_count = 0;
  clear_scenes(turntable);
  if (qa_scene_image_registry_is_resident(turntable->options.images, turntable->image->image))
    qa_scene_image_registry_release(turntable->options.images, turntable->image->image);
}

void qa_app_character_turntable_destroy(qa_app_character_turntable *turntable) {
  if (turntable == NULL) return;
  qa_app_character_turntable_close(turntable);
  qa_app_character_turntable_scene_release(turntable->scene);
  for (size_t i = 0; i < turntable->failure_count; i++) free(turntable->failures[i]);
  free(turntable->failures);
  for (size_t i = 0; i < QA_APP_TURNTABLE_FRAME_RING; i++) free(turntable->frames[i]);
  free((void *)turntable->initial_level.pixels.bytes);
  free(turntable->desired_key); free(turntable->scene_key);
  free(turntable);
}
