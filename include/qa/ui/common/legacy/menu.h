/*
 * Port of src/ui/common/legacy/menu.ts: Team Arena menu definition
 * parsing (ui_shared.c/ui_main.c/cg_main.c). C accommodations:
 *  - The TS allocation-backed getter/setter views become structs
 *    holding a UiMemoryAllocation handle plus accessor functions;
 *    the byte offsets are the TS QVM32 record offsets. Rect/color
 *    views are read/written by value (qa_rect / qa_vec4).
 *  - UiScript is qa_ui_menu_script (qa_ui_script_* is the landed
 *    script engine); its text getter reads through the retained
 *    UiStringReference each call, as in TS.
 *  - Shader/model/sound/font references share one kind-tagged
 *    qa_ui_resource_reference record (path NULL is TS null);
 *    the TS alias-kind checks read the tag.
 *  - Async registration/menu sinks are synchronous fn+ctx.
 *    UiMenuParseHost is kind-tagged (resolved vs script-sources).
 *  - MenuParseFailure rides a setjmp stack inside the parser;
 *    fail() throws through qa_ui_script_fail_throw (aborts with
 *    the ScriptLanguageError text form when uncaught).
 *  - Each load's definitions own everything that parse created:
 *    menus, items, scripts, references and typeData handles. Pool
 *    menu records are shared between reloads, so slots do not imply
 *    ownership; generations may be destroyed in any order. Shared
 *    pool storage outlives handles, as in TS.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_LEGACY_MENU_H
#define QA_UI_COMMON_LEGACY_MENU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/ui/common/legacy/script/lexer.h"
#include "qa/ui/common/legacy/script/preprocessor.h"
#include "qa/ui/common/legacy/team_arena/memory.h"

#define QA_MAX_UI_MENUS 64
#define QA_MAX_UI_MENU_ITEMS 96
#define QA_MAX_UI_COLOR_RANGES 10
#define QA_MAX_UI_LIST_COLUMNS 16
#define QA_MAX_UI_MULTI_CHOICES 31
#define QA_MAX_UI_SCRIPT_BYTES 1023
#define QA_MAX_HUD_MENU_SET_BYTES 4095

/** TS UiWindowFlag. */
enum {
  QA_UI_WINDOW_MOUSE_OVER = 0x00000001,
  QA_UI_WINDOW_HAS_FOCUS = 0x00000002,
  QA_UI_WINDOW_VISIBLE = 0x00000004,
  QA_UI_WINDOW_GREY = 0x00000008,
  QA_UI_WINDOW_DECORATION = 0x00000010,
  QA_UI_WINDOW_FADING_OUT = 0x00000020,
  QA_UI_WINDOW_FADING_IN = 0x00000040,
  QA_UI_WINDOW_MOUSE_OVER_TEXT = 0x00000080,
  QA_UI_WINDOW_IN_TRANSITION = 0x00000100,
  QA_UI_WINDOW_FORE_COLOR_SET = 0x00000200,
  QA_UI_WINDOW_HORIZONTAL = 0x00000400,
  QA_UI_WINDOW_LIST_LEFT_ARROW = 0x00000800,
  QA_UI_WINDOW_LIST_RIGHT_ARROW = 0x00001000,
  QA_UI_WINDOW_LIST_THUMB = 0x00002000,
  QA_UI_WINDOW_LIST_PAGE_UP = 0x00004000,
  QA_UI_WINDOW_LIST_PAGE_DOWN = 0x00008000,
  QA_UI_WINDOW_ORBITING = 0x00010000,
  QA_UI_WINDOW_OUT_OF_BOUNDS_CLICK = 0x00020000,
  QA_UI_WINDOW_WRAPPED = 0x00040000,
  QA_UI_WINDOW_AUTO_WRAPPED = 0x00080000,
  QA_UI_WINDOW_FORCED = 0x00100000,
  QA_UI_WINDOW_POPUP = 0x00200000,
  QA_UI_WINDOW_BACK_COLOR_SET = 0x00400000,
  QA_UI_WINDOW_TIMED_VISIBLE = 0x00800000
};

/** TS UiItemTypeCode. */
typedef enum qa_ui_item_type_code {
  QA_UI_ITEM_TEXT = 0, QA_UI_ITEM_BUTTON = 1, QA_UI_ITEM_RADIO_BUTTON = 2,
  QA_UI_ITEM_CHECK_BOX = 3, QA_UI_ITEM_EDIT_FIELD = 4, QA_UI_ITEM_COMBO = 5,
  QA_UI_ITEM_LIST_BOX = 6, QA_UI_ITEM_MODEL = 7, QA_UI_ITEM_OWNER_DRAW = 8,
  QA_UI_ITEM_NUMERIC_FIELD = 9, QA_UI_ITEM_SLIDER = 10, QA_UI_ITEM_YES_NO = 11,
  QA_UI_ITEM_MULTI = 12, QA_UI_ITEM_BIND = 13
} qa_ui_item_type_code;

