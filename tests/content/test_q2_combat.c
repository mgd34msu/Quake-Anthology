/*
 * Port tests for src/content/q2/foundation/weapons + monsters (no
 * dedicated TS tests exist under tests/content/q2 for these modules;
 * expectations cross-checked against the TS via bun). TypeScript
 * throw cases run in a child process and must abort. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q2/foundation/monsters.h"
#include "qa/content/q2/foundation/weapons.h"
#include "qa/content/q2/foundation/weapons/damage.h"
#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/content/q2/foundation/weapons/generic_frame.h"
#include "qa/content/q2/foundation/weapons/hand_action.h"
#include "qa/content/q2/foundation/weapons/hand_grenade.h"
#include "qa/content/q2/foundation/weapons/presentation.h"
#include "qa/content/q2/foundation/weapons/turn.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static qa_actor_id actor1(void) {
  qa_actor_id a;
  memset(&a, 0, sizeof a);
  a.slot = 1;
  return a;
}
static qa_q2w_projection fixed_project(void *ctx, qa_vec3 angles, qa_vec3 offset) {
  (void)ctx;
  CHECK(angles.x == -62.5 && angles.y == 10);
  CHECK(offset.x == 2 && offset.y == 0 && offset.z == -14);
  qa_q2w_projection p = {{1, 2, 3}, {0, 1, 0}};
  return p;
}
static double quad3(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return 3; }

typedef struct frame_env { int fires, no_ammo, changes; } frame_env;
static double fe_random(void *ctx) { (void)ctx; return 0; }
static double fe_ammo(void *ctx) { (void)ctx; return 10; }
static void fe_no_ammo(void *ctx) { ((frame_env *)ctx)->no_ammo++; }
static void fe_fire(void *ctx, bool buffered) { (void)buffered; ((frame_env *)ctx)->fires++; }
static void fe_change(void *ctx) { ((frame_env *)ctx)->changes++; }
static void fe_noop(void *ctx) { (void)ctx; }
static double fe_time(void *ctx) { (void)ctx; return 0.1; }

int main(void) {
  /* definitions.ts data */
  CHECK(qa_q2w_base_weapon_count == 11);
  const qa_q2w_base_definition *blaster = qa_q2w_weapon_definition("blaster");
  CHECK(blaster->ammo == NULL && blaster->player_model == 1 && blaster->idle_last == 52);
  const qa_q2w_base_definition *bfg = qa_q2w_weapon_from_classname("weapon_bfg");
  CHECK(bfg != NULL && bfg->quantity == 50 && bfg->warning == 50);
  CHECK(qa_q2w_weapon_from_classname("weapon_bfg2") == NULL);
  const qa_q2w_base_definition *chain = qa_q2w_weapon_definition("chaingun");
  CHECK(chain->fire_count == 17 && chain->fires[0] == 5 && chain->fires[16] == 21 && chain->repeating);
  CHECK_ABORTS((void)qa_q2w_weapon_definition("nope"));

  /* damage.ts (bun: 4 and 6) */
  qa_q2w_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  CHECK(qa_q2w_weapon_damage_multiplier(actor1(), (qa_q2w_damage_input){10, 10, true}, 5, &hooks) == 4);
  hooks.quad_multiplier = quad3;
  CHECK(qa_q2w_weapon_damage_multiplier(actor1(), (qa_q2w_damage_input){10, 10, false}, 5, &hooks) == 6);
  CHECK(qa_q2w_weapon_damage_multiplier(actor1(), (qa_q2w_damage_input){0, 0, false}, 5, &hooks) == 1);

  /* turn.ts latch (int32 semantics) */
  qa_q2w_turn_state turn = {0, 0, false};
  qa_q2w_latch_buttons(&turn, 1);
  CHECK(turn.latched_buttons == 1 && turn.buttons == 1);
  qa_q2w_latch_buttons(&turn, 1);
  CHECK(turn.latched_buttons == 1);
  qa_q2w_latch_buttons(&turn, 3);
  CHECK(turn.latched_buttons == 3);

  /* generic-frame millisecondSum + classic stepping */
  CHECK(qa_q2w_millisecond_sum(1.2345, 0.1) == 1.335);
  qa_q2w_state state;
  qa_q2w_state_init_default(&state);
  frame_env env = {0, 0, 0};
  qa_q2w_frame_hooks fh;
  memset(&fh, 0, sizeof fh);
  fh.ctx = &env; fh.random = fe_random; fh.ammo = fe_ammo; fh.no_ammo = fe_no_ammo;
  fh.fire = fe_fire; fh.change_weapon = fe_change; fh.reverse_animation = fe_noop;
  fh.attack_animation = fe_noop; fh.powerup_sound = fe_noop; fh.animation_time = fe_time;
  fh.prepare_drop = fe_noop;
  const qa_q2w_definition *shotgun = qa_q2w_weapon_definition("shotgun");
  state.phase = QA_Q2W_ACTIVATING; state.frame = 0;
  qa_q2w_step_classic_frame(&state, shotgun, (qa_q2w_classic_frame_input){false, false}, &fh);
  CHECK(state.frame == 1 && state.phase == QA_Q2W_ACTIVATING);
  state.frame = shotgun->activate_last;
  qa_q2w_step_classic_frame(&state, shotgun, (qa_q2w_classic_frame_input){false, false}, &fh);
  CHECK(state.phase == QA_Q2W_READY && state.frame == shotgun->fire_last + 1);
  qa_q2w_step_classic_frame(&state, shotgun, (qa_q2w_classic_frame_input){true, false}, &fh);
  CHECK(state.phase == QA_Q2W_FIRING && state.frame == shotgun->activate_last + 1);
  CHECK(env.fires == 1); /* ready+attack falls through; frame 8 is a fire frame */

  /* hand-grenade.ts (bun values) */
  CHECK(qa_q2w_hand_frame_seconds((qa_q2w_hand_tempo){QA_Q2_EDITION_RERELEASE, true, false}) == 0.05);
  CHECK(qa_q2w_hand_deadline(1.2345, 0.1, QA_Q2_EDITION_RERELEASE) == 1.335);
  CHECK(qa_q2w_hand_deadline(1.2345, 0.1, QA_Q2_EDITION_CLASSIC) == 1.3345);
  qa_q2w_hand_throw_input tin;
  memset(&tin, 0, sizeof tin);
  tin.edition = QA_Q2_EDITION_RERELEASE; tin.angles = (qa_vec3){-80, 10, 0}; tin.alive = true;
  tin.now = 2; tin.fuse_deadline = 3.5; tin.damage_multiplier = 2; tin.gravity = 800;
  tin.project = (qa_q2w_project_fn){NULL, fixed_project};
  qa_q2w_hand_projectile_spec spec = qa_q2w_calculate_hand_throw(tin);
  CHECK(spec.damage == 250 && spec.speed == 600 && spec.fuse == 1.5 && spec.radius == 165);

  /* presentation.ts */
  CHECK(qa_q2w_attack_frames(true, 1).first == 159 && qa_q2w_attack_frames(false, 0).last == 53);
  CHECK(qa_q2w_animation_rate((qa_q2w_animation_rate_input){true, 0.05, QA_Q2W_DROPPING, 0, 0, false, 0, false}) == 20);
  const char *sound = qa_q2w_powerup_sound((qa_q2w_powerup_sound_input){10, 10, true, 5});
  CHECK(sound != NULL && strcmp(sound, "ctf/tech2x.wav") == 0);
  CHECK(qa_q2w_powerup_sound((qa_q2w_powerup_sound_input){0, 0, true, 5}) == NULL);

  /* monsters data tables (bun dump counts) */
  CHECK(qa_q2m_classic_soldier_move_count == 22 && qa_q2m_rerelease_soldier_move_count == 28);
  CHECK(qa_q2m_classic_infantry_move_count == 12 && qa_q2m_rerelease_infantry_move_count == 17);
  CHECK(qa_q2m_muzzle_classic_count == 212 && qa_q2m_muzzle_rerelease_count == 290);
  CHECK(qa_q2m_muzzle_ids_value.SOLDIER_BLASTER_1 == 39);
  qa_vec3 first = qa_q2m_muzzle_offset(QA_Q2_EDITION_CLASSIC, 0);
  CHECK(first.x == 0 && first.y == 0 && first.z == 0);
  CHECK(qa_q2m_classic_soldier_frames_value.stand101 == 146);
  CHECK_ABORTS((void)qa_q2m_muzzle_offset(QA_Q2_EDITION_CLASSIC, 212));
  CHECK_ABORTS((void)qa_q2m_record_at(3, 1.5));

  /* alternate-fly-state defaults */
  qa_q2m_alternate_fly_state fly = qa_q2m_create_alternate_fly_state();
  CHECK(!fly.alternate_fly && fly.fly_speed == 0 && !fly.has_pathing);

  /* state defaults */
  qa_q2w_state fresh;
  qa_q2w_state_init_default(&fresh);
  CHECK(strcmp(fresh.weapon, "blaster") == 0 && fresh.gun_rate == 10 && fresh.kick_duration == 0.2);
  CHECK(strcmp(qa_q2w_phase_name(QA_Q2W_READY), "ready") == 0);
  CHECK(strcmp(qa_q2m_ai_name(QA_Q2M_AI_CHARGE), "charge") == 0);
  CHECK(qa_q2m_callback_name_count == 65 && qa_q2m_is_callback_name("soldier_fire1"));
  CHECK(!qa_q2m_is_callback_name("bogus"));

  /* spawn fields / numberField */
  qa_q2_spawn_fields fields = {0};
  fields.classname = "monster_soldier";
  CHECK(qa_q2_spawn_set(&fields, "health", "42") && qa_q2_spawn_set(&fields, "target", "x"));
  CHECK(qa_q2_number_field(&fields, "health", 1) == 42);
  CHECK(qa_q2_number_field(&fields, "missing", 7) == 7);
  qa_q2_spawn_fields_free(&fields);

  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
