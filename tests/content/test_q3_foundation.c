/*
 * Port of key cases from tests/gameplay/foundation/q3/foundation.test.ts
 * plus source-derived cases for src/content/q3/foundation, equipment,
 * guest-items, input-profile and product-restriction (no dedicated TS
 * tests exist under tests/content/q3). GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/content/q3/equipment/cgame_weapon_hud.h"
#include "qa/content/q3/equipment/combat_profile.h"
#include "qa/content/q3/equipment/grapple_profiles.h"
#include "qa/content/q3/equipment/inventory_profile.h"
#include "qa/content/q3/equipment/pickup_profile.h"
#include "qa/content/q3/equipment/threewave_grapple_profile.h"
#include "qa/content/q3/equipment/weapon_profile.h"
#include "qa/content/q3/foundation.h"
#include "qa/content/q3/foundation/held_weapons.h"
#include "qa/content/q3/foundation/weapon_behavior.h"
#include "qa/content/q3/guest_items.h"
#include "qa/content/q3/input_profile.h"
#include "qa/content/q3/product_restriction.h"
#include "qa/core/game_numeric.h"
#include "qa/movement/q3/constants.h"
#include "qa/world/actors/registry.h"

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

static bool near(double a, double b) { return fabs(a - b) < 1e-9; }
static const qa_inventory_entry *find_ammo(const qa_arsenal_state *a, const char *item) {
  for (size_t i = 0; i < a->ammo_count; i++)
    if (a->ammo[i].item && strcmp(a->ammo[i].item, item) == 0) return &a->ammo[i];
  return NULL;
}

static void test_arsenal(void) {
  CHECK(QA_Q3_WEAPON_ITEM_COUNT == 13);
  const qa_q3_weapon_item *mg = qa_q3_get_weapon_item(2);
  CHECK(mg && strcmp(mg->item, "q3:weapon/machinegun") == 0);
  CHECK(mg && strcmp(mg->ammo, "q3:ammo/machinegun") == 0);
  CHECK(qa_q3_get_weapon_item(0) == NULL);
  CHECK(qa_q3_get_weapon_item(99) == NULL);
  qa_arsenal_state loadout = qa_q3_spawn_loadout("q3:arsenal", "baseq3", false);
  CHECK(loadout.ammo_count == 18); /* TS-verified: 10 weapons + 8 ammo */
  const qa_inventory_entry *bullets = find_ammo(&loadout, "q3:ammo/machinegun");
  CHECK(bullets && bullets->count == 100 && bullets->capacity == 200);
  CHECK(find_ammo(&loadout, "q3:weapon/nailgun") == NULL);
  qa_arsenal_state mp = qa_q3_spawn_loadout("q3:arsenal", "missionpack", true);
  CHECK(mp.ammo_count == 24); /* TS-verified */
  const qa_inventory_entry *mpb = find_ammo(&mp, "q3:ammo/machinegun");
  CHECK(mpb && mpb->count == 50);
  free((void *)mp.ammo);

  /* foundation.test.ts: Q1 timing drives the selected Q3 weapon. */
  qa_weapon_step_input input = {0};
  input.frame.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0.016};
  input.frame.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0.016};
  input.frame.frame = 1;
  input.arsenal = loadout;
  input.animation = (qa_actor_animation_state){"q3:character", qa_q3_spawn_animation()};
  input.environment.health = 125;
  input.environment.gravity_multiplier = 1;
  qa_q3_arsenal_runtime_state spawned = qa_q3_spawn_arsenal_runtime("baseq3", 100, 0);
  qa_q3_arsenal_step released = qa_q3_step_arsenal(input, spawned,
      (qa_q3_arsenal_controls){false, false, QA_Q3_WP_MACHINEGUN}, NULL, NULL);
  input.arsenal = released.base.arsenal;
  input.animation = released.base.animation;
  qa_q3_arsenal_step fired = qa_q3_step_arsenal(input, released.runtime,
      (qa_q3_arsenal_controls){true, false, QA_Q3_WP_MACHINEGUN}, NULL, NULL);
  const qa_inventory_entry *after = find_ammo(&fired.base.arsenal, "q3:ammo/machinegun");
  CHECK(after && after->count == 99);
  bool saw_fire = false;
  for (size_t i = 0; i < fired.base.effect_count; i++)
    if (fired.base.effects[i].kind == QA_MOVEMENT_EFFECT_EVENT &&
        fired.base.effects[i].value.event == QA_Q3_EV_FIRE_WEAPON) saw_fire = true;
  CHECK(saw_fire);
  CHECK(fired.base.arsenal.state.source_weapon == 2);
  CHECK(fired.base.arsenal.state.state == 3);
  CHECK(near(fired.base.arsenal.state.time_milliseconds, 100));
  CHECK(fired.base.animation.state.kind && strcmp(fired.base.animation.state.kind, "q3") == 0);
  qa_q3_arsenal_step_destroy(&fired);
  qa_q3_arsenal_step_destroy(&released);

  qa_q3_arsenal_runtime_state r = qa_q3_spawn_arsenal_runtime("baseq3", 100, 0);
  CHECK_ABORTS((void)qa_q3_request_weapon(r, 11));
  r = qa_q3_request_weapon(r, 5);
  CHECK(r.has_requested_weapon && r.requested_weapon == 5);
  r = qa_q3_request_weapon_holster(r);
  CHECK(r.external_slot == QA_Q3_WEAPON_SLOT_HOLSTER_REQUESTED);
  CHECK_ABORTS((void)qa_q3_request_weapon_resume(r));
  r.external_slot = QA_Q3_WEAPON_SLOT_HOLSTERED;
  r = qa_q3_request_weapon_resume(r);
  CHECK(r.external_slot == QA_Q3_WEAPON_SLOT_RESUME_REQUESTED);
  r = qa_q3_request_weapon_holster(r);
  CHECK(r.external_slot == QA_Q3_WEAPON_SLOT_HOLSTERED);
  qa_animation_state spawn_anim = qa_q3_spawn_animation();
  CHECK(spawn_anim.legs == QA_Q3_ANIM_LEGS_IDLE && spawn_anim.torso == QA_Q3_ANIM_TORSO_STAND);
}

