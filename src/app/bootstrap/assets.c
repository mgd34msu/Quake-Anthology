/* Port of src/app/bootstrap/assets.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/render_settings.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "qa/app/bootstrap/image_reader.h"
#include "qa/content/catalog/index.h"
#include "qa/formats/bsp_kind.h"
#include "qa/formats/images/indexed.h"
#include "qa/formats/images/palette.h"
#include "qa/formats/q12_model.h"
#include "qa/formats/q1_map.h"
#include "qa/formats/q2_map.h"
#include "qa/formats/q3_map/entities.h"
#include "qa/formats/q3_map/world.h"
#include "qa/formats/q3_model/md3.h"
#include "qa/formats/q3_model/md5.h"
#include "qa/formats/q3_model/replacements.h"
#include "qa/formats/q3_model/scene.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/materials/compile.h"
#include "qa/media/material.h"
#include "qa/media/playback.h"
#include "qa/media/presentation.h"
#include "qa/text/mounted.h"

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count ? count : 1, size ? size : 1);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size ? size : 1);
  if (next == NULL) qa_range_error("Out of memory");
  return next;
}
static char *xdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  return copy;
}
#define GROW(array, count, cap) do { if ((count) == (cap)) { (cap) = (cap) ? (cap) * 2 : 8; \
  (array) = xrealloc((array), (cap) * sizeof *(array)); } } while (0)

/* Storage the TS garbage collector reclaims: freed at destroy. */
typedef struct owned_node { void (*fn)(void *arg); void *arg; } owned_node;

/* mountedImageReader(catalog, mounts) as a SceneAssetReader. The
 * winner cache lives in qa_app_mounted_image_reader; this adapter is its
 * mount host and records the reference behind the bytes it returned. */
typedef struct image_reader_adapter {
  const qa_content_catalog *catalog;
  qa_content_mounted *mounts;
  qa_app_mounted_image_reader *reader;
  const qa_resolved_resource_reference *last;
} image_reader_adapter;

/* One SceneTextureLoader with its reader (TS activeTextures member). */
typedef struct texture_slot {
  qa_scene_texture_loader *loader;
  image_reader_adapter *reader;
} texture_slot;

typedef struct qa_app_application_assets qa_app_application_assets;
typedef struct provider_entry {
  char *content;
  qa_app_application_assets *assets;
  qa_game_family family;
  qa_content_mounted *mounts;
  bool has_palette; qa_palette palette;
  qa_scene_shader_registry *shaders;
} provider_entry;

typedef struct movie_entry {
  char *content, *path;
  char *target_id; /* TS `${content}\0${path}` material target */
  bool failed; const char *error_name; char *error_message;
  bool has_video; qa_mat_registered_video video;
  uint8_t *bytes; qa_cinematic_playback *playback; qa_material_cinematic *movie;
  bool active;
} movie_entry;

typedef struct model_entry { char *content, *path; qa_app_model_asset asset; } model_entry;
typedef struct variant_entry { qa_app_model_variants *variants; char *content; const char *resource_path; } variant_entry;

/* A loaded menu font or typography (TS loadMenuFont / loadMenuTypography results). */
typedef struct font_entry { qa_app_menu_font *font; } font_entry;
typedef struct typography_entry { qa_app_menu_typography *typography; } typography_entry;

typedef struct retired_entry { int kind; texture_slot textures; font_entry font; typography_entry typography; } retired_entry;
enum { RETIRED_TEXTURES, RETIRED_FONT, RETIRED_TYPOGRAPHY };

struct qa_app_application_assets {
  qa_app_loaded_content *content;
  const qa_content_catalog *catalog;
  qa_scene_image_registry images; bool forked_images;
  qa_scene_material_registrations material_registrations;
  qa_media_clock media_clock;
  movie_entry **movies; size_t movie_count, movie_cap;
  texture_slot *active_textures; size_t active_count, active_cap;
  bool closed;
  provider_entry **providers; size_t provider_count, provider_cap;
  model_entry **models; size_t model_count, model_cap;
  variant_entry *variants; size_t variant_count, variant_cap;
  qa_world_scene **brush_scenes; size_t brush_count, brush_cap;
  qa_world_scene *current_world;
  bool has_font; font_entry fonts;
  bool has_typography; typography_entry typography;
  /* The TS font and typography promises stay rejected once they fail. */
  const char *font_error_name; char *font_error;
  const char *typography_error_name; char *typography_error;
  bool has_image_policy; qa_scene_image_policy_owned image_policy;
  qa_model_replacement_policy model_policy;
  retired_entry *retired; size_t retired_count, retired_cap;
  qa_finish_shader_profile (*shader_profile)(void *ctx); void *shader_profile_ctx;
  char **warned; size_t warned_count, warned_cap;
  void (*warn)(void *ctx, const char *text); void *warn_ctx;
  owned_node *owned; size_t owned_count, owned_cap;
  bool has_decoded_world; qa_decoded_world decoded_world;
};

static void own(qa_app_application_assets *assets, void (*fn)(void *arg), void *arg) {
  GROW(assets->owned, assets->owned_count, assets->owned_cap);
  assets->owned[assets->owned_count].fn = fn;
  assets->owned[assets->owned_count].arg = arg;
  assets->owned_count++;
}

static void assert_open(const qa_app_application_assets *assets, const char *message) {
  if (assets->closed) qa_raise("Error", 0, 0, "%s", message);
}

/* TS `{ sample: () => performance.now() }`. */
static double performance_now(void *ctx) {
  (void)ctx;
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}

static const qa_resource_provenance *provenance_of(const qa_resolved_resource_reference *reference) {
  return &reference->provenance;
}
static const qa_mount_identity *provenance_identity(const qa_resource_provenance *provenance) {
  return provenance->kind == QA_CONTENT_MOUNT_ARCHIVE ? &provenance->archive_mount->identity
      : &provenance->loose_mount->identity;
}
static qa_renderer_image resource_source(const qa_resolved_resource_reference *reference) {
  qa_renderer_image image;
  memset(&image, 0, sizeof image);
  image.source_is_resource = true;
  image.source_resource = *reference;
  return image;
}

/* --- mountedImageReader --- */

/* TS userMount(mount) over a provenance. */
static bool user_mount(const qa_content_catalog *catalog, const qa_resource_provenance *provenance) {
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, provenance_identity(provenance)->content);
  const qa_catalog_directory_content *user = product->user_content;
  if (user == NULL) return false;
  if (provenance->kind == QA_CONTENT_MOUNT_LOOSE) return strcmp(provenance->loose_mount->root_path, user->root) == 0;
  for (size_t i = 0; i < user->archive_count; i++)
    if (strcmp(user->archives[i]->path, provenance->archive_mount->archive_path) == 0) return true;
  return false;
}
typedef struct original_filter { const qa_content_catalog *catalog; const char *content; } original_filter;
static bool accept_original(void *ctx, const qa_content_mount *mount) {
  original_filter *filter = ctx;
  const qa_mount_identity *identity = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? &mount->as.archive.identity : &mount->as.loose.identity;
  if (strcmp(identity->content, filter->content) != 0) return false;
  qa_resource_provenance provenance;
  memset(&provenance, 0, sizeof provenance);
  provenance.kind = mount->kind;
  provenance.archive_mount = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? &mount->as.archive : NULL;
  provenance.loose_mount = mount->kind == QA_CONTENT_MOUNT_LOOSE ? &mount->as.loose : NULL;
  return !user_mount(filter->catalog, &provenance);
}
static bool reader_open(void *ctx, const char *path, const void *only_content, bool exclude_user,
    uint8_t **out_bytes, size_t *out_count, const void **out_mount, const char **out_content) {
  image_reader_adapter *adapter = ctx;
  original_filter filter = {adapter->catalog, only_content};
  qa_content_opened_resource opened;
  bool found = qa_content_mounted_open(adapter->mounts, path, exclude_user ? accept_original : NULL,
      exclude_user ? &filter : NULL, &opened);
  adapter->last = found ? opened.reference : NULL;
  if (!found) { *out_bytes = NULL; *out_count = 0; *out_mount = NULL; *out_content = NULL; return false; }
  *out_bytes = opened.bytes;
  *out_count = opened.byte_length;
  *out_mount = opened.reference;
  *out_content = provenance_identity(provenance_of(opened.reference))->content;
  return true;
}
static void reader_assert_open(void *ctx) {
  image_reader_adapter *adapter = ctx;
  qa_content_mounted_assert_open(adapter->mounts);
}
static bool reader_is_user_mount(void *ctx, const void *mount) {
  image_reader_adapter *adapter = ctx;
  return user_mount(adapter->catalog, provenance_of(mount));
}
static bool scene_asset(image_reader_adapter *adapter, bool found, qa_app_scene_asset_bytes bytes,
    qa_scene_asset *out, uint8_t **owned_bytes) {
  if (!found) return false;
  out->bytes = bytes.bytes;
  out->length = bytes.byte_count;
  out->source_sample = resource_source(adapter->last);
  *owned_bytes = bytes.bytes;
  return true;
}
static bool scene_read(void *ctx, const char *path, qa_scene_asset *out, uint8_t **owned_bytes) {
  image_reader_adapter *adapter = ctx;
  qa_app_scene_asset_bytes bytes = {NULL, 0};
  bool found = qa_app_mounted_image_reader_read(adapter->reader, path, &bytes);
  return scene_asset(adapter, found, bytes, out, owned_bytes);
}
static bool scene_read_original(void *ctx, const char *path, qa_scene_asset *out, uint8_t **owned_bytes) {
  image_reader_adapter *adapter = ctx;
  qa_app_scene_asset_bytes bytes = {NULL, 0};
  bool found = qa_app_mounted_image_reader_read_original(adapter->reader, path, &bytes);
  return scene_asset(adapter, found, bytes, out, owned_bytes);
}
static image_reader_adapter *image_reader_create(const qa_content_catalog *catalog, qa_content_mounted *mounts) {
  image_reader_adapter *adapter = xcalloc(1, sizeof *adapter);
  adapter->catalog = catalog;
  adapter->mounts = mounts;
  qa_app_image_reader_host host = {adapter, reader_open, reader_assert_open, reader_is_user_mount};
  adapter->reader = qa_app_mounted_image_reader_create(host);
  if (adapter->reader == NULL) qa_range_error("Out of memory");
  return adapter;
}
static void image_reader_destroy(image_reader_adapter *adapter) {
  if (adapter == NULL) return;
  qa_app_mounted_image_reader_destroy(adapter->reader);
  free(adapter);
}

