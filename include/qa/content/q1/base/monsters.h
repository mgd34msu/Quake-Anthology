/* Port of src/content/q1/base/monsters.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_MONSTERS_H
#define QA_CONTENT_Q1_BASE_MONSTERS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/base/animation.h"
#include "qa/content/q1/base/foundation_types.h"
#include "qa/content/q1/base/species.h"
#include "qa/persistence/value.h"

typedef struct qa_q1_base_monster qa_q1_base_monster;

/* TS BaseMonsterSource. frames NULL / actions NULL = TS absent. */
typedef struct qa_q1_monster_action_entry {
  const char *name;
  void (*fn)(void *ctx, qa_q1_base_monster *monster);
} qa_q1_monster_action_entry;
typedef struct qa_q1_base_monster_source {
  const char *callback_prefix;
  const qa_q1_monster_frame_table *frames;
  const qa_q1_monster_action_entry *actions; size_t action_count;
  void *actions_ctx;
} qa_q1_base_monster_source;

/* TS MonsterServices (fn+ctx). */
typedef struct qa_q1_monster_services {
  void *ctx;
  bool (*count_monster_kill)(void *ctx, qa_q1_base_monster *monster);
  const char *(*next_hell_knight_melee)(void *ctx);
  void (*finale)(void *ctx, qa_q1_base_monster *monster);
  void (*finish_finale)(void *ctx, qa_q1_base_monster *monster);
} qa_q1_monster_services;

/* TS BaseMonster. state is owned here; entity.monster aliases it. */
struct qa_q1_base_monster {
  qa_q1b_game *game;
  qa_q1b_actor *entity;
  const qa_q1_base_monster_species *spec;
  qa_q1_monster_services services;
  bool has_source;
  qa_q1_base_monster_source source;
  qa_q1b_monster_state state;
  char current_frame[64];
  char next_frame[64];
  double in_pain;
  double counter;
  double idle_until;
  bool lefty;
  bool sliding;
  double lightning_count;
  bool counted_death;
};

/* source NULL = TS undefined. */
qa_q1_base_monster *qa_q1_base_monster_create(qa_q1b_game *game, qa_q1b_actor *entity,
                                              const qa_q1_base_monster_species *spec,
                                              qa_q1_monster_services services,
                                              const qa_q1_base_monster_source *source);
void qa_q1_base_monster_destroy(qa_q1_base_monster *monster);
qa_save_value *qa_q1_base_monster_capture(const qa_q1_base_monster *monster);
void qa_q1_base_monster_restore(qa_q1_base_monster *monster, const qa_save_reader *reader);
qa_q1b_opt_actor qa_q1_base_monster_enemy(const qa_q1_base_monster *monster);
void qa_q1_base_monster_set_enemy(qa_q1_base_monster *monster, qa_q1b_opt_actor enemy);
qa_vec3 qa_q1_base_monster_origin(const qa_q1_base_monster *monster);
bool qa_q1_base_monster_target(const qa_q1_base_monster *monster, qa_vec3 *out);
double qa_q1_base_monster_distance(const qa_q1_base_monster *monster);
void qa_q1_base_monster_play(qa_q1_base_monster *monster, const char *name);
void qa_q1_base_monster_delay(qa_q1_base_monster *monster, double seconds);
void qa_q1_base_monster_change_yaw(qa_q1_base_monster *monster);
void qa_q1_base_monster_face(qa_q1_base_monster *monster);
qa_q1b_basis qa_q1_base_monster_make_vectors(qa_q1_base_monster *monster);
/* eye(out) for TS eye(target = own actor); false = TS null. */
bool qa_q1_base_monster_eye(qa_q1_base_monster *monster, qa_actor_id target, qa_vec3 *out);
double qa_q1_base_monster_range_distance(qa_q1_base_monster *monster, qa_q1b_opt_actor target);
bool qa_q1_base_monster_visible(qa_q1_base_monster *monster, qa_q1b_opt_actor target);
void qa_q1_base_monster_found(qa_q1_base_monster *monster, qa_actor_id target);
bool qa_q1_base_monster_find_target(qa_q1_base_monster *monster);
qa_q1b_opt_actor qa_q1_base_monster_route(qa_q1_base_monster *monster);
void qa_q1_base_monster_update_route(qa_q1_base_monster *monster);
void qa_q1_base_monster_ai(qa_q1_base_monster *monster, qa_q1_monster_ai mode, double distance);
void qa_q1_base_monster_run(qa_q1_base_monster *monster, double distance);
void qa_q1_base_monster_update_run_knowledge(qa_q1_base_monster *monster);
void qa_q1_base_monster_move_to_enemy(qa_q1_base_monster *monster, double distance);
bool qa_q1_base_monster_search_for_coop_target(qa_q1_base_monster *monster);
void qa_q1_base_monster_attack_finished(qa_q1_base_monster *monster, double seconds);
bool qa_q1_base_monster_try_attack(qa_q1_base_monster *monster);
void qa_q1_base_monster_melee_attack(qa_q1_base_monster *monster);
double qa_q1_base_monster_melee(qa_q1_base_monster *monster, double range, double scale, double rolls, bool sight);
void qa_q1_base_monster_retaliate(qa_q1_base_monster *monster, qa_q1b_opt_actor attacker);
void qa_q1_base_monster_pain(qa_q1_base_monster *monster, qa_q1b_opt_actor attacker, double damage);
void qa_q1_base_monster_count_kill(qa_q1_base_monster *monster);
void qa_q1_base_monster_die(qa_q1_base_monster *monster, qa_q1b_opt_actor attacker);
void qa_q1_base_monster_spawn(qa_q1_base_monster *monster);
void qa_q1_base_monster_start(qa_q1_base_monster *monster);
void qa_q1_base_monster_use(qa_q1_base_monster *monster, qa_q1b_opt_actor activator);
void qa_q1_base_monster_awake(qa_q1_base_monster *monster, qa_q1b_opt_actor activator);

typedef qa_q1_base_monster *(*qa_q1_monster_lookup_fn)(void *ctx, qa_q1b_actor *entity);
void qa_q1_register_monster_callbacks(qa_q1b_game *game, const char *prefix,
                                      qa_q1_monster_lookup_fn lookup, void *lookup_ctx);

#endif
