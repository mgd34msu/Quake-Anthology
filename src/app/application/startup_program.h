#ifndef QA_APPLICATION_STARTUP_PROGRAM_H
#define QA_APPLICATION_STARTUP_PROGRAM_H
#include "startup_flow.h"

typedef struct application_startup_program application_startup_program;
typedef struct application_startup_program_roster application_startup_program_roster;
bool application_startup_program_prepare(qa_application *, const qa_launch_snapshot *,
    const qa_application_startup_source *, const qa_application_startup_source *,
    application_startup_program **, qa_error *);
bool application_startup_program_refresh(application_startup_program *,
    const qa_application_startup_source *, qa_error *);
bool application_startup_program_preflight(application_startup_program *, qa_error *);
bool application_startup_program_seal(application_startup_program *, qa_error *);
bool application_startup_program_adopt(application_startup_program **, qa_error *);
bool application_startup_program_abort(application_startup_program **, qa_error *);
void application_startup_program_bound_client(qa_application *, application_provider *,
    const qa_application_startup_source *);
/* A direct replacement retains its genuine validated publication until all
 * program loans return. A failed preparation may retain the roster in *out. */
bool application_startup_program_publication_prepare(qa_application *, application_publication *,
    application_startup_program_roster **out, qa_error *);
bool application_startup_program_publication_seal(application_startup_program_roster *, qa_error *);
bool application_startup_program_publication_adopt(application_startup_program_roster **, qa_error *);
bool application_startup_program_publication_abort(application_startup_program_roster **, qa_error *);
#endif
