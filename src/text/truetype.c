/* Port of src/text/truetype.ts. GPL-2.0-or-later. */
#include "qa/text/truetype.h"

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* Internal error channel standing in for the BinaryError/RangeError
 * throws parseFont catches. Aborts would change observable behavior. */
typedef struct tt_err {
  jmp_buf jb;
  char msg[512];
} tt_err;

static _Noreturn void tt_fail(tt_err *err, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  vsnprintf(err->msg, sizeof err->msg, format, ap);
  va_end(ap);
  longjmp(err->jb, 1);
}

static void *tt_calloc(tt_err *err, size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p == NULL) tt_fail(err, "out of memory");
  return p;
}

static void *tt_grow(tt_err *err, void *ptr, size_t *capacity, size_t needed,
                     size_t elem_size) {
  if (*capacity >= needed) return ptr;
  size_t cap = *capacity == 0 ? 8 : *capacity;
  while (cap < needed) cap *= 2;
  void *next = realloc(ptr, cap * elem_size);
  if (next == NULL) tt_fail(err, "out of memory");
  *capacity = cap;
  return next;
}

/* DataView over a byte slice (offsets relative to the slice). An
 * out-of-range DataView read throws RangeError in the TypeScript. */
typedef struct tt_slice {
  const uint8_t *p;
  size_t n;
} tt_slice;

static void dv_check(tt_err *err, tt_slice s, int64_t off, size_t size) {
  if (off < 0 || (uint64_t)off > (uint64_t)s.n ||
      (uint64_t)size > (uint64_t)s.n - (uint64_t)off) {
    tt_fail(err, "Offset is outside the bounds of the DataView");
  }
}

static uint8_t dv_u8(tt_err *err, tt_slice s, int64_t off) {
  dv_check(err, s, off, 1);
  return s.p[(size_t)off];
}

static int8_t dv_i8(tt_err *err, tt_slice s, int64_t off) {
  return (int8_t)dv_u8(err, s, off);
}

static uint16_t dv_u16(tt_err *err, tt_slice s, int64_t off) {
  dv_check(err, s, off, 2);
  return (uint16_t)((s.p[(size_t)off] << 8) | s.p[(size_t)off + 1]);
}

static int16_t dv_i16(tt_err *err, tt_slice s, int64_t off) {
  return (int16_t)dv_u16(err, s, off);
}

static uint32_t dv_u32(tt_err *err, tt_slice s, int64_t off) {
  dv_check(err, s, off, 4);
  return ((uint32_t)s.p[(size_t)off] << 24) |
         ((uint32_t)s.p[(size_t)off + 1] << 16) |
         ((uint32_t)s.p[(size_t)off + 2] << 8) |
         (uint32_t)s.p[(size_t)off + 3];
}

/* at(): the TS bounds helper with its exact RangeError message. */
static uint8_t at_u8(tt_err *err, tt_slice s, int64_t index, const char *ctx) {
  if (index < 0 || (uint64_t)index >= (uint64_t)s.n) {
    tt_fail(err, "Missing %s at index %lld", ctx, (long long)index);
  }
  return s.p[(size_t)index];
}

typedef struct tt_raw_point {
  double x;
  double y;
  bool on_curve;
} tt_raw_point;

typedef struct tt_raw_contour {
  tt_raw_point *v;
  size_t n;
  size_t cap;
} tt_raw_contour;

typedef struct tt_raw_set {
  tt_raw_contour *v;
  size_t n;
  size_t cap;
} tt_raw_set;

static tt_raw_point raw_at(tt_err *err, const tt_raw_contour *c, int64_t index,
                           const char *ctx) {
  if (index < 0 || (uint64_t)index >= (uint64_t)c->n) {
    tt_fail(err, "Missing %s at index %lld", ctx, (long long)index);
  }
  return c->v[(size_t)index];
}

static void raw_push(tt_err *err, tt_raw_contour *c, tt_raw_point pt) {
  c->v = (tt_raw_point *)tt_grow(err, c->v, &c->cap, c->n + 1,
                                 sizeof(tt_raw_point));
  c->v[c->n++] = pt;
}

static void raw_set_push(tt_err *err, tt_raw_set *set, tt_raw_contour c) {
  set->v = (tt_raw_contour *)tt_grow(err, set->v, &set->cap, set->n + 1,
                                     sizeof(tt_raw_contour));
  set->v[set->n++] = c;
}

static void raw_set_free(tt_raw_set *set) {
  for (size_t i = 0; i < set->n; i++) free(set->v[i].v);
  free(set->v);
  set->v = NULL;
  set->n = 0;
  set->cap = 0;
}

static void flat_contour_push(tt_err *err, qa_flattened_contour *c,
                              size_t *cap, qa_tt_point pt) {
  c->points = (qa_tt_point *)tt_grow(err, c->points, cap, c->count + 1,
                                     sizeof(qa_tt_point));
  c->points[c->count++] = pt;
}

typedef struct tt_flat_builder {
  qa_tt_contours set;
  size_t *caps;
  size_t set_cap;
  size_t caps_cap;
} tt_flat_builder;

static qa_flattened_contour *flat_builder_new(tt_err *err,
                                              tt_flat_builder *b) {
  b->set.contours = (qa_flattened_contour *)tt_grow(
      err, b->set.contours, &b->set_cap, b->set.count + 1,
      sizeof(qa_flattened_contour));
  b->caps = (size_t *)tt_grow(err, b->caps, &b->caps_cap, b->set.count + 1,
                              sizeof(size_t));
  b->caps[b->set.count] = 0;
  qa_flattened_contour *c = &b->set.contours[b->set.count];
  c->points = NULL;
  c->count = 0;
  b->set.count += 1;
  return c;
}

static void flat_set_free(qa_tt_contours *set) {
  for (size_t i = 0; i < set->count; i++) free(set->contours[i].points);
  free(set->contours);
  set->contours = NULL;
  set->count = 0;
}

typedef struct tt_num_list {
  double *v;
  size_t n;
  size_t cap;
} tt_num_list;

static void num_push(tt_err *err, tt_num_list *list, double value) {
  list->v = (double *)tt_grow(err, list->v, &list->cap, list->n + 1,
                              sizeof(double));
  list->v[list->n++] = value;
}

static double num_at(tt_err *err, const tt_num_list *list, int64_t index,
                     const char *ctx) {
  if (index < 0 || (uint64_t)index >= (uint64_t)list->n) {
    tt_fail(err, "Missing %s at index %lld", ctx, (long long)index);
  }
  return list->v[(size_t)index];
}

typedef struct tt_u16_list {
  uint16_t *v;
  size_t n;
  size_t cap;
} tt_u16_list;

typedef struct tt_slice_list {
  tt_slice *v;
  size_t n;
  size_t cap;
} tt_slice_list;

static void slice_push(tt_err *err, tt_slice_list *list, tt_slice s) {
  list->v = (tt_slice *)tt_grow(err, list->v, &list->cap, list->n + 1,
                                sizeof(tt_slice));
  list->v[list->n++] = s;
}

static tt_slice slice_at(tt_err *err, const tt_slice_list *list, int64_t index,
                         const char *ctx) {
  if (index < 0 || (uint64_t)index >= (uint64_t)list->n) {
    tt_fail(err, "Missing %s at index %lld", ctx, (long long)index);
  }
  return list->v[(size_t)index];
}

typedef struct tt_table_record {
  char tag[4];
  uint32_t offset;
  uint32_t length;
} tt_table_record;

static bool read_tag(tt_err *err, tt_slice buf, int64_t off, char out[4]) {
  out[0] = (char)at_u8(err, buf, off, "buf");
  out[1] = (char)at_u8(err, buf, off + 1, "buf");
  out[2] = (char)at_u8(err, buf, off + 2, "buf");
  out[3] = (char)at_u8(err, buf, off + 3, "buf");
  return true;
}

static bool tag_eq(const char tag[4], const char *want) {
  return memcmp(tag, want, 4) == 0;
}

/* readSfntDirectory. Returns false for the TS null cases. The
 * BinaryReader.dataView range check throws BinaryError; its message
 * is reproduced (qa_binary_reader_data_view aborts, which would break
 * parseFont's catch, so the check is inlined). */
static bool read_sfnt_directory(tt_err *err, tt_slice buf,
                                tt_table_record **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (buf.n < 12) return false;
  uint32_t version_num = dv_u32(err, buf, 0);
  char tag[4];
  read_tag(err, buf, 0, tag);
  if (version_num != 0x00010000u && !tag_eq(tag, "OTTO") &&
      !tag_eq(tag, "true") && version_num != 0x74727565u) {
    return false;
  }
  uint16_t num_tables = dv_u16(err, buf, 4);
  tt_table_record *tables =
      (tt_table_record *)tt_calloc(err, num_tables, sizeof(tt_table_record));
  size_t count = 0;
  int64_t off = 12;
  for (int i = 0; i < num_tables; i++) {
    if (off + 16 > (int64_t)buf.n) {
      free(tables);
      return false;
    }
    char rec_tag[4];
    read_tag(err, buf, off, rec_tag);
    uint32_t t_off = dv_u32(err, buf, off + 8);
    uint32_t t_len = dv_u32(err, buf, off + 12);
    if ((uint64_t)t_off > (uint64_t)buf.n ||
        (uint64_t)t_len > (uint64_t)buf.n - (uint64_t)t_off) {
      free(tables);
      tt_fail(err, "<font>:%u: range of %u bytes exceeds %zu-byte input",
              t_off, t_len, buf.n);
    }
    /* Map.set: a duplicate tag replaces the earlier record. */
    size_t slot = count;
    for (size_t k = 0; k < count; k++) {
      if (memcmp(tables[k].tag, rec_tag, 4) == 0) slot = k;
    }
    memcpy(tables[slot].tag, rec_tag, 4);
    tables[slot].offset = t_off;
    tables[slot].length = t_len;
    if (slot == count) count += 1;
    off += 16;
  }
  *out = tables;
  *out_count = count;
  return true;
}

static const tt_table_record *find_table(const tt_table_record *tables,
                                         size_t count, const char *tag) {
  for (size_t i = 0; i < count; i++) {
    if (tag_eq(tables[i].tag, tag)) return &tables[i];
  }
  return NULL;
}

static int64_t cmap_format4_lookup(tt_err *err, tt_slice view,
                                   int64_t subtable_offset, int64_t cp) {
  if (cp > 0xffff) return 0;
  int64_t seg_x2 = dv_u16(err, view, subtable_offset + 6);
  int64_t seg_count = seg_x2 / 2;
  int64_t end_off = subtable_offset + 14;
  int64_t start_off = end_off + seg_x2 + 2;
  int64_t delta_off = start_off + seg_x2;
  int64_t range_off = delta_off + seg_x2;
  for (int64_t i = 0; i < seg_count; i++) {
    int64_t end_code = dv_u16(err, view, end_off + i * 2);
    if (cp > end_code) continue;
    int64_t start_code = dv_u16(err, view, start_off + i * 2);
    if (cp < start_code) return 0;
    int64_t id_delta = dv_i16(err, view, delta_off + i * 2);
    int64_t id_range_offset = dv_u16(err, view, range_off + i * 2);
    if (id_range_offset == 0) return (cp + id_delta) & 0xffff;
    int64_t addr = range_off + i * 2 + id_range_offset + (cp - start_code) * 2;
    int64_t g = dv_u16(err, view, addr);
    if (g == 0) return 0;
    return (g + id_delta) & 0xffff;
  }
  return 0;
}

static int64_t cmap_format12_lookup(tt_err *err, tt_slice view,
                                    int64_t subtable_offset, int64_t cp) {
  int64_t num_groups = (int64_t)dv_u32(err, view, subtable_offset + 12);
  int64_t groups_off = subtable_offset + 16;
  int64_t lo = 0;
  int64_t hi = num_groups - 1;
  while (lo <= hi) {
    int64_t mid = (lo + hi) >> 1;
    int64_t base = groups_off + mid * 12;
    int64_t start_char = (int64_t)dv_u32(err, view, base);
    int64_t end_char = (int64_t)dv_u32(err, view, base + 4);
    if (cp < start_char) {
      hi = mid - 1;
    } else if (cp > end_char) {
      lo = mid + 1;
    } else {
      int64_t start_glyph = (int64_t)dv_u32(err, view, base + 8);
      return start_glyph + (cp - start_char);
    }
  }
  return 0;
}

