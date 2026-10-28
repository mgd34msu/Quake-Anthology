/* Port of src/app/bootstrap/weapon-hud.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/weapon_hud.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/catalog/weapon_hud.h"
#include "qa/formats/images/indexed.h"
#include "qa/formats/images/palette.h"
#include "qa/formats/images/wad.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
typedef struct pic_entry { char *id; qa_picture_asset picture; } pic_entry;
typedef struct aspect_entry { char *id; double aspect; } aspect_entry;
typedef struct icon_entry { char *key; qa_weapon_hud_icon icon; } icon_entry;
typedef struct loaded_entry { char *key; char *id; } loaded_entry;
struct qa_app_weapon_hud_assets {
  qa_app_weapon_hud_assets_host host;
  pic_entry *pictures; size_t picture_count, picture_cap;
  aspect_entry *aspects; size_t aspect_count, aspect_cap;
  icon_entry *texture_icons; size_t texture_icon_count, texture_icon_cap;
  loaded_entry *loaded; size_t loaded_count, loaded_cap;
};
struct qa_app_weapon_hud_refresh {
  qa_app_weapon_hud_assets *assets;
  pic_entry *pictures; size_t picture_count, picture_cap;
  aspect_entry *aspects; size_t aspect_count, aspect_cap;
};
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static void put_picture(pic_entry **arr, size_t *count, size_t *cap, const char *id, qa_picture_asset picture) {
  for (size_t i = 0; i < *count; i++) if (strcmp((*arr)[i].id, id) == 0) { (*arr)[i].picture = picture; return; }
  if (*count == *cap) { *cap = *cap ? *cap * 2 : 8; *arr = realloc(*arr, *cap * sizeof **arr); if (!*arr) qa_range_error("Out of memory"); }
  (*arr)[*count].id = xdup(id);
  (*arr)[*count].picture = picture;
  (*count)++;
}
static void put_aspect(aspect_entry **arr, size_t *count, size_t *cap, const char *id, double aspect) {
  for (size_t i = 0; i < *count; i++) if (strcmp((*arr)[i].id, id) == 0) { (*arr)[i].aspect = aspect; return; }
  if (*count == *cap) { *cap = *cap ? *cap * 2 : 8; *arr = realloc(*arr, *cap * sizeof **arr); if (!*arr) qa_range_error("Out of memory"); }
  (*arr)[*count].id = xdup(id);
  (*arr)[*count].aspect = aspect;
  (*count)++;
}
static char *icon_key(qa_weapon_hud_icon icon) {
  char buf[4096];
  if (icon.kind == QA_WEAPON_HUD_ICON_SHADER) snprintf(buf, sizeof buf, "%s/%s", icon.content, icon.name);
  else snprintf(buf, sizeof buf, "%s/%s/%s", icon.resource.content, icon.resource.path, icon.kind == QA_WEAPON_HUD_ICON_WAD_PICTURE ? icon.lump : "");
  return xdup(buf);
}
static qa_resource_id load_icon_into(qa_app_weapon_hud_assets *a, qa_weapon_hud_icon icon, const char *key,
                                     qa_app_weapon_hud_provider provider_override, bool has_override,
                                     pic_entry **pictures, size_t *picture_count, size_t *picture_cap,
                                     aspect_entry **aspects, size_t *aspect_count, size_t *aspect_cap) {
  char id_buf[4200];
  snprintf(id_buf, sizeof id_buf, "resource:weapon-hud:%s", key);
  qa_content_id content = icon.kind == QA_WEAPON_HUD_ICON_SHADER ? icon.content : icon.resource.content;
  qa_app_weapon_hud_provider provider = has_override ? provider_override : a->host.provider(a->host.ctx, content);
  if (icon.kind == QA_WEAPON_HUD_ICON_SHADER) {
    put_picture(pictures, picture_count, picture_cap, id_buf, provider.register_shader_picture(provider.ctx, icon.name));
  } else if (strcmp(provider.family, "q1") == 0) {
    uint8_t *bytes = NULL; size_t length = 0;
    qa_resolved_resource_reference reference; memset(&reference, 0, sizeof reference);
    bool opened = provider.open(provider.ctx, icon.resource.path, &bytes, &length, &reference);
    if (!opened || !provider.has_palette) { free(bytes); qa_range_error("Weapon HUD picture missing: %s", key); }
    qa_indexed_image picture; memset(&picture, 0, sizeof picture);
    if (icon.kind == QA_WEAPON_HUD_ICON_WAD_PICTURE) {
      qa_wad_archive wad; memset(&wad, 0, sizeof wad);
      if (!qa_decode_wad(bytes, length, icon.resource.path, &wad)) qa_range_error("Weapon HUD WAD lump missing: %s", key);
      const qa_wad_lump *lump = NULL;
      for (size_t i = 0; i < wad.lump_count; i++) if (strcmp(wad.lumps[i].name, icon.lump) == 0) lump = &wad.lumps[i];
      if (lump == NULL) qa_range_error("Weapon HUD WAD lump missing: %s", key);
      qa_wad_image wad_image; memset(&wad_image, 0, sizeof wad_image);
      if (!qa_decode_wad_image(&wad, lump, key, &wad_image)) qa_range_error("Weapon HUD WAD lump missing: %s", key);
      picture = wad_image.image;
    } else if (!qa_decode_qpic(bytes, length, key, &picture)) qa_range_error("Weapon HUD picture missing: %s", key);
    qa_image_level level = {(double)picture.width, (double)picture.height, {picture.indices, picture.indices_length}};
    qa_palette_transparency transparency = {"index", 255};
    qa_render_image indexed = qa_indexed_render_image_make(&level, 1, provider.palette, transparency, NULL, NULL);
    qa_renderer_image image = a->host.register_image(a->host.ctx, key, indexed, reference);
    qa_picture_asset asset; memset(&asset, 0, sizeof asset);
    asset.kind = QA_PICTURE_IMAGE;
    asset.image.name = key;
    asset.image.image = image;
    put_picture(pictures, picture_count, picture_cap, id_buf, asset);
    free(bytes);
  } else {
    qa_renderer_image texture; memset(&texture, 0, sizeof texture);
    double width = 0, height = 0;
    if (!provider.load_texture(provider.ctx, icon.resource.path, &texture, &width, &height))
      qa_range_error("Weapon HUD image missing: %s", key);
    qa_picture_asset asset; memset(&asset, 0, sizeof asset);
    asset.kind = QA_PICTURE_IMAGE;
    asset.image.name = key;
    asset.image.image = texture;
    put_picture(pictures, picture_count, picture_cap, id_buf, asset);
    put_aspect(aspects, aspect_count, aspect_cap, id_buf, width / height);
    /* textureIcons retained for refresh (only on the main store). */
    if (pictures == &a->pictures) {
      bool exists = false;
      for (size_t i = 0; i < a->texture_icon_count; i++) if (strcmp(a->texture_icons[i].key, key) == 0) exists = true;
      if (!exists) {
        if (a->texture_icon_count == a->texture_icon_cap) { a->texture_icon_cap = a->texture_icon_cap ? a->texture_icon_cap * 2 : 8; a->texture_icons = realloc(a->texture_icons, a->texture_icon_cap * sizeof *a->texture_icons); if (!a->texture_icons) qa_range_error("Out of memory"); }
        a->texture_icons[a->texture_icon_count].key = xdup(key);
        a->texture_icons[a->texture_icon_count].icon = icon;
        a->texture_icon_count++;
      }
    }
  }
  /* Return a retained copy of the id string (owned by the assets). */
  for (size_t i = 0; i < a->loaded_count; i++) if (strcmp(a->loaded[i].key, key) == 0) return a->loaded[i].id;
  if (a->loaded_count == a->loaded_cap) { a->loaded_cap = a->loaded_cap ? a->loaded_cap * 2 : 8; a->loaded = realloc(a->loaded, a->loaded_cap * sizeof *a->loaded); if (!a->loaded) qa_range_error("Out of memory"); }
  a->loaded[a->loaded_count].key = xdup(key);
  a->loaded[a->loaded_count].id = xdup(id_buf);
  return a->loaded[a->loaded_count++].id;
}
qa_app_weapon_hud_assets *qa_app_weapon_hud_assets_create(qa_app_weapon_hud_assets_host host) {
  qa_app_weapon_hud_assets *a = calloc(1, sizeof *a);
  if (!a) return NULL;
  a->host = host;
  return a;
}
void qa_app_weapon_hud_assets_destroy(qa_app_weapon_hud_assets *a) {
  if (!a) return;
  for (size_t i = 0; i < a->picture_count; i++) free(a->pictures[i].id);
  free(a->pictures);
  for (size_t i = 0; i < a->aspect_count; i++) free(a->aspects[i].id);
  free(a->aspects);
  for (size_t i = 0; i < a->texture_icon_count; i++) free(a->texture_icons[i].key);
  free(a->texture_icons);
  for (size_t i = 0; i < a->loaded_count; i++) { free(a->loaded[i].key); free(a->loaded[i].id); }
  free(a->loaded);
  free(a);
}
const qa_picture_asset *qa_app_weapon_hud_assets_picture(qa_app_weapon_hud_assets *a, qa_resource_id id) {
  for (size_t i = 0; i < a->picture_count; i++) if (strcmp(a->pictures[i].id, id) == 0) return &a->pictures[i].picture;
  return NULL;
}
qa_app_weapon_hud_refresh *qa_app_weapon_hud_assets_prepare_image_refresh(qa_app_weapon_hud_assets *a) {
  qa_app_weapon_hud_refresh *refresh = calloc(1, sizeof *refresh);
  if (!refresh) qa_range_error("Out of memory");
  refresh->assets = a;
  for (size_t i = 0; i < a->texture_icon_count; i++) {
    qa_weapon_hud_icon icon = a->texture_icons[i].icon;
    qa_app_weapon_hud_provider provider = a->host.provider(a->host.ctx, icon.resource.content);
    load_icon_into(a, icon, a->texture_icons[i].key, provider, true,
                   &refresh->pictures, &refresh->picture_count, &refresh->picture_cap,
                   &refresh->aspects, &refresh->aspect_count, &refresh->aspect_cap);
  }
  return refresh;
}
void qa_app_weapon_hud_refresh_publish(qa_app_weapon_hud_refresh *refresh) {
  qa_app_weapon_hud_assets *a = refresh->assets;
  for (size_t i = 0; i < refresh->picture_count; i++)
    put_picture(&a->pictures, &a->picture_count, &a->picture_cap, refresh->pictures[i].id, refresh->pictures[i].picture);
  for (size_t i = 0; i < refresh->aspect_count; i++)
    put_aspect(&a->aspects, &a->aspect_count, &a->aspect_cap, refresh->aspects[i].id, refresh->aspects[i].aspect);
  for (size_t i = 0; i < refresh->picture_count; i++) free(refresh->pictures[i].id);
  free(refresh->pictures);
  for (size_t i = 0; i < refresh->aspect_count; i++) free(refresh->aspects[i].id);
  free(refresh->aspects);
  free(refresh);
}
double qa_app_weapon_hud_assets_aspect(qa_app_weapon_hud_assets *a, const qa_resource_id *id) {
  if (id == NULL) return 1;
  for (size_t i = 0; i < a->aspect_count; i++) if (strcmp(a->aspects[i].id, *id) == 0) return a->aspects[i].aspect;
  const qa_picture_asset *picture = qa_app_weapon_hud_assets_picture(a, *id);
  if (picture != NULL && picture->kind == QA_PICTURE_IMAGE) {
    double w = 0, h = 0;
    if (a->host.image_size && a->host.image_size(a->host.ctx, picture->image.image, &w, &h)) return w / h;
    return 1;
  }
  if (picture != NULL && picture->kind == QA_PICTURE_MATERIAL) {
    double aspect = 1;
    if (a->host.material_aspect && a->host.material_aspect(a->host.ctx, &picture->material, &aspect)) return aspect;
  }
  return 1;
}
qa_resource_id qa_app_weapon_hud_assets_load(qa_app_weapon_hud_assets *a, qa_weapon_hud_icon icon) {
  char *key = icon_key(icon);
  for (size_t i = 0; i < a->loaded_count; i++)
    if (strcmp(a->loaded[i].key, key) == 0) { qa_resource_id id = a->loaded[i].id; free(key); return id; }
  qa_app_weapon_hud_provider unused; memset(&unused, 0, sizeof unused);
  qa_resource_id id = load_icon_into(a, icon, key, unused, false,
                                     &a->pictures, &a->picture_count, &a->picture_cap,
                                     &a->aspects, &a->aspect_count, &a->aspect_cap);
  free(key);
  return id;
}
qa_app_weapon_hud_prepared qa_app_weapon_hud_assets_prepare(qa_app_weapon_hud_assets *a, const qa_weapon_hud_status *status) {
  qa_app_weapon_hud_prepared out; memset(&out, 0, sizeof out);
  if (status == NULL) return out;
  const qa_catalog_product_expectation *expectation = a->host.expectation(a->host.ctx, status->source.content);
  qa_weapon_hud_icons icons; memset(&icons, 0, sizeof icons);
  bool found = false;
  if (!qa_weapon_hud_icons_for(status->source, expectation, status->item, &icons, &found)) qa_range_error("Out of memory");
  if (!found) { qa_weapon_hud_icons_free(&icons); return out; }
  const qa_weapon_hud_icon *weapon_icon = icons.has_selected_weapon ? &icons.selected_weapon : icons.has_weapon ? &icons.weapon : NULL;
  if (weapon_icon != NULL) { out.weapon = qa_app_weapon_hud_assets_load(a, *weapon_icon); out.has_weapon = true; }
  if (icons.has_ammo) { out.ammo = qa_app_weapon_hud_assets_load(a, icons.ammo); out.has_ammo = true; }
  qa_weapon_hud_icons_free(&icons);
  return out;
}
