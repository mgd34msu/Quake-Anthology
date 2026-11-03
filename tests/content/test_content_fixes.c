/*
 * Small-defect fixes in content (small-fixes batch). Expected values come
 * from the TypeScript under bun: tests/app/q3-tinfo.test.ts, and the oracle
 * runs quoted at each check. Ownership fixes (mount references, Q1 emitted
 * text, Q1 character table, rerelease monster storage, mod declarations and
 * discovery) are pinned by running this binary under ASan with leak
 * detection: each case must finish clean.
 * GPL-2.0-or-later.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "q1_world_fixture.h"
#include "qa/content/mods/catalog.h"
#include "qa/content/mods/declaration.h"
#include "qa/content/mounts.h"
#include "qa/content/q1/base/player.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q2/rerelease/monsters.h"
#include "qa/content/q3/presentation/console.h"
#include "qa/content/q3/presentation/draw_icons.h"
#include "qa/content/q3/presentation/server_commands.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/foundation/support.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                    \
  do {                                                                                 \
    checks++;                                                                          \
    if (!(cond)) {                                                                     \
      failures++;                                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
    }                                                                                  \
  } while (0)

static int sentinel;
#define SENTINEL ((void *)&sentinel)

/* ---- item 4: a reference the mounter owns can be read back ---- */
static void test_mount_owned_reference(void) {
  char root[] = "/tmp/qa-small-fixes-mountXXXXXX";
  char *made = mkdtemp(root);
  CHECK(made != NULL);
  if (made == NULL) return;
  char maps[256], file[320];
  (void)snprintf(maps, sizeof maps, "%s/maps", root);
  CHECK(mkdir(maps, 0700) == 0);
  (void)snprintf(file, sizeof file, "%s/loose.bsp", maps);
  FILE *out = fopen(file, "wb");
  CHECK(out != NULL);
  if (out == NULL) return;
  (void)fwrite("loose", 1, 5, out);
  (void)fclose(out);
  qa_content_mount mounts[1];
  memset(mounts, 0, sizeof mounts);
  mounts[0].kind = QA_CONTENT_MOUNT_LOOSE;
  mounts[0].as.loose.identity = qa_create_mount_identity("mount:fixes:loose", "q2:classic:baseq2:installed", 0);
  mounts[0].as.loose.root_path = root;
  qa_mount_id order[1] = {"mount:fixes:loose"};
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:fixes:1";
  plan.mounts = mounts;
  plan.mount_count = 1;
  plan.default_order = order;
  plan.default_order_count = 1;
  qa_content_mounted *mounted = qa_open_mount_plan(&plan, NULL);
  CHECK(mounted != NULL);
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  CHECK(qa_content_mounted_open(mounted, "maps/loose.bsp", NULL, NULL, &opened));
  free(opened.bytes);
  /* TS read(resource) re-stores the same reference: reading the mounter's
   * own reference twice keeps its fields (no use after free). */
  for (int pass = 0; pass < 2; pass++) {
    size_t count = 0;
    const qa_resolved_resource_reference *owned = qa_content_mounted_opened_resources(mounted, &count);
    CHECK(count == 1);
    size_t length = 0;
    uint8_t *bytes = qa_content_mounted_read_reference(mounted, &owned[0], &length);
    CHECK(length == 5 && bytes != NULL && memcmp(bytes, "loose", 5) == 0);
    free(bytes);
    owned = qa_content_mounted_opened_resources(mounted, &count);
    CHECK(count == 1 && strcmp(owned[0].requested_path, "maps/loose.bsp") == 0);
  }
  qa_content_mounted_destroy(mounted);
  (void)unlink(file);
  (void)rmdir(maps);
  (void)rmdir(root);
}

