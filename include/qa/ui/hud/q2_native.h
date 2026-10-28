/*
 * Port of src/ui/hud/q2-native.ts: NativeQ2HudFrame/Operation/Arsenal,
 * q2LayoutOperations, q2NativeHudOperations, nativeQ2HudStat.
 * C accommodations:
 *  - TS ReadonlyMap configstrings is a borrowed entry array.
 *  - Operations are returned in an owned result; name/text strings
 *    are owned by the result and freed with _free. The operation
 *    kind is an enum (TS kind strings in comments).
 *  - Plain TS Errors and RangeErrors abort via qa_range_error with
 *    the identical message.
 *  - Q2ApplicationLayout and NativeInventoryReadout use their real
 *    owner types (q2_layout.h / native_primary_inventory.h).
 *  - NativeQ2HudEnvironment is defined in q2_rerelease_layout.h
 *    (its TS owner) and only forward-declared here.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_HUD_Q2_NATIVE_H
#define QA_UI_HUD_Q2_NATIVE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/compat/q2/native_primary_inventory.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"

typedef struct qa_native_q2_hud_environment qa_native_q2_hud_environment;

/** One TS configstrings Map entry. */
typedef struct qa_native_q2_config_string {
  double index;
  const char *value;
} qa_native_q2_config_string;

/** TS NativeQ2HudFrame. Arrays borrowed. */
typedef struct qa_native_q2_hud_frame {
  qa_q2_protocol_identity protocol;
  const double *stats;
  size_t stat_count;
  const qa_native_q2_config_string *configstrings;
  size_t configstring_count;
  const char *layout;
  const double *inventory;
  size_t inventory_count;
  double player_number;
  double server_frame;
  double time_milliseconds;
  bool has_frame_time_milliseconds;
  double frame_time_milliseconds;
} qa_native_q2_hud_frame;

/** TS NativeQ2HudOperation kind. */
typedef enum qa_native_q2_hud_operation_kind {
  QA_Q2_HUD_OP_FONT_TEXT,       /* "font-text" */
  QA_Q2_HUD_OP_SIZED_PICTURE,   /* "sized-picture" */
  QA_Q2_HUD_OP_FILL,            /* "fill" */
  QA_Q2_HUD_OP_PICTURE,         /* "picture" */
  QA_Q2_HUD_OP_ARSENAL_PICTURE, /* "arsenal-picture" */
  QA_Q2_HUD_OP_TEXT             /* "text" */
} qa_native_q2_hud_operation_kind;

/** TS NativeQ2HudOperation. has_anchor_before is TS anchor:"before";
 * has_shadow/has_xor mark the optional text fields. */
typedef struct qa_native_q2_hud_operation {
  qa_native_q2_hud_operation_kind kind;
  double x, y, width, height;
  const char *text; /* font-text/text, result-owned */
  const char *name; /* picture kinds, result-owned */
  bool alternate;
  qa_vec4 color; /* fill */
  qa_resource_id resource; /* arsenal-picture, borrowed */
  double aspect;
  bool has_anchor_before;
  bool has_shadow, shadow;
  bool has_xor, xor_;
} qa_native_q2_hud_operation;

/** Owned operations result; free with _free. Also the builder used
 * by q2_rerelease_layout.c (push helpers below). */
typedef struct qa_native_q2_hud_operations {
  qa_native_q2_hud_operation *operations;
  size_t count, capacity;
  char **owned;
  size_t owned_count, owned_capacity;
} qa_native_q2_hud_operations;

void qa_native_q2_hud_operations_free(qa_native_q2_hud_operations *result);
/* Builder helpers (shared with q2-rerelease-layout port). False only
 * on OOM. _own takes ownership of a malloc'd string into the pool. */
bool qa_native_q2_hud_ops_push(qa_native_q2_hud_operations *out,
                               qa_native_q2_hud_operation operation);
char *qa_native_q2_hud_ops_own(qa_native_q2_hud_operations *out, char *text);
char *qa_native_q2_hud_ops_dup(qa_native_q2_hud_operations *out,
                               const char *text);

/* TS NativeInventoryReadout row/readout: real owner types from
 * compat/q2/native_primary_inventory.h. */
typedef qa_compat_q2_native_inventory_row qa_native_q2_inventory_item;
typedef qa_compat_q2_native_inventory_readout qa_native_q2_inventory_readout;

/** TS NativeQ2HudArsenal. has_ flags mark TS undefined fields;
 * ammo has_count=false / has_icon=false are TS null. */
typedef struct qa_native_q2_hud_arsenal {
  bool has_selected_item;
  const char *selected_label;
  const char *selected_localized_label;
  bool selected_has_icon;
  qa_resource_id selected_icon_resource;
  double selected_icon_aspect;
  bool has_inventory;
  qa_native_q2_inventory_readout inventory;
  bool has_ammunition;
  bool ammo_has_count;
  double ammo_count;
  bool ammo_has_icon;
  qa_resource_id ammo_icon_resource;
  double ammo_icon_aspect;
} qa_native_q2_hud_arsenal;

/* TS Q2ApplicationLayout: real owner type from
 * app/bootstrap/network/q2_layout.h (has_n64_physics false = TS null). */
typedef qa_app_q2_layout qa_ui_hud_q2_app_layout;
qa_ui_hud_q2_app_layout qa_ui_hud_q2_application_layout_for(
    qa_q2_protocol_identity protocol);

/** Configstring lookup shared with q2-rerelease-layout port:
 * borrowed value or "" (TS `.get(k) ?? ""`). */
const char *qa_native_q2_config_string_at(const qa_native_q2_hud_frame *frame,
                                       double index);
bool qa_native_q2_is_rerelease(qa_q2_protocol_identity protocol);

/** TS nativeQ2HudStat. Aborts (RangeError) outside playerstate. */
double qa_native_q2_hud_stat(const qa_native_q2_hud_frame *frame, double index,
                             const qa_native_q2_hud_arsenal *arsenal);

/* TS q2LayoutOperations. arsenal/environment NULL are TS undefined.
 * Returns false only on OOM. */
bool qa_q2_layout_operations(const char *source,
                             const qa_native_q2_hud_frame *frame, double width,
                             double height,
                             const qa_native_q2_hud_arsenal *arsenal,
                             qa_native_q2_hud_environment *environment,
                             qa_native_q2_hud_operations *out);

/** TS q2NativeHudOperations binding callback: borrowed binding text
 * ("" when none). mode: "layout-overlay"|"replace-status". */
typedef const char *(*qa_q2_hud_binding_fn)(void *ctx, const char *command);
bool qa_q2_native_hud_operations(const qa_native_q2_hud_frame *frame,
                                 double width, double height,
                                 qa_q2_hud_binding_fn binding, void *binding_ctx,
                                 const char *mode,
                                 const qa_native_q2_hud_arsenal *arsenal,
                                 qa_native_q2_hud_environment *environment,
                                 qa_native_q2_hud_operations *out);

#endif
