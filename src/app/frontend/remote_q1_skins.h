#ifndef QA_FRONTEND_REMOTE_Q1_SKINS_H
#define QA_FRONTEND_REMOTE_Q1_SKINS_H
#include "remote_q1_restore.h"

typedef struct frontend_remote_q1_skins frontend_remote_q1_skins;
typedef struct frontend_remote_q1_skin {
    const char *name;
    uint32_t width, height;
    qa_bytes indices;
    const qa_resource *resource;
    const qa_vfs_acquisition *opening;
} frontend_remote_q1_skin;
/* Reads use the actual selected game view, including its base search path.
 * Downloads write through the separately declared shared QW base root. */
typedef struct frontend_remote_q1_skin_bindings {
    qa_vfs *files;
    qa_fs_root *root;
    size_t maximum_bytes;
    void *context;
    bool (*current)(void *, const frontend_remote_q1_domain *, qa_error *);
    bool (*permission)(void *, bool *allowed, bool *demo_recording, bool *demo_playback, qa_error *);
    bool (*nonce)(void *, uint64_t *, qa_error *);
    bool (*reliable)(void *, const char *, qa_error *);
    bool (*print)(void *, const char *, qa_error *);
} frontend_remote_q1_skin_bindings;
bool frontend_remote_q1_skins_create(frontend_remote_q1 *,
    const frontend_remote_q1_skin_bindings *, frontend_remote_q1_skins **, qa_error *);
bool frontend_remote_q1_skins_content(frontend_remote_q1_skins *, qa_vfs *actual_selected,
    qa_fs_root *actual_base_write_root, qa_error *);
bool frontend_remote_q1_skins_refresh(frontend_remote_q1_skins *, bool *ready, qa_error *);
bool frontend_remote_q1_skins_prepare(frontend_remote_q1_skins *, qa_error *);
bool frontend_remote_q1_skins_receive(frontend_remote_q1_skins *, const qa_qw_service *,
    bool *completed, qa_error *);
bool frontend_remote_q1_skins_all(frontend_remote_q1_skins *, const char *, bool *ready, qa_error *);
bool frontend_remote_q1_skins_cancel(frontend_remote_q1_skins *, qa_error *);
bool frontend_remote_q1_skins_retry(frontend_remote_q1_skins *, bool *ready, qa_error *);
bool frontend_remote_q1_skins_reset(frontend_remote_q1_skins *, qa_error *);
bool frontend_remote_q1_skins_idle(const frontend_remote_q1_skins *);
bool frontend_remote_q1_skins_destroy(frontend_remote_q1_skins **, qa_error *);
bool frontend_remote_q1_skins_at(const frontend_remote_q1_skins *, uint32_t physical_slot,
    frontend_remote_q1_skin *, bool *present, qa_error *);
qa_vfs *frontend_remote_q1_skins_files(const frontend_remote_q1_skins *);
bool frontend_remote_q1_skins_checkpoint(const frontend_remote_q1_skins *,
    const frontend_remote_q1_restore_refs *, qa_buffer *, qa_error *);
bool frontend_remote_q1_skins_restore(frontend_remote_q1 *,
    const frontend_remote_q1_skin_bindings *, const frontend_remote_q1_restore_refs *, qa_bytes,
    frontend_remote_q1_skins **, qa_error *);
/* Reattaches an existing partial native stage, validating its exact retained
 * bytes. It never creates/truncates a stage or repeats a download request. */
bool frontend_remote_q1_skins_resume(frontend_remote_q1_skins *, qa_error *);
#endif