/* new SceneTextureLoader(images, mountedImageReader(catalog, mounts), palette, options). */
static texture_slot texture_slot_create(qa_app_application_assets *assets, qa_content_mounted *mounts,
    const qa_palette *palette, const qa_scene_image_policy *policy, bool strict) {
  texture_slot slot;
  slot.reader = image_reader_create(assets->catalog, mounts);
  slot.loader = xcalloc(1, sizeof *slot.loader);
  qa_scene_asset_reader reader = {slot.reader, scene_read, scene_read_original};
  qa_scene_texture_loader_options options;
  memset(&options, 0, sizeof options);
  if (policy != NULL) { options.has_policy = true; options.policy = *policy; }
  options.strict = strict;
  if (!qa_scene_texture_loader_init(slot.loader, &assets->images, reader, palette, &options))
    qa_range_error("Out of memory");
  return slot;
}
static void texture_slot_destroy(texture_slot slot) {
  if (slot.loader != NULL) { qa_scene_texture_loader_destroy(slot.loader); free(slot.loader); }
  image_reader_destroy(slot.reader);
}
static void active_add(qa_app_application_assets *assets, texture_slot slot) {
  GROW(assets->active_textures, assets->active_count, assets->active_cap);
  assets->active_textures[assets->active_count++] = slot;
}
static void active_delete(qa_app_application_assets *assets, const qa_scene_texture_loader *loader) {
  for (size_t i = 0; i < assets->active_count; i++)
    if (assets->active_textures[i].loader == loader) {
      memmove(assets->active_textures + i, assets->active_textures + i + 1, (assets->active_count - i - 1) * sizeof *assets->active_textures);
      assets->active_count--;
      return;
    }
}
static texture_slot *active_find(qa_app_application_assets *assets, const qa_scene_texture_loader *loader) {
  for (size_t i = 0; i < assets->active_count; i++)
    if (assets->active_textures[i].loader == loader) return &assets->active_textures[i];
  return NULL;
}

/* --- construction --- */

static qa_finish_shader_profile default_shader_profile(void *ctx) { (void)ctx; return qa_default_shader_profile; }

static bool same_owner(qa_renderer_resource_owner a, qa_renderer_resource_owner b) {
  return a.identity == b.identity && a.session.token == b.session.token && a.generation == b.generation;
}

qa_app_application_assets *qa_app_application_assets_create(qa_app_loaded_content *content,
    qa_renderer_resource_owner owner, const qa_media_clock *media_clock,
    const qa_app_application_assets_options *options) {
  if (options != NULL && options->image_registry != NULL && !same_owner(options->image_registry->owner, owner))
    qa_raise("Error", 0, 0, "Application images belong to another renderer");
  qa_app_application_assets *assets = calloc(1, sizeof *assets);
  if (assets == NULL) return NULL;
  assets->content = content;
  assets->catalog = qa_app_loaded_content_catalog(content);
  if (media_clock != NULL) assets->media_clock = *media_clock;
  else { assets->media_clock.ctx = NULL; assets->media_clock.sample = performance_now; }
  if (options != NULL && options->image_registry != NULL) {
    qa_scene_image_registry_fork(options->image_registry, &assets->media_clock, &assets->images);
    assets->forked_images = true;
  } else qa_scene_image_registry_init(&assets->images, owner, &assets->media_clock);
  qa_scene_material_registrations_init(&assets->material_registrations);
  if (options != NULL && options->image_policy != NULL) {
    if (!qa_snapshot_image_policy(*options->image_policy, &assets->image_policy)) qa_range_error("Out of memory");
    assets->has_image_policy = true;
  }
  assets->model_policy = options != NULL && options->model_policy != NULL ? *options->model_policy
      : qa_default_model_replacement_policy;
  assets->warn = options != NULL ? options->warn : NULL;
  assets->warn_ctx = options != NULL ? options->warn_ctx : NULL;
  if (options != NULL && options->shader_profile != NULL) {
    assets->shader_profile = options->shader_profile;
    assets->shader_profile_ctx = options->shader_profile_ctx;
  } else assets->shader_profile = default_shader_profile;
  return assets;
}

qa_app_loaded_content *qa_app_application_assets_content(const qa_app_application_assets *assets) { return assets->content; }
qa_scene_image_registry *qa_app_application_assets_images(qa_app_application_assets *assets) { return &assets->images; }
qa_scene_material_registrations *qa_app_application_assets_material_registrations(qa_app_application_assets *assets) {
  return &assets->material_registrations;
}
void qa_app_application_assets_warn_once(qa_app_application_assets *assets, const char *key, const char *text) {
  for (size_t i = 0; i < assets->warned_count; i++) if (strcmp(assets->warned[i], key) == 0) return;
  GROW(assets->warned, assets->warned_count, assets->warned_cap);
  assets->warned[assets->warned_count++] = xdup(key);
  if (assets->warn != NULL) assets->warn(assets->warn_ctx, text);
}
const qa_scene_image_policy *qa_app_application_assets_image_policy(const qa_app_application_assets *assets) {
  return assets->has_image_policy ? &assets->image_policy.policy : NULL;
}
qa_model_replacement_policy qa_app_application_assets_model_policy(const qa_app_application_assets *assets) { return assets->model_policy; }
void qa_app_application_assets_set_model_policy(qa_app_application_assets *assets, qa_model_replacement_policy policy) {
  assets->model_policy = policy;
}
qa_world_scene *qa_app_application_assets_world(qa_app_application_assets *assets) {
  if (assets->current_world == NULL) qa_raise("Error", 0, 0, "World presentation has not loaded");
  return assets->current_world;
}

/* --- provider --- */

static const qa_catalog_product_expectation *expectation_of(const qa_app_application_assets *assets, const char *content) {
  return &qa_content_catalog_product(assets->catalog, content)->expectation;
}

static void pcx_release(void *arg) { qa_pcx_image_destroy(arg); }

/* TS paletteFor. Returns false for TS null. */
static bool palette_for(qa_content_mounted *mounts, qa_game_family family, qa_palette *out) {
  if (family == QA_GAME_FAMILY_Q3) return false;
  const char *path = family == QA_GAME_FAMILY_Q1 ? "gfx/palette.lmp" : "pics/colormap.pcx";
  qa_content_opened_resource asset;
  if (!qa_content_mounted_open(mounts, path, NULL, NULL, &asset))
    qa_raise("Error", 0, 0, "Selected %s content has no %s", qa_game_family_name(family), path);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, asset.bytes);
  if (family == QA_GAME_FAMILY_Q1) {
    memset(out, 0, sizeof *out);
    if (!qa_decode_palette(asset.bytes, asset.byte_length, *asset.reference, out)) qa_range_error("Out of memory");
  } else {
    qa_pcx_image pcx;
    memset(&pcx, 0, sizeof pcx);
    qa_cleanup pcx_guard;
    qa_cleanup_push(&pcx_guard, pcx_release, &pcx);
    if (!qa_decode_pcx(asset.bytes, asset.byte_length, path, &pcx)) qa_range_error("Out of memory");
    qa_cleanup_pop(&pcx_guard);
    if (pcx.palette == NULL) {
      qa_pcx_image_destroy(&pcx);
      qa_raise("Error", 0, 0, "Selected Quake II colormap has no palette: %s", asset.reference->id);
    }
    out->colors.bytes = pcx.palette;
    out->colors.length = pcx.palette_length;
    out->source = *asset.reference;
    pcx.palette = NULL;
    qa_pcx_image_destroy(&pcx);
  }
  qa_cleanup_pop(&guard);
  free(asset.bytes);
  return true;
}

typedef struct shader_path_set { char **paths; size_t count, cap; } shader_path_set;
static char *lowered(const char *text) {
  char *copy = xdup(text);
  for (char *c = copy; *c; c++) *c = (char)tolower((unsigned char)*c);
  return copy;
}
/* TS add: first name wins under a case-insensitive comparison. */
static void shader_path_add(shader_path_set *set, const char *path) {
  char *key = lowered(path);
  for (size_t i = 0; i < set->count; i++) {
    char *other = lowered(set->paths[i]);
    bool same = strcmp(other, key) == 0;
    free(other);
    if (same) { free(key); return; }
  }
  free(key);
  GROW(set->paths, set->count, set->cap);
  set->paths[set->count++] = xdup(path);
}
static void shader_path_release(void *arg) {
  shader_path_set *set = arg;
  for (size_t i = 0; i < set->count; i++) free(set->paths[i]);
  free(set->paths);
}
/* /^scripts\/[^/]+\.shader$/i */
static bool shader_entry(const char *path) {
  static const char prefix[] = "scripts/";
  for (size_t i = 0; i < 8; i++) if (tolower((unsigned char)path[i]) != prefix[i]) return false;
  const char *name = path + 8;
  size_t length = strlen(name);
  if (length < 8 || strchr(name, '/') != NULL) return false;
  const char *suffix = name + length - 7;
  static const char shader[] = ".shader";
  for (size_t i = 0; i < 7; i++) if (tolower((unsigned char)suffix[i]) != shader[i]) return false;
  return true;
}
/* /\.shader$/i on a directory entry name. */
static bool shader_name(const char *name) {
  size_t length = strlen(name);
  if (length < 7) return false;
  static const char shader[] = ".shader";
  for (size_t i = 0; i < 7; i++) if (tolower((unsigned char)name[length - 7 + i]) != shader[i]) return false;
  return true;
}
static int entry_ordinal_compare(const void *a, const void *b, void *ctx) {
  (void)ctx;
  int64_t left = (*(const qa_catalog_archive_entry *const *)a)->ordinal, right = (*(const qa_catalog_archive_entry *const *)b)->ordinal;
  return left < right ? -1 : left > right ? 1 : 0;
}
static int lowered_name_compare(const void *a, const void *b, void *ctx) {
  (void)ctx;
  char *left = lowered(*(char *const *)a), *right = lowered(*(char *const *)b);
  int order = strcmp(left, right);
  free(left); free(right);
  return order < 0 ? -1 : order > 0 ? 1 : 0;
}
typedef struct name_list { char **names; size_t count, cap; } name_list;
static void name_list_release(void *arg) {
  name_list *list = arg;
  for (size_t i = 0; i < list->count; i++) free(list->names[i]);
  free(list->names);
}
/* TS shaderPaths, FS_ListFilteredFiles order: highest-priority mount
 * first, zip order within an archive, sorted names in a directory;
 * the first name wins. */
