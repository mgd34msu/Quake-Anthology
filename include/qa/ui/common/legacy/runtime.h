/*
 * Port of src/ui/common/legacy/runtime.ts: Team Arena shared-menu
 * runtime (ui_shared.c). C accommodations:
 *  - All host interfaces are fn+ctx tables; TS async methods are
 *    synchronous (PORTING.md). Union-typed results use kind tags
 *    with has-value bools for TS undefined/null.
 *  - PictureAsset | (() => PictureAsset) is qa_ui_picture_or_lazy.
 *  - UiCapturedMenu handles are runtime-owned structs compared by
 *    pointer (the TS Map identity); definition updated in place.
 *  - Snapshots are owned value trees freed by the destroy fn.
 *  - TS numbers that are validated (key codes, times, feeder
 *    selections, coordinates) stay double at the boundary.
 *  - Plain TS Error throws abort via qa_ui_script_plain_error;
 *    RangeErrors via qa_range_error.
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_LEGACY_RUNTIME_H
#define QA_UI_COMMON_LEGACY_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/audio/wav.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/contracts/common.h"
#include "qa/contracts/math.h"
#include "qa/core/commands/index.h"
#include "qa/core/common_parse.h"
#include "qa/core/cvars/index.h"
#include "qa/text/draw2d.h"
#include "qa/text/q3_font.h"
#include "qa/ui/common/legacy/menu.h"

/** TS UiKeyEvent. */
typedef enum qa_ui_key_event_kind {
  QA_UI_KEY_EVENT_KEY,       /* "key" */
  QA_UI_KEY_EVENT_CHARACTER  /* "character" */
} qa_ui_key_event_kind;

typedef struct qa_ui_key_event {
  qa_ui_key_event_kind kind;
  double code;
  bool down; /* key only */
} qa_ui_key_event;

/** TS playLocal argument: PcmSound | number | undefined. */
typedef enum qa_ui_runtime_sound_arg_kind {
  QA_UI_SOUND_ARG_NONE,
  QA_UI_SOUND_ARG_PCM,
  QA_UI_SOUND_ARG_HANDLE
} qa_ui_runtime_sound_arg_kind;

typedef struct qa_ui_runtime_sound_arg {
  qa_ui_runtime_sound_arg_kind kind;
  const qa_pcm_sound *sound; /* borrowed */
  double handle;
} qa_ui_runtime_sound_arg;

/** TS UiRuntimeAudio. start_background path NULL is TS null. */
typedef struct qa_ui_runtime_audio {
  void *ctx;
  void (*play_local)(void *ctx, qa_ui_runtime_sound_arg sound);
  void (*start_background)(void *ctx, const char *path);
  void (*stop_background)(void *ctx);
} qa_ui_runtime_audio;

/** TS UiRuntimeResources.handles. */
typedef enum qa_ui_runtime_handles_kind {
  QA_UI_HANDLES_DIAGNOSTIC, /* "diagnostic" */
  QA_UI_HANDLES_SOURCE      /* "source" */
} qa_ui_runtime_handles_kind;

typedef struct qa_ui_runtime_handles {
  qa_ui_runtime_handles_kind kind;
  void *ctx;
  /* picture NULL is TS undefined. */
  double (*picture_handle)(void *ctx, const qa_picture_asset *picture);
  /* False is TS undefined. */
  bool (*picture_for_handle)(void *ctx, double handle,
                             qa_picture_asset *out);
  qa_q3_pres_scene_model (*model_for_handle)(void *ctx, double handle);
} qa_ui_runtime_handles;

/** TS UiCinematicAsset (path borrowed from the producing host). */
typedef struct qa_ui_cinematic_asset {
  const char *path;
} qa_ui_cinematic_asset;

/** TS UiRuntimeResources. Register fns false is TS undefined
 * (except register_font/model_for_handle shapes noted in TS);
 * path NULL is TS null. */
typedef struct qa_ui_runtime_resources {
  qa_ui_runtime_handles handles;
  void *ctx;
  void (*register_font)(void *ctx, const char *path, double point_size);
  bool (*register_picture)(void *ctx, const char *path,
                           qa_picture_asset *out);
  bool (*registered_picture)(void *ctx, const char *path,
                             qa_picture_asset *out);
  bool (*register_sound)(void *ctx, const char *path, qa_pcm_sound *out);
  bool (*registered_sound)(void *ctx, const char *path, qa_pcm_sound *out);
  qa_q3_pres_scene_model (*register_model)(void *ctx, const char *path);
  bool (*registered_model)(void *ctx, const char *path,
                           qa_q3_pres_scene_model *out);
  qa_ui_cinematic_asset (*prepare_cinematic)(void *ctx, const char *path);
} qa_ui_runtime_resources;

