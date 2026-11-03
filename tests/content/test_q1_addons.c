/*
 * Port of key cases from tests/gameplay/q1-addons/common.test.ts,
 * tests/gameplay/q1-addons/ctf/ctf.test.ts and
 * tests/gameplay/q1-addons/items/items.test.ts. As in the TS harness, the
 * addons run over the real Q1 game, Q1Base and world tables (random fixed
 * at 0.4; q1_addon_fixture.h). Every host callback checks a sentinel ctx.
 * GPL-2.0-or-later.
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

#include "q1_addon_fixture.h"
#include "qa/content/q1/base/rules.h"

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
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);          \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__); \
    }                                                                   \
  } while (0)

/* --- world: game + base + registerQ1CampaignAddons + one attached player --- */
typedef struct world {
  q1_addon_fixture af;
  qa_owned_actor player;
  int target_uses; /* uses observed on "test:count_use" targets */
} world;
static world *current_world;

static void count_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                      const qa_actor_id *activator) {
  (void)game; (void)entity; (void)other; (void)activator;
  Q1AF_CTX(ctx);
  current_world->target_uses++;
}

static void open_world_coop(world *w, qa_q1ad_program program, bool campaign, bool coop) {
  memset(w, 0, sizeof *w);
  current_world = w;
  qa_q1_options options = q1_world_fixture_options();
  options.coop = coop;
  options.campaign = program == QA_Q1AD_MG1 ? "q1:mg1" : program == QA_Q1AD_MG3 ? "q1:mg3" : "q1:ctf";
  q1af_open_world(&w->af, &options);
  w->af.context = campaign ? qa_q1ad_register_q1_campaign_addons(w->af.base, program, q1af_services())
                           : qa_q1ad_context_create(w->af.base, program, q1af_services());
  if (w->af.context == NULL) abort();
  qa_q1_callback_registry_register(w->af.fx.game->named, "test:count_use",
                                   (qa_q1_callback_handlers){.ctx = &q1af_sentinel, .use = count_use});
  w->player = q1_world_fixture_admit_player(&w->af.fx, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(w->af.fx.game, w->player, "axe", true, NULL);
}
static void open_world(world *w, qa_q1ad_program program, bool campaign) { open_world_coop(w, program, campaign, false); }
static void close_world(world *w) {
  q1af_close(&w->af);
  current_world = NULL;
}
/* A target that counts its uses (stands in for "useTargets happened"). */
static void counting_target(world *w, const char *targetname) {
  qa_q1_actor *target = q1af_create(&w->af, "info_notnull");
  qa_q1ad_set_text(&target->targetname, targetname);
  target->use = qa_q1_callback_registry_use(w->af.fx.game->named, target, "test:count_use");
}
static double inventory(world *w, const char *item) {
  return qa_shared_inventory_table_count(w->af.fx.inventory, w->player.id, item);
}

static void test_sigils(void) {
  for (int mg3 = 0; mg3 < 2; mg3++) {
    world w;
    open_world(&w, mg3 ? QA_Q1AD_MG3 : QA_Q1AD_MG1, true);
    qa_q1_actor *rune = q1af_create(&w.af, "item_sigil");
    rune->spawnflags = mg3 ? 136 : 16;
    qa_q1_spawn_entity(w.af.fx.game, rune);
    CHECK(strcmp(rune->model, mg3 ? "progs/end4.mdl" : "progs/mg1_rune5.mdl") == 0);
    q1af_touch(&w.af, rune, w.player.id);
    CHECK(w.af.campaign_flags == (mg3 ? 8 : 1040));
    CHECK(qa_q1ad_mg1_last_sigil(w.af.campaign_flags) == (mg3 ? 0 : 16));
    CHECK(qa_q1ad_mg3_rune_count(w.af.campaign_flags) == (mg3 ? 1 : 0));
    CHECK(strcmp(rune->solid, "none") == 0);
    CHECK(w.af.rune_events == 1);
    close_world(&w);
  }
}

static void test_counter_and_multitouch(void) {
  world w;
  open_world(&w, QA_Q1AD_MG1, true);
  counting_target(&w, "t");
  qa_q1_actor *counter = q1af_create(&w.af, "trigger_counter_timed");
  counter->count = 2;
  counter->delay = 2;
  qa_q1ad_set_text(&counter->target, "t");
  qa_q1_spawn_entity(w.af.fx.game, counter);
  q1af_use(&w.af, counter, &w.player.id);
  CHECK(counter->count == 1);
  q1af_think(&w.af, counter);
  CHECK(counter->count == 2);
  q1af_use(&w.af, counter, &w.player.id);
  q1af_use(&w.af, counter, &w.player.id);
  CHECK(w.target_uses == 1); /* "multi_use" fired once */
  CHECK(!qa_q1_live(w.af.fx.game, counter));
  w.target_uses = 0;
  counting_target(&w, "m");
  qa_q1_actor *trigger = q1af_create(&w.af, "trigger_multitouch");
  qa_q1ad_set_text(&trigger->target, "m");
  qa_q1_spawn_entity(w.af.fx.game, trigger);
  q1af_touch(&w.af, trigger, w.player.id);
  q1af_touch(&w.af, trigger, w.player.id);
  CHECK(w.target_uses == 1);
  q1af_think(&w.af, trigger);
  CHECK(w.target_uses == 2);
  q1af_touch(&w.af, trigger, w.player.id);
  CHECK(w.target_uses == 3);
  close_world(&w);
}

static void test_impulses(void) {
  world w;
  open_world(&w, QA_Q1AD_MG1, true);
  CHECK(qa_q1ad_handle_impulse(w.af.context, w.player.id, 99));
  CHECK(inventory(&w, "q1:key/silver") == 0);
  CHECK(inventory(&w, "q1:ammo/cells") == 200);
  CHECK(strcmp(qa_q1_player(w.af.fx.game, w.player.id)->weapon, "rocketlauncher") == 0);
  close_world(&w);

  open_world(&w, QA_Q1AD_MG3, true);
  CHECK(qa_q1ad_handle_impulse(w.af.context, w.player.id, 105));
  CHECK(qa_q1ad_handle_impulse(w.af.context, w.player.id, 223));
  CHECK(w.af.campaign_flags == (15 | 64 | 128));
  CHECK(w.af.skill_cvar == 3);
  w.af.campaign_flags = 64 | 128 | 8;
  CHECK(qa_q1ad_handle_impulse(w.af.context, w.player.id, 11));
  CHECK(w.af.campaign_flags == (64 | 128 | 8 | 1));
  close_world(&w);
}

static double fixed_random(void *ctx) { Q1AF_CTX(ctx); return 0.4; }

static void test_pure_helpers(void) {
  CHECK(qa_q1ad_light_style(0.5, false) == 'g');
  CHECK(qa_q1ad_light_style(0, false) == 'a');
  CHECK(qa_q1ad_light_style(1, true) == 'z');
  qa_q1ad_squad_spawn spawns[4];
  CHECK(qa_q1ad_horde_squad("3 grunts", 1, fixed_random, &q1af_sentinel, spawns, 4) == 3);
  CHECK(strcmp(spawns[0].monster, "grunt") == 0 && spawns[0].offset.y == -40);
  CHECK(qa_q1ad_horde_squad("3 grunts", 0, fixed_random, &q1af_sentinel, spawns, 4) == 2);
  const char *squad = NULL, *type = NULL;
  CHECK(qa_q1ad_choose_horde_squad(true, "fodder", fixed_random, &q1af_sentinel, &squad, &type));
  CHECK(strcmp(squad, "2 grunts, 1 dog") == 0 && strcmp(type, "normal") == 0);
  CHECK(!qa_q1ad_choose_horde_squad(true, "bosses", fixed_random, &q1af_sentinel, &squad, &type));
  CHECK(qa_q1ad_mg3_upgrade_flag("map2b") == 8192);
  CHECK(qa_q1ad_mg3_upgrade_flag("map1") == 1);
  CHECK(qa_q1ad_mg3_upgrade_flag("secret6") == 16384);
  CHECK(qa_q1ad_mg3_upgrade_flag("nowhere") == 0);
  CHECK(qa_q1ad_mg3_upgraded_maximum(50, 5) == 70);
  CHECK(qa_q1ad_mg3_weapon_rank("lightning") == 1);
  CHECK(qa_q1ad_mg3_weapon_rank("axe") == 8);
  CHECK(qa_q1ad_ctf_team_number("red") == 5);
  CHECK(strcmp(qa_q1ad_ctf_number_team(14), "blue") == 0);
}

/* Q1HordeServices with a sentinel ctx. */
static double hs_dead_flag(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); return 0; }
static bool hs_no_target(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); return false; }
static bool hs_is_bot(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); return false; }
static void hs_respawn_teammate(void *ctx, qa_actor_id player) { (void)player; Q1AF_CTX(ctx); }
static void hs_add_score(void *ctx, qa_actor_id player, double delta) { (void)player; (void)delta; Q1AF_CTX(ctx); }
static void hs_restart_session(void *ctx, const char *map, double flags) { (void)map; (void)flags; Q1AF_CTX(ctx); }

