/* Port of src/bots/navigation/nav.ts. Returns false only on OOM; format
 * errors abort via qa_binary_error. GPL-2.0-or-later. */
#include "qa/bots/navigation/nav.h"
#include "qa/core/binary/index.h"
#include <stdlib.h>
#include <string.h>

static char *dups(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
void qa_kex_navigation_asset_destroy(qa_kex_navigation_asset *a) {
  if (!a) return;
  free(a->source); free(a->nodes); free(a->links); free(a->traversals);
  for (size_t i = 0; i < a->entity_count; i++) free(a->entities[i].tail);
  free(a->entities);
  memset(a, 0, sizeof *a);
}
static qa_vec3 vector(qa_binary_reader *r) {
  double x = qa_binary_reader_finite_f32(r);
  double y = qa_binary_reader_finite_f32(r);
  double z = qa_binary_reader_finite_f32(r);
  return (qa_vec3){x, y, z};
}
static int64_t count(qa_binary_reader *r, int64_t stride, const char *source) {
  size_t offset = qa_binary_reader_offset(r);
  int32_t value = qa_binary_reader_i32(r);
  if (value < 0 || value > (int64_t)qa_binary_reader_length(r) / stride)
    qa_binary_error(source, (int64_t)offset, "invalid navigation record count");
  return value;
}
bool qa_parse_kex_navigation(qa_kex_navigation_asset *out, uint8_t *bytes, size_t length, const char *source) {
  memset(out, 0, sizeof *out);
  if (!source) source = "<nav>";
  qa_binary_reader r;
  if (!qa_binary_reader_init(&r, bytes, length, source)) return false;
  char *magic = qa_binary_reader_fixed_string(&r, 4);
  bool nav2 = magic && !strcmp(magic, "NAV2"), nav3 = magic && !strcmp(magic, "NAV3");
  if (!nav2 && !nav3) qa_binary_error(source, 0, "expected NAV2 or NAV3");
  bool is_nav3 = nav3;
  int64_t version = qa_binary_reader_i32(&r);
  if (!is_nav3 ? (version < 12 || version > 18) : (version < 1 || version > 6))
    qa_binary_error(source, 4, "unsupported %s version %lld", magic, (long long)version);
  free(magic);
  int64_t node_count = count(&r, 20, source), link_count = count(&r, 6, source),
          traversal_count = count(&r, 36, source);
  double heuristic = is_nav3 || version >= 16 ? qa_binary_reader_finite_f32(&r) : 1;
  if (heuristic <= 0) qa_binary_error(source, (int64_t)qa_binary_reader_offset(&r) - 4, "navigation cost multiplier must be positive");
  qa_kex_nav_node *nodes = calloc((size_t)(node_count ? node_count : 1), sizeof *nodes);
  qa_kex_nav_link *links = calloc((size_t)(link_count ? link_count : 1), sizeof *links);
  qa_traversal_hint *traversals = calloc((size_t)(traversal_count ? traversal_count : 1), sizeof *traversals);
  if (!nodes || !links || !traversals) { free(nodes); free(links); free(traversals); qa_binary_reader_destroy(&r); return false; }
  for (int64_t i = 0; i < node_count; i++) {
    int64_t flags = qa_binary_reader_u16(&r), links_n = qa_binary_reader_u16(&r),
            first_link = qa_binary_reader_u16(&r), radius = qa_binary_reader_u16(&r);
    if (first_link + links_n > link_count)
      qa_binary_error(source, (int64_t)qa_binary_reader_offset(&r) - 8, "node link range exceeds table");
    nodes[i] = (qa_kex_nav_node){flags, first_link, links_n, radius, {0, 0, 0}};
  }
  for (int64_t i = 0; i < node_count; i++) nodes[i].origin = vector(&r);
  for (int64_t i = 0; i < link_count; i++) {
    int64_t target = qa_binary_reader_u16(&r), type = qa_binary_reader_u8(&r),
            stored = qa_binary_reader_u8(&r), traversal = qa_binary_reader_u16(&r);
    if (target >= node_count || (traversal != 0xffff && traversal >= traversal_count))
      qa_binary_error(source, (int64_t)qa_binary_reader_offset(&r) - 6, "navigation link references missing record");
    int64_t flags = is_nav3 && version < 3 ? 3 : is_nav3 && version < 6 ? (stored & ~12) : stored;
    links[i] = (qa_kex_nav_link){target, type, flags, stored, traversal != 0xffff, traversal};
  }
  for (int64_t i = 0; i < traversal_count; i++) {
    traversals[i].funnel = vector(&r); traversals[i].start = vector(&r); traversals[i].end = vector(&r);
    if (is_nav3 && version >= 4) { traversals[i].has_ladder_plane = true; traversals[i].ladder_plane = vector(&r); }
  }
  int64_t entity_count = count(&r, 26, source);
  qa_kex_nav_entity *entities = calloc((size_t)(entity_count ? entity_count : 1), sizeof *entities);
  if (!entities) { free(nodes); free(links); free(traversals); qa_binary_reader_destroy(&r); return false; }
  for (int64_t i = 0; i < entity_count; i++) {
    qa_kex_nav_entity *e = &entities[i];
    e->link = qa_binary_reader_u16(&r);
    if (is_nav3) { e->has_model = true; e->model = version >= 2 ? qa_binary_reader_i32(&r) : 0; }
    e->bounds.min = vector(&r); e->bounds.max = vector(&r);
    if (!is_nav3) {
      size_t words = version <= 12 ? 0 : version <= 14 ? 2 : 1;
      if (words) {
        e->tail = malloc(words * sizeof *e->tail);
        if (!e->tail) { entities[i].tail = NULL; /* OOM cleanup below via destroy path */ }
        e->tail_count = words;
        for (size_t w = 0; w < words; w++) { int32_t v = qa_binary_reader_i32(&r); if (e->tail) e->tail[w] = v; }
      }
    }
    if (e->link >= link_count) qa_binary_error(source, (int64_t)qa_binary_reader_offset(&r), "entity references missing link");
    if (e->bounds.min.x > e->bounds.max.x || e->bounds.min.y > e->bounds.max.y || e->bounds.min.z > e->bounds.max.z)
      qa_binary_error(source, (int64_t)qa_binary_reader_offset(&r), "inverted entity bounds");
  }
  if (qa_binary_reader_remaining(&r) != 0) qa_binary_error(source, (int64_t)qa_binary_reader_offset(&r), "unconsumed navigation bytes");
  qa_binary_reader_destroy(&r);
  out->is_nav3 = is_nav3; out->source = dups(source); out->version = version; out->heuristic = heuristic;
  out->nodes = nodes; out->node_count = (size_t)node_count;
  out->links = links; out->link_count = (size_t)link_count;
  out->traversals = traversals; out->traversal_count = (size_t)traversal_count;
  out->entities = entities; out->entity_count = (size_t)entity_count;
  if (!out->source) { qa_kex_navigation_asset_destroy(out); return false; }
  return true;
}