/** TS UiCinematicInstance. */
typedef struct qa_ui_cinematic_instance {
  qa_ui_cinematic_asset asset;
  double handle_index;
} qa_ui_cinematic_instance;

/** TS UiRuntimeCinematics. play false is TS undefined. */
typedef struct qa_ui_runtime_cinematics {
  void *ctx;
  bool (*play)(void *ctx, qa_ui_cinematic_asset asset, qa_rect rect,
               qa_ui_cinematic_instance *out);
  void (*run)(void *ctx, double handle, double time);
  void (*draw)(void *ctx, double handle, qa_rect rect,
               const qa_draw2d *draw);
  void (*stop)(void *ctx, double handle);
} qa_ui_runtime_cinematics;

/** TS UiWidgetAssets. */
typedef struct qa_ui_widget_assets {
  qa_picture_asset white_shader;
  qa_picture_asset gradient_bar;
  qa_picture_asset scroll_bar;
  qa_picture_asset scroll_bar_arrow_down;
  qa_picture_asset scroll_bar_arrow_up;
  qa_picture_asset scroll_bar_arrow_left;
  qa_picture_asset scroll_bar_arrow_right;
  qa_picture_asset scroll_bar_thumb;
  qa_picture_asset slider_bar;
  qa_picture_asset slider_thumb;
} qa_ui_widget_assets;

/** TS PictureAsset | (() => PictureAsset). */
typedef enum qa_ui_picture_or_lazy_kind {
  QA_UI_PICTURE_NONE,
  QA_UI_PICTURE_VALUE,
  QA_UI_PICTURE_LAZY
} qa_ui_picture_or_lazy_kind;

typedef struct qa_ui_picture_or_lazy {
  qa_ui_picture_or_lazy_kind kind;
  qa_picture_asset picture;
  qa_picture_producer_fn produce;
  void *produce_ctx;
} qa_ui_picture_or_lazy;

/** TS UiModelPaintRequest. */
typedef struct qa_ui_model_paint_request {
  const qa_draw2d *draw;
  qa_q3_pres_scene_model model;
  qa_rect rect;
  double time;
  double angle;
  double field_of_view_x;
  double field_of_view_y;
} qa_ui_model_paint_request;

typedef void (*qa_ui_paint_model_fn)(void *ctx,
                                     const qa_ui_model_paint_request *request);

/** TS UiOwnerDrawPaintRequest. */
typedef struct qa_ui_owner_draw_paint_request {
  const qa_draw2d *draw;
  qa_rect rect;
  double text_x;
  double text_y;
  double owner_draw;
  double owner_draw_flags;
  double alignment;
  double special;
  double text_scale;
  qa_vec4 color;
  qa_ui_picture_or_lazy background;
  double text_style;
} qa_ui_owner_draw_paint_request;

/** TS UiRuntimeBindings. String results are malloc'd (caller
 * frees); NULL only on OOM. */
typedef struct qa_ui_runtime_bindings {
  void *ctx;
  char *(*key_name)(void *ctx, double key);
  char *(*get_binding)(void *ctx, double key);
  void (*set_binding)(void *ctx, double key, const char *command);
  bool (*get_overstrike)(void *ctx);
  void (*set_overstrike)(void *ctx, bool enabled);
} qa_ui_runtime_bindings;

/** TS UiRuntimeFeederItem. has_text false with text NULL is the
 * TS null text; the item fn returning false is TS undefined. */
typedef struct qa_ui_runtime_feeder_item {
  const char *text; /* borrowed for the call; NULL is TS null */
  bool has_picture;
  qa_picture_asset picture;
} qa_ui_runtime_feeder_item;

/** TS UiRuntimeFeeder. */
typedef struct qa_ui_runtime_feeder {
  void *ctx;
  double (*count)(void *ctx, double feeder);
  bool (*item)(void *ctx, double feeder, double index, double column,
               qa_ui_runtime_feeder_item *out);
  bool (*image)(void *ctx, double feeder, double index,
                qa_picture_asset *out);
  void (*select)(void *ctx, double feeder, double index);
} qa_ui_runtime_feeder;

/** TS UiOwnerDrawKeyResult. */
typedef struct qa_ui_owner_draw_key_result {
  bool handled;
  double special;
} qa_ui_owner_draw_key_result;