static void test_horde_prepare(void) {
  world w;
  open_world(&w, QA_Q1AD_MG1, true);
  qa_q1ad_horde *horde = qa_q1ad_register_horde(w.af.context, (qa_q1ad_horde_services){
      .ctx = &q1af_sentinel, .dead_flag = hs_dead_flag, .no_target = hs_no_target, .is_bot = hs_is_bot,
      .respawn_teammate = hs_respawn_teammate, .add_score = hs_add_score, .restart_session = hs_restart_session});
  CHECK(horde != NULL);
  qa_q1_actor *manager = q1af_spawn(&w.af, "horde_manager");
  CHECK(qa_q1ad_horde_prepare(horde, manager));
  CHECK(qa_q1_actor_number(manager, "fodder") == 3);
  CHECK(qa_q1_actor_number(manager, "army") == 1);
  CHECK(qa_q1_actor_number(manager, "wave") == 1);
  qa_q1ad_horde_change_keys(horde, "silver", 1);
  CHECK(inventory(&w, "q1:key/silver") == 1);
  qa_q1ad_horde_change_keys(horde, "silver", 1);
  CHECK(qa_q1_actor_number(manager, "keys_silver") == 2);
  qa_q1ad_horde_destroy(horde);
  close_world(&w);
}

/* Q1CtfServices with a sentinel ctx. */
static qa_q1ad_ctf_status ctf_last_status;
static bool ctf_has_status;
static double ctf_captures_red, ctf_captures_blue;
static const char *cs_name(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return "Player"; }
static bool cs_is_bot(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return false; }
static double cs_score(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return 0; }
static void cs_add_score(void *ctx, qa_actor_id actor, double delta) { (void)actor; (void)delta; Q1AF_CTX(ctx); }
static double cs_captures(void *ctx, const char *team) { Q1AF_CTX(ctx); return strcmp(team, "red") == 0 ? ctf_captures_red : ctf_captures_blue; }
static void cs_add_capture(void *ctx, const char *team) { (void)team; Q1AF_CTX(ctx); }
static qa_q1ad_ctf_input cs_input(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); qa_q1ad_ctf_input input; memset(&input, 0, sizeof input); return input; }
static void cs_consume_impulse(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); }
static bool cs_observer(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return false; }
static void cs_set_observer(void *ctx, qa_actor_id actor, bool observer) { (void)actor; (void)observer; Q1AF_CTX(ctx); }
static void cs_respawn(void *ctx, qa_actor_id actor, qa_q1_actor *spot) { (void)actor; (void)spot; Q1AF_CTX(ctx); }
static void cs_disconnect(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); }
static void cs_colors(void *ctx, qa_actor_id actor, double shirt, double pants) { (void)actor; (void)shirt; (void)pants; Q1AF_CTX(ctx); }
static bool cs_prompt_supported(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return true; }
static void cs_prompt(void *ctx, qa_actor_id actor, const char *title, const qa_q1ad_ctf_prompt_choice *choices, size_t count) {
  (void)actor; (void)title; (void)choices; (void)count; Q1AF_CTX(ctx);
}
static void cs_clear_prompt(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); }
static void cs_teleport(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity, double until) {
  (void)actor; (void)origin; (void)angles; (void)velocity; (void)until; Q1AF_CTX(ctx);
}
static void cs_select_grapple(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); }
static qa_item_id cs_selected_weapon(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return "q1:weapon/shotgun"; }
static qa_item_id cs_selected_ammo(void *ctx, qa_actor_id actor) { (void)actor; Q1AF_CTX(ctx); return "q1:ammo/shells"; }
static void cs_weapon_changed(void *ctx, qa_actor_id actor, qa_item_id acquired) { (void)actor; (void)acquired; Q1AF_CTX(ctx); }
static void cs_haste(void *ctx, qa_actor_id actor, bool enabled) { (void)actor; (void)enabled; Q1AF_CTX(ctx); }
static void cs_status(void *ctx, qa_actor_id actor, qa_q1ad_ctf_status status) {
  (void)actor; Q1AF_CTX(ctx);
  ctf_last_status = status;
  ctf_has_status = true;
}
static void cs_log(void *ctx, qa_actor_id actor, const char *action) { (void)actor; (void)action; Q1AF_CTX(ctx); }

