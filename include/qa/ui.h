#ifndef QA_UI_H
#define QA_UI_H

#include "qa/application.h"
#include "qa/font.h"
#include "qa/input.h"

typedef struct qa_ui qa_ui;
typedef struct qa_ui_input_binding {
    qa_input_seat *seat;
    qa_input_ui_handler handler;
    void *context;
    qa_input_ui_token token;
} qa_ui_input_binding;
bool qa_ui_input_binding_read(const qa_ui *, qa_ui_input_binding *);
typedef struct qa_ui_mods qa_ui_mods;
typedef struct qa_ui_rankings qa_ui_rankings;
typedef struct qa_ui_library qa_ui_library;
typedef struct qa_ui_llm qa_ui_llm;
typedef struct qa_llm qa_llm;
typedef enum qa_ui_color_mode {
    QA_UI_COLOR_STANDARD, QA_UI_COLOR_BLUE_YELLOW, QA_UI_COLOR_MONOCHROME
} qa_ui_color_mode;
typedef uint64_t qa_ui_id;
typedef enum qa_ui_action_kind {
    QA_UI_ACTIVATE, QA_UI_CHANGE_NUMBER, QA_UI_CHANGE_TEXT, QA_UI_SUBMIT,
    QA_UI_SELECT, QA_UI_ROW_ACTIVATE, QA_UI_ROW_DELETE
} qa_ui_action_kind;
typedef struct qa_ui_action {
    qa_ui_action_kind kind;
    union { double number; const char *text; size_t row; } value;
} qa_ui_action;
typedef bool (*qa_ui_action_fn)(void *, uint32_t seat, qa_ui_id control,
                                const qa_ui_action *, qa_error *);
typedef struct qa_ui_row {
    const char *key, *label, *detail;
    const qa_scene_image *image;
    bool enabled;
    const char *const *cells;
    size_t cell_count;
    const char *action_label;
} qa_ui_row;
typedef enum qa_ui_control_kind {
    QA_UI_BUTTON, QA_UI_TOGGLE, QA_UI_SLIDER, QA_UI_FIELD, QA_UI_CHOICE,
    QA_UI_LIST, QA_UI_OWNER_DRAW, QA_UI_TEXT
} qa_ui_control_kind;
typedef struct qa_ui_text_style {
    /* Zero scale selects the authored startup default 2.2; zero fit_width is unbounded. */
    float scale, fit_width;
    bool accent, heading, source, overlay;
} qa_ui_text_style;
typedef struct qa_ui_control {
    qa_ui_id id;
    qa_ui_control_kind kind;
    const char *label;
    qa_scene_rect_f rect;
    bool enabled, visible, scrolls;
    void *context;
    qa_ui_action_fn action;
    union {
        bool checked;
        qa_ui_text_style text;
        struct { double value, minimum, maximum, step; const char *label; } slider;
        struct { const char *text; size_t maximum; bool masked; } field;
        struct { const char *const *labels; size_t count, selected; } choice;
        struct { const qa_ui_row *rows; size_t count, selected; float row_height; uint64_t revision;
            const float *column_widths; size_t column_count; } list;
        struct {
            bool (*draw)(void *, uint32_t, qa_scene_frame *, qa_scene_rect, qa_scene_rect_f,
                         qa_error *);
            bool (*input)(void *, uint32_t, const qa_input_event *, bool *, qa_error *);
        } owner;
    } value;
} qa_ui_control;
typedef struct qa_ui_menu {
    qa_ui_id id;
    const char *title;
    const qa_ui_control *controls;
    size_t count;
    bool fullscreen, scrollable;
    qa_scene_rect_f scroll_rect;
    float content_height;
    bool source_title;
    /* Authored startup Home panel and heading; other menus use the wide panel. */
    bool narrow;
    bool picture_only;
} qa_ui_menu;
/* A factory returns borrowed spans valid until its next invocation. Factories
 * and open/close hooks do not mutate the controller. Actions may open/close
 * menus, but the controller and callback context survive the callback. */
typedef bool (*qa_ui_menu_factory)(void *, uint32_t, qa_ui_menu *, qa_error *);
typedef struct qa_ui_menu_registration {
    qa_ui_id id;
    void *context;
    qa_ui_menu_factory factory;
    bool (*open)(void *, uint32_t, qa_error *);
    void (*close)(void *, uint32_t);
} qa_ui_menu_registration;
typedef enum qa_ui_sound { QA_UI_OPEN, QA_UI_CLOSE, QA_UI_MOVE, QA_UI_CHANGE, QA_UI_REJECT } qa_ui_sound;
/* Borrowed resources and callback contexts outlive the controller. Callbacks
 * cannot destroy it; menu factories, lifecycle hooks and draw callbacks only
 * inspect it. List revisions change when row membership/order changes. */