/** TS UiRuntimeOwnerDraw. */
typedef struct qa_ui_runtime_owner_draw {
  void *ctx;
  bool (*visible)(void *ctx, double flags);
  double (*width)(void *ctx, double owner_draw, double scale);
  double (*value)(void *ctx, double owner_draw);
  qa_ui_owner_draw_key_result (*handle_key)(void *ctx, double owner_draw,
                                            double flags, double special,
                                            double key);
  void (*paint)(void *ctx, const qa_ui_owner_draw_paint_request *request);
  void (*close_cinematic)(void *ctx, double owner_draw);
} qa_ui_runtime_owner_draw;

/** TS UiExternalScriptContext. Names NULL is TS undefined
 * (borrowed for the call). */
typedef struct qa_ui_external_script_context {
  const char *menu_name;
  const char *item_name;
} qa_ui_external_script_context;

/** TS UiScriptCursor (runtime-owned; valid during run()). */
typedef struct qa_ui_script_cursor qa_ui_script_cursor;

double qa_ui_script_cursor_position(qa_ui_script_cursor *cursor);
double qa_ui_script_cursor_remaining(qa_ui_script_cursor *cursor);
/* False is TS undefined; out token owned by caller (destroy it). */
bool qa_ui_script_cursor_peek(qa_ui_script_cursor *cursor,
                              qa_ui_menu_script_token *out);
bool qa_ui_script_cursor_next(qa_ui_script_cursor *cursor,
                              qa_ui_menu_script_token *out);
/** TS string(): NONE is TS undefined, NULL_VALUE is TS null,
 * TEXT sets *out (malloc'd). */
typedef enum qa_ui_script_string_kind {
  QA_UI_SCRIPT_STRING_NONE,
  QA_UI_SCRIPT_STRING_NULL,
  QA_UI_SCRIPT_STRING_TEXT
} qa_ui_script_string_kind;

qa_ui_script_string_kind qa_ui_script_cursor_string(
    qa_ui_script_cursor *cursor, char **out);

/** TS UiExternalScriptHost. */
typedef struct qa_ui_external_script_host {
  void *ctx;
  void (*run)(void *ctx, qa_ui_script_cursor *cursor,
              qa_ui_external_script_context context);
} qa_ui_external_script_host;

/** TS UiRuntimeContext. */
typedef enum qa_ui_runtime_context_kind {
  QA_UI_RUNTIME_CONTEXT_UI,    /* "ui" */
  QA_UI_RUNTIME_CONTEXT_CGAME  /* "cgame" */
} qa_ui_runtime_context_kind;

typedef void (*qa_ui_runtime_pause_fn)(void *ctx, bool paused);

typedef struct qa_ui_runtime_context {
  qa_ui_runtime_context_kind kind;
  qa_ui_runtime_bindings bindings; /* ui */
  qa_ui_runtime_pause_fn pause;    /* ui */
  void *pause_ctx;
} qa_ui_runtime_context;

/** TS UiRuntimeOptions print / cvarValue callbacks. */
typedef void (*qa_ui_runtime_print_fn)(void *ctx, const char *text);
typedef double (*qa_ui_runtime_cvar_value_fn)(void *ctx, const char *name);
typedef qa_vec4 (*qa_ui_runtime_team_color_fn)(void *ctx);

/** TS UiRuntimeOptions. definitions is copied by value (the
 * runtime borrows its menus/assets; the caller keeps ownership).
 * source_parser/print/cvar_value NULL select the TS defaults. */
typedef struct qa_ui_runtime_options {
  qa_ui_menu_definitions definitions;
  qa_common_parse_state *source_parser;
  qa_ui_runtime_print_fn print;
  void *print_ctx;
  qa_ui_runtime_cvar_value_fn cvar_value;
  void *cvar_value_ctx;
  qa_cvar_registry *cvars;
  qa_command_buffer *commands;
  qa_command_context command_context;
  qa_ui_runtime_resources resources;
  qa_q3_font_set fonts;
  qa_ui_widget_assets widget_assets;
  qa_picture_asset zero_picture;
  qa_ui_runtime_audio audio;
  qa_ui_runtime_cinematics cinematics;
  qa_ui_paint_model_fn paint_model;
  void *paint_model_ctx;
  qa_ui_runtime_context context;
  qa_ui_runtime_feeder feeder;
  qa_ui_runtime_owner_draw owner_draw;
  qa_ui_external_script_host external_script;
  qa_ui_runtime_team_color_fn get_team_color;
  void *get_team_color_ctx;
} qa_ui_runtime_options;

/** TS UiCapturedMenu (runtime-owned handle). */
typedef struct qa_ui_captured_menu {
  qa_ui_menu_definition *definition;
} qa_ui_captured_menu;

