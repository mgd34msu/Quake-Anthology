/* Port of src/app/bootstrap/weapon-hud.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/weapon_hud.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/catalog/index.h"
#include "qa/content/catalog/weapon_hud.h"
#include "qa/content/mounts.h"
#include "qa/formats/images/indexed.h"
#include "qa/formats/images/palette.h"
#include "qa/formats/images/wad.h"
#include "qa/foundation/support.h"
#include "qa/materials/compile.h"
#include "qa/render/scene/material_registrations.h"
#include "qa/render/scene/resources.h"
#include "qa/render/scene/shaders.h"
#include "qa/render/scene/textures.h"

/* A picture entry is heap-allocated so a returned picture stays put while the map grows. */
typedef struct pic_entry { char *id; char *name; qa_picture_asset picture; } pic_entry;
typedef struct pic_map { pic_entry **items; size_t count, cap; } pic_map;
typedef struct aspect_entry { char *id; double aspect; } aspect_entry;
typedef struct aspect_map { aspect_entry *items; size_t count, cap; } aspect_map;
/* Deep copy of a WeaponHudIcon (the catalog result that produced it is freed). */
typedef struct icon_copy { qa_weapon_hud_icon icon; char *content, *path, *lump, *name; } icon_copy;
typedef struct icon_entry { char *key; icon_copy icon; } icon_entry;
typedef struct loaded_entry { char *key; char *id; } loaded_entry;
struct qa_app_weapon_hud_assets {
  qa_app_application_assets *assets;
  loaded_entry *pending; size_t pending_count, pending_cap; /* TS pending: key -> ResourceId */
  pic_map pictures;
  aspect_map logical_aspects;
  icon_entry *texture_icons; size_t texture_icon_count, texture_icon_cap;
};
struct qa_app_weapon_hud_refresh {
  qa_app_weapon_hud_assets *assets;
  pic_map pictures;
  aspect_map aspects;
};
/* Pick<ApplicationAssets, "provider">: the assets themselves or a PreparedApplicationImages. */
typedef struct providers { qa_app_application_assets *assets; qa_app_prepared_images *prepared; } providers;

