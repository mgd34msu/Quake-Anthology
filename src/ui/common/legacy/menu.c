/* See include/qa/ui/common/legacy/menu.h. Note: UiMenuTokenCursor
 * converts a reader source failure to EOF in TS; the landed C
 * script engine aborts uncaught failures (its catch stack is
 * engine-private), matching the bots script-sources port.
 * GPL-2.0-or-later. */
#include "qa/ui/common/legacy/menu.h"

#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_parse.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js_case.h"

#define FR(x) ((double)(float)(x))

static char *dupstr(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (copy != NULL) memcpy(copy, text, length + 1);
  return copy;
}

static qa_ui_script_location loc_copy(const qa_ui_script_location *value) {
  qa_ui_script_location out = {dupstr(value->path), value->line,
                               value->column};
  return out;
}

static qa_ui_script_location loc_make(const char *path, int64_t line,
                                      int64_t column) {
  qa_ui_script_location out = {dupstr(path), line, column};
  return out;
}

static qa_vec4 color4(double x, double y, double z, double w) {
  qa_vec4 out = {FR(x), FR(y), FR(z), FR(w)};
  return out;
}

const char *qa_ui_resource_kind_name(qa_ui_resource_kind kind) {
  switch (kind) {
    case QA_UI_RESOURCE_SHADER: return "shader";
    case QA_UI_RESOURCE_MODEL: return "model";
    case QA_UI_RESOURCE_SOUND: return "sound";
    case QA_UI_RESOURCE_FONT: return "font";
  }
  return "";
}

const char *qa_ui_item_behavior_kind_name(qa_ui_item_behavior_kind kind) {
  switch (kind) {
    case QA_UI_BEHAVIOR_TEXT: return "text";
    case QA_UI_BEHAVIOR_BUTTON: return "button";
    case QA_UI_BEHAVIOR_RADIO_BUTTON: return "radio-button";
    case QA_UI_BEHAVIOR_CHECK_BOX: return "check-box";
    case QA_UI_BEHAVIOR_EDIT_FIELD: return "edit-field";
    case QA_UI_BEHAVIOR_COMBO: return "combo";
    case QA_UI_BEHAVIOR_LIST_BOX: return "list-box";
    case QA_UI_BEHAVIOR_MODEL: return "model";
    case QA_UI_BEHAVIOR_OWNER_DRAW: return "owner-draw";
    case QA_UI_BEHAVIOR_NUMERIC_FIELD: return "numeric-field";
    case QA_UI_BEHAVIOR_SLIDER: return "slider";
    case QA_UI_BEHAVIOR_YES_NO: return "yes-no";
    case QA_UI_BEHAVIOR_MULTI: return "multi";
    case QA_UI_BEHAVIOR_BIND: return "bind";
    case QA_UI_BEHAVIOR_UNKNOWN: return "unknown";
  }
  return "";
}

const char *qa_ui_cvar_rule_kind_name(qa_ui_cvar_rule_kind kind) {
  switch (kind) {
    case QA_UI_CVAR_RULE_ENABLE: return "enable";
    case QA_UI_CVAR_RULE_DISABLE: return "disable";
    case QA_UI_CVAR_RULE_SHOW: return "show";
    case QA_UI_CVAR_RULE_HIDE: return "hide";
  }
  return "";
}

const char *qa_ui_menu_asset_field_name(qa_ui_menu_asset_field field) {
  switch (field) {
    case QA_UI_ASSET_TEXT_FONT: return "textFont";
    case QA_UI_ASSET_SMALL_FONT: return "smallFont";
    case QA_UI_ASSET_BIG_FONT: return "bigFont";
    case QA_UI_ASSET_CURSOR: return "cursor";
    case QA_UI_ASSET_GRADIENT_BAR: return "gradientBar";
    case QA_UI_ASSET_MENU_ENTER_SOUND: return "menuEnterSound";
    case QA_UI_ASSET_MENU_EXIT_SOUND: return "menuExitSound";
    case QA_UI_ASSET_MENU_BUZZ_SOUND: return "menuBuzzSound";
    case QA_UI_ASSET_ITEM_FOCUS_SOUND: return "itemFocusSound";
    case QA_UI_ASSET_FADE_CLAMP: return "fadeClamp";
    case QA_UI_ASSET_FADE_CYCLE: return "fadeCycle";
    case QA_UI_ASSET_FADE_AMOUNT: return "fadeAmount";
    case QA_UI_ASSET_SHADOW_X: return "shadowX";
    case QA_UI_ASSET_SHADOW_Y: return "shadowY";
    case QA_UI_ASSET_SHADOW_FADE_CLAMP: return "shadowFadeClamp";
    case QA_UI_ASSET_SHADOW_COLOR_COMPONENT: return "shadowColorComponent";
    case QA_UI_ASSET_FONT_REGISTERED: return "fontRegistered";
    case QA_UI_ASSET_CURSOR_STR: return "cursorStr";
  }
  return "";
}

static qa_ui_resource_reference *resource_create(qa_ui_resource_kind kind,
                                                 const char *path,
                                                 double point_size) {
  qa_ui_resource_reference *reference = calloc(1, sizeof *reference);
  if (reference == NULL) return NULL;
  reference->kind = kind;
  reference->path = dupstr(path);
  reference->point_size = point_size;
  reference->refs = 1;
  return reference;
}

qa_ui_shader_reference *qa_ui_shader_reference_create(const char *path) {
  return resource_create(QA_UI_RESOURCE_SHADER, path, 0);
}
qa_ui_model_reference *qa_ui_model_reference_create(const char *path) {
  return resource_create(QA_UI_RESOURCE_MODEL, path, 0);
}
qa_ui_sound_reference *qa_ui_sound_reference_create(const char *path) {
  return resource_create(QA_UI_RESOURCE_SOUND, path, 0);
}
qa_ui_font_reference *qa_ui_font_reference_create(const char *path,
                                                  double point_size) {
  return resource_create(QA_UI_RESOURCE_FONT, path, point_size);
}
qa_ui_resource_reference *qa_ui_resource_reference_retain(
    qa_ui_resource_reference *reference) {
  if (reference != NULL) reference->refs++;
  return reference;
}
void qa_ui_resource_reference_destroy(qa_ui_resource_reference *reference) {
  if (reference == NULL || --reference->refs > 0) return;
  free(reference->path);
  free(reference);
}

void qa_ui_menu_script_token_destroy(qa_ui_menu_script_token *token) {
  if (token == NULL) return;
  free(token->text);
  qa_ui_script_location_destroy(&token->location);
  token->text = NULL;
}

char *qa_ui_menu_script_text(qa_ui_menu_script *script) {
  return qa_ui_string_reference_read(script->text_ref);
}

void qa_ui_menu_script_destroy(qa_ui_menu_script *script) {
  if (script == NULL) return;
  qa_ui_string_reference_release(script->text_ref);
  for (size_t i = 0; i < script->token_count; i++)
    qa_ui_menu_script_token_destroy(&script->tokens[i]);
  free(script->tokens);
  free(script);
}

/* ---- allocation-backed rect/color views ---- */

typedef qa_ui_memory_allocation alloc_t;

static qa_rect rect_at(alloc_t *alloc, int64_t offset) {
  qa_rect out = {qa_ui_memory_allocation_get_float32(alloc, offset),
                 qa_ui_memory_allocation_get_float32(alloc, offset + 4),
                 qa_ui_memory_allocation_get_float32(alloc, offset + 8),
                 qa_ui_memory_allocation_get_float32(alloc, offset + 12)};
  return out;
}
static void rect_write(alloc_t *alloc, int64_t offset, qa_rect value) {
  qa_ui_memory_allocation_set_float32(alloc, offset, value.x);
  qa_ui_memory_allocation_set_float32(alloc, offset + 4, value.y);
  qa_ui_memory_allocation_set_float32(alloc, offset + 8, value.width);
  qa_ui_memory_allocation_set_float32(alloc, offset + 12, value.height);
}
static qa_vec4 color_at(alloc_t *alloc, int64_t offset) {
  qa_vec4 out = {qa_ui_memory_allocation_get_float32(alloc, offset),
                 qa_ui_memory_allocation_get_float32(alloc, offset + 4),
                 qa_ui_memory_allocation_get_float32(alloc, offset + 8),
                 qa_ui_memory_allocation_get_float32(alloc, offset + 12)};
  return out;
}
static void color_write(alloc_t *alloc, int64_t offset, qa_vec4 value) {
  qa_ui_memory_allocation_set_float32(alloc, offset, value.x);
  qa_ui_memory_allocation_set_float32(alloc, offset + 4, value.y);
  qa_ui_memory_allocation_set_float32(alloc, offset + 8, value.z);
  qa_ui_memory_allocation_set_float32(alloc, offset + 12, value.w);
}

/* ---- UiWindowDefinition ---- */

static void window_initialize(qa_ui_window_definition *window) {
  qa_ui_memory_allocation_clear(window->alloc);
  qa_ui_memory_allocation_set_float32(window->alloc, 64, 1);
  color_write(window->alloc, 112, color4(1, 1, 1, 1));
  qa_ui_memory_allocation_set_int32(window->alloc, 44, -1);
}

qa_rect qa_ui_window_rect(qa_ui_window_definition *w) {
  return rect_at(w->alloc, 0);
}
void qa_ui_window_set_rect(qa_ui_window_definition *w, qa_rect v) {
  rect_write(w->alloc, 0, v);
}
qa_rect qa_ui_window_client_rect(qa_ui_window_definition *w) {
  return rect_at(w->alloc, 16);
}
void qa_ui_window_set_client_rect(qa_ui_window_definition *w, qa_rect v) {
  rect_write(w->alloc, 16, v);
}
qa_rect qa_ui_window_rect_effects(qa_ui_window_definition *w) {
  return rect_at(w->alloc, 72);
}
void qa_ui_window_set_rect_effects(qa_ui_window_definition *w, qa_rect v) {
  rect_write(w->alloc, 72, v);
}
qa_rect qa_ui_window_rect_effects2(qa_ui_window_definition *w) {
  return rect_at(w->alloc, 88);
}
void qa_ui_window_set_rect_effects2(qa_ui_window_definition *w, qa_rect v) {
  rect_write(w->alloc, 88, v);
}
char *qa_ui_window_name(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_string(w->alloc, 32);
}
char *qa_ui_window_group(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_string(w->alloc, 36);
}
char *qa_ui_window_cinematic(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_string(w->alloc, 40);
}
int32_t qa_ui_window_style(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_int32(w->alloc, 48);
}
int32_t qa_ui_window_border(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_int32(w->alloc, 52);
}
int32_t qa_ui_window_owner_draw(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_int32(w->alloc, 56);
}
int32_t qa_ui_window_owner_draw_flags(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_int32(w->alloc, 60);
}
double qa_ui_window_border_size(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_float32(w->alloc, 64);
}
int32_t qa_ui_window_flags(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_int32(w->alloc, 68);
}
void qa_ui_window_set_flags(qa_ui_window_definition *w, int32_t v) {
  qa_ui_memory_allocation_set_int32(w->alloc, 68, v);
}
int32_t qa_ui_window_next_time(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_int32(w->alloc, 108);
}
void qa_ui_window_set_next_time(qa_ui_window_definition *w, int32_t v) {
  qa_ui_memory_allocation_set_int32(w->alloc, 108, v);
}
int32_t qa_ui_window_offset_time(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_int32(w->alloc, 104);
}
void qa_ui_window_set_offset_time(qa_ui_window_definition *w, int32_t v) {
  qa_ui_memory_allocation_set_int32(w->alloc, 104, v);
}
int32_t qa_ui_window_cinematic_handle(qa_ui_window_definition *w) {
  return qa_ui_memory_allocation_get_int32(w->alloc, 44);
}
void qa_ui_window_set_cinematic_handle(qa_ui_window_definition *w,
                                       int32_t v) {
  qa_ui_memory_allocation_set_int32(w->alloc, 44, v);
}
qa_vec4 qa_ui_window_fore_color(qa_ui_window_definition *w) {
  return color_at(w->alloc, 112);
}
void qa_ui_window_set_fore_color(qa_ui_window_definition *w, qa_vec4 v) {
  color_write(w->alloc, 112, v);
}
qa_vec4 qa_ui_window_back_color(qa_ui_window_definition *w) {
  return color_at(w->alloc, 128);
}
void qa_ui_window_set_back_color(qa_ui_window_definition *w, qa_vec4 v) {
  color_write(w->alloc, 128, v);
}
qa_vec4 qa_ui_window_border_color(qa_ui_window_definition *w) {
  return color_at(w->alloc, 144);
}
void qa_ui_window_set_border_color(qa_ui_window_definition *w, qa_vec4 v) {
  color_write(w->alloc, 144, v);
}
qa_vec4 qa_ui_window_outline_color(qa_ui_window_definition *w) {
  return color_at(w->alloc, 160);
}
void qa_ui_window_set_outline_color(qa_ui_window_definition *w, qa_vec4 v) {
  color_write(w->alloc, 160, v);
}
void qa_ui_window_set_fore_color_component(qa_ui_window_definition *w,
                                           int component, double value) {
  qa_ui_memory_allocation_set_float32(w->alloc, 112 + component * 4, value);
}
void qa_ui_window_set_back_color_component(qa_ui_window_definition *w,
                                           int component, double value) {
  qa_ui_memory_allocation_set_float32(w->alloc, 128 + component * 4, value);
}
void qa_ui_window_set_border_color_component(qa_ui_window_definition *w,
                                             int component, double value) {
  qa_ui_memory_allocation_set_float32(w->alloc, 144 + component * 4, value);
}
qa_ui_shader_reference *qa_ui_window_background(
    qa_ui_window_definition *w) {
  qa_ui_resource_reference *value =
      qa_ui_memory_allocation_get_resource(w->alloc, 176);
  if (value == NULL || value->kind == QA_UI_RESOURCE_SHADER) return value;
  qa_ui_script_plain_error(
      "UI window background aliases a different resource kind");
}
bool qa_ui_window_background_handle(qa_ui_window_definition *w,
                                    int32_t *out) {
  return qa_ui_memory_allocation_get_resource_handle(w->alloc, 176, out);
}
void qa_ui_window_set_background(qa_ui_window_definition *w,
                                 qa_ui_shader_reference *value,
                                 bool has_handle, int32_t handle) {
  qa_ui_memory_allocation_set_resource(w->alloc, 176, value, has_handle,
                                       handle);
}

/* ---- edit/list/multi/model views ---- */

double qa_ui_edit_minimum(qa_ui_edit_field_definition *e) {
  return qa_ui_memory_allocation_get_float32(e->alloc, 0);
}
double qa_ui_edit_maximum(qa_ui_edit_field_definition *e) {
  return qa_ui_memory_allocation_get_float32(e->alloc, 4);
}
double qa_ui_edit_default_value(qa_ui_edit_field_definition *e) {
  return qa_ui_memory_allocation_get_float32(e->alloc, 8);
}
double qa_ui_edit_range(qa_ui_edit_field_definition *e) {
  return qa_ui_memory_allocation_get_float32(e->alloc, 12);
}
int32_t qa_ui_edit_max_chars(qa_ui_edit_field_definition *e) {
  return qa_ui_memory_allocation_get_int32(e->alloc, 16);
}
int32_t qa_ui_edit_max_paint_chars(qa_ui_edit_field_definition *e) {
  return qa_ui_memory_allocation_get_int32(e->alloc, 20);
}
int32_t qa_ui_edit_paint_offset(qa_ui_edit_field_definition *e) {
  return qa_ui_memory_allocation_get_int32(e->alloc, 24);
}
void qa_ui_edit_set_paint_offset(qa_ui_edit_field_definition *e, int32_t v) {
  qa_ui_memory_allocation_set_int32(e->alloc, 24, v);
}

