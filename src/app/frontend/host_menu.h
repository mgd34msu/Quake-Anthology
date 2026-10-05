#ifndef QA_FRONTEND_HOST_MENU_H
#define QA_FRONTEND_HOST_MENU_H
#include "internal.h"
#include "qa/ui_library.h"

typedef struct frontend_host_menu frontend_host_menu;
typedef enum frontend_host_kind {
    FRONTEND_HOST_OFFLINE, FRONTEND_HOST_NATIVE, FRONTEND_HOST_UNIFIED
} frontend_host_kind;
typedef struct frontend_host_settings {
    frontend_host_kind kind;
    uint16_t port;
    qa_net_protocol_id q1_protocol;
} frontend_host_settings;

/* The physical seat, its UI and the shared game selection outlive this child.
 * Failed registration retains a non-NULL child until checked destruction. */
bool frontend_host_menu_create(frontend_seat *,qa_ui_library *,qa_ui_id,
    frontend_host_menu **,qa_error *);
bool frontend_host_menu_destroy(frontend_host_menu **,qa_error *);
bool frontend_host_menu_idle(const frontend_host_menu *);
bool frontend_host_menu_open(frontend_host_menu *,qa_error *);
/* Play reads the applied network choices against its actual launch draft. */
bool frontend_host_menu_read(const frontend_host_menu *,const qa_launch_draft *,
    frontend_host_settings *,qa_error *);
const char *frontend_host_menu_label(void *);
/* Private menu choices use the existing Source save owner; import never applies
 * a mode or starts a transport. */
#endif