/** TS UiRuntimeItemBehaviorSnapshot. */
typedef enum qa_ui_runtime_behavior_snapshot_kind {
  QA_UI_SNAPSHOT_LIST_BOX, /* "list-box" */
  QA_UI_SNAPSHOT_EDIT,     /* "edit" */
  QA_UI_SNAPSHOT_OTHER     /* "other" */
} qa_ui_runtime_behavior_snapshot_kind;

typedef struct qa_ui_runtime_behavior_snapshot {
  qa_ui_runtime_behavior_snapshot_kind kind;
  double start_position;
  double end_position;
  double cursor_position;
  double draw_padding;
  double paint_offset;
} qa_ui_runtime_behavior_snapshot;

/** TS UiRuntimeItemSnapshot. Strings NULL is TS undefined. */
typedef struct qa_ui_runtime_item_snapshot {
  char *name;
  char *group;
  double flags;
  qa_rect rect;
  qa_rect client_rect;
  qa_vec4 fore_color;
  qa_vec4 back_color;
  qa_vec4 border_color;
  bool has_background;
  qa_picture_asset background;
  double cursor_position;
  double special;
  bool enabled;
  bool shown;
  qa_ui_runtime_behavior_snapshot behavior;
} qa_ui_runtime_item_snapshot;

/** TS UiRuntimeMenuSnapshot. */
typedef struct qa_ui_runtime_menu_snapshot {
  char *name;
  double flags;
  qa_rect rect;
  double cursor_item;
  qa_ui_runtime_item_snapshot *items;
  size_t item_count;
} qa_ui_runtime_menu_snapshot;

/** TS UiRuntimeSnapshot. Owned; destroy frees it. */
typedef struct qa_ui_runtime_snapshot {
  char *focused_menu;
  char **open_stack; /* entries NULL is TS undefined */
  size_t open_stack_count;
  qa_ui_runtime_menu_snapshot *menus;
  size_t menu_count;
} qa_ui_runtime_snapshot;

void qa_ui_runtime_snapshot_destroy(qa_ui_runtime_snapshot *snapshot);

/** TS UiRuntimeFrame. */
typedef struct qa_ui_runtime_frame {
  double time;
  double frame_time;
  const qa_draw2d *draw;
} qa_ui_runtime_frame;

typedef struct qa_ui_runtime qa_ui_runtime;

/** TS UiRuntime.create. NULL only on OOM. */
qa_ui_runtime *qa_ui_runtime_create(const qa_ui_runtime_options *options);
void qa_ui_runtime_destroy(qa_ui_runtime *runtime);

/** TS resetDefinitions("menus"|"strings"). */
typedef enum qa_ui_runtime_reset {
  QA_UI_RUNTIME_RESET_MENUS,
  QA_UI_RUNTIME_RESET_STRINGS
} qa_ui_runtime_reset;

void qa_ui_runtime_reset_definitions(qa_ui_runtime *runtime,
                                     qa_ui_runtime_reset reset);
void qa_ui_runtime_accept_menu_registration(
    qa_ui_runtime *runtime, const qa_ui_menu_registration_event *event);
void qa_ui_runtime_publish_menu_asset(
    qa_ui_runtime *runtime, const qa_ui_menu_asset_publication *event);
void qa_ui_runtime_append_menu(qa_ui_runtime *runtime,
                               qa_ui_menu_definition *definition,
                               qa_ui_menu_memory_ownership memory);
void qa_ui_runtime_assert_menu_memory(
    qa_ui_runtime *runtime, qa_ui_menu_memory_ownership next_memory);
void qa_ui_runtime_reload_definitions(
    qa_ui_runtime *runtime, const qa_ui_menu_definitions *definitions);

/** TS activate(name | null | (() => string)). has_name false with
 * name NULL is TS null; fn form calls the getter per menu. */
typedef const char *(*qa_ui_runtime_name_fn)(void *ctx);
bool qa_ui_runtime_activate(qa_ui_runtime *runtime, const char *name);
bool qa_ui_runtime_activate_null(qa_ui_runtime *runtime);
bool qa_ui_runtime_activate_with(qa_ui_runtime *runtime,
                                 qa_ui_runtime_name_fn name, void *ctx);
bool qa_ui_runtime_show(qa_ui_runtime *runtime, const char *name);
bool qa_ui_runtime_close(qa_ui_runtime *runtime, const char *name);
void qa_ui_runtime_close_all(qa_ui_runtime *runtime);
bool qa_ui_runtime_any_full_screen_visible(qa_ui_runtime *runtime);
bool qa_ui_runtime_pointer_move(qa_ui_runtime *runtime, double x, double y);
void qa_ui_runtime_set_display_cursor(qa_ui_runtime *runtime, double x,
                                      double y);
