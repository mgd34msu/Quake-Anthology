/*
 * Port of src/contracts/ui.ts (types only): seat input/UI state, draw
 * commands, controls/menus, legacy Q2/Q3 client-module exports, and
 * weapon HUD shapes. TS closures become fn ptr + ctx; TS
 * Promise-returning Q3 exports are synchronous in C (noted per the
 * porting conventions on async flows).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_UI_H
#define QA_CONTRACTS_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/render.h"
#include "qa/contracts/time.h"

typedef const char *qa_ui_control_id; /* `ui:...:...` */
typedef const char *qa_ui_menu_id;    /* `menu:...:...` */

/* TS InputAction (exact strings): "attack"|"jump"|"forward"|"back"|
 * "move-left"|"move-right"|"move-up"|"move-down"|"use"|"crouch"|
 * "walk"|"scores"|"next-weapon"|"previous-weapon"|"menu". */
typedef const char *qa_input_action;
/* TS ControllerAxis: "left-x"|"left-y"|"right-x"|"right-y"|
 * "left-trigger"|"right-trigger". */
typedef const char *qa_controller_axis;

/* TS PhysicalInput union on kind. */
typedef enum qa_physical_input_kind {
  QA_PHYSICAL_INPUT_KEY, QA_PHYSICAL_INPUT_MOUSE_BUTTON,
  QA_PHYSICAL_INPUT_CONTROLLER_BUTTON, QA_PHYSICAL_INPUT_CONTROLLER_AXIS
} qa_physical_input_kind;
typedef struct qa_physical_input {
  qa_physical_input_kind kind;
  double code;                  /* key */
  double button;                /* mouse/controller button */
  double device;                /* controller */
  qa_controller_axis axis;
  const char *direction;        /* "positive" | "negative" */
} qa_physical_input;

/* TS InputBindingTarget: {kind:"action",action} | {kind:"command",text}. */
typedef struct qa_input_binding_target {
  bool is_action;
  qa_input_action action;
  const char *text;
} qa_input_binding_target;

typedef struct qa_input_binding {
  qa_physical_input input;
  qa_input_binding_target target;
} qa_input_binding;

/* TS SeatInputEvent: {seat,timeMilliseconds} & union on kind:
 * "key"|"text"|"mouse-motion"|"mouse-button"|"mouse-wheel"|
 * "controller-button"|"controller-axis"|"focus". */
typedef struct qa_seat_input_event {
  qa_seat_id seat;
  double time_milliseconds;
  const char *kind;
  double code;
  bool down, repeat;
  const char *text;
  qa_vec2 position, delta;
  double button, device;
  qa_controller_axis axis;
  double value;
  bool focused;
} qa_seat_input_event;

/* TS SeatInputFocus union on kind: "game" | "menu"(menu,control|null)
 * | "console" | "chat"(team,text). */
typedef struct qa_seat_input_focus {
  const char *kind;
  qa_ui_menu_id menu;
  qa_ui_control_id control; /* nullable */
  bool team;
  const char *text;
} qa_seat_input_focus;

typedef struct qa_ui_notification {
  double sequence;
  const char *text;
  bool chat;
  qa_source_time starts, duration;
} qa_ui_notification;

typedef struct qa_center_print_state {
  const char *text;
  qa_source_time starts, duration;
  bool instant;
  bool has_character_milliseconds;
  double character_milliseconds;
} qa_center_print_state;

typedef struct qa_seat_ui_state {
  qa_seat_id seat;
  qa_seat_input_focus focus;
  qa_vec2 cursor;
  const qa_input_binding *bindings; size_t binding_count;
  const qa_ui_notification *notifications; size_t notification_count;
  const qa_center_print_state *center_print; /* nullable */
  bool show_scores;
} qa_seat_ui_state;

typedef struct qa_seat_presentation_binding {
  qa_seat_id seat;
  qa_client_id client;
  qa_rect viewport, safe_area;
  double hud_scale;
  qa_presentation_selection presentation;
} qa_seat_presentation_binding;

/* TS UiDrawCommand union on kind: "fill" | "image" | "text"(align
 * "left"|"center"|"right") | "clip"(rect|null). */
