/* Port of src/content/q2/foundation/monsters/index.ts (Q2Monsters
 * provider; not a pure barrel). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_INDEX_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_INDEX_H

#include "qa/content/monsters/authored.h"
#include "qa/content/q2/foundation/monsters/checkpoint.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/perception.h"

typedef struct qa_q2m_monster_hooks {
  void *ctx;
  void (*drop_item)(void *ctx, qa_owned_actor actor, qa_q2f_game *game, const char *classname); /* optional */
  const char *(*platform_state)(void *ctx, qa_actor_id actor); /* optional; NULL return = null */
  const qa_monster_mission *(*mission)(void *ctx, qa_actor_id actor); /* optional */
} qa_q2m_monster_hooks;

/* Q2PathFollower / Q2CombatFollower as fn+ctx (getters become fns;
 * false return = null actor). */
typedef struct qa_q2m_path_follower {
  void *ctx;
  qa_owned_actor actor;
  bool (*move_target)(void *ctx, qa_actor_id *out);
  bool (*enemy)(void *ctx, qa_actor_id *out);
  void (*advance)(void *ctx, const char *name, const qa_actor_id *goal, double pause_until);
} qa_q2m_path_follower;
typedef struct qa_q2m_combat_follower {
  void *ctx;
  bool (*move_target)(void *ctx, qa_actor_id *out);
  bool (*enemy)(void *ctx, qa_actor_id *out);
  bool (*old_enemy)(void *ctx, qa_actor_id *out);
  bool (*activator)(void *ctx, qa_actor_id *out);
  bool walking;
  void (*advance)(void *ctx, const char *target, const qa_actor_id *goal, const qa_actor_id *move_target);
  void (*hold)(void *ctx);
  void (*finish)(void *ctx);
} qa_q2m_combat_follower;
typedef struct qa_q2m_follower_lookup {
  void *ctx;
  const qa_q2m_path_follower *(*path)(void *ctx, qa_actor_id actor);
  const qa_q2m_combat_follower *(*combat)(void *ctx, qa_actor_id actor);
} qa_q2m_follower_lookup;

void qa_q2m_place_triggered_monster(qa_q2f_game *game, qa_owned_actor actor);

typedef struct qa_q2m_actor_entry {
  qa_q2m_context context;
  qa_q2m_state *state; /* owned */
  const qa_q2m_definition *definition;
} qa_q2m_actor_entry;
typedef struct qa_q2m_pending_damage {
  qa_actor_id actor;
  qa_death_reaction reaction;
  bool has_attack;
  qa_attack_provenance attack;
  qa_actor_id attacker_storage, inflictor_storage;
  qa_actor_id attack_attacker_storage, attack_inflictor_storage;
} qa_q2m_pending_damage;

typedef struct qa_q2m_monsters {
  qa_q2m_weapons weapons;
  qa_q2m_monster_hooks hooks;
  qa_q2m_follower_lookup external_path;
  qa_q2m_follower_lookup external_combat;
  bool has_external_path, has_external_combat;
  const qa_q2m_definition **definitions; size_t definition_count, definition_cap;
  const qa_q2m_definition **edition_definitions[2]; size_t edition_counts[2], edition_caps[2];
  qa_q2m_actor_entry **entries; size_t entry_count, entry_cap;
  qa_q2m_perception perception;
  qa_q2m_pending_damage **pending; size_t pending_count, pending_cap;
  bool connected;
  const qa_q2m_source_combat_hooks *source_combat_hooks;
  qa_q2m_combat_rules source_combat_rules;
  const qa_q2m_hint_hooks *hint_hooks;
  /* Built-in definition storage (per classname+edition). */
  qa_q2m_definition *builtin_storage; size_t builtin_count, builtin_cap;
  /* Rebuilt merged callback tables (owned). */
  qa_q2f_named_think *merged_think; size_t merged_think_count;
  qa_q2f_named_use *merged_use; size_t merged_use_count;
  qa_q2f_named_touch *merged_touch; size_t merged_touch_count;
  qa_q2f_named_pain *merged_pain; size_t merged_pain_count;
  qa_q2f_named_die *merged_die; size_t merged_die_count;
  qa_q2f_named_blocked *merged_blocked; size_t merged_blocked_count;
} qa_q2m_monsters;

