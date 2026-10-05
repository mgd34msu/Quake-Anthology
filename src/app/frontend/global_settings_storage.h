#ifndef QA_FRONTEND_GLOBAL_SETTINGS_STORAGE_H
#define QA_FRONTEND_GLOBAL_SETTINGS_STORAGE_H
#include "qa/settings.h"

typedef struct frontend_global_settings_storage frontend_global_settings_storage;

/* The configured user directory and its console child exist independently of
 * a GAME or CLIENT. Product audio, view and seat stores remain separate. */
bool frontend_global_settings_storage_create(const char *user_root,
    frontend_global_settings_storage **,qa_error *);
bool frontend_global_settings_storage_idle(const frontend_global_settings_storage *);
qa_settings_store frontend_global_settings_storage_user_store(const frontend_global_settings_storage *);
qa_settings_store frontend_global_settings_storage_device_store(const frontend_global_settings_storage *);
bool frontend_global_settings_storage_destroy(frontend_global_settings_storage **,qa_error *);
#endif