/** TS UiRect / UiMutableRect. */
typedef qa_rect qa_ui_rect;

/** TS resource reference kinds (exact TS strings in comments). */
typedef enum qa_ui_resource_kind {
  QA_UI_RESOURCE_SHADER, /* "shader" */
  QA_UI_RESOURCE_MODEL,  /* "model" */
  QA_UI_RESOURCE_SOUND,  /* "sound" */
  QA_UI_RESOURCE_FONT    /* "font" */
} qa_ui_resource_kind;

const char *qa_ui_resource_kind_name(qa_ui_resource_kind kind);

/** TS UiShaderReference/UiModelReference/UiSoundReference/
 * UiFontReference. path NULL is TS null; point_size is font-only. */
struct qa_ui_resource_reference {
  qa_ui_resource_kind kind;
  char *path; /* owned */
  double point_size;
  size_t refs; /* a reload inherits the previous generation's assets */
};
typedef qa_ui_resource_reference qa_ui_shader_reference;
typedef qa_ui_resource_reference qa_ui_model_reference;
typedef qa_ui_resource_reference qa_ui_sound_reference;
typedef qa_ui_resource_reference qa_ui_font_reference;

/* NULL only on OOM. path NULL is TS null (copied otherwise). */
qa_ui_shader_reference *qa_ui_shader_reference_create(const char *path);
qa_ui_model_reference *qa_ui_model_reference_create(const char *path);
qa_ui_sound_reference *qa_ui_sound_reference_create(const char *path);
qa_ui_font_reference *qa_ui_font_reference_create(const char *path,
                                                  double point_size);
qa_ui_resource_reference *qa_ui_resource_reference_retain(
    qa_ui_resource_reference *reference);
/* Releases one reference; the last release frees it. */
void qa_ui_resource_reference_destroy(qa_ui_resource_reference *reference);

/** TS UiScriptToken. */
typedef struct qa_ui_menu_script_token {
  char *text; /* owned */
  qa_ui_script_location location;
} qa_ui_menu_script_token;

void qa_ui_menu_script_token_destroy(qa_ui_menu_script_token *token);

/** TS UiScript. text_ref is one retained reference (owned). */
struct qa_ui_menu_script {
  qa_ui_string_reference *text_ref;
  qa_ui_menu_script_token *tokens;
  size_t token_count;
  bool truncated;
};

/* Malloc'd current text read; NULL only on OOM. */
char *qa_ui_menu_script_text(qa_ui_menu_script *script);
void qa_ui_menu_script_destroy(qa_ui_menu_script *script);

/** TS UiWindowDefinition (view over a 180-byte windowDef_t). */
typedef struct qa_ui_window_definition {
  qa_ui_memory_allocation *alloc; /* owned handle */
} qa_ui_window_definition;

qa_rect qa_ui_window_rect(qa_ui_window_definition *window);
void qa_ui_window_set_rect(qa_ui_window_definition *window, qa_rect value);
qa_rect qa_ui_window_client_rect(qa_ui_window_definition *window);
void qa_ui_window_set_client_rect(qa_ui_window_definition *window,
                                  qa_rect value);
qa_rect qa_ui_window_rect_effects(qa_ui_window_definition *window);
void qa_ui_window_set_rect_effects(qa_ui_window_definition *window,
                                   qa_rect value);
qa_rect qa_ui_window_rect_effects2(qa_ui_window_definition *window);
void qa_ui_window_set_rect_effects2(qa_ui_window_definition *window,
                                    qa_rect value);
/* Malloc'd; NULL is TS undefined (or OOM). */
char *qa_ui_window_name(qa_ui_window_definition *window);
char *qa_ui_window_group(qa_ui_window_definition *window);
char *qa_ui_window_cinematic(qa_ui_window_definition *window);
int32_t qa_ui_window_style(qa_ui_window_definition *window);
int32_t qa_ui_window_border(qa_ui_window_definition *window);
int32_t qa_ui_window_owner_draw(qa_ui_window_definition *window);
int32_t qa_ui_window_owner_draw_flags(qa_ui_window_definition *window);
double qa_ui_window_border_size(qa_ui_window_definition *window);
int32_t qa_ui_window_flags(qa_ui_window_definition *window);
void qa_ui_window_set_flags(qa_ui_window_definition *window, int32_t value);
int32_t qa_ui_window_next_time(qa_ui_window_definition *window);
void qa_ui_window_set_next_time(qa_ui_window_definition *window,
                                int32_t value);
