/* Port of src/platform/freetype.ts. GPL-2.0-or-later. */
#include "qa/platform/freetype.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include "qa/foundation/support.h"
#include "qa/platform/freetype_layout.h"

/* TS `throw new Error(msg)`, as in src/text/atlas.c. */
static noreturn void qa_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

struct qa_freetype_face {
  FT_Face address;
  uint8_t *bytes; /* owned copy the face was created from */
  size_t length;
  qa_freetype_font_library *owner;
  struct qa_freetype_face *next;
  bool live;
};

static void emit(qa_freetype_print_fn print, void *ctx, const char *text) {
  if (print != NULL) print(text, ctx);
}

static void emitf(qa_freetype_print_fn print, void *ctx, const char *format, ...) {
  if (print == NULL) return;
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  print(buffer, ctx);
}

static void require_open(const qa_freetype_font_library *library) {
  if (library->handle == NULL) qa_error("FreeType library is closed");
}

static struct qa_freetype_face *require_face(qa_freetype_font_library *library,
                                             const qa_freetype_face *face) {
  require_open(library);
  for (struct qa_freetype_face *record = library->faces; record != NULL;
       record = record->next) {
    if (record == face && record->live && record->owner == library) return record;
  }
  qa_error("FreeType face is not owned by this library or was released");
}

qa_freetype_init_kind qa_freetype_font_library_open(
    qa_freetype_font_library *out, qa_freetype_print_fn print, void *print_ctx,
    char *error, size_t error_size) {
  memset(out, 0, sizeof *out);
  FT_Library handle = NULL;
  FT_Error code = FT_Init_FreeType(&handle);
  if (code != 0) {
    emit(print, print_ctx, "R_InitFreeType: Unable to initialize FreeType.\n");
    if (error != NULL && error_size != 0)
      snprintf(error, error_size, "FT_Init_FreeType failed with FreeType error %d",
               (int)code);
    out->last_error_code = (int)code;
    snprintf(out->last_error, sizeof out->last_error,
             "FT_Init_FreeType failed with FreeType error %d", (int)code);
    return QA_FREETYPE_FAILED;
  }
  if (handle == NULL) qa_error("FreeType initialized a null library");
  out->handle = handle;
  return QA_FREETYPE_READY;
}

size_t qa_freetype_font_library_face_count(const qa_freetype_font_library *library) {
  return library->face_count;
}

qa_freetype_face *qa_freetype_create_face(qa_freetype_font_library *library,
                                          const uint8_t *bytes, size_t length,
                                          int64_t size,
                                          qa_freetype_print_fn print, void *print_ctx) {
  require_open(library);
  if (size <= 0 || size > 0x1ffffff)
    qa_range_error("FreeType size must be a positive int32 26.6 pixel size");
  if (length > 0x7fffffff) qa_range_error("FreeType font exceeds signed long length");
  if (length == 0) {
    emit(print, print_ctx, "RE_RegisterFont: FreeType2, unable to allocate new face.\n");
    return NULL;
  }
  uint8_t *owned = (uint8_t *)malloc(length);
  if (owned == NULL) return NULL;
  memcpy(owned, bytes, length);
  FT_Face address = NULL;
  FT_Error code = FT_New_Memory_Face((FT_Library)library->handle, owned,
                                     (FT_Long)length, 0, &address);
  if (code != 0) {
    free(owned);
    emit(print, print_ctx, "RE_RegisterFont: FreeType2, unable to allocate new face.\n");
    return NULL;
  }
  if (address == NULL) {
    free(owned);
    qa_error("FreeType created a null face");
  }
  struct qa_freetype_face *face =
      (struct qa_freetype_face *)calloc(1, sizeof *face);
  if (face == NULL) {
    FT_Done_Face(address);
    free(owned);
    return NULL;
  }
  face->address = address;
  face->bytes = owned;
  face->length = length;
  face->owner = library;
  face->live = true;
  face->next = library->faces;
  library->faces = face;
  library->face_count++;
  /* Unicode is selected explicitly so code points never use a symbol charmap. */
  FT_Error charmap_error = FT_Select_Charmap(address, FT_ENCODING_UNICODE);
  FT_Error size_error = 0;
  if (charmap_error == 0)
    size_error = FT_Set_Char_Size(address, (FT_F26Dot6)(size * 64),
                                  (FT_F26Dot6)(size * 64), 72, 72);
  if (charmap_error != 0 || size_error != 0) {
    qa_freetype_release_face(library, face);
    if (charmap_error != 0)
      emitf(print, print_ctx, "FreeType Unicode charmap unavailable (%d).\n",
            (int)charmap_error);
    else
      emit(print, print_ctx, "RE_RegisterFont: FreeType2, Unable to set face char size.\n");
    return NULL;
  }
  return face;
}