/* selectCmapSubtable: returns format (4/12) and offset, or false. */
static bool select_cmap_subtable(tt_err *err, tt_slice view,
                                 int64_t cmap_offset, int *out_format,
                                 int64_t *out_offset) {
  int64_t num_tables = dv_u16(err, view, cmap_offset + 2);
  bool have = false;
  int64_t best_offset = 0;
  int best_format = 0;
  int best_score = -1;
  for (int64_t i = 0; i < num_tables; i++) {
    int64_t rec = cmap_offset + 4 + i * 8;
    int platform_id = dv_u16(err, view, rec);
    int encoding_id = dv_u16(err, view, rec + 2);
    int64_t sub_offset = cmap_offset + (int64_t)dv_u32(err, view, rec + 4);
    int format = dv_u16(err, view, sub_offset);
    int score = -1;
    if (format == 12 && ((platform_id == 3 && encoding_id == 10) ||
                         (platform_id == 0 && (encoding_id == 4 || encoding_id == 6)))) {
      score = 100;
    } else if (format == 12) {
      score = 90;
    } else if (format == 4 && ((platform_id == 3 && encoding_id == 1) || platform_id == 0)) {
      score = 80;
    } else if (format == 4) {
      score = 70;
    }
    if (score > best_score) {
      best_offset = sub_offset;
      best_format = format;
      best_score = score;
      have = true;
    }
  }
  if (!have) return false;
  if (best_format != 12 && best_format != 4) return false;
  *out_format = best_format;
  *out_offset = best_offset;
  return true;
}

static uint32_t *read_loca(tt_err *err, tt_slice view, int64_t offset,
                           int64_t num_glyphs, int64_t index_to_loc_format) {
  uint32_t *out =
      (uint32_t *)tt_calloc(err, (size_t)num_glyphs + 1, sizeof(uint32_t));
  if (index_to_loc_format == 0) {
    for (int64_t i = 0; i <= num_glyphs; i++) {
      out[i] = (uint32_t)dv_u16(err, view, offset + i * 2) * 2u;
    }
  } else {
    for (int64_t i = 0; i <= num_glyphs; i++) {
      out[i] = dv_u32(err, view, offset + i * 4);
    }
  }
  return out;
}

static tt_raw_set parse_simple_glyph(tt_err *err, tt_slice view, tt_slice buf,
                                     int64_t off, int64_t num_contours) {
  int64_t p = off;
  tt_num_list end_pts = {NULL, 0, 0};
  for (int64_t i = 0; i < num_contours; i++) {
    num_push(err, &end_pts, (double)dv_u16(err, view, p));
    p += 2;
  }
  int64_t num_points = num_contours > 0
      ? (int64_t)num_at(err, &end_pts, num_contours - 1, "endPts") + 1 : 0;
  int64_t instruction_length = dv_u16(err, view, p);
  p += 2 + instruction_length;
  tt_num_list flags = {NULL, 0, 0};
  while ((int64_t)flags.n < num_points) {
    uint8_t f = at_u8(err, buf, p, "buf");
    p += 1;
    num_push(err, &flags, (double)f);
    if (f & 0x08) {
      int repeat = at_u8(err, buf, p, "buf");
      p += 1;
      while (repeat > 0 && (int64_t)flags.n < num_points) {
        num_push(err, &flags, (double)f);
        repeat -= 1;
      }
    }
  }
  tt_num_list xs = {NULL, 0, 0};
  double x = 0;
  for (int64_t i = 0; i < num_points; i++) {
    int f = (int)num_at(err, &flags, i, "flags");
    if (f & 0x02) {
      int dx = at_u8(err, buf, p, "buf");
      p += 1;
      x += (f & 0x10) ? (double)dx : -(double)dx;
    } else if (!(f & 0x10)) {
      x += (double)dv_i16(err, view, p);
      p += 2;
    }
    num_push(err, &xs, x);
  }
  tt_num_list ys = {NULL, 0, 0};
  double y = 0;
  for (int64_t i = 0; i < num_points; i++) {
    int f = (int)num_at(err, &flags, i, "flags");
    if (f & 0x04) {
      int dy = at_u8(err, buf, p, "buf");
      p += 1;
      y += (f & 0x20) ? (double)dy : -(double)dy;
    } else if (!(f & 0x20)) {
      y += (double)dv_i16(err, view, p);
      p += 2;
    }
    num_push(err, &ys, y);
  }
  tt_raw_set contours = {NULL, 0, 0};
  int64_t start = 0;
  for (int64_t c = 0; c < num_contours; c++) {
    int64_t end = (int64_t)num_at(err, &end_pts, c, "endPts");
    tt_raw_contour pts = {NULL, 0, 0};
    for (int64_t i = start; i <= end; i++) {
      tt_raw_point pt;
      pt.x = num_at(err, &xs, i, "xs");
      pt.y = num_at(err, &ys, i, "ys");
      pt.on_curve = ((int)num_at(err, &flags, i, "flags") & 0x01) != 0;
      raw_push(err, &pts, pt);
    }
    raw_set_push(err, &contours, pts);
    start = end + 1;
  }
  free(end_pts.v);
  free(flags.v);
  free(xs.v);
  free(ys.v);
  return contours;
}

enum {
  TT_ARG_WORDS = 0x0001,
  TT_ARGS_XY = 0x0002,
  TT_HAVE_SCALE = 0x0008,
  TT_MORE_COMPONENTS = 0x0020,
  TT_HAVE_XY_SCALE = 0x0040,
  TT_HAVE_TWO_BY_TWO = 0x0080,
};

typedef tt_raw_set (*tt_resolve_fn)(void *ctx, int64_t gid, int64_t depth);

static tt_raw_set parse_composite_glyph(tt_err *err, tt_slice view, int64_t off,
                                        tt_resolve_fn resolve, void *ctx,
                                        int64_t depth) {
  int64_t p = off;
  tt_raw_set contours = {NULL, 0, 0};
  for (;;) {
    int flags = dv_u16(err, view, p);
    p += 2;
    int glyph_index = dv_u16(err, view, p);
    p += 2;
    double dx = 0;
    double dy = 0;
    if (flags & TT_ARG_WORDS) {
      int a1 = dv_i16(err, view, p);
      int a2 = dv_i16(err, view, p + 2);
      p += 4;
      if (flags & TT_ARGS_XY) {
        dx = (double)a1;
        dy = (double)a2;
      }
    } else {
      int a1 = dv_i8(err, view, p);
      int a2 = dv_i8(err, view, p + 1);
      p += 2;
      if (flags & TT_ARGS_XY) {
        dx = (double)a1;
        dy = (double)a2;
      }
    }
    double a = 1;
    double b = 0;
    double c = 0;
    double d = 1;
    if (flags & TT_HAVE_SCALE) {
      a = (double)dv_i16(err, view, p) / 16384.0;
      d = a;
      p += 2;
    } else if (flags & TT_HAVE_XY_SCALE) {
      a = (double)dv_i16(err, view, p) / 16384.0;
      d = (double)dv_i16(err, view, p + 2) / 16384.0;
      p += 4;
    } else if (flags & TT_HAVE_TWO_BY_TWO) {
      a = (double)dv_i16(err, view, p) / 16384.0;
      b = (double)dv_i16(err, view, p + 2) / 16384.0;
      c = (double)dv_i16(err, view, p + 4) / 16384.0;
      d = (double)dv_i16(err, view, p + 6) / 16384.0;
      p += 8;
    }
    if (depth < 8) {
      tt_raw_set sub = resolve(ctx, glyph_index, depth + 1);
      for (size_t k = 0; k < sub.n; k++) {
        tt_raw_contour mapped = {NULL, 0, 0};
        for (size_t j = 0; j < sub.v[k].n; j++) {
          tt_raw_point pt = sub.v[k].v[j];
          tt_raw_point m;
          m.x = a * pt.x + c * pt.y + dx;
          m.y = b * pt.x + d * pt.y + dy;
          m.on_curve = pt.on_curve;
          raw_push(err, &mapped, m);
        }
        raw_set_push(err, &contours, mapped);
      }
      /* Resolutions here are always depth>0, hence fresh in the TS
       * (the raw cache is only consulted at depth 0); free after copy. */
      raw_set_free(&sub);
    }
    if (!(flags & TT_MORE_COMPONENTS)) break;
  }
  return contours;
}

static void flatten_quad_segment(tt_err *err, qa_tt_point p0, qa_tt_point c,
                                 qa_tt_point p1, qa_flattened_contour *out,
                                 size_t *cap) {
  const int segments = 8;
  for (int s = 1; s <= segments; s++) {
    double t = (double)s / (double)segments;
    double mt = 1.0 - t;
    qa_tt_point pt;
    pt.x = mt * mt * p0.x + 2.0 * mt * t * c.x + t * t * p1.x;
    pt.y = mt * mt * p0.y + 2.0 * mt * t * c.y + t * t * p1.y;
    flat_contour_push(err, out, cap, pt);
  }
}

static qa_flattened_contour flatten_quadratic_contour(
    tt_err *err, const tt_raw_contour *raw_points) {
  qa_flattened_contour out = {NULL, 0};
  size_t out_cap = 0;
  int64_t n = (int64_t)raw_points->n;
  if (n < 2) return out;
  tt_raw_contour expanded = {NULL, 0, 0};
  for (int64_t i = 0; i < n; i++) {
    tt_raw_point cur = raw_at(err, raw_points, i, "rawPoints");
    tt_raw_point next = raw_at(err, raw_points, (i + 1) % n, "rawPoints");
    raw_push(err, &expanded, cur);
    if (!cur.on_curve && !next.on_curve) {
      tt_raw_point mid;
      mid.x = (cur.x + next.x) / 2.0;
      mid.y = (cur.y + next.y) / 2.0;
      mid.on_curve = true;
      raw_push(err, &expanded, mid);
    }
  }
  int64_t start_idx = -1;
  for (size_t i = 0; i < expanded.n; i++) {
    if (expanded.v[i].on_curve) {
      start_idx = (int64_t)i;
      break;
    }
  }
  if (start_idx == -1) {
    free(expanded.v);
    return out;
  }
  tt_raw_contour ring = {NULL, 0, 0};
  for (size_t i = (size_t)start_idx; i < expanded.n; i++) {
    raw_push(err, &ring, expanded.v[i]);
  }
  for (int64_t i = 0; i < start_idx; i++) {
    raw_push(err, &ring, expanded.v[i]);
  }
  free(expanded.v);
  int64_t m = (int64_t)ring.n;
  tt_raw_point first = raw_at(err, &ring, 0, "ring");
  qa_tt_point cur = {first.x, first.y};
  flat_contour_push(err, &out, &out_cap, cur);
  int64_t i = 1;
  while (i < m) {
    tt_raw_point p = raw_at(err, &ring, i, "ring");
    if (p.on_curve) {
      qa_tt_point pt = {p.x, p.y};
      flat_contour_push(err, &out, &out_cap, pt);
      cur = pt;
      i += 1;
    } else {
      tt_raw_point end = raw_at(err, &ring, (i + 1) % m, "ring");
      qa_tt_point ctrl = {p.x, p.y};
      qa_tt_point e = {end.x, end.y};
      flatten_quad_segment(err, cur, ctrl, e, &out, &out_cap);
      cur = e;
      i += 2;
    }
  }
  free(ring.v);
  return out;
}

/* --- CFF --- */

typedef struct tt_cff_dict_entry {
  int op;
  double *operands;
  size_t n;
} tt_cff_dict_entry;

typedef struct tt_cff_dict {
  tt_cff_dict_entry *v;
  size_t n;
  size_t cap;
} tt_cff_dict;

static const tt_cff_dict_entry *cff_dict_get(const tt_cff_dict *dict, int op) {
  for (size_t i = 0; i < dict->n; i++) {
    if (dict->v[i].op == op) return &dict->v[i];
  }
  return NULL;
}

static void cff_dict_free(tt_cff_dict *dict) {
  for (size_t i = 0; i < dict->n; i++) free(dict->v[i].operands);
  free(dict->v);
  dict->v = NULL;
  dict->n = 0;
  dict->cap = 0;
}

static double dict_at(tt_err *err, const tt_cff_dict_entry *entry,
                      int64_t index, const char *ctx) {
  if (index < 0 || (uint64_t)index >= (uint64_t)entry->n) {
    tt_fail(err, "Missing %s at index %lld", ctx, (long long)index);
  }
  return entry->operands[(size_t)index];
}

typedef struct tt_cff_font {
  tt_slice_list char_strings;
  tt_slice_list global_subrs;
  int64_t global_bias;
  bool is_cid;
  tt_slice_list default_local_subrs;
  int64_t default_local_bias;
  tt_slice_list *fd_local_subrs;
  size_t fd_count;
  int64_t *fd_local_bias;
  uint8_t *fd_select; /* NULL when absent */
  size_t fd_select_len;
} tt_cff_font;

static void cff_font_free(tt_cff_font *cff) {
  free(cff->char_strings.v);
  free(cff->global_subrs.v);
  free(cff->default_local_subrs.v);
  for (size_t i = 0; i < cff->fd_count; i++) free(cff->fd_local_subrs[i].v);
  free(cff->fd_local_subrs);
  free(cff->fd_local_bias);
  free(cff->fd_select);
  memset(cff, 0, sizeof *cff);
}