int32_t qa_ui_list_start_position(qa_ui_list_box_definition *l) {
  return qa_ui_memory_allocation_get_int32(l->alloc, 0);
}
void qa_ui_list_set_start_position(qa_ui_list_box_definition *l, int32_t v) {
  qa_ui_memory_allocation_set_int32(l->alloc, 0, v);
}
int32_t qa_ui_list_end_position(qa_ui_list_box_definition *l) {
  return qa_ui_memory_allocation_get_int32(l->alloc, 4);
}
void qa_ui_list_set_end_position(qa_ui_list_box_definition *l, int32_t v) {
  qa_ui_memory_allocation_set_int32(l->alloc, 4, v);
}
int32_t qa_ui_list_draw_padding(qa_ui_list_box_definition *l) {
  return qa_ui_memory_allocation_get_int32(l->alloc, 8);
}
void qa_ui_list_set_draw_padding(qa_ui_list_box_definition *l, int32_t v) {
  qa_ui_memory_allocation_set_int32(l->alloc, 8, v);
}
int32_t qa_ui_list_cursor_position(qa_ui_list_box_definition *l) {
  return qa_ui_memory_allocation_get_int32(l->alloc, 12);
}
void qa_ui_list_set_cursor_position(qa_ui_list_box_definition *l, int32_t v) {
  qa_ui_memory_allocation_set_int32(l->alloc, 12, v);
}
double qa_ui_list_element_width(qa_ui_list_box_definition *l) {
  return qa_ui_memory_allocation_get_float32(l->alloc, 16);
}
double qa_ui_list_element_height(qa_ui_list_box_definition *l) {
  return qa_ui_memory_allocation_get_float32(l->alloc, 20);
}
int32_t qa_ui_list_element_style(qa_ui_list_box_definition *l) {
  return qa_ui_memory_allocation_get_int32(l->alloc, 24);
}
size_t qa_ui_list_column_count(qa_ui_list_box_definition *l) {
  int32_t count = qa_ui_memory_allocation_get_int32(l->alloc, 28);
  if (count > QA_MAX_UI_LIST_COLUMNS)
    qa_range_error("listBoxDef_t columns exceed the source 16-slot array");
  return count < 0 ? 0 : (size_t)count;
}
qa_ui_list_column qa_ui_list_column_at(qa_ui_list_box_definition *l,
                                       size_t index) {
  int64_t offset = 32 + (int64_t)index * 12;
  qa_ui_list_column out = {qa_ui_memory_allocation_get_int32(l->alloc, offset),
                           qa_ui_memory_allocation_get_int32(l->alloc, offset + 4),
                           qa_ui_memory_allocation_get_int32(l->alloc, offset + 8)};
  return out;
}
qa_ui_menu_script *qa_ui_list_double_click(qa_ui_list_box_definition *l) {
  return qa_ui_memory_allocation_get_script(l->alloc, 224);
}
bool qa_ui_list_not_selectable(qa_ui_list_box_definition *l) {
  return qa_ui_memory_allocation_get_int32(l->alloc, 228) != 0;
}

int32_t qa_ui_multi_count(qa_ui_multi_definition *m) {
  return qa_ui_memory_allocation_get_int32(m->alloc, 384);
}
bool qa_ui_multi_string_definition(qa_ui_multi_definition *m) {
  return qa_ui_memory_allocation_get_int32(m->alloc, 388) != 0;
}
static int64_t multi_slot(double index) {
  if (index != trunc(index) || index < 0 || index >= 32)
    qa_range_error("multiDef_t index exceeds the source 32-slot arrays");
  return (int64_t)index * 4;
}
char *qa_ui_multi_label(qa_ui_multi_definition *m, double index) {
  return qa_ui_memory_allocation_get_string(m->alloc, multi_slot(index));
}
char *qa_ui_multi_string_value(qa_ui_multi_definition *m, double index) {
  return qa_ui_memory_allocation_get_string(m->alloc, 128 + multi_slot(index));
}
double qa_ui_multi_number_value(qa_ui_multi_definition *m, double index) {
  return qa_ui_memory_allocation_get_float32(m->alloc, 256 + multi_slot(index));
}

int32_t qa_ui_model_angle(qa_ui_model_definition *m) {
  return qa_ui_memory_allocation_get_int32(m->alloc, 0);
}
void qa_ui_model_set_angle(qa_ui_model_definition *m, int32_t v) {
  qa_ui_memory_allocation_set_int32(m->alloc, 0, v);
}
qa_vec3 qa_ui_model_origin(qa_ui_model_definition *m) {
  qa_vec3 out = {qa_ui_memory_allocation_get_float32(m->alloc, 4),
                 qa_ui_memory_allocation_get_float32(m->alloc, 8),
                 qa_ui_memory_allocation_get_float32(m->alloc, 12)};
  return out;
}
double qa_ui_model_field_of_view_x(qa_ui_model_definition *m) {
  return qa_ui_memory_allocation_get_float32(m->alloc, 16);
}
double qa_ui_model_field_of_view_y(qa_ui_model_definition *m) {
  return qa_ui_memory_allocation_get_float32(m->alloc, 20);
}
int32_t qa_ui_model_rotation_speed(qa_ui_model_definition *m) {
  return qa_ui_memory_allocation_get_int32(m->alloc, 24);
}

/* ---- UiItemDefinition ---- */

static qa_ui_item_definition *item_create(alloc_t *alloc,
                                          const qa_ui_script_location *at,
                                          bool has_offset, double offset) {
  qa_ui_item_definition *item = calloc(1, sizeof *item);
  if (item == NULL) return NULL;
  item->alloc = alloc;
  item->window.alloc = qa_ui_memory_allocation_subrecord(alloc, 0, 180);
  item->location = loc_copy(at);
  item->has_allocation_offset = has_offset;
  item->allocation_offset = offset;
  return item;
}

static void item_initialize(qa_ui_item_definition *item) {
  qa_ui_memory_allocation_clear(item->alloc);
  qa_ui_memory_allocation_set_float32(item->alloc, 216, 0.55);
  window_initialize(&item->window);
}

void qa_ui_item_definition_destroy(qa_ui_item_definition *item) {
  if (item == NULL) return;
  qa_ui_memory_allocation_destroy(item->edit_alloc);
  qa_ui_memory_allocation_destroy(item->list_alloc);
  qa_ui_memory_allocation_destroy(item->model_alloc);
  qa_ui_memory_allocation_destroy(item->multi_alloc);
  qa_ui_memory_allocation_destroy(item->window.alloc);
  qa_ui_memory_allocation_destroy(item->alloc);
  qa_ui_script_location_destroy(&item->location);
  free(item);
}

int32_t qa_ui_item_type(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_int32(i->alloc, 196);
}
qa_ui_menu_definition *qa_ui_item_parent(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_menu(i->alloc, 228);
}
void qa_ui_item_set_parent(qa_ui_item_definition *i,
                           qa_ui_menu_definition *parent) {
  qa_ui_memory_allocation_set_menu(i->alloc, 228, parent);
}
qa_rect qa_ui_item_text_rect(qa_ui_item_definition *i) {
  return rect_at(i->alloc, 180);
}
void qa_ui_item_set_text_rect(qa_ui_item_definition *i, qa_rect v) {
  rect_write(i->alloc, 180, v);
}
void qa_ui_item_set_text_rect_component(qa_ui_item_definition *i,
                                        int component, double value) {
  qa_ui_memory_allocation_set_float32(i->alloc, 180 + component * 4, value);
}
qa_ui_item_behavior_kind qa_ui_item_behavior(qa_ui_item_definition *i) {
  switch (qa_ui_item_type(i)) {
    case QA_UI_ITEM_TEXT: return QA_UI_BEHAVIOR_TEXT;
    case QA_UI_ITEM_BUTTON: return QA_UI_BEHAVIOR_BUTTON;
    case QA_UI_ITEM_RADIO_BUTTON: return QA_UI_BEHAVIOR_RADIO_BUTTON;
    case QA_UI_ITEM_CHECK_BOX: return QA_UI_BEHAVIOR_CHECK_BOX;
    case QA_UI_ITEM_EDIT_FIELD: return QA_UI_BEHAVIOR_EDIT_FIELD;
    case QA_UI_ITEM_COMBO: return QA_UI_BEHAVIOR_COMBO;
    case QA_UI_ITEM_LIST_BOX: return QA_UI_BEHAVIOR_LIST_BOX;
    case QA_UI_ITEM_MODEL: return QA_UI_BEHAVIOR_MODEL;
    case QA_UI_ITEM_OWNER_DRAW: return QA_UI_BEHAVIOR_OWNER_DRAW;
    case QA_UI_ITEM_NUMERIC_FIELD: return QA_UI_BEHAVIOR_NUMERIC_FIELD;
    case QA_UI_ITEM_SLIDER: return QA_UI_BEHAVIOR_SLIDER;
    case QA_UI_ITEM_YES_NO: return QA_UI_BEHAVIOR_YES_NO;
    case QA_UI_ITEM_MULTI: return QA_UI_BEHAVIOR_MULTI;
    case QA_UI_ITEM_BIND: return QA_UI_BEHAVIOR_BIND;
    default: return QA_UI_BEHAVIOR_UNKNOWN;
  }
}
int32_t qa_ui_item_alignment(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_int32(i->alloc, 200);
}
int32_t qa_ui_item_text_alignment(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_int32(i->alloc, 204);
}
double qa_ui_item_text_align_x(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_float32(i->alloc, 208);
}
double qa_ui_item_text_align_y(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_float32(i->alloc, 212);
}
double qa_ui_item_text_scale(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_float32(i->alloc, 216);
}
int32_t qa_ui_item_text_style(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_int32(i->alloc, 220);
}
char *qa_ui_item_text(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_string(i->alloc, 224);
}
char *qa_ui_item_cvar(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_string(i->alloc, 264);
}
char *qa_ui_item_cvar_test(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_string(i->alloc, 268);
}
qa_ui_resource_reference *qa_ui_item_asset(qa_ui_item_definition *i) {
  qa_ui_resource_reference *value =
      qa_ui_memory_allocation_get_resource(i->alloc, 232);
  if (value == NULL || value->kind == QA_UI_RESOURCE_SHADER ||
      value->kind == QA_UI_RESOURCE_MODEL)
    return value;
  qa_ui_script_plain_error("UI item asset aliases a sound handle");
}
bool qa_ui_item_asset_handle(qa_ui_item_definition *i, int32_t *out) {
  return qa_ui_memory_allocation_get_resource_handle(i->alloc, 232, out);
}
qa_ui_menu_script *qa_ui_item_mouse_enter_text(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_script(i->alloc, 236);
}
qa_ui_menu_script *qa_ui_item_mouse_exit_text(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_script(i->alloc, 240);
}
qa_ui_menu_script *qa_ui_item_mouse_enter(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_script(i->alloc, 244);
}
qa_ui_menu_script *qa_ui_item_mouse_exit(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_script(i->alloc, 248);
}
qa_ui_menu_script *qa_ui_item_action(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_script(i->alloc, 252);
}
qa_ui_menu_script *qa_ui_item_on_focus(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_script(i->alloc, 256);
}
qa_ui_menu_script *qa_ui_item_leave_focus(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_script(i->alloc, 260);
}
qa_ui_menu_script *qa_ui_item_cvar_script(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_script(i->alloc, 272);
}
int32_t qa_ui_item_cvar_flags(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_int32(i->alloc, 276);
}
bool qa_ui_item_cvar_rule(qa_ui_item_definition *i,
                          qa_ui_cvar_rule_kind *out_kind,
                          qa_ui_menu_script **out_script) {
  int32_t flags = qa_ui_item_cvar_flags(i);
  if ((flags & 15) == 0) return false;
  *out_script = qa_ui_item_cvar_script(i);
  if ((flags & 1) != 0) *out_kind = QA_UI_CVAR_RULE_ENABLE;
  else if ((flags & 2) != 0) *out_kind = QA_UI_CVAR_RULE_DISABLE;
  else if ((flags & 4) != 0) *out_kind = QA_UI_CVAR_RULE_SHOW;
  else if ((flags & 8) != 0) *out_kind = QA_UI_CVAR_RULE_HIDE;
  else return false;
  return true;
}
qa_ui_sound_reference *qa_ui_item_focus_sound(qa_ui_item_definition *i) {
  qa_ui_resource_reference *value =
      qa_ui_memory_allocation_get_resource(i->alloc, 280);
  if (value == NULL || value->kind == QA_UI_RESOURCE_SOUND) return value;
  qa_ui_script_plain_error(
      "UI focus sound aliases a different resource kind");
}
bool qa_ui_item_focus_sound_handle(qa_ui_item_definition *i, int32_t *out) {
  return qa_ui_memory_allocation_get_resource_handle(i->alloc, 280, out);
}
size_t qa_ui_item_color_range_count(qa_ui_item_definition *i) {
  int32_t count = qa_ui_memory_allocation_get_int32(i->alloc, 284);
  if (count > QA_MAX_UI_COLOR_RANGES)
    qa_range_error("UI color ranges exceed the source array");
  return count < 0 ? 0 : (size_t)count;
}
qa_ui_color_range qa_ui_item_color_range_at(qa_ui_item_definition *i,
                                            size_t index) {
  int64_t offset = 288 + (int64_t)index * 24;
  qa_ui_color_range out;
  out.color = color_at(i->alloc, offset);
  out.low = qa_ui_memory_allocation_get_float32(i->alloc, offset + 16);
  out.high = qa_ui_memory_allocation_get_float32(i->alloc, offset + 20);
  return out;
}
double qa_ui_item_special(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_float32(i->alloc, 528);
}
void qa_ui_item_set_special(qa_ui_item_definition *i, double v) {
  qa_ui_memory_allocation_set_float32(i->alloc, 528, v);
}
int32_t qa_ui_item_cursor_position(qa_ui_item_definition *i) {
  return qa_ui_memory_allocation_get_int32(i->alloc, 532);
}
void qa_ui_item_set_cursor_position(qa_ui_item_definition *i, int32_t v) {
  qa_ui_memory_allocation_set_int32(i->alloc, 532, v);
}

static alloc_t *item_type_data(qa_ui_item_definition *item) {
  return qa_ui_memory_allocation_get_allocation_pointer(item->alloc, 536);
}

static bool view_for(qa_ui_item_definition *item, alloc_t **source_slot,
                     alloc_t **view_slot, int64_t size, alloc_t **out) {
  alloc_t *data = item_type_data(item);
  if (data == NULL) return false;
  if (*source_slot != data) {
    qa_ui_memory_allocation_destroy(*view_slot);
    *view_slot = qa_ui_memory_allocation_dereference(data, size);
    *source_slot = data;
  }
  *out = *view_slot;
  return true;
}