static char *xdup(const char *s) {
  if (s == NULL) return NULL;
  size_t n = strlen(s) + 1;
  char *r = malloc(n);
  if (r == NULL) qa_range_error("Out of memory");
  memcpy(r, s, n);
  return r;
}
static void *grow(void *items, size_t *cap, size_t size) {
  *cap = *cap ? *cap * 2 : 8;
  void *next = realloc(items, *cap * size);
  if (next == NULL) qa_range_error("Out of memory");
  return next;
}
static pic_entry *pic_find(const pic_map *map, const char *id) {
  for (size_t i = 0; i < map->count; i++) if (strcmp(map->items[i]->id, id) == 0) return map->items[i];
  return NULL;
}
/* Map.set(id, picture): name is the owned ImagePicture name (NULL for a material picture). */
static void pic_set(pic_map *map, const char *id, char *name, qa_picture_asset picture) {
  pic_entry *entry = pic_find(map, id);
  if (entry == NULL) {
    if (map->count == map->cap) map->items = grow(map->items, &map->cap, sizeof *map->items);
    entry = calloc(1, sizeof *entry);
    if (entry == NULL) qa_range_error("Out of memory");
    entry->id = xdup(id);
    map->items[map->count++] = entry;
  } else free(entry->name);
  entry->name = name;
  entry->picture = picture;
  if (picture.kind == QA_PICTURE_IMAGE) entry->picture.image.name = name;
}
static void pic_map_free(pic_map *map) {
  for (size_t i = 0; i < map->count; i++) { free(map->items[i]->id); free(map->items[i]->name); free(map->items[i]); }
  free(map->items);
  memset(map, 0, sizeof *map);
}
static void aspect_set(aspect_map *map, const char *id, double aspect) {
  for (size_t i = 0; i < map->count; i++) if (strcmp(map->items[i].id, id) == 0) { map->items[i].aspect = aspect; return; }
  if (map->count == map->cap) map->items = grow(map->items, &map->cap, sizeof *map->items);
  map->items[map->count].id = xdup(id);
  map->items[map->count++].aspect = aspect;
}
static void aspect_map_free(aspect_map *map) {
  for (size_t i = 0; i < map->count; i++) free(map->items[i].id);
  free(map->items);
  memset(map, 0, sizeof *map);
}
static icon_copy icon_copy_of(qa_weapon_hud_icon icon) {
  icon_copy out;
  memset(&out, 0, sizeof out);
  out.icon = icon;
  out.content = xdup(icon.content); out.path = xdup(icon.resource.path);
  out.lump = xdup(icon.lump); out.name = xdup(icon.name);
  out.icon.resource.content = xdup(icon.resource.content);
  out.icon.content = out.content; out.icon.resource.path = out.path;
  out.icon.lump = out.lump; out.icon.name = out.name;
  return out;
}
static void icon_copy_free(icon_copy *icon) {
  free((char *)icon->icon.resource.content);
  free(icon->content); free(icon->path); free(icon->lump); free(icon->name);
}
static char *icon_key(qa_weapon_hud_icon icon) {
  const char *a, *b, *c = "";
  if (icon.kind == QA_WEAPON_HUD_ICON_SHADER) { a = icon.content; b = icon.name; }
  else { a = icon.resource.content; b = icon.resource.path; if (icon.kind == QA_WEAPON_HUD_ICON_WAD_PICTURE) c = icon.lump; }
  size_t length = strlen(a) + strlen(b) + strlen(c) + 3;
  char *key = malloc(length);
  if (key == NULL) qa_range_error("Out of memory");
  if (icon.kind == QA_WEAPON_HUD_ICON_SHADER) snprintf(key, length, "%s/%s", a, b);
  else snprintf(key, length, "%s/%s/%s", a, b, c);
  return key;
}
static qa_app_provider_scene_assets provider_of(providers source, qa_content_id content) {
  return source.prepared != NULL ? qa_app_prepared_images_provider(source.prepared, content)
                                 : qa_app_application_assets_provider(source.assets, content);
}
static void free_bytes(void *bytes) { free(bytes); }
static void wad_release(void *wad) { qa_wad_archive_destroy(wad); }
static void indexed_release(void *image) { qa_indexed_image_destroy(image); }

