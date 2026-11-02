/* Port of src/content/q1/missionpacks/travel.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_TRAVEL_H
#define QA_CONTENT_Q1_MISSIONPACKS_TRAVEL_H
#include "qa/content/q1/missionpacks/types.h"

/* Mirror of Q1TravelState (TS owner: src/content/q1/base/travel.ts;
 * landed owned-list form is qa/content/q1/base/travel.h). inventory
 * is a caller-owned fixed list; extensions are borrowed byte blobs. */
#define QA_Q1MP_TRAVEL_ITEMS 32
#define QA_Q1MP_TRAVEL_EXTS 8
typedef struct qa_q1mp_travel_extension { const char *id; const uint8_t *bytes; size_t length; } qa_q1mp_travel_extension;
typedef struct qa_q1mp_travel_state {
  double health; double max_health; qa_armor_state armor;
  qa_inventory_entry inventory[QA_Q1MP_TRAVEL_ITEMS]; size_t inventory_count;
  const char *weapon;
  qa_q1mp_travel_extension extensions[QA_Q1MP_TRAVEL_EXTS]; size_t extension_count;
} qa_q1mp_travel_state;
/* Base travel (same TS owner) via the landed base/travel port,
 * converting to/from this fixed-array state. */
qa_q1mp_travel_state qa_q1mp_ext_new_q1_travel(const qa_q1b_options *options);
qa_q1mp_travel_state qa_q1mp_ext_capture_q1_travel(qa_q1mp_game *game, qa_owned_actor actor, const char *weapon, double max_health, bool reset_in_deathmatch);
void qa_q1mp_ext_admit_q1_travel(qa_q1mp_game *game, qa_owned_actor actor, const qa_q1mp_travel_state *state);
qa_q1mp_travel_state qa_q1mp_new_mission_pack_travel(qa_q1mp_game *game, qa_q1mp_pack pack);
qa_q1mp_travel_state qa_q1mp_capture_mission_pack_travel(qa_q1mp_game *game, qa_owned_actor actor, qa_q1mp_pack pack);
qa_q1mp_travel_state qa_q1mp_decode_mission_pack_travel(qa_q1mp_game *game, qa_q1mp_travel_state state, double server_flags, qa_q1mp_pack pack);
void qa_q1mp_admit_mission_pack_travel(qa_q1mp_game *game, qa_owned_actor actor, qa_q1mp_travel_state state, qa_q1mp_pack pack);

#endif
