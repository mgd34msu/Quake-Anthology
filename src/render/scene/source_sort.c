/* Port of src/render/scene/source-sort.ts. GPL-2.0-or-later. */
#include "qa/render/scene/source_sort.h"
#include <math.h>
#include "qa/foundation/support.h"
static int32_t i32(double v) { if (!isfinite(v)) return 0; double m = fmod(trunc(v), 4294967296.0); if (m < 0) m += 4294967296.0; if (m >= 2147483648.0) m -= 4294967296.0; return (int32_t)m; }
uint32_t qa_pack_source_draw_sort(double s, double e, double f, double d) {
  /* JS int32 shifts wrap; shifting a negative or overflowing int32 is undefined in C, so shift the bits unsigned. */
  return ((uint32_t)i32(s) << 17) | ((uint32_t)i32(e) << 7) | ((uint32_t)i32(f) << 2) | (uint32_t)i32(d);
}
typedef struct iv { int lo, hi; } iv;
static void defer(iv *st, int *n, int lo, int hi) {
  if (*n >= 30) qa_range_error("qsortFast exceeds the source 30-entry work stack");
  st[(*n)++] = (iv){ lo, hi };
}
static void shortsort(qa_source_draw_sort_range r, int lo, int hi) {
  while (hi > lo) { int mx = lo; for (int p = lo + 1; p <= hi; p++) if (r.get_sort(r.ctx, (size_t)p) > r.get_sort(r.ctx, (size_t)mx)) mx = p; r.swap(r.ctx, (size_t)mx, (size_t)hi); hi--; }
}
void qa_sort_draw_surfs(qa_source_draw_sort_range r) {
  if (r.length < 2) return;
  iv st[30]; int n = 0, lo = 0, hi = (int)r.length - 1;
  for (;;) {
    int size = hi - lo + 1;
    if (size <= 8) shortsort(r, lo, hi);
    else {
      int mid = lo + size / 2; r.swap(r.ctx, (size_t)mid, (size_t)lo);
      int loguy = lo, higuy = hi + 1;
      for (;;) {
        do loguy++; while (loguy <= hi && r.get_sort(r.ctx, (size_t)loguy) <= r.get_sort(r.ctx, (size_t)lo));
        do higuy--; while (higuy > lo && r.get_sort(r.ctx, (size_t)higuy) >= r.get_sort(r.ctx, (size_t)lo));
        if (higuy < loguy) break;
        r.swap(r.ctx, (size_t)loguy, (size_t)higuy);
      }
      r.swap(r.ctx, (size_t)lo, (size_t)higuy);
      if ((higuy - lo) * 8 - 1 >= (hi - loguy) * 8) {
        if (lo + 1 < higuy) defer(st, &n, lo, higuy - 1);
        if (loguy < hi) { lo = loguy; continue; }
      } else {
        if (loguy < hi) defer(st, &n, loguy, hi);
        if (lo + 1 < higuy) { hi = higuy - 1; continue; }
      }
    }
    if (n == 0) return;
    iv p = st[--n]; lo = p.lo; hi = p.hi;
  }
}