static int read_int16_be(tt_err *err, tt_slice b, int64_t pos) {
  int v = (at_u8(err, b, pos, "b") << 8) | at_u8(err, b, pos + 1, "b");
  return v >= 0x8000 ? v - 0x10000 : v;
}

static int32_t read_int32_be(tt_err *err, tt_slice b, int64_t pos) {
  uint32_t v = ((uint32_t)at_u8(err, b, pos, "b") << 24) |
               ((uint32_t)at_u8(err, b, pos + 1, "b") << 16) |
               ((uint32_t)at_u8(err, b, pos + 2, "b") << 8) |
               (uint32_t)at_u8(err, b, pos + 3, "b");
  int32_t out;
  memcpy(&out, &v, sizeof out);
  return out;
}

static int64_t subr_bias(int64_t n) {
  return n < 1240 ? 107 : n < 33900 ? 1131 : 32768;
}

typedef struct tt_cff_index_result {
  tt_slice_list items;
  int64_t end;
} tt_cff_index_result;

static tt_cff_index_result read_cff_index(tt_err *err, tt_slice buf,
                                          tt_slice view, int64_t pos) {
  tt_cff_index_result result;
  result.items.v = NULL;
  result.items.n = 0;
  result.items.cap = 0;
  int64_t count = dv_u16(err, view, pos);
  pos += 2;
  if (count == 0) {
    result.end = pos;
    return result;
  }
  int off_size = at_u8(err, buf, pos, "buf");
  if (off_size < 1 || off_size > 4) {
    tt_fail(err, "Invalid CFF INDEX offset size %d", off_size);
  }
  pos += 1;
  tt_num_list offsets = {NULL, 0, 0};
  for (int64_t i = 0; i <= count; i++) {
    double v = 0;
    for (int b = 0; b < off_size; b++) {
      v = v * 256.0 + (double)at_u8(err, buf, pos, "buf");
      pos += 1;
    }
    num_push(err, &offsets, v);
  }
  int64_t data_start = pos - 1;
  if (num_at(err, &offsets, 0, "CFF INDEX offset") != 1.0) {
    tt_fail(err, "CFF INDEX offsets must start at one");
  }
  for (int64_t i = 0; i < count; i++) {
    int64_t start = data_start + (int64_t)num_at(err, &offsets, i, "CFF INDEX offset");
    int64_t end = data_start + (int64_t)num_at(err, &offsets, i + 1, "CFF INDEX offset");
    if (end < start || end > (int64_t)buf.n) {
      tt_fail(err, "CFF INDEX item exceeds table bounds");
    }
    tt_slice item;
    item.p = buf.p + start;
    item.n = (size_t)(end - start);
    slice_push(err, &result.items, item);
  }
  result.end = data_start + (int64_t)num_at(err, &offsets, count, "offsets");
  free(offsets.v);
  return result;
}

static tt_cff_dict parse_cff_dict(tt_err *err, tt_slice b) {
  tt_cff_dict dict = {NULL, 0, 0};
  tt_num_list operands = {NULL, 0, 0};
  int64_t i = 0;
  while (i < (int64_t)b.n) {
    int b0 = at_u8(err, b, i, "b");
    if (b0 <= 21) {
      int op;
      if (b0 == 12) {
        op = 1200 + at_u8(err, b, i + 1, "b");
        i += 2;
      } else {
        op = b0;
        i += 1;
      }
      tt_cff_dict_entry entry;
      entry.op = op;
      entry.operands = operands.v;
      entry.n = operands.n;
      dict.v = (tt_cff_dict_entry *)tt_grow(err, dict.v, &dict.cap, dict.n + 1,
                                            sizeof(tt_cff_dict_entry));
      /* Map.set replaces an existing operator. */
      size_t slot = dict.n;
      for (size_t k = 0; k < dict.n; k++) {
        if (dict.v[k].op == op) slot = k;
      }
      if (slot < dict.n) free(dict.v[slot].operands);
      dict.v[slot] = entry;
      if (slot == dict.n) dict.n += 1;
      operands.v = NULL;
      operands.n = 0;
      operands.cap = 0;
    } else if (b0 == 28) {
      num_push(err, &operands, (double)read_int16_be(err, b, i + 1));
      i += 3;
    } else if (b0 == 29) {
      num_push(err, &operands, (double)read_int32_be(err, b, i + 1));
      i += 5;
    } else if (b0 == 30) {
      i += 1;
      bool done = false;
      while (!done && i < (int64_t)b.n) {
        int byte = at_u8(err, b, i, "b");
        i += 1;
        if ((byte >> 4) == 0xf || (byte & 0xf) == 0xf) done = true;
      }
      num_push(err, &operands, 0.0);
    } else if (b0 >= 32 && b0 <= 246) {
      num_push(err, &operands, (double)(b0 - 139));
      i += 1;
    } else if (b0 >= 247 && b0 <= 250) {
      num_push(err, &operands, (double)((b0 - 247) * 256 + at_u8(err, b, i + 1, "b") + 108));
      i += 2;
    } else if (b0 >= 251 && b0 <= 254) {
      num_push(err, &operands, (double)(-(b0 - 251) * 256 - at_u8(err, b, i + 1, "b") - 108));
      i += 2;
    } else {
      i += 1;
    }
  }
  free(operands.v);
  return dict;
}

static bool private_entry_of(tt_err *err, const tt_cff_dict *dict,
                             double *out_size, double *out_offset) {
  const tt_cff_dict_entry *entry = cff_dict_get(dict, 18);
  if (entry == NULL || entry->n != 2) return false;
  *out_size = dict_at(err, entry, 0, "entry");
  *out_offset = dict_at(err, entry, 1, "entry");
  return true;
}

static void parse_private_and_local_subrs(tt_err *err, tt_slice buf,
                                          tt_slice view, bool have_private,
                                          double size, double offset,
                                          tt_slice_list *out_subrs,
                                          int64_t *out_bias) {
  out_subrs->v = NULL;
  out_subrs->n = 0;
  out_subrs->cap = 0;
  if (!have_private) {
    *out_bias = subr_bias(0);
    return;
  }
  int64_t off = (int64_t)offset;
  int64_t sz = (int64_t)size;
  /* buf.subarray(offset, offset+size) clamps like a typed array. */
  tt_slice priv;
  if (off < 0) off = 0;
  if (off > (int64_t)buf.n) off = (int64_t)buf.n;
  int64_t end = (int64_t)offset + sz;
  if (end < off) end = off;
  if (end > (int64_t)buf.n) end = (int64_t)buf.n;
  priv.p = buf.p + off;
  priv.n = (size_t)(end - off);
  tt_cff_dict priv_dict = parse_cff_dict(err, priv);
  const tt_cff_dict_entry *subrs_rel = cff_dict_get(&priv_dict, 19);
  if (subrs_rel == NULL || subrs_rel->n == 0) {
    cff_dict_free(&priv_dict);
    *out_bias = subr_bias(0);
    return;
  }
  double rel = dict_at(err, subrs_rel, 0, "subrsRel");
  cff_dict_free(&priv_dict);
  tt_cff_index_result idx = read_cff_index(err, buf, view, (int64_t)offset + (int64_t)rel);
  *out_subrs = idx.items;
  *out_bias = subr_bias((int64_t)idx.items.n);
}

static uint8_t *parse_fd_select(tt_err *err, tt_slice buf, tt_slice view,
                                int64_t offset, int64_t num_glyphs) {
  uint8_t *result = (uint8_t *)tt_calloc(err, (size_t)num_glyphs, 1);
  int format = at_u8(err, buf, offset, "buf");
  if (format == 0) {
    for (int64_t i = 0; i < num_glyphs; i++) {
      result[i] = at_u8(err, buf, offset + 1 + i, "buf");
    }
  } else if (format == 3) {
    int64_t n_ranges = dv_u16(err, view, offset + 1);
    int64_t p = offset + 3;
    int64_t prev_first = dv_u16(err, view, p);
    uint8_t prev_fd = at_u8(err, buf, p + 2, "buf");
    p += 3;
    for (int64_t r = 1; r < n_ranges; r++) {
      int64_t first = dv_u16(err, view, p);
      uint8_t fd = at_u8(err, buf, p + 2, "buf");
      for (int64_t g = prev_first; g < first; g++) {
        /* Typed-array store: out-of-range writes drop silently. */
        if (g >= 0 && g < num_glyphs) result[g] = prev_fd;
      }
      prev_first = first;
      prev_fd = fd;
      p += 3;
    }
    int64_t sentinel = dv_u16(err, view, p);
    for (int64_t g = prev_first; g < sentinel; g++) {
      if (g >= 0 && g < num_glyphs) result[g] = prev_fd;
    }
  }
  return result;
}

static bool parse_cff(tt_err *err, tt_slice buf, int64_t table_offset,
                      int64_t table_length, tt_cff_font *out) {
  memset(out, 0, sizeof *out);
  tt_slice cff;
  cff.p = buf.p + table_offset;
  cff.n = (size_t)table_length;
  if (cff.n < 4) return false;
  int64_t hdr_size = at_u8(err, cff, 2, "cff");
  int64_t pos = hdr_size;
  tt_cff_index_result name_idx = read_cff_index(err, cff, cff, pos);
  pos = name_idx.end;
  free(name_idx.items.v);
  tt_cff_index_result top_idx = read_cff_index(err, cff, cff, pos);
  pos = top_idx.end;
  tt_cff_index_result string_idx = read_cff_index(err, cff, cff, pos);
  pos = string_idx.end;
  free(string_idx.items.v);
  tt_cff_index_result global_idx = read_cff_index(err, cff, cff, pos);
  if (top_idx.items.n == 0) {
    free(top_idx.items.v);
    free(global_idx.items.v);
    return false;
  }
  tt_cff_dict top_dict =
      parse_cff_dict(err, slice_at(err, &top_idx.items, 0, "topDictIdx.items"));
  free(top_idx.items.v);
  const tt_cff_dict_entry *cs_off = cff_dict_get(&top_dict, 17);
  if (cs_off == NULL || cs_off->n == 0) {
    cff_dict_free(&top_dict);
    free(global_idx.items.v);
    return false;
  }
  double cs_offset = dict_at(err, cs_off, 0, "charStringsOff");
  tt_cff_index_result cs_idx = read_cff_index(err, cff, cff, (int64_t)cs_offset);
  out->char_strings = cs_idx.items;
  out->is_cid = cff_dict_get(&top_dict, 1230) != NULL;
  out->global_subrs = global_idx.items;
  out->global_bias = subr_bias((int64_t)out->global_subrs.n);
  double psize = 0, poffset = 0;
  bool have_private = private_entry_of(err, &top_dict, &psize, &poffset);
  parse_private_and_local_subrs(err, cff, cff, have_private, psize, poffset,
                                &out->default_local_subrs,
                                &out->default_local_bias);
  if (out->is_cid) {
    const tt_cff_dict_entry *fd_array_off = cff_dict_get(&top_dict, 1236);
    const tt_cff_dict_entry *fd_select_off = cff_dict_get(&top_dict, 1237);
    double fd_array_offset = 0, fd_select_offset = 0;
    bool have_fd_array = fd_array_off != NULL && fd_array_off->n > 0;
    bool have_fd_select = fd_select_off != NULL && fd_select_off->n > 0;
    if (have_fd_array) fd_array_offset = dict_at(err, fd_array_off, 0, "fdArrayOff");
    if (have_fd_select) fd_select_offset = dict_at(err, fd_select_off, 0, "fdSelectOff");
    if (have_fd_array) {
      tt_cff_index_result fd_idx =
          read_cff_index(err, cff, cff, (int64_t)fd_array_offset);
      out->fd_count = fd_idx.items.n;
      out->fd_local_subrs = (tt_slice_list *)tt_calloc(
          err, out->fd_count, sizeof(tt_slice_list));
      out->fd_local_bias =
          (int64_t *)tt_calloc(err, out->fd_count, sizeof(int64_t));
      for (size_t k = 0; k < fd_idx.items.n; k++) {
        tt_cff_dict fd_dict = parse_cff_dict(err, fd_idx.items.v[k]);
        double s2 = 0, o2 = 0;
        bool hp = private_entry_of(err, &fd_dict, &s2, &o2);
        cff_dict_free(&fd_dict);
        parse_private_and_local_subrs(err, cff, cff, hp, s2, o2,
                                      &out->fd_local_subrs[k],
                                      &out->fd_local_bias[k]);
      }
      free(fd_idx.items.v);
    }
    if (have_fd_select) {
      out->fd_select = parse_fd_select(err, cff, cff, (int64_t)fd_select_offset,
                                       (int64_t)out->char_strings.n);
      out->fd_select_len = out->char_strings.n;
    }
  }
  cff_dict_free(&top_dict);
  return true;
}

/* --- Type2 charstrings --- */

