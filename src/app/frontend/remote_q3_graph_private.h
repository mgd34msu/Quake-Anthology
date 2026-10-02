#ifndef QA_FRONTEND_REMOTE_Q3_GRAPH_PRIVATE_H
#define QA_FRONTEND_REMOTE_Q3_GRAPH_PRIVATE_H

#include "remote_q3_graph.h"
#include "save_private.h"
#include "network_restore.h"
#include "network_restore_attempt.h"

struct frontend_remote_q3_graph_recipe {
    qa_frontend *frontend;
    qa_application *application;
    qa_application_content_graph *graph;
    frontend_remote_q3_graph_resources resources;
    uint64_t mounts, catalog, descriptor_view, provider_view, roles;
    uint64_t configuration_generation, epoch, restart_generation;
    qa_actor_owner receiver;
    qa_string_id service_owner;
    uint32_t logical_seat, product;
    char *instance;
    qa_sha256_digest descriptor_identity;
    qa_buffer portals;
    bool reading, attempted;
};

bool frontend_remote_q3_graph_geometry_validate(qa_bytes,qa_error *);

#endif
