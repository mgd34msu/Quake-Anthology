/* Port of src/app/bootstrap/menu-font.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/menu_font.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/image_reader.h"
#include "qa/content/roots.h"
#include <stdio.h>
#include "qa/formats/images/indexed.h"
#include "qa/formats/images/palette.h"
#include "qa/formats/images/wad.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/textures.h"
#include "qa/text/mounted.h"
#include "qa/text/q3_font.h"

/* ---- MountedContent / SceneImageRegistry views for createMountedTextFonts ---------- */
typedef struct mounted_view {
  qa_content_mounted *mounts;
  uint8_t *bytes; /* the last opened resource's bytes (borrowed by the text fonts until the next open) */
  qa_scene_image_registry *images;
} mounted_view;
static bool view_open(void *ctx, const char *path, qa_opened_resource *out) {
  mounted_view *view = ctx;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(view->mounts, path, NULL, NULL, &opened)) return false;
  free(view->bytes);
  view->bytes = opened.bytes;
  out->reference = *opened.reference;
  out->bytes.bytes = opened.bytes;
  out->bytes.length = opened.byte_length;
  return true;
}
static qa_renderer_image view_register(void *ctx, const char *name, const qa_render_image *content, qa_texture_sampling sampling) {
  mounted_view *view = ctx;
  return qa_scene_image_registry_register_generated(view->images, name, *content, sampling)->image;
}
static void view_release(void *ctx, qa_renderer_image image) {
  mounted_view *view = ctx;
  qa_scene_image_registry_release(view->images, image);
}
static mounted_view *view_open_fonts(qa_content_mounted *mounts, qa_scene_image_registry *images, qa_mounted_text_fonts *mounted,
    qa_text_font_registry *fonts) {
  mounted_view *view = calloc(1, sizeof *view);
  if (view == NULL) qa_range_error("Out of memory");
  view->mounts = mounts;
  view->images = images;
  qa_mounted_content content = {view, view_open};
  qa_mounted_scene_image_registry registry = {view, view_register, view_release};
  qa_create_mounted_text_fonts(mounted, content, registry, fonts);
  return view;
}
static void view_free(mounted_view *view) {
  if (view == NULL) return;
  free(view->bytes);
  free(view);
}

/* ---- new SceneTextureLoader(images, mountedImageReader(catalog, mounts), palette, { strict: true, policy? }) ---- */
typedef struct texture_slot {
  qa_app_scene_image_reader *reader;
  qa_scene_texture_loader loader;
  bool has_loader;
  qa_palette palette; bool has_palette;
} texture_slot;
static void texture_slot_open(texture_slot *slot, const qa_content_catalog *catalog, qa_content_mounted *mounts,
    qa_scene_image_registry *images, const qa_scene_image_policy *policy) {
  slot->reader = qa_app_scene_image_reader_open(catalog, mounts);
  qa_scene_texture_loader_options options;
  memset(&options, 0, sizeof options);
  options.strict = true;
  if (policy != NULL) { options.has_policy = true; options.policy = *policy; }
  if (!qa_scene_texture_loader_init(&slot->loader, images, qa_app_scene_image_reader_reader(slot->reader),
                                    slot->has_palette ? &slot->palette : NULL, &options))
    qa_range_error("Out of memory");
  slot->has_loader = true;
}
/* textures.load(path, { family, mipmap: false, wrap: "clamp" }) */
static const qa_scene_texture *texture_slot_load(texture_slot *slot, const char *path, qa_game_family family) {
  qa_scene_texture_load_options options;
  memset(&options, 0, sizeof options);
  options.has_mipmap = true;
  options.mipmap = false;
  options.wrap = "clamp";
  options.family = qa_game_family_name(family);
  return qa_scene_texture_loader_load(&slot->loader, path, &options);
}
/* The TS loader's decoded images die with GC; here the loader record is kept until the
 * font closes because the registered conchars content borrows its pixels. */
