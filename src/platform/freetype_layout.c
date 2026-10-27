/* Port of src/platform/freetype-layout.ts. GPL-2.0-or-later. */
#include "qa/platform/freetype_layout.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static const qa_freetype_layout lp64_layout = {8, "i64", "u64", 152, 144, 152, 200};
static const qa_freetype_layout llp64_layout = {4, "i32", "u32", 120, 96, 104, 152};

bool qa_freetype_layout_for(const char *platform, const char *arch,
                            const char *byte_order, qa_freetype_layout *out) {
  if (strcmp(byte_order, "LE") != 0) return false;
  if (strcmp(platform, "win32") == 0 && strcmp(arch, "x64") == 0) {
    *out = llp64_layout;
    return true;
  }
  if ((strcmp(platform, "linux") == 0 || strcmp(platform, "darwin") == 0) &&
      (strcmp(arch, "x64") == 0 || strcmp(arch, "arm64") == 0)) {
    *out = lp64_layout;
    return true;
  }
  return false;
}

int32_t qa_freetype_metric(const uint8_t *bytes, size_t length, int64_t offset,
                           const qa_freetype_layout *layout) {
  size_t size = layout->long_bytes == 4 ? 4u : 8u;
  if (offset < 0 || (uint64_t)offset > (uint64_t)length ||
      (uint64_t)size > (uint64_t)length - (uint64_t)offset)
    qa_range_error("Offset is outside the bounds of the DataView");
  const uint8_t *p = bytes + (size_t)offset;
  uint64_t raw = 0;
  for (size_t i = 0; i < size; i++) raw |= (uint64_t)p[i] << (8u * i);
  if (layout->long_bytes == 4) {
    int32_t value;
    uint32_t word = (uint32_t)raw;
    memcpy(&value, &word, sizeof value);
    return value;
  }
  int64_t value;
  memcpy(&value, &raw, sizeof value);
  if (value < -2147483648LL || value > 2147483647LL)
    qa_range_error("FreeType metric exceeds source int32");
  return (int32_t)value;
}

int64_t qa_freetype_bitmap_length(int64_t width, int64_t height, int64_t pitch,
                                  int64_t pixel_mode) {
  int64_t abs_pitch = pitch < 0 ? -pitch : pitch;
  if (width < 0 || height < 0 || abs_pitch > 0x7fffffffLL)
    qa_range_error("Invalid FreeType bitmap dimensions");
  int64_t row_bytes;
  switch (pixel_mode) {
    case 0:
      if (width != 0 && height != 0)
        qa_range_error("Nonempty FreeType bitmap has no pixel mode");
      row_bytes = 0;
      break;
    case 1: row_bytes = (width + 7) / 8; break;
    case 2:
    case 5:
    case 6: row_bytes = width; break;
    case 3: row_bytes = (width + 3) / 4; break;
    case 4: row_bytes = (width + 1) / 2; break;
    case 7:
      if (width > QA_MAX_SAFE_INTEGER / 4)
        qa_range_error("FreeType bitmap exceeds row or 64 MiB allocation bounds");
      row_bytes = width * 4;
      break;
    default: qa_range_error("Unsupported FreeType pixel mode");
  }
  if (height > 0 && abs_pitch < row_bytes)
    qa_range_error("FreeType bitmap exceeds row or 64 MiB allocation bounds");
  if (height != 0 && abs_pitch > QA_MAX_SAFE_INTEGER / height)
    qa_range_error("FreeType bitmap exceeds row or 64 MiB allocation bounds");
  int64_t length = abs_pitch * height;
  if (length > 64 * 1024 * 1024)
    qa_range_error("FreeType bitmap exceeds row or 64 MiB allocation bounds");
  return length;
}

void qa_normalize_freetype_bitmap_rows(uint8_t *bytes, size_t length,
                                       int64_t height, int64_t pitch) {
  int64_t abs_pitch = pitch < 0 ? -pitch : pitch;
  if (height < 0 || (height != 0 && abs_pitch > QA_MAX_SAFE_INTEGER / height) ||
      (uint64_t)(height * abs_pitch) != (uint64_t)length)
    qa_range_error("FreeType bitmap storage does not match its rows");
  if (pitch >= 0 || height < 2) return;
  size_t stride = (size_t)-pitch;
  uint8_t *temporary = (uint8_t *)malloc(stride == 0 ? 1 : stride);
  if (temporary == NULL) qa_range_error("out of memory");
  for (int64_t top = 0, bottom = height - 1; top < bottom; top++, bottom--) {
    memcpy(temporary, bytes + (size_t)top * stride, stride);
    memcpy(bytes + (size_t)top * stride, bytes + (size_t)bottom * stride, stride);
    memcpy(bytes + (size_t)bottom * stride, temporary, stride);
  }
  free(temporary);
}