int32_t qa_ui_window_offset_time(qa_ui_window_definition *window);
void qa_ui_window_set_offset_time(qa_ui_window_definition *window,
                                  int32_t value);
int32_t qa_ui_window_cinematic_handle(qa_ui_window_definition *window);
void qa_ui_window_set_cinematic_handle(qa_ui_window_definition *window,
                                       int32_t value);
qa_vec4 qa_ui_window_fore_color(qa_ui_window_definition *window);
void qa_ui_window_set_fore_color(qa_ui_window_definition *window,
                                 qa_vec4 value);
qa_vec4 qa_ui_window_back_color(qa_ui_window_definition *window);
void qa_ui_window_set_back_color(qa_ui_window_definition *window,
                                 qa_vec4 value);
qa_vec4 qa_ui_window_border_color(qa_ui_window_definition *window);
void qa_ui_window_set_border_color(qa_ui_window_definition *window,
                                   qa_vec4 value);
qa_vec4 qa_ui_window_outline_color(qa_ui_window_definition *window);
void qa_ui_window_set_outline_color(qa_ui_window_definition *window,
                                    qa_vec4 value);
/* Component access for in-place color writes (index 0..3 = x,y,z,w). */
void qa_ui_window_set_fore_color_component(qa_ui_window_definition *window,
                                           int component, double value);
void qa_ui_window_set_back_color_component(qa_ui_window_definition *window,
                                           int component, double value);
void qa_ui_window_set_border_color_component(
    qa_ui_window_definition *window, int component, double value);
/* Borrowed; NULL is TS undefined. Aborts on a non-shader alias. */
qa_ui_shader_reference *qa_ui_window_background(
    qa_ui_window_definition *window);
/* False is TS undefined handle. */
bool qa_ui_window_background_handle(qa_ui_window_definition *window,
                                    int32_t *out);
/* Takes ownership of `value` (retained by the allocation map). */
void qa_ui_window_set_background(qa_ui_window_definition *window,
                                 qa_ui_shader_reference *value,
                                 bool has_handle, int32_t handle);

/** TS UiColorRange. */
typedef struct qa_ui_color_range {
  double low;
  double high;
  qa_vec4 color;
} qa_ui_color_range;

/** TS UiEditFieldDefinition (view over a 28-byte editFieldDef_t). */
typedef struct qa_ui_edit_field_definition {
  qa_ui_memory_allocation *alloc; /* borrowed from the owning item */
} qa_ui_edit_field_definition;

double qa_ui_edit_minimum(qa_ui_edit_field_definition *edit);
double qa_ui_edit_maximum(qa_ui_edit_field_definition *edit);
double qa_ui_edit_default_value(qa_ui_edit_field_definition *edit);
double qa_ui_edit_range(qa_ui_edit_field_definition *edit);
int32_t qa_ui_edit_max_chars(qa_ui_edit_field_definition *edit);
int32_t qa_ui_edit_max_paint_chars(qa_ui_edit_field_definition *edit);
int32_t qa_ui_edit_paint_offset(qa_ui_edit_field_definition *edit);
void qa_ui_edit_set_paint_offset(qa_ui_edit_field_definition *edit,
                                 int32_t value);

/** TS UiListColumn. */
typedef struct qa_ui_list_column {
  int32_t position;
  int32_t width;
  int32_t max_chars;
} qa_ui_list_column;

/** TS UiListBoxDefinition (view over a 232-byte listBoxDef_t). */
typedef struct qa_ui_list_box_definition {
  qa_ui_memory_allocation *alloc; /* borrowed from the owning item */
} qa_ui_list_box_definition;

int32_t qa_ui_list_start_position(qa_ui_list_box_definition *list);
void qa_ui_list_set_start_position(qa_ui_list_box_definition *list,
                                   int32_t value);
int32_t qa_ui_list_end_position(qa_ui_list_box_definition *list);
void qa_ui_list_set_end_position(qa_ui_list_box_definition *list,
                                 int32_t value);
int32_t qa_ui_list_draw_padding(qa_ui_list_box_definition *list);
void qa_ui_list_set_draw_padding(qa_ui_list_box_definition *list,
                                 int32_t value);
int32_t qa_ui_list_cursor_position(qa_ui_list_box_definition *list);
void qa_ui_list_set_cursor_position(qa_ui_list_box_definition *list,
                                    int32_t value);
double qa_ui_list_element_width(qa_ui_list_box_definition *list);
double qa_ui_list_element_height(qa_ui_list_box_definition *list);
int32_t qa_ui_list_element_style(qa_ui_list_box_definition *list);
/* Aborts (RangeError) when the stored count exceeds 16. */
size_t qa_ui_list_column_count(qa_ui_list_box_definition *list);
qa_ui_list_column qa_ui_list_column_at(qa_ui_list_box_definition *list,
                                       size_t index);
