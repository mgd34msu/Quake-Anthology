/* Port of src/network/q3/visibility.ts (sv_snapshot.c). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_VISIBILITY_H
#define QA_NETWORK_Q3_VISIBILITY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/network/q3/message.h"
#include "qa/network/q3/state/entity.h"
#include "qa/network/q3/state/player.h"
typedef enum qa_q3_server_entity_flags {
  QA_Q3_SVF_NO_CLIENT = 1, QA_Q3_SVF_CLIENT_MASK = 2, QA_Q3_SVF_BOT = 8, QA_Q3_SVF_BROADCAST = 32,
  QA_Q3_SVF_PORTAL = 64, QA_Q3_SVF_USE_CURRENT_ORIGIN = 128, QA_Q3_SVF_SINGLE_CLIENT = 256,
  QA_Q3_SVF_NO_SERVER_INFO = 512, QA_Q3_SVF_NOT_SINGLE_CLIENT = 2048
} qa_q3_server_entity_flags;
/* TS Q3VisibilityEntity. state points at live record (FIXING ENT->S.NUMBER mutates). */
typedef struct qa_q3_visibility_entity {
  qa_q3_entity_state_record *state; bool linked; int64_t flags, single_client;
} qa_q3_visibility_entity;
typedef struct qa_q3_visibility_link {
  int64_t areanum, areanum2; const int64_t *clusters; size_t cluster_count; int64_t last_cluster;
} qa_q3_visibility_link;
typedef int64_t (*qa_q3_vis_point_leaf_fn)(void *ctx, qa_vec3 point);
typedef int64_t (*qa_q3_vis_leaf_fn)(void *ctx, int64_t leaf);
typedef int64_t (*qa_q3_vis_write_area_bits_fn)(void *ctx, uint8_t *bytes, int64_t area);
typedef int64_t (*qa_q3_vis_pvs_byte_fn)(void *ctx, int64_t cluster, int64_t index); /* clusterPVS(cluster).byteAt(index) */
typedef bool (*qa_q3_vis_areas_connected_fn)(void *ctx, int64_t first, int64_t second);
/* TS Q3VisibilityWorld. PVS byteAt folded to (cluster,index) fn. */
typedef struct qa_q3_visibility_world {
  qa_q3_vis_point_leaf_fn point_leafnum;
  qa_q3_vis_leaf_fn leaf_area, leaf_cluster;
  qa_q3_vis_write_area_bits_fn write_area_bits;
  qa_q3_vis_pvs_byte_fn pvs_byte_at;
  qa_q3_vis_areas_connected_fn areas_connected;
  void *ctx;
} qa_q3_visibility_world;
typedef qa_q3_visibility_entity (*qa_q3_vis_entity_fn)(void *ctx, int64_t number);
typedef bool (*qa_q3_vis_link_fn)(void *ctx, int64_t number, qa_q3_visibility_link *out); /* false = undefined */
/* TS Q3VisibilityBindings. */
typedef struct qa_q3_visibility_bindings {
  qa_q3_visibility_world collision;
  int64_t entity_count; bool dead;
  qa_q3_vis_entity_fn entity; qa_q3_vis_link_fn link; void *ctx;
  qa_q3_message_print_fn print; void *print_ctx;
} qa_q3_visibility_bindings;
/* TS Q3VisibleEntities: entities are borrowed state pointers. */
typedef struct qa_q3_visible_entities {
  uint8_t *area_mask; size_t area_mask_len;
  qa_q3_entity_state_record **entities; size_t entity_count;
} qa_q3_visible_entities;
bool qa_q3_select_snapshot_entities(const qa_q3_player_state_record *player, const qa_q3_visibility_bindings *host, qa_q3_visible_entities *out);
void qa_q3_visible_entities_destroy(qa_q3_visible_entities *v);
#endif