typedef enum qa_ui_draw_command_kind {
  QA_UI_DRAW_FILL, QA_UI_DRAW_IMAGE, QA_UI_DRAW_TEXT, QA_UI_DRAW_CLIP
} qa_ui_draw_command_kind;
typedef struct qa_ui_draw_command {
  qa_ui_draw_command_kind kind;
  qa_rect rect;
  qa_vec4 color;
  qa_resource_id resource;
  qa_vec2 tex_coords[2];
  qa_vec2 origin;
  const char *text;
  qa_resource_id font;
  double scale;
  const char *align;
  bool shadow;
  bool has_clip_rect;
} qa_ui_draw_command;

typedef struct qa_ui_draw_context {
  qa_seat_presentation_binding binding;
  double time_milliseconds;
} qa_ui_draw_context;

typedef struct qa_ui_choice { const char *id; const char *label; } qa_ui_choice;

/* UiListRow.action: optional {label, activate(seat)}. */
typedef struct qa_ui_list_row_action {
  const char *label;
  void *ctx;
  void (*activate)(void *ctx, qa_seat_id seat);
} qa_ui_list_row_action;
typedef struct qa_ui_list_row {
  const qa_ui_list_row_action *action; /* optional */
  const char *id;
  qa_string_view_list cells;
  qa_resource_id image; /* nullable */
  bool enabled;
} qa_ui_list_row;

/* TS UiControl = UiControlBase & union on kind: "button"|"toggle"|
 * "slider"|"text-entry"|"choice"|"list"|"owner-draw". All TS
 * callbacks share one ctx. draw() returns pointer + count. */
typedef struct qa_ui_control {
  qa_ui_control_id id;
  const char *label;
  qa_rect rect;
  bool enabled, visible;
  const char *kind;
  void *ctx;
  void (*activate_seat)(void *ctx, qa_seat_id seat);                 /* button */
  bool checked;                                                      /* toggle */
  void (*change_checked)(void *ctx, qa_seat_id seat, bool checked);
  double minimum, maximum, step, value;                              /* slider */
  const char *value_label;                                           /* optional */
  void (*change_value)(void *ctx, qa_seat_id seat, double value);
  bool has_masked;                                                   /* text-entry */
  bool masked;
  const char *text;
  double maximum_length;
  void (*change_text)(void *ctx, qa_seat_id seat, const char *text);
  void (*submit_text)(void *ctx, qa_seat_id seat, const char *text);
  const qa_ui_choice *choices; size_t choice_count;                  /* choice */
  const char *selected;                                              /* nullable */
  void (*select_choice)(void *ctx, qa_seat_id seat, const char *choice);
  bool has_row_height;                                               /* list */
  double row_height;
  qa_double_view column_widths;
  void (*activate_row)(void *ctx, qa_seat_id seat, const char *row); /* optional */
  const qa_ui_list_row *rows; size_t row_count;
  void (*select_row)(void *ctx, qa_seat_id seat, const char *row);
  qa_provider_id owner;                                              /* owner-draw */
  double source_id;
  const qa_ui_draw_command *(*draw)(void *ctx, qa_ui_draw_context context,
                                    size_t *out_count);
  bool (*key)(void *ctx, qa_seat_id seat, double code, bool down);
} qa_ui_control;

typedef struct qa_ui_menu_scroll {
  qa_rect rect;
  double content_height;
  const qa_ui_control_id *controls;
  size_t control_count;
} qa_ui_menu_scroll;

typedef struct qa_ui_menu {
  const qa_ui_menu_scroll *scroll; /* optional */
  qa_ui_menu_id id;
  const char *title;
  bool full_screen;
  const qa_ui_control *controls; size_t control_count;
  void *ctx;
  void (*open)(void *ctx, qa_seat_id seat);
  void (*close)(void *ctx, qa_seat_id seat);
} qa_ui_menu;

/* TS LegacyUiScript kind is the literal "q3-menu-script". */
typedef struct qa_legacy_ui_script {
  qa_provider_id module;
  const char *source;
} qa_legacy_ui_script;

/** TS SeatUiController (function-valued fields share one ctx). */
typedef struct qa_seat_ui_controller {
  void *ctx;
  qa_seat_id seat;
  qa_seat_ui_state (*state)(void *ctx);
  bool (*input)(void *ctx, qa_seat_input_event event);
  void (*open_menu)(void *ctx, qa_ui_menu_id menu);
  void (*close_menu)(void *ctx);
  void (*execute_script)(void *ctx, qa_legacy_ui_script script);
  const qa_ui_draw_command *(*draw)(void *ctx, qa_ui_draw_context context,
                                    size_t *out_count);
} qa_seat_ui_controller;