static void test_weapon_behavior_and_grip(void) {
  CHECK(strcmp(qa_q3_projectile_behavior(5).role, "rocket") == 0);
  CHECK(strcmp(qa_q3_projectile_behavior(4).role, "grenade") == 0);
  CHECK(strcmp(qa_q3_projectile_behavior(12).role, "grenade") == 0);
  CHECK(strcmp(qa_q3_projectile_behavior(10).weapon, "q3:weapon/grapple") == 0);
  CHECK_ABORTS((void)qa_q3_projectile_behavior(2));
  CHECK_ABORTS((void)qa_q3_projectile_behavior(42));
  CHECK(near(qa_q3_weapon_hand_grip.origin.x, -2.9841071642362156));
  CHECK(near(qa_q3_weapon_hand_grip.axis.vectors[0].x, 1));
  CHECK(near(qa_q3_weapon_hand_grip.scale.z, 1));
}

static void test_animation_config(void) {
  char text[8192] = "footsteps boot\nsex f\nheadoffset 1 2 3\nbogusdirective\n";
  size_t used = strlen(text);
  for (int i = 0; i < 31; i++)
    used += (size_t)snprintf(text + used, sizeof text - used, "%d 4 2 20\n", i * 10);
  qa_q3_player_animation_config config;
  qa_q3_parse_player_animation_config(text, NULL, NULL, &config);
  CHECK(config.footsteps == QA_Q3_FOOTSTEPS_BOOT);
  CHECK(config.gender == QA_Q3_GENDER_FEMALE);
  CHECK(near(config.head_offset.x, 1) && near(config.head_offset.z, 3));
  CHECK(config.warning_count == 1);
  CHECK(!config.animation_present[31]);
  CHECK(near(config.animations[0].frame_lerp, 50));
  /* LEGS_WALKCR(13): skip=130-60=70, firstFrame 130-70=60. */
  CHECK(near(config.animations[QA_Q3_ANIM_LEGS_WALKCR].first_frame, 60));
  CHECK(config.animations[QA_Q3_ANIM_LEGS_BACKCR].reversed);
  CHECK(near(config.animations[QA_Q3_ANIM_FLAG_RUN].num_frames, 16));
  CHECK(near(config.animations[QA_Q3_ANIM_FLAG_STAND].first_frame, 16));
  qa_q3_player_animation_config_destroy(&config);
  /* Referenced so -Wunused does not drop the generated length bookkeeping. */
  CHECK(used > 200);
  CHECK_ABORTS({ qa_q3_player_animation_config c2; qa_q3_parse_player_animation_config("", NULL, NULL, &c2); });
  CHECK_ABORTS({ qa_q3_player_animation_config c3; qa_q3_parse_player_animation_config("0 1 0 20\n", NULL, NULL, &c3); });
}