/* Borrowed; NULL is TS undefined. */
qa_ui_menu_script *qa_ui_list_double_click(qa_ui_list_box_definition *list);
bool qa_ui_list_not_selectable(qa_ui_list_box_definition *list);

/** TS UiMultiDefinition (view over a 392-byte multiDef_t). */
typedef struct qa_ui_multi_definition {
  qa_ui_memory_allocation *alloc; /* borrowed from the owning item */
} qa_ui_multi_definition;

int32_t qa_ui_multi_count(qa_ui_multi_definition *multi);
bool qa_ui_multi_string_definition(qa_ui_multi_definition *multi);
/* Malloc'd; NULL is TS undefined (or OOM). Index checked to 0..31. */
char *qa_ui_multi_label(qa_ui_multi_definition *multi, double index);
char *qa_ui_multi_string_value(qa_ui_multi_definition *multi, double index);
double qa_ui_multi_number_value(qa_ui_multi_definition *multi, double index);

/** TS UiModelDefinition (view over a 28-byte modelDef_t). */
typedef struct qa_ui_model_definition {
  qa_ui_memory_allocation *alloc; /* borrowed from the owning item */
} qa_ui_model_definition;

int32_t qa_ui_model_angle(qa_ui_model_definition *model);
void qa_ui_model_set_angle(qa_ui_model_definition *model, int32_t value);
qa_vec3 qa_ui_model_origin(qa_ui_model_definition *model);
double qa_ui_model_field_of_view_x(qa_ui_model_definition *model);
double qa_ui_model_field_of_view_y(qa_ui_model_definition *model);
int32_t qa_ui_model_rotation_speed(qa_ui_model_definition *model);

/** TS UiItemBehavior kind (exact TS strings via the name fn). */
typedef enum qa_ui_item_behavior_kind {
  QA_UI_BEHAVIOR_TEXT, QA_UI_BEHAVIOR_BUTTON, QA_UI_BEHAVIOR_RADIO_BUTTON,
  QA_UI_BEHAVIOR_CHECK_BOX, QA_UI_BEHAVIOR_EDIT_FIELD, QA_UI_BEHAVIOR_COMBO,
  QA_UI_BEHAVIOR_LIST_BOX, QA_UI_BEHAVIOR_MODEL, QA_UI_BEHAVIOR_OWNER_DRAW,
  QA_UI_BEHAVIOR_NUMERIC_FIELD, QA_UI_BEHAVIOR_SLIDER, QA_UI_BEHAVIOR_YES_NO,
  QA_UI_BEHAVIOR_MULTI, QA_UI_BEHAVIOR_BIND, QA_UI_BEHAVIOR_UNKNOWN
} qa_ui_item_behavior_kind;

const char *qa_ui_item_behavior_kind_name(qa_ui_item_behavior_kind kind);

/** TS UiCvarRule kind. */
typedef enum qa_ui_cvar_rule_kind {
  QA_UI_CVAR_RULE_ENABLE, QA_UI_CVAR_RULE_DISABLE,
  QA_UI_CVAR_RULE_SHOW, QA_UI_CVAR_RULE_HIDE
} qa_ui_cvar_rule_kind;

const char *qa_ui_cvar_rule_kind_name(qa_ui_cvar_rule_kind kind);

/** TS UiItemDefinition (view over a 540-byte itemDef_t). The
 * edit/list/model/multi view handles are cached per typeData
 * allocation, mirroring the TS WeakMap views. */
struct qa_ui_item_definition {
  qa_ui_memory_allocation *alloc; /* owned handle */
  qa_ui_window_definition window;
  qa_ui_script_location location;
  bool has_allocation_offset; /* false is TS undefined */
  double allocation_offset;
  qa_ui_memory_allocation *edit_source;
  qa_ui_memory_allocation *edit_alloc;
  qa_ui_memory_allocation *list_source;
  qa_ui_memory_allocation *list_alloc;
  qa_ui_memory_allocation *model_source;
  qa_ui_memory_allocation *model_alloc;
  qa_ui_memory_allocation *multi_source;
  qa_ui_memory_allocation *multi_alloc;
};

void qa_ui_item_definition_destroy(qa_ui_item_definition *item);

int32_t qa_ui_item_type(qa_ui_item_definition *item);
/* Borrowed; NULL is TS undefined. */
qa_ui_menu_definition *qa_ui_item_parent(qa_ui_item_definition *item);
void qa_ui_item_set_parent(qa_ui_item_definition *item,
                           qa_ui_menu_definition *parent);
