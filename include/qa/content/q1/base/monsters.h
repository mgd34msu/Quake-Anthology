/* Port of src/content/q1/base/monsters.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_MONSTERS_H
#define QA_CONTENT_Q1_BASE_MONSTERS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/base/animation.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q1/base/species.h"
#include "qa/persistence/value.h"

typedef struct qa_q1_base_monster qa_q1_base_monster;

/* TS BaseMonsterSource. frames NULL / actions NULL = TS absent. */
/* TS actions map entry (a closure). ctx is the entry's own closure state;
 * NULL falls back to the source's shared actions_ctx. */
typedef struct qa_q1_monster_action_entry {
  const char *name;
  void (*fn)(void *ctx, qa_q1_base_monster *monster);
  void *ctx;
} qa_q1_monster_action_entry;
typedef struct qa_q1_base_monster_source {
  const char *callback_prefix;
  const qa_q1_monster_frame_table *frames;
  const qa_q1_monster_action_entry *actions; size_t action_count;
  void *actions_ctx;
  /* MG1 and MG3 Demon_JumpTouch only hurts once attack_finished has
   * passed, then holds it for half a second. */
  bool jump_attack_gate;
} qa_q1_base_monster_source;

/* TS MonsterServices (fn+ctx). */
typedef struct qa_q1_monster_services {
  void *ctx;
  bool (*count_monster_kill)(void *ctx, qa_q1_base_monster *monster);
  const char *(*next_hell_knight_melee)(void *ctx);
  void (*finale)(void *ctx, qa_q1_base_monster *monster);
  void (*finish_finale)(void *ctx, qa_q1_base_monster *monster);
} qa_q1_monster_services;

/* TS virtual dispatch: every BaseMonster method a TS subclass overrides
 * (MissionMonster, Mg3Monster and its subclasses, Q1FinalBoss, Q1Ghost,
 * Q1Oldnew, Q1ShubZombie, Q1DopaOrdinaryMonster). NULL slot = not
 * overridden. qa_q1_base_monster_<m> dispatches (TS `this.m()`);
 * qa_q1_base_monster_<m>_super runs the BaseMonster body (TS `super.m()`).
 * destroy frees the derived object (NULL: the base was malloc'd by
 * qa_q1_base_monster_create). */
typedef struct qa_q1_base_monster_ops {
  void (*spawn)(qa_q1_base_monster *monster);
  void (*start)(qa_q1_base_monster *monster);
  void (*play)(qa_q1_base_monster *monster, const char *name);
  void (*pain)(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage);
  void (*die)(qa_q1_base_monster *monster, const qa_actor_id *attacker);
  void (*use)(qa_q1_base_monster *monster, const qa_actor_id *activator);
  void (*awake)(qa_q1_base_monster *monster, const qa_actor_id *activator);
  void (*ai)(qa_q1_base_monster *monster, qa_q1_monster_ai mode, double distance);
  void (*run)(qa_q1_base_monster *monster, double distance);
  void (*move_to_enemy)(qa_q1_base_monster *monster, double distance);
  void (*hunt_target)(qa_q1_base_monster *monster);
  void (*found)(qa_q1_base_monster *monster, qa_actor_id target);
  bool (*find_target)(qa_q1_base_monster *monster);
  bool (*try_attack)(qa_q1_base_monster *monster);
  void (*melee_attack)(qa_q1_base_monster *monster);
  void (*attack_finished)(qa_q1_base_monster *monster, double seconds);
  qa_q1_basis (*make_vectors)(qa_q1_base_monster *monster);
  void (*update_run_knowledge)(qa_q1_base_monster *monster);
  bool (*search_for_coop_target)(qa_q1_base_monster *monster);
  void (*retaliate)(qa_q1_base_monster *monster, const qa_actor_id *attacker);
  bool (*fires_through_blockers)(qa_q1_base_monster *monster); /* TS protected */
  /* TS protected sfx(path): source variants (Rogue statues) swap sound
   * paths. The result must outlive the sound call (a literal or path). */
  const char *(*sfx)(qa_q1_base_monster *monster, const char *path);
  /* TS protected gibDeath(): the gibbing death; returns whether it ran. */
  bool (*gib_death)(qa_q1_base_monster *monster);
  void (*destroy)(qa_q1_base_monster *monster);
} qa_q1_base_monster_ops;