typedef struct tt_type2_ctx {
  double x;
  double y;
  tt_flat_builder contours;
  qa_flattened_contour current;
  size_t current_cap;
  bool has_current;
  tt_num_list stack;
  int64_t n_stems;
  bool width_parsed;
} tt_type2_ctx;

static void t2_push_current(tt_err *err, tt_type2_ctx *ctx) {
  qa_flattened_contour *c = flat_builder_new(err, &ctx->contours);
  *c = ctx->current;
  ctx->contours.caps[ctx->contours.set.count - 1] = ctx->current_cap;
  ctx->current.points = NULL;
  ctx->current.count = 0;
  ctx->current_cap = 0;
  ctx->has_current = false;
}

static void t2_move_to(tt_err *err, tt_type2_ctx *ctx, double dx, double dy) {
  if (ctx->has_current && ctx->current.count > 0) t2_push_current(err, ctx);
  free(ctx->current.points);
  ctx->current.points = NULL;
  ctx->current.count = 0;
  ctx->current_cap = 0;
  ctx->x += dx;
  ctx->y += dy;
  qa_tt_point pt = {ctx->x, ctx->y};
  flat_contour_push(err, &ctx->current, &ctx->current_cap, pt);
  ctx->has_current = true;
}

static void t2_line_to(tt_err *err, tt_type2_ctx *ctx, double dx, double dy) {
  ctx->x += dx;
  ctx->y += dy;
  qa_tt_point pt = {ctx->x, ctx->y};
  flat_contour_push(err, &ctx->current, &ctx->current_cap, pt);
  ctx->has_current = true;
}

static void t2_emit_curve_abs(tt_err *err, tt_type2_ctx *ctx, double c1x,
                              double c1y, double c2x, double c2y, double ex,
                              double ey) {
  if (!ctx->has_current) {
    qa_tt_point pt = {ctx->x, ctx->y};
    flat_contour_push(err, &ctx->current, &ctx->current_cap, pt);
    ctx->has_current = true;
  }
  double p0x = ctx->x;
  double p0y = ctx->y;
  const int segments = 8;
  for (int s = 1; s <= segments; s++) {
    double t = (double)s / (double)segments;
    double mt = 1.0 - t;
    qa_tt_point pt;
    pt.x = mt * mt * mt * p0x + 3.0 * mt * mt * t * c1x +
           3.0 * mt * t * t * c2x + t * t * t * ex;
    pt.y = mt * mt * mt * p0y + 3.0 * mt * mt * t * c1y +
           3.0 * mt * t * t * c2y + t * t * t * ey;
    flat_contour_push(err, &ctx->current, &ctx->current_cap, pt);
  }
  ctx->x = ex;
  ctx->y = ey;
}

static void t2_curve_to(tt_err *err, tt_type2_ctx *ctx, double dx1, double dy1,
                        double dx2, double dy2, double dx3, double dy3) {
  double c1x = ctx->x + dx1;
  double c1y = ctx->y + dy1;
  double c2x = c1x + dx2;
  double c2y = c1y + dy2;
  t2_emit_curve_abs(err, ctx, c1x, c1y, c2x, c2y, c2x + dx3, c2y + dy3);
}

static void t2_maybe_take_width(tt_type2_ctx *ctx, bool has_extra) {
  if (ctx->width_parsed) return;
  ctx->width_parsed = true;
  if (has_extra && ctx->stack.n > 0) {
    memmove(ctx->stack.v, ctx->stack.v + 1,
            (ctx->stack.n - 1) * sizeof(double));
    ctx->stack.n -= 1;
  }
}

static double stack_at(tt_err *err, const tt_num_list *s, int64_t i) {
  return num_at(err, s, i, "stack");
}

static double s_at(tt_err *err, const tt_num_list *s, int64_t i) {
  return num_at(err, s, i, "s");
}

static void exec_hflex(tt_err *err, tt_type2_ctx *ctx, const tt_num_list *s) {
  double dx1 = s_at(err, s, 0), dx2 = s_at(err, s, 1), dy2 = s_at(err, s, 2);
  double dx3 = s_at(err, s, 3), dx4 = s_at(err, s, 4), dx5 = s_at(err, s, 5);
  double dx6 = s_at(err, s, 6);
  double x0 = ctx->x, y0 = ctx->y;
  double c1x = x0 + dx1, c1y = y0;
  double c2x = c1x + dx2, c2y = c1y + dy2;
  double p1x = c2x + dx3;
  t2_emit_curve_abs(err, ctx, c1x, c1y, c2x, c2y, p1x, c2y);
  double c3x = p1x + dx4, c3y = c2y;
  double c4x = c3x + dx5;
  t2_emit_curve_abs(err, ctx, c3x, c3y, c4x, c3y, c4x + dx6, y0);
}

static void exec_flex(tt_err *err, tt_type2_ctx *ctx, const tt_num_list *s) {
  t2_curve_to(err, ctx, s_at(err, s, 0), s_at(err, s, 1), s_at(err, s, 2),
              s_at(err, s, 3), s_at(err, s, 4), s_at(err, s, 5));
  t2_curve_to(err, ctx, s_at(err, s, 6), s_at(err, s, 7), s_at(err, s, 8),
              s_at(err, s, 9), s_at(err, s, 10), s_at(err, s, 11));
}

static void exec_hflex1(tt_err *err, tt_type2_ctx *ctx, const tt_num_list *s) {
  double dx1 = s_at(err, s, 0), dy1 = s_at(err, s, 1), dx2 = s_at(err, s, 2);
  double dy2 = s_at(err, s, 3), dx3 = s_at(err, s, 4), dx4 = s_at(err, s, 5);
  double dx5 = s_at(err, s, 6), dy5 = s_at(err, s, 7), dx6 = s_at(err, s, 8);
  double x0 = ctx->x, y0 = ctx->y;
  double c1x = x0 + dx1, c1y = y0 + dy1;
  double c2x = c1x + dx2, c2y = c1y + dy2;
  double p1x = c2x + dx3;
  t2_emit_curve_abs(err, ctx, c1x, c1y, c2x, c2y, p1x, c2y);
  double c3x = p1x + dx4, c3y = c2y;
  double c4x = c3x + dx5, c4y = c3y + dy5;
  t2_emit_curve_abs(err, ctx, c3x, c3y, c4x, c4y, c4x + dx6, y0);
}

static void exec_flex1(tt_err *err, tt_type2_ctx *ctx, const tt_num_list *s) {
  double dx1 = s_at(err, s, 0), dy1 = s_at(err, s, 1), dx2 = s_at(err, s, 2);
  double dy2 = s_at(err, s, 3), dx3 = s_at(err, s, 4), dy3 = s_at(err, s, 5);
  double dx4 = s_at(err, s, 6), dy4 = s_at(err, s, 7), dx5 = s_at(err, s, 8);
  double dy5 = s_at(err, s, 9), d6 = s_at(err, s, 10);
  double x0 = ctx->x, y0 = ctx->y;
  double c1x = x0 + dx1, c1y = y0 + dy1;
  double c2x = c1x + dx2, c2y = c1y + dy2;
  double p1x = c2x + dx3, p1y = c2y + dy3;
  t2_emit_curve_abs(err, ctx, c1x, c1y, c2x, c2y, p1x, p1y);
  double c3x = p1x + dx4, c3y = p1y + dy4;
  double c4x = c3x + dx5, c4y = c3y + dy5;
  double sum_dx = dx1 + dx2 + dx3 + dx4 + dx5;
  double sum_dy = dy1 + dy2 + dy3 + dy4 + dy5;
  if (fabs(sum_dx) > fabs(sum_dy)) {
    t2_emit_curve_abs(err, ctx, c3x, c3y, c4x, c4y, c4x + d6, y0);
  } else {
    t2_emit_curve_abs(err, ctx, c3x, c3y, c4x, c4y, x0, c4y + d6);
  }
}

static void exec_escape_op(tt_err *err, int op, tt_type2_ctx *ctx,
                           tt_num_list *stack) {
  switch (op) {
    case 34: exec_hflex(err, ctx, stack); break;
    case 35: exec_flex(err, ctx, stack); break;
    case 36: exec_hflex1(err, ctx, stack); break;
    case 37: exec_flex1(err, ctx, stack); break;
    default: break;
  }
  stack->n = 0;
}

static tt_slice subr_at(tt_err *err, const tt_slice_list *list, double index,
                        const char *ctx) {
  if (index != trunc(index) || index < 0 || index >= (double)list->n) {
    if (index == trunc(index) && fabs(index) < 9.0e15) {
      tt_fail(err, "Missing %s at index %lld", ctx, (long long)index);
    }
    tt_fail(err, "Missing %s at index %g", ctx, index);
  }
  return list->v[(size_t)index];
}