qa_rect qa_ui_item_text_rect(qa_ui_item_definition *item);
void qa_ui_item_set_text_rect(qa_ui_item_definition *item, qa_rect value);
/* Component writes into textRect (index 0..3 = x,y,width,height). */
void qa_ui_item_set_text_rect_component(qa_ui_item_definition *item,
                                        int component, double value);
qa_ui_item_behavior_kind qa_ui_item_behavior(qa_ui_item_definition *item);
int32_t qa_ui_item_alignment(qa_ui_item_definition *item);
int32_t qa_ui_item_text_alignment(qa_ui_item_definition *item);
double qa_ui_item_text_align_x(qa_ui_item_definition *item);
double qa_ui_item_text_align_y(qa_ui_item_definition *item);
double qa_ui_item_text_scale(qa_ui_item_definition *item);
int32_t qa_ui_item_text_style(qa_ui_item_definition *item);
/* Malloc'd; NULL is TS undefined (or OOM). */
char *qa_ui_item_text(qa_ui_item_definition *item);
char *qa_ui_item_cvar(qa_ui_item_definition *item);
char *qa_ui_item_cvar_test(qa_ui_item_definition *item);
/* Borrowed resource; NULL is TS undefined. Aborts on sound alias. */
qa_ui_resource_reference *qa_ui_item_asset(qa_ui_item_definition *item);
bool qa_ui_item_asset_handle(qa_ui_item_definition *item, int32_t *out);
/* Borrowed scripts; NULL is TS undefined. */
qa_ui_menu_script *qa_ui_item_mouse_enter_text(qa_ui_item_definition *item);
qa_ui_menu_script *qa_ui_item_mouse_exit_text(qa_ui_item_definition *item);
qa_ui_menu_script *qa_ui_item_mouse_enter(qa_ui_item_definition *item);
qa_ui_menu_script *qa_ui_item_mouse_exit(qa_ui_item_definition *item);
qa_ui_menu_script *qa_ui_item_action(qa_ui_item_definition *item);
qa_ui_menu_script *qa_ui_item_on_focus(qa_ui_item_definition *item);
qa_ui_menu_script *qa_ui_item_leave_focus(qa_ui_item_definition *item);
qa_ui_menu_script *qa_ui_item_cvar_script(qa_ui_item_definition *item);
int32_t qa_ui_item_cvar_flags(qa_ui_item_definition *item);
/* False is TS undefined rule; *out_script borrowed (may be NULL). */
bool qa_ui_item_cvar_rule(qa_ui_item_definition *item,
                          qa_ui_cvar_rule_kind *out_kind,
                          qa_ui_menu_script **out_script);
/* Borrowed; NULL is TS undefined. Aborts on a non-sound alias. */
qa_ui_sound_reference *qa_ui_item_focus_sound(qa_ui_item_definition *item);
bool qa_ui_item_focus_sound_handle(qa_ui_item_definition *item,
                                   int32_t *out);
/* Aborts (RangeError) when the stored count exceeds 10. */
size_t qa_ui_item_color_range_count(qa_ui_item_definition *item);
qa_ui_color_range qa_ui_item_color_range_at(qa_ui_item_definition *item,
                                            size_t index);
double qa_ui_item_special(qa_ui_item_definition *item);
void qa_ui_item_set_special(qa_ui_item_definition *item, double value);
int32_t qa_ui_item_cursor_position(qa_ui_item_definition *item);
void qa_ui_item_set_cursor_position(qa_ui_item_definition *item,
                                    int32_t value);
/* False is TS undefined typeData view. */
bool qa_ui_item_edit_data(qa_ui_item_definition *item,
                          qa_ui_edit_field_definition *out);
bool qa_ui_item_list_data(qa_ui_item_definition *item,
                          qa_ui_list_box_definition *out);
bool qa_ui_item_model_data(qa_ui_item_definition *item,
                           qa_ui_model_definition *out);
bool qa_ui_item_multi_data(qa_ui_item_definition *item,
                           qa_ui_multi_definition *out);
/* Behavior-shaped required views (abort on NULL typeData, as TS). */
qa_ui_edit_field_definition qa_ui_item_required_edit_data(
    qa_ui_item_definition *item);
qa_ui_list_box_definition qa_ui_item_required_list_data(
    qa_ui_item_definition *item);

/** TS UiMenuDefinition (view over a 644-byte menuDef_t). */
struct qa_ui_menu_definition {
  qa_ui_memory_allocation *alloc; /* owned handle (menuRecord) */
  qa_ui_window_definition window;
  qa_ui_script_location location;
};