/* ---- item 6: presentation ---- */
typedef struct tinfo_host { qa_q3_pres_client_game_static_state *static_state; } tinfo_host;
static qa_q3_pres_client_info *tinfo_client_info(void *ctx, double index) {
  tinfo_host *host = ctx;
  return &host->static_state->client_info[(size_t)index];
}
static const char *tinfo_config(void *ctx, double index) { (void)ctx; (void)index; return ""; }
static void tinfo_refresh(void *ctx) { (void)ctx; }
static void tinfo_set_cvar(void *ctx, const char *name, const char *value) { (void)ctx; (void)name; (void)value; }
static void run_tinfo(qa_q3_base_product product, const char *const *argv, size_t argc,
                      qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state) {
  CHECK(qa_q3_pres_client_game_state_init(state, product, 0, 0));
  qa_q3_pres_client_game_static_state_init(static_state, product);
  tinfo_host host = {static_state};
  qa_q3_pres_client_server_command_host h;
  memset(&h, 0, sizeof h);
  h.state = state;
  h.static_state = static_state;
  h.clients_ctx = &host;
  h.client_info = tinfo_client_info;
  h.ctx = &host;
  h.config_string = tinfo_config;
  h.refresh_game_state = tinfo_refresh;
  h.set_cvar = tinfo_set_cvar;
  qa_q3_pres_client_server_command_runtime *runtime = NULL;
  CHECK(qa_q3_pres_client_server_command_runtime_create(&runtime, h));
  CHECK(qa_q3_pres_client_server_command_runtime_execute_command(runtime, argv, argc));
  qa_q3_pres_client_server_command_runtime_destroy(runtime);
}
static void test_tinfo(void) {
  /* TS q3-tinfo.test.ts: 40 entries clamp to the 32-player overlay. */
  char storage[1 + 1 + 32 * 6][16];
  const char *argv[2 + 32 * 6];
  size_t argc = 0;
  argv[argc++] = "tinfo";
  argv[argc++] = "40";
  for (int slot = 0; slot < 32; slot++) {
    int values[6] = {slot + 10, slot, 100 - slot, slot, slot % 16, slot * 2};
    for (int i = 0; i < 6; i++) {
      (void)snprintf(storage[argc], sizeof storage[argc], "%d", values[i]);
      argv[argc] = storage[argc];
      argc++;
    }
  }
  qa_q3_pres_client_game_state state;
  qa_q3_pres_client_game_static_state static_state;
  run_tinfo(QA_Q3_BASE_PRODUCT_BASEQ3, argv, argc, &state, &static_state);
  CHECK(state.num_sorted_team_players == 32);
  CHECK(state.sorted_team_players[31] == 41);
  CHECK(static_state.client_info[41].health == 69);
  CHECK(static_state.client_info[41].cur_weapon == 15);
  CHECK(static_state.client_info[41].powerups == 62);
  qa_q3_pres_client_game_state_destroy(&state);
  qa_q3_pres_client_game_static_state_destroy(&static_state);
  /* TS: an entry naming client 99 ends the list there (count 1, slot 1). */
  const char *const stop[] = {"tinfo", "3", "1", "0", "50", "5", "7", "0", "99", "0", "50", "5", "7", "0",
                              "2", "0", "50", "5", "7", "0"};
  run_tinfo(QA_Q3_BASE_PRODUCT_BASEQ3, stop, sizeof stop / sizeof stop[0], &state, &static_state);
  CHECK(state.num_sorted_team_players == 1);
  CHECK(state.sorted_team_players[0] == 1);
  qa_q3_pres_client_game_state_destroy(&state);
  qa_q3_pres_client_game_static_state_destroy(&static_state);
}

static qa_cvar_snapshot console_vm_cvar(void *ctx, qa_q3_pres_console_cvar name) {
  CHECK(ctx == SENTINEL);
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.integer_value = name == QA_Q3_PRES_CONSOLE_CVAR_CURRENT_SELECTED_PLAYER ? 20 : 0;
  return snapshot;
}
static bool console_no_flag(void *ctx) { CHECK(ctx == SENTINEL); return false; }
static void test_console_next_order(void) {
  /* bun oracle: missionpack, time 1000, snap client 0, sortedTeamPlayers[20] = 0,
   * cg_currentSelectedPlayer 20 -> {"currentOrder":1,"orderPending":true,"orderTime":4000}. */
  qa_q3_pres_client_game_state state;
  qa_q3_pres_client_game_static_state static_state;
  CHECK(qa_q3_pres_client_game_state_init(&state, QA_Q3_BASE_PRODUCT_MISSIONPACK, 0, 0));
  qa_q3_pres_client_game_static_state_init(&static_state, QA_Q3_BASE_PRODUCT_MISSIONPACK);
  qa_q3_pres_retail_snapshot *snap = calloc(1, sizeof *snap);
  CHECK(snap != NULL);
  if (snap == NULL) return;
  snap->player_state.client_num = 0;
  state.snap = snap;
  state.time = 1000;
  state.sorted_team_players[20] = 0;
  qa_q3_pres_client_console_host host;
  memset(&host, 0, sizeof host);
  host.ctx = SENTINEL;
  host.read_vm_cvar = console_vm_cvar;
  host.orders_available = true;
  host.orders_ctx = SENTINEL;
  host.other_team_has_flag = console_no_flag;
  host.your_team_has_flag = console_no_flag;
  qa_q3_pres_client_console_runtime runtime;
  qa_q3_pres_client_console_runtime_init(&runtime, &state, &static_state, host, &state, &state);
  const char *const argv[] = {"nextOrder"};
  (void)qa_q3_pres_client_console_runtime_execute(&runtime, argv, 1);
  CHECK(static_state.current_order == 1);
  CHECK(static_state.order_pending);
  CHECK(static_state.order_time == 4000);
  state.snap = NULL;
  free(snap);
  qa_q3_pres_client_game_state_destroy(&state);
  qa_q3_pres_client_game_static_state_destroy(&static_state);
}

