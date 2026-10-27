/* Port of src/render/scene/skin.ts. GPL-2.0-or-later. */
#include "qa/render/scene/skin.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
uint8_t *qa_flood_skin(const uint8_t *indices, size_t length, double width_d, double height_d, qa_palette palette, size_t *out_length) {
  uint8_t *result = malloc(length ? length : 1); if (!result) return NULL;
  if (length) memcpy(result, indices, length);
  if (out_length) *out_length = length;
  long width = (long)width_d, height = (long)height_d;
  int black = 0;
  for (int i = 0; i < 256; i++) {
    size_t o = (size_t)i * 3;
    uint8_t r = o < palette.colors.length ? palette.colors.bytes[o] : 0;
    uint8_t g = o + 1 < palette.colors.length ? palette.colors.bytes[o + 1] : 0;
    uint8_t b = o + 2 < palette.colors.length ? palette.colors.bytes[o + 2] : 0;
    if (r == 0 && g == 0 && b == 0) { black = i; break; }
  }
  if (length == 0) return result;
  uint8_t fill = result[0];
  if (fill == black || fill == 255) return result;
  size_t *queue = malloc(length * sizeof *queue); if (!queue) { free(result); return NULL; }
  size_t qlen = 0; queue[qlen++] = 0; result[0] = 255;
  for (size_t head = 0; head < qlen; head++) {
    size_t pixel = queue[head];
    long x = width ? (long)(pixel % (size_t)width) : 0, y = width ? (long)(pixel / (size_t)width) : 0;
    long cand[4]; cand[0] = x > 0 ? (long)pixel - 1 : -1; cand[1] = x + 1 < width ? (long)pixel + 1 : -1;
    cand[2] = y > 0 ? (long)pixel - width : -1; cand[3] = y + 1 < height ? (long)pixel + width : -1;
    uint8_t color = (uint8_t)black;
    for (int k = 0; k < 4; k++) {
      long next = cand[k]; if (next < 0 || (size_t)next >= length) continue;
      uint8_t v = result[next];
      if (v == fill) { result[next] = 255; queue[qlen++] = (size_t)next; }
      else if (v != 255) color = v;
    }
    result[pixel] = color;
  }
  free(queue); (void)qa_range_error; return result;
}