static void test_lerp_and_pose(void) {
  qa_q3_player_animation_config config = {0};
  for (int i = 0; i < QA_Q3_TOTAL_ANIMATION_COUNT; i++) {
    config.animations[i] = (qa_q3_animation){100, 4, 4, 100, 100, false, false};
    config.animation_present[i] = i != 31;
  }
  qa_q3_animation_set set = qa_q3_animation_set_from_config(&config);
  qa_q3_lerp_frame frame = qa_q3_create_lerp_frame();
  qa_q3_clear_lerp_frame(set, &frame, QA_Q3_ANIM_LEGS_RUN, 1000, NULL, NULL);
  CHECK(near(frame.frame, 100) && near(frame.old_frame, 100));
  qa_q3_run_lerp_frame(set, &frame, (qa_q3_run_lerp_frame_input){1250, QA_Q3_ANIM_LEGS_RUN, 1, false}, NULL, NULL);
  CHECK(near(frame.frame, 100)); /* TS-verified */
  qa_q3_run_lerp_frame(set, &frame, (qa_q3_run_lerp_frame_input){1400, QA_Q3_ANIM_LEGS_RUN, 1, false}, NULL, NULL);
  CHECK(near(frame.frame, 102)); /* TS-verified */
  CHECK_ABORTS(qa_q3_set_lerp_frame_animation(set, &frame, 99, NULL, NULL));
  qa_q3_run_lerp_frame(set, &frame, (qa_q3_run_lerp_frame_input){1300, QA_Q3_ANIM_LEGS_RUN, 1, true}, NULL, NULL);
  CHECK(near(frame.frame, 0) && near(frame.back_lerp, 0));

  qa_q3_swing_angles_result still = qa_q3_swing_angles((qa_q3_swing_angles_input){10, 25, 90, 0.3, 100, 0, false});
  CHECK(!still.swinging && near(still.angle, 0));
  CHECK_ABORTS((void)qa_q3_swing_angles((qa_q3_swing_angles_input){0, -1, 90, 0.3, 100, 0, false}));
  qa_vec3 twitch = qa_q3_add_pain_twitch((qa_vec3){0, 0, 0}, (qa_q3_pain_twitch_input){1000, 0, true});
  CHECK(near(twitch.z, 0));
  qa_vec3 twitch2 = qa_q3_add_pain_twitch((qa_vec3){0, 0, 0}, (qa_q3_pain_twitch_input){100, 0, true});
  CHECK(near(twitch2.z, 10));
  qa_q3_player_pose_state pose = qa_q3_create_player_pose_state();
  qa_q3_calculate_player_pose_input in = {0};
  in.entity.velocity = (qa_vec3){0, 0, 0};
  in.entity.legs_anim = QA_Q3_ANIM_LEGS_IDLE;
  in.entity.torso_anim = QA_Q3_ANIM_TORSO_STAND;
  in.lerp_angles = (qa_vec3){0, 90, 0};
  in.time_ms = 1000; in.frame_time_ms = 100; in.swing_speed = 0.3;
  qa_q3_player_pose out = qa_q3_calculate_player_pose(&pose, in);
  /* TS-verified: yaw 59.996337890625, legs forward ~(0.5000553, 0.8659934). */
  CHECK(fabs(pose.legs.yaw_angle - 59.996337890625) < 1e-6);
  CHECK(fabs(out.legs.vectors[0].x - 0.5000553131103516) < 1e-6);
  CHECK(fabs(out.legs.vectors[0].y - 0.8659934401512146) < 1e-6);
  /* Weapon pose / barrel. */
  qa_q3_weapon_barrel barrel; qa_q3_weapon_barrel_init(&barrel);
  qa_q3_weapon_barrel_step_result spin = qa_q3_weapon_barrel_step(&barrel, 100, true);
  CHECK(!spin.stopped);
  qa_q3_weapon_barrel_step_result stop = qa_q3_weapon_barrel_step(&barrel, 200, false);
  CHECK(stop.stopped);
  CHECK(near(qa_q3_torso_weapon_frame(&config, 101), 7)); /* 101-100+6 */
}