bool qa_q2m_monsters_init(qa_q2m_monsters *m, qa_q2m_weapons weapons, qa_q2m_monster_hooks hooks);
void qa_q2m_monsters_destroy(qa_q2m_monsters *m);
void qa_q2m_monsters_set_source_combat_rules(qa_q2m_monsters *m, qa_q2m_combat_rules rules,
                                             const qa_q2m_source_combat_hooks *hooks);
void qa_q2m_monsters_set_hint_paths(qa_q2m_monsters *m, const qa_q2m_hint_hooks *hooks);
void qa_q2m_monsters_set_route(qa_q2m_monsters *m, qa_actor_id actor, const qa_actor_id *goal, double pause_until);
qa_q2f_callback_definitions qa_q2m_monsters_callbacks(qa_q2m_monsters *m);
const qa_q2m_definition *qa_q2m_monsters_definition(qa_q2m_monsters *m, const char *classname, qa_q2f_game *game);
/* edition < 0 = TS undefined (edition-independent). */
void qa_q2m_monsters_register(qa_q2m_monsters *m, const qa_q2m_definition *definition, int edition);
bool qa_q2m_monsters_capture(qa_q2m_monsters *m, qa_q2m_monsters_checkpoint *out);
void qa_q2m_monsters_checkpoint_free(qa_q2m_monsters_checkpoint *checkpoint);
void qa_q2m_monsters_restore(qa_q2m_monsters *m, qa_q2f_game *game, const qa_q2m_monsters_checkpoint *checkpoint);
qa_q2m_context *qa_q2m_monsters_context(qa_q2m_monsters *m, qa_actor_id actor);
bool qa_q2m_monsters_current_sight_client(qa_q2m_monsters *m, qa_actor_id *out);
void qa_q2m_monsters_found_target(qa_q2m_monsters *m, qa_q2m_context *context);
void qa_q2m_monsters_hunt_target(qa_q2m_monsters *m, qa_q2m_context *context);
qa_q2m_context *qa_q2m_monsters_respawn(qa_q2m_monsters *m, qa_q2f_entity *entity, qa_q2f_game *game);
void qa_q2m_monsters_resume_monster(qa_q2m_monsters *m, qa_q2f_entity *entity, qa_q2f_game *game);
qa_q2m_context *qa_q2m_monsters_spawn_infantry_driver(qa_q2m_monsters *m, qa_q2f_entity *entity, qa_q2f_game *game);
void qa_q2m_monsters_begin_frame(qa_q2m_monsters *m, qa_q2f_game *game);
void qa_q2m_monsters_end_frame(qa_q2m_monsters *m, qa_q2f_game *game);
void qa_q2m_monsters_process_pain(qa_q2m_monsters *m, qa_q2f_entity *entity, qa_q2f_game *game);
void qa_q2m_monsters_report_noise(qa_q2m_monsters *m, qa_actor_id actor, qa_vec3 origin, bool secondary);
bool qa_q2m_monsters_spawn(qa_q2m_monsters *m, qa_q2f_entity *entity, qa_q2f_game *game);
qa_q2m_context *qa_q2m_monsters_spawn_summoned(qa_q2m_monsters *m, qa_q2f_entity *entity, qa_q2f_game *game);
void qa_q2m_monsters_dodge(qa_q2m_monsters *m, qa_q2f_entity *entity, qa_q2f_game *game, qa_actor_id attacker,
                            double eta, const qa_trace_result *trace, bool gravity);
void qa_q2m_monsters_touch_path_corner(qa_q2m_monsters *m, qa_q2f_entity *corner, qa_q2f_game *game, qa_actor_id actor);
void qa_q2m_monsters_touch_combat_point(qa_q2m_monsters *m, qa_q2f_entity *corner, qa_q2f_game *game, qa_actor_id actor);
#endif
