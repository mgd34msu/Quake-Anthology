/* Port of src/ui/hud/q1-wheel.ts: rerelease wwheel.txt grammar.
 * Nullable TS fields carry has_ flags; strings are owned by the
 * parse result. unknown keeps insertion order (TS Map).
 * GPL-2.0-or-later. */
#ifndef QA_UI_HUD_Q1_WHEEL_H
#define QA_UI_HUD_Q1_WHEEL_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/ui/hud/wheel.h"

/** One TS ReadonlyMap entry of Q1WheelSlot.unknown. */
typedef struct qa_ui_q1_wheel_unknown {
  char *key;
  char **values;
  size_t value_count;
} qa_ui_q1_wheel_unknown;

/** TS Q1WheelSlot. */
typedef struct qa_ui_q1_wheel_slot {
  double slot;
  bool has_impulse;
  double impulse;
  char *icon;          /* nullable */
  char *selected_icon; /* nullable */
  char *ammo_icon;     /* nullable */
  bool has_entity_variable_byte_offset;
  double entity_variable_byte_offset;
  bool has_weapon_bits;
  double weapon_bits;
  qa_ui_q1_wheel_unknown *unknown;
  size_t unknown_count;
} qa_ui_q1_wheel_slot;

/** Owned result of qa_ui_parse_q1_weapon_wheel. */
typedef struct qa_ui_q1_wheel_parse {
  qa_ui_q1_wheel_slot *slots;
  size_t slot_count;
  char **errors;
  size_t error_count;
} qa_ui_q1_wheel_parse;

void qa_ui_q1_wheel_parse_free(qa_ui_q1_wheel_parse *result);

/* TS parseQ1WeaponWheel(text). Returns false only on OOM. */
bool qa_ui_parse_q1_weapon_wheel(const char *text,
                                 qa_ui_q1_wheel_parse *out);

/** TS Q1WheelState (one shared ctx). image NULL return is TS null. */
typedef struct qa_ui_q1_wheel_state {
  void *ctx;
  double items;
  double (*entity_float)(void *ctx, double byte_offset);
  const char *(*label)(void *ctx, const qa_ui_q1_wheel_slot *slot);
  qa_resource_id (*image)(void *ctx, const char *path); /* nullable */
} qa_ui_q1_wheel_state;

/** Owned result of qa_ui_q1_wheel_items; ids are owned strings. */
typedef struct qa_ui_q1_wheel_items {
  qa_ui_wheel_item *items;
  char **owned_ids;
  size_t count;
} qa_ui_q1_wheel_items;

void qa_ui_q1_wheel_items_free(qa_ui_q1_wheel_items *result);

/* TS q1WheelItems(slots, state). Returns false only on OOM.
 * Labels borrow from the state's label callback. */
bool qa_ui_q1_wheel_items_for(const qa_ui_q1_wheel_slot *slots,
                              size_t slot_count,
                              const qa_ui_q1_wheel_state *state,
                              qa_ui_q1_wheel_items *out);

#endif