static double rand7(void *ctx) { (void)ctx; return 7; }
static void test_events(void) {
  /* foundation.test.ts pain debounce case, with GameRandom-shaped stub. */
  qa_q3_player_pose_state pose = qa_q3_create_player_pose_state();
  qa_q3_character_event_presenter presenter;
  qa_q3_character_event_presenter_init(&presenter, &pose, QA_Q3_FOOTSTEPS_BOOT, rand7, NULL);
  qa_q3_character_presentation_effect effects[4];
  size_t n = qa_q3_character_event_presenter_pain(&presenter, 1000, 20, effects, 4);
  CHECK(n == 1 && effects[0].kind == QA_Q3_EFFECT_CUSTOM_SOUND);
  CHECK(strcmp(effects[0].text, "*pain25_1.wav") == 0);
  CHECK(strcmp(effects[0].channel, "voice") == 0);
  CHECK(qa_q3_character_event_presenter_pain(&presenter, 1300, 10, effects, 4) == 0);
  n = qa_q3_character_event_presenter_pain(&presenter, 1500, 80, effects, 4);
  CHECK(n == 1 && strcmp(effects[0].text, "*pain100_1.wav") == 0);
  CHECK(pose.pain_direction == false);
  qa_q3_character_event source = {0};
  source.event = QA_Q3_EV_FOOTSTEP; source.time_milliseconds = 2000;
  n = qa_q3_character_event_presenter_event(&presenter, source,
      (qa_q3_character_event_options){true, true, true, 0}, effects, 4);
  CHECK(n == 1 && effects[0].kind == QA_Q3_EFFECT_FOOTSTEP);
  CHECK(effects[0].material == QA_Q3_FOOTSTEP_MATERIAL_BOOT);
  CHECK(near(effects[0].variant, 3)); /* 7 & 3 */
  source.event = QA_Q3_EV_JUMP_PAD;
  n = qa_q3_character_event_presenter_event(&presenter, source,
      (qa_q3_character_event_options){true, true, true, 0}, effects, 4);
  CHECK(n == 3 && effects[0].kind == QA_Q3_EFFECT_JUMP_PAD_SMOKE);
  source.event = 999;
  n = qa_q3_character_event_presenter_event(&presenter, source,
      (qa_q3_character_event_options){true, true, true, 0}, effects, 4);
  CHECK(n == 1 && effects[0].kind == QA_Q3_EFFECT_SOURCE_EVENT);
}

static qa_q3_character_event g_events[16];
static size_t g_event_count;
static double g_time;
static double svc_time(void *ctx) { (void)ctx; return g_time; }
static void svc_emit(void *ctx, qa_q3_character_event e) {
  (void)ctx;
  if (g_event_count < 16) g_events[g_event_count++] = e;
}
static qa_q3_character_death_context svc_death(void *ctx, qa_owned_actor a) {
  (void)ctx; (void)a;
  return (qa_q3_character_death_context){true, false, false, 1022};
}
static void svc_noop_actor(void *ctx, qa_owned_actor a) { (void)ctx; (void)a; }
static qa_bounds abs_bounds(void *ctx, qa_owned_actor a, qa_body_state s) {
  (void)ctx; return qa_translated_body_bounds(a, s);
}
static void on_link(void *ctx, qa_linked_body b) { (void)ctx; (void)b; }
static void on_unlink(void *ctx, qa_actor_id a) { (void)ctx; (void)a; }

