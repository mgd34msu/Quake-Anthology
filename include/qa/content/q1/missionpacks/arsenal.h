/* Port of src/content/q1/missionpacks/arsenal.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_ARSENAL_H
#define QA_CONTENT_Q1_MISSIONPACKS_ARSENAL_H
#include "qa/content/q1/missionpacks/grapple.h"
#include "qa/content/q1/missionpacks/items.h"
#include "qa/content/q1/missionpacks/player.h"
#include "qa/content/q1/missionpacks/types.h"

/* TS MissionPackArsenal. Must not move after init (the game borrows its
 * members). grapple is NULL for hipnotic (TS constructs it only for rogue
 * and keeps no reference; C keeps the storage alive). */
typedef struct qa_q1mp_mission_pack_arsenal {
  qa_q1_services *game;
  qa_q1mp_pack pack;
  qa_q1mp_mission_pack_players players;
  qa_q1mp_rogue_grapple grapple_storage; qa_q1mp_rogue_grapple *grapple;
  qa_q1mp_mission_item_services items;
  bool has_charmer; qa_actor_id charmer;
} qa_q1mp_mission_pack_arsenal;
void qa_q1mp_register_hipnotic_weapons(qa_q1_services *game);
void qa_q1mp_mission_pack_arsenal_init(qa_q1mp_mission_pack_arsenal *arsenal, qa_q1_services *game, qa_q1mp_pack pack);
void qa_q1mp_mission_pack_arsenal_destroy(qa_q1mp_mission_pack_arsenal *arsenal);
/* TS registerMissionPackArsenal: initializes the caller's storage. */
qa_q1mp_mission_pack_arsenal *qa_q1mp_register_mission_pack_arsenal(qa_q1_services *game, qa_q1mp_pack pack, qa_q1mp_mission_pack_arsenal *out);
bool qa_q1mp_arsenal_impulse(qa_q1mp_mission_pack_arsenal *arsenal, qa_actor_id actor, double impulse);
/* TS hornCharmer getter; false = TS null. */
bool qa_q1mp_arsenal_horn_charmer(const qa_q1mp_mission_pack_arsenal *arsenal, qa_actor_id *out);

#endif
