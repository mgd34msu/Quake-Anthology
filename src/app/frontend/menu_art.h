#ifndef QA_FRONTEND_MENU_ART_H
#define QA_FRONTEND_MENU_ART_H
#include "internal.h"
bool frontend_menu_art_bind(qa_frontend *, qa_error *);
bool frontend_menu_art_create(qa_frontend *, qa_error *);
void frontend_menu_art_destroy(qa_frontend *);
bool frontend_menu_art_q1_source(frontend_seat *,qa_scene_resources **,qa_error *);
bool frontend_menu_art_q1_help_page(qa_scene_resources *,unsigned,const qa_scene_image **,qa_error *);
#endif