/* TS loadIcon(icon, key, providers = this.assets, pictures = this.pictures, aspects = this.logicalAspects). */
static void load_icon(qa_app_weapon_hud_assets *a, qa_weapon_hud_icon icon, const char *key, providers source,
                      pic_map *pictures, aspect_map *aspects, char *id) {
  qa_content_id content = icon.kind == QA_WEAPON_HUD_ICON_SHADER ? icon.content : icon.resource.content;
  qa_app_provider_scene_assets provider = provider_of(source, content);
  if (icon.kind == QA_WEAPON_HUD_ICON_SHADER) {
    pic_set(pictures, id, NULL, qa_scene_shader_registry_register_picture(provider.shaders, icon.name, false));
    return;
  }
  if (provider.family == QA_GAME_FAMILY_Q1) {
    qa_content_opened_resource asset;
    memset(&asset, 0, sizeof asset);
    bool opened = qa_content_mounted_open(provider.mounts, icon.resource.path, NULL, NULL, &asset);
    if (!opened || provider.palette == NULL) {
      if (opened) free(asset.bytes);
      qa_raise("Error", 0, 0, "Weapon HUD picture missing: %s", key);
    }
    qa_cleanup bytes_guard;
    qa_cleanup_push(&bytes_guard, free_bytes, asset.bytes);
    qa_wad_archive wad;
    memset(&wad, 0, sizeof wad);
    qa_cleanup wad_guard;
    qa_cleanup_push(&wad_guard, wad_release, &wad);
    const uint8_t *bytes = asset.bytes;
    size_t length = asset.byte_length;
    if (icon.kind == QA_WEAPON_HUD_ICON_WAD_PICTURE) {
      if (!qa_decode_wad(asset.bytes, asset.byte_length, icon.resource.path, &wad)) qa_range_error("Out of memory");
      const qa_wad_lump *lump = NULL;
      for (size_t i = 0; i < wad.lump_count && lump == NULL; i++)
        if (strcmp(wad.lumps[i].name, icon.lump) == 0) lump = &wad.lumps[i];
      if (lump == NULL) qa_raise("Error", 0, 0, "Weapon HUD WAD lump missing: %s", key);
      bytes = lump->bytes; length = lump->bytes_length;
    }
    qa_indexed_image picture;
    memset(&picture, 0, sizeof picture);
    qa_cleanup picture_guard;
    qa_cleanup_push(&picture_guard, indexed_release, &picture);
    if (!qa_decode_qpic(bytes, length, key, &picture)) qa_range_error("Out of memory");
    qa_image_level level = {(double)picture.width, (double)picture.height, {picture.indices, picture.indices_length}};
    qa_palette_transparency transparency = {"index", 255};
    qa_render_image indexed = qa_indexed_render_image_make(&level, 1, *provider.palette, transparency, NULL, NULL);
    qa_texture_sampling sampling = {"clamp", "linear"};
    qa_renderer_image source_image;
    memset(&source_image, 0, sizeof source_image);
    source_image.source_is_resource = true;
    source_image.source_resource = *asset.reference;
    const qa_scene_image_handle *handle =
        qa_scene_image_registry_register(qa_app_application_assets_images(a->assets), key, indexed, sampling, source_image);
    qa_cleanup_pop(&picture_guard);
    qa_indexed_image_destroy(&picture);
    qa_cleanup_pop(&wad_guard);
    qa_wad_archive_destroy(&wad);
    qa_cleanup_pop(&bytes_guard);
    free(asset.bytes);
    qa_picture_asset value;
    memset(&value, 0, sizeof value);
    value.kind = QA_PICTURE_IMAGE;
    value.image.image = handle->image;
    pic_set(pictures, id, xdup(key), value);
    return;
  }
  qa_scene_texture_load_options options = {true, false, "clamp", qa_game_family_name(provider.family), NULL};
  const qa_scene_texture *texture = qa_scene_texture_loader_load(provider.textures, icon.resource.path, &options);
  if (texture == NULL) qa_raise("Error", 0, 0, "Weapon HUD image missing: %s", key);
  qa_picture_asset value;
  memset(&value, 0, sizeof value);
  value.kind = QA_PICTURE_IMAGE;
  value.image.image = texture->image;
  pic_set(pictures, id, xdup(key), value);
  aspect_set(aspects, id, texture->width / texture->height);
  /* this.textureIcons.set(key, icon) */
  for (size_t i = 0; i < a->texture_icon_count; i++)
    if (strcmp(a->texture_icons[i].key, key) == 0) {
      icon_copy replacement = icon_copy_of(icon);
      icon_copy_free(&a->texture_icons[i].icon);
      a->texture_icons[i].icon = replacement;
      return;
    }
  if (a->texture_icon_count == a->texture_icon_cap) a->texture_icons = grow(a->texture_icons, &a->texture_icon_cap, sizeof *a->texture_icons);
  a->texture_icons[a->texture_icon_count].key = xdup(key);
  a->texture_icons[a->texture_icon_count].icon = icon_copy_of(icon);
  a->texture_icon_count++;
}
static char *resource_id_for(const char *key) {
  size_t length = strlen(key) + sizeof "resource:weapon-hud:";
  char *id = malloc(length);
  if (id == NULL) qa_range_error("Out of memory");
  snprintf(id, length, "resource:weapon-hud:%s", key);
  return id;
}

