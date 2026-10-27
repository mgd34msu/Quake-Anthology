/*
 * Port of src/text/mounted.ts: mounted-content font loading and the
 * retained font file reader.
 * C accommodations:
 *  - MountedContent (content/mounts, unported) and SceneImageRegistry
 *    (render/scene, unported) are the minimal service shapes this file
 *    uses: open() yields bytes borrowed from the service plus the
 *    resolved reference; register() takes the RenderImage and the
 *    exact sampling the TS passes ({wrap:"clamp", filter:"linear"}).
 *    The ports of those modules will supply these tables.
 *  - createMountedTextFonts initializes a TextFontRegistry over a
 *    caller-owned qa_mounted_text_fonts, which must outlive the
 *    registry (its services ctx points into it). Reads copy the
 *    opened bytes, as the TS hands the Uint8Array to the registry.
 *  - MountedFontReader caches one opened copy per path (the TS caches
 *    the open promise, null included). Retained files own a byte copy
 *    (the TS slices), freed by free_file or close. The extra TS
 *    `reference` field on retained files is outside the
 *    RetainedFontFile interface and has no C counterpart.
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_MOUNTED_H
#define QA_TEXT_MOUNTED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/text/atlas.h"
#include "qa/text/draw2d.h"

/* TS OpenedResource: bytes are borrowed from the content service. */
typedef struct qa_opened_resource {
  qa_resolved_resource_reference reference;
  qa_byte_view bytes;
} qa_opened_resource;

/* The MountedContent surface mounted.ts uses. open false is TS null
 * (also false on OOM inside a service). */
typedef struct qa_mounted_content {
  void *ctx;
  bool (*open)(void *ctx, const char *path, qa_opened_resource *out);
} qa_mounted_content;

/* The SceneImageRegistry surface mounted.ts uses. */
typedef struct qa_scene_image_registry {
  void *ctx;
  qa_renderer_image (*register_image)(void *ctx, const char *name,
                                      const qa_render_image *content,
                                      qa_texture_sampling sampling);
  void (*release)(void *ctx, qa_renderer_image image);
} qa_scene_image_registry;

/* Backing storage for the services qa_create_mounted_text_fonts
 * builds; keep alive while the registry is in use. */
typedef struct qa_mounted_text_fonts {
  qa_mounted_content content;
  qa_scene_image_registry images;
} qa_mounted_text_fonts;

/* TS createMountedTextFonts. */
void qa_create_mounted_text_fonts(qa_mounted_text_fonts *mounted,
                                  qa_mounted_content content,
                                  qa_scene_image_registry images,
                                  qa_text_font_registry *out);

typedef struct qa_mounted_file_entry {
  char *path; /* owned */
  bool found;
  uint8_t *bytes; /* owned copy */
  size_t length;
  qa_resolved_resource_reference reference;
} qa_mounted_file_entry;

/* TS MountedFontReader implements FontFileReader. */
typedef struct qa_mounted_font_reader {
  qa_mounted_content content;
  qa_mounted_file_entry *files; /* owned */
  size_t file_count;
  size_t file_capacity;
  const uint8_t **retained; /* owned byte copies, by identity */
  size_t retained_count;
  size_t retained_capacity;
} qa_mounted_font_reader;

void qa_mounted_font_reader_init(qa_mounted_font_reader *reader,
                                 qa_mounted_content content);
/* TS close: drops retained files and the open cache. */
void qa_mounted_font_reader_close(qa_mounted_font_reader *reader);
/* TS readFileLength / readFileRetained / freeFile. */
double qa_mounted_font_reader_read_file_length(qa_mounted_font_reader *reader,
                                               const char *path);
bool qa_mounted_font_reader_read_file_retained(qa_mounted_font_reader *reader,
                                               const char *path,
                                               qa_retained_font_file *out);
void qa_mounted_font_reader_free_file(qa_mounted_font_reader *reader,
                                      qa_retained_font_file file);
/* Views the reader as a FontFileReader service table. */
qa_font_file_reader qa_mounted_font_reader_as_reader(
    qa_mounted_font_reader *reader);

#endif