static void shader_paths(const qa_app_application_assets *assets, qa_content_mounted *mounts, shader_path_set *out) {
  const qa_resolved_mount_plan *plan = qa_content_mounted_plan(mounts);
  const qa_content_catalog *catalog = assets->catalog;
  for (size_t o = 0; o < plan->default_order_count; o++) {
    const qa_content_mount *mount = NULL;
    for (size_t m = 0; m < plan->mount_count; m++) {
      const qa_mount_identity *identity = plan->mounts[m].kind == QA_CONTENT_MOUNT_ARCHIVE
          ? &plan->mounts[m].as.archive.identity : &plan->mounts[m].as.loose.identity;
      if (strcmp(identity->id, plan->default_order[o]) == 0) mount = &plan->mounts[m];
    }
    if (mount == NULL) continue;
    if (mount->kind == QA_CONTENT_MOUNT_ARCHIVE) {
      /* new Map(path => archive): the last product listing a path wins. */
      const qa_catalog_archive *archive = NULL;
      for (size_t p = 0; p < catalog->product_count; p++)
        for (size_t a = 0; a < catalog->products[p].archive_count; a++)
          if (strcmp(catalog->products[p].archives[a].path, mount->as.archive.archive_path) == 0) archive = &catalog->products[p].archives[a];
      if (archive == NULL) continue;
      const qa_catalog_archive_entry **entries = xcalloc(archive->entry_count, sizeof *entries);
      for (size_t e = 0; e < archive->entry_count; e++) entries[e] = &archive->entries[e];
      qa_js_stable_sort(entries, archive->entry_count, sizeof *entries, entry_ordinal_compare, NULL);
      qa_cleanup guard;
      qa_cleanup_push(&guard, free, entries);
      for (size_t e = 0; e < archive->entry_count; e++) if (shader_entry(entries[e]->path)) shader_path_add(out, entries[e]->path);
      qa_cleanup_pop(&guard);
      free(entries);
      continue;
    }
    const char *root = mount->as.loose.root_path;
    size_t length = strlen(root);
    char *directory = malloc(length + 9);
    if (directory == NULL) qa_range_error("Out of memory");
    memcpy(directory, root, length);
    if (length == 0 || root[length - 1] != '/') directory[length++] = '/';
    memcpy(directory + length, "scripts", 8);
    DIR *dir = opendir(directory);
    if (dir == NULL) {
      int error = errno;
      free(directory);
      if (error == ENOENT) continue;
      qa_raise("Error", error, 0, "%s: %s", strerror(error), root);
    }
    name_list names = {NULL, 0, 0};
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      if (!shader_name(entry->d_name)) continue;
      bool file = entry->d_type == DT_REG;
      if (entry->d_type == DT_UNKNOWN) {
        size_t full = strlen(directory) + strlen(entry->d_name) + 2;
        char *path = malloc(full);
        if (path == NULL) { closedir(dir); free(directory); name_list_release(&names); qa_range_error("Out of memory"); }
        snprintf(path, full, "%s/%s", directory, entry->d_name);
        struct stat status;
        file = lstat(path, &status) == 0 && S_ISREG(status.st_mode);
        free(path);
      }
      if (!file) continue;
      GROW(names.names, names.count, names.cap);
      names.names[names.count++] = xdup(entry->d_name);
    }
    closedir(dir);
    free(directory);
    qa_cleanup guard;
    qa_cleanup_push(&guard, name_list_release, &names);
    qa_js_stable_sort(names.names, names.count, sizeof *names.names, lowered_name_compare, NULL);
    for (size_t i = 0; i < names.count; i++) {
      size_t name_length = strlen(names.names[i]);
      char *path = malloc(name_length + 9);
      if (path == NULL) qa_range_error("Out of memory");
      memcpy(path, "scripts/", 8);
      memcpy(path + 8, names.names[i], name_length + 1);
      shader_path_add(out, path);
      free(path);
    }
    qa_cleanup_pop(&guard);
    name_list_release(&names);
  }
}

typedef struct load_scripts_scope { provider_entry *entry; } load_scripts_scope;
typedef struct script_texts { char **texts; const char **sources; size_t count; } script_texts;
static void script_texts_release(void *ctx) {
  script_texts *files = ctx;
  for (size_t i = 0; i < files->count; i++) free(files->texts[i]);
  free(files->texts); free(files->sources);
  files->texts = NULL; files->sources = NULL; files->count = 0;
}
static void load_scripts(void *ctx) {
  load_scripts_scope *scope = ctx;
  provider_entry *entry = scope->entry;
  shader_path_set set = {NULL, 0, 0};
  qa_cleanup guard;
  qa_cleanup_push(&guard, shader_path_release, &set);
  shader_paths(entry->assets, entry->mounts, &set);
  /* Every script is read first, then added together: addScriptFiles applies id's
     ScanAndLoadShaderFiles precedence across the whole set. */
  script_texts files = {calloc(set.count ? set.count : 1, sizeof(char *)), calloc(set.count ? set.count : 1, sizeof(char *)), 0};
  if (files.texts == NULL || files.sources == NULL) { free(files.texts); free(files.sources); qa_range_error("Out of memory"); }
  qa_cleanup files_guard;
  qa_cleanup_push(&files_guard, script_texts_release, &files);
  for (size_t i = 0; i < set.count; i++) {
    qa_content_opened_resource asset;
    if (!qa_content_mounted_open(entry->mounts, set.paths[i], NULL, NULL, &asset)) continue;
    char *text = malloc(asset.byte_length + 1);
    if (text == NULL) { free(asset.bytes); qa_range_error("Out of memory"); }
    if (asset.byte_length != 0) memcpy(text, asset.bytes, asset.byte_length);
    text[asset.byte_length] = '\0';
    free(asset.bytes);
    files.texts[files.count] = text;
    files.sources[files.count] = set.paths[i];
    files.count++;
  }
  qa_scene_shader_registry_add_script_files(entry->shaders, (const char *const *)files.texts, files.sources, files.count);
  qa_cleanup_pop(&files_guard);
  script_texts_release(&files);
  qa_cleanup_pop(&guard);
  shader_path_release(&set);
}

static bool material_movie(void *ctx, const char *argument, qa_mat_registered_video *out);

static qa_app_provider_scene_assets provider_value(const qa_app_application_assets *assets, const provider_entry *entry) {
  qa_app_provider_scene_assets value;
  value.model_policy = assets->model_policy;
  value.family = entry->family;
  value.mounts = entry->mounts;
  value.palette = entry->has_palette ? &entry->palette : NULL;
  value.shaders = entry->shaders;
  value.textures = entry->shaders->textures;
  return value;
}
static provider_entry *provider_find(qa_app_application_assets *assets, const char *content) {
  for (size_t i = 0; i < assets->provider_count; i++)
    if (strcmp(assets->providers[i]->content, content) == 0) return assets->providers[i];
  return NULL;
}
static void provider_entry_free(provider_entry *entry) {
  if (entry == NULL) return;
  if (entry->shaders != NULL) { qa_scene_shader_registry_destroy(entry->shaders); free(entry->shaders); }
  if (entry->has_palette) qa_palette_destroy(&entry->palette);
  free(entry->content);
  free(entry);
}
static void provider_entry_release(void *arg) { provider_entry_free(arg); }

typedef struct provider_failure { qa_app_application_assets *assets; texture_slot slot; } provider_failure;
static void provider_texture_failure(void *arg) {
  provider_failure *failure = arg;
  active_delete(failure->assets, failure->slot.loader);
  qa_scene_texture_loader_dispose_images(failure->slot.loader);
  texture_slot_destroy(failure->slot);
}

static provider_entry *provider_entry_load(qa_app_application_assets *assets, const char *content) {
  provider_entry *existing = provider_find(assets, content);
  if (existing != NULL) return existing;
  assert_open(assets, "Application assets are closed");
  qa_provider_shader_registrations *registrations = qa_scene_material_registrations_provider(&assets->material_registrations, content);
  provider_entry *entry = xcalloc(1, sizeof *entry);
  qa_cleanup entry_guard;
  qa_cleanup_push(&entry_guard, provider_entry_release, entry);
  entry->content = xdup(content);
  entry->assets = assets;
  entry->family = expectation_of(assets, content)->family;
  entry->mounts = (qa_content_mounted *)qa_app_loaded_content_for_content(assets->content, content);
  entry->has_palette = palette_for(entry->mounts, entry->family, &entry->palette);
  assert_open(assets, "Scene provider loaded after assets closed");
  provider_failure failure;
  failure.assets = assets;
  failure.slot = texture_slot_create(assets, entry->mounts, entry->has_palette ? &entry->palette : NULL,
      assets->has_image_policy ? &assets->image_policy.policy : NULL, false);
  active_add(assets, failure.slot);
  qa_cleanup texture_guard;
  qa_cleanup_push(&texture_guard, provider_texture_failure, &failure);
  qa_finish_shader_profile profile = assets->shader_profile(assets->shader_profile_ctx);
  entry->shaders = xcalloc(1, sizeof *entry->shaders);
  qa_scene_shader_registry_init(entry->shaders, failure.slot.loader, registrations, &profile, material_movie, entry,
      qa_game_family_name(entry->family), NULL);
  load_scripts_scope scope = {entry};
  if (entry->family == QA_GAME_FAMILY_Q3) qa_scene_shader_registry_initialize_source_materials(entry->shaders, load_scripts, &scope);
  else load_scripts(&scope);
  assert_open(assets, "Scene provider loaded after assets closed");
  qa_cleanup_pop(&texture_guard);
  qa_cleanup_pop(&entry_guard);
  GROW(assets->providers, assets->provider_count, assets->provider_cap);
  assets->providers[assets->provider_count++] = entry;
  return entry;
}

qa_app_provider_scene_assets qa_app_application_assets_provider(qa_app_application_assets *assets, qa_content_id content) {
  assert_open(assets, "Application assets are closed");
  return provider_value(assets, provider_entry_load(assets, content));
}

/* The TS ProviderSceneAssets getters: textures reads shaders.textures and
 * modelPolicy the assets' current policy, each time it is read. */
