/* Port of src/bots/navigation/aas.ts. Owned arrays; destroy frees.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_AAS_H
#define QA_BOTS_NAVIGATION_AAS_H
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_aas_lump {
  int64_t offset, length;
} qa_aas_lump;
typedef struct qa_aas_bbox {
  int64_t presence, flags;
  qa_bounds bounds;
} qa_aas_bbox;
typedef struct qa_aas_plane_rec {
  qa_vec3 normal;
  double distance;
  int64_t type;
} qa_aas_plane_rec;
typedef struct qa_aas_edge_rec {
  int64_t vertices[2];
} qa_aas_edge_rec;
typedef struct qa_aas_face_rec {
  int64_t plane, flags, edge_count, first_edge, front_area, back_area;
} qa_aas_face_rec;
typedef struct qa_aas_area_rec {
  int64_t number, face_count, first_face;
  qa_bounds bounds;
  qa_vec3 center;
} qa_aas_area_rec;
typedef struct qa_aas_setting_rec {
  int64_t contents, flags, presence, cluster, cluster_area, reach_count, first_reach;
} qa_aas_setting_rec;
typedef struct qa_aas_reach_rec {
  int64_t area, face, edge;
  qa_vec3 start, end;
  int64_t travel_type, travel_time, padding;
} qa_aas_reach_rec;
typedef struct qa_aas_node_rec {
  int64_t plane, children[2];
} qa_aas_node_rec;
typedef struct qa_aas_portal_rec {
  int64_t area, front_cluster, back_cluster, cluster_areas[2];
} qa_aas_portal_rec;
typedef struct qa_aas_cluster_rec {
  int64_t area_count, reachability_area_count, portal_count, first_portal;
} qa_aas_cluster_rec;
typedef struct qa_aas_asset {
  char *source;
  int64_t version, bsp_checksum;
  qa_aas_lump *lumps;
  size_t lump_count;
  qa_aas_bbox *bboxes;
  size_t bbox_count;
  qa_vec3 *vertices;
  size_t vertex_count;
  qa_aas_plane_rec *planes;
  size_t plane_count;
  qa_aas_edge_rec *edges;
  size_t edge_count;
  int64_t *edge_indexes;
  size_t edge_index_count;
  qa_aas_face_rec *faces;
  size_t face_count;
  int64_t *face_indexes;
  size_t face_index_count;
  qa_aas_area_rec *areas;
  size_t area_count;
  qa_aas_setting_rec *settings;
  size_t setting_count;
  qa_aas_reach_rec *reachability;
  size_t reachability_count;
  qa_aas_node_rec *nodes;
  size_t node_count;
  qa_aas_portal_rec *portals;
  size_t portal_count;
  int64_t *portal_indexes;
  size_t portal_index_count;
  qa_aas_cluster_rec *clusters;
  size_t cluster_count;
} qa_aas_asset;
void qa_aas_asset_destroy(qa_aas_asset *a);
/* has_expected false ignores expected (TS undefined). Returns false only on OOM; format errors
 * abort via qa_binary_error/qa_range_error. */
bool qa_parse_aas(qa_aas_asset *out, uint8_t *bytes, size_t length, const char *source,
                  bool has_expected, int64_t expected_bsp_checksum);
int64_t qa_aas_point_area(const qa_aas_asset *asset, qa_vec3 point);
typedef struct qa_aas_area_crossing {
  int64_t area;
  qa_vec3 point;
} qa_aas_area_crossing;
/* Caller frees *out. maximum<0 aborts (TS RangeError via non-integer check approximated for int64).
 */
qa_aas_area_crossing *qa_aas_trace_areas(const qa_aas_asset *asset, qa_vec3 start, qa_vec3 end,
                                         int64_t maximum, size_t *out_count);
int64_t *qa_aas_bbox_areas(const qa_aas_asset *asset, qa_bounds bounds, int64_t maximum,
                           size_t *out_count);
/* TS AasTrace. */
typedef struct qa_aas_trace {
  bool start_solid; double fraction; qa_vec3 end;
  int64_t entity_num, last_area, area, plane;
} qa_aas_trace;
/* TS aasTraceClientBBox: AAS_TraceClientBBox, a walk down the area tree that stops at the first leaf
 * not admitting `presence`. It sees no entities, so a mover linked over the floor does not stop it,
 * unlike a BSP trace. print NULL is the TS default (discard). */
qa_aas_trace qa_aas_trace_client_bbox(const qa_aas_asset *asset, qa_vec3 start, qa_vec3 end, int64_t presence,
                                      void (*print)(void *ctx, const char *text), void *print_ctx);
#endif