static void test_ctf_update(void) {
  world w;
  open_world(&w, QA_Q1AD_CTF, false);
  qa_q1ad_ctf_services services = {
      .ctx = &q1af_sentinel, .name = cs_name, .is_bot = cs_is_bot, .score = cs_score, .add_score = cs_add_score,
      .captures = cs_captures, .add_capture = cs_add_capture, .input = cs_input, .consume_impulse = cs_consume_impulse,
      .observer = cs_observer, .set_observer = cs_set_observer, .respawn = cs_respawn, .disconnect = cs_disconnect,
      .colors = cs_colors, .prompt_supported = cs_prompt_supported, .prompt = cs_prompt, .clear_prompt = cs_clear_prompt,
      .teleport = cs_teleport, .select_grapple = cs_select_grapple, .selected_weapon = cs_selected_weapon,
      .selected_ammo = cs_selected_ammo, .weapon_changed = cs_weapon_changed, .haste = cs_haste, .status = cs_status,
      .log = cs_log};
  qa_q1ad_ctf *ctf = qa_q1ad_register_ctf(w.af.context, services, NULL);
  CHECK(ctf != NULL);
  CHECK(ctf->weapon_pickups.weapon_rank(ctf->weapon_pickups.ctx, "nailgun") == 6);
  qa_q1_actor *red = q1af_create(&w.af, "item_flag_team1");
  qa_q1_actor *blue = q1af_create(&w.af, "item_flag_team2");
  red->count = 0;
  blue->count = 2;
  ctf_captures_red = 3;
  ctf_captures_blue = 1;
  ctf_has_status = false;
  qa_q1ad_ctf_update(ctf, NULL);
  CHECK(ctf_has_status);
  CHECK(ctf_last_status.red == 3 && ctf_last_status.blue == 1);
  CHECK(ctf_last_status.flags == (1 | (4 << 3)));
  CHECK(ctf_last_status.rune_items == 0);
  qa_q1ad_ctf_destroy(ctf);
  close_world(&w);
}

