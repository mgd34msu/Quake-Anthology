/* Port of src/content/q1/addons/horde/index.ts (Q1Horde class).
 * Header lives beside siblings; the umbrella horde.h mirrors the
 * barrel role. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_HORDE_INDEX_H
#define QA_CONTENT_Q1_ADDONS_HORDE_INDEX_H
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/horde/types.h"
typedef struct qa_q1ad_horde {
  qa_q1ad_context *context;
  qa_q1ad_horde_services services;
} qa_q1ad_horde;
void qa_q1ad_horde_init(qa_q1ad_horde *horde, qa_q1ad_context *context, qa_q1ad_horde_services services);
qa_q1ad_horde *qa_q1ad_register_horde(qa_q1ad_horde *horde, qa_q1ad_context *context, qa_q1ad_horde_services services);
qa_q1ad_actor *qa_q1ad_horde_manager(qa_q1ad_horde *horde);
void qa_q1ad_horde_schedule(qa_q1ad_horde *horde, qa_q1ad_actor *entity, const char *name, double delay);
size_t qa_q1ad_horde_living_players(qa_q1ad_horde *horde, qa_actor_id *out, size_t capacity);
size_t qa_q1ad_horde_living_monsters(qa_q1ad_horde *horde, qa_q1ad_actor **out, size_t capacity);
bool qa_q1ad_horde_target(qa_q1ad_horde *horde, qa_actor_id *out);
bool qa_q1ad_horde_blocked(qa_q1ad_horde *horde, qa_q1ad_actor *point);
qa_q1ad_actor *qa_q1ad_horde_find_spawn(qa_q1ad_horde *horde, const char *type);
qa_q1ad_actor *qa_q1ad_horde_spawn_monster(qa_q1ad_horde *horde, const char *kind, qa_vec3 origin, qa_vec3 angles, qa_q1ad_actor *owner);
bool qa_q1ad_horde_prepare(qa_q1ad_horde *horde, qa_q1ad_actor *manager);
void qa_q1ad_horde_spawn_wave(qa_q1ad_horde *horde, qa_q1ad_actor *manager);
void qa_q1ad_horde_check_wave(qa_q1ad_horde *horde, qa_q1ad_actor *manager);
void qa_q1ad_horde_remote_wavecheck(qa_q1ad_horde *horde);
void qa_q1ad_horde_get_key(qa_q1ad_horde *horde, qa_q1ad_actor *manager);
void qa_q1ad_horde_restore_keys(qa_q1ad_horde *horde, qa_actor_id player);
void qa_q1ad_horde_change_keys(qa_q1ad_horde *horde, const char *key, int delta);
void qa_q1ad_horde_teammate_killed(qa_q1ad_horde *horde, qa_actor_id attacker);
bool qa_q1ad_horde_request_respawn(qa_q1ad_horde *horde);
void qa_q1ad_horde_restart_after_defeat(qa_q1ad_horde *horde);
/* Rule closures registered on the host (kill-count, weapon, player ext). */
bool qa_q1ad_horde_kill_count_rule(qa_q1ad_horde *horde, qa_q1ad_actor *monster);
double qa_q1ad_horde_attack_delay(qa_q1ad_horde *horde, qa_q1ad_player *player, double delay);
void qa_q1ad_horde_player_attach(qa_q1ad_horde *horde, qa_q1ad_player *player);
void qa_q1ad_horde_after_physics(qa_q1ad_horde *horde, qa_q1ad_player *player);
#endif