bool qa_ui_item_edit_data(qa_ui_item_definition *item,
                          qa_ui_edit_field_definition *out) {
  return view_for(item, &item->edit_source, &item->edit_alloc, 28,
                  &out->alloc);
}
bool qa_ui_item_list_data(qa_ui_item_definition *item,
                          qa_ui_list_box_definition *out) {
  return view_for(item, &item->list_source, &item->list_alloc, 232,
                  &out->alloc);
}
bool qa_ui_item_model_data(qa_ui_item_definition *item,
                           qa_ui_model_definition *out) {
  return view_for(item, &item->model_source, &item->model_alloc, 28,
                  &out->alloc);
}
bool qa_ui_item_multi_data(qa_ui_item_definition *item,
                           qa_ui_multi_definition *out) {
  return view_for(item, &item->multi_source, &item->multi_alloc, 392,
                  &out->alloc);
}
qa_ui_edit_field_definition qa_ui_item_required_edit_data(
    qa_ui_item_definition *item) {
  qa_ui_edit_field_definition out;
  if (!qa_ui_item_edit_data(item, &out))
    qa_ui_script_plain_error("UI edit operation dereferences NULL typeData");
  return out;
}
qa_ui_list_box_definition qa_ui_item_required_list_data(
    qa_ui_item_definition *item) {
  qa_ui_list_box_definition out;
  if (!qa_ui_item_list_data(item, &out))
    qa_ui_script_plain_error("UI list operation dereferences NULL typeData");
  return out;
}

/* ---- UiMenuDefinition ---- */

static qa_ui_menu_definition *menu_create(alloc_t *alloc,
                                          const qa_ui_script_location *at) {
  qa_ui_menu_definition *menu = calloc(1, sizeof *menu);
  if (menu == NULL) return NULL;
  menu->alloc = alloc;
  menu->window.alloc = qa_ui_memory_allocation_subrecord(alloc, 0, 180);
  menu->location = loc_copy(at);
  return menu;
}

void qa_ui_menu_definition_destroy(qa_ui_menu_definition *menu) {
  if (menu == NULL) return;
  qa_ui_memory_allocation_destroy(menu->window.alloc);
  qa_ui_memory_allocation_destroy(menu->alloc);
  qa_ui_script_location_destroy(&menu->location);
  free(menu);
}

double qa_ui_menu_source_index(qa_ui_menu_definition *m) {
  return (double)qa_ui_memory_allocation_offset(m->alloc) / 644;
}
char *qa_ui_menu_font(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_string(m->alloc, 180);
}
int32_t qa_ui_menu_full_screen(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_int32(m->alloc, 184);
}
int32_t qa_ui_menu_cursor_item(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_int32(m->alloc, 196);
}
void qa_ui_menu_set_cursor_item(qa_ui_menu_definition *m, int32_t v) {
  qa_ui_memory_allocation_set_int32(m->alloc, 196, v);
}
int32_t qa_ui_menu_font_index(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_int32(m->alloc, 192);
}
int32_t qa_ui_menu_fade_cycle(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_int32(m->alloc, 200);
}
double qa_ui_menu_fade_clamp(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_float32(m->alloc, 204);
}
double qa_ui_menu_fade_amount(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_float32(m->alloc, 208);
}
qa_ui_menu_script *qa_ui_menu_on_open(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_script(m->alloc, 212);
}
qa_ui_menu_script *qa_ui_menu_on_close(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_script(m->alloc, 216);
}
qa_ui_menu_script *qa_ui_menu_on_escape(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_script(m->alloc, 220);
}
char *qa_ui_menu_sound_loop_path(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_string(m->alloc, 224);
}
qa_vec4 qa_ui_menu_focus_color(qa_ui_menu_definition *m) {
  return color_at(m->alloc, 228);
}
qa_vec4 qa_ui_menu_disable_color(qa_ui_menu_definition *m) {
  return color_at(m->alloc, 244);
}
int32_t qa_ui_menu_item_count(qa_ui_menu_definition *m) {
  return qa_ui_memory_allocation_get_int32(m->alloc, 188);
}
qa_ui_item_definition *qa_ui_menu_item_at(qa_ui_menu_definition *m,
                                          double index) {
  if (index != trunc(index) || index < 0 || index >= QA_MAX_UI_MENU_ITEMS)
    qa_range_error("UI menu item slot exceeds the source array");
  return qa_ui_memory_allocation_get_item(m->alloc,
                                           260 + (int64_t)index * 4);
}

static void menu_set_item(qa_ui_menu_definition *menu, double index,
                          qa_ui_item_definition *item) {
  if (index != trunc(index) || index < 0 || index >= QA_MAX_UI_MENU_ITEMS)
    qa_range_error("UI menu item slot exceeds the source array");
  qa_ui_memory_allocation_set_item(menu->alloc, 260 + (int64_t)index * 4,
                                   item);
}

static void menu_initialize(qa_ui_menu_definition *menu,
                            const qa_ui_global_assets *assets) {
  qa_ui_memory_allocation_clear(menu->alloc);
  qa_ui_memory_allocation_set_int32(menu->alloc, 196, -1);
  qa_ui_memory_allocation_set_float32(menu->alloc, 208, assets->fade_amount);
  qa_ui_memory_allocation_set_float32(menu->alloc, 204, assets->fade_clamp);
  qa_ui_memory_allocation_set_int32(menu->alloc, 200,
                                    (int32_t)assets->fade_cycle);
  window_initialize(&menu->window);
}

/* TS items getter: NULL-free list up to itemCount (aborts on NULL). */
static void menu_check_items(qa_ui_menu_definition *menu) {
  int32_t count = qa_ui_menu_item_count(menu);
  if (count > QA_MAX_UI_MENU_ITEMS)
    qa_range_error("UI menu item count exceeds the source array");
  for (int32_t i = 0; i < count; i++) {
    if (qa_ui_memory_allocation_get_item(menu->alloc, 260 + i * 4) == NULL)
      qa_ui_script_plain_error("UI menu dereferences a NULL item pointer");
  }
}

/* ---- assets / definitions ---- */

static qa_ui_global_assets new_assets(const qa_ui_global_assets *initial) {
  qa_ui_global_assets out;
  memset(&out, 0, sizeof out);
  if (initial != NULL) {
    out = *initial;
    out.shadow_color = color4(initial->shadow_color.x,
                              initial->shadow_color.y,
                              initial->shadow_color.z,
                              initial->shadow_color.w);
  }
  return out;
}

static qa_ui_global_assets freeze_assets(const qa_ui_global_assets *assets) {
  qa_ui_global_assets out = *assets;
  out.shadow_color = color4(assets->shadow_color.x, assets->shadow_color.y,
                            assets->shadow_color.z, assets->shadow_color.w);
  return out;
}

/* Everything one parse created, whatever pool slots name it later: QVM-backed
 * menu records are reused by each reload, so their item slots cannot identify
 * the generation that owns an item. */
typedef enum owned_kind {
  OWNED_ITEM, OWNED_MENU, OWNED_SCRIPT, OWNED_REFERENCE, OWNED_ALLOCATION
} owned_kind;
typedef struct owned_entry {
  owned_kind kind;
  void *value;
} owned_entry;
struct qa_ui_menu_ownership {
  owned_entry *entries;
  size_t count, capacity;
  qa_ui_team_arena_memory *storage; /* the parser's own unaccounted pool */
};