static qa_scene_texture_loader *provider_textures_getter(void *ctx) {
  const provider_entry *entry = ctx;
  return entry->shaders->textures;
}
static qa_model_replacement_policy provider_model_policy_getter(void *ctx) {
  const provider_entry *entry = ctx;
  return entry->assets->model_policy;
}
qa_model_render_provider qa_app_application_assets_render_provider(qa_app_application_assets *assets, qa_content_id content) {
  assert_open(assets, "Application assets are closed");
  provider_entry *entry = provider_entry_load(assets, content);
  qa_model_render_provider provider;
  memset(&provider, 0, sizeof provider);
  provider.has_model_policy = true;
  provider.model_policy = assets->model_policy;
  provider.family = entry->family;
  provider.palette = entry->has_palette ? &entry->palette : NULL;
  provider.textures = entry->shaders->textures;
  provider.shaders = entry->shaders;
  provider.getter_ctx = entry;
  provider.textures_getter = provider_textures_getter;
  provider.model_policy_getter = provider_model_policy_getter;
  return provider;
}

/* --- material movies --- */

static void silent_audio(void *ctx, const qa_cinematic_audio *audio, qa_cinematic_target target) {
  (void)ctx; (void)audio; (void)target;
  qa_raise("Error", 0, 0, "Silent material movie produced audio");
}
static void ignore_reset(void *ctx, qa_cinematic_target target) { (void)ctx; (void)target; }
static void ignore_pause(void *ctx, bool paused, qa_cinematic_target target) { (void)ctx; (void)paused; (void)target; }
static void ignore_complete(void *ctx, qa_cinematic_end_reason reason, qa_cinematic_target target) {
  (void)ctx; (void)reason; (void)target;
}
static qa_renderer_image movie_allocate(void *ctx, int64_t width, int64_t height, bool source_is_resource,
    qa_resolved_resource_reference source_resource, const char *source_name) {
  qa_app_application_assets *assets = ctx;
  qa_renderer_image source;
  memset(&source, 0, sizeof source);
  source.source_is_resource = source_is_resource;
  source.source_resource = source_resource;
  source.source_name = source_name;
  return qa_scene_image_registry_allocate(&assets->images, (double)width, (double)height, source)->image;
}
static void movie_uploaded(void *ctx, const qa_image_resource_operation *operation) {
  qa_app_application_assets *assets = ctx;
  qa_scene_image_registry_commit(&assets->images, *operation);
}
static void movie_entry_free(movie_entry *entry) {
  if (entry == NULL) return;
  if (entry->movie != NULL) { qa_material_cinematic_destroy(entry->movie); free(entry->movie); }
  if (entry->playback != NULL) { qa_cinematic_playback_destroy(entry->playback); free(entry->playback); }
  free(entry->bytes);
  free(entry->content); free(entry->path); free(entry->target_id); free(entry->error_message);
  free(entry);
}

typedef struct dimension_scope { const qa_cinematic_source *source; int64_t width, height; bool decoded; } dimension_scope;
static void dimension_body(void *ctx) {
  dimension_scope *scope = ctx;
  scope->decoded = qa_cinematic_dimensions(scope->source, &scope->width, &scope->height);
}
/* The body of TS materialMovie's pending promise. */
static void movie_load(qa_app_application_assets *assets, provider_entry *provider, movie_entry *entry) {
  qa_content_opened_resource asset;
  bool found = qa_content_mounted_open(provider->mounts, entry->path, NULL, NULL, &asset);
  if (found) entry->bytes = asset.bytes;
  if (assets->closed) qa_raise("Error", 0, 0, "Material movie loaded after assets closed");
  /* A shader's videomap that is absent or cannot be decoded falls back
   * to the default image in the source engines -- never a fatal load. */
  if (!found) return;
  /* path.toLowerCase().split(".").at(-1) */
  const char *dot = strrchr(entry->path, '.');
  const char *last = dot == NULL ? entry->path : dot + 1;
  char extension[4];
  size_t length = strlen(last);
  if (length != 3) return;
  for (size_t i = 0; i < 3; i++) extension[i] = (char)tolower((unsigned char)last[i]);
  extension[3] = '\0';
  qa_cinematic_format format;
  if (strcmp(extension, "roq") == 0) format = QA_CINEMATIC_FORMAT_ROQ;
  else if (strcmp(extension, "cin") == 0) format = QA_CINEMATIC_FORMAT_CIN;
  else if (strcmp(extension, "ogv") == 0) format = QA_CINEMATIC_FORMAT_OGV;
  else return;
  qa_cinematic_source source = qa_cinematic_bytes(format, entry->bytes, asset.byte_length, entry->path);
  dimension_scope dimensions = {&source, 0, 0, false};
  qa_caught_error caught;
  if (!qa_try(dimension_body, &dimensions, &caught) || !dimensions.decoded) {
    if (assets->closed) qa_raise("Error", 0, 0, "Material movie loaded after assets closed");
    return;
  }
  const qa_scene_image_handle *handle = qa_scene_image_registry_allocate(&assets->images,
      (double)dimensions.width, (double)dimensions.height, resource_source(asset.reference));
  entry->playback = xcalloc(1, sizeof *entry->playback);
  qa_cinematic_options options;
  memset(&options, 0, sizeof options);
  options.clock = assets->media_clock;
  options.target.kind = QA_CINEMATIC_TARGET_MATERIAL;
  options.target.id = entry->target_id;
  options.loop = true;
  options.silent = true;
  options.on_audio = silent_audio;
  options.on_audio_reset = ignore_reset;
  options.on_audio_pause = ignore_pause;
  options.on_complete = ignore_complete;
  if (!qa_cinematic_playback_init(entry->playback, &source, &options, NULL)) {
    free(entry->playback); entry->playback = NULL;
    qa_range_error("Out of memory");
  }
  entry->movie = xcalloc(1, sizeof *entry->movie);
  qa_material_cinematic_init(entry->movie, entry->playback, handle->image, movie_allocate, assets, movie_uploaded, assets);
  entry->active = true;
  entry->has_video = true;
  memset(&entry->video, 0, sizeof entry->video);
  entry->video.source = qa_material_cinematic_as_source(entry->movie);
  entry->video.image.frame.image = handle->image;
  entry->video.image.tmu = 0;
}
typedef struct movie_load_scope { qa_app_application_assets *assets; provider_entry *provider; movie_entry *entry; } movie_load_scope;
static void movie_load_body(void *ctx) {
  movie_load_scope *scope = ctx;
  movie_load(scope->assets, scope->provider, scope->entry);
}
/* TS materialMovie(content, mounts, argument): the registry's
 * play-cinematic service. False is TS null. */
static bool material_movie(void *ctx, const char *argument, qa_mat_registered_video *out) {
  provider_entry *provider = ctx;
  qa_app_application_assets *assets = provider->assets;
  assert_open(assets, "Application assets are closed");
  /* argument.slice(0, argument.indexOf("\0")): a C argument ends there. */
  const char *name = argument;
  size_t name_length = strlen(name);
  bool qualified = strchr(name, '/') != NULL || strchr(name, '\\') != NULL;
  char *path = malloc(name_length + 7);
  if (path == NULL) qa_range_error("Out of memory");
  if (qualified) memcpy(path, name, name_length + 1);
  else { memcpy(path, "video/", 6); memcpy(path + 6, name, name_length + 1); }
  for (size_t i = 0; i < assets->movie_count; i++) {
    movie_entry *existing = assets->movies[i];
    if (strcmp(existing->content, provider->content) != 0 || strcmp(existing->path, path) != 0) continue;
    free(path);
    if (existing->failed) qa_raise(existing->error_name, 0, 0, "%s", existing->error_message);
    if (!existing->has_video) return false;
    *out = existing->video;
    return true;
  }
  movie_entry *entry = xcalloc(1, sizeof *entry);
  entry->content = xdup(provider->content);
  entry->path = path;
  size_t content_length = strlen(provider->content), path_length = strlen(path);
  entry->target_id = malloc(content_length + path_length + 2);
  if (entry->target_id == NULL) qa_range_error("Out of memory");
  memcpy(entry->target_id, provider->content, content_length);
  entry->target_id[content_length] = '\0';
  memcpy(entry->target_id + content_length + 1, path, path_length + 1);
  GROW(assets->movies, assets->movie_count, assets->movie_cap);
  assets->movies[assets->movie_count++] = entry;
  movie_load_scope scope = {assets, provider, entry};
  qa_caught_error caught;
  if (!qa_try(movie_load_body, &scope, &caught)) {
    /* The TS movies map keeps the rejected promise. */
    entry->failed = true;
    entry->error_name = caught.name;
    entry->error_message = xdup(caught.message);
    qa_rethrow(&caught);
  }
  if (!entry->has_video) return false;
  *out = entry->video;
  return true;
}

/* --- worlds --- */

static double q2_light_modulate(const qa_app_application_assets *assets, const char *content) {
  return expectation_of(assets, content)->family == QA_GAME_FAMILY_Q2 ? 2 : 1;
}
static qa_decoded_world content_world(qa_app_application_assets *assets) {
  if (assets->has_decoded_world) return assets->decoded_world;
  qa_app_application_world world = qa_app_loaded_content_world(assets->content);
  qa_decoded_world decoded;
  memset(&decoded, 0, sizeof decoded);
  if (world.kind != NULL && strcmp(world.kind, "q1-bsp") == 0) {
    decoded.kind = QA_DECODED_WORLD_Q1; decoded.as.q1 = &((const qa_q1_map *)world.value)->world;
  } else if (world.kind != NULL && strcmp(world.kind, "q2-bsp") == 0) {
    decoded.kind = QA_DECODED_WORLD_Q2; decoded.as.q2 = &((const qa_q2_decoded_map *)world.value)->world;
  } else if (world.kind != NULL && strcmp(world.kind, "q3-bsp") == 0) {
    decoded.kind = QA_DECODED_WORLD_Q3; decoded.as.q3 = world.value;
  } else qa_raise("Error", 0, 0, "Application content has no decoded world");
  assets->decoded_world = decoded;
  assets->has_decoded_world = true;
  return decoded;
}
static const char *world_entities(qa_decoded_world world) {
  return world.kind == QA_DECODED_WORLD_Q1 ? world.as.q1->entities
      : world.kind == QA_DECODED_WORLD_Q2 ? world.as.q2->entities : world.as.q3->entities;
}
static size_t world_model_count(qa_decoded_world world) {
  return world.kind == QA_DECODED_WORLD_Q1 ? world.as.q1->model_count
      : world.kind == QA_DECODED_WORLD_Q2 ? world.as.q2->model_count : world.as.q3->model_count;
}
static void world_scene_free(qa_world_scene *scene) {
  if (scene == NULL) return;
  qa_world_scene_destroy(scene);
  free(scene);
}
typedef struct entities_hold { qa_q3_entity *entities; size_t count; } entities_hold;
static void entities_release(void *arg) {
  entities_hold *hold = arg;
  qa_q3_entities_destroy(hold->entities, hold->count);
}

