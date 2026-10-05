#ifndef QA_FRONTEND_SETTINGS_MENU_H
#define QA_FRONTEND_SETTINGS_MENU_H
#include "internal.h"
bool frontend_settings_create(frontend_seat *, qa_error *);
void frontend_settings_destroy(frontend_seat *);
bool frontend_settings_accessibility_menu(void *, uint32_t, qa_ui_menu *, qa_error *);
bool frontend_settings_match_menu(void *, uint32_t, qa_ui_menu *, qa_error *);
#endif