static void test_context_words_and_aborts(void) {
  world w;
  open_world(&w, QA_Q1AD_MG3, true);
  qa_q1ad_set_player_number(w.af.context, w.player.id, "score", 42);
  CHECK(qa_q1ad_player_number(w.af.context, w.player.id, "score") == 42);
  const qa_q1_state_extension *extension = q1af_extension(&w.af);
  uint8_t *bytes = NULL;
  size_t length = 0;
  CHECK(extension != NULL && extension->capture(extension->ctx, &bytes, &length));
  CHECK(bytes != NULL && length > 0);
  qa_q1ad_set_player_number(w.af.context, w.player.id, "score", 1);
  extension->restore(extension->ctx, bytes, length);
  CHECK(qa_q1ad_player_number(w.af.context, w.player.id, "score") == 42);
  free(bytes);
  qa_session_actor_registry_release(w.af.fx.actors, w.player); /* host onRelease */
  CHECK(qa_q1ad_player_number(w.af.context, w.player.id, "score") == 0);
  CHECK_ABORTS(qa_q1ad_set_player_number(w.af.context, w.player.id, "x", 1));
  qa_q1_actor *corpse = q1af_create(&w.af, "misc_corpse");
  CHECK(qa_q1_actor_field_set(corpse, "style", "99"));
  CHECK_ABORTS(qa_q1_spawn_entity(w.af.fx.game, corpse));
  qa_q1_actor *exit_trigger = q1af_create(&w.af, "trigger_changelevel");
  CHECK_ABORTS(qa_q1_spawn_entity(w.af.fx.game, exit_trigger));
  close_world(&w);
}