qa_world_scene *qa_app_application_assets_load_world(qa_app_application_assets *assets) {
  if (assets->current_world != NULL) return assets->current_world;
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(assets->content);
  provider_entry *provider = provider_entry_load(assets, recipe->presentation.assets);
  qa_decoded_world world = content_world(assets);
  const char *text = world_entities(world);
  entities_hold hold = {NULL, 0};
  hold.entities = qa_q3_parse_entities(text != NULL ? text : "", NULL, &hold.count);
  qa_cleanup guard;
  qa_cleanup_push(&guard, entities_release, &hold);
  const qa_q3_entity *worldspawn = NULL;
  for (size_t i = 0; i < hold.count && worldspawn == NULL; i++) {
    const char *classname = qa_q3_entity_get(&hold.entities[i], "classname");
    if (classname != NULL && strcmp(classname, "worldspawn") == 0) worldspawn = &hold.entities[i];
  }
  qa_world_scene_options options;
  memset(&options, 0, sizeof options);
  if (world.kind == QA_DECODED_WORLD_Q2) {
    const char *sky = worldspawn == NULL ? NULL : qa_q3_entity_get(worldspawn, "sky");
    options.q2_sky_name = sky != NULL ? sky : "unit1_";
    options.has_q2_light_modulate = true;
    options.q2_light_modulate = q2_light_modulate(assets, provenance_identity(provenance_of(&recipe->map.geometry))->content);
  }
  qa_world_scene *scene = xcalloc(1, sizeof *scene);
  qa_cleanup scene_guard;
  qa_cleanup_push(&scene_guard, free, scene);
  qa_world_scene_load(world, provider->shaders, &options, scene);
  qa_cleanup_pop(&scene_guard);
  qa_cleanup_pop(&guard);
  qa_q3_entities_destroy(hold.entities, hold.count);
  assets->current_world = scene;
  return scene;
}

/* --- menu fonts --- */

static void font_entry_close(font_entry *entry) {
  if (entry->font != NULL) qa_app_menu_font_close(entry->font);
  entry->font = NULL;
}
static void typography_entry_close(typography_entry *entry) {
  if (entry->typography != NULL) qa_app_menu_typography_close(entry->typography);
  entry->typography = NULL;
}
/* TS loadMenuFont({ catalog, mounts, family, rerelease, images, imagePolicy }). */
static font_entry load_menu_font(qa_app_application_assets *assets, provider_entry *provider, const char *content,
    const qa_scene_image_policy *policy) {
  font_entry entry;
  qa_app_menu_font_options options;
  options.catalog = assets->catalog;
  options.mounts = provider->mounts;
  options.family = provider->family;
  const char *edition = qa_content_catalog_product(assets->catalog, content)->expectation.edition;
  options.rerelease = edition != NULL && strcmp(edition, "rerelease") == 0;
  options.images = &assets->images;
  options.image_policy = policy;
  entry.font = qa_app_load_menu_font(&options);
  return entry;
}
static typography_entry load_menu_typography(qa_app_application_assets *assets, const qa_text_atlas *classic,
    const qa_scene_image_policy *policy) {
  typography_entry entry;
  entry.typography = qa_app_load_menu_typography((qa_content_catalog *)assets->catalog, &assets->images, classic, policy);
  return entry;
}

typedef struct console_font_scope { qa_app_application_assets *assets; } console_font_scope;
static void console_font_body(void *ctx) {
  qa_app_application_assets *assets = ((console_font_scope *)ctx)->assets;
  const char *content = qa_app_loaded_content_recipe(assets->content)->presentation.assets;
  provider_entry *provider = provider_entry_load(assets, content);
  assets->fonts = load_menu_font(assets, provider, content, assets->has_image_policy ? &assets->image_policy.policy : NULL);
  assets->has_font = true;
}
qa_text_font_selection qa_app_application_assets_load_console_font(qa_app_application_assets *assets) {
  if (assets->font_error != NULL) qa_raise(assets->font_error_name, 0, 0, "%s", assets->font_error);
  if (assets->has_font) return qa_app_menu_font_selection(assets->fonts.font);
  console_font_scope scope = {assets};
  qa_caught_error caught;
  if (!qa_try(console_font_body, &scope, &caught)) {
    assets->font_error_name = caught.name;
    assets->font_error = xdup(caught.message);
    qa_rethrow(&caught);
  }
  return qa_app_menu_font_selection(assets->fonts.font);
}
static void typography_body(void *ctx) {
  qa_app_application_assets *assets = ((console_font_scope *)ctx)->assets;
  qa_text_font_selection font = qa_app_application_assets_load_console_font(assets);
  assets->typography = load_menu_typography(assets, font.classic, assets->has_image_policy ? &assets->image_policy.policy : NULL);
  assets->has_typography = true;
}
const qa_app_menu_typography *qa_app_application_assets_load_menu_typography(qa_app_application_assets *assets) {
  if (assets->typography_error != NULL) qa_raise(assets->typography_error_name, 0, 0, "%s", assets->typography_error);
  if (assets->has_typography) return assets->typography.typography;
  console_font_scope scope = {assets};
  qa_caught_error caught;
  if (!qa_try(typography_body, &scope, &caught)) {
    assets->typography_error_name = caught.name;
    assets->typography_error = xdup(caught.message);
    qa_rethrow(&caught);
  }
  return assets->typography.typography;
}

/* --- models --- */

/* TS ApplicationModelProvider over a provider (textures read live, or
 * a staged replacement loader during an image refresh). */
