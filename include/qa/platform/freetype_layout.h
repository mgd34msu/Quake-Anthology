/*
 * Port of src/platform/freetype-layout.ts: public FreeType 2 record
 * layouts plus the pure bitmap-length, metric and row-normalization
 * helpers. The layouts gated raw Bun FFI memory reads in the
 * TypeScript; the C freetype port reads the same records through
 * <ft2build.h>, but the layout values and helpers are observable
 * (tests/platform/freetype.test.ts) and are ported unchanged.
 * signed_long/unsigned_long retain the TypeScript FFI type names.
 * GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_FREETYPE_LAYOUT_H
#define QA_PLATFORM_FREETYPE_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_freetype_layout {
  int long_bytes; /* 4 or 8 */
  const char *signed_long;   /* "i32" or "i64" */
  const char *unsigned_long; /* "u32" or "u64" */
  int64_t face_glyph;
  int64_t slot_format;
  int64_t slot_bitmap;
  int64_t slot_outline;
} qa_freetype_layout;

/* False when the ABI is unsupported (TypeScript null). */
bool qa_freetype_layout_for(const char *platform, const char *arch,
                            const char *byte_order, qa_freetype_layout *out);

/*
 * Little-endian C long at offset in bytes[0..length). An out-of-range
 * DataView read throws in the TypeScript, so this aborts; a 64-bit
 * value outside int32 aborts with the TypeScript RangeError message.
 */
int32_t qa_freetype_metric(const uint8_t *bytes, size_t length, int64_t offset,
                           const qa_freetype_layout *layout);

/* |pitch| * height after the TypeScript row/mode/64 MiB validation. */
int64_t qa_freetype_bitmap_length(int64_t width, int64_t height, int64_t pitch,
                                  int64_t pixel_mode);

/* Reverses rows in place for negative pitch; validates storage size. */
void qa_normalize_freetype_bitmap_rows(uint8_t *bytes, size_t length,
                                       int64_t height, int64_t pitch);

#endif