static void texture_slot_destroy(texture_slot *slot) {
  if (slot->has_loader) qa_scene_texture_loader_destroy(&slot->loader);
  slot->has_loader = false;
  qa_app_scene_image_reader_destroy(slot->reader);
  slot->reader = NULL;
  if (slot->has_palette) free((void *)slot->palette.colors.bytes);
  slot->has_palette = false;
}

/* ---- loadMenuFont ------------------------------------------------------------------ */
struct qa_app_menu_font {
  qa_text_font_selection font;
  qa_text_atlas classic; bool has_classic;
  qa_mounted_text_fonts mounted_fonts;
  qa_text_font_registry fonts; bool has_fonts;
  mounted_view *view;
  qa_scene_image_registry *images;
  qa_renderer_image image; bool has_image;
  texture_slot textures;
  uint8_t *conchars; /* the q1 conchars pixels the registered image borrows */
};
static void font_cleanup(void *arg) { qa_app_menu_font_close(arg); }
typedef struct q1_scope { qa_content_opened_resource wad, palette; bool has_wad, has_palette; qa_wad_archive archive; bool has_archive; } q1_scope;
static void q1_scope_free(void *arg) {
  q1_scope *scope = arg;
  if (scope->has_archive) qa_wad_archive_destroy(&scope->archive);
  if (scope->has_wad) free(scope->wad.bytes);
  if (scope->has_palette) free(scope->palette.bytes);
  memset(scope, 0, sizeof *scope);
}
static void load_q1_conchars(qa_app_menu_font *font, const qa_app_menu_font_options *options, qa_texture_sampling sampling) {
  q1_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, q1_scope_free, &scope);
  scope.has_wad = qa_content_mounted_open(options->mounts, "gfx.wad", NULL, NULL, &scope.wad);
  scope.has_palette = qa_content_mounted_open(options->mounts, "gfx/palette.lmp", NULL, NULL, &scope.palette);
  if (!scope.has_wad || !scope.has_palette) qa_raise("Error", 0, 0, "Quake console requires gfx.wad and its palette");
  if (!qa_decode_wad(scope.wad.bytes, scope.wad.byte_length, "gfx.wad", &scope.archive)) qa_range_error("Out of memory");
  scope.has_archive = true;
  const qa_wad_lump *lump = NULL;
  for (size_t i = 0; i < scope.archive.lump_count && lump == NULL; i++)
    if (strcmp(scope.archive.lumps[i].name, "conchars") == 0) lump = &scope.archive.lumps[i];
  if (lump == NULL || lump->compression != 0 || lump->bytes_length != 128u * 128u) qa_raise("Error", 0, 0, "Quake conchars is missing or malformed");
  qa_palette palette;
  memset(&palette, 0, sizeof palette);
  if (!qa_decode_palette(scope.palette.bytes, scope.palette.byte_length, *scope.palette.reference, &palette)) qa_range_error("Out of memory");
  font->conchars = malloc(lump->bytes_length);
  if (font->conchars == NULL) { qa_palette_destroy(&palette); qa_range_error("Out of memory"); }
  memcpy(font->conchars, lump->bytes, lump->bytes_length);
  qa_image_level level = {128, 128, {font->conchars, lump->bytes_length}};
  qa_palette_transparency transparency = {"index", 0};
  qa_render_image content = qa_indexed_render_image_make(&level, 1, palette, transparency, NULL, NULL);
  /* images.register("conchars", ..., { kind: "resource", resource: wad.reference }) */
  qa_renderer_image source;
  memset(&source, 0, sizeof source);
  source.source_is_resource = true;
  source.source_resource = *scope.wad.reference;
  font->image = qa_scene_image_registry_register(options->images, "conchars", content, sampling, source)->image;
  font->has_image = true;
  qa_palette_destroy(&palette);
  qa_cleanup_pop(&guard);
  q1_scope_free(&scope);
}
static void load_texture_conchars(qa_app_menu_font *font, const qa_app_menu_font_options *options, qa_texture_sampling sampling) {
  texture_slot *slot = &font->textures;
  if (options->family == QA_GAME_FAMILY_Q2) {
    /* paletteAsset = mounts.open("pics/colormap.pcx"); colors = decodePcx(...).palette */
    qa_content_opened_resource asset;
    if (qa_content_mounted_open(options->mounts, "pics/colormap.pcx", NULL, NULL, &asset)) {
      qa_cleanup guard;
      qa_cleanup_push(&guard, free, asset.bytes);
      qa_pcx_image pcx;
      memset(&pcx, 0, sizeof pcx);
      if (!qa_decode_pcx(asset.bytes, asset.byte_length, "pics/colormap.pcx", &pcx))
        qa_raise("Error", 0, 0, "pics/colormap.pcx is not a PCX image");
      qa_cleanup_pop(&guard);
      if (pcx.palette != NULL) {
        slot->palette.colors.bytes = pcx.palette;
        slot->palette.colors.length = pcx.palette_length;
        slot->palette.source = *asset.reference;
        slot->has_palette = true;
        pcx.palette = NULL;
      }
      qa_pcx_image_destroy(&pcx);
      free(asset.bytes);
    }
  }
  texture_slot_open(slot, options->catalog, options->mounts, options->images, options->image_policy);
  const qa_scene_texture *texture = texture_slot_load(slot, options->family == QA_GAME_FAMILY_Q2 ? "pics/conchars.pcx" : "gfx/2d/bigchars",
                                                      options->family);
  if (texture == NULL) {
    qa_scene_texture_loader_dispose_images(&slot->loader);
    qa_raise("Error", 0, 0, "Quake III console charset is missing");
  }
  /* images.register("conchars", texture.content, { wrap: "clamp", filter: "nearest" }, texture.image.source) */
  font->image = qa_scene_image_registry_register(options->images, "conchars", texture->content, sampling, texture->image)->image;
  font->has_image = true;
  /* finally { textures.disposeImages() } */
  qa_scene_texture_loader_dispose_images(&slot->loader);
}
qa_app_menu_font *qa_app_load_menu_font(const qa_app_menu_font_options *options) {
  qa_app_menu_font *font = calloc(1, sizeof *font);
  if (font == NULL) qa_range_error("Out of memory");
  font->images = options->images;
  qa_cleanup guard;
  qa_cleanup_push(&guard, font_cleanup, font);
  qa_texture_sampling sampling = {"clamp", "nearest"};
  if (options->family == QA_GAME_FAMILY_Q1) load_q1_conchars(font, options, sampling);
  else load_texture_conchars(font, options, sampling);
  if (!qa_classic_charset(font->image, "conchars", options->family != QA_GAME_FAMILY_Q3, &font->classic)) qa_range_error("Out of memory");
  font->has_classic = true;
  font->view = view_open_fonts(options->mounts, options->images, &font->mounted_fonts, &font->fonts);
  font->has_fonts = true;
  if (options->family == QA_GAME_FAMILY_Q2 && options->rerelease) {
    qa_text_font_request request;
    memset(&request, 0, sizeof request);
    request.kind = QA_FONT_REQUEST_KFONT;
    request.path = "fonts/qconfont.kfont";
    if (!qa_text_font_registry_select(&font->fonts, &request, &font->classic, &font->font)) qa_range_error("Out of memory");
  } else {
    memset(&font->font, 0, sizeof font->font);
    font->font.classic = &font->classic;
  }
  qa_cleanup_pop(&guard);
  return font;
}
qa_text_font_selection qa_app_menu_font_selection(const qa_app_menu_font *font) { return font->font; }
/* close() { fonts.close(); images.release(image); } */
void qa_app_menu_font_close(qa_app_menu_font *font) {
  if (font == NULL) return;
  if (font->has_fonts) qa_text_font_registry_close(&font->fonts);
  view_free(font->view);
  if (font->has_image) qa_scene_image_registry_release(font->images, font->image);
  if (font->has_classic) qa_text_atlas_destroy(&font->classic);
  texture_slot_destroy(&font->textures);
  free(font->conchars);
  free(font);
}