typedef struct model_host {
  qa_app_application_assets *assets;
  qa_scene_texture_loader *textures;
  const char *resource_path; /* requestedPath of the alias being paired */
} model_host;
typedef struct decoded_storage {
  qa_decoded_model model;
  bool md3; qa_decoded_md3_model md3_model; qa_decoded_scene_md3 scene;
  qa_q12_model q12;
} decoded_storage;
static void decoded_storage_free(void *arg) {
  decoded_storage *storage = arg;
  if (storage->md3) { qa_decoded_scene_md3_destroy(&storage->scene); qa_decoded_md3_model_destroy(&storage->md3_model); }
  else qa_q12_model_destroy(&storage->q12);
  free(storage);
}
static bool ends_with_ci(const char *text, const char *suffix) {
  size_t a = strlen(text), b = strlen(suffix);
  if (a < b) return false;
  for (size_t i = 0; i < b; i++)
    if (tolower((unsigned char)text[a - b + i]) != tolower((unsigned char)suffix[i])) return false;
  return true;
}
static void model_load_texture(void *ctx, const char *path, qa_game_family family) {
  model_host *host = ctx;
  qa_scene_texture_load_options options;
  memset(&options, 0, sizeof options);
  options.family = qa_game_family_name(family);
  options.usage = "skin";
  (void)qa_scene_texture_loader_load(host->textures, path, &options);
}
/* path.toLowerCase().endsWith(".md3") ? toSceneMd3(parseMd3(...)) : parseQ12Model(...). */
static const qa_decoded_model *model_decode(void *ctx, const uint8_t *bytes, size_t length, const char *path) {
  model_host *host = ctx;
  decoded_storage *storage = xcalloc(1, sizeof *storage);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, storage);
  if (ends_with_ci(path, ".md3")) {
    qa_parse_md3(bytes, length, path, &storage->md3_model);
    qa_to_scene_md3(&storage->md3_model.model, &storage->scene);
    storage->md3 = true;
    storage->model.kind = QA_DECODED_MODEL_Q3_MESH;
    storage->model.as.q3_mesh = &storage->scene.model;
  } else {
    qa_parse_q12_model(bytes, length, path, &storage->q12);
    switch (storage->q12.kind) {
      case QA_Q12_MODEL_MDL: storage->model.kind = QA_DECODED_MODEL_Q1_ALIAS; storage->model.as.q1_alias = &storage->q12.as.mdl; break;
      case QA_Q12_MODEL_MD2: storage->model.kind = QA_DECODED_MODEL_Q2_ALIAS; storage->model.as.q2_alias = &storage->q12.as.md2; break;
      case QA_Q12_MODEL_SPR: storage->model.kind = QA_DECODED_MODEL_Q1_SPRITE; storage->model.as.q1_sprite = &storage->q12.as.spr; break;
      default: storage->model.kind = QA_DECODED_MODEL_Q2_SPRITE; storage->model.as.q2_sprite = &storage->q12.as.sp2; break;
    }
  }
  qa_cleanup_pop(&guard);
  own(host->assets, decoded_storage_free, storage);
  return &storage->model;
}
/* Storage of one MD5 replacement pair (TS objects the variants retain). */
typedef struct md5_storage {
  char *mesh_text, *anim_text, *scale_text;
  bool has_mesh; qa_md5_mesh_file mesh;
  bool has_anim; qa_md5_animation anim;
  bool has_decoded; qa_decoded_md5_model decoded;
  bool has_selection; qa_md5_skin_selection selection;
  char **diagnostics; size_t diagnostic_count;
  const char **skins; size_t skin_count;
} md5_storage;
static void md5_storage_free(void *arg) {
  md5_storage *storage = arg;
  if (storage->has_decoded) qa_decoded_md5_model_destroy(&storage->decoded);
  if (storage->has_selection) qa_md5_skin_selection_destroy(&storage->selection);
  if (storage->has_anim) qa_md5_animation_destroy(&storage->anim);
  if (storage->has_mesh) qa_md5_mesh_file_destroy(&storage->mesh);
  for (size_t i = 0; i < storage->diagnostic_count; i++) free(storage->diagnostics[i]);
  free(storage->diagnostics);
  free((void *)storage->skins);
  free(storage->mesh_text); free(storage->anim_text); free(storage->scale_text);
  free(storage);
}
static char *text_of(const uint8_t *bytes, size_t length) {
  char *text = malloc(length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  if (length != 0) memcpy(text, bytes, length);
  text[length] = '\0';
  return text;
}
typedef struct md5_scope {
  md5_storage *storage; qa_game_family family;
  const char *mesh_path, *anim_path, *scale_path, *resource_path;
  const qa_decoded_model *alias;
} md5_scope;
static void md5_body(void *ctx) {
  md5_scope *scope = ctx;
  md5_storage *storage = scope->storage;
  if (scope->family == QA_GAME_FAMILY_Q1) {
    qa_parse_md5_mesh(storage->mesh_text, scope->mesh_path, &storage->mesh); storage->has_mesh = true;
    qa_parse_md5_anim(storage->anim_text, scope->anim_path, NULL, &storage->anim); storage->has_anim = true;
    qa_create_md5_model(&storage->mesh, &storage->anim, NULL, &storage->decoded); storage->has_decoded = true;
    qa_q1_replacement_skin_selection(&storage->decoded.model, scope->alias->as.q1_alias, &storage->selection);
    storage->has_selection = true;
    storage->decoded.model.skin_selection = storage->selection;
    return;
  }
  qa_md5_scale_source scale = {scope->scale_path, storage->scale_text};
  qa_parse_md5_anim(storage->anim_text, scope->anim_path, storage->scale_text == NULL ? NULL : &scale, &storage->anim);
  storage->has_anim = true;
  const qa_q2_alias_model *alias = scope->alias->as.q2_alias;
  storage->diagnostic_count = storage->anim.diagnostics.count;
  storage->diagnostics = xcalloc(storage->diagnostic_count + 1, sizeof *storage->diagnostics);
  for (size_t i = 0; i < storage->anim.diagnostics.count; i++) storage->diagnostics[i] = xdup(storage->anim.diagnostics.values[i]);
  if (storage->anim.frame_count < alias->frame_count) {
    char text[4096];
    snprintf(text, sizeof text, "%s has fewer frames than %s (%zu < %zu)", scope->anim_path, scope->resource_path,
        storage->anim.frame_count, alias->frame_count);
    storage->diagnostics[storage->diagnostic_count++] = xdup(text);
  }
  qa_parse_md5_mesh(storage->mesh_text, scope->mesh_path, &storage->mesh); storage->has_mesh = true;
  qa_md2_replacement_skin_selection(alias->skins.values, alias->skins.count, (double)alias->frame_count,
      storage->scale_text == NULL ? NULL : scope->scale_path, (const char *const *)storage->diagnostics,
      storage->diagnostic_count, &storage->selection);
  storage->has_selection = true;
  qa_create_md5_model(&storage->mesh, &storage->anim, &storage->selection, &storage->decoded);
  storage->has_decoded = true;
}
static void md5_storage_release(void *arg) { md5_storage_free(arg); }
static void skin_add(md5_storage *storage, size_t *cap, const char *name) {
  for (size_t i = 0; i < storage->skin_count; i++) if (strcmp(storage->skins[i], name) == 0) return;
  GROW(storage->skins, storage->skin_count, *cap);
  storage->skins[storage->skin_count++] = name;
}
/* The q1Replacement / q2Replacement pair assembly: false is the TS
 * `return null` for a broken retail pair (ModelTextError, RangeError). */
static bool model_md5(void *ctx, qa_game_family family,
    const uint8_t *mesh, size_t mesh_length, const char *mesh_path,
    const uint8_t *anim, size_t anim_length, const char *anim_path,
    const uint8_t *scale, size_t scale_length, const char *scale_path,
    const qa_decoded_model *alias, qa_md5_model *out,
    const char *const **skins_out, size_t *skin_count_out, bool *skins_are_sidecars) {
  model_host *host = ctx;
  md5_storage *storage = xcalloc(1, sizeof *storage);
  qa_cleanup guard;
  qa_cleanup_push(&guard, md5_storage_release, storage);
  storage->mesh_text = text_of(mesh, mesh_length);
  storage->anim_text = text_of(anim, anim_length);
  if (scale != NULL) storage->scale_text = text_of(scale, scale_length);
  md5_scope scope = {storage, family, mesh_path, anim_path, scale_path, host->resource_path, alias};
  qa_caught_error caught;
  if (!qa_try(md5_body, &scope, &caught)) {
    if (strcmp(caught.name, "ModelTextError") != 0 && strcmp(caught.name, "RangeError") != 0) qa_rethrow(&caught);
    qa_cleanup_pop(&guard);
    md5_storage_free(storage);
    return false;
  }
  size_t cap = 0;
  if (family == QA_GAME_FAMILY_Q1) {
    const qa_md5_skin_selection *selection = &storage->decoded.model.skin_selection;
    if (selection->kind == NULL || strcmp(selection->kind, "q1-mdl-replacement") != 0)
      qa_raise("Error", 0, 0, "Q1 replacement lost its alias skin selection");
    for (size_t r = 0; r < selection->mesh_skin_group_row_count; r++)
      for (size_t g = 0; g < selection->mesh_skin_group_row_counts[r]; g++) {
        const qa_timed_frames *group = &selection->mesh_skin_group_rows[r][g];
        if (!group->is_group) skin_add(storage, &cap, group->single_frame);
        else for (size_t f = 0; f < group->frame_count; f++) skin_add(storage, &cap, group->frames[f].frame);
      }
    *skins_are_sidecars = true;
  } else {
    const qa_md5_skin_selection *selection = &storage->decoded.model.skin_selection;
    for (size_t i = 0; i < selection->skins.count; i++) skin_add(storage, &cap, selection->skins.values[i]);
    *skins_are_sidecars = false;
  }
  qa_cleanup_pop(&guard);
  own(host->assets, md5_storage_free, storage);
  *out = storage->decoded.model;
  *skins_out = storage->skins;
  *skin_count_out = storage->skin_count;
  return true;
}
typedef struct qpic_scope { const uint8_t *bytes; size_t length; const char *path; qa_indexed_image image; } qpic_scope;
static void qpic_body(void *ctx) {
  qpic_scope *scope = ctx;
  if (!qa_decode_qpic(scope->bytes, scope->length, scope->path, &scope->image)) qa_range_error("Out of memory");
}
/* decodeQpic: a BinaryError keeps the MDL. */
static bool model_validate_qpic(void *ctx, const uint8_t *bytes, size_t length, const char *path) {
  (void)ctx;
  qpic_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.bytes = bytes; scope.length = length; scope.path = path;
  qa_caught_error caught;
  if (!qa_try(qpic_body, &scope, &caught)) {
    if (strcmp(caught.name, "BinaryError") != 0) qa_rethrow(&caught);
    return false;
  }
  qa_indexed_image_destroy(&scope.image);
  return true;
}
static qa_app_model_provider model_provider(model_host *host, qa_game_family family, qa_content_mounted *mounts) {
  qa_app_model_provider provider;
  memset(&provider, 0, sizeof provider);
  provider.ctx = host;
  provider.family = family;
  provider.mounts = mounts;
  provider.load_texture = model_load_texture;
  provider.decode_model = model_decode;
  provider.md5_replacement = model_md5;
  provider.validate_qpic = model_validate_qpic;
  return provider;
}

static model_entry *model_find(qa_app_application_assets *assets, const char *content, const char *path) {
  for (size_t i = 0; i < assets->model_count; i++)
    if (strcmp(assets->models[i]->content, content) == 0 && strcmp(assets->models[i]->path, path) == 0) return assets->models[i];
  return NULL;
}
static qa_app_model_asset model_value(const qa_app_application_assets *assets, const model_entry *entry) {
  qa_app_model_asset asset = entry->asset;
  provider_entry *provider = NULL;
  for (size_t i = 0; i < assets->provider_count; i++)
    if (strcmp(assets->providers[i]->content, entry->content) == 0) provider = assets->providers[i];
  if (provider != NULL) asset.provider = provider_value(assets, provider);
  if (asset.variants != NULL) asset.model = qa_app_model_variants_model(asset.variants);
  return asset;
}
typedef struct bsp_storage {
  uint8_t *bytes;
  int family;
  qa_q1_map q1; qa_q2_decoded_map q2; qa_q3_decoded_world q3;
} bsp_storage;
static void bsp_storage_free(void *arg) {
  bsp_storage *storage = arg;
  if (storage->family == 1) qa_q1_map_destroy(&storage->q1);
  else if (storage->family == 2) qa_q2_decoded_map_destroy(&storage->q2);
  else if (storage->family == 3) qa_q3_decoded_world_destroy(&storage->q3);
  free(storage->bytes);
  free(storage);
}
static void bsp_storage_release(void *arg) { bsp_storage_free(arg); }
static void variants_release(void *arg) { qa_app_model_variants_destroy(arg); }

qa_app_model_asset qa_app_application_assets_model(qa_app_application_assets *assets, qa_content_id content, const char *path) {
  model_entry *existing = model_find(assets, content, path);
  if (existing != NULL) return model_value(assets, existing);
  provider_entry *provider = provider_entry_load(assets, content);
  qa_app_model_asset asset;
  memset(&asset, 0, sizeof asset);
  asset.provider = provider_value(assets, provider);
  if (path[0] == '*') {
    double number = qa_js_string_to_number(path + 1);
    qa_decoded_world world = content_world(assets);
    bool safe = number == trunc(number) && fabs(number) <= 9007199254740991.0;
    if (!safe || number < 0 || number >= (double)world_model_count(world))
      qa_raise("Error", 0, 0, "Missing inline model %s", path);
    asset.resource = qa_app_loaded_content_recipe(assets->content)->map.geometry;
    asset.model.kind = QA_DECODED_MODEL_BRUSH_MODEL;
    asset.model.brush_world = world;
    asset.model.brush_model_index = number;
    asset.brush_scene = qa_app_application_assets_world(assets);
  } else {
    qa_content_opened_resource opened;
    if (!qa_content_mounted_open(provider->mounts, path, NULL, NULL, &opened))
      qa_raise("Error", 0, 0, "Model is absent from selected content: %s/%s", content, path);
    if (ends_with_ci(path, ".bsp")) {
      bsp_storage *storage = xcalloc(1, sizeof *storage);
      storage->bytes = opened.bytes;
      qa_cleanup guard;
      qa_cleanup_push(&guard, bsp_storage_release, storage);
      const char *format = qa_classify_bsp(opened.bytes, opened.byte_length, path);
      qa_decoded_world world;
      memset(&world, 0, sizeof world);
      if (strcmp(format, "q1") == 0) {
        qa_q1_map_options options;
        memset(&options, 0, sizeof options);
        options.source = path;
        qa_q1_read_bsp(opened.bytes, opened.byte_length, &options, &storage->q1);
        storage->family = 1;
        world.kind = QA_DECODED_WORLD_Q1; world.as.q1 = &storage->q1.world;
      } else if (strcmp(format, "q2") == 0) {
        qa_q2_bsp bsp;
        memset(&bsp, 0, sizeof bsp);
        qa_read_q2_bsp(opened.bytes, opened.byte_length, path, &bsp);
        qa_to_q2_world_geometry(&bsp, NULL, &storage->q2);
        storage->family = 2;
        world.kind = QA_DECODED_WORLD_Q2; world.as.q2 = &storage->q2.world;
      } else {
        qa_q3_decode_world(opened.bytes, opened.byte_length, path, &storage->q3);
        storage->family = 3;
        world.kind = QA_DECODED_WORLD_Q3; world.as.q3 = &storage->q3.world;
      }
      qa_world_scene_options options;
      memset(&options, 0, sizeof options);
      if (world.kind == QA_DECODED_WORLD_Q2) {
        options.has_q2_light_modulate = true;
        options.q2_light_modulate = q2_light_modulate(assets, provenance_identity(provenance_of(opened.reference))->content);
      }
      qa_world_scene *scene = xcalloc(1, sizeof *scene);
      qa_cleanup scene_guard;
      qa_cleanup_push(&scene_guard, free, scene);
      qa_world_scene_load(world, provider->shaders, &options, scene);
      qa_cleanup_pop(&scene_guard);
      qa_cleanup_pop(&guard);
      own(assets, bsp_storage_free, storage);
      GROW(assets->brush_scenes, assets->brush_count, assets->brush_cap);
      assets->brush_scenes[assets->brush_count++] = scene;
      asset.resource = *opened.reference;
      asset.model.kind = QA_DECODED_MODEL_BRUSH_MODEL;
      asset.model.brush_world = world;
      asset.model.brush_model_index = 0;
      asset.brush_scene = scene;
    } else {
      qa_cleanup guard;
      qa_cleanup_push(&guard, free, opened.bytes);
      model_host host = {assets, provider->shaders->textures, opened.reference->requested_path};
      qa_app_model_provider loader = model_provider(&host, provider->family, provider->mounts);
      qa_app_loaded_model loaded = qa_app_load_application_model(&loader, &opened,
          qa_load_model_replacement(provider->family, assets->model_policy));
      qa_cleanup_pop(&guard);
      free(opened.bytes);
      asset.resource = loaded.resource;
      asset.model = loaded.model;
      asset.variants = loaded.variants;
      if (loaded.variants != NULL) {
        own(assets, variants_release, loaded.variants);
        GROW(assets->variants, assets->variant_count, assets->variant_cap);
        assets->variants[assets->variant_count].variants = loaded.variants;
        assets->variants[assets->variant_count].content = xdup(content);
        assets->variants[assets->variant_count].resource_path = loaded.resource.requested_path;
        assets->variant_count++;
      }
    }
  }
  model_entry *entry = xcalloc(1, sizeof *entry);
  entry->content = xdup(content);
  entry->path = xdup(path);
  entry->asset = asset;
  GROW(assets->models, assets->model_count, assets->model_cap);
  assets->models[assets->model_count++] = entry;
  return model_value(assets, entry);
}

/* --- image refresh --- */

typedef struct replacement_row {
  provider_entry *provider;
  texture_slot slot;
  qa_scene_shader_registry *shaders;
  bool staged; /* shaders initialized as a replacement */
} replacement_row;
typedef struct world_row {
  qa_world_scene *current, *replacement;
  qa_scene_shader_registry *original;
  bool loaded;
} world_row;
struct qa_app_prepared_images {
  qa_app_application_assets *assets;
  qa_scene_image_policy_owned policy;
  qa_model_replacement_policy model_policy;
  replacement_row **rows; size_t row_count, row_cap;
  world_row *worlds; size_t world_count, world_cap;
  qa_app_model_replacement_commit **models; size_t model_count, model_cap;
  bool has_remaps; qa_prepared_remap_refresh remaps;
  bool has_fonts; font_entry fonts;
  bool has_typography; typography_entry typography;
};

static replacement_row *row_for_shaders(qa_app_prepared_images *prepared, const qa_scene_shader_registry *shaders) {
  for (size_t i = 0; i < prepared->row_count; i++)
    if (prepared->rows[i]->provider->shaders == shaders) return prepared->rows[i];
  return NULL;
}
/* TS stagedProvider(content). */
static replacement_row *staged_row(qa_app_prepared_images *prepared, const char *content) {
  for (size_t i = 0; i < prepared->row_count; i++)
    if (strcmp(prepared->rows[i]->provider->content, content) == 0) return prepared->rows[i];
  qa_app_application_assets *assets = prepared->assets;
  provider_entry *provider = provider_entry_load(assets, content);
  replacement_row *row = xcalloc(1, sizeof *row);
  row->provider = provider;
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, row);
  row->slot = texture_slot_create(assets, provider->mounts, provider->has_palette ? &provider->palette : NULL,
      &prepared->policy.policy, true);
  row->shaders = xcalloc(1, sizeof *row->shaders);
  qa_scene_shader_registry_replacement(provider->shaders, row->slot.loader, row->shaders);
  row->staged = true;
  qa_cleanup_pop(&guard);
  GROW(prepared->rows, prepared->row_count, prepared->row_cap);
  prepared->rows[prepared->row_count++] = row;
  qa_scene_texture_loader_prepare_replacement(provider->shaders->textures, row->slot.loader);
  qa_scene_shader_registry_prepare_replacement(provider->shaders, row->shaders);
  return row;
}
static qa_app_provider_scene_assets staged_value(qa_app_prepared_images *prepared, replacement_row *row) {
  qa_app_provider_scene_assets value = provider_value(prepared->assets, row->provider);
  value.textures = row->slot.loader;
  value.shaders = row->shaders;
  return value;
}
qa_app_provider_scene_assets qa_app_prepared_images_provider(qa_app_prepared_images *prepared, qa_content_id content) {
  return staged_value(prepared, staged_row(prepared, content));
}
static qa_scene_shader_registry *remap_source_for(void *ctx, qa_scene_shader_registry *source) {
  replacement_row *row = row_for_shaders(ctx, source);
  if (row == NULL) qa_raise("Error", 0, 0, "Remap image provider is absent");
  return row->shaders;
}

