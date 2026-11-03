/* Port of src/content/q2/missionpacks/types.ts. GPL-2.0-or-later.
 * Host/game/entity types are the one Q2 family (foundation/host.h);
 * Q2Ballistics is qa_q2w_ballistics and MonsterContext qa_q2m_context. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_TYPES_H
#define QA_CONTENT_Q2_MISSIONPACKS_TYPES_H
#include <stdbool.h>
#include "qa/content/q2/foundation/weapons/ballistics.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

struct qa_q2m_context;

/* TS Q2MissionPack: "xatrix" | "rogue". */
typedef enum qa_q2mp_pack { QA_Q2MP_PACK_XATRIX, QA_Q2MP_PACK_ROGUE } qa_q2mp_pack;
const char *qa_q2mp_pack_name(qa_q2mp_pack pack);

/* TS Q2MissionPackPlayerEffect union on kind (exact TS strings via
 * qa_q2mp_player_effect_kind_name). sphere kind: has_sphere false = null. */
typedef enum qa_q2mp_player_effect_kind {
  QA_Q2MP_EFFECT_TRACKER_PAIN, QA_Q2MP_EFFECT_NUKE_BLIND, QA_Q2MP_EFFECT_IR, QA_Q2MP_EFFECT_SPHERE_CAMERA
} qa_q2mp_player_effect_kind;
const char *qa_q2mp_player_effect_kind_name(qa_q2mp_player_effect_kind kind);
typedef struct qa_q2mp_player_effect {
  qa_q2mp_player_effect_kind kind;
  qa_actor_id actor;
  double until; /* tracker-pain/nuke-blind/ir */
  bool has_sphere; qa_actor_id sphere; /* sphere-camera */
  qa_vec3 origin, angles; /* sphere-camera */
} qa_q2mp_player_effect;

/* TS Q2MissionPackProjectileHooks. gravity NULL = TS undefined;
 * strongMines false = TS undefined (TS tests === true). */
typedef struct qa_q2mp_projectile_hooks {
  qa_q2w_ballistics *base;
  bool strong_mines;
  void *gravity_ctx;
  double (*gravity)(void *ctx); /* optional */
  void *ctx;
  struct qa_q2m_context *(*monster)(void *ctx, qa_actor_id actor);
  void (*player_effect)(void *ctx, qa_q2mp_player_effect effect);
} qa_q2mp_projectile_hooks;

/* TS q2MissionPackDamage record as constants. */
enum {
  QA_Q2MP_MOD_RIPPER = 34, QA_Q2MP_MOD_PHALANX = 35, QA_Q2MP_MOD_BRAIN_TENTACLE = 36,
  QA_Q2MP_MOD_BLASTOFF = 37, QA_Q2MP_MOD_GEKK = 38, QA_Q2MP_MOD_TRAP = 39,
  QA_Q2MP_MOD_CHAINFIST = 40, QA_Q2MP_MOD_DISINTEGRATOR = 41, QA_Q2MP_MOD_FLECHETTE = 42,
  QA_Q2MP_MOD_BLASTER2 = 43, QA_Q2MP_MOD_HEATBEAM = 44, QA_Q2MP_MOD_TESLA = 45, QA_Q2MP_MOD_PROX = 46,
  QA_Q2MP_MOD_NUKE = 47, QA_Q2MP_MOD_VENGEANCE_SPHERE = 48, QA_Q2MP_MOD_HUNTER_SPHERE = 49,
  QA_Q2MP_MOD_DEFENDER_SPHERE = 50, QA_Q2MP_MOD_TRACKER = 51, QA_Q2MP_MOD_DEATHBALL_CRUSH = 52,
  QA_Q2MP_MOD_DOPPLE_EXPLODE = 53, QA_Q2MP_MOD_DOPPLE_VENGEANCE = 54, QA_Q2MP_MOD_DOPPLE_HUNTER = 55
};

#endif