/* Frees this view only: its items belong to the parse that created them. */
void qa_ui_menu_definition_destroy(qa_ui_menu_definition *menu);

double qa_ui_menu_source_index(qa_ui_menu_definition *menu);
/* Malloc'd; NULL is TS undefined (or OOM). */
char *qa_ui_menu_font(qa_ui_menu_definition *menu);
int32_t qa_ui_menu_full_screen(qa_ui_menu_definition *menu);
int32_t qa_ui_menu_cursor_item(qa_ui_menu_definition *menu);
void qa_ui_menu_set_cursor_item(qa_ui_menu_definition *menu, int32_t value);
int32_t qa_ui_menu_font_index(qa_ui_menu_definition *menu);
int32_t qa_ui_menu_fade_cycle(qa_ui_menu_definition *menu);
double qa_ui_menu_fade_clamp(qa_ui_menu_definition *menu);
double qa_ui_menu_fade_amount(qa_ui_menu_definition *menu);
/* Borrowed scripts; NULL is TS undefined. */
qa_ui_menu_script *qa_ui_menu_on_open(qa_ui_menu_definition *menu);
qa_ui_menu_script *qa_ui_menu_on_close(qa_ui_menu_definition *menu);
qa_ui_menu_script *qa_ui_menu_on_escape(qa_ui_menu_definition *menu);
/* Malloc'd sound-loop path; NULL is TS undefined soundLoop. */
char *qa_ui_menu_sound_loop_path(qa_ui_menu_definition *menu);
qa_vec4 qa_ui_menu_focus_color(qa_ui_menu_definition *menu);
qa_vec4 qa_ui_menu_disable_color(qa_ui_menu_definition *menu);
int32_t qa_ui_menu_item_count(qa_ui_menu_definition *menu);
/* Borrowed; NULL is TS undefined. Index checked to 0..95. */
qa_ui_item_definition *qa_ui_menu_item_at(qa_ui_menu_definition *menu,
                                          double index);

/** TS UiGlobalAssets. Reference pointers NULL is TS undefined
 * (borrowed from the owning definitions). */
typedef struct qa_ui_global_assets {
  qa_ui_font_reference *text_font;
  qa_ui_font_reference *small_font;
  qa_ui_font_reference *big_font;
  qa_ui_shader_reference *cursor;
  qa_ui_shader_reference *gradient_bar;
  qa_ui_sound_reference *menu_enter_sound;
  qa_ui_sound_reference *menu_exit_sound;
  qa_ui_sound_reference *menu_buzz_sound;
  qa_ui_sound_reference *item_focus_sound;
  double fade_clamp;
  double fade_cycle;
  double fade_amount;
  double shadow_x;
  double shadow_y;
  qa_vec4 shadow_color;
  double shadow_fade_clamp;
} qa_ui_global_assets;

/** TS UiMenuRegistrationEvent. */
typedef struct qa_ui_menu_registration_event {
  qa_ui_resource_kind kind;
  qa_ui_resource_reference *reference; /* borrowed */
  qa_ui_script_location location;
} qa_ui_menu_registration_event;

/** TS UiMenuRegistrationSink: false result is TS void; true sets
 * *out_handle (TS {handle}). */
typedef bool (*qa_ui_menu_register_fn)(
    void *ctx, const qa_ui_menu_registration_event *event,
    double *out_handle);

/** TS UiMenuAssetPublication field. */
typedef enum qa_ui_menu_asset_field {
  QA_UI_ASSET_TEXT_FONT, QA_UI_ASSET_SMALL_FONT, QA_UI_ASSET_BIG_FONT,
  QA_UI_ASSET_CURSOR, QA_UI_ASSET_GRADIENT_BAR,
  QA_UI_ASSET_MENU_ENTER_SOUND, QA_UI_ASSET_MENU_EXIT_SOUND,
  QA_UI_ASSET_MENU_BUZZ_SOUND, QA_UI_ASSET_ITEM_FOCUS_SOUND,
  QA_UI_ASSET_FADE_CLAMP, QA_UI_ASSET_FADE_CYCLE, QA_UI_ASSET_FADE_AMOUNT,
  QA_UI_ASSET_SHADOW_X, QA_UI_ASSET_SHADOW_Y, QA_UI_ASSET_SHADOW_FADE_CLAMP,
  QA_UI_ASSET_SHADOW_COLOR_COMPONENT, /* component 0..3 = x,y,z,w */
  QA_UI_ASSET_FONT_REGISTERED,
  QA_UI_ASSET_CURSOR_STR /* string_value NULL is TS null */
} qa_ui_menu_asset_field;

const char *qa_ui_menu_asset_field_name(qa_ui_menu_asset_field field);

