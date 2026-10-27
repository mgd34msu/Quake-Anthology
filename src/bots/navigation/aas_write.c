/* See header. GPL-2.0-or-later. */
#include "qa/bots/navigation/aas_write.h"
#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
static void vw(qa_binary_writer *w, qa_vec3 v) { qa_binary_writer_f32(w, v.x); qa_binary_writer_f32(w, v.y); qa_binary_writer_f32(w, v.z); }
uint8_t *qa_write_aas(const qa_aas_asset *world, size_t *out_length) {
  size_t total = 124 + world->bbox_count*32 + world->vertex_count*12 + world->plane_count*20 + world->edge_count*8 + world->edge_index_count*4 + world->face_count*24 + world->face_index_count*4 + world->area_count*48 + world->setting_count*28 + world->reachability_count*44 + world->node_count*12 + world->portal_count*20 + world->portal_index_count*4 + world->cluster_count*16;
  qa_binary_writer w; if (!qa_binary_writer_init(&w, (int64_t)total)) return NULL;
  /* Header placeholders rewritten after lumps: simpler single pass with known offsets. */
  int64_t offsets[14], off = 124;
  size_t sizes[14] = {world->bbox_count*32, world->vertex_count*12, world->plane_count*20, world->edge_count*8, world->edge_index_count*4, world->face_count*24, world->face_index_count*4, world->area_count*48, world->setting_count*28, world->reachability_count*44, world->node_count*12, world->portal_count*20, world->portal_index_count*4, world->cluster_count*16};
  for (int i = 0; i < 14; i++) { offsets[i] = off; off += (int64_t)sizes[i]; }
  qa_binary_writer_i32(&w, 0x53414145); qa_binary_writer_i32(&w, 5); qa_binary_writer_i32(&w, world->bsp_checksum);
  for (int i = 0; i < 14; i++) { qa_binary_writer_i32(&w, offsets[i]); qa_binary_writer_i32(&w, (int64_t)sizes[i]); }
  for (size_t i = 0; i < world->bbox_count; i++) { qa_binary_writer_i32(&w, world->bboxes[i].presence); qa_binary_writer_i32(&w, world->bboxes[i].flags); vw(&w, world->bboxes[i].bounds.min); vw(&w, world->bboxes[i].bounds.max); }
  for (size_t i = 0; i < world->vertex_count; i++) vw(&w, world->vertices[i]);
  for (size_t i = 0; i < world->plane_count; i++) { vw(&w, world->planes[i].normal); qa_binary_writer_f32(&w, world->planes[i].distance); qa_binary_writer_i32(&w, world->planes[i].type); }
  for (size_t i = 0; i < world->edge_count; i++) { qa_binary_writer_i32(&w, world->edges[i].vertices[0]); qa_binary_writer_i32(&w, world->edges[i].vertices[1]); }
  for (size_t i = 0; i < world->edge_index_count; i++) qa_binary_writer_i32(&w, world->edge_indexes[i]);
  for (size_t i = 0; i < world->face_count; i++) { const qa_aas_face_rec *f = &world->faces[i]; qa_binary_writer_i32(&w, f->plane); qa_binary_writer_i32(&w, f->flags); qa_binary_writer_i32(&w, f->edge_count); qa_binary_writer_i32(&w, f->first_edge); qa_binary_writer_i32(&w, f->front_area); qa_binary_writer_i32(&w, f->back_area); }
  for (size_t i = 0; i < world->face_index_count; i++) qa_binary_writer_i32(&w, world->face_indexes[i]);
  for (size_t i = 0; i < world->area_count; i++) { const qa_aas_area_rec *a = &world->areas[i]; qa_binary_writer_i32(&w, a->number); qa_binary_writer_i32(&w, a->face_count); qa_binary_writer_i32(&w, a->first_face); vw(&w, a->bounds.min); vw(&w, a->bounds.max); vw(&w, a->center); }
  for (size_t i = 0; i < world->setting_count; i++) { const qa_aas_setting_rec *s = &world->settings[i]; qa_binary_writer_i32(&w, s->contents); qa_binary_writer_i32(&w, s->flags); qa_binary_writer_i32(&w, s->presence); qa_binary_writer_i32(&w, s->cluster); qa_binary_writer_i32(&w, s->cluster_area); qa_binary_writer_i32(&w, s->reach_count); qa_binary_writer_i32(&w, s->first_reach); }
  for (size_t i = 0; i < world->reachability_count; i++) { const qa_aas_reach_rec *x = &world->reachability[i]; qa_binary_writer_i32(&w, x->area); qa_binary_writer_i32(&w, x->face); qa_binary_writer_i32(&w, x->edge); vw(&w, x->start); vw(&w, x->end); qa_binary_writer_i32(&w, x->travel_type); qa_binary_writer_u16(&w, x->travel_time); qa_binary_writer_u16(&w, x->padding); }
  for (size_t i = 0; i < world->node_count; i++) { qa_binary_writer_i32(&w, world->nodes[i].plane); qa_binary_writer_i32(&w, world->nodes[i].children[0]); qa_binary_writer_i32(&w, world->nodes[i].children[1]); }
  for (size_t i = 0; i < world->portal_count; i++) { const qa_aas_portal_rec *p = &world->portals[i]; qa_binary_writer_i32(&w, p->area); qa_binary_writer_i32(&w, p->front_cluster); qa_binary_writer_i32(&w, p->back_cluster); qa_binary_writer_i32(&w, p->cluster_areas[0]); qa_binary_writer_i32(&w, p->cluster_areas[1]); }
  for (size_t i = 0; i < world->portal_index_count; i++) qa_binary_writer_i32(&w, world->portal_indexes[i]);
  for (size_t i = 0; i < world->cluster_count; i++) { const qa_aas_cluster_rec *c = &world->clusters[i]; qa_binary_writer_i32(&w, c->area_count); qa_binary_writer_i32(&w, c->reachability_area_count); qa_binary_writer_i32(&w, c->portal_count); qa_binary_writer_i32(&w, c->first_portal); }
  uint8_t *outp = qa_binary_writer_finish(&w, out_length); qa_binary_writer_destroy(&w); if (!outp) return NULL;
  for (size_t i = 8; i < 124 && i < *out_length; i++) outp[i] = (uint8_t)(outp[i] ^ (uint8_t)((i - 8) * 119 & 255));
  qa_aas_asset check; if (!qa_parse_aas(&check, outp, *out_length, world->source, true, world->bsp_checksum)) { free(outp); return NULL; } qa_aas_asset_destroy(&check);
  return outp;
}