/* Frees what the prepared refresh still owns; the TS catch block and
 * discard closure run discard_resources first. */
static void prepared_free(qa_app_prepared_images *prepared) {
  for (size_t i = 0; i < prepared->model_count; i++) qa_app_model_replacement_commit_destroy(prepared->models[i]);
  free(prepared->models);
  for (size_t i = 0; i < prepared->world_count; i++) world_scene_free(prepared->worlds[i].replacement);
  free(prepared->worlds);
  for (size_t i = 0; i < prepared->row_count; i++) {
    replacement_row *row = prepared->rows[i];
    if (row->shaders != NULL) { qa_scene_shader_registry_destroy(row->shaders); free(row->shaders); }
    texture_slot_destroy(row->slot);
    free(row);
  }
  free(prepared->rows);
  if (prepared->has_fonts) font_entry_close(&prepared->fonts);
  if (prepared->has_typography) typography_entry_close(&prepared->typography);
  qa_scene_image_policy_owned_destroy(&prepared->policy);
  free(prepared);
}
static void discard_resources(qa_app_prepared_images *prepared) {
  if (prepared->has_typography) { typography_entry_close(&prepared->typography); prepared->has_typography = false; }
  if (prepared->has_fonts) { font_entry_close(&prepared->fonts); prepared->has_fonts = false; }
  for (size_t i = 0; i < prepared->world_count; i++)
    if (prepared->worlds[i].loaded) qa_world_scene_close(prepared->worlds[i].replacement);
  for (size_t i = 0; i < prepared->row_count; i++) {
    replacement_row *row = prepared->rows[i];
    if (row->staged) qa_scene_shader_registry_discard_replacement(row->shaders);
    qa_scene_texture_loader_dispose_images(row->slot.loader);
  }
}
static void prepared_failure(void *arg) {
  qa_app_prepared_images *prepared = arg;
  discard_resources(prepared);
  prepared_free(prepared);
}

qa_app_prepared_images *qa_app_application_assets_prepare_image_refresh(qa_app_application_assets *assets,
    const qa_scene_image_policy *policy, const qa_model_replacement_policy *model_policy) {
  assert_open(assets, "Application assets are closed");
  if (assets->retired_count != 0) qa_raise("Error", 0, 0, "Previous image refresh has not finished rebinding");
  qa_app_prepared_images *prepared = xcalloc(1, sizeof *prepared);
  prepared->assets = assets;
  if (!qa_snapshot_image_policy(*policy, &prepared->policy)) { free(prepared); qa_range_error("Out of memory"); }
  prepared->model_policy = model_policy != NULL ? *model_policy : assets->model_policy;
  qa_cleanup guard;
  qa_cleanup_push(&guard, prepared_failure, prepared);
  /* for (const content of this.providers.keys()): a snapshot of the keys
   * present now; staging loads no new providers. */
  size_t provider_count = assets->provider_count;
  for (size_t i = 0; i < provider_count; i++) staged_row(prepared, assets->providers[i]->content);
  if (prepared->model_policy.q1_enhanced != assets->model_policy.q1_enhanced
      || prepared->model_policy.q2_load != assets->model_policy.q2_load) {
    for (size_t i = 0; i < assets->variant_count; i++) {
      variant_entry *variant = &assets->variants[i];
      replacement_row *row = staged_row(prepared, variant->content);
      qa_game_family family = row->provider->family;
      bool next = qa_load_model_replacement(family, prepared->model_policy);
      if (next == qa_load_model_replacement(family, assets->model_policy)) continue;
      model_host host = {assets, row->slot.loader, variant->resource_path};
      qa_app_model_provider provider = model_provider(&host, family, row->provider->mounts);
      GROW(prepared->models, prepared->model_count, prepared->model_cap);
      prepared->models[prepared->model_count++] = qa_app_model_variants_prepare_replacement(variant->variants, &provider, next);
    }
  }
  size_t scene_count = (assets->current_world != NULL ? 1 : 0) + assets->brush_count;
  for (size_t i = 0; i < scene_count; i++) {
    qa_world_scene *current = assets->current_world != NULL
        ? (i == 0 ? assets->current_world : assets->brush_scenes[i - 1]) : assets->brush_scenes[i];
    replacement_row *row = row_for_shaders(prepared, current->shaders);
    if (row == NULL) qa_raise("Error", 0, 0, "World image provider is absent");
    GROW(prepared->worlds, prepared->world_count, prepared->world_cap);
    world_row *world = &prepared->worlds[prepared->world_count++];
    world->current = current;
    world->original = current->shaders;
    world->replacement = xcalloc(1, sizeof *world->replacement);
    world->loaded = false;
    qa_world_scene_prepare_images(current, row->shaders, remap_source_for, prepared, world->replacement);
    world->loaded = true;
  }
  {
    qa_remap_refresh_source *sources = xcalloc(prepared->row_count, sizeof *sources);
    for (size_t i = 0; i < prepared->row_count; i++) {
      sources[i].old_source = qa_scene_shader_registry_remap_source(prepared->rows[i]->provider->shaders);
      sources[i].new_source = qa_scene_shader_registry_remap_source(prepared->rows[i]->shaders);
    }
    qa_cleanup sources_guard;
    qa_cleanup_push(&sources_guard, free, sources);
    qa_scene_material_registrations_prepare_remap_refresh(&assets->material_registrations, sources, prepared->row_count, &prepared->remaps);
    qa_cleanup_pop(&sources_guard);
    free(sources);
    prepared->has_remaps = true;
  }
  {
    const char *content = qa_app_loaded_content_recipe(assets->content)->presentation.assets;
    provider_entry *provider = provider_entry_load(assets, content);
    prepared->fonts = load_menu_font(assets, provider, content, &prepared->policy.policy);
    prepared->has_fonts = true;
    prepared->typography = load_menu_typography(assets, qa_app_menu_font_selection(prepared->fonts.font).classic,
        &prepared->policy.policy);
    prepared->has_typography = true;
  }
  qa_cleanup_pop(&guard);
  return prepared;
}
qa_text_font_selection qa_app_prepared_images_font(const qa_app_prepared_images *prepared) {
  return qa_app_menu_font_selection(prepared->fonts.font);
}
const qa_app_menu_typography *qa_app_prepared_images_typography(const qa_app_prepared_images *prepared) {
  return prepared->typography.typography;
}
const qa_scene_image_policy *qa_app_prepared_images_policy(const qa_app_prepared_images *prepared) {
  return &prepared->policy.policy;
}

