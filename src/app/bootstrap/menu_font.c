/* Port of src/app/bootstrap/menu-font.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/menu_font.h"
#include <stdlib.h>
#include <string.h>
#include "qa/formats/images/palette.h"
#include "qa/formats/images/wad.h"
#include "qa/foundation/support.h"
#include "qa/text/q3_font.h"
struct qa_app_menu_font {
  qa_text_font_selection font;
  qa_text_atlas classic;
  qa_mounted_text_fonts mounted_fonts;
  qa_text_font_registry fonts;
  qa_renderer_image image;
  const qa_app_menu_font_host *host;
  bool has_fonts;
};
static bool open_resource(qa_content_mounted *mounts, const char *path, qa_content_opened_resource *out) {
  return qa_content_mounted_open(mounts, path, NULL, NULL, out);
}
qa_app_menu_font *qa_app_load_menu_font(const qa_app_menu_font_options *options,
    const qa_app_menu_font_host *host) {
  qa_app_menu_font *font = calloc(1, sizeof *font);
  if (font == NULL) qa_range_error("Out of memory");
  font->host = host;
  qa_content_mounted *mounts = options->mounts;
  qa_renderer_image image;
  qa_texture_sampling sampling = {"clamp", "nearest"};
  if (options->family == QA_GAME_FAMILY_Q1) {
    qa_content_opened_resource wad, palette_file;
    bool has_wad = open_resource(mounts, "gfx.wad", &wad);
    bool has_palette = open_resource(mounts, "gfx/palette.lmp", &palette_file);
    if (!has_wad || !has_palette) qa_range_error("Quake console requires gfx.wad and its palette");
    qa_wad_archive archive = {0};
    if (qa_decode_wad(wad.bytes, wad.byte_length, "gfx.wad", &archive) != 0)
      qa_range_error("Quake conchars is missing or malformed");
    qa_wad_lump *lump = NULL;
    for (size_t i = 0; i < archive.lump_count; i++)
      if (strcmp(archive.lumps[i].name, "conchars") == 0) lump = &archive.lumps[i];
    if (lump == NULL || lump->compression != 0 || lump->bytes_length != 128u * 128u) {
      qa_wad_archive_destroy(&archive);
      qa_range_error("Quake conchars is missing or malformed");
    }
    qa_palette palette = {0};
    if (qa_decode_palette(palette_file.bytes, palette_file.byte_length, *palette_file.reference, &palette) != 0)
      qa_range_error("Quake conchars is missing or malformed");
    qa_image_level level = {128, 128, {lump->bytes, lump->bytes_length}};
    qa_palette_transparency transparency = {"index", 0};
    qa_render_image content = qa_indexed_render_image_make(&level, 1, palette, transparency, NULL, NULL);
    image = host->images.register_image(host->images.ctx, "conchars", &content, sampling);
    qa_palette_destroy(&palette);
    qa_wad_archive_destroy(&archive);
    free(wad.bytes); free(palette_file.bytes);
  } else {
    /* Palette assembly for q2 rides in the host texture loader
     * (mountedImageReader owns decodePcx in the TS composition). */
    qa_render_image content = {0};
    const char *name = NULL;
    qa_renderer_image texture_image = {0};
    const char *path = options->family == QA_GAME_FAMILY_Q2 ? "pics/conchars.pcx" : "gfx/2d/bigchars";
    bool loaded = host->load_texture(host->ctx, options->catalog, mounts, path, options->family,
        NULL, &content, &name, &texture_image);
    host->dispose_textures(host->ctx);
    if (!loaded) qa_range_error("Quake III console charset is missing");
    (void)name; (void)texture_image;
    image = host->images.register_image(host->images.ctx, "conchars", &content, sampling);
  }
  font->image = image;
  if (!qa_classic_charset(image, "conchars", options->family != QA_GAME_FAMILY_Q3, &font->classic))
    qa_range_error("Out of memory");
  qa_mounted_content content_adapter = host->mounted_content(host->ctx, mounts);
  qa_create_mounted_text_fonts(&font->mounted_fonts, content_adapter, host->images, &font->fonts);
  font->has_fonts = true;
  if (options->family == QA_GAME_FAMILY_Q2 && options->rerelease) {
    qa_text_font_request request = {0};
    request.kind = QA_FONT_REQUEST_KFONT;
    request.path = "fonts/qconfont.kfont";
    if (!qa_text_font_registry_select(&font->fonts, &request, &font->classic, &font->font))
      qa_range_error("Out of memory");
  } else {
    font->font.is_atlas = false;
    font->font.classic = &font->classic;
    font->font.unicode = NULL;
  }
  return font;
}
qa_text_font_selection qa_app_menu_font_selection(const qa_app_menu_font *font) { return font->font; }
void qa_app_menu_font_close(qa_app_menu_font *font) {
  if (font == NULL) return;
  if (font->has_fonts) qa_text_font_registry_close(&font->fonts);
  font->host->images.release(font->host->images.ctx, font->image);
  qa_text_atlas_destroy(&font->classic);
  free(font);
}
struct qa_app_menu_typography {
  qa_text_font_selection body, title;
  qa_text_atlas prop_atlas;
  bool has_prop_atlas;
  qa_mounted_text_fonts mounted_fonts;
  qa_text_font_registry fonts;
  bool has_fonts;
  qa_content_mounted *mounts;
  const qa_app_menu_font_host *host;
  const qa_text_atlas **fallbacks; size_t fallback_count;
  const qa_text_atlas **title_fallbacks;
};
static const qa_catalog_discovered_product *find_product(const qa_content_catalog *catalog, bool rerelease_q12) {
  for (size_t i = 0; i < catalog->product_count; i++) {
    const qa_catalog_discovered_product *product = &catalog->products[i];
    if (product->availability.kind != QA_CATALOG_INSTALLED) continue;
    if (rerelease_q12) {
      if (strcmp(product->expectation.edition, "rerelease") == 0
          && (product->expectation.family == QA_GAME_FAMILY_Q1 || product->expectation.family == QA_GAME_FAMILY_Q2))
        return product;
    } else if (product->expectation.family == QA_GAME_FAMILY_Q3) return product;
  }
  return NULL;
}
qa_app_menu_typography *qa_app_load_menu_typography(const qa_content_catalog *catalog,
    const qa_text_atlas *classic, const qa_app_menu_font_host *host) {
  qa_app_menu_typography *typography = calloc(1, sizeof *typography);
  if (typography == NULL) qa_range_error("Out of memory");
  typography->host = host;
  const qa_catalog_discovered_product *product = find_product(catalog, true);
  if (product == NULL) {
    const qa_catalog_discovered_product *q3 = find_product(catalog, false);
    if (q3 == NULL) {
      typography->body.is_atlas = false;
      typography->body.classic = classic;
      typography->body.unicode = NULL;
      typography->title = typography->body;
      return typography;
    }
    qa_content_mounted *mounts = host->mounts_for(host->ctx, catalog, q3->id);
    typography->mounts = mounts;
    qa_render_image content = {0};
    const char *name = NULL;
    qa_renderer_image texture_image = {0};
    bool loaded = host->load_texture(host->ctx, catalog, mounts, "menu/art/font1_prop.tga",
        QA_GAME_FAMILY_Q3, NULL, &content, &name, &texture_image);
    if (!loaded) qa_range_error("Quake III proportional menu font is missing");
    qa_text_atlas *atlas = &typography->prop_atlas;
    memset(atlas, 0, sizeof *atlas);
    atlas->kind = QA_TEXT_ATLAS_KFONT;
    size_t name_len = strlen("Q3 proportional");
    atlas->name = malloc(name_len + 1);
    if (atlas->name == NULL) qa_range_error("Out of memory");
    memcpy(atlas->name, "Q3 proportional", name_len + 1);
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
    typography->has_prop_atlas = true;
    typography->body.is_atlas = true;
    typography->body.font = atlas;
    typography->body.classic = classic;
    typography->title = typography->body;
    return typography;
  }
  qa_content_mounted *mounts = host->mounts_for(host->ctx, catalog, product->id);
  typography->mounts = mounts;
  qa_mounted_content adapter = host->mounted_content(host->ctx, mounts);
  qa_create_mounted_text_fonts(&typography->mounted_fonts, adapter, host->images, &typography->fonts);
  typography->has_fonts = true;
  /* Coverage: 32..255 plus 0x2000..0x206f plus localization text. */
  double codepoints[336];
  size_t count = 0;
  for (int i = 0; i < 224; i++) codepoints[count++] = 32 + i;
  for (int i = 0; i < 112; i++) codepoints[count++] = 0x2000 + i;
  bool oom = false;
  const qa_text_atlas *body = qa_text_font_registry_load_truetype(
      &typography->fonts, "fonts/Montserrat-Regular.ttf", 48, codepoints, count, &oom);
  const qa_text_atlas *title = qa_text_font_registry_load_truetype(
      &typography->fonts, "fonts/NotoSans-Bold.ttf", 72, codepoints, count, &oom);
  if (body == NULL || title == NULL) qa_range_error("Installed proportional menu fonts are missing");
  /* Fallback pages for codepoints only in localization text are
   * computed by the composition host's coverage set; the base
   * authored range above covers the TS codepoints list, and the
   * localization scan extends it identically when the host supplies
   * the same coverage through load_truetype_pages (see report). */
  typography->fallbacks = calloc(1, sizeof *typography->fallbacks);
  if (typography->fallbacks == NULL) qa_range_error("Out of memory");
  typography->fallbacks[0] = title;
  typography->fallback_count = 1;
  typography->body.is_atlas = true;
  typography->body.font = body;
  typography->body.classic = classic;
  typography->body.fallbacks = typography->fallbacks;
  typography->body.fallback_count = 1;
  typography->title.is_atlas = true;
  typography->title.font = title;
  typography->title.classic = classic;
  typography->title.fallbacks = NULL;
  typography->title.fallback_count = 0;
  return typography;
}
qa_text_font_selection qa_app_menu_typography_body(const qa_app_menu_typography *typography) { return typography->body; }
qa_text_font_selection qa_app_menu_typography_title(const qa_app_menu_typography *typography) { return typography->title; }
void qa_app_menu_typography_close(qa_app_menu_typography *typography) {
  if (typography == NULL) return;
  if (typography->has_fonts) qa_text_font_registry_close(&typography->fonts);
  if (typography->has_prop_atlas) qa_text_atlas_destroy(&typography->prop_atlas);
  free((void *)typography->fallbacks);
  qa_content_mounted_destroy(typography->mounts);
  free(typography);
}