/* TS BaseMonster. state aliases entity->monster (TS
 * `this.state = entity.monster ?? {...}; entity.monster = this.state`);
 * the actor owns that object. Derived monsters embed this struct as
 * their first member and set ops. */
struct qa_q1_base_monster {
  const qa_q1_base_monster_ops *ops;
  qa_q1_services *game;
  qa_q1_actor *entity;
  const qa_q1_base_monster_species *spec;
  qa_q1_monster_services services;
  bool has_source;
  qa_q1_base_monster_source source;
  qa_q1_monster *state;
  char current_frame[64];
  char next_frame[64];
  double in_pain;
  double counter;
  double idle_until;
  bool lefty;
  bool sliding;
  bool counted_death;
};

/* TS `new BaseMonster(game, entity, spec, services, source)`, in place
 * (ops NULL). source NULL = TS undefined. False only on OOM. */
bool qa_q1_base_monster_init(qa_q1_base_monster *monster, qa_q1_services *game, qa_q1_actor *entity,
                             const qa_q1_base_monster_species *spec,
                             qa_q1_monster_services services,
                             const qa_q1_base_monster_source *source);
/* Heap BaseMonster (plain TS BaseMonster). */
qa_q1_base_monster *qa_q1_base_monster_create(qa_q1_services *game, qa_q1_actor *entity,
                                              const qa_q1_base_monster_species *spec,
                                              qa_q1_monster_services services,
                                              const qa_q1_base_monster_source *source);
/* ops->destroy when set, else free(). */
void qa_q1_base_monster_destroy(qa_q1_base_monster *monster);
qa_save_value *qa_q1_base_monster_capture(const qa_q1_base_monster *monster);
void qa_q1_base_monster_restore(qa_q1_base_monster *monster, const qa_save_reader *reader);
const qa_actor_id *qa_q1_base_monster_enemy(const qa_q1_base_monster *monster);
void qa_q1_base_monster_set_enemy(qa_q1_base_monster *monster, const qa_actor_id *enemy);
qa_vec3 qa_q1_base_monster_origin(const qa_q1_base_monster *monster);
bool qa_q1_base_monster_target(const qa_q1_base_monster *monster, qa_vec3 *out);
double qa_q1_base_monster_distance(const qa_q1_base_monster *monster);
void qa_q1_base_monster_play(qa_q1_base_monster *monster, const char *name);
void qa_q1_base_monster_delay(qa_q1_base_monster *monster, double seconds);
void qa_q1_base_monster_change_yaw(qa_q1_base_monster *monster);
void qa_q1_base_monster_face(qa_q1_base_monster *monster);
qa_q1_basis qa_q1_base_monster_make_vectors(qa_q1_base_monster *monster);
/* eye(out) for TS eye(target = own actor); false = TS null. */
bool qa_q1_base_monster_eye(qa_q1_base_monster *monster, qa_actor_id target, qa_vec3 *out);
double qa_q1_base_monster_range_distance(qa_q1_base_monster *monster, const qa_actor_id *target);
bool qa_q1_base_monster_visible(qa_q1_base_monster *monster, const qa_actor_id *target);
void qa_q1_base_monster_found(qa_q1_base_monster *monster, qa_actor_id target);
bool qa_q1_base_monster_find_target(qa_q1_base_monster *monster);
const qa_actor_id *qa_q1_base_monster_route(qa_q1_base_monster *monster);
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
void qa_q1_base_monster_retaliate(qa_q1_base_monster *monster, const qa_actor_id *attacker);
void qa_q1_base_monster_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage);
void qa_q1_base_monster_count_kill(qa_q1_base_monster *monster);
void qa_q1_base_monster_die(qa_q1_base_monster *monster, const qa_actor_id *attacker);
void qa_q1_base_monster_spawn(qa_q1_base_monster *monster);
void qa_q1_base_monster_start(qa_q1_base_monster *monster);
void qa_q1_base_monster_use(qa_q1_base_monster *monster, const qa_actor_id *activator);
void qa_q1_base_monster_awake(qa_q1_base_monster *monster, const qa_actor_id *activator);
/* TS this.sfx(path) / this.gibDeath() (virtual). */
const char *qa_q1_base_monster_sfx(qa_q1_base_monster *monster, const char *path);
bool qa_q1_base_monster_gib_death(qa_q1_base_monster *monster);

