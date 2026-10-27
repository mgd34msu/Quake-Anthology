/* Port of src/content/q2/rerelease/world-text.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/world_text.h"
#include <string.h>

qa_world_text qa_q2_world_text_utf16(qa_vec3 origin, const qa_vec3 *angles,
                                     const uint16_t *text, size_t text_length,
                                     qa_vec4 color, double size, bool depth_test,
                                     char *out_text, size_t out_cap) {
  size_t n = text_length < 127 ? text_length : 127;
  if (out_cap > 0) {
    size_t w = n < out_cap - 1 ? n : out_cap - 1;
    for (size_t i = 0; i < w; i++) out_text[i] = (char)(text[i] & 255u);
    out_text[w] = '\0';
  }
  qa_world_text r;
  memset(&r, 0, sizeof r);
  r.text = out_text;
  r.origin = origin;
  r.color = color;
  r.cell_size = size * 8;
  r.has_distance_cull_factor = true;
  r.distance_cull_factor = 0.004;
  r.orientation_fixed = angles != NULL;
  if (angles) r.angles = *angles;
  r.depth_test = depth_test;
  r.font_selected = false; /* "classic" */
  return r;
}
qa_world_text qa_q2_world_text(qa_vec3 origin, const qa_vec3 *angles, const char *text,
                               qa_vec4 color, double size, bool depth_test,
                               char *out_text, size_t out_cap) {
  uint16_t units[127];
  size_t len = text ? strlen(text) : 0;
  size_t n = len < 127 ? len : 127;
  for (size_t i = 0; i < n; i++) units[i] = (uint16_t)(unsigned char)text[i];
  return qa_q2_world_text_utf16(origin, angles, units, n, color, size, depth_test, out_text, out_cap);
}
