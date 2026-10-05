#ifndef QA_FRONTEND_CONTENT_LIBRARY_SERVICES_H
#define QA_FRONTEND_CONTENT_LIBRARY_SERVICES_H
#include "content_library_menu.h"
#include "demo_service.h"

typedef struct frontend_content_library_services frontend_content_library_services;
typedef struct frontend_content_library_selection {
    void *context;
    bool (*play_addon)(void *, const char *product, const char *start, qa_error *);
    bool (*select_server_profile)(void *, const char *relative, qa_error *);
} frontend_content_library_selection;
bool frontend_content_library_services_create(frontend_seat *,
    frontend_content_library_services **, qa_error *);
bool frontend_content_library_services_read(frontend_content_library_services *,
    frontend_library_service[FRONTEND_LIBRARY_COUNT], qa_error *);
bool frontend_content_library_services_bind_selection(frontend_content_library_services *,
    const frontend_content_library_selection *, qa_error *);
bool frontend_content_library_services_bind_demo(frontend_content_library_services *,
    frontend_demo_service *, qa_error *);
bool frontend_content_library_demo_read(qa_frontend *, const qa_command_context *,
    const char *, qa_buffer *, bool *found, qa_error *);
bool frontend_content_library_demo_root(qa_frontend *, const qa_command_context *,
    qa_fs_root **borrowed, qa_error *);
/* Called after the shared HTTP pump, outside entered transport callbacks. */
bool frontend_content_library_services_pump(frontend_content_library_services *, qa_error *);
bool frontend_content_library_services_idle(const frontend_content_library_services *);
bool frontend_content_library_services_destroy(frontend_content_library_services **, qa_error *);
#endif