static qa_q3_pres_client_draw_icon_settings icons_3d(void *ctx) {
  CHECK(ctx == SENTINEL);
  qa_q3_pres_client_draw_icon_settings settings = {true, true};
  return settings;
}
static void test_draw_head_default_model(void) {
  /* bun oracle: draw3dIcons, default head model, deferred client -> 0 pictures.
   * The tools are zeroed: any draw would fail. */
  qa_q3_pres_client_game_state state;
  qa_q3_pres_client_game_static_state static_state;
  CHECK(qa_q3_pres_client_game_state_init(&state, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 0));
  qa_q3_pres_client_game_static_state_init(&static_state, QA_Q3_BASE_PRODUCT_BASEQ3);
  static_state.client_info[3].deferred = true;
  CHECK(static_state.client_info[3].head_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT);
  qa_q3_pres_client_draw_tools tools;
  memset(&tools, 0, sizeof tools);
  qa_q3_pres_draw_icons_media media;
  memset(&media, 0, sizeof media);
  media.product = QA_Q3_BASE_PRODUCT_BASEQ3;
  media.static_state = &static_state;
  qa_q3_pres_client_draw_icons icons;
  qa_q3_pres_client_draw_icons_init(&icons, &state, &tools, media, icons_3d, SENTINEL, NULL);
  qa_rect rect = {0, 0, 10, 10};
  qa_q3_pres_client_draw_icons_draw_head(&icons, rect, 3, (qa_vec3){0, 0, 0});
  CHECK(true);
  qa_q3_pres_client_game_state_destroy(&state);
  qa_q3_pres_client_game_static_state_destroy(&static_state);
}