static void retire(qa_app_application_assets *assets, retired_entry entry) {
  GROW(assets->retired, assets->retired_count, assets->retired_cap);
  assets->retired[assets->retired_count++] = entry;
}

void qa_app_prepared_images_commit(qa_app_prepared_images *prepared) {
  qa_app_application_assets *assets = prepared->assets;
  qa_prepared_remap_refresh_validate(&prepared->remaps);
  for (size_t i = 0; i < prepared->world_count; i++)
    qa_world_scene_validate_images(prepared->worlds[i].current, prepared->worlds[i].replacement);
  for (size_t i = 0; i < prepared->row_count; i++)
    qa_scene_shader_registry_validate_replacement(prepared->rows[i]->provider->shaders, prepared->rows[i]->shaders);
  for (size_t i = 0; i < prepared->model_count; i++) qa_app_model_replacement_commit_apply(prepared->models[i]);
  for (size_t i = 0; i < prepared->row_count; i++) {
    replacement_row *row = prepared->rows[i];
    qa_scene_texture_loader *previous = row->provider->shaders->textures;
    qa_scene_shader_registry_commit_replacement(row->provider->shaders, row->shaders);
    row->staged = false;
    active_add(assets, row->slot);
    texture_slot *slot = active_find(assets, previous);
    retired_entry entry;
    memset(&entry, 0, sizeof entry);
    entry.kind = RETIRED_TEXTURES;
    if (slot != NULL) entry.textures = *slot;
    else entry.textures.loader = previous;
    retire(assets, entry);
    row->slot.loader = NULL; row->slot.reader = NULL;
  }
  for (size_t i = 0; i < prepared->world_count; i++) {
    world_row *world = &prepared->worlds[i];
    qa_world_scene_commit_images(world->current, world->replacement);
    /* TS commitImages keeps the scene's own registry; the C commit
     * adopts the staging registry this refresh frees below. */
    world->current->shaders = world->original;
  }
  qa_prepared_remap_refresh_commit(&prepared->remaps);
  if (assets->has_font) {
    retired_entry entry;
    memset(&entry, 0, sizeof entry);
    entry.kind = RETIRED_FONT; entry.font = assets->fonts;
    retire(assets, entry);
  }
  if (assets->has_typography) {
    retired_entry entry;
    memset(&entry, 0, sizeof entry);
    entry.kind = RETIRED_TYPOGRAPHY; entry.typography = assets->typography;
    retire(assets, entry);
  }
  free(assets->font_error); assets->font_error = NULL;
  free(assets->typography_error); assets->typography_error = NULL;
  assets->fonts = prepared->fonts; assets->has_font = true;
  assets->typography = prepared->typography; assets->has_typography = true;
  prepared->has_fonts = false; prepared->has_typography = false;
  if (assets->has_image_policy) qa_scene_image_policy_owned_destroy(&assets->image_policy);
  assets->image_policy = prepared->policy;
  assets->has_image_policy = true;
  memset(&prepared->policy, 0, sizeof prepared->policy);
  assets->model_policy = prepared->model_policy;
  prepared_free(prepared);
}
void qa_app_prepared_images_discard(qa_app_prepared_images *prepared) {
  discard_resources(prepared);
  prepared_free(prepared);
}

static void run_retired(retired_entry *entry, qa_app_application_assets *assets) {
  if (entry->kind == RETIRED_TEXTURES) {
    qa_scene_texture_loader_dispose_images(entry->textures.loader);
    active_delete(assets, entry->textures.loader);
    if (entry->textures.reader != NULL) texture_slot_destroy(entry->textures);
  } else if (entry->kind == RETIRED_FONT) font_entry_close(&entry->font);
  else typography_entry_close(&entry->typography);
}
void qa_app_application_assets_finish_image_refresh(qa_app_application_assets *assets) {
  retired_entry *entries = assets->retired;
  size_t count = assets->retired_count;
  assets->retired = NULL; assets->retired_count = 0; assets->retired_cap = 0;
  for (size_t i = 0; i < count; i++) run_retired(&entries[i], assets);
  free(entries);
}

/* --- close --- */

void qa_app_application_assets_close(qa_app_application_assets *assets) {
  if (assets == NULL || assets->closed) return;
  assets->closed = true;
  qa_app_application_assets_finish_image_refresh(assets);
  for (size_t i = 0; i < assets->active_count; i++) qa_scene_texture_loader_close(assets->active_textures[i].loader);
  for (size_t i = 0; i < assets->movie_count; i++) {
    movie_entry *entry = assets->movies[i];
    if (entry->active) { qa_image_resource_operation release; (void)qa_material_cinematic_close(entry->movie, &release); entry->active = false; }
  }
  if (assets->has_typography) { typography_entry_close(&assets->typography); assets->has_typography = false; }
  if (assets->has_font) { font_entry_close(&assets->fonts); assets->has_font = false; }
  free(assets->font_error); assets->font_error = NULL;
  free(assets->typography_error); assets->typography_error = NULL;
  if (assets->current_world != NULL) { qa_world_scene_close(assets->current_world); world_scene_free(assets->current_world); }
  assets->current_world = NULL;
  for (size_t i = 0; i < assets->brush_count; i++) { qa_world_scene_close(assets->brush_scenes[i]); world_scene_free(assets->brush_scenes[i]); }
  assets->brush_count = 0;
  qa_scene_image_registry_close(&assets->images);
  for (size_t i = 0; i < assets->movie_count; i++) movie_entry_free(assets->movies[i]);
  assets->movie_count = 0;
  for (size_t i = 0; i < assets->model_count; i++) { free(assets->models[i]->content); free(assets->models[i]->path); free(assets->models[i]); }
  assets->model_count = 0;
  for (size_t i = 0; i < assets->variant_count; i++) free(assets->variants[i].content);
  assets->variant_count = 0;
  for (size_t i = 0; i < assets->provider_count; i++) provider_entry_free(assets->providers[i]);
  assets->provider_count = 0;
  for (size_t i = 0; i < assets->active_count; i++) texture_slot_destroy(assets->active_textures[i]);
  assets->active_count = 0;
}

void qa_app_application_assets_destroy(qa_app_application_assets *assets) {
  if (assets == NULL) return;
  qa_app_application_assets_close(assets);
  for (size_t i = assets->owned_count; i > 0; i--) assets->owned[i - 1].fn(assets->owned[i - 1].arg);
  free(assets->owned);
  free(assets->movies); free(assets->models); free(assets->variants); free(assets->providers);
  free(assets->brush_scenes); free(assets->active_textures); free(assets->retired);
  for (size_t i = 0; i < assets->warned_count; i++) free(assets->warned[i]);
  free(assets->warned);
  if (assets->has_image_policy) qa_scene_image_policy_owned_destroy(&assets->image_policy);
  qa_scene_material_registrations_destroy(&assets->material_registrations);
  /* A fork shares its parent's lifetime record, which the parent frees. */
  if (assets->forked_images) assets->images._lifetime = NULL;
  qa_scene_image_registry_destroy(&assets->images);
  free(assets);
}

/* ---- TS createWorldAssets ---- */
static qa_finish_shader_profile world_shader_profile(void *ctx) {
  return qa_app_shader_profile_from_cvars(ctx);
}

qa_app_application_assets *qa_app_create_world_assets(qa_app_loaded_content *content,
    qa_renderer_resource_owner owner, qa_scene_image_registry *images,
    void (*warn)(void *ctx, const char *text), void *warn_ctx, const qa_app_world_asset_images *world_images) {
  qa_app_application_assets_options options;
  memset(&options, 0, sizeof options);
  options.image_registry = images;
  options.warn = warn; options.warn_ctx = warn_ctx;
  if (world_images != NULL) {
    options.image_policy = &world_images->policy;
    options.model_policy = &world_images->model_policy;
    options.shader_profile = world_shader_profile; options.shader_profile_ctx = world_images->cvars;
  }
  return qa_app_application_assets_create(content, owner, NULL, &options);
}

const qa_content_catalog *qa_app_application_assets_catalog(const qa_app_application_assets *assets) { return assets->catalog; }
struct qa_app_world_font { font_entry entry; };
qa_app_world_font *qa_app_application_assets_load_world_font(qa_app_application_assets *assets, qa_content_id content,
                                                             const qa_scene_image_policy *policy) {
  provider_entry *provider = provider_entry_load(assets, content);
  qa_app_world_font *font = xcalloc(1, sizeof *font);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, font);
  font->entry = load_menu_font(assets, provider, content, policy);
  qa_cleanup_pop(&guard);
  return font;
}
qa_text_font_selection qa_app_world_font_selection(const qa_app_world_font *font) {
  return qa_app_menu_font_selection(font->entry.font);
}
void qa_app_world_font_close(qa_app_world_font *font) {
  if (font == NULL) return;
  font_entry_close(&font->entry);
  free(font);
}
