#ifndef QA_APPLICATION_MODE_IDENTITY_H
#define QA_APPLICATION_MODE_IDENTITY_H
#include "qa/arena.h"
#include "qa/launch.h"

typedef struct application_mode_identity {
    qa_launch_mode mode;
    qa_arena text;
} application_mode_identity;

bool application_mode_identity_bind(application_mode_identity *, const qa_launch_mode *, qa_error *);
bool application_mode_identity_read(application_mode_identity *, qa_bytes, qa_error *);
void application_mode_identity_free(application_mode_identity *);
#endif
