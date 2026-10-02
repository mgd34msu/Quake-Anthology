#ifndef QA_FRONTEND_GLOBAL_SETTINGS_STORAGE_H
#define QA_FRONTEND_GLOBAL_SETTINGS_STORAGE_H
#include "qa/settings.h"
#include "qa/persistence_content.h"

typedef struct frontend_global_settings_storage frontend_global_settings_storage;

/* The configured user directory and its console child exist independently of
 * a GAME or CLIENT. Product audio, view and seat stores remain separate. */
bool frontend_global_settings_storage_create(const char *user_root,
    frontend_global_settings_storage **,qa_error *);
bool frontend_global_settings_storage_idle(const frontend_global_settings_storage *);
qa_settings_store frontend_global_settings_storage_user_store(const frontend_global_settings_storage *);
qa_settings_store frontend_global_settings_storage_device_store(const frontend_global_settings_storage *);
bool frontend_global_settings_storage_visit(const frontend_global_settings_storage *,
    const qa_application_content_visitor *,qa_error *);
bool frontend_global_settings_storage_checkpoint(const frontend_global_settings_storage *,
    const qa_application_content_graph *,qa_buffer *,qa_error *);
/* The graph already owns actual mapped directory capabilities. This claims
 * their saved VFS and pool without opening paths or reading preference files. */
bool frontend_global_settings_storage_restore(qa_application_content_graph *,qa_bytes,
    frontend_global_settings_storage **,qa_error *);
bool frontend_global_settings_storage_destroy(frontend_global_settings_storage **,qa_error *);
#endif