static void test_character(void) {
  CHECK(near(qa_q3_maximum_health("100"), 100));
  CHECK(near(qa_q3_maximum_health("0"), 100));
  CHECK(near(qa_q3_maximum_health("42"), 42));
  qa_combat_state combat = qa_q3_initial_combat("100", NULL);
  CHECK(near(combat.health, 125));
  CHECK(combat.armor.regular.kind == QA_REGULAR_ARMOR_Q3);
  qa_q3_death_animation_sequence seq; qa_q3_death_animation_sequence_init(&seq);
  CHECK(qa_q3_death_animation_sequence_next(&seq).event == QA_Q3_EV_DEATH1);
  CHECK(qa_q3_death_animation_sequence_next(&seq).event == QA_Q3_EV_DEATH2);
  CHECK(qa_q3_death_animation_sequence_next(&seq).event == QA_Q3_EV_DEATH3);
  CHECK(qa_q3_death_animation_sequence_next(&seq).event == QA_Q3_EV_DEATH1);
  qa_q3_death_animation_sequence_restore(&seq, (qa_q3_death_animation_checkpoint){1, 2});
  CHECK(near(qa_q3_death_animation_sequence_capture(&seq).index, 2));
  CHECK_ABORTS(qa_q3_death_animation_sequence_restore(&seq, (qa_q3_death_animation_checkpoint){1, 3}));

  /* foundation.test.ts life cycle (direct die after lethal health set). */
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "q3-life"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 1024);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_body_link_hooks body_hooks = {0};
  body_hooks.absolute_bounds = abs_bounds; body_hooks.on_link = on_link; body_hooks.on_unlink = on_unlink;
  qa_shared_body_table *bodies = qa_shared_body_table_create(actors, body_hooks);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  qa_gameplay_hooks hooks = {0};
  qa_gameplay_authority *authority = qa_gameplay_authority_create(actors, callbacks, hooks);
  CHECK(actors && callbacks && bodies && inventory && authority);
  qa_owned_actor actor = qa_session_actor_registry_allocate(actors, "q3:character", "q3:character/sarge");
  qa_q3_death_animation_sequence deaths; qa_q3_death_animation_sequence_init(&deaths);
  qa_q3_character_services services = {0};
  services.bodies = bodies; services.callbacks = callbacks; services.combat = authority;
  services.inventory = inventory; services.time_milliseconds = svc_time;
  services.emit = svc_emit; services.death_context = svc_death;
  services.spawn_targets = svc_noop_actor; services.kill_box = svc_noop_actor;
  qa_q3_character_actor player;
  qa_q3_character_actor_init(&player, actor, "q3:character", "baseq3", services, &deaths);
  qa_arsenal_state loadout = qa_q3_spawn_loadout("q3:arsenal", "baseq3", false);
  qa_body_state body = {0};
  body.bounds = qa_q3_character_bounds;
  qa_q3_character_spawn spawn = {body, qa_q3_initial_combat("100", NULL), loadout.ammo, loadout.ammo_count};
  g_time = 1000; g_event_count = 0;
  qa_q3_character_actor_spawn(&player, spawn);
  qa_gameplay_authority_set_health(authority, actor, -15);
  qa_q3_character_actor_die(&player);
  CHECK(g_event_count >= 1 && g_events[g_event_count - 1].event == QA_Q3_EV_DEATH1);
  qa_body_state_result br = qa_shared_body_table_read(bodies, actor.id);
  CHECK(br.has_value && near(br.value.bounds.max.z, -8));
  CHECK(!qa_q3_character_actor_wants_respawn(&player, 2700, true, false, 0));
  CHECK(qa_q3_character_actor_wants_respawn(&player, 2701, true, false, 0));
  g_time = 2800;
  qa_q3_character_actor_spawn(&player, spawn);
  qa_combat_state_result hr = qa_gameplay_authority_read(authority, actor.id);
  CHECK(hr.has_value && near(hr.value.health, 125));
  CHECK(g_events[g_event_count - 1].event == QA_Q3_EV_PLAYER_TELEPORT_IN);
  CHECK(near(qa_shared_inventory_table_count(inventory, actor.id, "q3:ammo/machinegun"), 100));
  free((void *)loadout.ammo);
  qa_gameplay_authority_destroy(authority);
  qa_shared_inventory_table_destroy(inventory);
  qa_shared_body_table_destroy(bodies);
  qa_actor_callback_table_destroy(callbacks);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

static qa_q3_equipment_qvm_artifact make_artifact(const char *digest, const char *role,
    qa_q3_equipment_qvm_instruction *instructions, size_t instruction_count,
    uint8_t *data, size_t data_length) {
  qa_q3_equipment_qvm_artifact a = {0};
  a.module.id = "test:module"; a.module.artifact_path = "vm/qagame.qvm";
  a.module.digest = digest; a.module.revision = "r1";
  a.role = role; a.abi_profile = "q3-modern";
  a.image.initialized_data = data; a.image.initialized_data_length = data_length;
  a.image.allocated_data_length = 4194304;
  a.image.instructions = instructions; a.image.instruction_count = instruction_count;
  a.image.data_length = (double)data_length; a.image.literal_length = 1048576; a.image.bss_length = 1048576;
  return a;
}
static void fill_enter(qa_q3_equipment_qvm_instruction *ins, size_t n) {
  for (size_t i = 0; i < n; i++) ins[i] = (qa_q3_equipment_qvm_instruction){(double)i, QA_Q3_EQUIPMENT_OP_ENTER, 4, 0};
}

