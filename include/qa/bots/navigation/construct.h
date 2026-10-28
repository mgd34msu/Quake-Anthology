/* Port of src/bots/navigation/construct.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_CONSTRUCT_H
#define QA_BOTS_NAVIGATION_CONSTRUCT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/navigation/types.h"
#include "qa/contracts/scene.h"
typedef struct qa_navigation_connection {
  qa_traversal_request request;
  int64_t id;
  int64_t source_travel_type;
  double travel_seconds;
} qa_navigation_connection;
typedef struct qa_navigation_construction {
  qa_decoded_world geometry;
  qa_navigation_map_identity map;
  qa_navigation_profile profile;
  const qa_navigation_world *world;
  bool has_spacing; double spacing;
  bool has_link_distance; double link_distance;
  bool has_maximum_nodes; int64_t maximum_nodes;
  const qa_navigation_connection *connections; size_t connection_count;
} qa_navigation_construction;
/* False only on OOM; invalid limits/identity abort with TS messages.
 * On success out owns its arrays (qa_navigation_graph_destroy). */
bool qa_construct_navigation(qa_navigation_graph *out, qa_navigation_construction options);
#endif
