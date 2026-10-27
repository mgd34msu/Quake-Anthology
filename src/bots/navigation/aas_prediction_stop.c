/* See header. GPL-2.0-or-later. */
#include "qa/bots/navigation/aas_prediction_stop.h"
#include <stdlib.h>
#include <string.h>
bool qa_aas_prediction_stop(const qa_aas_asset *asset, qa_vec3 start, qa_vec3 end, int64_t frame, int64_t events, int64_t stop_area, qa_bot_movement_stop *out) {
  if ((events & (512 | 128 | 256 | 4096)) == 0) return false;
  size_t n = 0; qa_aas_area_crossing *cs = qa_aas_trace_areas(asset, start, end, 20, &n);
  for (size_t i = 0; i < n; i++) { int64_t contents = (cs[i].area >= 0 && cs[i].area < (int64_t)asset->setting_count) ? asset->settings[cs[i].area].contents : 0;
    int64_t ev = 0; if ((events & 512) && cs[i].area == stop_area) ev = 512; else if ((events & 128) && frame != 0 && (contents & 128)) ev = 128; else if ((events & 256) && (contents & 64)) ev = 256; else if ((events & 4096) && (contents & 8)) ev = 4096;
    if (ev) { memset(out, 0, sizeof *out); out->events = ev; out->origin = cs[i].point; out->has_area = true; out->area = cs[i].area; free(cs); return true; } }
  free(cs); return false;
}