static void test_equipment(void) {
  enum { NINS = 220000 };
  qa_q3_equipment_qvm_instruction *ins = malloc(NINS * sizeof *ins);
  uint8_t *data = calloc(1, 65536);
  CHECK(ins && data);
  fill_enter(ins, NINS);
  qa_q3_equipment_qvm_artifact lr = make_artifact(qa_lrctf_grapple_digest, "qagame", ins, NINS, data, 65536);
  struct qa_qvm_grapple_definition grapple;
  CHECK(qa_q3_grapple_profile(&lr, &grapple));
  CHECK(strcmp(grapple.id, "lrctf-1.2") == 0);
  CHECK(near(grapple.fields.hook, 840));
  qa_q3_equipment_qvm_artifact tw = make_artifact(qa_threewave_grapple_digest, "qagame", ins, NINS, data, 65536);
  CHECK(qa_q3_grapple_profile(&tw, &grapple));
  CHECK(strcmp(grapple.id, "threewave-1.7") == 0);
  CHECK(near(grapple.presentation.weapon_index, 11));
  qa_q3_equipment_qvm_artifact other = make_artifact("sha256:other", "qagame", ins, NINS, data, 65536);
  CHECK(!qa_q3_grapple_profile(&other, &grapple));

  qa_q3_equipment_qvm_primary_combat_profile combat_profile;
  CHECK(qa_q3_native_combat_profile(&lr, &combat_profile));
  CHECK(near(combat_profile.armor.check_armor, 139839));
  CHECK(near(combat_profile.reactions.pain, 728));
  CHECK(qa_q3_native_combat_profile(&tw, &combat_profile));
  CHECK(combat_profile.armor.has_tiers);
  CHECK(near(combat_profile.armor.values[2].protection, (float)0.8));

  qa_q3_qvm_input_definition input_profile;
  CHECK(qa_q3_input_profile(&lr, &input_profile));
  CHECK(near(input_profile.entity_stride, 856) && near(input_profile.entry_slice, 21620));
  CHECK(!qa_q3_input_profile(&other, &input_profile));

  qa_q3_equipment_qvm_pickup_profile pickup;
  CHECK(qa_q3_native_pickup_profile(&tw, &pickup));
  CHECK(pickup.grant_count == 3);
  CHECK(near(pickup.items.address, 5304));
  /* threewaveDroppedOwner with lithium: mode!=10 -> eligible. */
  uint8_t call_words[16] = {0}, item[200] = {0}, player[200] = {0};
  qa_q3_equipment_qvm_pickup_eligibility elig = {{call_words, 16}, {item, 200}, {player, 200}};
  CHECK(pickup.grants[1].eligible(NULL, elig));

  qa_q3_equipment_qvm_primary_weapon_profile weapon;
  CHECK(qa_q3_primary_weapon_profile(&tw, qa_q3_standard_guest_weapons, 13, &weapon));
  CHECK(near(weapon.max_health, 220));
  CHECK(weapon.stage.selection.value_count == 13);
  CHECK(weapon.stage.selection.values[1].value == 2);
  qa_q3_equipment_qvm_primary_weapon_profile_destroy(&weapon);
  CHECK(!qa_q3_primary_weapon_profile(&lr, qa_q3_standard_guest_weapons, 13, &weapon));

  qa_q3_equipment_qvm_presentation_profile hud;
  qa_q3_equipment_qvm_artifact cg = make_artifact(
      "sha256:a4744482c9b93852cc71f4d7ce03b3e4337e5d89844d27d272c2c16d74df07fa", "cgame", ins, NINS, data, 65536);
  CHECK(qa_q3_equipment_presentation_profile(&cg, &hud));
  CHECK(near(hud.hud, 108261) && !hud.status_is_regions);
  cg.role = "qagame";
  CHECK(!qa_q3_equipment_presentation_profile(&cg, &hud));

  /* Inventory: stock constant path with qualified instructions. */
  fill_enter(ins, NINS);
  ins[103202] = (qa_q3_equipment_qvm_instruction){0, QA_Q3_EQUIPMENT_OP_CONST, 4, 200};
  ins[103203] = (qa_q3_equipment_qvm_instruction){0, QA_Q3_EQUIPMENT_OP_LEI, 4, 0};
  ins[103216] = (qa_q3_equipment_qvm_instruction){0, QA_Q3_EQUIPMENT_OP_CONST, 4, 200};
  ins[103217] = (qa_q3_equipment_qvm_instruction){0, QA_Q3_EQUIPMENT_OP_STORE4, 0, 0};
  qa_q3_equipment_qvm_artifact stock = make_artifact(
      "sha256:57c52bf22e4f528c064f8af1553a7103723bab0a02276bb11eed944bf829b219", "qagame", ins, NINS, data, 65536);
  qa_q3_equipment_qvm_inventory_profile inventory_profile;
  CHECK(qa_q3_native_inventory_profile(&stock, &inventory_profile));
  qa_q3_equipment_qvm_memory memory = {data, 65536};
  CHECK(near(inventory_profile.capacity(inventory_profile.capacity_ctx, memory, 2), 200));
  qa_q3_equipment_qvm_inventory_profile_destroy(&inventory_profile);
  free(ins); free(data);
}