static void exec_charstring(tt_err *err, tt_slice code, tt_type2_ctx *ctx,
                            const tt_slice_list *global_subrs,
                            int64_t global_bias,
                            const tt_slice_list *local_subrs,
                            int64_t local_bias, int64_t depth) {
  if (depth > 10) return;
  tt_num_list *stack = &ctx->stack;
  int64_t i = 0;
  while (i < (int64_t)code.n) {
    int b0 = at_u8(err, code, i, "code");
    if (b0 >= 32 || b0 == 28) {
      double v;
      if (b0 == 28) {
        v = (double)read_int16_be(err, code, i + 1);
        i += 3;
      } else if (b0 <= 246) {
        v = (double)(b0 - 139);
        i += 1;
      } else if (b0 <= 250) {
        v = (double)((b0 - 247) * 256 + at_u8(err, code, i + 1, "code") + 108);
        i += 2;
      } else if (b0 <= 254) {
        v = (double)(-(b0 - 251) * 256 - at_u8(err, code, i + 1, "code") - 108);
        i += 2;
      } else {
        v = (double)read_int32_be(err, code, i + 1) / 65536.0;
        i += 5;
      }
      num_push(err, stack, v);
      continue;
    }
    i += 1;
    switch (b0) {
      case 1:
      case 3:
      case 18:
      case 23: {
        t2_maybe_take_width(ctx, stack->n % 2 == 1);
        ctx->n_stems += (int64_t)(stack->n / 2);
        stack->n = 0;
        break;
      }
      case 19:
      case 20: {
        t2_maybe_take_width(ctx, stack->n % 2 == 1);
        ctx->n_stems += (int64_t)(stack->n / 2);
        stack->n = 0;
        i += (ctx->n_stems + 7) >> 3;
        break;
      }
      case 21: {
        t2_maybe_take_width(ctx, stack->n > 2);
        double a = stack_at(err, stack, 0), b = stack_at(err, stack, 1);
        t2_move_to(err, ctx, a, b);
        stack->n = 0;
        break;
      }
      case 22: {
        t2_maybe_take_width(ctx, stack->n > 1);
        double a = stack_at(err, stack, 0);
        t2_move_to(err, ctx, a, 0);
        stack->n = 0;
        break;
      }
      case 4: {
        t2_maybe_take_width(ctx, stack->n > 1);
        double a = stack_at(err, stack, 0);
        t2_move_to(err, ctx, 0, a);
        stack->n = 0;
        break;
      }
      case 5: {
        for (int64_t k = 0; k + 1 < (int64_t)stack->n; k += 2) {
          double a = stack_at(err, stack, k), b = stack_at(err, stack, k + 1);
          t2_line_to(err, ctx, a, b);
        }
        stack->n = 0;
        break;
      }
      case 6:
      case 7: {
        bool horiz = b0 == 6;
        for (int64_t k = 0; k < (int64_t)stack->n; k++) {
          double a = stack_at(err, stack, k);
          if (horiz) t2_line_to(err, ctx, a, 0);
          else t2_line_to(err, ctx, 0, a);
          horiz = !horiz;
        }
        stack->n = 0;
        break;
      }
      case 8: {
        for (int64_t k = 0; k + 5 < (int64_t)stack->n; k += 6) {
          t2_curve_to(err, ctx, stack_at(err, stack, k),
                      stack_at(err, stack, k + 1), stack_at(err, stack, k + 2),
                      stack_at(err, stack, k + 3), stack_at(err, stack, k + 4),
                      stack_at(err, stack, k + 5));
        }
        stack->n = 0;
        break;
      }
      case 24: {
        int64_t k = 0;
        for (; k + 5 < (int64_t)stack->n - 2; k += 6) {
          t2_curve_to(err, ctx, stack_at(err, stack, k),
                      stack_at(err, stack, k + 1), stack_at(err, stack, k + 2),
                      stack_at(err, stack, k + 3), stack_at(err, stack, k + 4),
                      stack_at(err, stack, k + 5));
        }
        double a = stack_at(err, stack, k), b = stack_at(err, stack, k + 1);
        t2_line_to(err, ctx, a, b);
        stack->n = 0;
        break;
      }
      case 25: {
        int64_t k = 0;
        for (; k + 1 < (int64_t)stack->n - 6; k += 2) {
          double a = stack_at(err, stack, k), b = stack_at(err, stack, k + 1);
          t2_line_to(err, ctx, a, b);
        }
        t2_curve_to(err, ctx, stack_at(err, stack, k),
                    stack_at(err, stack, k + 1), stack_at(err, stack, k + 2),
                    stack_at(err, stack, k + 3), stack_at(err, stack, k + 4),
                    stack_at(err, stack, k + 5));
        stack->n = 0;
        break;
      }
      case 26: {
        int64_t k = 0;
        double dx1 = 0;
        if (stack->n % 4 == 1) {
          dx1 = stack_at(err, stack, 0);
          k = 1;
        }
        for (; k + 3 < (int64_t)stack->n; k += 4) {
          double c1x = ctx->x + dx1;
          double c1y = ctx->y + stack_at(err, stack, k);
          double c2x = c1x + stack_at(err, stack, k + 1);
          double c2y = c1y + stack_at(err, stack, k + 2);
          t2_emit_curve_abs(err, ctx, c1x, c1y, c2x, c2y, c2x,
                            c2y + stack_at(err, stack, k + 3));
          dx1 = 0;
        }
        stack->n = 0;
        break;
      }
      case 27: {
        int64_t k = 0;
        double dy1 = 0;
        if (stack->n % 4 == 1) {
          dy1 = stack_at(err, stack, 0);
          k = 1;
        }
        for (; k + 3 < (int64_t)stack->n; k += 4) {
          double c1x = ctx->x + stack_at(err, stack, k);
          double c1y = ctx->y + dy1;
          double c2x = c1x + stack_at(err, stack, k + 1);
          double c2y = c1y + stack_at(err, stack, k + 2);
          t2_emit_curve_abs(err, ctx, c1x, c1y, c2x, c2y,
                            c2x + stack_at(err, stack, k + 3), c2y);
          dy1 = 0;
        }
        stack->n = 0;
        break;
      }
      case 30:
      case 31: {
        bool horiz = b0 == 31;
        int64_t k = 0;
        while (k + 3 < (int64_t)stack->n) {
          bool has_extra = k + 5 == (int64_t)stack->n;
          if (horiz) {
            double c1x = ctx->x + stack_at(err, stack, k);
            double c1y = ctx->y;
            double c2x = c1x + stack_at(err, stack, k + 1);
            double c2y = c1y + stack_at(err, stack, k + 2);
            double ey = c2y + stack_at(err, stack, k + 3);
            double ex = has_extra ? c2x + stack_at(err, stack, k + 4) : c2x;
            t2_emit_curve_abs(err, ctx, c1x, c1y, c2x, c2y, ex, ey);
          } else {
            double c1x = ctx->x;
            double c1y = ctx->y + stack_at(err, stack, k);
            double c2x = c1x + stack_at(err, stack, k + 1);
            double c2y = c1y + stack_at(err, stack, k + 2);
            double ex = c2x + stack_at(err, stack, k + 3);
            double ey = has_extra ? c2y + stack_at(err, stack, k + 4) : c2y;
            t2_emit_curve_abs(err, ctx, c1x, c1y, c2x, c2y, ex, ey);
          }
          horiz = !horiz;
          k += 4;
        }
        stack->n = 0;
        break;
      }
      case 10: {
        if (stack->n == 0) tt_fail(err, "CFF callsubr has no operand");
        double operand = stack->v[--stack->n];
        tt_slice sub = subr_at(err, local_subrs, operand + (double)local_bias,
                               "CFF local subroutine");
        exec_charstring(err, sub, ctx, global_subrs, global_bias, local_subrs,
                        local_bias, depth + 1);
        break;
      }
      case 29: {
        if (stack->n == 0) tt_fail(err, "CFF callgsubr has no operand");
        double operand = stack->v[--stack->n];
        tt_slice sub = subr_at(err, global_subrs, operand + (double)global_bias,
                               "CFF global subroutine");
        exec_charstring(err, sub, ctx, global_subrs, global_bias, local_subrs,
                        local_bias, depth + 1);
        break;
      }
      case 11: {
        return;
      }
      case 14: {
        t2_maybe_take_width(ctx, stack->n == 1 || stack->n == 5);
        return;
      }
      case 12: {
        int b1 = at_u8(err, code, i, "code");
        i += 1;
        exec_escape_op(err, b1, ctx, stack);
        break;
      }
      default: {
        stack->n = 0;
        break;
      }
    }
  }
}

static qa_tt_contours get_cff_contours(tt_err *err, const tt_cff_font *cff,
                                       int64_t gid) {
  qa_tt_contours empty = {NULL, 0};
  if (gid < 0 || (uint64_t)gid >= (uint64_t)cff->char_strings.n) return empty;
  tt_slice code = cff->char_strings.v[gid];
  const tt_slice_list *local_subrs = &cff->default_local_subrs;
  int64_t local_bias = cff->default_local_bias;
  if (cff->is_cid && cff->fd_select != NULL) {
    tt_slice fds;
    fds.p = cff->fd_select;
    fds.n = cff->fd_select_len;
    int fd = at_u8(err, fds, gid, "CFF glyph FD selection");
    if (fd < 0 || (size_t)fd >= cff->fd_count) {
      tt_fail(err, "Missing CFF font dictionary at index %d", fd);
    }
    local_subrs = &cff->fd_local_subrs[fd];
    local_bias = cff->fd_local_bias[fd];
  }
  tt_type2_ctx ctx;
  memset(&ctx, 0, sizeof ctx);
  exec_charstring(err, code, &ctx, &cff->global_subrs, cff->global_bias,
                  local_subrs, local_bias, 0);
  if (ctx.has_current && ctx.current.count > 0) {
    t2_push_current(err, &ctx);
  } else {
    free(ctx.current.points);
  }
  free(ctx.stack.v);
  free(ctx.contours.caps);
  return ctx.contours.set;
}

/* --- kern / COLR / CPAL --- */

typedef struct tt_kern_pair {
  int64_t key;
  int64_t value;
} tt_kern_pair;

typedef struct tt_colr_base {
  int64_t gid;
  int64_t first_layer_index;
  int64_t num_layers;
} tt_colr_base;

struct qa_parsed_font {
  uint8_t *buf;
  size_t length;
  int64_t units_per_em;
  int64_t num_glyphs;
  int64_t ascent;
  int64_t descent;
  int64_t line_gap;
  qa_outline_format outline_format;
  uint16_t *advance_widths;
  int cmap_format;
  int64_t cmap_offset;
  /* glyf */
  uint32_t glyf_offset;
  uint32_t glyf_length;
  uint32_t *loca_offsets;
  tt_raw_set *raw_cache;
  bool *raw_valid;
  /* shared flat cache, flat_cache_len entries */
  qa_tt_contours *flat_cache;
  bool *flat_valid;
  size_t flat_cache_len;
  /* cff */
  tt_cff_font cff;
  /* kern */
  tt_kern_pair *kern_pairs;
  size_t kern_count;
  size_t kern_cap;
  /* COLR */
  bool has_colr;
  tt_colr_base *colr_base;
  size_t colr_base_count;
  qa_color_layer *colr_layers;
  size_t colr_layer_count;
  /* CPAL */
  bool has_cpal;
  int64_t cpal_num_entries;
  int64_t cpal_num_palettes;
  uint16_t *cpal_indices;
  qa_tt_color *cpal_records;
  size_t cpal_record_count;
};

static void parse_kern_format0(tt_err *err, qa_parsed_font *font, tt_slice view,
                               int64_t offset, int64_t length) {
  if (length < 4) return;
  if (dv_u16(err, view, offset) != 0) return;
  int64_t n_tables = dv_u16(err, view, offset + 2);
  int64_t pos = offset + 4;
  for (int64_t t = 0; t < n_tables && pos + 6 <= offset + length; t++) {
    int sub_version = dv_u16(err, view, pos);
    int64_t sub_length = dv_u16(err, view, pos + 2);
    int coverage = dv_u16(err, view, pos + 4);
    int format = coverage >> 8;
    if (sub_version == 0 && format == 0) {
      int64_t n_pairs = dv_u16(err, view, pos + 6);
      int64_t p = pos + 14;
      for (int64_t i = 0; i < n_pairs; i++) {
        int64_t left = dv_u16(err, view, p);
        int64_t right = dv_u16(err, view, p + 2);
        int64_t value = dv_i16(err, view, p + 4);
        int64_t key = left * 65536 + right;
        size_t slot = font->kern_count;
        for (size_t k = 0; k < font->kern_count; k++) {
          if (font->kern_pairs[k].key == key) slot = k;
        }
        font->kern_pairs = (tt_kern_pair *)tt_grow(
            err, font->kern_pairs, &font->kern_cap, slot + 1,
            sizeof(tt_kern_pair));
        font->kern_pairs[slot].key = key;
        font->kern_pairs[slot].value = value;
        if (slot == font->kern_count) font->kern_count += 1;
        p += 6;
      }
    }
    pos += sub_length;
  }
}

static void parse_colr(tt_err *err, qa_parsed_font *font, tt_slice view,
                       int64_t offset, int64_t length) {
  if (length < 14) return;
  if (dv_u16(err, view, offset) != 0) return;
  int64_t num_base = dv_u16(err, view, offset + 2);
  int64_t base_offset = (int64_t)dv_u32(err, view, offset + 4);
  int64_t layer_offset = (int64_t)dv_u32(err, view, offset + 8);
  int64_t num_layers = dv_u16(err, view, offset + 12);
  font->colr_base =
      (tt_colr_base *)tt_calloc(err, (size_t)num_base, sizeof(tt_colr_base));
  font->colr_base_count = (size_t)num_base;
  int64_t bp = offset + base_offset;
  for (int64_t i = 0; i < num_base; i++) {
    font->colr_base[i].gid = dv_u16(err, view, bp);
    font->colr_base[i].first_layer_index = dv_u16(err, view, bp + 2);
    font->colr_base[i].num_layers = dv_u16(err, view, bp + 4);
    bp += 6;
  }
  font->colr_layers = (qa_color_layer *)tt_calloc(err, (size_t)num_layers,
                                                  sizeof(qa_color_layer));
  font->colr_layer_count = (size_t)num_layers;
  int64_t lp = offset + layer_offset;
  for (int64_t i = 0; i < num_layers; i++) {
    font->colr_layers[i].gid = dv_u16(err, view, lp);
    font->colr_layers[i].palette_index = dv_u16(err, view, lp + 2);
    lp += 4;
  }
  font->has_colr = true;
}

static void parse_cpal(tt_err *err, qa_parsed_font *font, tt_slice view,
                       int64_t offset, int64_t length) {
  if (length < 12) return;
  font->cpal_num_entries = dv_u16(err, view, offset + 2);
  font->cpal_num_palettes = dv_u16(err, view, offset + 4);
  int64_t num_records = dv_u16(err, view, offset + 6);
  int64_t records_offset = (int64_t)dv_u32(err, view, offset + 8);
  font->cpal_indices = (uint16_t *)tt_calloc(
      err, (size_t)font->cpal_num_palettes, sizeof(uint16_t));
  int64_t idx_base = offset + 12;
  for (int64_t i = 0; i < font->cpal_num_palettes; i++) {
    font->cpal_indices[i] = dv_u16(err, view, idx_base + i * 2);
  }
  font->cpal_records =
      (qa_tt_color *)tt_calloc(err, (size_t)num_records, sizeof(qa_tt_color));
  font->cpal_record_count = (size_t)num_records;
  int64_t rec_base = offset + records_offset;
  for (int64_t i = 0; i < num_records; i++) {
    int64_t p = rec_base + i * 4;
    font->cpal_records[i].b = (double)dv_u8(err, view, p);
    font->cpal_records[i].g = (double)dv_u8(err, view, p + 1);
    font->cpal_records[i].r = (double)dv_u8(err, view, p + 2);
    font->cpal_records[i].a = (double)dv_u8(err, view, p + 3);
  }
  font->has_cpal = true;
}

/* --- glyf raw glyph resolution (getRaw) --- */

typedef struct tt_glyph_ctx {
  qa_parsed_font *font;
  tt_err *err;
} tt_glyph_ctx;

