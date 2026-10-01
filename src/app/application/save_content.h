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

bool application_save_content_collect(const qa_application *,
    qa_application_content_visit_fn, void *, qa_application_content_graph **,
    qa_error *);
bool application_save_content_encode(const qa_application_content_graph *,
    qa_buffer *, qa_error *);
bool application_save_content_prepare(qa_bytes, const qa_vfs_checkpoint_refs *,
    qa_application_content_graph **, qa_error *);
void application_save_content_destroy(qa_application_content_graph *);
bool application_save_content_ready(const qa_application_content_graph *, qa_error *);
/* Called only by the already-qualified allocation-free publication. Native
 * admission normalization ends so later saves capture current view state. */
void application_save_content_publish(qa_application_content_graph *);

uint64_t application_save_content_application_pool(const qa_application_content_graph *);
uint64_t application_save_content_application_catalog(const qa_application_content_graph *);
uint64_t application_save_content_launch_catalog(const qa_application_content_graph *);
uint64_t application_save_content_launch_view(const qa_application_content_graph *);
/* source.artifact_acquisition is the graph-owned recipe captured at the real
 * executable opening and qualified against the restored owning VFS. */
bool application_save_content_instance(const qa_application_content_graph *,
    const char *, application_saved_instance_content *, qa_error *);
bool application_save_content_launch_resource(const qa_application_content_graph *,
    qa_product_id, const char *, const qa_resource **, qa_error *);
size_t application_save_content_launch_resource_count(const qa_application_content_graph *);
bool application_save_content_launch_resource_at(const qa_application_content_graph *,
    size_t, qa_launch_resource *, qa_error *);

#endif
