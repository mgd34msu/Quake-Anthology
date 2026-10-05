#ifndef QA_FRONTEND_SAVE_MENU_H
#define QA_FRONTEND_SAVE_MENU_H
#include "ui_features.h"
typedef struct frontend_save_menu frontend_save_menu;
bool frontend_save_menu_create(frontend_seat *, qa_error *);
bool frontend_save_menu_destroy(frontend_seat *, qa_error *);
bool frontend_save_menu_checkpoint(frontend_seat *, qa_buffer *, qa_error *);
bool frontend_save_menu_restore(frontend_seat *, qa_bytes, qa_error *);
#endif
