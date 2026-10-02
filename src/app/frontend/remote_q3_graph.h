#ifndef QA_FRONTEND_REMOTE_Q3_GRAPH_H
#define QA_FRONTEND_REMOTE_Q3_GRAPH_H

#include "qa/collision.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"

typedef struct frontend_remote_q3_graph_recipe frontend_remote_q3_graph_recipe;
typedef struct frontend_remote_q3_graph_resources {
    bool decoded;
    uint64_t identity;
    uint32_t physical_seat;
    qa_vfs *mounts;
    const qa_resource *map;
    qa_bytes portals;
} frontend_remote_q3_graph_resources;

/* Resource topology is independent of the later private child envelope. These
 * calls record genuine constructor parents, including InitialUI's absent map.
 * The actual application content graph holds all returned resource aliases. */
bool frontend_remote_q3_graph_resources_checkpoint(const frontend_remote_q3_resources *,
    qa_buffer *,qa_error *);
bool frontend_remote_q3_graph_initial_checkpoint(const frontend_remote_q3_initial_view *,
    qa_buffer *,qa_error *);
/* Complete decoding precedes any view transfer or detached constructor. The
 * saved descriptor refers to the actual QNRS metadata restored by the source
 * owner; its catalogue, content, digest, roles and physical tuple must match.
 * Input bytes and the actual isolated frontend/content graph outlive the recipe.
 * Read and ready observations close once either prepare attempts to claim the
 * saved view, including failure before a partial parent retains the claim. */
bool frontend_remote_q3_graph_recipe_decode(qa_frontend *,qa_bytes,
    frontend_remote_q3_graph_recipe **,qa_error *);
bool frontend_remote_q3_graph_recipe_read(const frontend_remote_q3_graph_recipe *,
    frontend_remote_q3_graph_resources *,qa_error *);
bool frontend_remote_q3_graph_prepare_resources(frontend_remote_q3_graph_recipe *,
    const frontend_network_client_domain *,frontend_remote_q3 **,qa_error *);
/* InitialUI's genuine candidate attempt can qualify the same decoded recipe
 * without pretending a decoded gamestate or CGAME Init exists. */
bool frontend_remote_q3_graph_initial_ready(const frontend_remote_q3_graph_recipe *,
    const frontend_network_client_attempt *,qa_error *);
/* Claims the saved private view once. The actual empty InitialUI constructor
 * retains any admitted partial output for checked modules-first cleanup. */
bool frontend_remote_q3_graph_prepare_initial(frontend_remote_q3_graph_recipe *,
    const frontend_network_client_attempt *,frontend_remote_q3_initial **,qa_error *);
void frontend_remote_q3_graph_recipe_destroy(frontend_remote_q3_graph_recipe *);

/* The graph's actual resource parent owns the immutable BSP and collision
 * geometry throughout these calls. This capsule retains only mutable portal
 * continuation; it creates no media, source, transport or Init receipt. */
bool frontend_remote_q3_graph_geometry_checkpoint(const qa_collision_geometry *,
    qa_buffer *,qa_error *);
/* Decode the whole capsule before importing into the genuine detached Q3
 * geometry constructed from the held map resource. The lower owner validates
 * its exact format, map fingerprint, areas and reference-count matrix. */
bool frontend_remote_q3_graph_geometry_restore(qa_collision_geometry *,qa_bytes,qa_error *);

#endif