typedef struct qa_hud_frame {
  qa_seat_presentation_binding binding;
  qa_source_time time;
  qa_network_snapshot snapshot;
  qa_seat_ui_state ui;
} qa_hud_frame;

/* TS StereoView = "center"|"left"|"right"; Q3CgameEventHandling =
 * "none"|"team-menu"|"scoreboard"|"edit-hud" (exact strings). */
typedef const char *qa_stereo_view;
typedef const char *qa_q3_cgame_event_handling;

/* TS number | null results. */
typedef struct qa_number_result { bool has_value; double value; } qa_number_result;

/** TS Q3CgameExports. C accommodation: TS Promise<T> returns are
 * synchronous T here. */
typedef struct qa_q3_cgame_exports {
  void *ctx;
  qa_q3_api_identity api; /* kind "q3-cgame" */
  qa_seat_id seat;
  void (*init)(void *ctx, double server_message_number,
               double server_command_sequence, double client_number);
  void (*shutdown)(void *ctx);
  bool (*console_command)(void *ctx, qa_string_view_list arguments);
  void (*draw_active_frame)(void *ctx, double server_time_milliseconds,
                            qa_stereo_view stereo_view, bool demo_playback);
  qa_number_result (*crosshair_player)(void *ctx);
  qa_number_result (*last_attacker)(void *ctx);
  void (*key_event)(void *ctx, double key, bool down);
  void (*mouse_event)(void *ctx, double dx, double dy);
  void (*event_handling)(void *ctx, qa_q3_cgame_event_handling mode);
} qa_q3_cgame_exports;

/* TS Q3MenuCommand = "none"|"main"|"ingame"|"need-cd"|"bad-cd-key"|
 * "team"|"postgame". */
typedef const char *qa_q3_menu_command;

/** TS Q3UiExports (promises synchronous in C, as above). */
typedef struct qa_q3_ui_exports {
  void *ctx;
  qa_q3_api_identity api; /* kind "q3-ui" */
  qa_seat_id seat;
  void (*init)(void *ctx, bool connecting);
  void (*shutdown)(void *ctx);
  void (*key_event)(void *ctx, double key, bool down);
  void (*mouse_event)(void *ctx, double dx, double dy);
  void (*refresh)(void *ctx, double real_time_milliseconds);
  bool (*is_fullscreen)(void *ctx);
  void (*set_active_menu)(void *ctx, qa_q3_menu_command menu);
  bool (*console_command)(void *ctx, double real_time_milliseconds,
                          qa_string_view_list arguments);
  void (*draw_connect_screen)(void *ctx, bool overlay);
  bool (*has_unique_cd_key)(void *ctx);
} qa_q3_ui_exports;

/* TS ArsenalAmmoWarning = "none" | "low" | "empty". */
typedef const char *qa_arsenal_ammo_warning;

/* WeaponHudStatus.ammo: {kind:"unmetered"} |
 * {kind:"finite", item, count, hasAmmoToStart, low} (the finite
 * fields are flattened below, gated by ammo_is_finite). */
typedef struct qa_weapon_hud_status {
  qa_provider_reference source;
  qa_item_id item;
  const char *label;
  bool ammo_is_finite;
  qa_item_id ammo_item;
  double ammo_count;
  bool has_ammo_to_start;
  bool low;
} qa_weapon_hud_status;

/* TS WeaponHudIcon union on kind: "wad-picture"(resource,lump) |
 * "image"(resource) | "shader"(content,name). */
typedef enum qa_weapon_hud_icon_kind {
  QA_WEAPON_HUD_ICON_WAD_PICTURE, QA_WEAPON_HUD_ICON_IMAGE, QA_WEAPON_HUD_ICON_SHADER
} qa_weapon_hud_icon_kind;
typedef struct qa_weapon_hud_icon {
  qa_weapon_hud_icon_kind kind;
  qa_resource_request resource;
  const char *lump;
  qa_content_id content;
  const char *name;
} qa_weapon_hud_icon;

#endif
