#ifndef QA_FRONTEND_UNIFIED_GRAPH_SAVE_H
#define QA_FRONTEND_UNIFIED_GRAPH_SAVE_H
#include "remote_unified_presentation_save.h"
#include "component_scene_save.h"
#include "qa/console_save.h"
typedef struct frontend_unified_graph frontend_unified_graph;
typedef struct frontend_unified_graph_refs {
    qa_application_content_graph *content;
    frontend_scene_namespace *scene;
    frontend_model_inventory *models;
    frontend_world_inventory *roots;
    qa_audio_asset_inventory *assets;
    const qa_audio_checkpoint_refs *audio;
    const frontend_component_scene_restore_set *components;
} frontend_unified_graph_refs;
bool frontend_unified_graph_capture_numbers(qa_frontend *,frontend_scene_namespace *,qa_error *);
bool frontend_unified_graph_audio_scope(qa_frontend *,uint64_t,uint32_t *,uint64_t *,uint64_t *);
bool frontend_unified_graph_audio_resolve(qa_frontend *,uint32_t,uint64_t,uint64_t,uint64_t *);
bool frontend_unified_graph_checkpoint(qa_frontend *,const frontend_unified_graph_refs *,qa_buffer *,qa_error *);
/* This envelope retains the physical service separately from QANF's sole
 * replica prefix. Its complete primitive decode precedes service staging. */
bool frontend_unified_graph_decode(qa_frontend *,qa_bytes,frontend_unified_graph **,qa_error *);
bool frontend_unified_graph_stage(frontend_unified_graph *,qa_application_content_graph *,const qa_console_save_resolvers *,qa_error *);
bool frontend_unified_graph_prepare(frontend_unified_graph *,const frontend_unified_graph_refs *,qa_error *);
bool frontend_unified_graph_prepare_components(frontend_unified_graph *,const frontend_unified_graph_refs *,qa_error *);
bool frontend_unified_graph_roots(frontend_unified_graph *,const frontend_unified_graph_refs *,qa_error *);
bool frontend_unified_graph_audio_prefix(frontend_unified_graph *,const frontend_unified_graph_refs *,qa_error *);
bool frontend_unified_graph_finish(frontend_unified_graph *,const frontend_unified_graph_refs *,qa_error *);
void frontend_unified_graph_destroy(frontend_unified_graph *);
#endif
