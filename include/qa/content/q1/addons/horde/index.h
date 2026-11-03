/* Port of src/content/q1/addons/horde/index.ts (Q1Horde). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_HORDE_INDEX_H
#define QA_CONTENT_Q1_ADDONS_HORDE_INDEX_H
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/horde/types.h"

/* TS Q1Horde. Registered with the game (kill-count rule, weapon rules,
 * player extension, named callbacks and spawns all carry it as ctx), so
 * destroy it after the last use of the game. */
typedef struct qa_q1ad_horde {
  qa_q1ad_context *context;
  qa_q1ad_horde_services services;
  double last_corpse_scan; /* private lastCorpseScan */
  qa_q1_weapon_rules weapon_rules;
  qa_q1_player_extension player_extension;
} qa_q1ad_horde;

/* TS registerQ1Horde(context, services) / new Q1Horde: heap object, NULL only on OOM. */
qa_q1ad_horde *qa_q1ad_register_horde(qa_q1ad_context *context, qa_q1ad_horde_services services);
void qa_q1ad_horde_destroy(qa_q1ad_horde *horde);

/* get manager(): NULL = TS null. */
qa_q1_actor *qa_q1ad_horde_manager(qa_q1ad_horde *horde);
void qa_q1ad_horde_schedule(qa_q1ad_horde *horde, qa_q1_actor *entity, const char *name, double delay);
/* livingPlayers()/livingMonsters(): write up to capacity, return the full count. */
size_t qa_q1ad_horde_living_players(qa_q1ad_horde *horde, qa_actor_id *out, size_t capacity);
size_t qa_q1ad_horde_living_monsters(qa_q1ad_horde *horde, qa_q1_actor **out, size_t capacity);
/* target(): false = TS null. */
bool qa_q1ad_horde_target(qa_q1ad_horde *horde, qa_actor_id *out);
bool qa_q1ad_horde_blocked(qa_q1ad_horde *horde, qa_q1_actor *point);
/* type: "normal"|"ranged"|"flying"|"boss"; NULL = TS null. */
qa_q1_actor *qa_q1ad_horde_find_spawn(qa_q1ad_horde *horde, const char *type);
qa_q1_actor *qa_q1ad_horde_spawn_monster(qa_q1ad_horde *horde, const char *kind, qa_vec3 origin, qa_vec3 angles,
                                         qa_q1_actor *owner);
bool qa_q1ad_horde_prepare(qa_q1ad_horde *horde, qa_q1_actor *manager);
void qa_q1ad_horde_spawn_wave(qa_q1ad_horde *horde, qa_q1_actor *manager);
void qa_q1ad_horde_check_wave(qa_q1ad_horde *horde, qa_q1_actor *manager);
void qa_q1ad_horde_remote_wavecheck(qa_q1ad_horde *horde);
void qa_q1ad_horde_get_key(qa_q1ad_horde *horde, qa_q1_actor *manager);
void qa_q1ad_horde_restore_keys(qa_q1ad_horde *horde, qa_actor_id player);
/* key: "silver"|"gold"; delta: 1 | -1. */
void qa_q1ad_horde_change_keys(qa_q1ad_horde *horde, const char *key, int delta);
void qa_q1ad_horde_teammate_killed(qa_q1ad_horde *horde, qa_actor_id attacker);
/** Called after the source dead-player release/press gate; true consumes ordinary respawn. */
bool qa_q1ad_horde_request_respawn(qa_q1ad_horde *horde);
void qa_q1ad_horde_restart_after_defeat(qa_q1ad_horde *horde);
#endif
