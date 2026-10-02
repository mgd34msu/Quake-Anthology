#ifndef QA_FRONTEND_REMOTE_Q2_RESTORE_H
#define QA_FRONTEND_REMOTE_Q2_RESTORE_H
#include "remote_q2_client.h"
#include "world_inventory.h"
#include "font_inventory.h"
#include "qa/network_downloads_save.h"

typedef struct frontend_remote_q2_restore_refs {
    qa_application_content_graph *content;
    frontend_model_inventory *models;
    frontend_world_inventory *roots;
    frontend_scene_namespace *scene;
    qa_download_checkpoint_refs downloads;
} frontend_remote_q2_restore_refs;
bool frontend_remote_q2_download_refs(frontend_remote_q2 *, qa_download_checkpoint_refs *, qa_error *);
/* Parent resource dictionaries encode genuine image/material/font/audio/world
 * owners. This leaf preserves their actual cache references and CLIENT state. */
bool frontend_remote_q2_checkpoint(const frontend_remote_q2 *,
    const frontend_remote_q2_restore_refs *, qa_buffer *, qa_error *);
/* Decode/claim actual saved catalog and private VFS before parent dictionaries.
 * Creates empty detached media holders, runs no Init/acquire/load callbacks,
 * and leaves ordinary receiver/current/read/send unavailable until finish. */
bool frontend_remote_q2_restore_prepare(qa_frontend *, const frontend_remote_q2_options *,
    const frontend_remote_q2_restore_refs *, qa_bytes, frontend_remote_q2 **, qa_error *);
bool frontend_remote_q2_import_read(const frontend_remote_q2 *, frontend_remote_q2_view *, qa_error *);
bool frontend_remote_q2_roots_attach_restored(frontend_remote_q2 *,
    const frontend_remote_q2_restore_refs *, qa_error *);
/* Parent supplies imported dictionary rows after genuine lower/session and
 * media owners restore. Acquires real parsed-model tokens and adopts roots. */
bool frontend_remote_q2_restore_finish(frontend_remote_q2 *,
    const frontend_remote_q2_restore_refs *, qa_error *);
#endif
