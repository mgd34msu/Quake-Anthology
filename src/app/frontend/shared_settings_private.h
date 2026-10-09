#ifndef QA_FRONTEND_SHARED_SETTINGS_PRIVATE_H
#define QA_FRONTEND_SHARED_SETTINGS_PRIVATE_H
#include "shared_settings.h"

typedef struct frontend_shared_publication frontend_shared_publication;
struct frontend_shared_settings {
    qa_frontend *frontend;
    frontend_config_store *manager;
    qa_application *application;
    const qa_launch_snapshot *candidate;
    qa_application_client_preparation *client;
    qa_console *root_console;
    frontend_shared_values *values;
    frontend_input_settings *input;
    frontend_shared_publication *publication;
    qa_input_platform_settings projected;
    bool projected_window;
    bool before_complete,after_started,after_complete,aborting,scalar_aborted,consumed;
    bool release_deferred_warned;
};
#endif