typedef struct qa_ui_art {
    const qa_scene_image *background, *main_background, *panel, *focus;
} qa_ui_art;
typedef struct qa_ui_options {
    uint32_t seat;
    qa_input_seat *input;
    qa_font_selection fonts;
    const qa_scene_image *white;
    qa_ui_art art;
    qa_font_selection title_fonts;
    void *context;
    void (*sound)(void *, uint32_t, qa_ui_sound);
    const char *(*localize)(void *, const char *);
    const char *(*clipboard)(void *);
    bool (*binding)(void *, uint32_t, qa_physical_input, qa_error *);
    void (*binding_cancel)(void *, uint32_t);
    /* Optional physical input clock for focus releases and held navigation.
     * Open/close/tick arguments remain the UI presentation clock. The clock
     * callback is a pure read of the retained context and returns finite ms. */
    double (*input_now_ms)(void *);
} qa_ui_options;
typedef struct qa_ui_state {
    uint32_t seat;
    qa_ui_id menu, control;
    qa_vec2 cursor;
    size_t depth;
    bool fullscreen, binding_capture;
} qa_ui_state;
bool qa_ui_create(const qa_ui_options *, qa_ui **, qa_error *);
/* Publish borrowed typography and text metrics together between callbacks.
 * The caller retains fonts and fallback arrays for the controller lifetime. */
bool qa_ui_set_presentation(qa_ui *, const qa_font_selection *, float text_scale,
    qa_ui_color_mode, qa_error *);
typedef struct qa_ui_presentation {
    qa_font_selection fonts;
    float text_scale;
    qa_ui_color_mode color_mode;
} qa_ui_presentation;
/* Pure borrowed view of the current seat presentation, between UI callbacks.
 * Font resources and fallback spans keep their existing owner's lifetime. */
bool qa_ui_presentation_read(const qa_ui *,qa_ui_presentation *,qa_error *);
/* Read-only parent admission for actual input and drawing callbacks. */
bool qa_ui_idle(const qa_ui *);
/* Remove the borrowed input handler before releasing its context. */
bool qa_ui_destroy(qa_ui *, double time_ms, qa_error *);
bool qa_ui_register(qa_ui *, const qa_ui_menu_registration *, qa_error *);
bool qa_ui_unregister(qa_ui *, qa_ui_id, double time_ms, qa_error *);
bool qa_ui_open(qa_ui *, qa_ui_id, double time_ms, qa_error *);
bool qa_ui_close(qa_ui *, double time_ms, qa_error *);
bool qa_ui_close_all(qa_ui *, double time_ms, qa_error *);
bool qa_ui_state_read(qa_ui *, qa_ui_state *, qa_error *);
bool qa_ui_menu_opened(const qa_ui *, qa_ui_id);
bool qa_ui_input(qa_ui *, const qa_input_event *, bool *, qa_error *);
bool qa_ui_tick(qa_ui *, double time_ms, qa_error *);
bool qa_ui_capture_binding(qa_ui *, bool, qa_error *);
/* Viewport is in display pixels; controls use an aspect-preserving 640x480
 * canvas. Drawing uses shared font/scene resources and frame scratch memory. */
bool qa_ui_draw(qa_ui *, qa_scene_frame *, qa_scene_rect viewport, float scale,
                 bool high_contrast, bool startup, qa_error *);
/* Authored 640x480 text, using the active menu transform, palette and typography.
 * Only call from a live UI draw callback; text and style are borrowed for this call.
 * Explicit startup text scales follow menuScale independently of body textScale. */
bool qa_ui_menu_text(qa_ui *, qa_scene_frame *, qa_scene_rect target, qa_vec2 origin,
    const char *, const qa_ui_text_style *, qa_error *);
const qa_error *qa_ui_error(const qa_ui *);

/* Dedicated addition-mod controls stage a complete launch draft. Apply uses
 * application admission; validation errors retain both live state and draft. */
bool qa_ui_mods_create(qa_ui *, qa_application *, qa_ui_id, qa_ui_mods **, qa_error *);
bool qa_ui_mods_destroy(qa_ui_mods *, double time_ms, qa_error *);
bool qa_ui_mods_refresh(qa_ui_mods *, qa_error *);
bool qa_ui_mods_apply(qa_ui_mods *, qa_error *);
bool qa_ui_mods_cancel(qa_ui_mods *, qa_error *);

/* The caller supplies an admitted source slot from application player routing;
 * seat IDs and names are not ranking identities. Operations execute on the
 * application's owner thread, outside its active service callbacks. */
bool qa_ui_rankings_create(qa_ui *, qa_application *, qa_ui_id menu, int32_t source_slot,
                           qa_ui_rankings **, qa_error *);
bool qa_ui_rankings_set_slot(qa_ui_rankings *, int32_t source_slot, qa_error *);
bool qa_ui_rankings_reset_binding(qa_ui_rankings *, qa_error *);
bool qa_ui_rankings_destroy(qa_ui_rankings *, double time_ms, qa_error *);

/* Native product/map/start selection uses catalog ownership and application
 * admission directly, preserving authored start-command syntax. */
bool qa_ui_library_create(qa_ui *, qa_application *, qa_ui_id, const qa_launch_seat *local_players, size_t local_player_count,
                          qa_ui_library **, qa_error *);
bool qa_ui_library_destroy(qa_ui_library *, double time_ms, qa_error *);
bool qa_ui_library_refresh(qa_ui_library *, qa_error *);

/* The shared assistance owner and UI survive action callbacks. This adapter
 * never reads or displays stored credentials; entered API keys are cleared
 * after the private settings owner accepts them. */
bool qa_ui_llm_create(qa_ui *, qa_llm *, qa_ui_id, qa_ui_llm **, qa_error *);
bool qa_ui_llm_destroy(qa_ui_llm *, double time_ms, qa_error *);

#endif
