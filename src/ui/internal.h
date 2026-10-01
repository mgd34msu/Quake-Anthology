#ifndef QA_UI_INTERNAL_H
#define QA_UI_INTERNAL_H
#include "qa/ui.h"
#include "qa/text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct ui_cursor { qa_ui_id menu, control; float scroll; } ui_cursor;
typedef struct ui_field { qa_ui_id menu, control; size_t cursor, top; uint64_t revision; bool overstrike, scrolled; } ui_field;
struct qa_ui {
    qa_ui_options options;
    qa_ui_menu_registration *menus;
    size_t menu_count, menu_capacity;
    ui_cursor *stack;
    size_t depth, stack_capacity;
    ui_field *fields;
    size_t field_count, field_capacity;
    qa_input_ui_token input_token;
    qa_input_pair cursor, pointer;
    float scale, bias_x, bias_y, text_scale;
    qa_ui_color_mode color_mode;
    qa_scene_rect viewport;
    qa_ui_id dragging;
    float drag_offset;
    bool shift, control, capture, handling, drawing, menu_dragging, has_pointer;
    int held_direction[QA_AXIS_COUNT];
    double repeat_at[QA_AXIS_COUNT];
    double time_ms;
    qa_error error;
};
bool ui_fail(qa_error *, const char *);
bool ui_input_time(qa_ui *,double fallback,double *,qa_error *);
bool ui_reserve(void **, size_t *, size_t, size_t, qa_error *);
qa_ui_menu_registration *ui_registration(qa_ui *, qa_ui_id);
bool ui_active(qa_ui *, qa_ui_menu *, qa_error *);
qa_ui_control ui_control(qa_ui *, const qa_ui_menu *, size_t);
ui_field *ui_field_get(qa_ui *, qa_ui_id, qa_error *);
size_t ui_list_page(const qa_ui_control *);
ui_field *ui_list_state(qa_ui *, const qa_ui_control *, qa_error *);
void ui_capture_cancel(qa_ui *);
bool ui_action(qa_ui *, const qa_ui_control *, const qa_ui_action *, qa_error *);
bool ui_move(qa_ui *, int, qa_error *);
bool ui_change(qa_ui *, const qa_ui_control *, int, qa_error *);
bool ui_text(qa_ui *, const qa_ui_control *, const char *, qa_error *);
bool ui_activate(qa_ui *, const qa_ui_control *, qa_error *);
bool ui_inside(qa_scene_rect_f, qa_input_pair);
bool ui_fill(qa_ui *, qa_scene_frame *, qa_scene_rect, qa_scene_rect_f, qa_scene_vec4, qa_error *);
bool ui_draw_text(qa_ui *, qa_scene_frame *, qa_scene_rect, float, float, const char *,
                  qa_scene_vec4, float, qa_font_alignment, qa_error *);
bool ui_draw_source_text(qa_ui *,qa_scene_frame *,qa_scene_rect,float,float,const char *,qa_scene_vec4,
    float,qa_font_alignment,qa_error *);
float ui_glyph_width(qa_ui *, uint32_t);
bool ui_search(const char *, const char *, const char *, qa_bytes, bool *, qa_error *);
#endif
