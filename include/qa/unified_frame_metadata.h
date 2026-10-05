#ifndef QA_UNIFIED_FRAME_METADATA_H
#define QA_UNIFIED_FRAME_METADATA_H

#include "qa/network_unified_frame.h"
#include "qa/network_q3.h"

typedef struct qa_unified_style_pattern {
    qa_game_family family;
    uint32_t index;
    char *pattern;
} qa_unified_style_pattern;
typedef struct qa_unified_q3_configuration {
    char *provider_name, *instance, *content;
    uint64_t publication, map_revision, configuration_revision;
    qa_q3_gamestate *game_state;
    uint64_t config_revisions[QA_Q3_CONFIGSTRINGS];
} qa_unified_q3_configuration;
typedef struct qa_unified_frame_metadata {
    uint32_t epoch;
    uint64_t frame;
    uint64_t configuration_revision, roster_revision, style_revision;
    bool replace_configurations, replace_styles, replace_q3;
    qa_unified_configuration_state *configurations;
    size_t configuration_count;
    qa_unified_style_pattern *styles;
    size_t style_count;
    qa_unified_q3_configuration *q3_configurations;
    size_t q3_configuration_count;
} qa_unified_frame_metadata;

void qa_unified_frame_metadata_destroy(qa_unified_frame_metadata *);
bool qa_unified_document_create_metadata(qa_unified_frame_metadata **owned,
    qa_unified_document **out, qa_error *);
const qa_unified_frame_metadata *qa_unified_document_metadata(const qa_unified_document *);
/* Merge real revision-keyed replacement domains into one retained complete
 * record. An unchanged domain never replaces an empty domain implicitly. */
bool qa_unified_metadata_apply(const qa_unified_document *previous,
    const qa_unified_document *update, qa_unified_document **out, qa_error *);

#endif
