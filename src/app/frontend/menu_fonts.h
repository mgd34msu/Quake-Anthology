#ifndef QA_FRONTEND_MENU_FONTS_H
#define QA_FRONTEND_MENU_FONTS_H
#include "internal.h"
bool frontend_menu_font_view(qa_frontend *, const qa_product **, const qa_product **, qa_error *);
bool frontend_source_classic_font(qa_vfs *, qa_scene_resources *, qa_font_library *,
    qa_game_family, const qa_font **, qa_error *);
bool frontend_world_text_draw(qa_frontend *, const qa_scene_view *, const qa_entity_text *,
    size_t, const qa_strings *, const qa_font *const *, uint64_t through, qa_error *);
bool frontend_world_text_local(qa_frontend *, const qa_scene_view *, qa_error *);
bool frontend_menu_charset(qa_frontend *, const qa_product *, qa_error *);
bool frontend_menu_typography(qa_frontend *, const qa_product *, qa_error *);
bool frontend_menu_font_selection(qa_frontend *, uint32_t, bool, qa_font_selection *, qa_error *);
bool frontend_console_font_selection(qa_frontend *, uint32_t, qa_font_selection *, qa_error *);
bool frontend_q2_hud_font_options(qa_frontend *, uint32_t, bool, qa_hud_q2_options *, qa_error *);
#endif
