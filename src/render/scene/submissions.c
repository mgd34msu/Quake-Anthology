/* Port of src/render/scene/submissions.ts. GPL-2.0-or-later. */
#include "qa/render/scene/submissions.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/render/scene/source_sort.h"
static void *xalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Allocation failed"); return p; }
static void *xg(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) qa_range_error("Allocation failed"); return q; }
static int is_int_num(double v) { return isfinite(v) && v == trunc(v); }
void qa_source_scene_order_init(qa_source_scene_order *o, qa_scene_material_registrations *registrations) {
  memset(o, 0, sizeof *o); o->registrations = registrations; o->identity = o;
}
bool qa_source_scene_order_equals(const qa_source_scene_order *a, const qa_source_scene_order *b) { return a->identity == b->identity; }
double qa_source_scene_order_reserve_entities(qa_source_scene_order *o, double count) {
  if (!is_int_num(count) || count < 0 || o->entity_count + count > 1022)
    qa_range_error("Source view exceeds the refentity draw-sort field");
  double first = o->entity_count; o->entity_count += count; return first;
}
double qa_reserve_source_entity_range(qa_source_scene_order *view, double count) { return qa_source_scene_order_reserve_entities(view, count); }
const char *qa_sequence_phase_name(qa_sequence_phase phase) {
  return phase == QA_SEQUENCE_SKY ? "sky" : phase == QA_SEQUENCE_OPAQUE ? "opaque" : "translucent";
}
void qa_scene_group_destroy(qa_scene_group *group) { if (!group) return; free(group->owned_operation); memset(group, 0, sizeof *group); }
static qa_scene_group make_group(qa_scene_group_order order, const qa_draw_batch *batches, size_t count, int empty_ops) {
  qa_scene_group g; memset(&g, 0, sizeof g); g.order = order;
  if (!empty_ops) {
    g.owned_operation = xalloc(sizeof *g.owned_operation);
    memset(g.owned_operation, 0, sizeof *g.owned_operation);
    g.owned_operation->kind = QA_RENDER_OP_DRAW;
    g.owned_operation->batches = batches; g.owned_operation->batch_count = count;
    g.operations = g.owned_operation; g.operation_count = 1;
  }
  return g;
}
qa_scene_group qa_compiled_draw_group(const qa_compiled_material *material, const qa_draw_batch *batches, size_t count) {
  qa_scene_group_order o; memset(&o, 0, sizeof o); o.kind = QA_GROUP_COMPILED; o.material = material;
  return make_group(o, batches, count, 0);
}
qa_scene_group qa_source_draw_group(qa_registered_scene_material material, qa_source_surface_order source, const qa_draw_batch *batches, size_t count) {
  if (!is_int_num(source.surface) || source.surface < 0) qa_range_error("Source surface index must be nonnegative");
  if (source.entity.is_refentity && (!is_int_num(source.entity.index) || source.entity.index < 0 || source.entity.index >= 1022))
    qa_range_error("Source refentity index exceeds the draw-sort field");
  if (!is_int_num(source.fog) || source.fog < 0 || source.fog > 31) qa_range_error("Source fog index exceeds the draw-sort field");
  if (!is_int_num(source.dlight) || source.dlight < 0 || source.dlight > 3) qa_range_error("Source light flag exceeds the draw-sort field");
  if (!material.registration || material.registration->provider->owner != source.view->registrations)
    qa_range_error("Source material belongs to another scene");
  qa_scene_group_order o; memset(&o, 0, sizeof o); o.kind = QA_GROUP_SOURCE; o.source_material = material; o.source = source;
  return make_group(o, batches, count, count == 0);
}
qa_scene_group qa_sequence_draw_group(qa_sequence_phase phase, const qa_draw_batch *batches, size_t count) {
  qa_scene_group_order o; memset(&o, 0, sizeof o); o.kind = QA_GROUP_SEQUENCE; o.phase = phase;
  return make_group(o, batches, count, 0);
}
const qa_draw_batch **qa_scene_model_batches(const qa_scene_group *groups, size_t count, size_t *out_count) {
  size_t n = 0;
  for (size_t i = 0; i < count; i++) for (size_t k = 0; k < groups[i].operation_count; k++) n += groups[i].operations[k].batch_count;
  const qa_draw_batch **result = xalloc((n ? n : 1) * sizeof *result);
  size_t w = 0;
  for (size_t i = 0; i < count; i++) for (size_t k = 0; k < groups[i].operation_count; k++)
    for (size_t b = 0; b < groups[i].operations[k].batch_count; b++) result[w++] = &groups[i].operations[k].batches[b];
  if (out_count) *out_count = w;
  return result;
}
static double priority(qa_scene_group_order o) {
  if (o.kind != QA_GROUP_SEQUENCE)
    return o.kind == QA_GROUP_COMPILED ? o.material->finished.sort : qa_registered_scene_material_finished(o.source_material)->sort;
  return o.phase == QA_SEQUENCE_SKY ? 2 : o.phase == QA_SEQUENCE_OPAQUE ? 3 : 9;
}
void qa_finished_operations_destroy(qa_finished_operations *o) { if (!o) return; free(o->operations); memset(o, 0, sizeof *o); }
typedef struct pending { const qa_scene_group *group; size_t ordinal; uint32_t sort; } pending;
typedef struct sctx { pending *items; } sctx;
static uint32_t sc_get(void *ctx, size_t i) { return ((sctx *)ctx)->items[i].sort; }
static void sc_set(void *ctx, size_t i, uint32_t s) { ((sctx *)ctx)->items[i].sort = s; }
static void sc_swap(void *ctx, size_t a, size_t b) { pending *items = ((sctx *)ctx)->items; pending t = items[a]; items[a] = items[b]; items[b] = t; }
static int cmp_pending(const void *a, const void *b) {
  const pending *x = a, *y = b;
  double d = priority(x->group->order) - priority(y->group->order);
  if (d != 0) return d < 0 ? -1 : 1;
  return x->ordinal < y->ordinal ? -1 : x->ordinal > y->ordinal ? 1 : 0;
}
bool qa_finish_scene_operations(const qa_scene_operation *input, size_t count, qa_finished_operations *out) {
  memset(out, 0, sizeof *out);
  size_t cap_r = 0;
  pending *pend = NULL; size_t np = 0, cp = 0;
  for (size_t idx = 0; idx <= count; idx++) {
    int flush = idx == count || !input[idx].is_group;
    if (!flush) {
      if (np == cp) { cp = cp ? cp * 2 : 16; pend = xg(pend, cp * sizeof *pend); }
      pend[np] = (pending){ &input[idx].group, np, 0 }; np++;
      continue;
    }
    /* flush */
    pending *generic = xalloc((np ? np : 1) * sizeof *generic); size_t ng = 0;
    pending *source = xalloc((np ? np : 1) * sizeof *source); size_t ns = 0;
    pending *sequence = xalloc((np ? np : 1) * sizeof *sequence); size_t nseq = 0;
    for (size_t i = 0; i < np; i++) {
      if (pend[i].group->order.kind == QA_GROUP_COMPILED) generic[ng++] = pend[i];
      else if (pend[i].group->order.kind == QA_GROUP_SOURCE) source[ns++] = pend[i];
      else sequence[nseq++] = pend[i];
    }
    qsort(generic, ng, sizeof *generic, cmp_pending);
    if (ns) {
      qa_source_scene_order *view = source[0].group->order.source.view;
      size_t snap_n = 0;
      const qa_registered_scene_material *snap = qa_scene_material_registrations_snapshot(view->registrations, true, &snap_n);
      for (size_t i = 0; i < ns; i++) {
        qa_scene_group_order *order = (qa_scene_group_order *)&source[i].group->order;
        if (order->source.view != view) qa_range_error("Different source views share a sortable range");
        long rank = -1;
        for (size_t k = 0; k < snap_n; k++) if (snap[k].registration == order->source_material.registration) rank = (long)k;
        if (rank < 0) qa_range_error("Source material has no published shader rank");
        if (rank >= 16384) qa_range_error("Source shader rank exceeds the draw-sort field");
        double entity = order->source.entity.is_refentity ? order->source.entity.index : 1022;
        source[i].sort = qa_pack_source_draw_sort((double)rank, entity, order->source.fog, order->source.dlight);
      }
      sctx sc = { source };
      qa_source_draw_sort_range range = { &sc, ns, sc_get, sc_set, sc_swap };
      qa_sort_draw_surfs(range);
    }
    pending *compiled = xalloc(((ns + ng) ? ns + ng : 1) * sizeof *compiled); size_t nc = 0;
    size_t si = 0, gi = 0;
    while (si < ns || gi < ng) {
      if (si < ns && (gi >= ng || priority(source[si].group->order) < priority(generic[gi].group->order)
          || (priority(source[si].group->order) == priority(generic[gi].group->order) && source[si].ordinal < generic[gi].ordinal)))
        compiled[nc++] = source[si++];
      else if (gi < ng) compiled[nc++] = generic[gi++];
    }
    size_t mi = 0, qi = 0;
    while (mi < nc || qi < nseq) {
      const pending *take;
      if (mi < nc && (qi >= nseq || priority(compiled[mi].group->order) < priority(sequence[qi].group->order)
          || (priority(compiled[mi].group->order) == priority(sequence[qi].group->order) && compiled[mi].ordinal < sequence[qi].ordinal)))
        take = &compiled[mi++];
      else take = &sequence[qi++];
      for (size_t k = 0; k < take->group->operation_count; k++) {
        if (out->count == cap_r) { cap_r = cap_r ? cap_r * 2 : 16; out->operations = xg(out->operations, cap_r * sizeof *out->operations); }
        out->operations[out->count++] = take->group->operations[k];
      }
    }
    free(generic); free(source); free(sequence); free(compiled);
    np = 0;
    if (idx < count) {
      if (out->count == cap_r) { cap_r = cap_r ? cap_r * 2 : 16; out->operations = xg(out->operations, cap_r * sizeof *out->operations); }
      out->operations[out->count++] = input[idx].operation;
    }
  }
  free(pend);
  return true;
}
/* ---- merge ---- */
static int str_eq(const char *a, const char *b) { return a == b || (a && b && !strcmp(a, b)); }
static int same_state(qa_render_state a, qa_render_state b) {
  return str_eq(a.blend.source, b.blend.source) && str_eq(a.blend.destination, b.blend.destination)
    && str_eq(a.depth_test, b.depth_test) && a.depth_write == b.depth_write && str_eq(a.alpha_test, b.alpha_test)
    && str_eq(a.cull, b.cull) && a.depth_range[0] == b.depth_range[0] && a.depth_range[1] == b.depth_range[1]
    && ((a.polygon_offset == NULL && b.polygon_offset == NULL) || (a.polygon_offset && b.polygon_offset && a.polygon_offset->factor == b.polygon_offset->factor && a.polygon_offset->units == b.polygon_offset->units));
}
static int image_eq(qa_renderer_image a, qa_renderer_image b) {
  /* TS compares RendererImage by identity; C approximates by owner+ordinal. */
  return a.owner.identity == b.owner.identity && a.ordinal == b.ordinal && a.owner.generation == b.owner.generation;
}
static int same_texture(qa_texture_binding a, qa_texture_binding b) {
  if (a.kind != b.kind) return 0;
  if (a.kind == QA_TEXTURE_BINDING_BIND_IMAGE) return image_eq(a.image, b.image);
  if (a.kind == QA_TEXTURE_BINDING_DYNAMIC_IMAGE) return a.source.ctx == b.source.ctx && a.source.resolve == b.source.resolve;
  return 1;
}
static int same_lighting(qa_batch_lighting a, qa_batch_lighting b) {
  if (!str_eq(a.kind, b.kind)) return 0;
  if (str_eq(a.kind, "vertex")) return 1;
  if (str_eq(a.kind, "q2-model-shadow"))
    return a.atlas_value.image.owner.identity == b.atlas_value.image.owner.identity && a.atlas_value.image.ordinal == b.atlas_value.image.ordinal
      && (!a.has_shade_scale ? !b.has_shade_scale : b.has_shade_scale && a.shade_scale == b.shade_scale)
      && a.lights == b.lights && a.light_count == b.light_count;
  if (str_eq(a.kind, "q2-world")) {
    if (!str_eq(a.pass, b.pass)) return 0;
    int atlas_eq = (a.atlas == NULL && b.atlas == NULL) || (a.atlas && b.atlas && a.atlas->image.owner.identity == b.atlas->image.owner.identity && a.atlas->image.ordinal == b.atlas->image.ordinal);
    if (!atlas_eq || a.lights != b.lights) return 0;
    return !str_eq(a.pass, "model") || (str_eq(b.pass, "model") && a.shade_scale == b.shade_scale);
  }
  return 0;
}
static int mergeable(const qa_draw_batch *a, const qa_draw_batch *b) {
  if (!str_eq(a->space ? a->space : "clip", b->space ? b->space : "clip")) return 0;
  if (a->texturing_is_pair != b->texturing_is_pair || a->primitive_is_lines != b->primitive_is_lines || !str_eq(a->texture_effect, b->texture_effect)) return 0;
  if (a->primitive_is_lines && a->line_width != b->line_width) return 0;
  if (!same_texture(a->texture, b->texture) || !same_state(a->state, b->state) || !same_lighting(a->lighting, b->lighting)) return 0;
  if (a->texturing_is_pair && (!str_eq(a->second_texture.environment, b->second_texture.environment) || !same_texture(a->second_texture.binding, b->second_texture.binding))) return 0;
  if ((a->fog == NULL) != (b->fog == NULL)) return 0;
  if (a->fog && b->fog) {
    if (!str_eq(a->fog->kind, b->fog->kind) || a->fog->color.x != b->fog->color.x || a->fog->color.y != b->fog->color.y || a->fog->color.z != b->fog->color.z) return 0;
    if (str_eq(a->fog->kind, "exp2") && (a->fog->density != b->fog->density || !str_eq(a->fog->effect, b->fog->effect))) return 0;
    if (str_eq(a->fog->kind, "constant") && a->fog->amount != b->fog->amount) return 0;
  }
  return 1;
}
typedef struct builder {
  qa_draw_batch tmpl;
  qa_render_vertex *sv; size_t nsv, csv;
  qa_multitexture_vertex *pv; size_t npv, cpv;
  double *idx; size_t ni, ci;
  qa_vec3 *wp; size_t nwp, cwp; qa_vec3 *normals; size_t nn, cn;
} builder;
typedef struct builders { builder **items; size_t count, cap; } builders;
static void builder_start(builders *bs, builder **out, qa_draw_batch tmpl) {
  builder *b = xalloc(sizeof *b); memset(b, 0, sizeof *b); b->tmpl = tmpl;
  if (!tmpl.texturing_is_pair) {
    b->sv = xalloc((tmpl.vertex_count ? tmpl.vertex_count : 1) * sizeof *b->sv); b->nsv = b->csv = tmpl.vertex_count;
    memcpy(b->sv, tmpl.vertices, tmpl.vertex_count * sizeof *b->sv);
  } else {
    b->pv = xalloc((tmpl.pair_vertex_count ? tmpl.pair_vertex_count : 1) * sizeof *b->pv); b->npv = b->cpv = tmpl.pair_vertex_count;
    memcpy(b->pv, tmpl.pair_vertices, tmpl.pair_vertex_count * sizeof *b->pv);
  }
  b->idx = xalloc((tmpl.indices.count ? tmpl.indices.count : 1) * sizeof *b->idx); b->ni = b->ci = tmpl.indices.count;
  memcpy(b->idx, tmpl.indices.values, tmpl.indices.count * sizeof *b->idx);
  if (str_eq(tmpl.lighting.kind, "q2-world") || str_eq(tmpl.lighting.kind, "q2-model-shadow")) {
    b->wp = xalloc((tmpl.lighting.world_position_count ? tmpl.lighting.world_position_count : 1) * sizeof *b->wp);
    b->nwp = b->cwp = tmpl.lighting.world_position_count;
    memcpy(b->wp, tmpl.lighting.world_positions, b->nwp * sizeof *b->wp);
    if (str_eq(tmpl.lighting.kind, "q2-world")) {
      b->normals = xalloc((tmpl.lighting.normal_count ? tmpl.lighting.normal_count : 1) * sizeof *b->normals);
      b->nn = b->cn = tmpl.lighting.normal_count;
      memcpy(b->normals, tmpl.lighting.normals, b->nn * sizeof *b->normals);
    }
  }
  if (bs->count == bs->cap) { bs->cap = bs->cap ? bs->cap * 2 : 8; bs->items = xg(bs->items, bs->cap * sizeof *bs->items); }
  bs->items[bs->count++] = b; *out = b;
}
static void builder_extend(builder *b, const qa_draw_batch *batch) {
  size_t offset = b->tmpl.texturing_is_pair ? b->npv : b->nsv;
  if (!b->tmpl.texturing_is_pair) {
    while (b->nsv + batch->vertex_count > b->csv) { b->csv = b->csv ? b->csv * 2 : 16; }
    b->sv = xg(b->sv, b->csv * sizeof *b->sv);
    memcpy(b->sv + b->nsv, batch->vertices, batch->vertex_count * sizeof *b->sv); b->nsv += batch->vertex_count;
  } else {
    while (b->npv + batch->pair_vertex_count > b->cpv) { b->cpv = b->cpv ? b->cpv * 2 : 16; }
    b->pv = xg(b->pv, b->cpv * sizeof *b->pv);
    memcpy(b->pv + b->npv, batch->pair_vertices, batch->pair_vertex_count * sizeof *b->pv); b->npv += batch->pair_vertex_count;
  }
  while (b->ni + batch->indices.count > b->ci) { b->ci = b->ci ? b->ci * 2 : 16; }
  b->idx = xg(b->idx, b->ci * sizeof *b->idx);
  for (size_t i = 0; i < batch->indices.count; i++) b->idx[b->ni++] = batch->indices.values[i] + (double)offset;
  if (str_eq(b->tmpl.lighting.kind, "q2-world") && str_eq(batch->lighting.kind, "q2-world")) {
    while (b->nwp + batch->lighting.world_position_count > b->cwp) { b->cwp = b->cwp ? b->cwp * 2 : 16; }
    b->wp = xg(b->wp, b->cwp * sizeof *b->wp);
    memcpy(b->wp + b->nwp, batch->lighting.world_positions, batch->lighting.world_position_count * sizeof *b->wp);
    b->nwp += batch->lighting.world_position_count;
    while (b->nn + batch->lighting.normal_count > b->cn) { b->cn = b->cn ? b->cn * 2 : 16; }
    b->normals = xg(b->normals, b->cn * sizeof *b->normals);
    memcpy(b->normals + b->nn, batch->lighting.normals, batch->lighting.normal_count * sizeof *b->normals);
    b->nn += batch->lighting.normal_count;
  } else if (str_eq(b->tmpl.lighting.kind, "q2-model-shadow") && str_eq(batch->lighting.kind, "q2-model-shadow")) {
    while (b->nwp + batch->lighting.world_position_count > b->cwp) { b->cwp = b->cwp ? b->cwp * 2 : 16; }
    b->wp = xg(b->wp, b->cwp * sizeof *b->wp);
    memcpy(b->wp + b->nwp, batch->lighting.world_positions, batch->lighting.world_position_count * sizeof *b->wp);
    b->nwp += batch->lighting.world_position_count;
  }
}
static qa_draw_batch builder_finish(builder *b) {
  qa_draw_batch r = b->tmpl;
  if (str_eq(r.lighting.kind, "q2-world")) { r.lighting.world_positions = b->wp; r.lighting.world_position_count = b->nwp; r.lighting.normals = b->normals; r.lighting.normal_count = b->nn; }
  else if (str_eq(r.lighting.kind, "q2-model-shadow")) { r.lighting.world_positions = b->wp; r.lighting.world_position_count = b->nwp; }
  if (!r.texturing_is_pair) { r.vertices = b->sv; r.vertex_count = b->nsv; }
  else { r.pair_vertices = b->pv; r.pair_vertex_count = b->npv; }
  r.indices.values = b->idx; r.indices.count = b->ni;
  return r;
}
void qa_merged_operations_destroy(qa_merged_operations *o) {
  if (!o) return;
  if (o->_builders) {
    builders *bs = o->_builders;
    for (size_t i = 0; i < bs->count; i++) { free(bs->items[i]->sv); free(bs->items[i]->pv); free(bs->items[i]->idx); free(bs->items[i]->wp); free(bs->items[i]->normals); free(bs->items[i]); }
    free(bs->items); free(bs);
  }
  if (o->batches) for (size_t i = 0; i < o->count; i++) free(o->batches[i]);
  free(o->batches); free(o->operations); memset(o, 0, sizeof *o);
}
bool qa_merge_draw_operations(const qa_render_operation *input, size_t count, qa_merged_operations *out) {
  memset(out, 0, sizeof *out);
  builders *bs = xalloc(sizeof *bs); memset(bs, 0, sizeof *bs); out->_builders = bs;
  qa_draw_batch *pending = NULL; size_t np = 0, cp = 0;
  int *pending_owned = NULL;
  builder *current = NULL;
  size_t cap_r = 0;
  for (size_t oi = 0; oi <= count; oi++) {
    int flush_all = oi == count || input[oi].kind != QA_RENDER_OP_DRAW;
    if (!flush_all) {
      for (size_t bi = 0; bi < input[oi].batch_count; bi++) {
        const qa_draw_batch *batch = &input[oi].batches[bi];
        if (current) {
          if (mergeable(&current->tmpl, batch)) { builder_extend(current, batch); continue; }
          if (np == cp) { cp = cp ? cp * 2 : 16; pending = xg(pending, cp * sizeof *pending); pending_owned = xg(pending_owned, cp * sizeof *pending_owned); }
          pending[np] = builder_finish(current); pending_owned[np] = 1; np++; current = NULL;
        }
        if (np && mergeable(&pending[np - 1], batch)) {
          qa_draw_batch last = pending[--np];
          builder_start(bs, &current, last);
          builder_extend(current, batch);
          continue;
        }
        if (np == cp) { cp = cp ? cp * 2 : 16; pending = xg(pending, cp * sizeof *pending); pending_owned = xg(pending_owned, cp * sizeof *pending_owned); }
        pending[np] = *batch; pending_owned[np] = 0; np++;
      }
      continue;
    }
    if (current) {
      if (np == cp) { cp = cp ? cp * 2 : 16; pending = xg(pending, cp * sizeof *pending); pending_owned = xg(pending_owned, cp * sizeof *pending_owned); }
      pending[np] = builder_finish(current); pending_owned[np] = 1; np++; current = NULL;
    }
    if (np) {
      if (out->count == cap_r) { cap_r = cap_r ? cap_r * 2 : 16; out->operations = xg(out->operations, cap_r * sizeof *out->operations); out->batches = xg(out->batches, cap_r * sizeof *out->batches); }
      size_t k = out->count++;
      out->batches[k] = pending;
      memset(&out->operations[k], 0, sizeof out->operations[k]);
      out->operations[k].kind = QA_RENDER_OP_DRAW;
      out->operations[k].batches = pending; out->operations[k].batch_count = np;
      pending = NULL; np = 0; cp = 0; free(pending_owned); pending_owned = NULL;
    }
    if (oi < count) {
      if (out->count == cap_r) { cap_r = cap_r ? cap_r * 2 : 16; out->operations = xg(out->operations, cap_r * sizeof *out->operations); out->batches = xg(out->batches, cap_r * sizeof *out->batches); }
      size_t k = out->count++;
      out->batches[k] = NULL; out->operations[k] = input[oi];
    }
  }
  free(pending); free(pending_owned);
  return true;
}