/* ---- item 8: Q1 emitted text and the character table ---- */
typedef struct q1_world {
  q1_world_fixture fx;
  qa_q1_campaign_state campaign;
  qa_q1_base *base;
  qa_owned_actor player;
} q1_world;
static void q1_world_create(q1_world *w) {
  memset(w, 0, sizeof *w);
  CHECK(q1_world_fixture_create(&w->fx, q1_world_fixture_options()));
  qa_q1_campaign_state_init(&w->campaign, 0, 1);
  qa_q1_base_options options;
  memset(&options, 0, sizeof options);
  options.campaign = &w->campaign.binding;
  w->base = qa_q1_register_base(w->fx.game, &options);
  CHECK(w->base != NULL);
  w->player = q1_world_fixture_admit_player(&w->fx, QA_Q1_ZERO, 100);
  (void)qa_q1_attach_player(w->fx.game, w->player, "shotgun", false, NULL);
}
static void q1_world_destroy(q1_world *w) {
  qa_q1_base_destroy(w->base);
  q1_world_fixture_destroy(&w->fx);
}
static void emit_formatted(qa_q1_services *game, const char *name) {
  char text[64], arg[32];
  (void)snprintf(text, sizeof text, "%s joins the team", name);
  (void)snprintf(arg, sizeof arg, "%s", name);
  qa_q1_msg_arg args[1];
  memset(args, 0, sizeof args);
  args[0].text = arg;
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "message";
  event.text = text;
  event.args = args;
  event.arg_count = 1;
  qa_q1_emit(game, event);
  memset(text, 'x', sizeof text - 1);
  memset(arg, 'y', sizeof arg - 1);
}
static void test_q1_emitted_text(void) {
  /* A host keeping a shallow copy of the event (the fixture does) reads the
   * text TS would have kept, after the emit site's stack is gone. */
  q1_world w;
  q1_world_create(&w);
  size_t before = w.fx.event_count;
  emit_formatted(w.fx.game, "Ranger");
  emit_formatted(w.fx.game, "Ranger");
  CHECK(w.fx.event_count == before + 2);
  if (w.fx.event_count == before + 2) {
    const qa_q1_event *first = &w.fx.events[before], *second = &w.fx.events[before + 1];
    CHECK(strcmp(first->text, "Ranger joins the team") == 0);
    CHECK(first->arg_count == 1 && strcmp(first->args[0].text, "Ranger") == 0);
    /* Equal texts share one game-owned copy. */
    CHECK(first->text == second->text && first->args == second->args);
  }
  q1_world_destroy(&w);
}
static void test_q1_character_destroy(void) {
  q1_world w;
  q1_world_create(&w);
  qa_q1_character_actor *character = qa_q1_character_actor_create(w.fx.game, w.player, NULL);
  CHECK(character != NULL);
  if (character == NULL) { q1_world_destroy(&w); return; }
  qa_q1_character_input input;
  memset(&input, 0, sizeof input);
  input.water_level = 3;
  input.water_type = "water";
  (void)qa_q1_character_actor_frame(character, 0, input);
  qa_q1_character_actor_pain(character, NULL, 10, false);
  qa_q1_actor *timer = NULL;
  for (size_t i = 0; i < w.fx.game->entity_count; i++)
    if (strcmp(w.fx.game->entities[i]->classname, "death_bubbles") == 0) timer = w.fx.game->entities[i];
  CHECK(timer != NULL);
  qa_q1_character_actor_destroy(character);
  /* The pending timer looks the owner up after the character is gone. */
  if (timer != NULL) qa_q1_run_think(w.fx.game, timer);
  q1_world_destroy(&w);
}

/* ---- item 9: rerelease monster definitions are owned by the module ---- */
static void test_rerelease_monster_storage(void) {
  qa_q2_game_services game;
  memset(&game, 0, sizeof game);
  game.options.edition = QA_Q2_EDITION_RERELEASE;
  qa_q2m_weapons weapons;
  memset(&weapons, 0, sizeof weapons);
  qa_q2m_monsters registry;
  memset(&registry, 0, sizeof registry);
  CHECK(qa_q2m_monsters_init(&registry, weapons, (qa_q2m_monster_hooks){0}));
  qa_q2mpm_monster_state source;
  qa_q2mpm_state_init(&source);
  qa_q2rm_monster_options options;
  memset(&options, 0, sizeof options);
  options.source = &source;
  options.expansion = "base";
  qa_q2rm_monsters_module module;
  qa_q2rm_register_rerelease_monsters(&module, &registry, options);
  CHECK(qa_q2m_monsters_definition(&registry, "monster_berserk", &game) != NULL);
  CHECK(module.arena.count > 0);
  qa_q2m_monsters_destroy(&registry);
  qa_q2rm_monsters_module_destroy(&module);
  CHECK(module.arena.count == 0);
  qa_q2mpm_state_destroy(&source);
  qa_q2rm_ordinary_module ordinary;
  CHECK(qa_q2m_monsters_init(&registry, weapons, (qa_q2m_monster_hooks){0}));
  qa_q2mpm_weapons mission;
  memset(&mission, 0, sizeof mission);
  qa_q2rm_register_rerelease_ordinary_monsters(&ordinary, &registry, mission);
  CHECK(qa_q2m_monsters_definition(&registry, "monster_chick", &game) != NULL);
  qa_q2m_monsters_destroy(&registry);
  qa_q2rm_ordinary_module_destroy(&ordinary);
}