/** TS UiMenuAssetPublication. */
typedef struct qa_ui_menu_asset_publication {
  qa_ui_menu_asset_field field;
  qa_ui_resource_reference *reference;      /* resource fields */
  double number;                            /* numeric fields */
  int component;                            /* shadowColorComponent */
  bool boolean;                             /* fontRegistered */
  qa_ui_string_reference *string_value;     /* cursorStr, borrowed */
} qa_ui_menu_asset_publication;

/** TS UiMenuAssetSink. */
typedef void (*qa_ui_menu_publish_asset_fn)(
    void *ctx, const qa_ui_menu_asset_publication *event);

/** TS UiMenuRegistrationState kind. */
typedef enum qa_ui_menu_registration_kind {
  QA_UI_REGISTRATION_DEFERRED,  /* "deferred" */
  QA_UI_REGISTRATION_COMPLETED  /* "completed" */
} qa_ui_menu_registration_kind;

/** TS UiMenuMemoryOwnership. memory is borrowed (qvm32 only). */
typedef enum qa_ui_menu_memory_kind {
  QA_UI_MENU_MEMORY_UNACCOUNTED, /* "unaccounted" */
  QA_UI_MENU_MEMORY_QVM32        /* "qvm32" */
} qa_ui_menu_memory_kind;

typedef struct qa_ui_menu_memory_ownership {
  qa_ui_menu_memory_kind kind;
  qa_ui_team_arena_memory *memory;
} qa_ui_menu_memory_ownership;

typedef struct qa_ui_menu_ownership qa_ui_menu_ownership;

/** TS UiMenuDefinitions. Owned; destroy frees everything but the
 * borrowed memory owner. */
typedef struct qa_ui_menu_definitions {
  qa_ui_menu_memory_ownership memory;
  qa_ui_menu_definition **menus;
  size_t menu_count;
  qa_ui_global_assets assets;
  char **loaded_files;
  size_t loaded_file_count;
  qa_ui_script_diagnostic *diagnostics;
  size_t diagnostic_count;
  qa_ui_menu_registration_kind registration_kind;
  qa_ui_menu_registration_event *registration_events;
  size_t registration_event_count;
  bool font_registered;
  qa_ui_menu_ownership *ownership;
} qa_ui_menu_definitions;

void qa_ui_menu_definitions_destroy(qa_ui_menu_definitions *definitions);

/** TS UiMenuResolver (IncludeResolver + resolveRoot). Resolve fns
 * return false for TS undefined. */
typedef bool (*qa_ui_menu_resolve_root_fn)(void *ctx, const char *path,
                                           qa_ui_script_source *out);
typedef struct qa_ui_menu_resolver {
  qa_ui_menu_resolve_root_fn resolve_root;
  void *resolve_root_ctx;
  qa_ui_script_resolve_fn resolve;
  void *resolve_ctx;
} qa_ui_menu_resolver;

/** TS UiMenuRandom.nextInt (TS number; range-checked by parser). */
typedef double (*qa_ui_menu_random_fn)(void *ctx);

/** TS UiMenuScriptSources. read/source fns false is TS undefined. */
typedef struct qa_ui_menu_script_sources {
  void *ctx;
  double (*load_source_handle)(void *ctx, const char *path);
  bool (*read_token_handle)(void *ctx, double handle,
                            qa_ui_script_token_record *out);
  bool (*source_file_and_line)(void *ctx, double handle, char *filename,
                               size_t capacity, int64_t *out_line);
  bool (*free_source_handle)(void *ctx, double handle);
} qa_ui_menu_script_sources;

/** TS UiMenuParseHost, kind-tagged. */
typedef enum qa_ui_menu_parse_host_kind {
  QA_UI_PARSE_HOST_RESOLVED,
  QA_UI_PARSE_HOST_SCRIPT_SOURCES
} qa_ui_menu_parse_host_kind;

typedef qa_ui_menu_script_sources (*qa_ui_menu_script_sources_fn)(void *ctx);
typedef void (*qa_ui_menu_assert_current_fn)(void *ctx);

typedef struct qa_ui_menu_parse_host {
  qa_ui_menu_parse_host_kind kind;
  qa_ui_menu_resolver resolver; /* resolved */
  qa_ui_menu_random_fn random;
  void *random_ctx;
  qa_ui_menu_script_sources_fn script_sources; /* script-sources */
  void *script_sources_ctx;
  qa_ui_menu_assert_current_fn assert_current_operation;
  void *assert_current_ctx;
} qa_ui_menu_parse_host;

/** TS UiMenuResolvedParseHost (loadMenuDefinitions host shape). */
typedef qa_ui_menu_parse_host qa_ui_menu_resolved_parse_host;

