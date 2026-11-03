/* Port of src/content/q1/base/travel.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_TRAVEL_H
#define QA_CONTENT_Q1_BASE_TRAVEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q1/foundation/entity_services.h"

/* TS Q1TravelState. inventory/extensions are owned lists; extension
 * bytes are owned copies. */
typedef struct qa_q1_travel_extension {
  char *id; uint8_t *bytes; size_t length;
} qa_q1_travel_extension;
typedef struct qa_q1_travel_state {
  double health; double max_health;
  qa_armor_state armor;
  qa_inventory_entry *inventory; size_t inventory_count;
  qa_item_id *inventory_items; /* owned item strings, parallel */
  const char *weapon; /* TS Q1Weapon */
  qa_q1_travel_extension *extensions; size_t extension_count;
} qa_q1_travel_state;
void qa_q1_travel_state_destroy(qa_q1_travel_state *state);

/* TS Pick<Q1FoundationOptions, "edition"|"skill"|"deathmatch">. */
qa_q1_travel_state qa_q1_new_travel(const qa_q1_options *options);
/* weapon NULL / max_health NULL = TS defaults. */
typedef struct qa_q1_travel_policy { bool has_reset_in_deathmatch; bool reset_in_deathmatch; } qa_q1_travel_policy;
/* TS captureQ1TravelExtensions; out entries are owned (id, bytes). */
size_t qa_q1_capture_travel_extensions(qa_q1_services *game, qa_owned_actor actor,
                                       qa_q1_travel_extension *out, size_t capacity);
qa_q1_travel_state qa_q1_capture_travel(qa_q1_services *game, qa_owned_actor actor,
                                        const char *weapon, const double *max_health,
                                        qa_q1_travel_policy policy);
/* decode returns a new state or a shallow copy sharing state's lists
 * (TS returns state itself); destroy only when returned fresh flag set:
 * the result always owns fresh lists except extensions, which are
 * shared with the input (do not destroy both). */
qa_q1_travel_state qa_q1_decode_travel(qa_q1_services *game, const qa_q1_travel_state *state, double server_flags);
void qa_q1_admit_travel(qa_q1_services *game, qa_owned_actor actor, const qa_q1_travel_state *state);

#endif