qa_app_weapon_hud_assets *qa_app_weapon_hud_assets_create(qa_app_application_assets *assets) {
  qa_app_weapon_hud_assets *a = calloc(1, sizeof *a);
  if (a == NULL) return NULL;
  a->assets = assets;
  return a;
}
void qa_app_weapon_hud_assets_destroy(qa_app_weapon_hud_assets *a) {
  if (a == NULL) return;
  pic_map_free(&a->pictures);
  aspect_map_free(&a->logical_aspects);
  for (size_t i = 0; i < a->texture_icon_count; i++) { free(a->texture_icons[i].key); icon_copy_free(&a->texture_icons[i].icon); }
  free(a->texture_icons);
  for (size_t i = 0; i < a->pending_count; i++) { free(a->pending[i].key); free(a->pending[i].id); }
  free(a->pending);
  free(a);
}
const qa_picture_asset *qa_app_weapon_hud_assets_picture(qa_app_weapon_hud_assets *a, qa_resource_id id) {
  pic_entry *entry = pic_find(&a->pictures, id);
  return entry == NULL ? NULL : &entry->picture;
}
static void refresh_release(void *ctx) { qa_app_weapon_hud_refresh_discard(ctx); }
qa_app_weapon_hud_refresh *qa_app_weapon_hud_assets_prepare_image_refresh(qa_app_weapon_hud_assets *a,
                                                                          qa_app_prepared_images *prepared) {
  qa_app_weapon_hud_refresh *refresh = calloc(1, sizeof *refresh);
  if (refresh == NULL) qa_range_error("Out of memory");
  refresh->assets = a;
  qa_cleanup guard;
  qa_cleanup_push(&guard, refresh_release, refresh);
  providers source = {a->assets, prepared};
  /* Iterate a snapshot: loadIcon re-sets textureIcons entries (same keys). */
  for (size_t i = 0; i < a->texture_icon_count; i++) {
    char *key = xdup(a->texture_icons[i].key);
    icon_copy icon = icon_copy_of(a->texture_icons[i].icon.icon);
    char *id = resource_id_for(key);
    load_icon(a, icon.icon, key, source, &refresh->pictures, &refresh->aspects, id);
    free(id); icon_copy_free(&icon); free(key);
  }
  qa_cleanup_pop(&guard);
  return refresh;
}
void qa_app_weapon_hud_refresh_publish(qa_app_weapon_hud_refresh *refresh) {
  qa_app_weapon_hud_assets *a = refresh->assets;
  for (size_t i = 0; i < refresh->pictures.count; i++) {
    pic_entry *entry = refresh->pictures.items[i];
    pic_set(&a->pictures, entry->id, entry->name, entry->picture);
    entry->name = NULL;
  }
  for (size_t i = 0; i < refresh->aspects.count; i++)
    aspect_set(&a->logical_aspects, refresh->aspects.items[i].id, refresh->aspects.items[i].aspect);
  qa_app_weapon_hud_refresh_discard(refresh);
}
void qa_app_weapon_hud_refresh_discard(qa_app_weapon_hud_refresh *refresh) {
  if (refresh == NULL) return;
  pic_map_free(&refresh->pictures);
  aspect_map_free(&refresh->aspects);
  free(refresh);
}
/* picture.material.compiled.registered.stages: the first loaded image stage's first frame. */
static bool material_aspect(const qa_material_picture *material, double *out) {
  const qa_compiled_material *compiled = material->compiled_is_handle
      ? ((const qa_scene_material_handle *)material->compiled)->current
      : (const qa_compiled_material *)material->compiled;
  if (compiled == NULL) return false;
  for (size_t i = 0; i < compiled->registered.stage_count; i++) {
    const qa_mat_registered_stage *stage = &compiled->registered.stages[i];
    if (strcmp(stage->kind, "loaded") != 0 || strcmp(stage->binding.kind, "images") != 0) continue;
    const qa_mat_finished_playback *playback = &stage->binding.playback;
    const qa_renderer_image *image = strcmp(playback->kind, "single") == 0 ? &playback->image.image : &playback->frames[0].image;
    *out = image->width / image->height;
    return true;
  }
  return false;
}
double qa_app_weapon_hud_assets_aspect(qa_app_weapon_hud_assets *a, const qa_resource_id *id) {
  if (id == NULL) return 1;
  for (size_t i = 0; i < a->logical_aspects.count; i++)
    if (strcmp(a->logical_aspects.items[i].id, *id) == 0) return a->logical_aspects.items[i].aspect;
  const qa_picture_asset *picture = qa_app_weapon_hud_assets_picture(a, *id);
  if (picture != NULL && picture->kind == QA_PICTURE_IMAGE) return picture->image.image.width / picture->image.image.height;
  double aspect = 1;
  if (picture != NULL && picture->kind == QA_PICTURE_MATERIAL && material_aspect(&picture->material, &aspect)) return aspect;
  return 1;
}
qa_resource_id qa_app_weapon_hud_assets_load(qa_app_weapon_hud_assets *a, qa_weapon_hud_icon icon) {
  char *key = icon_key(icon);
  for (size_t i = 0; i < a->pending_count; i++)
    if (strcmp(a->pending[i].key, key) == 0) { free(key); return a->pending[i].id; }
  qa_cleanup key_guard;
  qa_cleanup_push(&key_guard, free_bytes, key);
  char *id = resource_id_for(key);
  qa_cleanup id_guard;
  qa_cleanup_push(&id_guard, free_bytes, id);
  providers source = {a->assets, NULL};
  load_icon(a, icon, key, source, &a->pictures, &a->logical_aspects, id);
  qa_cleanup_pop(&id_guard);
  qa_cleanup_pop(&key_guard);
  if (a->pending_count == a->pending_cap) a->pending = grow(a->pending, &a->pending_cap, sizeof *a->pending);
  a->pending[a->pending_count].key = key;
  a->pending[a->pending_count].id = id;
  return a->pending[a->pending_count++].id;
}
qa_app_weapon_hud_prepared qa_app_weapon_hud_assets_prepare(qa_app_weapon_hud_assets *a, const qa_weapon_hud_status *status) {
  qa_app_weapon_hud_prepared out;
  memset(&out, 0, sizeof out);
  if (status == NULL) return out;
  const qa_catalog_discovered_product *product =
      qa_content_catalog_product(qa_app_application_assets_catalog(a->assets), status->source.content);
  qa_weapon_hud_icons icons;
  memset(&icons, 0, sizeof icons);
  bool found = false;
  if (!qa_weapon_hud_icons_for(status->source, &product->expectation, status->item, &icons, &found)) qa_range_error("Out of memory");
  if (!found) { qa_weapon_hud_icons_free(&icons); return out; }
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_weapon_hud_icons_free, &icons);
  /* Promise.all([load(selectedWeapon ?? weapon), load(ammo)]): both loads start in order. */
  const qa_weapon_hud_icon *weapon_icon = icons.has_selected_weapon ? &icons.selected_weapon : icons.has_weapon ? &icons.weapon : NULL;
  if (weapon_icon != NULL) { out.weapon = qa_app_weapon_hud_assets_load(a, *weapon_icon); out.has_weapon = true; }
  if (icons.has_ammo) { out.ammo = qa_app_weapon_hud_assets_load(a, icons.ammo); out.has_ammo = true; }
  qa_cleanup_pop(&guard);
  qa_weapon_hud_icons_free(&icons);
  return out;
}
qa_app_application_assets *qa_app_weapon_hud_assets_assets(qa_app_weapon_hud_assets *a) { return a->assets; }
