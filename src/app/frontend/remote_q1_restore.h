#ifndef QA_FRONTEND_REMOTE_Q1_RESTORE_H
#define QA_FRONTEND_REMOTE_Q1_RESTORE_H
#include "remote_q1_client.h"
#include "world_inventory.h"
#include "qa/audio_bank_graph_save.h"
typedef struct frontend_remote_q1_restore_refs {
    qa_application_content_graph *content;
    frontend_model_inventory *models;
    frontend_world_inventory *roots;
    frontend_scene_namespace *scene;
    const qa_audio_asset_inventory *assets;
    uint64_t owner;
} frontend_remote_q1_restore_refs;
bool frontend_remote_q1_checkpoint(const frontend_remote_q1 *, const frontend_remote_q1_restore_refs *, qa_buffer *, qa_error *);
bool frontend_remote_q1_restore_prepare(qa_frontend *, const frontend_remote_q1_options *,
    const frontend_remote_q1_restore_refs *, qa_bytes, frontend_remote_q1 **, qa_error *);
bool frontend_remote_q1_import_read(const frontend_remote_q1 *, frontend_remote_q1_view *, qa_error *);
bool frontend_remote_q1_roots_attach_restored(frontend_remote_q1 *, const frontend_remote_q1_restore_refs *, qa_error *);
bool frontend_remote_q1_restore_finish(frontend_remote_q1 *, const frontend_remote_q1_restore_refs *, qa_error *);
#endif