uint32_t qa_freetype_glyph_index(qa_freetype_font_library *library,
                                 const qa_freetype_face *face, int64_t code) {
  struct qa_freetype_face *record = require_face(library, face);
  if (code < 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
    qa_range_error("FreeType code point must be a Unicode scalar value");
  return (uint32_t)FT_Get_Char_Index(record->address, (FT_ULong)code);
}

static FT_GlyphSlot load_glyph(qa_freetype_font_library *library,
                               const qa_freetype_face *face, int64_t code,
                               qa_freetype_print_fn print, void *print_ctx) {
  uint32_t index = qa_freetype_glyph_index(library, face, code);
  struct qa_freetype_face *record = require_face(library, face);
  FT_Error error = FT_Load_Glyph(record->address, index, FT_LOAD_DEFAULT);
  if (error != 0) {
    emitf(print, print_ctx, "FT_Load_Glyph failed (%d).\n", (int)error);
    return NULL;
  }
  FT_GlyphSlot slot = record->address->glyph;
  if (slot == NULL) qa_error("FreeType face has no glyph slot");
  return slot;
}

static int64_t floor_div64(int64_t value) {
  return value >= 0 ? value / 64 : -((-value + 63) / 64);
}

static int64_t ceil_div64(int64_t value) {
  return value >= 0 ? (value + 63) / 64 : -((-value) / 64);
}

bool qa_freetype_render_glyph(qa_freetype_font_library *library,
                              const qa_freetype_face *face, int64_t code,
                              qa_freetype_print_fn print, void *print_ctx,
                              qa_font_glyph_bitmap *out) {
  memset(out, 0, sizeof *out);
  FT_GlyphSlot slot = load_glyph(library, face, code, print, print_ctx);
  if (slot == NULL) return false;
  if (slot->format != FT_GLYPH_FORMAT_OUTLINE) {
    emit(print, print_ctx, "Non-outline fonts are not supported\n");
    return false;
  }
  int64_t width26 = (int64_t)slot->metrics.width;
  int64_t height26 = (int64_t)slot->metrics.height;
  int64_t bearing_x = (int64_t)slot->metrics.horiBearingX;
  int64_t bearing_y = (int64_t)slot->metrics.horiBearingY;
  int64_t left = floor_div64(bearing_x) * 64;
  int64_t right = ceil_div64(bearing_x + width26) * 64;
  int64_t top = ceil_div64(bearing_y) * 64;
  int64_t bottom = floor_div64(bearing_y - height26) * 64;
  if (width26 < 0 || height26 < 0 || left == INT64_MIN || -left < -2147483648LL ||
      -left > 2147483647LL || bottom == INT64_MIN || -bottom < -2147483648LL ||
      -bottom > 2147483647LL)
    qa_range_error("FreeType outline translation exceeds source int32");
  int64_t width = (right - left) / 64;
  int64_t height = (top - bottom) / 64;
  int64_t pitch = ((width + 3) / 4) * 4;
  int64_t length = qa_freetype_bitmap_length(width, height, pitch, FT_PIXEL_MODE_GRAY);
  size_t alloc = length == 0 ? 1u : (size_t)length;
  uint8_t *pixels = (uint8_t *)calloc(alloc, 1);
  if (pixels == NULL) return false;
  FT_Bitmap bitmap;
  memset(&bitmap, 0, sizeof bitmap);
  bitmap.rows = (unsigned int)height;
  bitmap.width = (unsigned int)width;
  bitmap.pitch = (int)pitch;
  bitmap.buffer = pixels;
  bitmap.num_grays = 256;
  bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
  FT_Outline_Translate(&slot->outline, (FT_Pos)-left, (FT_Pos)-bottom);
  FT_Error error = length == 0
                       ? 0
                       : FT_Outline_Get_Bitmap((FT_Library)library->handle,
                                               &slot->outline, &bitmap);
  if (error != 0) {
    free(pixels);
    emitf(print, print_ctx, "FT_Outline_Get_Bitmap failed (%d).\n", (int)error);
    return false;
  }
  out->width = width;
  out->height = height;
  out->pitch = pitch;
  out->top = floor_div64(bearing_y) + 1;
  out->bottom = bottom;
  out->x_skip = floor_div64((int64_t)slot->metrics.horiAdvance) + 1;
  out->pixels = pixels;
  out->pixel_length = (size_t)length;
  return true;
}

bool qa_freetype_render_bitmap(qa_freetype_font_library *library,
                               const qa_freetype_face *face, int64_t code,
                               qa_freetype_print_fn print, void *print_ctx,
                               qa_native_font_glyph_bitmap *out) {
  memset(out, 0, sizeof *out);
  FT_GlyphSlot slot = load_glyph(library, face, code, print, print_ctx);
  if (slot == NULL) return false;
  FT_Error error = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
  if (error != 0) {
    emitf(print, print_ctx, "FT_Render_Glyph failed (%d).\n", (int)error);
    return false;
  }
  int64_t height = (int64_t)slot->bitmap.rows;
  int64_t width = (int64_t)slot->bitmap.width;
  int64_t native_pitch = (int64_t)slot->bitmap.pitch;
  int64_t pitch = native_pitch < 0 ? -native_pitch : native_pitch;
  int64_t pixel_mode = (int64_t)slot->bitmap.pixel_mode;
  int64_t length = qa_freetype_bitmap_length(width, height, native_pitch, pixel_mode);
  uint8_t *pixels = (uint8_t *)malloc(length == 0 ? 1u : (size_t)length);
  if (pixels == NULL) return false;
  if (length != 0) {
    const uint8_t *start = slot->bitmap.buffer;
    if (native_pitch < 0) start += (ptrdiff_t)((height - 1) * native_pitch);
    memcpy(pixels, start, (size_t)length);
    qa_normalize_freetype_bitmap_rows(pixels, (size_t)length, height, native_pitch);
  }
  out->width = width;
  out->height = height;
  out->pitch = pitch;
  out->native_pitch = native_pitch;
  out->pixel_mode = pixel_mode;
  out->num_grays = (int64_t)slot->bitmap.num_grays;
  out->left = (int64_t)slot->bitmap_left;
  out->top = (int64_t)slot->bitmap_top;
  out->advance_x26 = (int64_t)slot->advance.x;
  out->advance_y26 = (int64_t)slot->advance.y;
  out->pixels = pixels;
  out->pixel_length = (size_t)length;
  return true;
}

void qa_freetype_release_face(qa_freetype_font_library *library,
                              qa_freetype_face *face) {
  struct qa_freetype_face *record = require_face(library, face);
  FT_Error error = FT_Done_Face(record->address);
  record->address = NULL;
  record->live = false;
  free(record->bytes);
  record->bytes = NULL;
  if (library->face_count != 0) library->face_count--;
  if (error != 0)
    qa_error("FT_Done_Face failed with FreeType error %d", (int)error);
}

bool qa_freetype_font_library_close(qa_freetype_font_library *library) {
  if (library->handle == NULL) return true;
  bool ok = true;
  for (struct qa_freetype_face *record = library->faces; record != NULL;
       record = record->next) {
    if (!record->live) continue;
    FT_Error code = FT_Done_Face(record->address);
    if (code != 0 && ok) {
      ok = false;
      library->last_error_code = (int)code;
      snprintf(library->last_error, sizeof library->last_error,
               "FT_Done_Face failed with FreeType error %d", (int)code);
    }
    record->live = false;
    record->address = NULL;
    free(record->bytes);
    record->bytes = NULL;
  }
  FT_Error code = FT_Done_FreeType((FT_Library)library->handle);
  if (code != 0 && ok) {
    ok = false;
    library->last_error_code = (int)code;
    snprintf(library->last_error, sizeof library->last_error,
             "FT_Done_FreeType failed with FreeType error %d", (int)code);
  }
  library->handle = NULL;
  library->face_count = 0;
  struct qa_freetype_face *record = library->faces;
  while (record != NULL) {
    struct qa_freetype_face *next = record->next;
    free(record);
    record = next;
  }
  library->faces = NULL;
  return ok;
}

void qa_font_glyph_bitmap_dispose(qa_font_glyph_bitmap *bitmap) {
  free(bitmap->pixels);
  bitmap->pixels = NULL;
  bitmap->pixel_length = 0;
}

void qa_native_font_glyph_bitmap_dispose(qa_native_font_glyph_bitmap *bitmap) {
  free(bitmap->pixels);
  bitmap->pixels = NULL;
  bitmap->pixel_length = 0;
}