void qa_q1_base_monster_hunt_target(qa_q1_base_monster *monster);

/* TS super.m(): the BaseMonster bodies, bypassing ops. */
void qa_q1_base_monster_spawn_super(qa_q1_base_monster *monster);
void qa_q1_base_monster_start_super(qa_q1_base_monster *monster);
void qa_q1_base_monster_play_super(qa_q1_base_monster *monster, const char *name);
void qa_q1_base_monster_pain_super(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage);
void qa_q1_base_monster_die_super(qa_q1_base_monster *monster, const qa_actor_id *attacker);
void qa_q1_base_monster_use_super(qa_q1_base_monster *monster, const qa_actor_id *activator);
void qa_q1_base_monster_awake_super(qa_q1_base_monster *monster, const qa_actor_id *activator);
void qa_q1_base_monster_ai_super(qa_q1_base_monster *monster, qa_q1_monster_ai mode, double distance);
void qa_q1_base_monster_run_super(qa_q1_base_monster *monster, double distance);
void qa_q1_base_monster_move_to_enemy_super(qa_q1_base_monster *monster, double distance);
void qa_q1_base_monster_hunt_target_super(qa_q1_base_monster *monster);
void qa_q1_base_monster_found_super(qa_q1_base_monster *monster, qa_actor_id target);
bool qa_q1_base_monster_find_target_super(qa_q1_base_monster *monster);
bool qa_q1_base_monster_try_attack_super(qa_q1_base_monster *monster);
void qa_q1_base_monster_melee_attack_super(qa_q1_base_monster *monster);
void qa_q1_base_monster_attack_finished_super(qa_q1_base_monster *monster, double seconds);
qa_q1_basis qa_q1_base_monster_make_vectors_super(qa_q1_base_monster *monster);
void qa_q1_base_monster_update_run_knowledge_super(qa_q1_base_monster *monster);
bool qa_q1_base_monster_search_for_coop_target_super(qa_q1_base_monster *monster);
void qa_q1_base_monster_retaliate_super(qa_q1_base_monster *monster, const qa_actor_id *attacker);
bool qa_q1_base_monster_gib_death_super(qa_q1_base_monster *monster);

typedef qa_q1_base_monster *(*qa_q1_monster_lookup_fn)(void *ctx, qa_q1_actor *entity);
/* TS registerMonsterCallbacks. Returns the closure state the registry
 * keeps; the registering owner frees it (free()) when it is torn down. */
void *qa_q1_register_monster_callbacks(qa_q1_services *game, const char *prefix,
                                      qa_q1_monster_lookup_fn lookup, void *lookup_ctx);

/* creatures.ts registerLegacyCallbacks: registers the bare callback names a
 * version 5 save gives grunts and dogs (monster_frame, monster_path_end,
 * monster_found_target, walkmonster_start_go, monster_use, monster_pain,
 * monster_die, Dog_JumpTouch). Returns the closure the caller owns. */
void *qa_q1_register_legacy_monster_callbacks(qa_q1_services *game, qa_q1_monster_lookup_fn lookup, void *lookup_ctx);
/* TS setMonsterRoute: points a monster at a route goal (or back to
 * standing) outside its own think, as the monster-mission host does.
 * goal NULL = TS null. */
void qa_q1_set_monster_route(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *goal, double pause_until);

#endif