static tt_raw_set get_raw(void *ctx_ptr, int64_t gid, int64_t depth) {
  tt_glyph_ctx *ctx = (tt_glyph_ctx *)ctx_ptr;
  qa_parsed_font *font = ctx->font;
  tt_err *err = ctx->err;
  tt_raw_set empty = {NULL, 0, 0};
  if (depth == 0 && gid >= 0 && gid < font->num_glyphs &&
      font->raw_valid[gid]) {
    return font->raw_cache[gid];
  }
  if (gid < 0 || gid >= font->num_glyphs) return empty;
  if (depth > 8) {
    tt_fail(err, "Composite glyph recursion exceeds eight levels");
  }
  uint32_t start = font->loca_offsets[gid];
  uint32_t end = font->loca_offsets[gid + 1];
  if (end < start || end > font->glyf_length) {
    tt_fail(err, "Glyph location exceeds glyf table bounds");
  }
  if (end == start) return empty;
  tt_slice glyph;
  glyph.p = font->buf + font->glyf_offset + start;
  glyph.n = (size_t)(end - start);
  int num_contours = dv_i16(err, glyph, 0);
  tt_raw_set result = num_contours >= 0
      ? parse_simple_glyph(err, glyph, glyph, 10, num_contours)
      : parse_composite_glyph(err, glyph, 10, get_raw, ctx, depth);
  if (depth == 0) {
    font->raw_cache[gid] = result;
    font->raw_valid[gid] = true;
  }
  return result;
}

/* --- parseFont --- */

static const char *parse_font_data(tt_err *err, qa_parsed_font *font) {
  tt_slice view;
  view.p = font->buf;
  view.n = font->length;
  tt_table_record *tables = NULL;
  size_t table_count = 0;
  if (!read_sfnt_directory(err, view, &tables, &table_count)) {
    return "not a recognized sfnt file (bad signature)";
  }
  const tt_table_record *head_t = find_table(tables, table_count, "head");
  const tt_table_record *hhea_t = find_table(tables, table_count, "hhea");
  const tt_table_record *maxp_t = find_table(tables, table_count, "maxp");
  const tt_table_record *hmtx_t = find_table(tables, table_count, "hmtx");
  const tt_table_record *cmap_t = find_table(tables, table_count, "cmap");
  const char *fail = NULL;
  if (head_t == NULL || hhea_t == NULL || maxp_t == NULL || hmtx_t == NULL ||
      cmap_t == NULL) {
    fail = "missing a required table (head/hhea/maxp/hmtx/cmap)";
  } else if (head_t->length < 54) {
    fail = "head table too short";
  } else if (hhea_t->length < 36) {
    fail = "hhea table too short";
  } else if (maxp_t->length < 6) {
    fail = "maxp table too short";
  }
  if (fail != NULL) {
    free(tables);
    return fail;
  }
  font->units_per_em = dv_u16(err, view, (int64_t)head_t->offset + 18);
  if (font->units_per_em == 0) {
    free(tables);
    return "invalid unitsPerEm";
  }
  int64_t index_to_loc_format = dv_i16(err, view, (int64_t)head_t->offset + 50);
  font->ascent = dv_i16(err, view, (int64_t)hhea_t->offset + 4);
  font->descent = dv_i16(err, view, (int64_t)hhea_t->offset + 6);
  font->line_gap = dv_i16(err, view, (int64_t)hhea_t->offset + 8);
  int64_t num_h_metrics = dv_u16(err, view, (int64_t)hhea_t->offset + 34);
  font->num_glyphs = dv_u16(err, view, (int64_t)maxp_t->offset + 4);
  if (num_h_metrics < 1 || num_h_metrics > font->num_glyphs ||
      (int64_t)hmtx_t->length <
          num_h_metrics * 4 + (font->num_glyphs - num_h_metrics) * 2) {
    free(tables);
    return "invalid horizontal metrics";
  }
  font->advance_widths = (uint16_t *)tt_calloc(
      err, (size_t)font->num_glyphs, sizeof(uint16_t));
  {
    int64_t h_off = (int64_t)hmtx_t->offset;
    uint16_t last_advance = 0;
    for (int64_t g = 0; g < font->num_glyphs; g++) {
      if (g < num_h_metrics) {
        last_advance = dv_u16(err, view, h_off);
        h_off += 4;
      }
      font->advance_widths[g] = last_advance;
    }
  }
  if (!select_cmap_subtable(err, view, (int64_t)cmap_t->offset,
                            &font->cmap_format, &font->cmap_offset)) {
    free(tables);
    return "no supported cmap subtable (need format 4 or 12)";
  }
  const tt_table_record *glyf_t = find_table(tables, table_count, "glyf");
  const tt_table_record *loca_t = find_table(tables, table_count, "loca");
  const tt_table_record *cff_t = find_table(tables, table_count, "CFF ");
  if (glyf_t != NULL && loca_t != NULL) {
    font->outline_format = QA_OUTLINE_GLYF;
    if ((index_to_loc_format != 0 && index_to_loc_format != 1) ||
        (int64_t)loca_t->length <
            (font->num_glyphs + 1) * (index_to_loc_format == 0 ? 2 : 4)) {
      free(tables);
      return "invalid loca table";
    }
    font->glyf_offset = glyf_t->offset;
    font->glyf_length = glyf_t->length;
    font->loca_offsets = read_loca(err, view, (int64_t)loca_t->offset,
                                   font->num_glyphs, index_to_loc_format);
    font->raw_cache = (tt_raw_set *)tt_calloc(err, (size_t)font->num_glyphs,
                                              sizeof(tt_raw_set));
    font->raw_valid =
        (bool *)tt_calloc(err, (size_t)font->num_glyphs, sizeof(bool));
    font->flat_cache_len = (size_t)font->num_glyphs;
  } else if (cff_t != NULL) {
    font->outline_format = QA_OUTLINE_CFF;
    if (!parse_cff(err, view, (int64_t)cff_t->offset, (int64_t)cff_t->length,
                   &font->cff)) {
      free(tables);
      return "malformed CFF table";
    }
    font->flat_cache_len = font->cff.char_strings.n;
  } else {
    free(tables);
    return "no outline table found (need glyf+loca or CFF )";
  }
  font->flat_cache = (qa_tt_contours *)tt_calloc(err, font->flat_cache_len,
                                                 sizeof(qa_tt_contours));
  font->flat_valid =
      (bool *)tt_calloc(err, font->flat_cache_len, sizeof(bool));
  const tt_table_record *kern_t = find_table(tables, table_count, "kern");
  if (kern_t != NULL) {
    parse_kern_format0(err, font, view, (int64_t)kern_t->offset,
                       (int64_t)kern_t->length);
  }
  const tt_table_record *colr_t = find_table(tables, table_count, "COLR");
  if (colr_t != NULL) {
    parse_colr(err, font, view, (int64_t)colr_t->offset,
               (int64_t)colr_t->length);
  }
  const tt_table_record *cpal_t = find_table(tables, table_count, "CPAL");
  if (cpal_t != NULL) {
    parse_cpal(err, font, view, (int64_t)cpal_t->offset,
               (int64_t)cpal_t->length);
  }
  free(tables);
  return NULL;
}

static char *tt_strdup(const char *s) {
  size_t n = strlen(s) + 1;
  char *copy = (char *)malloc(n);
  if (copy != NULL) memcpy(copy, s, n);
  return copy;
}

qa_parse_font_result qa_truetype_parse_font(const uint8_t *buf, size_t length) {
  qa_parse_font_result result;
  result.ok = false;
  result.reason = NULL;
  result.font = NULL;
  qa_parsed_font *volatile font =
      (qa_parsed_font *)calloc(1, sizeof(qa_parsed_font));
  if (font == NULL) {
    result.reason = tt_strdup("out of memory");
    return result;
  }
  font->buf = (uint8_t *)malloc(length == 0 ? 1 : length);
  if (font->buf == NULL) {
    free(font);
    result.reason = tt_strdup("out of memory");
    return result;
  }
  if (length > 0) memcpy(font->buf, buf, length);
  font->length = length;
  tt_err *volatile err = (tt_err *)calloc(1, sizeof(tt_err));
  if (err == NULL) {
    qa_parsed_font_destroy(font);
    result.reason = tt_strdup("out of memory");
    return result;
  }
  if (setjmp(err->jb) != 0) {
    result.reason = tt_strdup(err->msg);
    free(err);
    qa_parsed_font_destroy(font);
    return result;
  }
  const char *fail = parse_font_data(err, font);
  free(err);
  if (fail != NULL) {
    result.reason = tt_strdup(fail);
    qa_parsed_font_destroy(font);
    return result;
  }
  result.ok = true;
  result.font = font;
  return result;
}

void qa_parse_font_result_destroy(qa_parse_font_result *result) {
  if (result == NULL) return;
  free(result->reason);
  qa_parsed_font_destroy(result->font);
  result->reason = NULL;
  result->font = NULL;
  result->ok = false;
}

void qa_parsed_font_destroy(qa_parsed_font *font) {
  if (font == NULL) return;
  if (font->raw_cache != NULL) {
    for (int64_t i = 0; i < font->num_glyphs; i++) {
      if (font->raw_valid[i]) raw_set_free(&font->raw_cache[i]);
    }
  }
  free(font->raw_cache);
  free(font->raw_valid);
  if (font->flat_cache != NULL) {
    for (size_t i = 0; i < font->flat_cache_len; i++) {
      if (font->flat_valid[i]) flat_set_free(&font->flat_cache[i]);
    }
  }
  free(font->flat_cache);
  free(font->flat_valid);
  cff_font_free(&font->cff);
  free(font->advance_widths);
  free(font->loca_offsets);
  free(font->kern_pairs);
  free(font->colr_base);
  free(font->colr_layers);
  free(font->cpal_indices);
  free(font->cpal_records);
  free(font->buf);
  free(font);
}

int64_t qa_parsed_font_units_per_em(const qa_parsed_font *font) {
  return font->units_per_em;
}
int64_t qa_parsed_font_num_glyphs(const qa_parsed_font *font) {
  return font->num_glyphs;
}
int64_t qa_parsed_font_ascent(const qa_parsed_font *font) {
  return font->ascent;
}
int64_t qa_parsed_font_descent(const qa_parsed_font *font) {
  return font->descent;
}
int64_t qa_parsed_font_line_gap(const qa_parsed_font *font) {
  return font->line_gap;
}
qa_outline_format qa_parsed_font_outline_format(const qa_parsed_font *font) {
  return font->outline_format;
}
const char *qa_parsed_font_outline_format_name(const qa_parsed_font *font) {
  return font->outline_format == QA_OUTLINE_CFF ? "cff" : "glyf";
}

int64_t qa_parsed_font_cmap_lookup(qa_parsed_font *font, int64_t codepoint) {
  tt_err *volatile err = (tt_err *)calloc(1, sizeof(tt_err));
  if (err == NULL) qa_range_error("out of memory");
  if (setjmp(err->jb) != 0) {
    char msg[512];
    memcpy(msg, err->msg, sizeof msg);
    free(err);
    qa_range_error("%s", msg);
  }
  tt_slice view;
  view.p = font->buf;
  view.n = font->length;
  int64_t gid = font->cmap_format == 12
      ? cmap_format12_lookup(err, view, font->cmap_offset, codepoint)
      : cmap_format4_lookup(err, view, font->cmap_offset, codepoint);
  free(err);
  return gid;
}

int64_t qa_parsed_font_advance_width(const qa_parsed_font *font, int64_t gid) {
  if (gid < 0 || gid >= font->num_glyphs) return 0;
  return (int64_t)font->advance_widths[gid];
}

qa_tt_contours qa_parsed_font_contours(qa_parsed_font *font, int64_t gid) {
  qa_tt_contours empty = {NULL, 0};
  if (gid >= 0 && (uint64_t)gid < (uint64_t)font->flat_cache_len &&
      font->flat_valid[gid]) {
    return font->flat_cache[gid];
  }
  tt_err *volatile err = (tt_err *)calloc(1, sizeof(tt_err));
  if (err == NULL) qa_range_error("out of memory");
  if (setjmp(err->jb) != 0) {
    char msg[512];
    memcpy(msg, err->msg, sizeof msg);
    free(err);
    qa_range_error("%s", msg);
  }
  qa_tt_contours flat = {NULL, 0};
  if (font->outline_format == QA_OUTLINE_GLYF) {
    tt_glyph_ctx ctx;
    ctx.font = font;
    ctx.err = err;
    tt_raw_set raw = get_raw(&ctx, gid, 0);
    tt_flat_builder builder;
    memset(&builder, 0, sizeof builder);
    for (size_t i = 0; i < raw.n; i++) {
      qa_flattened_contour fc = flatten_quadratic_contour(err, &raw.v[i]);
      qa_flattened_contour *slot = flat_builder_new(err, &builder);
      *slot = fc;
      builder.caps[builder.set.count - 1] = fc.count;
    }
    free(builder.caps);
    flat = builder.set;
  } else {
    flat = (gid >= 0 && (uint64_t)gid < (uint64_t)font->cff.char_strings.n)
        ? get_cff_contours(err, &font->cff, gid)
        : empty;
  }
  free(err);
  if (gid >= 0 && (uint64_t)gid < (uint64_t)font->flat_cache_len) {
    font->flat_cache[gid] = flat;
    font->flat_valid[gid] = true;
    return flat;
  }
  /* Out-of-cache-range gids yield [] in both TS outline paths; a
   * non-empty result here is impossible, but never leak it. */
  if (flat.count > 0) flat_set_free(&flat);
  return empty;
}