/* ---- loadMenuTypography ------------------------------------------------------------ */
struct qa_app_menu_typography {
  qa_text_font_selection body, title;
  qa_text_atlas prop_atlas; bool has_prop_atlas;
  texture_slot textures;
  qa_mounted_text_fonts mounted_fonts;
  qa_text_font_registry fonts; bool has_fonts;
  mounted_view *view;
  qa_content_mounted *mounted;
  qa_catalog_mount_list mounts; bool has_mounts;
  qa_mount_id *order;
  const qa_text_atlas **fallbacks; size_t fallback_count;
};
static const qa_catalog_discovered_product *find_installed(const qa_content_catalog *catalog, bool rerelease_q12) {
  for (size_t i = 0; i < catalog->product_count; i++) {
    const qa_catalog_discovered_product *product = &catalog->products[i];
    if (product->availability.kind != QA_CATALOG_INSTALLED) continue;
    if (rerelease_q12) {
      if (product->expectation.edition != NULL && strcmp(product->expectation.edition, "rerelease") == 0 &&
          (product->expectation.family == QA_GAME_FAMILY_Q1 || product->expectation.family == QA_GAME_FAMILY_Q2))
        return product;
    } else if (product->expectation.family == QA_GAME_FAMILY_Q3) return product;
  }
  return NULL;
}
/* catalog.mountsFor(id) + openMountPlan({ id: "mount-plan:menu:typography", mounts, defaultOrder, prefixOrders: [] }) */
static void open_typography_mounts(qa_app_menu_typography *typography, qa_content_catalog *catalog, const char *product) {
  if (!qa_content_catalog_mounts_for(catalog, product, &typography->mounts)) qa_range_error("Out of memory");
  typography->has_mounts = true;
  size_t count = typography->mounts.count;
  typography->order = calloc(count != 0 ? count : 1, sizeof *typography->order);
  if (typography->order == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    const qa_content_mount *mount = &typography->mounts.mounts[i];
    typography->order[i] = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? mount->as.archive.identity.id : mount->as.loose.identity.id;
  }
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:menu:typography";
  plan.mounts = typography->mounts.mounts;
  plan.mount_count = count;
  plan.default_order = typography->order;
  plan.default_order_count = count;
  typography->mounted = qa_open_mount_plan(&plan, NULL);
  if (typography->mounted == NULL) qa_range_error("Out of memory");
}
static void typography_cleanup(void *arg) { qa_app_menu_typography_close(arg); }
static void load_q3_typography(qa_app_menu_typography *typography, qa_content_catalog *catalog, qa_scene_image_registry *images,
    const qa_text_atlas *classic, const qa_scene_image_policy *policy, const qa_catalog_discovered_product *q3) {
  open_typography_mounts(typography, catalog, q3->id);
  texture_slot_open(&typography->textures, catalog, typography->mounted, images, policy);
  const qa_scene_texture *texture = texture_slot_load(&typography->textures, "menu/art/font1_prop.tga", QA_GAME_FAMILY_Q3);
  if (texture == NULL) qa_raise("Error", 0, 0, "Quake III proportional menu font is missing");
  qa_text_atlas *atlas = &typography->prop_atlas;
  memset(atlas, 0, sizeof *atlas);
  atlas->kind = QA_TEXT_ATLAS_KFONT;
  atlas->name = malloc(sizeof "Q3 proportional");
  if (atlas->name == NULL) qa_range_error("Out of memory");
  memcpy(atlas->name, "Q3 proportional", sizeof "Q3 proportional");
  typography->has_prop_atlas = true;
  atlas->picture.kind = QA_PICTURE_IMAGE;
  atlas->picture.image.name = texture->name;
  atlas->picture.image.image = texture->image;
  atlas->line_height = 27;
  atlas->glyph_capacity = 95;
  atlas->glyphs = calloc(95, sizeof *atlas->glyphs);
  if (atlas->glyphs == NULL) qa_range_error("Out of memory");
  for (int code = 32; code < 127; code++) {
    qa_q3_atlas_metric metric = qa_q3_prop_metric(code);
    qa_text_glyph_entry *entry = &atlas->glyphs[atlas->glyph_count++];
    entry->codepoint = code;
    entry->glyph.x = metric.values[0];
    entry->glyph.y = metric.values[1];
    entry->glyph.width = metric.values[2];
    entry->glyph.height = 27;
    entry->glyph.advance = metric.values[2] + 3;
    entry->glyph.color = false;
  }
  memset(&typography->body, 0, sizeof typography->body);
  typography->body.is_atlas = true;
  typography->body.font = atlas;
  typography->body.classic = classic;
  typography->title = typography->body;
}
static bool atlas_has(const qa_text_atlas *atlas, double codepoint) { return qa_text_atlas_glyph(atlas, codepoint) != NULL; }
static void push_fallback(qa_app_menu_typography *typography, const qa_text_atlas *atlas) {
  const qa_text_atlas **grown = realloc(typography->fallbacks, (typography->fallback_count + 1) * sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  typography->fallbacks = grown;
  typography->fallbacks[typography->fallback_count++] = atlas;
}
typedef struct coverage_scope { double *points; size_t count, capacity; char **names; size_t name_count; double *missing; } coverage_scope;
static void coverage_free(void *arg) {
  coverage_scope *scope = arg;
  free(scope->points);
  if (scope->names != NULL) qa_content_free_string_list(scope->names, scope->name_count);
  free(scope->missing);
  memset(scope, 0, sizeof *scope);
}
static void coverage_add(coverage_scope *scope, double point) {
  for (size_t i = 0; i < scope->count; i++)
    if (scope->points[i] == point) return;
  if (scope->count == scope->capacity) {
    scope->capacity = scope->capacity != 0 ? scope->capacity * 2 : 512;
    double *grown = realloc(scope->points, scope->capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    scope->points = grown;
  }
  scope->points[scope->count++] = point;
}
static void load_installed_typography(qa_app_menu_typography *typography, qa_content_catalog *catalog, qa_scene_image_registry *images,
    const qa_text_atlas *classic, const qa_catalog_discovered_product *product) {
  open_typography_mounts(typography, catalog, product->id);
  typography->view = view_open_fonts(typography->mounted, images, &typography->mounted_fonts, &typography->fonts);
  typography->has_fonts = true;
  double codepoints[336];
  size_t count = 0;
  for (int i = 0; i < 224; i++) codepoints[count++] = 32 + i;
  for (int i = 0; i < 112; i++) codepoints[count++] = 0x2000 + i;
  coverage_scope coverage;
  memset(&coverage, 0, sizeof coverage);
  qa_cleanup guard;
  qa_cleanup_push(&guard, coverage_free, &coverage);
  for (size_t i = 0; i < count; i++) coverage_add(&coverage, codepoints[i]);
  coverage.names = qa_content_mounted_list_files(typography->mounted, "localization", ".txt", &coverage.name_count);
  if (coverage.names == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < coverage.name_count; i++) {
    char path[1024];
    snprintf(path, sizeof path, "localization/%s", coverage.names[i]);
    qa_content_opened_resource resource;
    if (!qa_content_mounted_open(typography->mounted, path, NULL, NULL, &resource)) continue;
    size_t point_count = 0;
    int64_t *points = qa_text_codepoints((const char *)resource.bytes, resource.byte_length, &point_count);
    free(resource.bytes);
    if (points == NULL) qa_range_error("Out of memory");
    for (size_t j = 0; j < point_count; j++)
      if (points[j] >= 32) coverage_add(&coverage, (double)points[j]);
    free(points);
  }
  bool oom = false;
  const qa_text_atlas *body = qa_text_font_registry_load_truetype(&typography->fonts, "fonts/Montserrat-Regular.ttf", 48, codepoints, count, &oom);
  if (oom) qa_range_error("Out of memory");
  const qa_text_atlas *title = qa_text_font_registry_load_truetype(&typography->fonts, "fonts/NotoSans-Bold.ttf", 72, codepoints, count, &oom);
  if (oom) qa_range_error("Out of memory");
  if (body == NULL || title == NULL) qa_raise("Error", 0, 0, "Installed proportional menu fonts are missing");
  push_fallback(typography, title);
  static const char *const paths[] = {"fonts/NotoSans-Bold.ttf", "fonts/NotoSansJP-Regular.otf", "fonts/NotoSansKR-Regular.otf"};
  coverage.missing = calloc(coverage.count != 0 ? coverage.count : 1, sizeof *coverage.missing);
  if (coverage.missing == NULL) qa_range_error("Out of memory");
  for (size_t p = 0; p < 3; p++) {
    size_t missing = 0;
    for (size_t i = 0; i < coverage.count; i++) {
      double point = coverage.points[i];
      if (atlas_has(body, point)) continue;
      bool covered = false;
      for (size_t f = 0; f < typography->fallback_count && !covered; f++) covered = atlas_has(typography->fallbacks[f], point);
      if (!covered) coverage.missing[missing++] = point;
    }
    if (missing == 0) break;
    const qa_text_atlas **pages = NULL;
    size_t page_count = 0;
    if (!qa_text_font_registry_load_truetype_pages(&typography->fonts, paths[p], 48, coverage.missing, missing, &pages, &page_count))
      qa_range_error("Out of memory");
    for (size_t i = 0; i < page_count; i++) push_fallback(typography, pages[i]);
    free((void *)pages);
  }
  qa_cleanup_pop(&guard);
  coverage_free(&coverage);
  memset(&typography->body, 0, sizeof typography->body);
  typography->body.is_atlas = true;
  typography->body.font = body;
  typography->body.classic = classic;
  typography->body.fallbacks = typography->fallbacks;
  typography->body.fallback_count = typography->fallback_count;
  memset(&typography->title, 0, sizeof typography->title);
  typography->title.is_atlas = true;
  typography->title.font = title;
  typography->title.classic = classic;
  typography->title.fallbacks = typography->fallbacks + 1;
  typography->title.fallback_count = typography->fallback_count - 1;
}
qa_app_menu_typography *qa_app_load_menu_typography(qa_content_catalog *catalog, qa_scene_image_registry *images,
    const qa_text_atlas *classic, const qa_scene_image_policy *image_policy) {
  qa_app_menu_typography *typography = calloc(1, sizeof *typography);
  if (typography == NULL) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, typography_cleanup, typography);
  const qa_catalog_discovered_product *product = find_installed(catalog, true);
  if (product == NULL) {
    const qa_catalog_discovered_product *q3 = find_installed(catalog, false);
    if (q3 == NULL) {
      memset(&typography->body, 0, sizeof typography->body);
      typography->body.classic = classic;
      typography->title = typography->body;
    } else load_q3_typography(typography, catalog, images, classic, image_policy, q3);
  } else load_installed_typography(typography, catalog, images, classic, product);
  qa_cleanup_pop(&guard);
  return typography;
}
qa_text_font_selection qa_app_menu_typography_body(const qa_app_menu_typography *typography) { return typography->body; }
qa_text_font_selection qa_app_menu_typography_title(const qa_app_menu_typography *typography) { return typography->title; }
/* close(): Q3 { textures.disposeImages(); mounted.close() }; installed { fonts.close(); mounted.close() }. */
void qa_app_menu_typography_close(qa_app_menu_typography *typography) {
  if (typography == NULL) return;
  if (typography->textures.has_loader) qa_scene_texture_loader_dispose_images(&typography->textures.loader);
  if (typography->has_fonts) qa_text_font_registry_close(&typography->fonts);
  view_free(typography->view);
  if (typography->mounted != NULL) {
    qa_content_mounted_close(typography->mounted);
    qa_content_mounted_destroy(typography->mounted);
  }
  texture_slot_destroy(&typography->textures);
  if (typography->has_prop_atlas) qa_text_atlas_destroy(&typography->prop_atlas);
  if (typography->has_mounts) qa_content_catalog_mount_list_free(&typography->mounts);
  free(typography->order);
  free((void *)typography->fallbacks);
  free(typography);
}
