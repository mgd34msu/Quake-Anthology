/*
 * Port of src/platform/freetype.ts: FreeTypeFontLibrary owning font
 * bytes and native faces, the Q3 outline raster contract
 * (id Software code/renderer/tr_font.c) and FreeType's normal bitmap
 * renderer. C accommodation: faces are opaque handles owned by the
 * library (the TypeScript checks object identity the same way);
 * Printf-style callbacks become fn+ctx (NULL drops output); the
 * native records the TypeScript reads by byte offset are read here
 * through <ft2build.h> structs. Bitmaps are malloc'd and owned.
 * GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_FREETYPE_H
#define QA_PLATFORM_FREETYPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*qa_freetype_print_fn)(const char *text, void *ctx);

typedef struct qa_freetype_face qa_freetype_face;

typedef struct qa_freetype_font_library {
  void *handle;                    /* FT_Library; NULL when closed */
  struct qa_freetype_face *faces;  /* owned face records */
  size_t face_count;
  size_t face_capacity;
  int last_error_code; /* FreeTypeError code of the last failed cleanup */
  char last_error[128];
} qa_freetype_font_library;

typedef enum qa_freetype_init_kind {
  QA_FREETYPE_UNAVAILABLE,
  QA_FREETYPE_FAILED,
  QA_FREETYPE_READY,
} qa_freetype_init_kind;

typedef struct qa_font_glyph_bitmap {
  int64_t width;
  int64_t height;
  int64_t pitch;
  int64_t top;
  int64_t bottom;
  int64_t x_skip;
  uint8_t *pixels; /* owned, pitch * height bytes */
  size_t pixel_length;
} qa_font_glyph_bitmap;

typedef struct qa_native_font_glyph_bitmap {
  int64_t width;
  int64_t height;
  int64_t pitch; /* positive row stride of the owned copy */
  int64_t native_pitch;
  int64_t pixel_mode;
  int64_t num_grays;
  int64_t left;
  int64_t top;
  int64_t advance_x26;
  int64_t advance_y26;
  uint8_t *pixels; /* owned, top to bottom */
  size_t pixel_length;
} qa_native_font_glyph_bitmap;

/*
 * Mirrors FreeTypeFontLibrary.open: READY fills *out; FAILED records
 * the FreeTypeError text in *out.message-bearing fields below (the
 * library struct stays closed); UNAVAILABLE covers an unsupported ABI
 * (never on this direct-linked Linux build).
 */
qa_freetype_init_kind qa_freetype_font_library_open(
    qa_freetype_font_library *out, qa_freetype_print_fn print, void *print_ctx,
    char *error, size_t error_size);

size_t qa_freetype_font_library_face_count(const qa_freetype_font_library *library);
/* NULL where the TypeScript returns null after printing. */
qa_freetype_face *qa_freetype_create_face(qa_freetype_font_library *library,
                                          const uint8_t *bytes, size_t length,
                                          int64_t size,
                                          qa_freetype_print_fn print, void *print_ctx);
uint32_t qa_freetype_glyph_index(qa_freetype_font_library *library,
                                 const qa_freetype_face *face, int64_t code);
/* False where the TypeScript returns null after printing/OOM. */
bool qa_freetype_render_glyph(qa_freetype_font_library *library,
                              const qa_freetype_face *face, int64_t code,
                              qa_freetype_print_fn print, void *print_ctx,
                              qa_font_glyph_bitmap *out);
bool qa_freetype_render_bitmap(qa_freetype_font_library *library,
                               const qa_freetype_face *face, int64_t code,
                               qa_freetype_print_fn print, void *print_ctx,
                               qa_native_font_glyph_bitmap *out);
/* Aborts with the FreeTypeError text when FT_Done_Face reports one. */
void qa_freetype_release_face(qa_freetype_font_library *library,
                              qa_freetype_face *face);
/*
 * False when a cleanup step failed (the TypeScript AggregateError);
 * the first FreeTypeError text lands in last_error. Idempotent.
 */
bool qa_freetype_font_library_close(qa_freetype_font_library *library);

void qa_font_glyph_bitmap_dispose(qa_font_glyph_bitmap *bitmap);
void qa_native_font_glyph_bitmap_dispose(qa_native_font_glyph_bitmap *bitmap);

#endif