static void ownership_add(qa_ui_menu_ownership **slot, owned_kind kind,
                          void *value) {
  if (value == NULL) return;
  if (*slot == NULL) *slot = calloc(1, sizeof **slot);
  if (*slot == NULL) qa_range_error("UI memory allocation failed");
  qa_ui_menu_ownership *ownership = *slot;
  if (ownership->count == ownership->capacity) {
    size_t capacity = ownership->capacity == 0 ? 64 : ownership->capacity * 2;
    owned_entry *grown = realloc(ownership->entries, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("UI memory allocation failed");
    ownership->entries = grown;
    ownership->capacity = capacity;
  }
  ownership->entries[ownership->count].kind = kind;
  ownership->entries[ownership->count].value = value;
  ownership->count++;
}

static void ownership_destroy(qa_ui_menu_ownership *ownership) {
  if (ownership == NULL) return;
  for (size_t i = ownership->count; i-- > 0;) {
    void *value = ownership->entries[i].value;
    switch (ownership->entries[i].kind) {
      case OWNED_ITEM: qa_ui_item_definition_destroy(value); break;
      case OWNED_MENU: qa_ui_menu_definition_destroy(value); break;
      case OWNED_SCRIPT: qa_ui_menu_script_destroy(value); break;
      case OWNED_REFERENCE: qa_ui_resource_reference_destroy(value); break;
      case OWNED_ALLOCATION: qa_ui_memory_allocation_destroy(value); break;
    }
  }
  free(ownership->entries);
  if (ownership->storage != NULL)
    qa_ui_team_arena_memory_destroy(ownership->storage);
  free(ownership);
}

void qa_ui_menu_definitions_destroy(qa_ui_menu_definitions *definitions) {
  if (definitions == NULL) return;
  free(definitions->menus);
  for (size_t i = 0; i < definitions->loaded_file_count; i++)
    free(definitions->loaded_files[i]);
  free(definitions->loaded_files);
  for (size_t i = 0; i < definitions->diagnostic_count; i++)
    qa_ui_script_diagnostic_destroy(&definitions->diagnostics[i]);
  free(definitions->diagnostics);
  for (size_t i = 0; i < definitions->registration_event_count; i++)
    qa_ui_script_location_destroy(
        &definitions->registration_events[i].location);
  free(definitions->registration_events);
  ownership_destroy(definitions->ownership);
  memset(definitions, 0, sizeof *definitions);
}

qa_ui_menu_load_plan qa_ui_default_hud_menu_plan(void) {
  qa_ui_menu_load_plan plan = {"ui/hud.txt"};
  return plan;
}

/* ---- UiMenuTokenCursor ---- */

struct qa_ui_menu_token_cursor {
  qa_ui_menu_token_source source;
  uint32_t last_int;
  double last_float;
  qa_ui_script_source_reader *reader;
  qa_ui_menu_script_sources_fn sources_fn;
  void *sources_ctx;
  qa_ui_menu_assert_current_fn current_fn;
  void *current_ctx;
  double handle;
  char *handle_path;
};

qa_ui_menu_token_cursor *qa_ui_menu_token_cursor_create(
    const qa_ui_menu_token_source *source) {
  qa_ui_menu_token_cursor *cursor = calloc(1, sizeof *cursor);
  if (cursor == NULL) return NULL;
  cursor->source = *source;
  return cursor;
}

static bool reader_next(void *ctx, qa_ui_script_token_record *out) {
  qa_ui_menu_token_cursor *cursor = ctx;
  return qa_ui_script_source_reader_next(cursor->reader, out);
}
static void reader_position(void *ctx, char *filename, size_t capacity,
                            int64_t *out_line) {
  qa_ui_menu_token_cursor *cursor = ctx;
  qa_ui_script_source_reader_position(cursor->reader, filename, capacity,
                                      out_line);
}
static void reader_dispose(void *ctx) {
  qa_ui_menu_token_cursor *cursor = ctx;
  qa_ui_script_source_reader_dispose(cursor->reader);
}

qa_ui_menu_token_cursor *qa_ui_menu_token_cursor_open(
    const qa_ui_script_source *source, qa_ui_script_resolve_fn resolve,
    void *resolve_ctx, const qa_ui_script_preprocessor_options *options) {
  qa_ui_menu_token_cursor *cursor = calloc(1, sizeof *cursor);
  if (cursor == NULL) return NULL;
  cursor->reader = qa_ui_script_source_reader_open(source, resolve,
                                                   resolve_ctx, options);
  if (cursor->reader == NULL) {
    free(cursor);
    return NULL;
  }
  cursor->source.ctx = cursor;
  cursor->source.path = source->path;
  cursor->source.next = reader_next;
  cursor->source.position = reader_position;
  cursor->source.dispose = reader_dispose;
  return cursor;
}

static bool handle_next(void *ctx, qa_ui_script_token_record *out) {
  qa_ui_menu_token_cursor *cursor = ctx;
  cursor->current_fn(cursor->current_ctx);
  qa_ui_menu_script_sources sources = cursor->sources_fn(cursor->sources_ctx);
  bool found = sources.read_token_handle(sources.ctx, cursor->handle, out);
  cursor->current_fn(cursor->current_ctx);
  return found;
}
static void handle_position(void *ctx, char *filename, size_t capacity,
                            int64_t *out_line) {
  qa_ui_menu_token_cursor *cursor = ctx;
  cursor->current_fn(cursor->current_ctx);
  qa_ui_menu_script_sources sources = cursor->sources_fn(cursor->sources_ctx);
  bool found = sources.source_file_and_line(sources.ctx, cursor->handle,
                                            filename, capacity, out_line);
  cursor->current_fn(cursor->current_ctx);
  if (!found) {
    if (capacity > 0) filename[0] = '\0';
    *out_line = 0;
  }
}
static void handle_dispose(void *ctx) {
  qa_ui_menu_token_cursor *cursor = ctx;
  cursor->current_fn(cursor->current_ctx);
  qa_ui_menu_script_sources sources = cursor->sources_fn(cursor->sources_ctx);
  (void)sources.free_source_handle(sources.ctx, cursor->handle);
  cursor->current_fn(cursor->current_ctx);
}

qa_ui_menu_token_cursor *qa_ui_menu_token_cursor_open_handle(
    const char *path, qa_ui_menu_script_sources_fn sources,
    void *sources_ctx, qa_ui_menu_assert_current_fn current,
    void *current_ctx) {
  current(current_ctx);
  qa_ui_menu_script_sources table = sources(sources_ctx);
  double handle = table.load_source_handle(table.ctx, path);
  current(current_ctx);
  if (handle == 0) return NULL;
  qa_ui_menu_token_cursor *cursor = calloc(1, sizeof *cursor);
  if (cursor == NULL) return NULL;
  cursor->sources_fn = sources;
  cursor->sources_ctx = sources_ctx;
  cursor->current_fn = current;
  cursor->current_ctx = current_ctx;
  cursor->handle = handle;
  cursor->handle_path = dupstr(path);
  cursor->source.ctx = cursor;
  cursor->source.path = cursor->handle_path;
  cursor->source.next = handle_next;
  cursor->source.position = handle_position;
  cursor->source.dispose = handle_dispose;
  return cursor;
}

void qa_ui_menu_token_cursor_destroy(qa_ui_menu_token_cursor *cursor) {
  if (cursor == NULL) return;
  if (cursor->reader != NULL) qa_ui_script_source_reader_destroy(cursor->reader);
  free(cursor->handle_path);
  free(cursor);
}

const char *qa_ui_menu_token_cursor_path(qa_ui_menu_token_cursor *cursor) {
  return cursor->source.path;
}

void qa_ui_menu_token_cursor_position(qa_ui_menu_token_cursor *cursor,
                                      char *filename, size_t capacity,
                                      int64_t *out_line) {
  cursor->source.position(cursor->source.ctx, filename, capacity, out_line);
}

bool qa_ui_menu_token_cursor_next(qa_ui_menu_token_cursor *cursor,
                                  qa_ui_script_token **out) {
  qa_ui_script_token_record record;
  if (!cursor->source.next(cursor->source.ctx, &record)) return false;
  cursor->last_int = record.integer_value;
  cursor->last_float = record.float_value;
  *out = record.token;
  return true;
}

void qa_ui_menu_token_cursor_dispose(qa_ui_menu_token_cursor *cursor) {
  cursor->source.dispose(cursor->source.ctx);
}

/* ---- UiMenuSourceParser ---- */

typedef struct parser_catch {
  jmp_buf buf;
  qa_ui_menu_token_cursor *cursor;
  struct parser_catch *prev;
} parser_catch;

struct qa_ui_menu_source_parser {
  qa_ui_menu_memory_ownership memory;
  qa_ui_team_arena_memory *storage;
  bool own_storage;
  qa_ui_menu_parse_host host;
  qa_ui_script_preprocessor_options preprocessor_options;
  qa_ui_menu_parse_options parse_options;
  qa_ui_menu_definition **menus;
  size_t menu_count, menu_capacity;
  char **loaded_files;
  size_t loaded_count, loaded_capacity;
  qa_ui_script_diagnostic *reported;
  size_t reported_count, reported_capacity;
  qa_ui_global_assets assets;
  qa_ui_menu_registration_event *registrations;
  size_t registration_count, registration_capacity;
  bool font_registered;
  qa_ui_script_fail_env fail_env;
  parser_catch *catch_top;
  qa_ui_menu_ownership *ownership; /* moves to the definitions on load */
  void **bridges; /* report bridges of opened text sources, freed with the parser */
  size_t bridge_count, bridge_capacity;
};

typedef qa_ui_menu_source_parser parser_t;
typedef qa_ui_menu_token_cursor cursor_t;

/** Parser token: token reference + malloc'd tokenValue text. */
typedef struct ptok {
  qa_ui_script_token *token;
  char *value;
} ptok;

static void ptok_release(ptok *tok) {
  qa_ui_script_token_release(tok->token);
  free(tok->value);
  tok->token = NULL;
  tok->value = NULL;
}

static char *dup_cstr(const char *bytes, size_t length) {
  size_t end = 0;
  while (end < length && bytes[end] != '\0') end++;
  char *out = malloc(end + 1);
  if (out == NULL) return NULL;
  memcpy(out, bytes, end);
  out[end] = '\0';
  return out;
}

static bool cursor_next_ptok(cursor_t *cursor, ptok *out) {
  qa_ui_script_token *token = NULL;
  if (!qa_ui_menu_token_cursor_next(cursor, &token)) return false;
  out->token = token;
  out->value = token->kind == QA_UI_SCRIPT_KIND_STRING
                   ? dup_cstr(token->value, token->value_length)
                   : dup_cstr(token->text, token->text_length);
  return true;
}

/* TS text.toLowerCase() (Unicode). */
static char *lower_dup(const char *text) { return qa_js_to_lower_case(text); }

static bool starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

_Noreturn static void type_error(const char *message) {
  fprintf(stderr, "TypeError: %s\n", message);
  abort();
}

static void report_diagnostic(parser_t *parser,
                              const qa_ui_script_diagnostic *diagnostic) {
  if (parser->reported_count == parser->reported_capacity) {
    size_t capacity = parser->reported_capacity == 0
                          ? 8
                          : parser->reported_capacity * 2;
    qa_ui_script_diagnostic *grown =
        realloc(parser->reported, capacity * sizeof *grown);
    if (grown == NULL) return;
    parser->reported = grown;
    parser->reported_capacity = capacity;
  }
  qa_ui_script_diagnostic *slot =
      &parser->reported[parser->reported_count++];
  slot->severity = diagnostic->severity;
  slot->message = dupstr(diagnostic->message);
  slot->location = loc_copy(&diagnostic->location);
  if (parser->parse_options.report_diagnostic != NULL)
    parser->parse_options.report_diagnostic(
        parser->parse_options.report_diagnostic_ctx, slot);
}

static void warn(parser_t *parser, const char *message,
                 const qa_ui_script_location *at) {
  qa_ui_script_diagnostic diagnostic;
  diagnostic.severity = QA_UI_SCRIPT_SEVERITY_WARNING;
  diagnostic.message = (char *)message;
  diagnostic.location = *at;
  report_diagnostic(parser, &diagnostic);
}

static void warn_fmt(parser_t *parser, const qa_ui_script_location *at,
                     const char *prefix, const char *suffix) {
  size_t length = strlen(prefix) + strlen(suffix) + 1;
  char *message = malloc(length);
  if (message == NULL) return;
  snprintf(message, length, "%s%s", prefix, suffix);
  warn(parser, message, at);
  free(message);
}

static void source_error(parser_t *parser, const char *message,
                         const qa_ui_script_location *at, cursor_t *cursor) {
  char filename[1024];
  int64_t line = 0;
  qa_ui_menu_token_cursor_position(cursor, filename, sizeof filename, &line);
  qa_ui_script_location location = loc_make(filename, line, at->column);
  qa_ui_script_diagnostic diagnostic;
  diagnostic.severity = QA_UI_SCRIPT_SEVERITY_ERROR;
  diagnostic.message = (char *)message;
  diagnostic.location = location;
  report_diagnostic(parser, &diagnostic);
  qa_ui_script_location_destroy(&location);
}

static void source_error_fmt(parser_t *parser, const char *prefix,
                             const char *suffix,
                             const qa_ui_script_location *at,
                             cursor_t *cursor) {
  size_t length = strlen(prefix) + strlen(suffix) + 1;
  char *message = malloc(length);
  if (message == NULL) return;
  snprintf(message, length, "%s%s", prefix, suffix);
  source_error(parser, message, at, cursor);
  free(message);
}

_Noreturn static void parser_fail(parser_t *parser, const char *message,
                                  const qa_ui_script_location *at) {
  qa_ui_script_diagnostic diagnostic;
  diagnostic.severity = QA_UI_SCRIPT_SEVERITY_ERROR;
  diagnostic.message = dupstr(message);
  diagnostic.location = loc_copy(at);
  report_diagnostic(parser, &diagnostic);
  qa_ui_script_fail_throw(&parser->fail_env, &diagnostic, NULL);
}

_Noreturn static void parser_fail_fmt(parser_t *parser, const char *prefix,
                                      const char *suffix,
                                      const qa_ui_script_location *at) {
  size_t length = strlen(prefix) + strlen(suffix) + 1;
  char *message = malloc(length);
  if (message != NULL) snprintf(message, length, "%s%s", prefix, suffix);
  parser_fail(parser, message != NULL ? message : prefix, at);
}

/* TS throw new MenuParseFailure(cursor, message). */
_Noreturn static void parse_failure(parser_t *parser) {
  longjmp(parser->catch_top->buf, 1);
}

/* Usage: PARSER_TRY(p, cur, c) { body; PARSER_TRY_END(p, c); }
 *        PARSER_CATCH(p, c) { handler } */
#define PARSER_TRY(parser, cur, catch_var)                                \
  parser_catch catch_var;                                                 \
  catch_var.cursor = (cur);                                               \
  catch_var.prev = (parser)->catch_top;                                   \
  (parser)->catch_top = &catch_var;                                       \
  if (setjmp(catch_var.buf) == 0)

#define PARSER_TRY_END(parser, catch_var)                                 \
  (parser)->catch_top = catch_var.prev

#define PARSER_CATCH(parser, catch_var)                                   \
  else for ((parser)->catch_top = catch_var.prev; ; (void)0)

static void push_loaded(parser_t *parser, const char *path) {
  if (parser->loaded_count == parser->loaded_capacity) {
    size_t capacity =
        parser->loaded_capacity == 0 ? 8 : parser->loaded_capacity * 2;
    char **grown = realloc(parser->loaded_files, capacity * sizeof *grown);
    if (grown == NULL) return;
    parser->loaded_files = grown;
    parser->loaded_capacity = capacity;
  }
  parser->loaded_files[parser->loaded_count++] = dupstr(path);
}

static void push_menu(parser_t *parser, qa_ui_menu_definition *menu) {
  if (parser->menu_count == parser->menu_capacity) {
    size_t capacity =
        parser->menu_capacity == 0 ? 8 : parser->menu_capacity * 2;
    qa_ui_menu_definition **grown =
        realloc(parser->menus, capacity * sizeof *grown);
    if (grown == NULL) return;
    parser->menus = grown;
    parser->menu_capacity = capacity;
  }
  parser->menus[parser->menu_count++] = menu;
}

static void register_event(parser_t *parser, qa_ui_resource_kind kind,
                           qa_ui_resource_reference *reference,
                           const qa_ui_script_location *at, bool *has_handle,
                           double *handle) {
  *has_handle = false;
  *handle = 0;
  /* Every parsed reference is announced exactly once, here. */
  ownership_add(&parser->ownership, OWNED_REFERENCE, reference);
  qa_ui_menu_registration_event event;
  event.kind = kind;
  event.reference = reference;
  event.location = loc_copy(at);
  {
    if (parser->registration_count == parser->registration_capacity) {
      size_t capacity = parser->registration_capacity == 0
                            ? 16
                            : parser->registration_capacity * 2;
      qa_ui_menu_registration_event *grown =
          realloc(parser->registrations, capacity * sizeof *grown);
      if (grown != NULL) {
        parser->registrations = grown;
        parser->registration_capacity = capacity;
      }
    }
    if (parser->registration_count < parser->registration_capacity)
      parser->registrations[parser->registration_count++] = event;
    else
      qa_ui_script_location_destroy(&event.location);
  }
  if (parser->parse_options.registration_sink != NULL) {
    qa_ui_menu_registration_event call = event;
    *has_handle = parser->parse_options.registration_sink(
        parser->parse_options.registration_sink_ctx, &call, handle);
  }
}

static void publish_asset(parser_t *parser,
                          qa_ui_menu_asset_publication publication) {
  if (parser->parse_options.asset_sink != NULL)
    parser->parse_options.asset_sink(parser->parse_options.asset_sink_ctx,
                                     &publication);
}

static alloc_t *parser_allocate(parser_t *parser, int64_t size) {
  int64_t offset = 0;
  if (!qa_ui_team_arena_memory_allocate(parser->storage, size, &offset))
    return NULL;
  return qa_ui_team_arena_memory_borrow(parser->storage, offset, size);
}

/* NULL is TS undefined; caller releases the reference. */
static qa_ui_string_reference *allocate_string_reference(parser_t *parser,
                                                         const char *text) {
  if (parser->memory.kind == QA_UI_MENU_MEMORY_UNACCOUNTED)
    return qa_ui_string_reference_literal(text);
  /* The pool result is borrowed; retain so both paths are caller-released. */
  return qa_ui_string_reference_retain(
      qa_ui_team_arena_memory_string_alloc_reference(parser->storage, text));
}

/* Malloc'd; NULL is TS undefined. */
static char *allocate_string(parser_t *parser, const char *text) {
  qa_ui_string_reference *reference = allocate_string_reference(parser, text);
  if (reference == NULL) return NULL;
  char *out = qa_ui_string_reference_read(reference);
  qa_ui_string_reference_release(reference);
  return out;
}

static ptok expect_any(parser_t *parser, cursor_t *cursor,
                       const char *message,
                       const qa_ui_script_location *at) {
  ptok tok;
  if (!cursor_next_ptok(cursor, &tok)) {
    if (strcmp(message, "end of file inside menu item") == 0)
      source_error(parser, message, at, cursor);
    parse_failure(parser);
  }
  return tok;
}

static void expect_token(parser_t *parser, cursor_t *cursor,
                         const char *expected, const char *message,
                         const qa_ui_script_location *at) {
  ptok tok = expect_any(parser, cursor, message, at);
  char *lower = lower_dup(tok.value);
  bool match = lower != NULL && strcmp(lower, expected) == 0;
  free(lower);
  ptok_release(&tok);
  if (!match) parse_failure(parser);
}

static void expect_first(parser_t *parser, cursor_t *cursor,
                         const char *expected, const char *message,
                         const qa_ui_script_location *at) {
  ptok tok = expect_any(parser, cursor, message, at);
  char *lower = lower_dup(tok.value);
  bool match = lower != NULL && starts_with(lower, expected);
  free(lower);
  ptok_release(&tok);
  if (!match) parse_failure(parser);
}

static qa_ui_string_reference *read_string_reference(
    parser_t *parser, cursor_t *cursor, const qa_ui_script_location *at) {
  ptok tok = expect_any(parser, cursor, "expected string", at);
  qa_ui_string_reference *reference =
      allocate_string_reference(parser, tok.value);
  ptok_release(&tok);
  return reference;
}

static void read_string_reference_into(parser_t *parser, cursor_t *cursor,
                                       const qa_ui_script_location *at,
                                       alloc_t *alloc, int64_t offset) {
  qa_ui_string_reference *reference =
      read_string_reference(parser, cursor, at);
  qa_ui_memory_allocation_set_string_reference(alloc, offset, reference);
  qa_ui_string_reference_release(reference);
}

/* Malloc'd; NULL is TS undefined. */
static char *read_string(parser_t *parser, cursor_t *cursor,
                         const qa_ui_script_location *at) {
  ptok tok = expect_any(parser, cursor, "expected string", at);
  char *out = allocate_string(parser, tok.value);
  ptok_release(&tok);
  return out;
}

static int32_t read_int(parser_t *parser, cursor_t *cursor,
                        const qa_ui_script_location *at) {
  ptok tok = expect_any(parser, cursor, "expected integer", at);
  bool negative = false;
  if (starts_with(tok.value, "-")) {
    negative = true;
    qa_ui_script_location location = loc_copy(&tok.token->location);
    ptok_release(&tok);
    tok = expect_any(parser, cursor, "expected integer after minus sign",
                     &location);
    qa_ui_script_location_destroy(&location);
  }
  if (tok.token->kind != QA_UI_SCRIPT_KIND_NUMBER) {
    source_error_fmt(parser, "expected integer but found ", tok.value,
                     &tok.token->location, cursor);
    ptok_release(&tok);
    parse_failure(parser);
  }
  int32_t stored = (int32_t)cursor->last_int;
  ptok_release(&tok);
  return negative ? (int32_t)(~(uint32_t)stored + 1u) : stored;
}

static double read_float(parser_t *parser, cursor_t *cursor,
                         const qa_ui_script_location *at) {
  ptok tok = expect_any(parser, cursor, "expected float", at);
  bool negative = false;
  if (starts_with(tok.value, "-")) {
    negative = true;
    qa_ui_script_location location = loc_copy(&tok.token->location);
    ptok_release(&tok);
    tok = expect_any(parser, cursor, "expected float after minus sign",
                     &location);
    qa_ui_script_location_destroy(&location);
  }
  if (tok.token->kind != QA_UI_SCRIPT_KIND_NUMBER) {
    source_error_fmt(parser, "expected float but found ", tok.value,
                     &tok.token->location, cursor);
    ptok_release(&tok);
    parse_failure(parser);
  }
  double stored = FR(cursor->last_float);
  ptok_release(&tok);
  return negative ? FR(-stored) : stored;
}

static void read_rect_into(parser_t *parser, alloc_t *alloc, int64_t offset,
                           cursor_t *cursor,
                           const qa_ui_script_location *at) {
  qa_rect value;
  value.x = read_float(parser, cursor, at);
  value.y = read_float(parser, cursor, at);
  value.width = read_float(parser, cursor, at);
  value.height = read_float(parser, cursor, at);
  rect_write(alloc, offset, value);
}

static void read_color_into(parser_t *parser, alloc_t *alloc, int64_t offset,
                            cursor_t *cursor,
                            const qa_ui_script_location *at) {
  qa_vec4 value;
  value.x = read_float(parser, cursor, at);
  value.y = read_float(parser, cursor, at);
  value.z = read_float(parser, cursor, at);
  value.w = read_float(parser, cursor, at);
  color_write(alloc, offset, value);
}

static void read_fore_color(parser_t *parser, qa_ui_window_definition *window,
                            cursor_t *cursor,
                            const qa_ui_script_location *at) {
  for (int component = 0; component < 4; component++) {
    double value = read_float(parser, cursor, at);
    qa_ui_memory_allocation_set_float32(window->alloc,
                                        112 + component * 4, value);
    int32_t flags = qa_ui_memory_allocation_get_int32(window->alloc, 68);
    qa_ui_memory_allocation_set_int32(window->alloc, 68,
                                      flags | QA_UI_WINDOW_FORE_COLOR_SET);
  }
}

static qa_vec4 read_color(parser_t *parser, cursor_t *cursor,
                          const qa_ui_script_location *at) {
  double x = read_float(parser, cursor, at);
  double y = read_float(parser, cursor, at);
  double z = read_float(parser, cursor, at);
  double w = read_float(parser, cursor, at);
  return color4(x, y, z, w);
}

static qa_ui_font_reference *read_font(parser_t *parser, cursor_t *cursor,
                                       const qa_ui_script_location *at) {
  char *path = read_string(parser, cursor, at);
  int32_t point_size = read_int(parser, cursor, at);
  qa_ui_font_reference *reference =
      qa_ui_font_reference_create(path, point_size);
  free(path);
  return reference;
}

static qa_ui_menu_script *read_script(parser_t *parser, cursor_t *cursor,
                                      const qa_ui_script_location *at) {
  expect_token(parser, cursor, "{", "expected { before script", at);
  char text[QA_MAX_UI_SCRIPT_BYTES + 1];
  size_t text_length = 0;
  text[0] = '\0';
  bool truncated = false;
  qa_ui_menu_script_token *tokens = NULL;
  size_t count = 0, capacity = 0;
  for (;;) {
    ptok tok = expect_any(parser, cursor, "end of file inside script", at);
    if (strcmp(tok.value, "}") == 0) {
      ptok_release(&tok);
      if (truncated) {
        char message[64];
        snprintf(message, sizeof message, "script truncated to %d bytes",
                 QA_MAX_UI_SCRIPT_BYTES);
        warn(parser, message, at);
      }
      qa_ui_string_reference *allocated =
          allocate_string_reference(parser, text);
      if (allocated == NULL) {
        for (size_t i = 0; i < count; i++)
          qa_ui_menu_script_token_destroy(&tokens[i]);
        free(tokens);
        return NULL;
      }
      qa_ui_menu_script *script = calloc(1, sizeof *script);
      if (script == NULL) {
        qa_ui_string_reference_release(allocated);
        for (size_t i = 0; i < count; i++)
          qa_ui_menu_script_token_destroy(&tokens[i]);
        free(tokens);
        return NULL;
      }
      script->text_ref = allocated;
      script->tokens = tokens;
      script->token_count = count;
      script->truncated = truncated;
      ownership_add(&parser->ownership, OWNED_SCRIPT, script);
      return script;
    }
    size_t value_length = strlen(tok.value);
    /* TS 8-bit check is inherent for byte tokens. */
    char *rendered = malloc(value_length + 3);
    size_t rendered_length;
    if (value_length > 1) {
      snprintf(rendered, value_length + 3, "\"%s\"", tok.value);
      rendered_length = value_length + 2;
    } else {
      memcpy(rendered, tok.value, value_length + 1);
      rendered_length = value_length;
    }
    size_t addition = rendered_length + 1;
    size_t remaining = QA_MAX_UI_SCRIPT_BYTES - text_length;
    if (remaining >= addition) {
      memcpy(text + text_length, rendered, rendered_length);
      text_length += rendered_length;
      text[text_length++] = ' ';
      text[text_length] = '\0';
      if (count == capacity) {
        size_t grown_capacity = capacity == 0 ? 8 : capacity * 2;
        qa_ui_menu_script_token *grown =
            realloc(tokens, grown_capacity * sizeof *grown);
        if (grown != NULL) {
          tokens = grown;
          capacity = grown_capacity;
        }
      }
      if (count < capacity) {
        tokens[count].text = dupstr(tok.value);
        tokens[count].location = loc_copy(&tok.token->location);
        count++;
      }
    } else {
      if (remaining > 0) {
        /* addition is rendered + ' '; slice it like the TS. */
        size_t take = remaining <= rendered_length ? remaining
                                                   : rendered_length;
        memcpy(text + text_length, rendered, take);
        text_length += take;
        if (remaining > rendered_length) text[text_length++] = ' ';
        text[text_length] = '\0';
      }
      truncated = true;
    }
    free(rendered);
    ptok_release(&tok);
  }
}

static bool is_edit_type(int32_t type) {
  return type == QA_UI_ITEM_TEXT || type == QA_UI_ITEM_EDIT_FIELD ||
         type == QA_UI_ITEM_NUMERIC_FIELD || type == QA_UI_ITEM_SLIDER ||
         type == QA_UI_ITEM_YES_NO || type == QA_UI_ITEM_BIND;
}

static void validate_type_data(parser_t *parser, qa_ui_item_definition *item) {
  if (!qa_ui_memory_allocation_is_null_pointer(item->alloc, 536)) return;
  int32_t type = qa_ui_item_type(item);
  int kind = type == QA_UI_ITEM_LIST_BOX    ? 1
             : type == QA_UI_ITEM_MODEL     ? 2
             : type == QA_UI_ITEM_MULTI     ? 3
             : is_edit_type(type)           ? 4
                                            : 0;
  if (kind == 0) return;
  alloc_t *allocated =
      parser_allocate(parser, kind == 1 ? 232 : kind == 3 ? 392 : 28);
  ownership_add(&parser->ownership, OWNED_ALLOCATION, allocated);
  qa_ui_memory_allocation_set_allocation_pointer(item->alloc, 536, allocated);
  if (kind == 4 || kind == 1) {
    if (allocated == NULL)
      parser_fail(parser,
                  "Item_ValidateTypeData memset dereferences a failed UI_Alloc",
                  &item->location);
    qa_ui_memory_allocation_clear(allocated);
    if (type == QA_UI_ITEM_EDIT_FIELD)
      qa_ui_memory_allocation_set_int32(allocated, 20, 256);
  }
}

static qa_ui_edit_field_definition require_edit(parser_t *parser,
                                                qa_ui_item_definition *item) {
  validate_type_data(parser, item);
  qa_ui_edit_field_definition edit;
  if (!qa_ui_item_edit_data(item, &edit)) parse_failure(parser);
  return edit;
}

static qa_ui_list_box_definition require_list(
    parser_t *parser, qa_ui_item_definition *item,
    const qa_ui_script_location *at) {
  validate_type_data(parser, item);
  qa_ui_list_box_definition list;
  if (!qa_ui_item_list_data(item, &list))
    parser_fail(parser, "list parser dereferences NULL typeData", at);
  return list;
}

static qa_ui_list_box_definition checked_list(parser_t *parser,
                                              qa_ui_item_definition *item) {
  validate_type_data(parser, item);
  qa_ui_list_box_definition list;
  if (!qa_ui_item_list_data(item, &list)) parse_failure(parser);
  return list;
}

static qa_ui_multi_definition require_multi(parser_t *parser,
                                            qa_ui_item_definition *item) {
  validate_type_data(parser, item);
  qa_ui_multi_definition multi;
  if (!qa_ui_item_multi_data(item, &multi)) parse_failure(parser);
  return multi;
}

static qa_ui_model_definition require_model(
    parser_t *parser, qa_ui_item_definition *item,
    const qa_ui_script_location *at) {
  validate_type_data(parser, item);
  qa_ui_model_definition model;
  if (!qa_ui_item_model_data(item, &model))
    parser_fail(parser, "model parser dereferences NULL typeData", at);
  return model;
}

static void read_columns(parser_t *parser, qa_ui_list_box_definition list,
                         cursor_t *cursor, const qa_ui_script_location *at) {
  int32_t count = read_int(parser, cursor, at);
  if (count > QA_MAX_UI_LIST_COLUMNS) count = QA_MAX_UI_LIST_COLUMNS;
  qa_ui_memory_allocation_set_int32(list.alloc, 28, count);
  for (int32_t index = 0; index < count; index++) {
    int32_t position = read_int(parser, cursor, at);
    int32_t width = read_int(parser, cursor, at);
    int32_t max_chars = read_int(parser, cursor, at);
    int64_t offset = 32 + index * 12;
    qa_ui_memory_allocation_set_int32(list.alloc, offset, position);
    qa_ui_memory_allocation_set_int32(list.alloc, offset + 4, width);
    qa_ui_memory_allocation_set_int32(list.alloc, offset + 8, max_chars);
  }
}

static void multi_set_string(parser_t *parser, qa_ui_multi_definition multi,
                             int64_t offset, const char *value) {
  qa_ui_string_reference *reference =
      allocate_string_reference(parser, value);
  qa_ui_memory_allocation_set_string_reference(multi.alloc, offset, reference);
  qa_ui_string_reference_release(reference);
}

static void read_string_choices(parser_t *parser,
                                qa_ui_multi_definition multi,
                                cursor_t *cursor,
                                const qa_ui_script_location *at) {
  qa_ui_memory_allocation_set_int32(multi.alloc, 384, 0);
  qa_ui_memory_allocation_set_int32(multi.alloc, 388, 1);
  expect_first(parser, cursor, "{", "expected { before cvarStrList", at);
  bool reading_label = true;
  for (;;) {
    ptok tok = expect_any(parser, cursor, "end of file inside menu item", at);
    bool done = starts_with(tok.value, "}");
    bool skip = starts_with(tok.value, ",") || starts_with(tok.value, ";");
    if (!done && !skip) {
      int32_t count = qa_ui_memory_allocation_get_int32(multi.alloc, 384);
      if (reading_label) {
        multi_set_string(parser, multi, count * 4, tok.value);
        reading_label = false;
      } else {
        multi_set_string(parser, multi, 128 + count * 4, tok.value);
        reading_label = true;
        count++;
        qa_ui_memory_allocation_set_int32(multi.alloc, 384, count);
        if (count >= 32) {
          ptok_release(&tok);
          parse_failure(parser);
        }
      }
    }
    ptok_release(&tok);
    if (done) return;
  }
}

static void read_number_choices(parser_t *parser,
                                qa_ui_multi_definition multi,
                                cursor_t *cursor,
                                const qa_ui_script_location *at) {
  qa_ui_memory_allocation_set_int32(multi.alloc, 384, 0);
  qa_ui_memory_allocation_set_int32(multi.alloc, 388, 0);
  expect_first(parser, cursor, "{", "expected { before cvarFloatList", at);
  for (;;) {
    ptok tok = expect_any(parser, cursor, "end of file inside menu item", at);
    bool done = starts_with(tok.value, "}");
    bool skip = starts_with(tok.value, ",") || starts_with(tok.value, ";");
    if (!done && !skip) {
      int32_t count = qa_ui_memory_allocation_get_int32(multi.alloc, 384);
      qa_ui_script_location location = loc_copy(&tok.token->location);
      multi_set_string(parser, multi, count * 4, tok.value);
      ptok_release(&tok);
      double value = read_float(parser, cursor, &location);
      qa_ui_script_location_destroy(&location);
      qa_ui_memory_allocation_set_float32(multi.alloc, 256 + count * 4,
                                          value);
      count++;
      qa_ui_memory_allocation_set_int32(multi.alloc, 384, count);
      if (count >= 32) parse_failure(parser);
      continue;
    }
    ptok_release(&tok);
    if (done) return;
  }
}

static void post_parse(parser_t *parser, qa_ui_menu_definition *menu) {
  (void)parser;
  if (qa_ui_menu_full_screen(menu) != 0) {
    qa_rect full = {0, 0, 640, 480};
    qa_ui_window_set_rect(&menu->window, full);
  }
  qa_rect menu_rect = qa_ui_window_rect(&menu->window);
  double x = menu_rect.x, y = menu_rect.y;
  if (qa_ui_window_border(&menu->window) != 0) {
    double border_size = qa_ui_window_border_size(&menu->window);
    x = FR(x + border_size);
    y = FR(y + border_size);
  }
  int32_t count = qa_ui_menu_item_count(menu);
  for (int32_t index = 0; index < count; index++) {
    qa_ui_item_definition *item = qa_ui_menu_item_at(menu, index);
    if (item == NULL) continue;
    double item_x = x, item_y = y;
    if (qa_ui_window_border(&item->window) != 0) {
      double border_size = qa_ui_window_border_size(&item->window);
      item_x = FR(item_x + border_size);
      item_y = FR(item_y + border_size);
    }
    qa_rect client = qa_ui_window_client_rect(&item->window);
    qa_rect rect = {FR(item_x + client.x), FR(item_y + client.y),
                    client.width, client.height};
    qa_ui_window_set_rect(&item->window, rect);
    qa_ui_item_set_text_rect_component(item, 2, 0);
    qa_ui_item_set_text_rect_component(item, 3, 0);
  }
}

static void publish_resource_asset(parser_t *parser,
                                   qa_ui_menu_asset_field field,
                                   qa_ui_resource_reference *reference) {
  qa_ui_menu_asset_publication publication;
  memset(&publication, 0, sizeof publication);
  publication.field = field;
  publication.reference = reference;
  publish_asset(parser, publication);
}

static void publish_number_asset(parser_t *parser,
                                 qa_ui_menu_asset_field field, double value) {
  qa_ui_menu_asset_publication publication;
  memset(&publication, 0, sizeof publication);
  publication.field = field;
  publication.number = value;
  publish_asset(parser, publication);
}

static void parse_assets(parser_t *parser, cursor_t *cursor,
                         const qa_ui_script_location *start) {
  expect_token(parser, cursor, "{", "expected { after assetGlobalDef", start);
  for (;;) {
    ptok field = expect_any(parser, cursor, "end of file inside assetGlobalDef",
                            start);
    qa_ui_script_location at = loc_copy(&field.token->location);
    char *value = dupstr(field.value);
    char *keyword = lower_dup(field.value);
    ptok_release(&field);
    if (strcmp(value, "}") == 0) {
      free(value);
      free(keyword);
      qa_ui_script_location_destroy(&at);
      return;
    }
    bool has_handle;
    double handle;
    if (strcmp(keyword, "font") == 0 || strcmp(keyword, "smallfont") == 0 ||
        strcmp(keyword, "bigfont") == 0) {
      qa_ui_font_reference *reference = read_font(parser, cursor, &at);
      register_event(parser, QA_UI_RESOURCE_FONT, reference, &at, &has_handle,
                     &handle);
      qa_ui_menu_asset_field asset_field =
          strcmp(keyword, "font") == 0      ? QA_UI_ASSET_TEXT_FONT
          : strcmp(keyword, "smallfont") == 0 ? QA_UI_ASSET_SMALL_FONT
                                              : QA_UI_ASSET_BIG_FONT;
      if (asset_field == QA_UI_ASSET_TEXT_FONT) parser->assets.text_font = reference;
      else if (asset_field == QA_UI_ASSET_SMALL_FONT) parser->assets.small_font = reference;
      else parser->assets.big_font = reference;
      publish_resource_asset(parser, asset_field, reference);
    } else if (strcmp(keyword, "gradientbar") == 0 ||
               strcmp(keyword, "menuentersound") == 0 ||
               strcmp(keyword, "menuexitsound") == 0 ||
               strcmp(keyword, "itemfocussound") == 0 ||
               strcmp(keyword, "menubuzzsound") == 0) {
      char *path = read_string(parser, cursor, &at);
      bool is_picture = strcmp(keyword, "gradientbar") == 0;
      qa_ui_resource_reference *reference =
          is_picture ? qa_ui_shader_reference_create(path)
                     : qa_ui_sound_reference_create(path);
      free(path);
      register_event(parser,
                     is_picture ? QA_UI_RESOURCE_SHADER : QA_UI_RESOURCE_SOUND,
                     reference, &at, &has_handle, &handle);
      qa_ui_menu_asset_field asset_field;
      if (is_picture) {
        parser->assets.gradient_bar = reference;
        asset_field = QA_UI_ASSET_GRADIENT_BAR;
      } else if (strcmp(keyword, "menuentersound") == 0) {
        parser->assets.menu_enter_sound = reference;
        asset_field = QA_UI_ASSET_MENU_ENTER_SOUND;
      } else if (strcmp(keyword, "menuexitsound") == 0) {
        parser->assets.menu_exit_sound = reference;
        asset_field = QA_UI_ASSET_MENU_EXIT_SOUND;
      } else if (strcmp(keyword, "itemfocussound") == 0) {
        parser->assets.item_focus_sound = reference;
        asset_field = QA_UI_ASSET_ITEM_FOCUS_SOUND;
      } else {
        parser->assets.menu_buzz_sound = reference;
        asset_field = QA_UI_ASSET_MENU_BUZZ_SOUND;
      }
      publish_resource_asset(parser, asset_field, reference);
    } else if (strcmp(keyword, "cursor") == 0) {
      qa_ui_string_reference *path =
          read_string_reference(parser, cursor, &at);
      qa_ui_menu_asset_publication publication;
      memset(&publication, 0, sizeof publication);
      publication.field = QA_UI_ASSET_CURSOR_STR;
      publication.string_value = path;
      publish_asset(parser, publication);
      char *path_text =
          path == NULL ? NULL : qa_ui_string_reference_read(path);
      qa_ui_shader_reference *reference =
          qa_ui_shader_reference_create(path_text);
      free(path_text);
      qa_ui_string_reference_release(path);
      register_event(parser, QA_UI_RESOURCE_SHADER, reference, &at,
                     &has_handle, &handle);
      parser->assets.cursor = reference;
      publish_resource_asset(parser, QA_UI_ASSET_CURSOR, reference);
    } else if (strcmp(keyword, "fadeclamp") == 0) {
      parser->assets.fade_clamp = read_float(parser, cursor, &at);
      publish_number_asset(parser, QA_UI_ASSET_FADE_CLAMP,
                           parser->assets.fade_clamp);
    } else if (strcmp(keyword, "fadecycle") == 0) {
      parser->assets.fade_cycle = read_int(parser, cursor, &at);
      publish_number_asset(parser, QA_UI_ASSET_FADE_CYCLE,
                           parser->assets.fade_cycle);
    } else if (strcmp(keyword, "fadeamount") == 0) {
      parser->assets.fade_amount = read_float(parser, cursor, &at);
      publish_number_asset(parser, QA_UI_ASSET_FADE_AMOUNT,
                           parser->assets.fade_amount);
    } else if (strcmp(keyword, "shadowx") == 0) {
      parser->assets.shadow_x = read_float(parser, cursor, &at);
      publish_number_asset(parser, QA_UI_ASSET_SHADOW_X, parser->assets.shadow_x);
    } else if (strcmp(keyword, "shadowy") == 0) {
      parser->assets.shadow_y = read_float(parser, cursor, &at);
      publish_number_asset(parser, QA_UI_ASSET_SHADOW_Y, parser->assets.shadow_y);
    } else if (strcmp(keyword, "shadowcolor") == 0) {
      double components[4];
      for (int component = 0; component < 4; component++) {
        components[component] = read_float(parser, cursor, &at);
        qa_ui_menu_asset_publication publication;
        memset(&publication, 0, sizeof publication);
        publication.field = QA_UI_ASSET_SHADOW_COLOR_COMPONENT;
        publication.component = component;
        publication.number = components[component];
        publish_asset(parser, publication);
      }
      parser->assets.shadow_color.x = components[0];
      parser->assets.shadow_color.y = components[1];
      parser->assets.shadow_color.z = components[2];
      parser->assets.shadow_color.w = components[3];
      parser->assets.shadow_fade_clamp = parser->assets.shadow_color.w;
      publish_number_asset(parser, QA_UI_ASSET_SHADOW_FADE_CLAMP,
                           parser->assets.shadow_fade_clamp);
    } else {
      warn_fmt(parser, &at, "unknown asset keyword ", value);
    }
    free(value);
    free(keyword);
    qa_ui_script_location_destroy(&at);
  }
}

static void set_handle(int32_t *out, bool has_handle, double handle) {
  *out = has_handle ? (int32_t)handle : 0;
}

static void parse_item_field(parser_t *parser, qa_ui_item_definition *item,
                             cursor_t *cursor, const char *keyword,
                             const char *original,
                             const qa_ui_script_location *at) {
  alloc_t *alloc = item->alloc;
  qa_ui_window_definition *window = &item->window;
  bool has_handle;
  double handle;
  int32_t handle32;
  if (strcmp(keyword, "name") == 0) {
    read_string_reference_into(parser, cursor, at, alloc, 32);
  } else if (strcmp(keyword, "text") == 0) {
    read_string_reference_into(parser, cursor, at, alloc, 224);
  } else if (strcmp(keyword, "group") == 0) {
    read_string_reference_into(parser, cursor, at, alloc, 36);
  } else if (strcmp(keyword, "asset_model") == 0) {
    validate_type_data(parser, item);
    qa_ui_model_definition model;
    bool has_model = qa_ui_item_model_data(item, &model);
    char *path = read_string(parser, cursor, at);
    qa_ui_model_reference *reference = qa_ui_model_reference_create(path);
    free(path);
    register_event(parser, QA_UI_RESOURCE_MODEL, reference, at, &has_handle,
                   &handle);
    set_handle(&handle32, has_handle, handle);
    qa_ui_memory_allocation_set_resource(alloc, 232, reference, has_handle,
                                         handle32);
    double random = parser->host.random(parser->host.random_ctx);
    if (random != trunc(random) || random < 0 || random > 0x7fff)
      parser_fail(parser, "UI random must return an integer in [0, 32767]",
                  at);
    if (!has_model)
      parser_fail(parser,
                  "ItemParse_asset_model dereferences NULL model typeData",
                  at);
    qa_ui_memory_allocation_set_int32(model.alloc, 0,
                                      (int32_t)random % 360);
  } else if (strcmp(keyword, "asset_shader") == 0) {
    char *path = read_string(parser, cursor, at);
    qa_ui_shader_reference *reference = qa_ui_shader_reference_create(path);
    free(path);
    register_event(parser, QA_UI_RESOURCE_SHADER, reference, at, &has_handle,
                   &handle);
    set_handle(&handle32, has_handle, handle);
    qa_ui_memory_allocation_set_resource(alloc, 232, reference, has_handle,
                                         handle32);
  } else if (strcmp(keyword, "model_origin") == 0) {
    qa_ui_model_definition model = require_model(parser, item, at);
    double x = read_float(parser, cursor, at);
    double y = read_float(parser, cursor, at);
    double z = read_float(parser, cursor, at);
    qa_ui_memory_allocation_set_float32(model.alloc, 4, x);
    qa_ui_memory_allocation_set_float32(model.alloc, 8, y);
    qa_ui_memory_allocation_set_float32(model.alloc, 12, z);
  } else if (strcmp(keyword, "model_fovx") == 0) {
    qa_ui_model_definition model = require_model(parser, item, at);
    qa_ui_memory_allocation_set_float32(model.alloc, 16,
                                        read_float(parser, cursor, at));
  } else if (strcmp(keyword, "model_fovy") == 0) {
    qa_ui_model_definition model = require_model(parser, item, at);
    qa_ui_memory_allocation_set_float32(model.alloc, 20,
                                        read_float(parser, cursor, at));
  } else if (strcmp(keyword, "model_rotation") == 0) {
    qa_ui_model_definition model = require_model(parser, item, at);
    qa_ui_memory_allocation_set_int32(model.alloc, 24,
                                      read_int(parser, cursor, at));
  } else if (strcmp(keyword, "model_angle") == 0) {
    qa_ui_model_definition model = require_model(parser, item, at);
    qa_ui_memory_allocation_set_int32(model.alloc, 0,
                                      read_int(parser, cursor, at));
  } else if (strcmp(keyword, "rect") == 0) {
    read_rect_into(parser, window->alloc, 16, cursor, at);
  } else if (strcmp(keyword, "style") == 0) {
    qa_ui_memory_allocation_set_int32(window->alloc, 48,
                                      read_int(parser, cursor, at));
  } else if (strcmp(keyword, "decoration") == 0) {
    qa_ui_window_set_flags(window, qa_ui_window_flags(window) |
                                       QA_UI_WINDOW_DECORATION);
  } else if (strcmp(keyword, "notselectable") == 0) {
    validate_type_data(parser, item);
    if (qa_ui_item_type(item) == QA_UI_ITEM_LIST_BOX) {
      qa_ui_list_box_definition list;
      if (qa_ui_item_list_data(item, &list))
        qa_ui_memory_allocation_set_int32(list.alloc, 228, 1);
    }
  } else if (strcmp(keyword, "wrapped") == 0) {
    qa_ui_window_set_flags(window,
                           qa_ui_window_flags(window) | QA_UI_WINDOW_WRAPPED);
  } else if (strcmp(keyword, "autowrapped") == 0) {
    qa_ui_window_set_flags(window, qa_ui_window_flags(window) |
                                       QA_UI_WINDOW_AUTO_WRAPPED);
  } else if (strcmp(keyword, "horizontalscroll") == 0) {
    qa_ui_window_set_flags(window, qa_ui_window_flags(window) |
                                       QA_UI_WINDOW_HORIZONTAL);
  } else if (strcmp(keyword, "type") == 0) {
    qa_ui_memory_allocation_set_int32(alloc, 196, read_int(parser, cursor, at));
    validate_type_data(parser, item);
  } else if (strcmp(keyword, "elementwidth") == 0) {
    qa_ui_list_box_definition list = require_list(parser, item, at);
    qa_ui_memory_allocation_set_float32(list.alloc, 16,
                                        read_float(parser, cursor, at));
  } else if (strcmp(keyword, "elementheight") == 0) {
    qa_ui_list_box_definition list = require_list(parser, item, at);
    qa_ui_memory_allocation_set_float32(list.alloc, 20,
                                        read_float(parser, cursor, at));
  } else if (strcmp(keyword, "feeder") == 0) {
    qa_ui_item_set_special(item, read_float(parser, cursor, at));
  } else if (strcmp(keyword, "elementtype") == 0) {
    qa_ui_list_box_definition list = checked_list(parser, item);
    qa_ui_memory_allocation_set_int32(list.alloc, 24,
                                      read_int(parser, cursor, at));
  } else if (strcmp(keyword, "columns") == 0) {
    read_columns(parser, checked_list(parser, item), cursor, at);
  } else if (strcmp(keyword, "border") == 0) {
    qa_ui_memory_allocation_set_int32(window->alloc, 52,
                                      read_int(parser, cursor, at));
  } else if (strcmp(keyword, "bordersize") == 0) {
    qa_ui_memory_allocation_set_float32(window->alloc, 64,
                                        read_float(parser, cursor, at));
  } else if (strcmp(keyword, "visible") == 0) {
    if (read_int(parser, cursor, at) != 0)
      qa_ui_window_set_flags(window, qa_ui_window_flags(window) |
                                         QA_UI_WINDOW_VISIBLE);
  } else if (strcmp(keyword, "ownerdraw") == 0) {
    qa_ui_memory_allocation_set_int32(window->alloc, 56,
                                      read_int(parser, cursor, at));
    qa_ui_memory_allocation_set_int32(alloc, 196, QA_UI_ITEM_OWNER_DRAW);
  } else if (strcmp(keyword, "align") == 0) {
    qa_ui_memory_allocation_set_int32(alloc, 200, read_int(parser, cursor, at));
  } else if (strcmp(keyword, "textalign") == 0) {
    qa_ui_memory_allocation_set_int32(alloc, 204, read_int(parser, cursor, at));
  } else if (strcmp(keyword, "textalignx") == 0) {
    qa_ui_memory_allocation_set_float32(alloc, 208,
                                        read_float(parser, cursor, at));
  } else if (strcmp(keyword, "textaligny") == 0) {
    qa_ui_memory_allocation_set_float32(alloc, 212,
                                        read_float(parser, cursor, at));
  } else if (strcmp(keyword, "textscale") == 0) {
    qa_ui_memory_allocation_set_float32(alloc, 216,
                                        read_float(parser, cursor, at));
  } else if (strcmp(keyword, "textstyle") == 0) {
    qa_ui_memory_allocation_set_int32(alloc, 220, read_int(parser, cursor, at));
  } else if (strcmp(keyword, "backcolor") == 0) {
    read_color_into(parser, window->alloc, 128, cursor, at);
  } else if (strcmp(keyword, "forecolor") == 0) {
    read_fore_color(parser, window, cursor, at);
  } else if (strcmp(keyword, "bordercolor") == 0) {
    read_color_into(parser, window->alloc, 144, cursor, at);
  } else if (strcmp(keyword, "outlinecolor") == 0) {
    read_color_into(parser, window->alloc, 160, cursor, at);
  } else if (strcmp(keyword, "background") == 0) {
    char *path = read_string(parser, cursor, at);
    qa_ui_shader_reference *reference = qa_ui_shader_reference_create(path);
    free(path);
    register_event(parser, QA_UI_RESOURCE_SHADER, reference, at, &has_handle,
                   &handle);
    set_handle(&handle32, has_handle, handle);
    qa_ui_window_set_background(window, reference, has_handle, handle32);
  } else if (strcmp(keyword, "onfocus") == 0) {
    qa_ui_memory_allocation_set_script(alloc, 256,
                                       read_script(parser, cursor, at));
  } else if (strcmp(keyword, "leavefocus") == 0) {
    qa_ui_memory_allocation_set_script(alloc, 260,
                                       read_script(parser, cursor, at));
  } else if (strcmp(keyword, "mouseenter") == 0) {
    qa_ui_memory_allocation_set_script(alloc, 244,
                                       read_script(parser, cursor, at));
  } else if (strcmp(keyword, "mouseexit") == 0) {
    qa_ui_memory_allocation_set_script(alloc, 248,
                                       read_script(parser, cursor, at));
  } else if (strcmp(keyword, "mouseentertext") == 0) {
    qa_ui_memory_allocation_set_script(alloc, 236,
                                       read_script(parser, cursor, at));
  } else if (strcmp(keyword, "mouseexittext") == 0) {
    qa_ui_memory_allocation_set_script(alloc, 240,
                                       read_script(parser, cursor, at));
  } else if (strcmp(keyword, "action") == 0) {
    qa_ui_memory_allocation_set_script(alloc, 252,
                                       read_script(parser, cursor, at));
  } else if (strcmp(keyword, "special") == 0) {
    qa_ui_item_set_special(item, read_float(parser, cursor, at));
  } else if (strcmp(keyword, "cvar") == 0) {
    validate_type_data(parser, item);
    read_string_reference_into(parser, cursor, at, alloc, 264);
    qa_ui_edit_field_definition edit;
    if (qa_ui_item_edit_data(item, &edit)) {
      qa_ui_memory_allocation_set_float32(edit.alloc, 0, -1);
      qa_ui_memory_allocation_set_float32(edit.alloc, 4, -1);
      qa_ui_memory_allocation_set_float32(edit.alloc, 8, -1);
    }
  } else if (strcmp(keyword, "maxchars") == 0) {
    qa_ui_edit_field_definition edit = require_edit(parser, item);
    qa_ui_memory_allocation_set_int32(edit.alloc, 16,
                                      read_int(parser, cursor, at));
  } else if (strcmp(keyword, "maxpaintchars") == 0) {
    qa_ui_edit_field_definition edit = require_edit(parser, item);
    qa_ui_memory_allocation_set_int32(edit.alloc, 20,
                                      read_int(parser, cursor, at));
  } else if (strcmp(keyword, "focussound") == 0) {
    char *path = read_string(parser, cursor, at);
    qa_ui_sound_reference *reference = qa_ui_sound_reference_create(path);
    free(path);
    register_event(parser, QA_UI_RESOURCE_SOUND, reference, at, &has_handle,
                   &handle);
    set_handle(&handle32, has_handle, handle);
    qa_ui_memory_allocation_set_resource(alloc, 280, reference, has_handle,
                                         handle32);
  } else if (strcmp(keyword, "cvarfloat") == 0) {
    qa_ui_edit_field_definition edit = require_edit(parser, item);
    read_string_reference_into(parser, cursor, at, alloc, 264);
    qa_ui_memory_allocation_set_float32(edit.alloc, 8,
                                        read_float(parser, cursor, at));
    qa_ui_memory_allocation_set_float32(edit.alloc, 0,
                                        read_float(parser, cursor, at));
    qa_ui_memory_allocation_set_float32(edit.alloc, 4,
                                        read_float(parser, cursor, at));
  } else if (strcmp(keyword, "cvarstrlist") == 0) {
    read_string_choices(parser, require_multi(parser, item), cursor, at);
  } else if (strcmp(keyword, "cvarfloatlist") == 0) {
    read_number_choices(parser, require_multi(parser, item), cursor, at);
  } else if (strcmp(keyword, "addcolorrange") == 0) {
    double low = read_float(parser, cursor, at);
    double high = read_float(parser, cursor, at);
    qa_vec4 color = read_color(parser, cursor, at);
    int32_t index = qa_ui_memory_allocation_get_int32(alloc, 284);
    if (index < QA_MAX_UI_COLOR_RANGES) {
      if (index < 0) qa_range_error("UI color range index is negative");
      int64_t offset = 288 + index * 24;
      color_write(alloc, offset, color);
      qa_ui_memory_allocation_set_float32(alloc, offset + 16, low);
      qa_ui_memory_allocation_set_float32(alloc, offset + 20, high);
      qa_ui_memory_allocation_set_int32(alloc, 284, index + 1);
    }
  } else if (strcmp(keyword, "ownerdrawflag") == 0) {
    int32_t current = qa_ui_window_owner_draw_flags(window);
    qa_ui_memory_allocation_set_int32(window->alloc, 60,
                                      current | read_int(parser, cursor, at));
  } else if (strcmp(keyword, "enablecvar") == 0 ||
             strcmp(keyword, "disablecvar") == 0 ||
             strcmp(keyword, "showcvar") == 0 ||
             strcmp(keyword, "hidecvar") == 0) {
    qa_ui_menu_script *script = read_script(parser, cursor, at);
    qa_ui_memory_allocation_set_script(alloc, 272, script);
    int32_t flags = strcmp(keyword, "enablecvar") == 0    ? 1
                    : strcmp(keyword, "disablecvar") == 0 ? 2
                    : strcmp(keyword, "showcvar") == 0    ? 4
                                                          : 8;
    qa_ui_memory_allocation_set_int32(alloc, 276, flags);
  } else if (strcmp(keyword, "cvartest") == 0) {
    read_string_reference_into(parser, cursor, at, alloc, 268);
  } else if (strcmp(keyword, "cinematic") == 0) {
    read_string_reference_into(parser, cursor, at, alloc, 40);
  } else if (strcmp(keyword, "doubleclick") == 0) {
    qa_ui_list_box_definition list = checked_list(parser, item);
    qa_ui_memory_allocation_set_script(list.alloc, 224,
                                       read_script(parser, cursor, at));
  } else {
    source_error_fmt(parser, "unknown menu item keyword ", original, at,
                     cursor);
  }
}

static qa_ui_item_definition *parse_item(parser_t *parser, cursor_t *cursor,
                                         const qa_ui_script_location *start,
                                         qa_ui_menu_definition *menu) {
  alloc_t *allocation = parser_allocate(parser, 540);
  if (allocation == NULL) {
    menu_set_item(menu, qa_ui_menu_item_count(menu), NULL);
    parser_fail(parser, "Item_Init dereferences a failed UI_Alloc itemDef_t",
                start);
  }
  qa_ui_item_definition *volatile item = item_create(
      allocation, start, parser->memory.kind == QA_UI_MENU_MEMORY_QVM32,
      (double)qa_ui_memory_allocation_offset(allocation));
  ownership_add(&parser->ownership, OWNED_ITEM, item);
  menu_set_item(menu, qa_ui_menu_item_count(menu), item);
  item_initialize(item);
  ptok opening;
  if (!cursor_next_ptok(cursor, &opening)) return NULL;
  bool opened = starts_with(opening.value, "{");
  ptok_release(&opening);
  if (!opened) return NULL;
  for (;;) {
    ptok field;
    if (!cursor_next_ptok(cursor, &field)) {
      source_error(parser, "end of file inside menu item", start, cursor);
      return NULL;
    }
    qa_ui_script_location at = loc_copy(&field.token->location);
    char *volatile value = dupstr(field.value);
    char *keyword = lower_dup(field.value);
    ptok_release(&field);
    if (starts_with(value, "}")) {
      free(value);
      free(keyword);
      qa_ui_script_location_destroy(&at);
      return item;
    }
    volatile bool failed = false;
    PARSER_TRY(parser, cursor, item_catch) {
      parse_item_field(parser, item, cursor, keyword, value, &at);
      PARSER_TRY_END(parser, item_catch);
    } PARSER_CATCH(parser, item_catch) {
      failed = true;
      break;
    }
    if (failed) {
      source_error_fmt(parser, "couldn't parse menu item keyword ", value,
                       &at, cursor);
      free(value);
      free(keyword);
      qa_ui_script_location_destroy(&at);
      return NULL;
    }
    free(value);
    free(keyword);
    qa_ui_script_location_destroy(&at);
  }
}

static qa_ui_menu_definition *parse_menu(parser_t *parser, cursor_t *cursor,
                                         const qa_ui_script_location *start,
                                         int64_t index) {
  qa_ui_menu_definition *menu = menu_create(
      qa_ui_team_arena_memory_menu_record(parser->storage, index), start);
  ownership_add(&parser->ownership, OWNED_MENU, menu);
  menu_initialize(menu, &parser->assets);
  ptok opening;
  if (!cursor_next_ptok(cursor, &opening)) return NULL;
  bool opened = starts_with(opening.value, "{");
  ptok_release(&opening);
  if (!opened) return NULL;
  for (;;) {
    ptok field;
    if (!cursor_next_ptok(cursor, &field)) {
      source_error(parser, "end of file inside menu", start, cursor);
      return NULL;
    }
    qa_ui_script_location at = loc_copy(&field.token->location);
    char *volatile value = dupstr(field.value);
    char *keyword = lower_dup(field.value);
    ptok_release(&field);
    if (starts_with(value, "}")) {
      free(value);
      free(keyword);
      qa_ui_script_location_destroy(&at);
      post_parse(parser, menu);
      return menu;
    }
    volatile bool failed = false;
    PARSER_TRY(parser, cursor, menu_catch) {
      alloc_t *alloc = menu->alloc;
      qa_ui_window_definition *window = &menu->window;
      bool has_handle;
      double handle;
      int32_t handle32;
      if (strcmp(keyword, "font") == 0) {
        read_string_reference_into(parser, cursor, &at, alloc, 180);
        if (!parser->font_registered) {
          char *font_path = qa_ui_menu_font(menu);
          qa_ui_font_reference *reference =
              qa_ui_font_reference_create(font_path, 48);
          free(font_path);
          register_event(parser, QA_UI_RESOURCE_FONT, reference, &at,
                         &has_handle, &handle);
          parser->assets.text_font = reference;
          publish_resource_asset(parser, QA_UI_ASSET_TEXT_FONT, reference);
          parser->font_registered = true;
          qa_ui_menu_asset_publication publication;
          memset(&publication, 0, sizeof publication);
          publication.field = QA_UI_ASSET_FONT_REGISTERED;
          publication.boolean = true;
          publish_asset(parser, publication);
        }
      } else if (strcmp(keyword, "name") == 0) {
        read_string_reference_into(parser, cursor, &at, alloc, 32);
      } else if (strcmp(keyword, "fullscreen") == 0) {
        qa_ui_memory_allocation_set_int32(alloc, 184,
                                          read_int(parser, cursor, &at));
      } else if (strcmp(keyword, "rect") == 0) {
        read_rect_into(parser, window->alloc, 0, cursor, &at);
      } else if (strcmp(keyword, "style") == 0) {
        qa_ui_memory_allocation_set_int32(window->alloc, 48,
                                          read_int(parser, cursor, &at));
      } else if (strcmp(keyword, "visible") == 0) {
        if (read_int(parser, cursor, &at) != 0)
          qa_ui_window_set_flags(window, qa_ui_window_flags(window) |
                                             QA_UI_WINDOW_VISIBLE);
      } else if (strcmp(keyword, "onopen") == 0) {
        qa_ui_memory_allocation_set_script(alloc, 212,
                                           read_script(parser, cursor, &at));
      } else if (strcmp(keyword, "onclose") == 0) {
        qa_ui_memory_allocation_set_script(alloc, 216,
                                           read_script(parser, cursor, &at));
      } else if (strcmp(keyword, "onesc") == 0) {
        qa_ui_memory_allocation_set_script(alloc, 220,
                                           read_script(parser, cursor, &at));
      } else if (strcmp(keyword, "border") == 0) {
        qa_ui_memory_allocation_set_int32(window->alloc, 52,
                                          read_int(parser, cursor, &at));
      } else if (strcmp(keyword, "bordersize") == 0) {
        qa_ui_memory_allocation_set_float32(window->alloc, 64,
                                            read_float(parser, cursor, &at));
      } else if (strcmp(keyword, "backcolor") == 0) {
        read_color_into(parser, window->alloc, 128, cursor, &at);
      } else if (strcmp(keyword, "forecolor") == 0) {
        read_fore_color(parser, window, cursor, &at);
      } else if (strcmp(keyword, "bordercolor") == 0) {
        read_color_into(parser, window->alloc, 144, cursor, &at);
      } else if (strcmp(keyword, "focuscolor") == 0) {
        read_color_into(parser, alloc, 228, cursor, &at);
      } else if (strcmp(keyword, "disablecolor") == 0) {
        read_color_into(parser, alloc, 244, cursor, &at);
      } else if (strcmp(keyword, "outlinecolor") == 0) {
        read_color_into(parser, window->alloc, 160, cursor, &at);
      } else if (strcmp(keyword, "background") == 0) {
        char *path = read_string(parser, cursor, &at);
        qa_ui_shader_reference *reference =
            qa_ui_shader_reference_create(path);
        free(path);
        register_event(parser, QA_UI_RESOURCE_SHADER, reference, &at,
                       &has_handle, &handle);
        set_handle(&handle32, has_handle, handle);
        qa_ui_window_set_background(window, reference, has_handle, handle32);
      } else if (strcmp(keyword, "ownerdraw") == 0) {
        qa_ui_memory_allocation_set_int32(window->alloc, 56,
                                          read_int(parser, cursor, &at));
      } else if (strcmp(keyword, "ownerdrawflag") == 0) {
        int32_t current = qa_ui_window_owner_draw_flags(window);
        qa_ui_memory_allocation_set_int32(
            window->alloc, 60, current | read_int(parser, cursor, &at));
      } else if (strcmp(keyword, "outofboundsclick") == 0) {
        qa_ui_window_set_flags(window, qa_ui_window_flags(window) |
                                           QA_UI_WINDOW_OUT_OF_BOUNDS_CLICK);
      } else if (strcmp(keyword, "soundloop") == 0) {
        read_string_reference_into(parser, cursor, &at, alloc, 224);
      } else if (strcmp(keyword, "itemdef") == 0) {
        if (qa_ui_menu_item_count(menu) < QA_MAX_UI_MENU_ITEMS) {
          if (parse_item(parser, cursor, &at, menu) == NULL)
            parse_failure(parser);
          qa_ui_item_definition *item =
              qa_ui_menu_item_at(menu, qa_ui_menu_item_count(menu));
          if (item != NULL && qa_ui_item_type(item) == QA_UI_ITEM_LIST_BOX) {
            qa_ui_list_box_definition list;
            bool has_list = qa_ui_item_list_data(item, &list);
            qa_ui_item_set_cursor_position(item, 0);
            if (has_list) {
              qa_ui_list_set_cursor_position(&list, 0);
              qa_ui_list_set_start_position(&list, 0);
              qa_ui_list_set_end_position(&list, 0);
              qa_ui_list_set_cursor_position(&list, 0);
            }
          }
          qa_ui_item_definition *parent_item =
              qa_ui_menu_item_at(menu, qa_ui_menu_item_count(menu));
          qa_ui_memory_allocation_set_int32(
              alloc, 188, qa_ui_menu_item_count(menu) + 1);
          if (parent_item == NULL)
            parser_fail(parser,
                        "MenuParse_itemDef dereferences a NULL retained item parent",
                        &at);
          qa_ui_item_set_parent(parent_item, menu);
        }
      } else if (strcmp(keyword, "cinematic") == 0) {
        read_string_reference_into(parser, cursor, &at, alloc, 40);
      } else if (strcmp(keyword, "popup") == 0) {
        qa_ui_window_set_flags(window,
                               qa_ui_window_flags(window) | QA_UI_WINDOW_POPUP);
      } else if (strcmp(keyword, "fadeclamp") == 0) {
        qa_ui_memory_allocation_set_float32(alloc, 204,
                                            read_float(parser, cursor, &at));
      } else if (strcmp(keyword, "fadecycle") == 0) {
        qa_ui_memory_allocation_set_int32(alloc, 200,
                                          read_int(parser, cursor, &at));
      } else if (strcmp(keyword, "fadeamount") == 0) {
        qa_ui_memory_allocation_set_float32(alloc, 208,
                                            read_float(parser, cursor, &at));
      } else {
        source_error_fmt(parser, "unknown menu keyword ", value, &at, cursor);
      }
      PARSER_TRY_END(parser, menu_catch);
    } PARSER_CATCH(parser, menu_catch) {
      failed = true;
      break;
    }
    if (failed) {
      source_error_fmt(parser, "couldn't parse menu keyword ", value, &at,
                       cursor);
      free(value);
      free(keyword);
      qa_ui_script_location_destroy(&at);
      return NULL;
    }
    free(value);
    free(keyword);
    qa_ui_script_location_destroy(&at);
  }
}

/* A resolved root/menu file: text source or an open handle cursor. */
typedef struct resolved_file {
  bool is_cursor;
  qa_ui_script_source source;
  cursor_t *cursor;
  const char *path;
} resolved_file;

static bool open_file(parser_t *parser, const char *path,
                      resolved_file *out) {
  memset(out, 0, sizeof *out);
  if (parser->host.kind == QA_UI_PARSE_HOST_RESOLVED) {
    if (!parser->host.resolver.resolve_root(
            parser->host.resolver.resolve_root_ctx, path, &out->source))
      return false;
    out->is_cursor = false;
    out->path = out->source.path;
    return true;
  }
  out->cursor = qa_ui_menu_token_cursor_open_handle(
      path, parser->host.script_sources, parser->host.script_sources_ctx,
      parser->host.assert_current_operation, parser->host.assert_current_ctx);
  if (out->cursor == NULL) return false;
  out->is_cursor = true;
  out->path = qa_ui_menu_token_cursor_path(out->cursor);
  return true;
}

typedef struct report_bridge {
  parser_t *parser;
  qa_ui_script_report_fn report;
  void *report_ctx;
} report_bridge;

static void report_bridge_fn(void *ctx,
                             const qa_ui_script_diagnostic *diagnostic) {
  report_bridge *bridge = ctx;
  if (bridge->report != NULL) bridge->report(bridge->report_ctx, diagnostic);
  report_diagnostic(bridge->parser, diagnostic);
}

static cursor_t *open_source(parser_t *parser, const resolved_file *file) {
  if (file->is_cursor) return file->cursor;
  if (parser->host.kind != QA_UI_PARSE_HOST_RESOLVED)
    type_error("Parsed text sources require a diagnostic menu resolver");
  qa_ui_script_preprocessor_options options = parser->preprocessor_options;
  if (parser->bridge_count == parser->bridge_capacity) {
    size_t capacity = parser->bridge_capacity == 0 ? 8 : parser->bridge_capacity * 2;
    void **grown = realloc(parser->bridges, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("UI memory allocation failed");
    parser->bridges = grown;
    parser->bridge_capacity = capacity;
  }
  report_bridge *bridge = malloc(sizeof *bridge);
  if (bridge == NULL) qa_range_error("UI memory allocation failed");
  parser->bridges[parser->bridge_count++] = bridge;
  bridge->parser = parser;
  bridge->report = options.report;
  bridge->report_ctx = options.report_ctx;
  options.report = report_bridge_fn;
  options.report_ctx = bridge;
  return qa_ui_menu_token_cursor_open(&file->source,
                                      parser->host.resolver.resolve,
                                      parser->host.resolver.resolve_ctx,
                                      &options);
}

static void resolve_root(parser_t *parser, const char *path,
                         const char *label, resolved_file *out) {
  if (path[0] == '\0') {
    qa_ui_script_location at = loc_make("<ui>", 1, 1);
    parser_fail_fmt(parser, label, " path cannot be empty", &at);
  }
  if (!open_file(parser, path, out)) {
    qa_ui_script_location at = loc_make(path, 1, 1);
    size_t length = strlen(label) + strlen(path) + 16;
    char *message = malloc(length);
    snprintf(message, length, "couldn't load %s %s", label, path);
    parser_fail(parser, message, &at);
  }
  if (out->path[0] == '\0') {
    qa_ui_script_location at = loc_make(path, 1, 1);
    parser_fail_fmt(parser, label,
                    " resolver returned an empty canonical path", &at);
  }
}

static void parse_with_cursor(parser_t *parser, cursor_t *cursor,
                              const char *path);

static void load_menu_file(parser_t *parser, const char *path) {
  resolved_file file;
  bool found = open_file(parser, path, &file);
  if (!found && strcmp(path, "ui/testhud.menu") != 0)
    found = open_file(parser, "ui/testhud.menu", &file);
  if (!found) return;
  parse_with_cursor(parser, open_source(parser, &file), file.path);
}

/* TS parseSource. Its `finally { cursor.dispose() }` has no C counterpart on
   the failure path: a parse failure reaches no catch in C (the fail env has
   no catch frame), so it aborts like the uncaught TS ScriptLanguageError. */
static void parse_with_cursor(parser_t *parser, cursor_t *cursor,
                              const char *path) {
  if (path[0] == '\0') {
    qa_ui_script_location at = loc_make(path, 1, 1);
    parser_fail(parser, "menu file resolver returned an empty canonical path",
                &at);
  }
  push_loaded(parser, path);
  for (;;) {
    ptok tok;
    if (!cursor_next_ptok(cursor, &tok)) break;
    qa_ui_script_location at = loc_copy(&tok.token->location);
    char *keyword = lower_dup(tok.value);
    bool closing = starts_with(tok.value, "}");
    ptok_release(&tok);
    if (closing) {
      free(keyword);
      qa_ui_script_location_destroy(&at);
      break;
    }
    if (strcmp(keyword, "assetglobaldef") == 0) {
      volatile bool failed = false;
      PARSER_TRY(parser, cursor, asset_catch) {
        parse_assets(parser, cursor, &at);
        PARSER_TRY_END(parser, asset_catch);
      } PARSER_CATCH(parser, asset_catch) {
        failed = true;
        break;
      }
      free(keyword);
      qa_ui_script_location_destroy(&at);
      if (failed) break;
      continue;
    }
    if (strcmp(keyword, "menudef") == 0) {
      double index = (double)parser->menu_count;
      if (index >= QA_MAX_UI_MENUS) {
        char message[48];
        snprintf(message, sizeof message, "more than %d menus defined",
                 QA_MAX_UI_MENUS);
        parser_fail(parser, message, &at);
      }
      qa_ui_menu_definition *menu =
          parse_menu(parser, cursor, &at, (int64_t)index);
      if (menu == NULL) {
        free(keyword);
        qa_ui_script_location_destroy(&at);
        continue;
      }
      /* Menu_New increments the current count after parsing its original
         slot. A reentrant registration may already have advanced that
         count. */
      double published_index = (double)parser->menu_count;
      qa_ui_menu_definition *published = menu;
      if (published_index != index) {
        published = menu_create(qa_ui_team_arena_memory_menu_record(
                                    parser->storage, (int64_t)published_index),
                                &at);
        /* Items parsed above still name the superseded view as parent, so
         * it stays alive with the rest of this parse's ownership. */
        ownership_add(&parser->ownership, OWNED_MENU, published);
      }
      push_menu(parser, published);
    }
    free(keyword);
    qa_ui_script_location_destroy(&at);
  }
  qa_ui_menu_token_cursor_dispose(cursor);
  qa_ui_menu_token_cursor_destroy(cursor);
}

static void load_hud_set(parser_t *parser,
                         const qa_ui_script_source *source) {
  qa_common_parse_state state;
  qa_common_parse_state_init(&state);
  size_t compressed_length = 0;
  char *compressed = qa_compress_common_text(
      (const char *)source->text, source->text_length, false,
      &compressed_length);
  qa_common_parse_cursor cursor;
  qa_common_parse_cursor_init(&cursor, compressed, compressed_length, false);
  for (;;) {
    const char *token = qa_common_parse_state_parse(&state, &cursor, true);
    if (token[0] == '\0' || starts_with(token, "}")) break;
    char *lower = lower_dup(token);
    bool is_load = lower != NULL && strcmp(lower, "loadmenu") == 0;
    free(lower);
    if (!is_load) continue;
    if (!starts_with(qa_common_parse_state_parse(&state, &cursor, true), "{"))
      break;
    bool done = false;
    while (!done) {
      char *path = dupstr(qa_common_parse_state_parse(&state, &cursor, true));
      if (strcmp(path, "}") == 0) {
        free(path);
        break;
      }
      if (path[0] == '\0') {
        free(path);
        done = true;
        break;
      }
      load_menu_file(parser, path);
      free(path);
    }
    if (done) break;
  }
  qa_common_parse_cursor_destroy(&cursor);
  free(compressed);
}

static void load_set(parser_t *parser, const char *path) {
  if (parser->host.kind != QA_UI_PARSE_HOST_RESOLVED)
    type_error("HUD menu sets require a bounded COM text resolver");
  resolved_file file;
  resolve_root(parser, path, "menu set", &file);
  push_loaded(parser, file.path);
  if (file.is_cursor)
    type_error("HUD menu sets require a bounded COM text resolver");
  if (file.source.text_length > QA_MAX_HUD_MENU_SET_BYTES) {
    qa_ui_script_location at = loc_make(file.source.path, 1, 1);
    parser_fail_fmt(parser, "menu file too large: ", file.source.path, &at);
  }
  load_hud_set(parser, &file.source);
}

qa_ui_menu_source_parser *qa_ui_menu_source_parser_create(
    const qa_ui_menu_parse_host *host,
    const qa_ui_script_preprocessor_options *preprocessor_options,
    const qa_ui_menu_parse_options *parse_options) {
  parser_t *parser = calloc(1, sizeof *parser);
  if (parser == NULL) return NULL;
  parser->host = *host;
  if (preprocessor_options != NULL)
    parser->preprocessor_options = *preprocessor_options;
  if (parse_options != NULL) parser->parse_options = *parse_options;
  parser->memory.kind = QA_UI_MENU_MEMORY_UNACCOUNTED;
  if (parser->parse_options.has_memory)
    parser->memory = parser->parse_options.memory;
  if (parser->memory.kind == QA_UI_MENU_MEMORY_QVM32) {
    parser->storage = parser->memory.memory;
  } else {
    parser->storage = qa_ui_team_arena_memory_create(
        "qvm32", NULL, NULL, QA_UI_TEAM_ARENA_MODULE_UI);
    parser->own_storage = true;
  }
  parser->assets = new_assets(parser->parse_options.initial_assets);
  /* Inherited assets must outlive the generation that parsed them. */
  qa_ui_resource_reference *const inherited[] = {
      parser->assets.text_font, parser->assets.small_font,
      parser->assets.big_font, parser->assets.cursor,
      parser->assets.gradient_bar, parser->assets.menu_enter_sound,
      parser->assets.menu_exit_sound, parser->assets.menu_buzz_sound,
      parser->assets.item_focus_sound};
  for (size_t i = 0; i < sizeof inherited / sizeof inherited[0]; i++)
    ownership_add(&parser->ownership, OWNED_REFERENCE,
                  qa_ui_resource_reference_retain(inherited[i]));
  parser->font_registered =
      parser->parse_options.has_initial_font_registered &&
      parser->parse_options.initial_font_registered;
  qa_ui_script_fail_env_init(&parser->fail_env);
  return parser;
}

void qa_ui_menu_source_parser_destroy(qa_ui_menu_source_parser *parser) {
  if (parser == NULL) return;
  free(parser->menus);
  for (size_t i = 0; i < parser->loaded_count; i++)
    free(parser->loaded_files[i]);
  free(parser->loaded_files);
  for (size_t i = 0; i < parser->reported_count; i++)
    qa_ui_script_diagnostic_destroy(&parser->reported[i]);
  free(parser->reported);
  for (size_t i = 0; i < parser->registration_count; i++)
    qa_ui_script_location_destroy(&parser->registrations[i].location);
  free(parser->registrations);
  for (size_t i = 0; i < parser->bridge_count; i++) free(parser->bridges[i]);
  free(parser->bridges);
  ownership_destroy(parser->ownership);
  if (parser->own_storage) qa_ui_team_arena_memory_destroy(parser->storage);
  qa_ui_script_fail_env_destroy(&parser->fail_env);
  free(parser);
}

void qa_ui_menu_source_parser_load(qa_ui_menu_source_parser *parser,
                                   const qa_ui_menu_load_plan *plan,
                                   qa_ui_menu_definitions *out) {
  load_set(parser, plan->set_path);
  memset(out, 0, sizeof *out);
  out->memory = parser->memory;
  out->menus = parser->menus;
  out->menu_count = parser->menu_count;
  parser->menus = NULL;
  parser->menu_count = 0;
  parser->menu_capacity = 0;
  out->assets = freeze_assets(&parser->assets);
  out->loaded_files = parser->loaded_files;
  out->loaded_file_count = parser->loaded_count;
  parser->loaded_files = NULL;
  parser->loaded_count = 0;
  parser->loaded_capacity = 0;
  out->diagnostics = parser->reported;
  out->diagnostic_count = parser->reported_count;
  parser->reported = NULL;
  parser->reported_count = 0;
  parser->reported_capacity = 0;
  out->registration_kind =
      parser->parse_options.registration_sink == NULL
          ? QA_UI_REGISTRATION_DEFERRED
          : QA_UI_REGISTRATION_COMPLETED;
  out->registration_events = parser->registrations;
  out->registration_event_count = parser->registration_count;
  parser->registrations = NULL;
  parser->registration_count = 0;
  parser->registration_capacity = 0;
  out->font_registered = parser->font_registered;
  if (parser->own_storage) {
    /* Handles keep the pool's bytes alive; the definitions keep the pool. */
    if (parser->ownership == NULL) parser->ownership = calloc(1, sizeof *parser->ownership);
    if (parser->ownership == NULL) qa_range_error("UI memory allocation failed");
    parser->ownership->storage = parser->storage;
    parser->own_storage = false;
  }
  out->ownership = parser->ownership;
  parser->ownership = NULL;
  for (size_t i = 0; i < out->menu_count; i++) menu_check_items(out->menus[i]);
}

void qa_ui_menu_source_parser_parse_source(
    qa_ui_menu_source_parser *parser, const qa_ui_script_source *source) {
  resolved_file file;
  memset(&file, 0, sizeof file);
  file.source = *source;
  file.path = source->path;
  parse_with_cursor(parser, open_source(parser, &file), file.path);
}

void qa_ui_menu_source_parser_parse_cursor(
    qa_ui_menu_source_parser *parser, qa_ui_menu_token_cursor *cursor) {
  parse_with_cursor(parser, cursor, qa_ui_menu_token_cursor_path(cursor));
}

void qa_ui_load_menu_definitions(
    const qa_ui_menu_resolved_parse_host *host,
    const qa_ui_menu_load_plan *plan,
    const qa_ui_script_preprocessor_options *preprocessor_options,
    const qa_ui_menu_parse_options *parse_options,
    qa_ui_menu_definitions *out) {
  qa_ui_script_preprocessor_options empty_preprocessor;
  qa_ui_menu_parse_options empty_parse;
  memset(&empty_preprocessor, 0, sizeof empty_preprocessor);
  memset(&empty_parse, 0, sizeof empty_parse);
  parser_t *parser = qa_ui_menu_source_parser_create(
      host,
      preprocessor_options != NULL ? preprocessor_options
                                   : &empty_preprocessor,
      parse_options != NULL ? parse_options : &empty_parse);
  qa_ui_menu_source_parser_load(parser, plan, out);
  qa_ui_menu_source_parser_destroy(parser);
}
