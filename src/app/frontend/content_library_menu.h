#ifndef QA_FRONTEND_CONTENT_LIBRARY_MENU_H
#define QA_FRONTEND_CONTENT_LIBRARY_MENU_H
#include "internal.h"

typedef enum frontend_library_kind {
    FRONTEND_LIBRARY_ADDONS, FRONTEND_LIBRARY_DEMOS, FRONTEND_LIBRARY_MOVIES,
    FRONTEND_LIBRARY_CONFIGURATIONS, FRONTEND_LIBRARY_SERVER_PROFILES,
    FRONTEND_LIBRARY_PLAYER_PROGRESS, FRONTEND_LIBRARY_COUNT
} frontend_library_kind;
/* Rows, status and scope are borrowed from the actual service owner. Row
 * content/lifetime changes advance revision. Disabled rows carry their
 * unavailable reason in detail. Contexts outlive the menu. */
typedef struct frontend_library_service {
    void *context;
    const char *(*scope)(void *);
    bool (*entries)(void *, const qa_ui_row **, size_t *, uint64_t *, qa_error *);
    const char *(*status)(void *);
    bool (*refresh)(void *, qa_error *);
    bool (*activate)(void *, const char *, qa_error *);
    const char *create_label;
    bool (*create)(void *, const char *, qa_error *);
    const char *stop_label;
    bool (*stop)(void *, qa_error *);
} frontend_library_service;
typedef struct frontend_content_library_menu frontend_content_library_menu;
typedef struct frontend_config_files frontend_config_files;
/* One actual seat/primary-source route shared by library backends. scripts is
 * absent before a source exists; files then belongs to the admitted input store. */
bool frontend_content_library_source_read(frontend_seat *, frontend_config_files **,
    qa_command_context *, qa_vfs **, qa_cvars **, qa_console **, qa_error *);
bool frontend_content_library_menu_create(frontend_seat *,
    const frontend_library_service[FRONTEND_LIBRARY_COUNT], frontend_content_library_menu **, qa_error *);
bool frontend_content_library_menu_destroy(frontend_content_library_menu **, qa_error *);
#endif
