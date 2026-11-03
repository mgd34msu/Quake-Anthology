/* Port of src/app/bootstrap/q3-client/assets.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/audio/wav.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>
#include "qa/audio/bank.h"
#include "qa/content/catalog/index.h"
#include "qa/render/scene/material_registrations.h"
#include "qa/render/scene/shaders.h"
#include "qa/render/scene/world.h"
#include "qa/audio/streams.h"
#include "qa/content/mounts/paths.h"
#include "qa/formats/q3_model/md3.h"
#include "qa/text/mounted.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
typedef struct retained_entry { char *key; uint8_t *bytes; size_t length; qa_resource_id resource; } retained_entry;
typedef struct sound_entry { char *path; qa_sound_asset sound; } sound_entry;
typedef struct model_entry {
  qa_q3_pres_scene_model_kind kind;
  char *path;
  qa_resource_id resource_id;
  qa_app_q3_provider_scene_assets provider;
  qa_content_id content;
  char *content_copy;
} model_entry;
struct qa_app_application_q3_assets {
  qa_app_q3_application_assets_host host;
  qa_content_id content;
  char *content_copy;
  qa_app_q3_provider_scene_assets provider;
  void (*print)(void *ctx, const char *text); void *print_ctx;
  const char *scope;
  bool closed;
  char **names; size_t name_count, name_capacity;
  retained_entry *retained; size_t retained_count, retained_capacity;
  sound_entry *sounds; size_t sound_count, sound_capacity;
  model_entry *models; size_t model_count, model_capacity;
  qa_q3_pres_presentation_sound_bank *bank;
  qa_sound_bank *sound_bank;
  qa_mounted_font_reader font_reader;
  qa_mounted_content font_content;
  qa_q3_renderer_font_registry fonts;
  qa_q3_ui_asset_registry font_registry;
  bool fonts_init;
  /* TS registry pictures are fresh objects per registration: each shader
   * reply keeps its own identity (and name copy) for the assets' lifetime. */
  qa_material_picture **pictures; size_t picture_count, picture_capacity;
  bool owns_host; /* create_from_application built host */
  void *font_write; /* font generation services context (font_write_ctx), owned */
  /* Parsed skins stay valid for the assets' lifetime (TS values). */
  qa_q3_pres_scene_skin **skins; size_t skin_count, skin_capacity;
};
static char *xstrdup(const char *s) { char *p = strdup(s != NULL ? s : ""); if (p == NULL) qa_range_error("Out of memory"); return p; }
/* TS path.toLowerCase(): Unicode lowering of UTF-8 paths. */
static char *lowered_copy(const char *s) {
  char *out = qa_js_to_lower_case(s != NULL ? s : "");
  if (out == NULL) qa_range_error("Out of memory");
  return out;
}
static bool ends_with_ci(const char *v, const char *suffix) {
  size_t a = strlen(v), b = strlen(suffix);
  if (a < b) return false;
  for (size_t i = 0; i < b; i++)
    if (tolower((unsigned char)v[a - b + i]) != suffix[i]) return false;
  return true;
}
/* TS normalizeResourcePath RangeError conditions as a predicate. */
static bool resource_path_valid(const char *value) {
  if (value[0] == '\0') return false;
  if (isalpha((unsigned char)value[0]) && value[1] == ':') return false;
  const char *part = value;
  for (;;) {
    const char *slash = strchr(part, '/');
    size_t len = slash != NULL ? (size_t)(slash - part) : strlen(part);
    if (len == 0) return false;
    if (len == 1 && part[0] == '.') return false;
    if (len == 2 && part[0] == '.' && part[1] == '.') return false;
    if (slash == NULL) return true;
    part = slash + 1;
  }
}
static char *normalized_or_null(const char *path) {
  char *copy = xstrdup(path);
  for (char *p = copy; *p != '\0'; p++) if (*p == '\\') *p = '/';
  if (!resource_path_valid(copy)) { free(copy); return NULL; }
  return copy;
}
bool qa_app_register_q3_model_request(const char *path,
    bool (*load)(void *ctx, const char *path, qa_q3_pres_scene_model *out), void *load_ctx,
    qa_q3_pres_scene_model *out) {
  const char *effective = path;
  char *normalized = NULL;
  if (!strncmp(path, "*", 1) == 0 && path[0] != '*') {
    normalized = normalized_or_null(path);
    if (normalized == NULL) { *out = qa_q3_pres_default_model; return true; }
    effective = normalized;
  }
  bool ok = load(load_ctx, effective, out);
  free(normalized);
  return ok;
}
bool qa_app_register_q3_shader_request(const char *path,
    bool (*load)(void *ctx, const char *path, void *out), void *load_ctx, void *out) {
  char *normalized = normalized_or_null(path);
  if (normalized == NULL) return false;
  bool ok = load(load_ctx, normalized, out);
  free(normalized);
  return ok;
}
static void names_add(qa_app_application_q3_assets *a, const char *lowered) {
  for (size_t i = 0; i < a->name_count; i++) if (strcmp(a->names[i], lowered) == 0) return;
  if (a->name_count == a->name_capacity) {
    size_t cap = a->name_capacity == 0 ? 64 : a->name_capacity * 2;
    char **next = realloc(a->names, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    a->names = next; a->name_capacity = cap;
  }
  a->names[a->name_count++] = xstrdup(lowered);
}
static void walk_loose(qa_app_application_q3_assets *a, const char *root, const char *dir) {
  DIR *stream = opendir(dir);
  if (stream == NULL) return; /* TS ENOENT -> [] (other errors rethrow; opendir cannot split them portably) */
  struct dirent *entry;
  while ((entry = readdir(stream)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    size_t len = strlen(dir) + 1 + strlen(entry->d_name) + 1;
    char *full = malloc(len);
    if (full == NULL) qa_range_error("Out of memory");
    snprintf(full, len, "%s/%s", dir, entry->d_name);
    struct stat st;
    if (stat(full, &st) == 0) {
      if (S_ISDIR(st.st_mode)) walk_loose(a, root, full);
      else if (S_ISREG(st.st_mode)) {
        size_t root_len = strlen(root);
        const char *rel = full + root_len;
        if (*rel == '/') rel++;
        char *lowered = lowered_copy(rel);
        names_add(a, lowered);
        free(lowered);
      }
    }
    free(full);
  }
  closedir(stream);
}
static bool font_open_bridge(void *ctx, const char *path, qa_opened_resource *out) {
  qa_content_mounted *mounts = ctx;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(mounts, path, NULL, NULL, &opened)) return false;
  out->bytes.bytes = opened.bytes;
  out->bytes.length = opened.byte_length;
  if (opened.reference != NULL) out->reference = *opened.reference;
  return true;
}
static bool font_picture_bridge(void *ctx, const char *path, qa_material_picture *out) {
  qa_app_application_q3_assets *a = ctx;
  return a->provider.register_picture(a->provider.ctx, path, out);
}
static void font_print_bridge(void *ctx, const char *text) {
  qa_app_application_q3_assets *a = ctx;
  a->print(a->print_ctx, text);
}
static bool font_asset_picture_bridge(void *ctx, const char *path, const char *mode, qa_picture_asset *out) {
  (void)mode;
  qa_app_application_q3_assets *a = ctx;
  qa_material_picture picture;
  if (!a->provider.register_picture(a->provider.ctx, path, &picture)) return false;
  out->kind = QA_PICTURE_MATERIAL;
  out->material = picture;
  return true;
}
static const void *font_asset_font_bridge(void *ctx, const char *path, double size,
                                          qa_font_print_fn print, void *print_ctx) {
  qa_app_application_q3_assets *a = ctx;
  return qa_q3_renderer_font_registry_register_font(&a->fonts, path, size, print, print_ctx, NULL, NULL);
}
typedef struct font_write_ctx {
  qa_app_application_q3_assets *assets; const char *root;
  bool (*save_font_data)(void *ctx); void *save_ctx;
} font_write_ctx;
static bool font_image_bridge(void *ctx, const char *name, qa_byte_view rgba, qa_material_picture *out) {
  font_write_ctx *w = ctx;
  qa_app_application_q3_assets *a = w->assets;
  return a->provider.register_generated_picture(a->provider.ctx, name, rgba, out);
}
static bool font_save_bridge(void *ctx) {
  font_write_ctx *w = ctx;
  return w->save_font_data(w->save_ctx);
}
static void font_write_file(void *ctx, const char *name, qa_byte_view bytes) {
  font_write_ctx *w = ctx;
  if (w->root == NULL) qa_range_error("Font export requires a content user directory");
  size_t len = strlen(w->root) + 1 + strlen(name) + 1;
  char *path = malloc(len);
  if (path == NULL) qa_range_error("Out of memory");
  snprintf(path, len, "%s/%s", w->root, name);
  FILE *file = fopen(path, "wb");
  free(path);
  if (file == NULL) qa_range_error("Cannot export generated font %s", name);
  size_t wrote = fwrite(bytes.bytes, 1, bytes.length, file);
  fclose(file);
  if (wrote != bytes.length) qa_range_error("Incomplete generated font export %s", name);
}
static void sync_noop(void *ctx) { (void)ctx; }
static const qa_sound_asset *bank_load_sync(void *ctx, const char *path, bool compressed) {
  (void)compressed;
  return qa_app_application_q3_assets_sound(ctx, path);
}
qa_app_application_q3_assets *qa_app_application_q3_assets_create(
    qa_app_q3_application_assets_host host, qa_content_id content,
    void (*print)(void *ctx, const char *text), void *print_ctx,
    bool (*save_font_data)(void *ctx), void *save_font_data_ctx,
    const char *mode, const char *scope) {
  qa_app_application_q3_assets *a = calloc(1, sizeof *a);
  if (a == NULL) qa_range_error("Out of memory");
  a->host = host;
  a->content_copy = xstrdup(content);
  a->content = a->content_copy;
  a->print = print;
  a->print_ctx = print_ctx;
  a->scope = scope != NULL ? scope : "selected";
  host.provider(host.ctx, content, &a->provider);
  a->sound_bank = qa_sound_bank_create(a->provider.mounts);
  a->bank = qa_q3_pres_presentation_sound_bank_create(a->sound_bank, NULL, bank_load_sync, a);
  a->font_content.ctx = a->provider.mounts;
  a->font_content.open = font_open_bridge;
  qa_mounted_font_reader_init(&a->font_reader, a->font_content);
  const char *user_root = host.user_content_root(host.ctx, content);
  font_write_ctx *write_ctx = malloc(sizeof *write_ctx);
  if (write_ctx == NULL) qa_range_error("Out of memory");
  write_ctx->assets = a;
  write_ctx->root = user_root;
  write_ctx->save_font_data = save_font_data;
  write_ctx->save_ctx = save_font_data_ctx;
  a->font_write = write_ctx;
  qa_q3_font_generation_services generation = {write_ctx, font_image_bridge, font_save_bridge, font_write_file};
  qa_q3_renderer_font_registry_init(&a->fonts, qa_mounted_font_reader_as_reader(&a->font_reader),
                                    font_picture_bridge, a, sync_noop, NULL, &generation);
  qa_font_asset_services font_services = {a, font_asset_picture_bridge, font_asset_font_bridge};
  qa_q3_ui_asset_registry_init(&a->font_registry, font_services, font_print_bridge, a);
  a->fonts_init = true;
  /* Name indexing (create()). */
  for (size_t i = 0; i < a->provider.archive_path_count; i++) {
    const char *const *entries = NULL;
    size_t entry_count = 0;
    if (host.catalog_archive_entries(host.ctx, a->provider.archive_paths[i], &entries, &entry_count))
      for (size_t j = 0; j < entry_count; j++) {
        char *lowered = lowered_copy(entries[j]);
        names_add(a, lowered);
        free(lowered);
      }
  }
  for (size_t i = 0; i < a->provider.loose_root_count; i++) {
    char *root = xstrdup(a->provider.loose_roots[i]);
    size_t len = strlen(root);
    while (len > 1 && root[len - 1] == '/') root[--len] = '\0';
    walk_loose(a, root, root);
    free(root);
  }
  bool guest_async = mode != NULL && strcmp(mode, "guest-async") == 0;
  if (!guest_async) {
    for (size_t i = 0; i < a->name_count; i++) {
      const char *path = a->names[i];
      bool script = ends_with_ci(path, ".menu") || ends_with_ci(path, ".hud") || ends_with_ci(path, ".txt")
        || ends_with_ci(path, ".cfg") || ends_with_ci(path, ".voice") || ends_with_ci(path, ".vc")
        || ends_with_ci(path, ".h");
      bool sound = strncmp(path, "sound/", 6) == 0 && (ends_with_ci(path, ".wav") || ends_with_ci(path, ".ogg"));
      if (!script && !sound) continue;
      qa_content_opened_resource opened;
      if (!qa_content_mounted_open(a->provider.mounts, path, NULL, NULL, &opened)) continue;
      if (a->retained_count == a->retained_capacity) {
        size_t cap = a->retained_capacity == 0 ? 64 : a->retained_capacity * 2;
        retained_entry *next = realloc(a->retained, cap * sizeof *next);
        if (next == NULL) qa_range_error("Out of memory");
        a->retained = next; a->retained_capacity = cap;
      }
      retained_entry *entry = &a->retained[a->retained_count++];
      entry->key = xstrdup(path);
      entry->bytes = opened.bytes;
      entry->length = opened.byte_length;
      entry->resource = opened.reference != NULL ? opened.reference->id : 0;
    }
  }
  return a;
}
void qa_app_application_q3_assets_destroy(qa_app_application_q3_assets *assets) {
  if (assets == NULL) return;
  qa_app_application_q3_assets_close(assets);
  qa_q3_pres_presentation_sound_bank_destroy(assets->bank);
  qa_sound_bank_destroy(assets->sound_bank);
  for (size_t i = 0; i < assets->name_count; i++) free(assets->names[i]);
  free(assets->names);
  for (size_t i = 0; i < assets->picture_count; i++) {
    free((char *)assets->pictures[i]->name);
    free(assets->pictures[i]);
  }
  free(assets->pictures);
  for (size_t i = 0; i < assets->skin_count; i++) {
    qa_q3_pres_scene_skin *skin = assets->skins[i];
    for (size_t j = 0; j < skin->surface_count; j++) {
      free((char *)skin->surfaces[j].name);
      free((char *)skin->surfaces[j].shader);
    }
    free((void *)skin->surfaces);
    free((char *)skin->path);
    free(skin);
  }
  free(assets->skins);
  free(assets->content_copy);
  free(assets->font_write);
  if (assets->owns_host) qa_app_q3_application_assets_host_destroy(&assets->host);
  free(assets);
}
const char *qa_app_application_q3_assets_user_content_root(const qa_app_application_q3_assets *assets) {
  return assets->host.user_content_root(assets->host.ctx, assets->content);
}
qa_content_id qa_app_application_q3_assets_content(const qa_app_application_q3_assets *assets) { return assets->content; }
void qa_app_application_q3_assets_print(const qa_app_application_q3_assets *assets, const char *text) {
  assets->print(assets->print_ctx, text);
}
qa_q3_pres_presentation_sound_bank *qa_app_application_q3_assets_bank(qa_app_application_q3_assets *assets) { return assets->bank; }
qa_q3_renderer_font_registry *qa_app_application_q3_assets_fonts(qa_app_application_q3_assets *assets) { return &assets->fonts; }
qa_q3_ui_asset_registry *qa_app_application_q3_assets_font_registry(qa_app_application_q3_assets *assets) { return &assets->font_registry; }
const qa_app_q3_provider_scene_assets *qa_app_application_q3_assets_provider(const qa_app_application_q3_assets *assets) { return &assets->provider; }
static bool model_identity(const qa_q3_pres_scene_model *model, const model_entry *entry) {
  return model->kind == entry->kind
    && strcmp(model->path != NULL ? model->path : "", entry->path) == 0
    && model->resource.id == entry->resource_id;
}
bool qa_app_application_q3_assets_model_provider(qa_app_application_q3_assets *assets,
    const qa_q3_pres_scene_model *model, qa_app_q3_provider_scene_assets *out) {
  for (size_t i = 0; i < assets->model_count; i++)
    if (model_identity(model, &assets->models[i])) { *out = assets->models[i].provider; return true; }
  return false;
}
bool qa_app_application_q3_assets_model_content(qa_app_application_q3_assets *assets,
    const qa_q3_pres_scene_model *model, qa_content_id *out) {
  for (size_t i = 0; i < assets->model_count; i++)
    if (model_identity(model, &assets->models[i])) { *out = assets->models[i].content; return true; }
  return false;
}
bool qa_app_application_q3_assets_has(qa_app_application_q3_assets *assets, const char *path) {
  char *lowered = lowered_copy(path);
  bool found = false;
  for (size_t i = 0; i < assets->name_count; i++) if (strcmp(assets->names[i], lowered) == 0) found = true;
  free(lowered);
  return found;
}
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
char **qa_app_application_q3_assets_list(qa_app_application_q3_assets *assets, const char *prefix, size_t *out_count) {
  char *lowered = lowered_copy(prefix != NULL ? prefix : "");
  size_t prefix_len = strlen(lowered);
  char **out = malloc((assets->name_count == 0 ? 1 : assets->name_count) * sizeof *out);
  if (out == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < assets->name_count; i++)
    if (strncmp(assets->names[i], lowered, prefix_len) == 0) out[count++] = xstrdup(assets->names[i]);
  free(lowered);
  qa_js_sort(out, count, sizeof *out, cmp_str);
  *out_count = count;
  return out;
}
static retained_entry *find_open(qa_app_application_q3_assets *a, const char *path) {
  char *key = lowered_copy(path);
  for (size_t i = 0; i < a->retained_count; i++)
    if (strcmp(a->retained[i].key, key) == 0) { free(key); return &a->retained[i]; }
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(a->provider.mounts, path, NULL, NULL, &opened)) { free(key); return NULL; }
  if (a->retained_count == a->retained_capacity) {
    size_t cap = a->retained_capacity == 0 ? 64 : a->retained_capacity * 2;
    retained_entry *next = realloc(a->retained, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    a->retained = next; a->retained_capacity = cap;
  }
  retained_entry *entry = &a->retained[a->retained_count++];
  entry->key = key;
  entry->bytes = opened.bytes;
  entry->length = opened.byte_length;
  entry->resource = opened.reference != NULL ? opened.reference->id : 0;
  return entry;
}
uint8_t *qa_app_application_q3_assets_read(qa_app_application_q3_assets *assets, const char *path, size_t *out_length) {
  retained_entry *opened = find_open(assets, path);
  size_t length = opened != NULL ? opened->length : 0;
  uint8_t *out = malloc(length == 0 ? 1 : length);
  if (out == NULL) qa_range_error("Out of memory");
  if (length > 0) memcpy(out, opened->bytes, length);
  *out_length = length;
  return out;
}
double qa_app_application_q3_assets_read_file_length(qa_app_application_q3_assets *assets, const char *path) {
  retained_entry *opened = find_open(assets, path);
  return opened != NULL ? (double)opened->length : -1;
}
const uint8_t *qa_app_application_q3_assets_read_sync(qa_app_application_q3_assets *assets,
                                                      const char *path, size_t *out_length) {
  char *key = lowered_copy(path);
  retained_entry *found = NULL;
  for (size_t i = 0; i < assets->retained_count; i++)
    if (strcmp(assets->retained[i].key, key) == 0) found = &assets->retained[i];
  free(key);
  if (found == NULL) qa_range_error("Q3 source requested an unretained synchronous asset: %s", path);
  *out_length = found->length;
  return found->bytes;
}
const qa_sound_asset *qa_app_application_q3_assets_sound(qa_app_application_q3_assets *assets, const char *name) {
  const char *rest = name[0] == '#' ? name + 1 : name;
  char buffer[1024];
  if (name[0] != '#' && strncmp(name, "sound/", 6) != 0) snprintf(buffer, sizeof buffer, "sound/%s", name);
  else snprintf(buffer, sizeof buffer, "%s", rest);
  char *path = lowered_copy(buffer);
  for (size_t i = 0; i < assets->sound_count; i++)
    if (strcmp(assets->sounds[i].path, path) == 0) { free(path); return &assets->sounds[i].sound; }
  retained_entry *opened = NULL;
  for (size_t i = 0; i < assets->retained_count; i++)
    if (strcmp(assets->retained[i].key, path) == 0) opened = &assets->retained[i];
  if (opened == NULL) { free(path); return NULL; }
  if (assets->sound_count == assets->sound_capacity) {
    size_t cap = assets->sound_capacity == 0 ? 16 : assets->sound_capacity * 2;
    sound_entry *next = realloc(assets->sounds, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    assets->sounds = next; assets->sound_capacity = cap;
  }
  sound_entry *entry = &assets->sounds[assets->sound_count];
  entry->path = path;
  entry->sound.reference = NULL;
  entry->sound.resource = opened->resource;
  entry->sound.name = entry->path;
  qa_decode_sound_bytes(opened->bytes, opened->length, path, &entry->sound.pcm);
  assets->sound_count++;
  return &entry->sound;
}
static qa_content_id content_for(qa_app_application_q3_assets *a, const char *path) {
  if (strcmp(a->scope, "source") == 0) return a->content;
  if (strncmp(path, "models/players/", 15) == 0) {
    qa_content_id content = a->host.character_content(a->host.ctx);
    if (a->host.product_is_q3(a->host.ctx, content)) return content;
  }
  if (strncmp(path, "models/weapons2/", 16) == 0 || strncmp(path, "models/weaphits/", 16) == 0
      || strncmp(path, "models/ammo/", 12) == 0) {
    qa_content_id weapon = NULL;
    if (a->host.weapon_content(a->host.ctx, &weapon)) return weapon;
  }
  return a->content;
}
typedef struct model_load_ctx { qa_app_application_q3_assets *assets; } model_load_ctx;
static bool host_model_load(void *ctx, const char *path, qa_q3_pres_scene_model *out) {
  model_load_ctx *c = ctx;
  qa_app_application_q3_assets *a = c->assets;
  qa_content_id content = content_for(a, path);
  qa_app_q3_provider_scene_assets provider;
  a->host.provider(a->host.ctx, content, &provider);
  if (path[0] != '*') {
    const qa_resolved_resource_reference *resolved = NULL;
    if (!qa_content_mounted_resolve(provider.mounts, path, &resolved)) {
      *out = qa_q3_pres_default_model;
      return true;
    }
  }
  /* assets.model(content, path) as a SceneModel: brush models become
   * inline models with their bounds (a missing bounds row raises
   * "Missing inline model bounds <path>" in the host). */
  a->host.model(a->host.ctx, content, path, out);
  if (a->model_count == a->model_capacity) {
    size_t cap = a->model_capacity == 0 ? 16 : a->model_capacity * 2;
    model_entry *next = realloc(a->models, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    a->models = next; a->model_capacity = cap;
  }
  model_entry *entry = &a->models[a->model_count++];
  entry->kind = out->kind;
  entry->path = xstrdup(path);
  entry->resource_id = out->resource.id;
  entry->provider = provider;
  entry->content_copy = xstrdup(content);
  entry->content = entry->content_copy;
  return true;
}
static bool host_model(void *ctx, const char *path, qa_q3_pres_scene_model *out) {
  model_load_ctx load = {ctx};
  return qa_app_register_q3_model_request(path, host_model_load, &load, out);
}
static bool host_skin(void *ctx, const char *path, const qa_q3_pres_scene_skin **out) {
  qa_app_application_q3_assets *a = ctx;
  qa_content_id content = content_for(a, path);
  qa_app_q3_provider_scene_assets provider;
  a->host.provider(a->host.ctx, content, &provider);
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(provider.mounts, path, NULL, NULL, &opened)) { *out = NULL; return true; }
  char *text = malloc(opened.byte_length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  memcpy(text, opened.bytes, opened.byte_length);
  text[opened.byte_length] = '\0';
  free(opened.bytes);
  size_t surface_count = 0;
  qa_skin_surface *parsed = qa_parse_skin(text, &surface_count);
  free(text);
  qa_q3_pres_scene_skin *skin = malloc(sizeof *skin);
  qa_q3_pres_scene_skin_surface *surfaces = malloc((surface_count == 0 ? 1 : surface_count) * sizeof *surfaces);
  if (skin == NULL || surfaces == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < surface_count; i++) {
    surfaces[i].name = parsed[i].name;
    surfaces[i].shader = parsed[i].shader;
  }
  free(parsed);
  skin->path = xstrdup(path);
  skin->surfaces = surfaces;
  skin->surface_count = surface_count;
  if (a->skin_count == a->skin_capacity) {
    size_t cap = a->skin_capacity == 0 ? 8 : a->skin_capacity * 2;
    qa_q3_pres_scene_skin **next = realloc(a->skins, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    a->skins = next; a->skin_capacity = cap;
  }
  a->skins[a->skin_count++] = skin;
  *out = skin;
  return true;
}
typedef struct shader_load_ctx { qa_app_application_q3_assets *assets; bool mip; } shader_load_ctx;
/* provider.shaders.registerSourcePicture(valid, mip): false is TS null. */
static bool host_shader_load(void *ctx, const char *path, void *out) {
  shader_load_ctx *c = ctx;
  qa_material_picture picture;
  if (!c->assets->provider.register_source_picture(c->assets->provider.ctx, path, c->mip, &picture))
    return false;
  /* The picture's name can view the normalized request path, which the
   * request frees on return: the reply owns a copy. */
  picture.name = xstrdup(picture.name);
  *(qa_material_picture *)out = picture;
  return true;
}
static bool host_shader(void *ctx, const char *path, bool mip, const qa_material_picture **out) {
  qa_app_application_q3_assets *a = ctx;
  qa_material_picture picture;
  shader_load_ctx load = {a, mip};
  if (!qa_app_register_q3_shader_request(path, host_shader_load, &load, &picture)) { *out = NULL; return true; }
  if (a->picture_count == a->picture_capacity) {
    size_t cap = a->picture_capacity == 0 ? 16 : a->picture_capacity * 2;
    qa_material_picture **next = realloc(a->pictures, cap * sizeof *next);
    if (next == NULL) { free((char *)picture.name); return false; }
    a->pictures = next; a->picture_capacity = cap;
  }
  qa_material_picture *owned = malloc(sizeof *owned);
  if (owned == NULL) { free((char *)picture.name); return false; }
  *owned = picture; /* owns the name host_shader_load copied */
  a->pictures[a->picture_count++] = owned;
  *out = owned;
  return true;
}
static bool host_world(void *ctx, const char *requested_path, qa_q3_pres_world_scene *out) {
  qa_app_application_q3_assets *a = ctx;
  (void)requested_path;
  a->host.world_map_models(a->host.ctx, out);
  return true;
}
/* TS current: () => !this.closed. */
static bool remap_current(void *ctx) { return !((qa_app_application_q3_assets *)ctx)->closed; }
static void host_remap(void *ctx, const char *original, const char *replacement, const char *offset) {
  qa_app_application_q3_assets *a = ctx;
  double value = qa_js_parse_float(offset); /* Number.parseFloat(offset); NaN -> 0 */
  double time_offset = isnan(value) ? 0 : value;
  if (!a->host.remap_shader(a->host.ctx, original, replacement, time_offset, &a->provider, remap_current, a))
    qa_raise("Error", 0, 0, "Shader remap belongs to a retired source request");
}
qa_q3_pres_resource_host qa_app_application_q3_assets_resource_host(
    qa_app_application_q3_assets *assets, qa_q3_pres_scene_recorder_view scene) {
  qa_q3_pres_resource_host host;
  host.scene = scene;
  host.zero_picture = assets->provider.source_default_picture;
  host.ctx = assets;
  host.model = host_model;
  host.skin = host_skin;
  host.shader = host_shader;
  host.world = host_world;
  host.remap_shader = host_remap;
  return host;
}
void qa_app_application_q3_assets_close(qa_app_application_q3_assets *assets) {
  if (assets->closed) return;
  assets->closed = true;
  if (assets->fonts_init) {
    qa_q3_renderer_font_registry_close(&assets->fonts);
    qa_mounted_font_reader_close(&assets->font_reader);
  }
  for (size_t i = 0; i < assets->retained_count; i++) {
    free(assets->retained[i].key);
    free(assets->retained[i].bytes);
  }
  free(assets->retained);
  assets->retained = NULL;
  assets->retained_count = 0;
  assets->retained_capacity = 0;
  for (size_t i = 0; i < assets->sound_count; i++) {
    qa_pcm_sound_dispose(&assets->sounds[i].sound.pcm);
    free(assets->sounds[i].path);
  }
  free(assets->sounds);
  assets->sounds = NULL;
  assets->sound_count = 0;
  assets->sound_capacity = 0;
  for (size_t i = 0; i < assets->model_count; i++) {
    free(assets->models[i].path);
    free(assets->models[i].content_copy);
  }
  free(assets->models);
  assets->models = NULL;
  assets->model_count = 0;
  assets->model_capacity = 0;
}

/* --- The host over a real ApplicationAssets --- */

typedef struct host_provider_row {
  const qa_content_mounted *mounts;
  const char **archive_paths; size_t archive_count;
  const char **loose_roots; size_t loose_count;
} host_provider_row;
typedef struct host_archive_row {
  char *path;
  const char **names; size_t count;
} host_archive_row;
typedef struct application_host {
  qa_app_application_assets *assets;
  host_provider_row *providers; size_t provider_count, provider_capacity;
  host_archive_row *archives; size_t archive_count, archive_capacity;
  char **paths; size_t path_count, path_capacity;
  qa_bounds *world_bounds;
} application_host;

static void *grow_array(void *items, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return items;
  size_t cap = *capacity == 0 ? 8 : *capacity * 2;
  void *next = realloc(items, cap * size);
  if (next == NULL) qa_range_error("Out of memory");
  *capacity = cap;
  return next;
}
static const qa_content_catalog *host_catalog(application_host *h) {
  return qa_app_loaded_content_catalog(qa_app_application_assets_content(h->assets));
}
static const qa_catalog_discovered_product *host_product(application_host *h, qa_content_id content) {
  return qa_content_catalog_product(host_catalog(h), content);
}
/* SceneModel paths live as long as the host (TS strings are values). */
static const char *host_intern(application_host *h, const char *path) {
  for (size_t i = 0; i < h->path_count; i++) if (strcmp(h->paths[i], path) == 0) return h->paths[i];
  h->paths = grow_array(h->paths, &h->path_capacity, h->path_count, sizeof *h->paths);
  h->paths[h->path_count] = xstrdup(path);
  return h->paths[h->path_count++];
}
/* provider.shaders.registerPicture(path) (mipmap default false). */
static bool app_register_picture(void *ctx, const char *path, qa_material_picture *out) {
  *out = qa_scene_shader_registry_register_picture(ctx, path, false).material;
  return true;
}
/* provider.shaders.registerSourcePicture(path, mip): false is TS null. */
static bool app_register_source_picture(void *ctx, const char *path, bool mip, qa_material_picture *out) {
  qa_picture_asset asset;
  if (!qa_scene_shader_registry_register_source_picture(ctx, path, mip, &asset)) return false;
  *out = asset.material;
  return true;
}
/* registerGeneratedPicture(name, { kind: "rgba8", levels: [{ width: 256,
 * height: 256, pixels: rgba }], borderColor: { 0, 0, 0, 0 } }). */
static bool app_register_generated_picture(void *ctx, const char *name, qa_byte_view rgba, qa_material_picture *out) {
  qa_image_level level = {256, 256, rgba};
  qa_render_image image;
  memset(&image, 0, sizeof image);
  image.kind = QA_RENDER_IMAGE_RGBA8;
  image.levels = &level;
  image.level_count = 1;
  *out = qa_scene_shader_registry_register_generated_picture(ctx, name, image).material;
  return true;
}
/* provider.mounts.plan.mounts split by kind, cached per mount plan. */
static host_provider_row *host_provider_row_of(application_host *h, const qa_content_mounted *mounts) {
  for (size_t i = 0; i < h->provider_count; i++) if (h->providers[i].mounts == mounts) return &h->providers[i];
  h->providers = grow_array(h->providers, &h->provider_capacity, h->provider_count, sizeof *h->providers);
  host_provider_row *row = &h->providers[h->provider_count++];
  memset(row, 0, sizeof *row);
  row->mounts = mounts;
  const qa_resolved_mount_plan *plan = qa_content_mounted_plan(mounts);
  size_t count = plan->mount_count;
  row->archive_paths = malloc((count == 0 ? 1 : count) * sizeof *row->archive_paths);
  row->loose_roots = malloc((count == 0 ? 1 : count) * sizeof *row->loose_roots);
  if (row->archive_paths == NULL || row->loose_roots == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    const qa_content_mount *mount = &plan->mounts[i];
    if (mount->kind == QA_CONTENT_MOUNT_ARCHIVE) row->archive_paths[row->archive_count++] = mount->as.archive.archive_path;
    else row->loose_roots[row->loose_count++] = mount->as.loose.root_path;
  }
  return row;
}
/* assets.provider(content). */
static void host_app_provider(void *ctx, qa_content_id content, qa_app_q3_provider_scene_assets *out) {
  application_host *h = ctx;
  qa_app_provider_scene_assets provider = qa_app_application_assets_provider(h->assets, content);
  memset(out, 0, sizeof *out);
  out->ctx = provider.shaders;
  out->mounts = provider.mounts;
  out->register_picture = app_register_picture;
  out->register_source_picture = app_register_source_picture;
  out->register_generated_picture = app_register_generated_picture;
  out->source_default_picture = qa_scene_shader_registry_source_default_picture(provider.shaders).material;
  host_provider_row *row = host_provider_row_of(h, provider.mounts);
  out->archive_paths = row->archive_paths;
  out->archive_path_count = row->archive_count;
  out->loose_roots = row->loose_roots;
  out->loose_root_count = row->loose_count;
}
/* TS world.models[index]?.bounds over the decoded world. */
static bool world_model_bounds(const qa_decoded_world *world, double index, qa_bounds *out) {
  if (!(index >= 0) || index != floor(index)) return false;
  size_t i = (size_t)index;
  if (world->kind == QA_DECODED_WORLD_Q1 && i < world->as.q1->model_count) { *out = world->as.q1->models[i].bounds; return true; }
  if (world->kind == QA_DECODED_WORLD_Q2 && i < world->as.q2->model_count) { *out = world->as.q2->models[i].bounds; return true; }
  if (world->kind == QA_DECODED_WORLD_Q3 && i < world->as.q3->model_count) { *out = world->as.q3->models[i].bounds; return true; }
  return false;
}
/* const asset = await this.assets.model(content, path); brush models
 * become { kind: "inline", path, index, geometry, resource, bounds },
 * everything else { kind: "model", path, model, resource }. */
static void host_app_model(void *ctx, qa_content_id content, const char *path, qa_q3_pres_scene_model *out) {
  application_host *h = ctx;
  qa_app_model_asset asset = qa_app_application_assets_model(h->assets, content, path);
  memset(out, 0, sizeof *out);
  out->path = host_intern(h, path);
  out->resource = asset.resource;
  if (asset.model.kind == QA_DECODED_MODEL_BRUSH_MODEL) {
    qa_bounds bounds;
    if (!world_model_bounds(&asset.model.brush_world, asset.model.brush_model_index, &bounds))
      qa_raise("Error", 0, 0, "Missing inline model bounds %s", path);
    out->kind = QA_Q3_PRES_SCENE_MODEL_INLINE;
    out->index = asset.model.brush_model_index;
    out->geometry = asset.model.brush_world;
    out->bounds = bounds;
    return;
  }
  out->kind = QA_Q3_PRES_SCENE_MODEL_LOADED;
  out->model = asset.model;
}
/* catalog.products[].archives with this path: every entry path. */
static bool host_app_archive_entries(void *ctx, const char *archive_path, const char *const **out_names, size_t *out_count) {
  application_host *h = ctx;
  for (size_t i = 0; i < h->archive_count; i++)
    if (strcmp(h->archives[i].path, archive_path) == 0) {
      *out_names = h->archives[i].names;
      *out_count = h->archives[i].count;
      return true;
    }
  const qa_content_catalog *catalog = host_catalog(h);
  bool found = false;
  size_t total = 0;
  for (size_t p = 0; p < catalog->product_count; p++)
    for (size_t a = 0; a < catalog->products[p].archive_count; a++)
      if (strcmp(catalog->products[p].archives[a].path, archive_path) == 0) {
        found = true;
        total += catalog->products[p].archives[a].entry_count;
      }
  if (!found) return false;
  h->archives = grow_array(h->archives, &h->archive_capacity, h->archive_count, sizeof *h->archives);
  host_archive_row *row = &h->archives[h->archive_count++];
  row->path = xstrdup(archive_path);
  row->names = malloc((total == 0 ? 1 : total) * sizeof *row->names);
  if (row->names == NULL) qa_range_error("Out of memory");
  row->count = 0;
  for (size_t p = 0; p < catalog->product_count; p++)
    for (size_t a = 0; a < catalog->products[p].archive_count; a++) {
      const qa_catalog_archive *archive = &catalog->products[p].archives[a];
      if (strcmp(archive->path, archive_path) != 0) continue;
      for (size_t e = 0; e < archive->entry_count; e++) row->names[row->count++] = archive->entries[e].path;
    }
  *out_names = row->names;
  *out_count = row->count;
  return true;
}
/* catalog.product(content).userContent?.root ?? null. */
static const char *host_app_user_root(void *ctx, qa_content_id content) {
  const qa_catalog_discovered_product *product = host_product(ctx, content);
  return product->user_content == NULL ? NULL : product->user_content->root;
}
/* catalog.product(content).expectation.family === "q3". */
static bool host_app_product_is_q3(void *ctx, qa_content_id content) {
  return host_product(ctx, content)->expectation.family == QA_GAME_FAMILY_Q3;
}
static const qa_executable_recipe *host_recipe(application_host *h) {
  return qa_app_loaded_content_recipe(qa_app_application_assets_content(h->assets));
}
/* recipe.character.appearance.content. */
static qa_content_id host_app_character(void *ctx) { return host_recipe(ctx)->character.appearance.content; }
/* recipe.weapons.find(weapon => product(weapon.content) is q3)?.content. */
static bool host_app_weapon(void *ctx, qa_content_id *out) {
  application_host *h = ctx;
  const qa_executable_recipe *recipe = host_recipe(h);
  for (size_t i = 0; i < recipe->weapons.count; i++)
    if (host_app_product_is_q3(h, recipe->weapons.values[i].content)) {
      *out = recipe->weapons.values[i].content;
      return true;
    }
  return false;
}
/* { map: { models: this.assets.world.map.models } }. */
static void host_app_world_models(void *ctx, qa_q3_pres_world_scene *out) {
  application_host *h = ctx;
  const qa_decoded_world *map = &qa_app_application_assets_world(h->assets)->map;
  size_t count = map->kind == QA_DECODED_WORLD_Q1 ? map->as.q1->model_count
               : map->kind == QA_DECODED_WORLD_Q2 ? map->as.q2->model_count : map->as.q3->model_count;
  free(h->world_bounds);
  h->world_bounds = malloc((count == 0 ? 1 : count) * sizeof *h->world_bounds);
  if (h->world_bounds == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) (void)world_model_bounds(map, (double)i, &h->world_bounds[i]);
  out->model_bounds = h->world_bounds;
  out->model_count = count;
}
/* this.assets.world.remapShader(original, replacement, timeOffset,
 * { source: source.shaders, current }) !== "stale". */
static bool host_app_remap(void *ctx, const char *original, const char *replacement, double time_offset,
                           const qa_app_q3_provider_scene_assets *source, bool (*current)(void *current_ctx),
                           void *current_ctx) {
  application_host *h = ctx;
  return qa_world_scene_remap_shader(qa_app_application_assets_world(h->assets), original, replacement, time_offset,
                                     source->ctx, current, current_ctx) != QA_REMAP_STALE;
}

qa_app_q3_application_assets_host qa_app_q3_application_assets_host_create(qa_app_application_assets *assets) {
  qa_app_q3_application_assets_host host;
  memset(&host, 0, sizeof host);
  application_host *h = calloc(1, sizeof *h);
  if (h == NULL) return host;
  h->assets = assets;
  host.ctx = h;
  host.provider = host_app_provider;
  host.model = host_app_model;
  host.catalog_archive_entries = host_app_archive_entries;
  host.user_content_root = host_app_user_root;
  host.product_is_q3 = host_app_product_is_q3;
  host.character_content = host_app_character;
  host.weapon_content = host_app_weapon;
  host.world_map_models = host_app_world_models;
  host.remap_shader = host_app_remap;
  return host;
}
/* TS ApplicationQ3Assets.assets: the ApplicationAssets behind an
 * application-built host (NULL for any other host). */
qa_app_application_assets *qa_app_application_q3_assets_application(const qa_app_application_q3_assets *assets) {
  if (assets->host.provider != host_app_provider || assets->host.ctx == NULL) return NULL;
  return ((const application_host *)assets->host.ctx)->assets;
}
void qa_app_q3_application_assets_host_destroy(qa_app_q3_application_assets_host *host) {
  application_host *h = host->ctx;
  if (h == NULL) return;
  for (size_t i = 0; i < h->provider_count; i++) {
    free(h->providers[i].archive_paths);
    free(h->providers[i].loose_roots);
  }
  free(h->providers);
  for (size_t i = 0; i < h->archive_count; i++) {
    free(h->archives[i].path);
    free(h->archives[i].names);
  }
  free(h->archives);
  for (size_t i = 0; i < h->path_count; i++) free(h->paths[i]);
  free(h->paths);
  free(h->world_bounds);
  free(h);
  host->ctx = NULL;
}
qa_app_application_q3_assets *qa_app_application_q3_assets_create_from_application(
    qa_app_application_assets *assets, qa_content_id content,
    void (*print)(void *ctx, const char *text), void *print_ctx,
    bool (*save_font_data)(void *ctx), void *save_font_data_ctx,
    const char *mode, const char *scope) {
  qa_app_q3_application_assets_host host = qa_app_q3_application_assets_host_create(assets);
  if (host.ctx == NULL) qa_range_error("Out of memory");
  qa_app_application_q3_assets *result = qa_app_application_q3_assets_create(
      host, content, print, print_ctx, save_font_data, save_font_data_ctx, mode, scope);
  result->owns_host = true;
  return result;
}
