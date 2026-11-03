/*
 * Port of src/content/q1/addons/monsters/ai/controller.ts (Mg3Monster).
 * GPL-2.0-or-later.
 *
 * TS `class Mg3Monster extends BaseMonster`: the struct embeds the real
 * qa_q1_base_monster first and its base.ops table holds Mg3Monster's
 * BaseMonster overrides. Subclasses (Q1Demodog, Q1Infected, Q1Orb,
 * Q1Oldnew, Q1ShubZombie, Q1HeavyMonster, Q1OrdinaryMonster) embed
 * qa_q1am_mg3_monster first, build their ops table from
 * QA_Q1AM_MG3_MONSTER_OPS plus their own overrides, and may set mg3 for
 * Mg3Monster's own overridable methods (sightSound, checkAttack).
 * The qa_q1am_mg3_<m> functions taking qa_q1_base_monster are the
 * Mg3Monster method bodies (TS `super.m()` from a subclass); they are
 * only valid on monsters built with qa_q1am_mg3_monster_init.
 */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_AI_CONTROLLER_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_AI_CONTROLLER_H

#include <stdbool.h>

#include "qa/content/q1/base/monsters.h"

typedef struct qa_q1am_mg3_monster qa_q1am_mg3_monster;

/* Mg3Monster's own overridable methods; NULL slot = not overridden. */
typedef struct qa_q1am_mg3_ops {
  void (*sight_sound)(qa_q1am_mg3_monster *monster);
  bool (*check_attack)(qa_q1am_mg3_monster *monster);
} qa_q1am_mg3_ops;

struct qa_q1am_mg3_monster {
  qa_q1_base_monster base;
  const qa_q1am_mg3_ops *mg3; /* NULL = no Mg3 method overridden */
};

/* Mg3Monster's BaseMonster overrides (makeVectors, attackFinished, found,
 * findTarget, ai, huntTarget, updateRunKnowledge, run). */
qa_q1_basis qa_q1am_mg3_make_vectors(qa_q1_base_monster *monster);
void qa_q1am_mg3_attack_finished(qa_q1_base_monster *monster, double seconds);
void qa_q1am_mg3_found(qa_q1_base_monster *monster, qa_actor_id target);
bool qa_q1am_mg3_find_target(qa_q1_base_monster *monster);
void qa_q1am_mg3_ai(qa_q1_base_monster *monster, qa_q1_monster_ai mode, double distance);
void qa_q1am_mg3_hunt_target(qa_q1_base_monster *monster);
void qa_q1am_mg3_update_run_knowledge(qa_q1_base_monster *monster);
void qa_q1am_mg3_run(qa_q1_base_monster *monster, double distance);

/* Designated initializers for an ops table of an Mg3Monster subclass. */
#define QA_Q1AM_MG3_MONSTER_OPS                                     \
  .make_vectors = qa_q1am_mg3_make_vectors,                         \
  .attack_finished = qa_q1am_mg3_attack_finished,                   \
  .found = qa_q1am_mg3_found,                                       \
  .find_target = qa_q1am_mg3_find_target,                           \
  .ai = qa_q1am_mg3_ai,                                             \
  .hunt_target = qa_q1am_mg3_hunt_target,                           \
  .update_run_knowledge = qa_q1am_mg3_update_run_knowledge,         \
  .run = qa_q1am_mg3_run

/* TS `new Mg3Monster(...)` in place: base init, base.ops = ops (an
 * Mg3Monster table built with QA_Q1AM_MG3_MONSTER_OPS; NULL = plain
 * Mg3Monster), mg3 = mg3_ops. False only on OOM. */
bool qa_q1am_mg3_monster_init(qa_q1am_mg3_monster *monster, qa_q1_services *game, qa_q1_actor *entity,
                              const qa_q1_base_monster_species *spec, qa_q1_monster_services services,
                              const qa_q1_base_monster_source *source, const qa_q1_base_monster_ops *ops,
                              const qa_q1am_mg3_ops *mg3_ops);
/* Plain Mg3Monster ops table (destroy = free). */
extern const qa_q1_base_monster_ops QA_Q1AM_MG3_BASE_OPS;

/* Mg3Monster methods: dispatching (TS this.m()) and the Mg3Monster body. */
void qa_q1am_mg3_sight_sound(qa_q1_base_monster *monster);
void qa_q1am_mg3_sight_sound_super(qa_q1_base_monster *monster);
bool qa_q1am_mg3_check_attack(qa_q1_base_monster *monster);
bool qa_q1am_mg3_check_attack_super(qa_q1_base_monster *monster);
/* Non-virtual Mg3Monster methods. target NULL = TS default this.enemy. */
void qa_q1am_mg3_source_word(qa_q1_base_monster *monster, const char *name, double value);
/* TS word(name): the world global, or the saved sourceWorldWords entry when there is no Q1 world actor. */
double qa_q1am_mg3_word(qa_q1_base_monster *monster, const char *name);
double qa_q1am_mg3_range_category(qa_q1_base_monster *monster, const qa_actor_id *target);
bool qa_q1am_mg3_in_front(qa_q1_base_monster *monster, qa_actor_id target);
bool qa_q1am_mg3_check_contents_damage(qa_q1_base_monster *monster);
void qa_q1am_mg3_path_to_goal(qa_q1_base_monster *monster, double distance);

#endif