static bool mounts_none(void *ctx, const char *path, uint8_t **out, size_t *len) {
  (void)ctx; (void)path; (void)out; (void)len; return false;
}
static void put32(uint8_t *data, size_t off, int32_t value) { memcpy(data + off, &value, 4); }
static void put_str(uint8_t *data, size_t off, const char *text) { memcpy(data + off, text, strlen(text) + 1); }

static void test_guest_and_restriction(void) {
  CHECK(strcmp(qa_q3_standard_guest_weapons[1].label, "Machinegun") == 0);
  CHECK(QA_Q3_BASE_GUEST_WEAPON_COUNT == 10);
  qa_q3_guest_mounts mounts = {NULL, mounts_none};
  qa_q3_equipment_qvm_artifact unknown = make_artifact("sha256:unknown", "qagame", NULL, 0, NULL, 0);
  qa_q3_guest_weapon_list base = qa_q3_guest_weapons(&unknown, mounts, NULL, false);
  CHECK(base.count == 10);
  qa_q3_guest_weapon_list_destroy(&base);
  qa_q3_equipment_known_artifact known = {"missionpack"};
  unknown.known = &known;
  qa_q3_guest_weapon_list standard = qa_q3_guest_weapons(&unknown, mounts, NULL, false);
  CHECK(standard.count == 13);
  qa_q3_guest_weapon_list_destroy(&standard);

  /* Declared layout over synthesized item table (weapon + ammo rows). */
  uint8_t *data = calloc(1, 16384);
  CHECK(data != NULL);
  size_t table = 1024, stride = 52;
  put32(data, table + 0, 4096); put32(data, table + 28, 4224); put32(data, table + 36, 1); put32(data, table + 40, 5);
  put32(data, table + stride + 0, 4352); put32(data, table + stride + 28, 4480);
  put32(data, table + stride + 36, 2); put32(data, table + stride + 40, 5);
  put_str(data, 4096, "weapon_rocketlauncher"); put_str(data, 4224, "Rocket Launcher");
  put_str(data, 4352, "ammo_rockets"); put_str(data, 4480, "Rockets");
  qa_q3_guest_qvm_item_layout layout = {false, (double)table, false, 2, 0, false, (double)stride, 0, 28, 36, 40, 1, 2};
  qa_q3_equipment_qvm_artifact guest = make_artifact("sha256:guest", "qagame", NULL, 0, data, 16384);
  qa_q3_guest_weapon_list weapons = qa_q3_guest_weapons(&guest, mounts, &layout, false);
  CHECK(weapons.count == 1);
  CHECK(weapons.values[0].weapon == 5);
  CHECK(strcmp(weapons.values[0].item, "q3:weapon/rocketlauncher") == 0);
  CHECK(strcmp(weapons.values[0].ammo, "q3:ammo/rocketlauncher") == 0);
  CHECK(strcmp(weapons.values[0].label, "Rocket Launcher") == 0);
  qa_q3_guest_weapon_list_destroy(&weapons);
  /* Live declared layout returns empty. */
  layout.source_live = true;
  qa_q3_guest_weapon_list live = qa_q3_guest_weapons(&guest, mounts, &layout, false);
  CHECK(live.count == 0);
  qa_q3_guest_weapon_list_destroy(&live);
  free(data);

}