/** TS UiMenuParseOptions. has_* flags mark TS-present fields;
 * sink pointers NULL is TS undefined. */
typedef struct qa_ui_menu_parse_options {
  bool has_memory;
  qa_ui_menu_memory_ownership memory;
  qa_ui_menu_register_fn registration_sink;
  void *registration_sink_ctx;
  qa_ui_menu_publish_asset_fn asset_sink;
  void *asset_sink_ctx;
  const qa_ui_global_assets *initial_assets;
  bool has_initial_font_registered;
  bool initial_font_registered;
  qa_ui_script_report_fn report_diagnostic;
  void *report_diagnostic_ctx;
} qa_ui_menu_parse_options;

/** TS UiMenuLoadPlan: only the HUD form { kind: "hud", setPath } remains
 * (the "ui" menus.txt/ingame.txt plan was dropped); set_path borrowed. */
typedef struct qa_ui_menu_load_plan {
  const char *set_path;
} qa_ui_menu_load_plan;

qa_ui_menu_load_plan qa_ui_default_hud_menu_plan(void);

/** TS UiMenuTokenSource. next false is TS undefined. */
typedef struct qa_ui_menu_token_source {
  void *ctx;
  const char *path; /* borrowed for the cursor's lifetime */
  bool (*next)(void *ctx, qa_ui_script_token_record *out);
  void (*position)(void *ctx, char *filename, size_t capacity,
                   int64_t *out_line);
  void (*dispose)(void *ctx);
} qa_ui_menu_token_source;

typedef struct qa_ui_menu_token_cursor qa_ui_menu_token_cursor;

/* NULL only on OOM; takes a copy of the source table. */
qa_ui_menu_token_cursor *qa_ui_menu_token_cursor_create(
    const qa_ui_menu_token_source *source);
/* TS UiMenuTokenCursor.open. NULL only on OOM. */
qa_ui_menu_token_cursor *qa_ui_menu_token_cursor_open(
    const qa_ui_script_source *source, qa_ui_script_resolve_fn resolve,
    void *resolve_ctx, const qa_ui_script_preprocessor_options *options);
/* TS UiMenuTokenCursor.openHandle; NULL is TS undefined (handle 0)
 * or OOM. `sources`/`current` may not be NULL. */
qa_ui_menu_token_cursor *qa_ui_menu_token_cursor_open_handle(
    const char *path, qa_ui_menu_script_sources_fn sources,
    void *sources_ctx, qa_ui_menu_assert_current_fn current,
    void *current_ctx);
void qa_ui_menu_token_cursor_destroy(qa_ui_menu_token_cursor *cursor);
const char *qa_ui_menu_token_cursor_path(qa_ui_menu_token_cursor *cursor);
void qa_ui_menu_token_cursor_position(qa_ui_menu_token_cursor *cursor,
                                      char *filename, size_t capacity,
                                      int64_t *out_line);
/* False is TS undefined; *out takes one token reference. */
bool qa_ui_menu_token_cursor_next(qa_ui_menu_token_cursor *cursor,
                                  qa_ui_script_token **out);
void qa_ui_menu_token_cursor_dispose(qa_ui_menu_token_cursor *cursor);

/** TS UiMenuSourceParser. */
typedef struct qa_ui_menu_source_parser qa_ui_menu_source_parser;

/* NULL only on OOM. Options/host copied by value. */
qa_ui_menu_source_parser *qa_ui_menu_source_parser_create(
    const qa_ui_menu_parse_host *host,
    const qa_ui_script_preprocessor_options *preprocessor_options,
    const qa_ui_menu_parse_options *parse_options);
void qa_ui_menu_source_parser_destroy(qa_ui_menu_source_parser *parser);
/* Fills *out (owned). ScriptLanguageError/TypeError aborts. */
void qa_ui_menu_source_parser_load(qa_ui_menu_source_parser *parser,
                                   const qa_ui_menu_load_plan *plan,
                                   qa_ui_menu_definitions *out);
/* TS parseSource over a text source. */
void qa_ui_menu_source_parser_parse_source(
    qa_ui_menu_source_parser *parser, const qa_ui_script_source *source);
/* TS parseSource over an open cursor (takes ownership). */
void qa_ui_menu_source_parser_parse_cursor(
    qa_ui_menu_source_parser *parser, qa_ui_menu_token_cursor *cursor);

/** TS loadMenuDefinitions. Zero options select TS defaults. */
void qa_ui_load_menu_definitions(
    const qa_ui_menu_resolved_parse_host *host,
    const qa_ui_menu_load_plan *plan,
    const qa_ui_script_preprocessor_options *preprocessor_options,
    const qa_ui_menu_parse_options *parse_options,
    qa_ui_menu_definitions *out);

#endif
