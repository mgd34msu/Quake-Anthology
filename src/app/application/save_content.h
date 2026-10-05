#ifndef QA_APPLICATION_SAVE_CONTENT_H
#define QA_APPLICATION_SAVE_CONTENT_H

#include "qa/persistence_content.h"
#include "qa/launch_save.h"

typedef struct application_saved_instance_content {
    qa_launch_restored_instance source;
    qa_catalog *product_catalog;
    const qa_product *product;
    uint64_t view;
} application_saved_instance_content;

bool application_save_content_collect(qa_application *,
    qa_application_content_visit_fn, void *, qa_application_content_graph **,
    qa_error *);
/* Stores only identities of used installed content. */
bool application_save_content_encode(const qa_application_content_graph *,
    qa_buffer *, qa_error *);
bool application_save_content_prepare(qa_bytes, const qa_application_options *,
    qa_application_content_graph **, qa_error *);
bool application_save_content_retain_current(qa_application *,
    qa_application_content_visit_fn, void *, qa_application_content_graph **, qa_error *);
void application_save_content_destroy(qa_application_content_graph *);
bool application_save_content_ready(const qa_application_content_graph *, qa_error *);

uint64_t application_save_content_application_pool(const qa_application_content_graph *);
uint64_t application_save_content_application_catalog(const qa_application_content_graph *);
uint64_t application_save_content_launch_catalog(const qa_application_content_graph *);
uint64_t application_save_content_launch_view(const qa_application_content_graph *);
/* source.artifact_acquisition belongs to the current installed content owner. */
bool application_save_content_instance(const qa_application_content_graph *,
    const char *, application_saved_instance_content *, qa_error *);
bool application_save_content_launch_resource(const qa_application_content_graph *,
    qa_product_id, const char *, const qa_resource **, qa_error *);
size_t application_save_content_launch_resource_count(const qa_application_content_graph *);
bool application_save_content_launch_resource_at(const qa_application_content_graph *,
    size_t, qa_launch_resource *, qa_error *);
bool application_save_content_launch_resource_origin(const qa_application_content_graph *,
    size_t, qa_launch_resource_origin *, qa_error *);
/* Take the independent Source content/base/authority views. On failure, out
 * preserves all actually transferred partial custody and clears borrowed views. */
bool application_save_content_launch_source_claim(qa_application_content_graph *,
    size_t, qa_launch_resource_origin *, qa_error *);
/* Each event resource owns one returned current content reference. */
bool application_save_content_event_pool(qa_application_content_graph *, uint64_t,
    qa_resource_pool **, qa_error *);
bool application_save_content_event_view(qa_application_content_graph *, uint64_t,
    qa_vfs **, qa_error *);

#endif