/* ---- item 10: mod declarations and discovery results can be freed ---- */
#define DIGEST "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define HEAD "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"progs.dat\",\"digest\":\"" DIGEST "\"},\"actorFields\":[],\"callbacks\":[]"
static const char *const FILES[][2] = {
  {"gameplay-mods.json", "{\"version\":1,\"components\":["
    "{\"id\":\"broken\",\"title\":\"b\",\"purpose\":\"addition\",\"callbacks\":\"broken.json\",\"requires\":[\"q1-classic-id1/healthy\"],\"conflicts\":[]},"
    "{\"id\":\"badprog\",\"title\":\"p\",\"purpose\":\"addition\",\"callbacks\":\"badprog.json\",\"requires\":[],\"conflicts\":[]},"
    "{\"id\":\"wrongdigest\",\"title\":\"w\",\"purpose\":\"addition\",\"callbacks\":\"wrong.json\",\"requires\":[],\"conflicts\":[]},"
    "{\"id\":\"healthy\",\"title\":\"h\",\"purpose\":\"addition\",\"callbacks\":\"healthy.json\",\"requires\":[],\"conflicts\":[\"q1-classic-id1/broken\"]}]}"},
  {"broken.json", "{"},
  {"badprog.json", "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"C:/progs.dat\",\"digest\":\"" DIGEST "\"},\"actorFields\":[],\"callbacks\":[]}"},
  {"wrong.json", "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"other.dat\",\"digest\":\"" DIGEST "\"},\"actorFields\":[],\"callbacks\":[]}"},
  {"healthy.json", HEAD "}"},
  {"progs.dat", "x"},
  {"other.dat", "y"},
};
static bool fake_open(void *ctx, const char *path, const char *filter, qa_mod_catalog_file *out) {
  (void)filter;
  CHECK(ctx == SENTINEL);
  memset(out, 0, sizeof *out);
  for (size_t i = 0; i < sizeof FILES / sizeof FILES[0]; i++) {
    if (strcmp(FILES[i][0], path) != 0) continue;
    out->bytes = (const uint8_t *)FILES[i][1];
    out->length = strlen(FILES[i][1]);
    out->digest = strcmp(path, "progs.dat") == 0 ? DIGEST : "sha256:c";
    return true;
  }
  return false;
}
static void test_mod_ownership(void) {
  qa_mod_mounted_content mounted = {SENTINEL, fake_open};
  qa_mod_catalog_product product;
  memset(&product, 0, sizeof product);
  product.id = "q1:classic:id1:installed";
  product.expectation.id = "q1-classic-id1";
  product.expectation.title = "Quake";
  product.expectation.family = "q1";
  product.expectation.edition = "classic";
  size_t count = 0;
  qa_discovered_gameplay_mod *found = qa_discover_gameplay_mods(&product, &mounted, &count);
  CHECK(count == 4);
  if (count == 4) {
    CHECK(!found[0].is_available && !found[1].is_available && !found[2].is_available);
    CHECK(strcmp(found[2].description.unavailable_reason, "Executable differs from its callback declaration") == 0);
    CHECK(found[3].is_available && strcmp(found[3].mod.selection.id, "healthy") == 0);
    CHECK(strcmp(found[3].mod.source_title, "Quake (Q1)") == 0);
    CHECK(found[3].mod.conflicts_count == 1 && strcmp(found[3].mod.conflicts[0].id, "broken") == 0);
    CHECK(found[3].mod.declaration.runtime == QA_MOD_RUNTIME_QUAKEC &&
          strcmp(found[3].mod.declaration.as.quakec->program.path, "progs.dat") == 0);
  }
  qa_discovered_gameplay_mods_free(found, count);
  const char *json = HEAD "}";
  qa_mod_declaration declaration = qa_parse_gameplay_mod_declaration((const uint8_t *)json, strlen(json));
  CHECK(declaration.runtime == QA_MOD_RUNTIME_QUAKEC && declaration.as.quakec != NULL);
  if (declaration.as.quakec != NULL) CHECK(declaration.as.quakec->owner != NULL);
  qa_mod_declaration_free(&declaration);
  CHECK(declaration.as.quakec == NULL);
}

int main(void) {
  test_mount_owned_reference();
  test_tinfo();
  test_console_next_order();
  test_draw_head_default_model();
  test_q1_emitted_text();
  test_q1_character_destroy();
  test_rerelease_monster_storage();
  test_mod_ownership();
  if (failures) {
    fprintf(stderr, "%d of %d check(s) failed\n", failures, checks);
    return 1;
  }
  printf("test_content_fixes: %d checks ok\n", checks);
  return 0;
}