static bool read_no_product(void *ctx, const uint8_t **out, size_t *len) {
  (void)ctx; (void)out; (void)len; return false;
}
typedef struct product_ctx { const uint8_t *bytes; size_t length; } product_ctx;
static bool read_product(void *ctx, const uint8_t **out, size_t *len) {
  product_ctx *c = ctx; *out = c->bytes; *len = c->length; return true;
}
static void test_product_restriction(void) {
  /* Demo policy returns before reading the product id (NULL reader safe). */
  qa_q3_mount_restriction demo = qa_q3_resolve_mount_restriction(
      qa_q3_product_policy_make(true, false), false, NULL, NULL);
  CHECK(demo.kind == QA_Q3_MOUNT_DEMO);
  /* Null product id re-evaluates with fsRestrict=true -> demo. */
  qa_q3_mount_restriction forced = qa_q3_resolve_mount_restriction(
      qa_q3_product_policy_make(false, false), false, read_no_product, NULL);
  CHECK(forced.kind == QA_Q3_MOUNT_DEMO);
  /* All-zero id mismatches the first unscrambled byte and aborts. */
  uint8_t zeros[148] = {0};
  product_ctx zctx = {zeros, sizeof zeros};
  CHECK_ABORTS((void)qa_q3_resolve_mount_restriction(
      qa_q3_product_policy_make(false, false), false, read_product, &zctx));
}

static void test_presenter(void) {
  qa_q3_character_assets assets = {0};
  for (int i = 0; i < QA_Q3_TOTAL_ANIMATION_COUNT; i++) {
    assets.animation.animations[i] = (qa_q3_animation){0, 4, 4, 100, 100, false, false};
    assets.animation.animation_present[i] = i != 31;
  }
  qa_q3_character_presenter presenter;
  qa_q3_character_presenter_init(&presenter, &assets);
  qa_q3_character_view view = {0};
  view.animation = qa_q3_spawn_animation();
  view.color = (qa_vec4){1, 1, 1, 1};
  qa_q3_character_presenter_reset(&presenter, view, 1000);
  qa_q3_character_render_options options = {0};
  options.time_milliseconds = 1100; options.frame_milliseconds = 100;
  options.shader_time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 1100};
  options.swing_speed = 0.3;
  size_t count = 0;
  const qa_q3_character_pass *passes = qa_q3_character_presenter_frame(&presenter, view, options, &count);
  CHECK(count == 1);
  CHECK(passes[0].shader == NULL);
  view.powerups = (double)(1 << QA_Q3_PW_QUAD);
  passes = qa_q3_character_presenter_frame(&presenter, view, options, &count);
  CHECK(count == 2);
  CHECK(passes[1].shader && strcmp(passes[1].shader, "powerups/quad") == 0);
  view.powerups = (double)(1 << QA_Q3_PW_INVIS);
  passes = qa_q3_character_presenter_frame(&presenter, view, options, &count);
  CHECK(count == 1);
  CHECK(passes[0].shader && strcmp(passes[0].shader, "powerups/invisibility") == 0);
  view.source_flags = 0x80;
  (void)qa_q3_character_presenter_frame(&presenter, view, options, &count);
  CHECK(count == 0);
  qa_q3_character_presenter_destroy(&presenter);
  /* Movement hooks controls mapping. */
  qa_q3_hook_context hook_ctx = {0};
  hook_ctx.command.buttons = QA_Q3_BUTTON_ATTACK | QA_Q3_BUTTON_USE_HOLDABLE;
  hook_ctx.command.weapon = 5;
  qa_q3_arsenal_controls controls = qa_q3_source_arsenal_controls(hook_ctx);
  CHECK(controls.attack && controls.use_holdable && controls.requested_weapon == 5);
  /* stepQ3CharacterAnimation locomotion selection. */
  qa_animation_step_input anim_in = {0};
  anim_in.animation = (qa_actor_animation_state){"q3:character", qa_q3_spawn_animation()};
  anim_in.frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 16};
  anim_in.locomotion = "run";
  qa_animation_step_result stepped = qa_q3_step_character_animation(anim_in, "baseq3", false, 0);
  CHECK(stepped.animation.state.legs == (((QA_Q3_ANIM_LEGS_IDLE & 128) ^ 128) | QA_Q3_ANIM_LEGS_RUN));
  free((void *)stepped.effects);
}

int main(void) {
  test_arsenal();
  test_weapon_behavior_and_grip();
  test_animation_config();
  test_lerp_and_pose();
  test_events();
  test_character();
  test_equipment();
  test_guest_and_restriction();
  test_product_restriction();
  test_presenter();
  printf("q3_foundation: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}