int64_t qa_parsed_font_kerning(const qa_parsed_font *font, int64_t left_gid,
                               int64_t right_gid) {
  int64_t key = left_gid * 65536 + right_gid;
  for (size_t i = 0; i < font->kern_count; i++) {
    if (font->kern_pairs[i].key == key) return font->kern_pairs[i].value;
  }
  return 0;
}

bool qa_parsed_font_color_layers(const qa_parsed_font *font, int64_t gid,
                                 qa_color_layer **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (!font->has_colr) return false;
  const tt_colr_base *rec = NULL;
  for (size_t i = 0; i < font->colr_base_count; i++) {
    if (font->colr_base[i].gid == gid) rec = &font->colr_base[i];
  }
  if (rec == NULL) return false;
  size_t count = 0;
  for (int64_t i = 0; i < rec->num_layers; i++) {
    int64_t idx = rec->first_layer_index + i;
    if (idx >= 0 && (uint64_t)idx < (uint64_t)font->colr_layer_count) {
      count += 1;
    }
  }
  if (count == 0) return true;
  qa_color_layer *layers =
      (qa_color_layer *)malloc(count * sizeof(qa_color_layer));
  if (layers == NULL) return true; /* OOM: count stays 0, per header. */
  size_t w = 0;
  for (int64_t i = 0; i < rec->num_layers; i++) {
    int64_t idx = rec->first_layer_index + i;
    if (idx >= 0 && (uint64_t)idx < (uint64_t)font->colr_layer_count) {
      layers[w++] = font->colr_layers[idx];
    }
  }
  *out = layers;
  *out_count = w;
  return true;
}

bool qa_parsed_font_palette_color(const qa_parsed_font *font,
                                  int64_t palette_index, int64_t palette_id,
                                  qa_tt_color *out) {
  if (!font->has_cpal) return false;
  if (palette_id < 0 || palette_id >= font->cpal_num_palettes) return false;
  if (palette_index < 0 || palette_index >= font->cpal_num_entries) {
    return false;
  }
  int64_t first = (int64_t)font->cpal_indices[palette_id];
  int64_t idx = first + palette_index;
  if (idx < 0 || (uint64_t)idx >= (uint64_t)font->cpal_record_count) {
    return false;
  }
  *out = font->cpal_records[idx];
  return true;
}

/* --- rasterization --- */

static double js_round(double value) { return floor(value + 0.5); }

/* Uint8Array store conversion: truncate toward zero, wrap mod 256. */
static uint8_t tt_to_u8(double value) {
  if (!isfinite(value)) return 0;
  double t = trunc(fmod(value, 256.0));
  if (t < 0) t += 256.0;
  return (uint8_t)t;
}

typedef struct tt_fill_edge {
  double x0;
  double y0;
  double x1;
  double y1;
  int dir;
} tt_fill_edge;

typedef struct tt_scan_x {
  double x;
  int dir;
} tt_scan_x;

static void accumulate_span(float *coverage, int64_t row, int64_t width,
                            double xa, double xb, double weight) {
  double a = xa;
  double b = xb;
  if (b <= a) return;
  if (a < 0) a = 0;
  if (b > (double)width) b = (double)width;
  if (b <= a) return;
  int64_t row_base = row * width;
  int64_t first_px = (int64_t)floor(a);
  int64_t last_px = (int64_t)floor(b - 1e-9);
  if (first_px == last_px) {
    size_t idx = (size_t)(row_base + first_px);
    coverage[idx] = (float)((double)coverage[idx] + (b - a) * weight);
    return;
  }
  size_t first_idx = (size_t)(row_base + first_px);
  coverage[first_idx] =
      (float)((double)coverage[first_idx] + ((double)first_px + 1.0 - a) * weight);
  for (int64_t px = first_px + 1; px < last_px; px++) {
    size_t idx = (size_t)(row_base + px);
    coverage[idx] = (float)((double)coverage[idx] + weight);
  }
  size_t last_idx = (size_t)(row_base + last_px);
  coverage[last_idx] =
      (float)((double)coverage[last_idx] + (b - (double)last_px) * weight);
}

static int scan_x_compare(const void *a, const void *b) {
  double d = ((const tt_scan_x *)a)->x - ((const tt_scan_x *)b)->x;
  if (d < 0) return -1;
  if (d > 0) return 1;
  return 0;
}

void qa_rasterized_glyph_destroy(qa_rasterized_glyph *glyph) {
  if (glyph == NULL) return;
  free(glyph->coverage);
  glyph->coverage = NULL;
  glyph->coverage_length = 0;
  glyph->width = 0;
  glyph->height = 0;
}

bool qa_truetype_rasterize_contours(const qa_flattened_contour *contours,
                                    size_t contour_count, int64_t width,
                                    int64_t height, double supersample_y,
                                    qa_rasterized_glyph *out) {
  memset(out, 0, sizeof *out);
  if (width < 0 || height < 0) {
    qa_range_error("Invalid typed array length: %lld", (long long)(width * height));
  }
  size_t total = (size_t)width * (size_t)height;
  float *coverage = (float *)calloc(total == 0 ? 1 : total, sizeof(float));
  if (coverage == NULL) return false;
  tt_fill_edge *edges = NULL;
  size_t edge_count = 0;
  size_t edge_cap = 0;
  bool oom = false;
  for (size_t ci = 0; ci < contour_count && !oom; ci++) {
    size_t n = contours[ci].count;
    if (n < 2) continue;
    for (size_t i = 0; i < n; i++) {
      qa_tt_point p0 = contours[ci].points[i];
      qa_tt_point p1 = contours[ci].points[(i + 1) % n];
      if (p0.y == p1.y) continue;
      tt_fill_edge e;
      if (p0.y < p1.y) {
        e.x0 = p0.x; e.y0 = p0.y; e.x1 = p1.x; e.y1 = p1.y; e.dir = 1;
      } else {
        e.x0 = p1.x; e.y0 = p1.y; e.x1 = p0.x; e.y1 = p0.y; e.dir = -1;
      }
      if (edge_count == edge_cap) {
        size_t cap = edge_cap == 0 ? 16 : edge_cap * 2;
        tt_fill_edge *next =
            (tt_fill_edge *)realloc(edges, cap * sizeof(tt_fill_edge));
        if (next == NULL) { oom = true; break; }
        edges = next;
        edge_cap = cap;
      }
      edges[edge_count++] = e;
    }
  }
  tt_scan_x *xs = NULL;
  if (!oom && edge_count > 0) {
    xs = (tt_scan_x *)malloc(edge_count * sizeof(tt_scan_x));
    if (xs == NULL) oom = true;
  }
  if (!oom) {
    double inv_ss = 1.0 / supersample_y;
    for (int64_t row = 0; row < height; row++) {
      for (double s = 0; s < supersample_y; s += 1.0) {
        double sy = (double)row + (s + 0.5) * inv_ss;
        size_t xs_count = 0;
        for (size_t ei = 0; ei < edge_count; ei++) {
          const tt_fill_edge *e = &edges[ei];
          if (sy >= e->y0 && sy < e->y1) {
            double t = (sy - e->y0) / (e->y1 - e->y0);
            xs[xs_count].x = e->x0 + t * (e->x1 - e->x0);
            xs[xs_count].dir = e->dir;
            xs_count += 1;
          }
        }
        if (xs_count < 2) continue;
        qsort(xs, xs_count, sizeof(tt_scan_x), scan_x_compare);
        int winding = 0;
        for (size_t k = 0; k + 1 < xs_count; k++) {
          winding += xs[k].dir;
          if (winding != 0) {
            accumulate_span(coverage, row, width, xs[k].x, xs[k + 1].x, inv_ss);
          }
        }
      }
    }
  }
  free(edges);
  free(xs);
  if (oom) {
    free(coverage);
    return false;
  }
  uint8_t *bytes = (uint8_t *)calloc(total == 0 ? 1 : total, 1);
  if (bytes == NULL) {
    free(coverage);
    return false;
  }
  for (size_t i = 0; i < total; i++) {
    double v = (double)coverage[i];
    bytes[i] = v <= 0 ? 0 : v >= 1 ? 255 : tt_to_u8(js_round(v * 255.0));
  }
  free(coverage);
  out->width = width;
  out->height = height;
  out->coverage = bytes;
  out->coverage_length = total;
  return true;
}

/* Scales font contours into freshly allocated pixel contours. */
static bool scale_contours(const qa_tt_contours *src, double scale,
                           int64_t ascent, qa_flattened_contour **out) {
  *out = NULL;
  if (src->count == 0) return true;
  qa_flattened_contour *dst = (qa_flattened_contour *)calloc(
      src->count, sizeof(qa_flattened_contour));
  if (dst == NULL) return false;
  for (size_t i = 0; i < src->count; i++) {
    size_t n = src->contours[i].count;
    if (n == 0) continue;
    dst[i].points = (qa_tt_point *)malloc(n * sizeof(qa_tt_point));
    if (dst[i].points == NULL) {
      for (size_t k = 0; k < i; k++) free(dst[k].points);
      free(dst);
      return false;
    }
    dst[i].count = n;
    for (size_t j = 0; j < n; j++) {
      dst[i].points[j].x = src->contours[i].points[j].x * scale;
      dst[i].points[j].y =
          ((double)ascent - src->contours[i].points[j].y) * scale;
    }
  }
  *out = dst;
  return true;
}

static void scaled_free(qa_flattened_contour *scaled, size_t count) {
  if (scaled == NULL) return;
  for (size_t i = 0; i < count; i++) free(scaled[i].points);
  free(scaled);
}

static double contours_y_min(const qa_tt_contours *contours) {
  double y_min = 0;
  for (size_t i = 0; i < contours->count; i++) {
    for (size_t j = 0; j < contours->contours[i].count; j++) {
      double y = contours->contours[i].points[j].y;
      if (y < y_min) y_min = y;
    }
  }
  return y_min;
}

bool qa_truetype_rasterize_glyph(qa_parsed_font *font, int64_t gid, double px,
                                 qa_rasterized_glyph *out) {
  memset(out, 0, sizeof *out);
  double scale = px / (double)font->units_per_em;
  double w = js_round((double)qa_parsed_font_advance_width(font, gid) * scale);
  int64_t width = (int64_t)(w > 1.0 ? w : 1.0);
  qa_tt_contours contours = qa_parsed_font_contours(font, gid);
  double bottom = contours_y_min(&contours);
  if (bottom > 0) bottom = 0;
  double h = js_round(((double)font->ascent - bottom) * scale);
  int64_t height = (int64_t)(h > 1.0 ? h : 1.0);
  if (contours.count == 0) {
    size_t total = (size_t)width * (size_t)height;
    uint8_t *bytes = (uint8_t *)calloc(total == 0 ? 1 : total, 1);
    if (bytes == NULL) return false;
    out->width = width;
    out->height = height;
    out->coverage = bytes;
    out->coverage_length = total;
    return true;
  }
  qa_flattened_contour *scaled = NULL;
  if (!scale_contours(&contours, scale, font->ascent, &scaled)) return false;
  bool ok = qa_truetype_rasterize_contours(scaled, contours.count, width,
                                           height, 4.0, out);
  scaled_free(scaled, contours.count);
  return ok;
}

void qa_rasterized_color_glyph_destroy(qa_rasterized_color_glyph *glyph) {
  if (glyph == NULL) return;
  free(glyph->pixels);
  glyph->pixels = NULL;
  glyph->pixels_length = 0;
  glyph->width = 0;
  glyph->height = 0;
}

static void composite_over(uint8_t *pixels, size_t i, double src_coverage,
                           qa_tt_color color) {
  double src_a = (src_coverage / 255.0) * (color.a / 255.0);
  if (src_a <= 0) return;
  size_t o = i * 4;
  double dst_a = (double)pixels[o + 3] / 255.0;
  double out_a = src_a + dst_a * (1.0 - src_a);
  if (out_a <= 0) return;
  double dst_factor = dst_a * (1.0 - src_a);
  pixels[o] = tt_to_u8(js_round((color.r * src_a + (double)pixels[o] * dst_factor) / out_a));
  pixels[o + 1] = tt_to_u8(js_round((color.g * src_a + (double)pixels[o + 1] * dst_factor) / out_a));
  pixels[o + 2] = tt_to_u8(js_round((color.b * src_a + (double)pixels[o + 2] * dst_factor) / out_a));
  pixels[o + 3] = tt_to_u8(js_round(out_a * 255.0));
}

