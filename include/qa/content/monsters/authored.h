/*
 * Port of src/content/monsters/authored.ts (types only): live
 * map-script monster records and the mission method table.
 * Mutable TS string fields are owned char * (caller frees/replaces);
 * readonly fields are const char *. The placement/activation unions
 * keep their exact kind strings. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MONSTERS_AUTHORED_H
#define QA_CONTENT_MONSTERS_AUTHORED_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

/* TS AuthoredTarget. */
typedef struct qa_authored_target {
  qa_owned_actor actor;
  const char *classname;
  char *targetname;
  char *target;
  char *killtarget;
  char *message;
  double delay;
} qa_authored_target;

/* TS placement "waiting" barrier row. */
typedef struct qa_authored_monster_barrier {
  qa_actor_id actor;
  qa_vec3 origin;
} qa_authored_monster_barrier;

/* TS placement: {kind:"ready"} | {kind:"teleport",origin} |
 * {kind:"waiting",barriers,activator}. */
typedef struct qa_authored_monster_placement {
  const char *kind;
  qa_vec3 origin; /* teleport */
  const qa_authored_monster_barrier *barriers; /* waiting */
  size_t barrier_count;
  qa_actor_id *activator; /* waiting, nullable, mutable in TS */
} qa_authored_monster_placement;

/* TS activation: {kind:"active"} | {kind:"dormant"} |
 * {kind:"scheduled",at,activator}. */
typedef struct qa_authored_monster_activation {
  const char *kind;
  double at; /* scheduled */
  const qa_actor_id *activator; /* scheduled, nullable */
} qa_authored_monster_activation;

/* TS AuthoredMonster extends AuthoredTarget. route_goal /
 * combat_goal are nullable (NULL = TS null). */
typedef struct qa_authored_monster {
  qa_authored_target base;
  double source_ordinal;
  double spawnflags;
  char *death_target;
  const char *drop_item;
  char *route;
  qa_actor_id *route_goal;
  bool route_resolved;
  bool counted_death;
  char *combat_target;
  qa_actor_id *combat_goal;
  bool stand_ground;
  qa_authored_monster_placement placement;
  qa_authored_monster_activation activation;
} qa_authored_monster;

/* TS MonsterMission.combatRoute() result. */
typedef struct qa_monster_combat_route {
  const qa_actor_id *goal; /* nullable */
  bool stand_ground;
} qa_monster_combat_route;

/* TS MonsterMission: methods become fn pointers + ctx. Attacker /
 * activator parameters are nullable in TS (NULL = null); route()
 * returns a nullable ActorId via out pointer. */
typedef struct qa_monster_mission {
  void *ctx;
  bool ambush;
  void (*spawned)(void *ctx);
  void (*started)(void *ctx);
  void (*killed)(void *ctx, const qa_actor_id *attacker);
  const qa_actor_id *(*route)(void *ctx);
  bool (*use)(void *ctx, const qa_actor_id *activator);
  qa_monster_combat_route (*combat_route)(void *ctx);
  void (*found_target)(void *ctx);
} qa_monster_mission;

#endif
