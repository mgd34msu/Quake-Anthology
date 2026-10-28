/* Port of src/app/bootstrap/network/unified-component-publication.ts.
 * Projected update/frame arrays are owned by the result; sources
 * are borrowed for the call and retained by value (shallow) as the
 * publisher's previous set, matching the TS reference retention.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_COMPONENT_PUBLICATION_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_COMPONENT_PUBLICATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/unified_components.h"
#include "qa/app/bootstrap/network/unified_native_components.h"
typedef struct qa_app_unified_component_publisher qa_app_unified_component_publisher;
typedef struct qa_app_unified_component_projection {
  bool has_update; qa_app_unified_component_update update;
  qa_app_unified_component_frames frame;
} qa_app_unified_component_projection;
void qa_app_unified_component_projection_free(qa_app_unified_component_projection *projection);
qa_app_unified_component_publisher *qa_app_unified_component_publisher_create(void);
void qa_app_unified_component_publisher_destroy(qa_app_unified_component_publisher *publisher);
qa_app_unified_component_projection qa_app_unified_component_publisher_project(
    qa_app_unified_component_publisher *publisher,
    const qa_app_unified_component_publication *sources, size_t source_count,
    const qa_app_unified_native_publication *native, size_t native_count);
#endif
