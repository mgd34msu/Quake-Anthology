#ifndef QA_FRONTEND_MENU_ART_H
#define QA_FRONTEND_MENU_ART_H
#include "internal.h"
bool frontend_menu_art_bind(qa_frontend *, qa_error *);
bool frontend_menu_art_create(qa_frontend *, qa_error *);
void frontend_menu_art_destroy(qa_frontend *);
#endif
