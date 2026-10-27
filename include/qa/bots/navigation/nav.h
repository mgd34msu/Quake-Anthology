/* Port of src/bots/navigation/nav.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_NAV_H
#define QA_BOTS_NAVIGATION_NAV_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/navigation/types.h"
typedef struct qa_kex_nav_node { int64_t flags, first_link, link_count, radius; qa_vec3 origin; } qa_kex_nav_node;
typedef struct qa_kex_nav_link { int64_t target, type, flags, stored_flags; bool has_traversal; int64_t traversal; } qa_kex_nav_link;
typedef struct qa_kex_nav_entity { int64_t link; bool has_model; int64_t model; qa_bounds bounds; double *tail; size_t tail_count; } qa_kex_nav_entity;
typedef struct qa_kex_navigation_asset {
  bool is_nav3; char *source; int64_t version; double heuristic;
  qa_kex_nav_node *nodes; size_t node_count; qa_kex_nav_link *links; size_t link_count;
  qa_traversal_hint *traversals; size_t traversal_count; qa_kex_nav_entity *entities; size_t entity_count;
} qa_kex_navigation_asset;
void qa_kex_navigation_asset_destroy(qa_kex_navigation_asset *a);
bool qa_parse_kex_navigation(qa_kex_navigation_asset *out, uint8_t *bytes, size_t length, const char *source);
#endif