bool qa_ui_runtime_handle_key(qa_ui_runtime *runtime, qa_ui_key_event event,
                              double x, double y);
/* Borrowed; NULL is TS undefined. */
qa_ui_captured_menu *qa_ui_runtime_focused_menu_handle(
    qa_ui_runtime *runtime);
bool qa_ui_runtime_handle_captured_key(qa_ui_runtime *runtime,
                                       qa_ui_captured_menu *handle,
                                       qa_ui_key_event event);
void qa_ui_runtime_run_frame(qa_ui_runtime *runtime, qa_ui_runtime_frame frame,
                         double frames_per_second);
void qa_ui_runtime_cache_all(qa_ui_runtime *runtime);
void qa_ui_runtime_set_display_time(qa_ui_runtime *runtime, double time);
bool qa_ui_runtime_paint_named(qa_ui_runtime *runtime, const char *name,
                               qa_ui_runtime_frame frame, bool force);
void qa_ui_runtime_paint_captured(qa_ui_runtime *runtime,
                                  qa_ui_captured_menu *handle,
                                  qa_ui_runtime_frame frame, bool force);
bool qa_ui_runtime_clear_forced(qa_ui_runtime *runtime, const char *name);
void qa_ui_runtime_clear_captured_forced(qa_ui_runtime *runtime,
                                         qa_ui_captured_menu *handle);
/* Borrowed; NULL is TS undefined. */
qa_ui_captured_menu *qa_ui_runtime_menu_handle(qa_ui_runtime *runtime,
                                               const char *name);
qa_ui_captured_menu *qa_ui_runtime_capture_menu(qa_ui_runtime *runtime,
                                                double x, double y);
/* Borrowed; NULL is TS undefined. */
qa_ui_item_definition *qa_ui_runtime_hit_test_menu(
    qa_ui_runtime *runtime, qa_ui_captured_menu *handle, double x,
    double y);
/* item NULL is TS null. */
void qa_ui_runtime_set_item_mouse_over(qa_ui_runtime *runtime,
                                       qa_ui_item_definition *item,
                                       bool focused);
void qa_ui_runtime_paint_item_image(qa_ui_runtime *runtime,
                                    qa_ui_item_definition *item,
                                    const qa_draw2d *draw);
void qa_ui_runtime_move_captured_menu(qa_ui_runtime *runtime,
                                      qa_ui_captured_menu *handle,
                                      double delta_x, double delta_y);
/* menu_name NULL selects the focused menu (TS optional). */
void qa_ui_runtime_set_feeder_selection(qa_ui_runtime *runtime,
                                        double feeder, double index,
                                        const char *menu_name);
void qa_ui_runtime_set_captured_feeder_selection(
    qa_ui_runtime *runtime, qa_ui_captured_menu *handle, double feeder,
    double index);
void qa_ui_runtime_scroll_feeder(qa_ui_runtime *runtime, double feeder,
                                 bool down, const char *menu_name);
void qa_ui_runtime_scroll_captured_feeder(qa_ui_runtime *runtime,
                                          qa_ui_captured_menu *handle,
                                          double feeder, bool down);
bool qa_ui_runtime_binding_pending(qa_ui_runtime *runtime);
void qa_ui_runtime_reload_bindings(qa_ui_runtime *runtime);
void qa_ui_runtime_reset_bindings(qa_ui_runtime *runtime);
void qa_ui_runtime_apply_bindings(qa_ui_runtime *runtime);

/** TS cursorType result. */
typedef enum qa_ui_cursor_type {
  QA_UI_CURSOR_ARROW, /* "arrow" */
  QA_UI_CURSOR_SIZER  /* "sizer" */
} qa_ui_cursor_type;

qa_ui_cursor_type qa_ui_runtime_cursor_type(qa_ui_runtime *runtime,
                                            double x, double y);
void qa_ui_runtime_run_menu_script(qa_ui_runtime *runtime,
                                   const char *menu_name,
                                   qa_ui_menu_script *script);
void qa_ui_runtime_run_item_script(qa_ui_runtime *runtime,
                                   const char *menu_name,
                                   const char *item_name,
                                   qa_ui_menu_script *script);
double qa_ui_runtime_menu_count(qa_ui_runtime *runtime);
qa_ui_runtime_snapshot qa_ui_runtime_take_snapshot(qa_ui_runtime *runtime);
void qa_ui_runtime_dispose(qa_ui_runtime *runtime);
void qa_ui_runtime_retire(qa_ui_runtime *runtime);

#endif