bool qa_truetype_rasterize_color_glyph(qa_parsed_font *font, int64_t base_gid,
                                       double px, int64_t palette_id,
                                       const qa_tt_color *foreground,
                                       qa_rasterized_color_glyph *out,
                                       bool *oom) {
  bool local_oom = false;
  if (oom == NULL) oom = &local_oom;
  *oom = false;
  memset(out, 0, sizeof *out);
  qa_tt_color fg = {255.0, 255.0, 255.0, 255.0};
  if (foreground != NULL) fg = *foreground;
  qa_color_layer *layers = NULL;
  size_t layer_count = 0;
  if (!qa_parsed_font_color_layers(font, base_gid, &layers, &layer_count)) {
    return false;
  }
  double scale = px / (double)font->units_per_em;
  double w = js_round((double)qa_parsed_font_advance_width(font, base_gid) * scale);
  int64_t width = (int64_t)(w > 1.0 ? w : 1.0);
  /* Resolved layers borrow contour caches; colors are copied. */
  qa_tt_color *colors = NULL;
  int64_t *layer_gids = NULL;
  size_t resolved = 0;
  if (layer_count > 0) {
    colors = (qa_tt_color *)calloc(layer_count, sizeof(qa_tt_color));
    layer_gids = (int64_t *)calloc(layer_count, sizeof(int64_t));
    if (colors == NULL || layer_gids == NULL) {
      free(colors);
      free(layer_gids);
      free(layers);
      *oom = true;
      return false;
    }
  }
  double y_min = 0;
  for (size_t i = 0; i < layer_count; i++) {
    qa_tt_color color;
    if (layers[i].palette_index == 0xffff) {
      color = fg;
    } else if (!qa_parsed_font_palette_color(font, layers[i].palette_index,
                                             palette_id, &color)) {
      continue;
    }
    qa_tt_contours contours = qa_parsed_font_contours(font, layers[i].gid);
    double ly = contours_y_min(&contours);
    if (ly < y_min) y_min = ly;
    colors[resolved] = color;
    layer_gids[resolved] = layers[i].gid;
    resolved += 1;
  }
  free(layers);
  double bottom = y_min < 0 ? y_min : 0;
  double h = js_round(((double)font->ascent - bottom) * scale);
  int64_t height = (int64_t)(h > 1.0 ? h : 1.0);
  size_t total = (size_t)width * (size_t)height;
  uint8_t *pixels = (uint8_t *)calloc(total == 0 ? 1 : total * 4, 1);
  if (pixels == NULL) {
    free(colors);
    free(layer_gids);
    *oom = true;
    return false;
  }
  for (size_t i = 0; i < resolved; i++) {
    qa_tt_contours contours = qa_parsed_font_contours(font, layer_gids[i]);
    if (contours.count == 0) continue;
    qa_flattened_contour *scaled = NULL;
    if (!scale_contours(&contours, scale, font->ascent, &scaled)) {
      free(colors);
      free(layer_gids);
      free(pixels);
      *oom = true;
      return false;
    }
    qa_rasterized_glyph raster;
    bool ok = qa_truetype_rasterize_contours(scaled, contours.count, width,
                                             height, 4.0, &raster);
    scaled_free(scaled, contours.count);
    if (!ok) {
      free(colors);
      free(layer_gids);
      free(pixels);
      *oom = true;
      return false;
    }
    for (size_t p = 0; p < raster.coverage_length; p++) {
      if (raster.coverage[p] == 0) continue;
      composite_over(pixels, p, (double)raster.coverage[p], colors[i]);
    }
    qa_rasterized_glyph_destroy(&raster);
  }
  free(colors);
  free(layer_gids);
  out->width = width;
  out->height = height;
  out->pixels = pixels;
  out->pixels_length = total * 4;
  return true;
}

/* --- atlas --- */

typedef struct tt_atlas_cell {
  int64_t codepoint;
  int64_t width;
  int64_t height;
  bool color;
  uint8_t *data; /* RGBA when color, coverage otherwise; owned */
} tt_atlas_cell;

static const tt_atlas_cell *g_sort_cells;

static int cell_order_compare(const void *a, const void *b) {
  size_t ia = *(const size_t *)a;
  size_t ib = *(const size_t *)b;
  int64_t d = g_sort_cells[ib].height - g_sort_cells[ia].height;
  if (d < 0) return -1;
  if (d > 0) return 1;
  if (ia < ib) return -1; /* JS sort is stable */
  if (ia > ib) return 1;
  return 0;
}

void qa_font_atlas_destroy(qa_font_atlas *atlas) {
  if (atlas == NULL) return;
  free(atlas->pixels);
  free(atlas->codepoints);
  free(atlas->glyphs);
  memset(atlas, 0, sizeof *atlas);
}

bool qa_font_atlas_rect_for(const qa_font_atlas *atlas, int64_t codepoint,
                            qa_atlas_rect *out) {
  for (size_t i = 0; i < atlas->glyph_count; i++) {
    if (atlas->codepoints[i] == codepoint) {
      *out = atlas->glyphs[i];
      return true;
    }
  }
  return false;
}

bool qa_truetype_build_font_atlas(qa_parsed_font *font,
                                  const int64_t *codepoints, size_t count,
                                  double px, int64_t max_width,
                                  qa_font_atlas *out) {
  memset(out, 0, sizeof *out);
  if (!isfinite(px) || px <= 0 || max_width < 1) {
    qa_range_error("Font atlas pixel size and width must be positive");
  }
  tt_atlas_cell *cells = NULL;
  size_t cell_count = 0;
  size_t cell_cap = 0;
  int64_t line_height = 0;
  bool oom = false;
  for (size_t ci = 0; ci < count && !oom; ci++) {
    int64_t cp = codepoints[ci];
    int64_t gid = qa_parsed_font_cmap_lookup(font, cp);
    if (gid == 0) continue;
    tt_atlas_cell cell;
    memset(&cell, 0, sizeof cell);
    cell.codepoint = cp;
    qa_color_layer *layers = NULL;
    size_t layer_count = 0;
    bool has_layers =
        qa_parsed_font_color_layers(font, gid, &layers, &layer_count);
    free(layers);
    if (has_layers) {
      qa_rasterized_color_glyph raster;
      bool raster_oom = false;
      if (!qa_truetype_rasterize_color_glyph(font, gid, px, 0, NULL, &raster,
                                             &raster_oom)) {
        if (raster_oom) oom = true;
        continue;
      }
      cell.width = raster.width;
      cell.height = raster.height;
      cell.color = true;
      cell.data = raster.pixels;
    } else {
      qa_rasterized_glyph raster;
      if (!qa_truetype_rasterize_glyph(font, gid, px, &raster)) {
        oom = true;
        break;
      }
      cell.width = raster.width;
      cell.height = raster.height;
      cell.color = false;
      cell.data = raster.coverage;
    }
    if (cell.height > line_height) line_height = cell.height;
    if (cell_count == cell_cap) {
      size_t cap = cell_cap == 0 ? 8 : cell_cap * 2;
      tt_atlas_cell *next =
          (tt_atlas_cell *)realloc(cells, cap * sizeof(tt_atlas_cell));
      if (next == NULL) {
        free(cell.data);
        oom = true;
        break;
      }
      cells = next;
      cell_cap = cap;
    }
    cells[cell_count++] = cell;
  }
  size_t *order = NULL;
  int64_t *place_x = NULL;
  int64_t *place_y = NULL;
  int64_t *place_cp = NULL;
  size_t place_count = 0;
  uint8_t *pixels = NULL;
  int64_t atlas_width = max_width;
  int64_t atlas_height = 0;
  if (!oom) {
    order = (size_t *)calloc(cell_count == 0 ? 1 : cell_count, sizeof(size_t));
    place_x = (int64_t *)calloc(cell_count == 0 ? 1 : cell_count, sizeof(int64_t));
    place_y = (int64_t *)calloc(cell_count == 0 ? 1 : cell_count, sizeof(int64_t));
    place_cp = (int64_t *)calloc(cell_count == 0 ? 1 : cell_count, sizeof(int64_t));
    if (order == NULL || place_x == NULL || place_y == NULL || place_cp == NULL) {
      oom = true;
    }
  }
  if (!oom) {
    for (size_t i = 0; i < cell_count; i++) order[i] = i;
    g_sort_cells = cells;
    qsort(order, cell_count, sizeof(size_t), cell_order_compare);
    const int64_t gap = 1;
    for (size_t i = 0; i < cell_count; i++) {
      int64_t need = cells[i].width + gap * 2;
      if (need > atlas_width) atlas_width = need;
    }
    int64_t cursor_x = gap;
    int64_t cursor_y = gap;
    int64_t shelf_height = 0;
    for (size_t oi = 0; oi < cell_count; oi++) {
      const tt_atlas_cell *cell = &cells[order[oi]];
      if (cursor_x + cell->width + gap > atlas_width) {
        cursor_x = gap;
        cursor_y += shelf_height + gap;
        shelf_height = 0;
      }
      size_t slot = place_count;
      for (size_t k = 0; k < place_count; k++) {
        if (place_cp[k] == cell->codepoint) slot = k;
      }
      place_cp[slot] = cell->codepoint;
      place_x[slot] = cursor_x;
      place_y[slot] = cursor_y;
      if (slot == place_count) place_count += 1;
      cursor_x += cell->width + gap;
      if (cell->height > shelf_height) shelf_height = cell->height;
    }
    atlas_height = cursor_y + shelf_height + gap;
    pixels = (uint8_t *)calloc(
        (size_t)(atlas_width * atlas_height) == 0
            ? 1
            : (size_t)(atlas_width * atlas_height) * 4,
        1);
    if (pixels == NULL) oom = true;
  }
  int64_t *out_cps = NULL;
  qa_atlas_rect *out_rects = NULL;
  size_t glyph_count = 0;
  if (!oom) {
    out_cps = (int64_t *)calloc(cell_count == 0 ? 1 : cell_count, sizeof(int64_t));
    out_rects = (qa_atlas_rect *)calloc(cell_count == 0 ? 1 : cell_count,
                                        sizeof(qa_atlas_rect));
    if (out_cps == NULL || out_rects == NULL) oom = true;
  }
  if (!oom) {
    for (size_t i = 0; i < cell_count; i++) {
      const tt_atlas_cell *cell = &cells[i];
      bool found = false;
      int64_t pos_x = 0, pos_y = 0;
      for (size_t k = 0; k < place_count; k++) {
        if (place_cp[k] == cell->codepoint) {
          found = true;
          pos_x = place_x[k];
          pos_y = place_y[k];
        }
      }
      if (!found) continue;
      if (cell->color) {
        for (int64_t y = 0; y < cell->height; y++) {
          for (int64_t x = 0; x < cell->width; x++) {
            size_t so = (size_t)(y * cell->width + x) * 4;
            size_t dst = (size_t)((pos_y + y) * atlas_width + (pos_x + x)) * 4;
            pixels[dst] = cell->data[so];
            pixels[dst + 1] = cell->data[so + 1];
            pixels[dst + 2] = cell->data[so + 2];
            pixels[dst + 3] = cell->data[so + 3];
          }
        }
      } else {
        for (int64_t y = 0; y < cell->height; y++) {
          for (int64_t x = 0; x < cell->width; x++) {
            uint8_t a = cell->data[(size_t)(y * cell->width + x)];
            size_t dst = (size_t)((pos_y + y) * atlas_width + (pos_x + x)) * 4;
            pixels[dst] = 255;
            pixels[dst + 1] = 255;
            pixels[dst + 2] = 255;
            pixels[dst + 3] = a;
          }
        }
      }
      size_t slot = glyph_count;
      for (size_t k = 0; k < glyph_count; k++) {
        if (out_cps[k] == cell->codepoint) slot = k;
      }
      out_cps[slot] = cell->codepoint;
      out_rects[slot].x = pos_x;
      out_rects[slot].y = pos_y;
      out_rects[slot].w = cell->width;
      out_rects[slot].h = cell->height;
      out_rects[slot].color = cell->color;
      if (slot == glyph_count) glyph_count += 1;
    }
  }
  for (size_t i = 0; i < cell_count; i++) free(cells[i].data);
  free(cells);
  free(order);
  free(place_x);
  free(place_y);
  free(place_cp);
  if (oom) {
    free(pixels);
    free(out_cps);
    free(out_rects);
    return false;
  }
  out->width = atlas_width;
  out->height = atlas_height;
  out->pixels = pixels;
  out->pixels_length = (size_t)(atlas_width * atlas_height) * 4;
  out->codepoints = out_cps;
  out->glyphs = out_rects;
  out->glyph_count = glyph_count;
  out->line_height = line_height;
  return true;
}

int64_t *qa_truetype_latin1_codepoints(size_t *out_count) {
  size_t count = (size_t)((0x7e - 0x20 + 1) + (0xff - 0xa0 + 1));
  int64_t *out = (int64_t *)malloc(count * sizeof(int64_t));
  if (out == NULL) {
    *out_count = 0;
    return NULL;
  }
  size_t w = 0;
  for (int64_t cp = 0x20; cp <= 0x7e; cp++) out[w++] = cp;
  for (int64_t cp = 0xa0; cp <= 0xff; cp++) out[w++] = cp;
  *out_count = w;
  return out;
}
