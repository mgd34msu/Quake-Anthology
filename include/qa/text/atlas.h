/*
 * Port of src/text/atlas.ts: classic charset, kfont and TrueType glyph
 * atlases, the per-lifetime TextFontRegistry, and glyph resolution.
 * C accommodations:
 *  - TextAtlas glyphs (TS ReadonlyMap) become an owned entry array.
 *    The atlas owns its name; picture.name borrows it. capInk presence
 *    is has_cap_ink. kind strings are qa_text_atlas_kind.
 *  - Async services are synchronous function pointers: read returns a
 *    malloc'd buffer (caller/registry frees) or NULL for TS null;
 *    register_image receives the same RenderImage the TS builds.
 *  - The TS promise cache becomes a synchronous result cache: a cached
 *    entry is a registry-owned atlas or a cached null. Loads return
 *    borrowed atlases valid until qa_text_font_registry_close, NULL for
 *    TS null; a separate false return marks OOM (no TS counterpart).
 *    Thrown TS Errors abort with the same message, as elsewhere.
 *  - loadTrueTypePages returns a malloc'd array of borrowed atlases;
 *    the caller frees the array only.
 *  - Codepoints are doubles at the validated boundaries (the TS checks
 *    Number.isInteger and the Unicode scalar range), int64 inside the
 *    TrueType port. textCodepoints decodes UTF-8 bytes where the TS
 *    iterates UTF-16 code points; results agree on valid text.
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_ATLAS_H
#define QA_TEXT_ATLAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/render.h"
#include "qa/text/draw2d.h"

typedef struct qa_atlas_glyph {
  double x, y, width, height, advance;
  bool color;
} qa_atlas_glyph;

typedef struct qa_text_glyph_entry {
  double codepoint;
  qa_atlas_glyph glyph;
} qa_text_glyph_entry;

typedef enum qa_text_atlas_kind {
  QA_TEXT_ATLAS_CLASSIC,  /* "classic" */
  QA_TEXT_ATLAS_KFONT,    /* "kfont" */
  QA_TEXT_ATLAS_TRUETYPE  /* "truetype" */
} qa_text_atlas_kind;

/* TS TextAtlas. Owns name and glyphs. */
typedef struct qa_text_atlas {
  qa_text_atlas_kind kind;
  char *name; /* owned */
  qa_picture_asset picture; /* image kind; picture.image.name == name */
  double line_height;
  bool has_cap_ink;
  double cap_top;
  double cap_height;
  qa_text_glyph_entry *glyphs; /* owned */
  size_t glyph_count;
  size_t glyph_capacity;
} qa_text_atlas;

void qa_text_atlas_destroy(qa_text_atlas *atlas);
/* Borrowed glyph, or NULL (TS Map.get undefined). */
const qa_atlas_glyph *qa_text_atlas_glyph(const qa_text_atlas *atlas,
                                          double codepoint);

/* TS TextFontSelection. is_atlas false is the "classic" kind, with
 * unicode NULL allowed; true is "atlas", with font set and optional
 * borrowed fallbacks. */
typedef struct qa_text_font_selection {
  bool is_atlas;
  const qa_text_atlas *font;    /* atlas kind */
  const qa_text_atlas *classic; /* both kinds */
  const qa_text_atlas *unicode; /* classic kind, may be NULL */
  const qa_text_atlas *const *fallbacks;
  size_t fallback_count;
} qa_text_font_selection;

/* TS FontImageServices. */
typedef struct qa_font_image_services {
  void *ctx;
  uint8_t *(*read)(void *ctx, const char *path, size_t *out_length);
  qa_renderer_image (*register_image)(void *ctx, const char *name,
                                      const qa_render_image *content);
  void (*release_image)(void *ctx, qa_renderer_image image);
} qa_font_image_services;

/* TS TextFontRequest. unicode_font NULL is TS null; codepoints are the
 * TrueType coverage request values (validated in the loader). */
typedef enum qa_text_font_request_kind {
  QA_FONT_REQUEST_CLASSIC,
  QA_FONT_REQUEST_KFONT,
  QA_FONT_REQUEST_TRUETYPE
} qa_text_font_request_kind;
typedef struct qa_text_font_request {
  qa_text_font_request_kind kind;
  const char *unicode_font; /* classic */
  const char *path;         /* kfont / truetype */
  double pixel_size;        /* truetype */
  const double *codepoints; /* truetype */
  size_t codepoint_count;
} qa_text_font_request;

/* TS classicCharset. color_baked false is the "tinted" policy; name
 * NULL is the TS default "conchars". False only on OOM. */
bool qa_classic_charset(qa_renderer_image image, const char *name,
                        bool color_baked, qa_text_atlas *out);

typedef struct qa_font_cache_entry {
  char *key;                 /* owned */
  qa_text_atlas *font;        /* owned; NULL with is_null */
  bool is_null;
} qa_font_cache_entry;

/* TS TextFontRegistry: one mounted-content and renderer lifetime. */
typedef struct qa_text_font_registry {
  qa_font_image_services services;
  qa_font_cache_entry *cache;
  size_t cache_count;
  size_t cache_capacity;
  qa_renderer_image *images;
  size_t image_count;
  size_t image_capacity;
  bool closed;
} qa_text_font_registry;

void qa_text_font_registry_init(qa_text_font_registry *registry,
                                qa_font_image_services services);
/* TS select. False only on OOM; *out borrows registry atlases. */
bool qa_text_font_registry_select(qa_text_font_registry *registry,
                                  const qa_text_font_request *request,
                                  const qa_text_atlas *classic,
                                  qa_text_font_selection *out);
/* TS loadKfont / loadTrueType. NULL is TS null. */
const qa_text_atlas *qa_text_font_registry_load_kfont(
    qa_text_font_registry *registry, const char *path, bool *oom);
const qa_text_atlas *qa_text_font_registry_load_truetype(
    qa_text_font_registry *registry, const char *path, double pixel_size,
    const double *codepoints, size_t codepoint_count, bool *oom);
/* TS loadTrueTypePages. *out is a malloc'd borrowed-atlas array. */
bool qa_text_font_registry_load_truetype_pages(
    qa_text_font_registry *registry, const char *path, double pixel_size,
    const double *codepoints, size_t codepoint_count,
    const qa_text_atlas ***out, size_t *out_count);
/* TS close: releases every registered image and clears the cache. */
void qa_text_font_registry_close(qa_text_font_registry *registry);

/* TS ResolvedTextGlyph: glyph is a copy of the atlas entry. */
typedef struct qa_resolved_text_glyph {
  const qa_text_atlas *atlas;
  qa_atlas_glyph glyph;
  double codepoint;
  bool visible;
} qa_resolved_text_glyph;

/* TS resolveTextGlyph / glyphUv / textCodepoints. */
qa_resolved_text_glyph qa_resolve_text_glyph(
    const qa_text_font_selection *selection, double codepoint,
    bool alternate);
qa_text_texture_rect qa_glyph_uv(const qa_resolved_text_glyph *glyph);
/* Malloc'd distinct codepoints in first-appearance order (UTF-8
 * input); caller frees. NULL only on OOM. */
int64_t *qa_text_codepoints(const char *text, size_t length,
                            size_t *out_count);

#endif
