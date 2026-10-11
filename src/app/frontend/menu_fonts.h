#ifndef QA_FRONTEND_MENU_FONTS_H
#define QA_FRONTEND_MENU_FONTS_H
#include "internal.h"
bool frontend_menu_font_view(qa_frontend *, const qa_product **, const qa_product **, qa_error *);
bool frontend_source_classic_font(qa_vfs *, qa_scene_resources *, qa_font_library *,
    qa_game_family, const qa_font **, qa_error *);
bool frontend_menu_charset(qa_frontend *, const qa_product *, qa_error *);
bool frontend_menu_typography(qa_frontend *, const qa_product *, qa_error *);
bool frontend_menu_font_selection(qa_frontend *, uint32_t, bool, qa_font_selection *, qa_error *);
bool frontend_console_font_selection(qa_frontend *, uint32_t, qa_font_selection *, qa_error *);
#endif