/* tests/gameplay/q1-addons/coop-spawns.test.ts */
static void test_coop_checkpoints(void) {
  for (int mg3 = 0; mg3 < 2; mg3++) {
    world w;
    open_world_coop(&w, mg3 ? QA_Q1AD_MG3 : QA_Q1AD_MG1, true, true);
    qa_q1_services *game = w.af.fx.game;
    (void)q1af_spawn_fields(&w.af, "info_player_start", (const char *const[]){"origin", "0 0 0"}, 1);
    qa_q1_actor *open = q1af_spawn_fields(&w.af, "info_player_coop", (const char *const[]){"origin", "64 0 0"}, 1);
    qa_q1_actor *first = q1af_spawn_fields(&w.af, "info_player_coop", (const char *const[]){"origin", "128 0 0", "targetname", "cp1"}, 2);
    qa_q1_actor *second = q1af_spawn_fields(&w.af, "info_player_coop", (const char *const[]){"origin", "192 0 0", "targetname", "cp2"}, 2);
    qa_q1_actor *flagged = q1af_spawn_fields(&w.af, "info_player_coop",
                                             (const char *const[]){"origin", "256 0 0", "targetname", "cp3", "spawnflags", "1"}, 3);
    CHECK(open->activated && !first->activated && !second->activated && flagged->activated);
    bool seen64 = false, seen256 = false, seen_other = false;
    for (int i = 0; i < 6; i++) {
      qa_q1_actor *point = qa_q1_spawn_selector_select(qa_q1_base_spawn_selector(w.af.base), false);
      if (point == NULL || strcmp(point->classname, "info_player_coop") != 0) continue;
      double x = qa_q1_get_body(game, point).origin.x;
      if (x == 64) seen64 = true; else if (x == 256) seen256 = true; else seen_other = true;
    }
    CHECK(seen64 && seen256 && !seen_other);
    qa_q1_actor *trigger = q1af_spawn_fields(&w.af, "trigger_activate_coop_spawns", (const char *const[]){"target", "cp1"}, 1);
    qa_q1_call_use(game, trigger, qa_q1_callback_registry_use(game->named, trigger, "mg:coop_spawns_use"), &w.player.id, &w.player.id);
    CHECK(!open->activated && first->activated && !second->activated && !flagged->activated);
    CHECK(qa_q1_live(game, trigger));
    bool only_first = true;
    for (int i = 0; i < 6; i++) {
      qa_q1_actor *point = qa_q1_spawn_selector_select(qa_q1_base_spawn_selector(w.af.base), false);
      if (point != NULL && strcmp(point->classname, "info_player_coop") == 0 && point != first) only_first = false;
    }
    CHECK(only_first);
    close_world(&w);
  }
}

static void test_coop_restore_weapons(void) {
  for (int mg3 = 0; mg3 < 2; mg3++) {
    world w;
    open_world_coop(&w, mg3 ? QA_Q1AD_MG3 : QA_Q1AD_MG1, true, true);
    qa_q1_services *game = w.af.fx.game;
    qa_shared_inventory_table *inv = w.af.fx.inventory;
    qa_item_id rockets = qa_q1_services_weapon_item(game, "rocketlauncher"), lightning = qa_q1_services_weapon_item(game, "lightning");
    (void)qa_shared_inventory_table_give(inv, w.player, rockets, 1);
    (void)qa_shared_inventory_table_give(inv, w.player, lightning, 1);
    qa_q1_actor *checkpoint = q1af_spawn_fields(&w.af, "info_player_coop", (const char *const[]){"origin", "128 0 0", "targetname", "cp1"}, 2);
    qa_q1_actor *trigger = q1af_spawn_fields(&w.af, "trigger_activate_coop_spawns", (const char *const[]){"target", "cp1"}, 1);
    qa_q1_call_use(game, trigger, qa_q1_callback_registry_use(game->named, trigger, "mg:coop_spawns_use"), &w.player.id, &w.player.id);
    const char *held = qa_q1_actor_text(checkpoint, "coop.weapons");
    CHECK(strstr(held, "lightning") != NULL && strstr(held, "rocketlauncher") != NULL);
    (void)qa_shared_inventory_table_consume(inv, w.player, rockets, 1);
    (void)qa_shared_inventory_table_consume(inv, w.player, lightning, 1);
    (void)qa_shared_inventory_table_consume(inv, w.player, "q1:ammo/rockets", inventory(&w, "q1:ammo/rockets"));
    (void)qa_shared_inventory_table_consume(inv, w.player, "q1:ammo/cells", inventory(&w, "q1:ammo/cells"));
    (void)qa_shared_inventory_table_give(inv, w.player, "q1:ammo/cells", 20);
    CHECK(inventory(&w, rockets) == 0);
    qa_q1ad_restore_coop_weapons(w.af.context, w.player.id, checkpoint);
    CHECK(inventory(&w, rockets) == 1);
    CHECK(inventory(&w, lightning) == 1);
    CHECK(inventory(&w, qa_q1_services_weapon_item(game, "nailgun")) == 0);
    CHECK(inventory(&w, "q1:ammo/rockets") == 4);
    CHECK(inventory(&w, "q1:ammo/cells") == 20);
    CHECK(inventory(&w, "q1:ammo/nails") == 0);
    close_world(&w);
  }
}

int main(void) {
  test_sigils();
  test_counter_and_multitouch();
  test_impulses();
  test_pure_helpers();
  test_horde_prepare();
  test_ctf_update();
  test_context_words_and_aborts();
  test_coop_checkpoints();
  test_coop_restore_weapons();
  CHECK(q1af_ctx_errors == 0); /* every addon host callback saw its sentinel ctx */
  checks += q1af_ctx_checks;    /* each callback invocation's sentinel check */
  failures += q1af_ctx_errors;
  if (failures == 0) printf("ok: %d checks\n", checks);
  else printf("test_q1_addons: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
