/* C checks for src/app/bootstrap/startup-selection.ts and startup.ts,
 * reproducing the unit-reachable parts of
 * tests/first-playable/startup-selection.test.ts,
 * tests/first-playable/startup-monster-roster.test.ts and the startup.ts
 * application tests. Content rows need QTS_TEST_CORPUS (the TS corpus root);
 * without it they are skipped, as the TS tests skip. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/startup_application.h"
#include "qa/network/q3/browser_view.h"
#include "../../src/app/bootstrap/startup_application_internal.h"
#include "qa/app/bootstrap/startup_selection.h"
#include "qa/app/bootstrap/startup_input_profile.h"
#include "qa/app/bootstrap/menu_font.h"
#include "qa/app/bootstrap/startup_audio.h"
#include "qa/render/scene/resources.h"
#include "qa/core/commands/index.h"
#include "qa/input/seat.h"
#include "qa/app/bootstrap/team_arena_skirmish.h"
#include "qa/content/catalog/index.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/profile.h"
#include "qa/network/common/js_number.h"
#include "startup_menu_fixture.h"

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
#define CHECK_STR(actual, expected)                                     \
  do {                                                                  \
    const char *a_ = (actual), *e_ = (expected);                        \
    checks++;                                                           \
    if (a_ == NULL || e_ == NULL ? a_ != e_ : strcmp(a_, e_) != 0) {    \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s = \"%s\", expected \"%s\"\n",    \
              __FILE__, __LINE__, #actual, a_ ? a_ : "(null)",          \
              e_ ? e_ : "(null)");                                      \
    }                                                                   \
  } while (0)

static const char *corpus_root;
#include <dirent.h>
#include <strings.h>
/* TS corpusHasFile: the Steam layout heads and case-insensitive segments. */
static bool resolve_case_insensitive(const char *base, const char *relative) {
  char current[4096], rest[2048];
  snprintf(current, sizeof current, "%s", base);
  snprintf(rest, sizeof rest, "%s", relative);
  for (char *segment = strtok(rest, "/"); segment != NULL; segment = strtok(NULL, "/")) {
    DIR *dir = opendir(current);
    if (dir == NULL) return false;
    struct dirent *entry;
    char found[1024] = "";
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, segment) == 0) { snprintf(found, sizeof found, "%s", entry->d_name); break; }
      if (found[0] == '\0' && strcasecmp(entry->d_name, segment) == 0) snprintf(found, sizeof found, "%s", entry->d_name);
    }
    closedir(dir);
    if (found[0] == '\0') return false;
    size_t length = strlen(current);
    snprintf(current + length, sizeof current - length, "/%s", found);
  }
  struct stat info;
  return stat(current, &info) == 0;
}
static bool corpus_has(const char *relative) {
  static const char *const heads[][2] = {{"q1/", "Quake/"}, {"q2/", "Quake 2/"}, {"q3a/", "Quake 3 Arena/"}};
  if (resolve_case_insensitive(corpus_root, relative)) return true;
  for (size_t i = 0; i < 3; i++) {
    size_t length = strlen(heads[i][0]);
    if (strncmp(relative, heads[i][0], length) != 0) continue;
    char steam[2048];
    snprintf(steam, sizeof steam, "%s%s", heads[i][1], relative + length);
    return resolve_case_insensitive(corpus_root, steam);
  }
  return false;
}

/* ---- qa_try wrappers (TS expect(...).toThrow(message)) ---- */
typedef struct call {
  qa_app_startup_selection_model *m;
  const char *field, *id, *classname;
  qa_app_startup_hosting hosting;
  int kind;
} call;
static void call_body(void *ctx) {
  call *c = ctx;
  switch (c->kind) {
    case 0: qa_app_startup_selection_select(c->m, c->field, c->id); break;
    case 1: qa_app_startup_selection_set_hosting(c->m, c->hosting); break;
    case 2: qa_app_startup_selection_select_monster(c->m, c->classname, c->id); break;
    case 3: qa_app_startup_selection_select_monster_source(c->m, c->id); break;
    case 4: qa_app_startup_selection_team_write(c->m, c->field, c->id); break;
    case 5: { qa_app_startup_launch l; qa_app_startup_selection_resolve(c->m, &l); qa_app_startup_launch_free(&l); break; }
    case 6: { qa_app_startup_launch l; qa_app_startup_selection_resolve_preset(c->m, c->id, c->classname != NULL, c->hosting.port, NULL, &l);
              qa_app_startup_launch_free(&l); break; }
    default: break;
  }
}
static bool throws(call c, const char *contains) {
  qa_caught_error caught;
  if (qa_try(call_body, &c, &caught)) return false;
  if (contains != NULL && strstr(caught.message, contains) == NULL) {
    fprintf(stderr, "  thrown: %s\n", caught.message);
    return false;
  }
  return true;
}
static bool select_throws(qa_app_startup_selection_model *m, const char *field, const char *id, const char *contains) {
  call c = {m, field, id, NULL, {0}, 0};
  return throws(c, contains);
}

static const qa_app_startup_selection_row *row_of(qa_app_startup_selection_model *m, const char *id) {
  size_t count = 0;
  const qa_app_startup_selection_row *rows = qa_app_startup_selection_rows(m, &count);
  for (size_t i = 0; i < count; i++) if (strcmp(rows[i].id, id) == 0) return &rows[i];
  return NULL;
}
static const qa_app_startup_selection_choice *choice_of(const qa_app_startup_selection_row *row, const char *id) {
  if (row == NULL) return NULL;
  for (size_t i = 0; i < row->choice_count; i++) if (strcmp(row->choices[i].id, id) == 0) return &row->choices[i];
  return NULL;
}
static const char *product_content(qa_app_startup_selection_model *m, const char *id) {
  return qa_content_catalog_require(qa_app_startup_selection_catalog(m), id)->id;
}
static qa_app_options parse_options(const char *const *argv, size_t argc) {
  qa_content_root_search search = {0};
  qa_app_application_command command = qa_app_parse_application_command(argv, argc, search, NULL);
  qa_app_options options = command.options;
  memset(&command.options, 0, sizeof command.options);
  qa_app_application_command_destroy(&command);
  return options;
}
static qa_content_catalog *discover(bool mods) {
  qa_content_catalog *catalog = calloc(1, sizeof *catalog);
  qa_discover_content_options options;
  memset(&options, 0, sizeof options);
  options.corpus_root = corpus_root;
  options.discover_mods = mods;
  options.inspector = qa_app_default_archive_inspector();
  if (!qa_discover_installed_content(&options, catalog)) abort();
  return catalog;
}
static void options_check(qa_app_startup_selection_model *m, void (*check)(const qa_app_options *, void *), void *ctx) {
  qa_app_options options;
  qa_app_startup_selection_options(m, &options);
  check(&options, ctx);
  qa_app_options_destroy(&options);
}
static const char *g_text;
static void map_is(const qa_app_options *o, void *ctx) { (void)ctx; CHECK_STR(o->map, g_text); }
static void rules_ctf_dm(const qa_app_options *o, void *ctx) { (void)ctx; CHECK_STR(o->rules, "ctf"); CHECK_STR(o->mode, "deathmatch"); }
static void model_is(const qa_app_options *o, void *ctx) { (void)ctx; CHECK_STR(o->character_model, g_text); }
static void mode_is(const qa_app_options *o, void *ctx) { (void)ctx; CHECK_STR(o->mode, g_text); }
static void network_is(const qa_app_options *o, void *ctx) { (void)ctx; CHECK_STR(o->network_kind, g_text); }
static void no_q1_protocol(const qa_app_options *o, void *ctx) { (void)ctx; CHECK(!o->has_q1_protocol); }
static const qa_q1_protocol_identity *g_protocol;
static void q1_protocol_is(const qa_app_options *o, void *ctx) {
  (void)ctx;
  CHECK(o->has_q1_protocol && o->q1_protocol.version == g_protocol->version && strcmp(o->q1_protocol.kind, g_protocol->kind) == 0);
}

/* TS "startup choices resolve independent installed source selections without starting a world". */
static void test_independent_selections(void) {
  if (!corpus_has("q3a/baseq3/pak0.pk3") || !corpus_has("q2/baseq2/pak0.pak")) { printf("skipped: independent selections\n"); return; }
  const char *argv[] = {"--content-root", corpus_root};
  qa_app_options options = parse_options(argv, 2);
  qa_app_startup_selection_model *model = qa_app_startup_selection_model_new(discover(false), &options);
  qa_app_startup_selection_prepare_maps(model);
  CHECK(choice_of(row_of(model, "model"), "sarge") != NULL);
  const qa_app_startup_selection_choice *demota = choice_of(row_of(model, "product"), "q3-demota");
  if (corpus_has("q3a/demota/pak0.pk3")) CHECK(demota != NULL && demota->unavailable == NULL);
  else CHECK(demota != NULL && demota->unavailable != NULL);
  qa_app_startup_launch original;
  qa_app_startup_selection_resolve(model, &original);
  CHECK(original.recipe.weapons.count == 1);
  CHECK_STR(original.recipe.weapons.values[0].provider, "q2:official");
  CHECK_STR(original.recipe.weapons.values[0].content, product_content(model, "q2-classic-baseq2"));
  qa_app_startup_launch_free(&original);
  qa_app_startup_selection_select(model, "weapons", "q3-baseq3");
  qa_app_startup_selection_select(model, "movement", "q2-rerelease-baseq2");
  qa_app_startup_selection_select(model, "grapple", "offhand");
  qa_app_startup_selection_select(model, "grappleStyle", "q2-ctf");
  qa_app_startup_selection_select(model, "grenades", "enabled");
  qa_app_startup_selection_select(model, "product", "q1-rerelease-id1");
  g_text = "maps/start.bsp"; options_check(model, map_is, NULL);
  CHECK(choice_of(row_of(model, "map"), "maps/b_bh10.bsp") == NULL);
  CHECK(select_throws(model, "map", "maps/b_bh10.bsp", "Unknown map"));
  qa_app_startup_selection_select(model, "map", "maps/dm4.bsp");
  qa_app_startup_selection_select(model, "mode", "deathmatch");
  qa_app_startup_selection_set_display(model, 1280, 720, 1.3);
  qa_app_startup_launch selected;
  qa_app_startup_selection_resolve(model, &selected);
  CHECK_STR(selected.recipe.map.geometry_content, product_content(model, "q1-rerelease-id1"));
  CHECK_STR(selected.recipe.map.geometry.requested_path, "maps/dm4.bsp");
  CHECK_STR(selected.options.map, "maps/dm4.bsp");
  CHECK(selected.recipe.weapons.count == 1);
  CHECK_STR(selected.recipe.weapons.values[0].provider, "q3:official");
  CHECK_STR(selected.recipe.weapons.values[0].content, product_content(model, "q3-baseq3"));
  bool rerelease_clock = false;
  for (size_t i = 0; i < selected.recipe.timing_count; i++)
    if (strcmp(selected.recipe.timing[i].provider, selected.recipe.movement.provider) == 0)
      rerelease_clock = selected.recipe.timing[i].clock.kind == QA_CLOCK_Q2_RERELEASE;
  CHECK(rerelease_clock);
  {
    qa_app_options for_recipe = qa_app_application_options_for_recipe(&selected.options, qa_app_startup_selection_catalog(model), &selected.recipe);
    CHECK(for_recipe.movement == QA_GAME_FAMILY_Q2);
    CHECK_STR(for_recipe.movement_product, "q2-rerelease-baseq2");
    qa_app_options_for_recipe_free(&for_recipe);
  }
  CHECK(!selected.recipe.campaign.is_campaign);
  CHECK(selected.recipe.equipment.grapple.is_enabled);
  CHECK_STR(selected.recipe.equipment.grapple.mechanic, "q2-ctf");
  CHECK_STR(selected.recipe.equipment.grapple.binding, "offhand");
  CHECK(selected.recipe.equipment.hand_grenades.is_enabled);
  const qa_app_startup_selection_row *grenades = row_of(model, "grenades");
  CHECK(grenades != NULL && grenades->choice_count == 2 && strcmp(grenades->choices[0].label, "Off") == 0 && strcmp(grenades->choices[1].label, "On") == 0);
  const qa_app_startup_selection_row *grapple = row_of(model, "grapple");
  CHECK(grapple != NULL && grapple->choice_count == 3 && strcmp(grapple->choices[1].label, "Weapon slot") == 0 && strcmp(grapple->choices[2].label, "Offhand") == 0);
  const qa_app_startup_selection_row *styles = row_of(model, "grappleStyle");
  bool threewave = false, plain = true;
  for (size_t i = 0; styles != NULL && i < styles->choice_count; i++) {
    if (strcmp(styles->choices[i].label, "Threewave CTF (Quake 2)") == 0) threewave = true;
    if (strstr(styles->choices[i].label, "offhand") || strstr(styles->choices[i].label, "weapon slot")) plain = false;
  }
  CHECK(threewave && plain);
  qa_ui_binding_capabilities capabilities = qa_app_startup_selection_binding_capabilities(model);
  CHECK(!capabilities.chat && capabilities.score_command == NULL && capabilities.offhand_grapple && capabilities.offhand_grenades);
  CHECK(selected.options.width == 1280 && selected.options.height == 720 && selected.options.gamma == 1.3);
  CHECK_STR(selected.options.mode, "deathmatch");
  CHECK(selected.options.movement == QA_GAME_FAMILY_Q2 && selected.options.character == QA_GAME_FAMILY_Q3);
  CHECK_STR(selected.options.character_model, "sarge");
  qa_app_startup_launch_free(&selected);
  size_t lines = 0;
  const char *const *summary = qa_app_startup_selection_summary(model, &lines);
  bool pickups = false;
  for (size_t i = 0; i < lines; i++) if (strstr(summary[i], "Independent pickup replacement is not implemented")) pickups = true;
  CHECK(pickups);
  CHECK(select_throws(model, "map", "maps/not-installed.bsp", "Unknown map"));
  if (corpus_has("q3a/demota/pak0.pk3")) CHECK(!select_throws(model, "product", "q3-demota", NULL));
  else CHECK(select_throws(model, "product", "q3-demota", NULL));
  qa_app_startup_selection_select(model, "product", "q2-classic-baseq2");
  g_text = "maps/base1.bsp"; options_check(model, map_is, NULL);
  qa_app_startup_selection_select(model, "rules", "ctf");
  qa_app_startup_launch ctf;
  qa_app_startup_selection_resolve(model, &ctf);
  CHECK_STR(ctf.recipe.match.provider, "q2:ctf");
  qa_app_startup_launch_free(&ctf);
  qa_app_startup_selection_select(model, "product", "q2-classic-ctf");
  qa_app_startup_selection_resolve(model, &ctf);
  CHECK_STR(ctf.recipe.match.provider, "q2:ctf");
  qa_app_startup_launch_free(&ctf);
  qa_app_startup_selection_select(model, "product", "q2-classic-baseq2");
  qa_app_startup_selection_select(model, "mode", "singleplayer");
  options_check(model, rules_ctf_dm, NULL);
  qa_app_startup_selection_resolve(model, &ctf);
  CHECK_STR(ctf.recipe.match.provider, "q2:ctf");
  qa_app_startup_launch_free(&ctf);
  qa_app_startup_selection_select(model, "rules", "standard");
  qa_app_startup_selection_resolve(model, &ctf);
  CHECK_STR(ctf.recipe.match.provider, "q2:official");
  qa_app_startup_launch_free(&ctf);
  CHECK_STR(row_of(model, "weapons")->value, "q3-baseq3");
  static const char *const cases[3][2] = {{"q1-classic-id1", "player"}, {"q2-classic-baseq2", "male"}, {"q3-baseq3", "sarge"}};
  for (int i = 0; i < 3; i++) {
    qa_app_startup_selection_select(model, "character", cases[i][0]);
    g_text = cases[i][1]; options_check(model, model_is, NULL);
    const qa_app_startup_selection_choice *appearance = choice_of(row_of(model, "model"), cases[i][1]);
    CHECK(appearance != NULL && appearance->unavailable == NULL);
  }
  {
    qa_app_options inferred_options;
    qa_app_startup_options_copy(&inferred_options, &options);
    free(inferred_options.product); inferred_options.product = strdup("q2-classic-ctf");
    inferred_options.mode = "deathmatch";
    qa_app_startup_selection_model *inferred = qa_app_startup_selection_model_new(discover(false), &inferred_options);
    qa_app_options inferred_view;
    qa_app_startup_selection_options(inferred, &inferred_view);
    CHECK_STR(inferred_view.rules, "ctf");
    qa_app_options_destroy(&inferred_view);
    qa_app_startup_selection_model_destroy(inferred);
    qa_app_options_destroy(&inferred_options);
  }
  qa_app_startup_hosting hosting = qa_app_startup_selection_hosting(model);
  CHECK_STR(hosting.kind, "offline");
  CHECK(hosting.port == 27910 && !hosting.has_q1_protocol);
  qa_app_startup_hosting unified = {"unified-server", 28123, false, {0}};
  qa_app_startup_selection_set_hosting(model, unified);
  g_text = "coop"; options_check(model, mode_is, NULL);
  qa_app_startup_launch hosted;
  qa_app_startup_selection_resolve(model, &hosted);
  CHECK_STR(hosted.options.network_kind, "unified-server");
  CHECK_STR(hosted.options.network_host, "0.0.0.0");
  CHECK(hosted.options.network_port == 28123);
  qa_app_startup_launch_free(&hosted);
  call bad = {model, NULL, NULL, NULL, {"native-server", 65536, false, {0}}, 1};
  CHECK(throws(bad, "Port must"));
  hosting = qa_app_startup_selection_hosting(model);
  CHECK_STR(hosting.kind, "unified-server");
  CHECK(hosting.port == 28123 && !hosting.has_q1_protocol);
  qa_app_startup_hosting native = {"native-server", 27910, false, {0}};
  qa_app_startup_selection_set_hosting(model, native);
  qa_app_startup_selection_resolve(model, &hosted);
  CHECK_STR(hosted.options.network_kind, "native-server");
  qa_app_startup_launch_free(&hosted);
  qa_app_startup_hosting offline = {"offline", 27910, false, {0}};
  qa_app_startup_selection_set_hosting(model, offline);
  g_text = "offline"; options_check(model, network_is, NULL);
  qa_app_startup_selection_model_destroy(model);
  qa_app_options_destroy(&options);
}

/* TS "Q1 hosting selects native protocol and removes stale protocol outside NetQuake hosting". */
static void test_q1_hosting(void) {
  if (!corpus_has("q1/id1/PAK0.PAK")) { printf("skipped: q1 hosting\n"); return; }
  const char *argv[] = {"--content-root", corpus_root, "--game", "q1-classic-id1", "--mode", "deathmatch"};
  qa_app_options options = parse_options(argv, 6);
  qa_app_startup_selection_model *model = qa_app_startup_selection_model_new(discover(false), &options);
  static const double versions[3] = {15, 666, 999};
  for (int i = 0; i < 3; i++) {
    qa_q1_protocol_identity protocol = qa_default_netquake_profile(versions[i]);
    qa_app_startup_hosting hosting = {"native-server", 26000, true, protocol};
    qa_app_startup_selection_set_hosting(model, hosting);
    g_protocol = &protocol; options_check(model, q1_protocol_is, NULL);
    qa_app_startup_hosting read = qa_app_startup_selection_hosting(model);
    CHECK(read.has_q1_protocol && read.q1_protocol.version == protocol.version);
  }
  qa_app_startup_hosting current = qa_app_startup_selection_hosting(model);
  current.kind = "unified-server";
  qa_app_startup_selection_set_hosting(model, current);
  options_check(model, no_q1_protocol, NULL);
  qa_app_startup_hosting again = {"native-server", 26000, true, qa_default_netquake_profile(999)};
  qa_app_startup_selection_set_hosting(model, again);
  current = qa_app_startup_selection_hosting(model);
  current.kind = "offline";
  qa_app_startup_selection_set_hosting(model, current);
  options_check(model, no_q1_protocol, NULL);
  static const char *const products[3] = {"q1-quakeworld", "q2-classic-baseq2", "q3-baseq3"};
  for (int i = 0; i < 3; i++) {
    qa_app_options other_options;
    qa_app_startup_options_copy(&other_options, &options);
    free(other_options.product); other_options.product = strdup(products[i]);
    other_options.has_q1_protocol = true; other_options.q1_protocol = qa_default_netquake_profile(666);
    other_options.network_kind = "native-server";
    free(other_options.network_host); other_options.network_host = strdup("0.0.0.0");
    other_options.network_port = 26000;
    qa_app_startup_selection_model *other = qa_app_startup_selection_model_new(discover(false), &other_options);
    CHECK(!qa_app_startup_selection_hosting(other).has_q1_protocol);
    options_check(other, no_q1_protocol, NULL);
    qa_app_startup_selection_model_destroy(other);
    qa_app_options_destroy(&other_options);
  }
  qa_app_startup_selection_model_destroy(model);
  qa_app_options_destroy(&options);
}

/* ---- startup.ts StartupApplication members (routeCommand, connect,
 * frontendCommand). TS has no unit test for these; the expectations come
 * from the TS bodies. Every callback checks its sentinel ctx. ---- */
static int sentinel_value;
#define SENTINEL ((void *)&sentinel_value)
static char printed[8192];
static int bad_ctx;
static void sentinel_print(void *ctx, const char *text) {
  if (ctx != SENTINEL) bad_ctx++;
  strncat(printed, text, sizeof printed - strlen(printed) - 1);
}
static qa_command_context local_seat_context(void) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  return context;
}
static bool route(qa_startup_application *app, const char *name, const char *const *args, size_t count, qa_command_context source) {
  printed[0] = '\0';
  return qa_startup_application_route_command(app, name, args, count, source, NULL, true);
}
typedef struct route_raise { qa_startup_application *app; const char *name; const char *const *args; size_t count; } route_raise;
static void route_raise_body(void *ctx) {
  route_raise *r = ctx;
  (void)qa_startup_application_route_command(r->app, r->name, r->args, r->count, local_seat_context(), NULL, true);
}
static char *make_temp_root(const char *prefix) {
  const char *base = getenv("TMPDIR");
  char *root = malloc(4096);
  snprintf(root, 4096, "%s/%sXXXXXX", base != NULL && base[0] != '\0' ? base : "/tmp", prefix);
  if (mkdtemp(root) == NULL) abort();
  return root;
}
static void remove_tree(const char *path) {
  char command[4352];
  snprintf(command, sizeof command, "rm -rf '%s'", path);
  if (system(command) != 0) fprintf(stderr, "could not remove %s\n", path);
}
static qa_startup_application_host sentinel_host(void) {
  qa_startup_application_host host;
  memset(&host, 0, sizeof host);
  host.base.ctx = SENTINEL;
  host.base.print = sentinel_print;
  host.base.has_external_save_directories = true; /* TS externalSaveDirectories: [] */
  return host;
}
static void test_route_command(void) {
  const char *saved_root = corpus_root;
  corpus_root = "/nonexistent/quake-test-corpus-is-not-configured";
  char *user_root = make_temp_root("startup-route-");
  const char *argv[] = {"--content-root", corpus_root, "--user-content-root", user_root};
  qa_app_options options = parse_options(argv, 4);
  qa_app_startup_selection_model *model = qa_app_startup_selection_model_new(discover(false), &options);
  qa_startup_application *app = qa_startup_application_create(model, sentinel_host(), "/tmp/qa-startup-port-saves", "menu");
  qa_command_context local = local_seat_context();
  qa_command_context remote = local;
  remote.origin.kind = QA_COMMAND_ORIGIN_REMOTE_CLIENT;
  const char *none[1] = {NULL};
  CHECK(!route(app, "quit", none, 0, remote));
  qa_command_origin caller = remote.origin;
  qa_command_context script = local;
  script.origin.kind = QA_COMMAND_ORIGIN_SCRIPT; script.origin.name = "autoexec.cfg"; script.origin.caller = &caller;
  CHECK(!route(app, "quit", none, 0, script));
  /* record family */
  const char *two[2] = {"a", "b"};
  CHECK(route(app, "record", two, 2, local)); CHECK_STR(printed, "Usage: record <name>\n");
  CHECK(qa_startup_application_pending_recording(app) == NULL);
  CHECK(route(app, "rerecord", none, 0, local)); CHECK_STR(printed, "Usage: rerecord <name>\n");
  CHECK(route(app, "record", none, 0, local)); CHECK(qa_startup_application_pending_recording(app) != NULL);
  CHECK_STR(qa_startup_application_pending_recording(app)->kind, "record");
  CHECK(qa_startup_application_pending_recording(app)->name == NULL);
  const char *demo[1] = {"demo1"};
  CHECK(route(app, "mvdrecord", demo, 1, local)); CHECK_STR(qa_startup_application_pending_recording(app)->kind, "mvdrecord");
  CHECK_STR(qa_startup_application_pending_recording(app)->name, "demo1");
  /* The pending intent keeps a copy of its source (the script caller chain outlives the invocation). */
  CHECK(route(app, "serverrecord", demo, 1, script) == false);
  caller.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  CHECK(route(app, "serverrecord", demo, 1, script));
  caller.kind = QA_COMMAND_ORIGIN_REMOTE_CLIENT;
  const qa_app_startup_recording_intent *intent = qa_startup_application_pending_recording(app);
  CHECK(intent->source.origin.kind == QA_COMMAND_ORIGIN_SCRIPT && intent->source.origin.caller != &caller
        && intent->source.origin.caller->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT);
  CHECK(route(app, "stoprecord", none, 0, local)); CHECK_STR(qa_startup_application_pending_recording(app)->kind, "stop");
  CHECK(route(app, "serverstop", none, 0, local)); CHECK_STR(qa_startup_application_pending_recording(app)->kind, "serverstop");
  CHECK(route(app, "mvdstop", demo, 1, local)); CHECK_STR(printed, "Usage: mvdstop\n");
  /* cinematic */
  CHECK(route(app, "cinematic", none, 0, local)); CHECK_STR(printed, "Usage: cinematic <name> [loop|hold]\n");
  const char *loop[2] = {"intro", "loop"}, *hold[2] = {"intro", "1"};
  CHECK(route(app, "cinematic", loop, 2, local));
  const qa_app_startup_movie_request *movie = qa_startup_application_pending_movie(app);
  CHECK(movie != NULL && movie->loop && !movie->hold); CHECK_STR(movie->name, "intro");
  CHECK(route(app, "cinematic", hold, 2, local));
  movie = qa_startup_application_pending_movie(app);
  CHECK(!movie->loop && movie->hold && !movie->silent);
  /* disconnect, then in_restart without a retained client throws */
  CHECK(route(app, "disconnect", none, 0, local));
  CHECK(qa_startup_application_pending(app) != NULL && qa_startup_application_pending(app)->kind == QA_APP_STARTUP_ACTION_FRONTEND);
  route_raise r = {app, "in_restart", none, 0};
  qa_caught_error caught;
  CHECK(!qa_try(route_raise_body, &r, &caught) && strcmp(caught.message, "Input command requires the retained client") == 0);
  /* mvdconnect / mvdisconnect */
  qa_startup_application_clear_pending(app);
  CHECK(route(app, "mvdconnect", none, 0, local));
  CHECK_STR(printed, "Usage: mvdconnect [-u user] [-p password] [-n name] <address[:port]>\n");
  const char *help[1] = {"-h"};
  CHECK(route(app, "mvdconnect", help, 1, local));
  CHECK_STR(printed, "Usage: mvdconnect [-u user] [-p password] [-n name] <address[:port]>\n");
  CHECK(qa_startup_application_pending(app) == NULL);
  const char *connect_gtv[3] = {"-n", "tv", "gtv.example:27910"};
  CHECK(route(app, "mvdconnect", connect_gtv, 3, local));
  const qa_app_startup_action *pending = qa_startup_application_pending(app);
  CHECK(pending != NULL && pending->kind == QA_APP_STARTUP_ACTION_GTV);
  CHECK_STR(pending->gtv_address, "gtv.example:27910"); CHECK_STR(pending->gtv_username, "unnamed");
  CHECK_STR(pending->gtv_password, ""); CHECK_STR(pending->gtv_label, "tv");
  qa_startup_application_clear_pending(app);
  CHECK(route(app, "mvdisconnect", none, 0, local)); CHECK_STR(printed, "No GTV connections.\n");
  CHECK(route(app, "mvdisconnect", help, 1, local)); CHECK_STR(printed, "Usage: mvdisconnect [-a|--all] [conn_id]\n");
  /* this.gtv?.source === this.client?.source.current: a published GTV source selects the current connection. */
  int source_identity = 0;
  qa_app_client_source_lifetime lifetime;
  memset(&lifetime, 0, sizeof lifetime);
  lifetime.ctx = &source_identity;
  qa_app_client_bootstrap client;
  memset(&client, 0, sizeof client);
  client.source_current = &lifetime;
  app->client = &client;
  app->gtv_source = (qa_remote_application *)&source_identity;
  app->gtv_id_value = 3;
  app->gtv_label = strdup("net3");
  const char *by_label[1] = {"net3"}, *wrong[1] = {"9"};
  CHECK(route(app, "mvdisconnect", by_label, 1, local));
  CHECK(qa_startup_application_pending(app) != NULL && qa_startup_application_pending(app)->kind == QA_APP_STARTUP_ACTION_FRONTEND);
  CHECK(route(app, "mvdisconnect", wrong, 1, local)); CHECK_STR(printed, "No such connection ID: 9\n");
  client.source_current = NULL; /* the GTV source is not the current source: no connection */
  CHECK(route(app, "mvdisconnect", by_label, 1, local)); CHECK_STR(printed, "No GTV connections.\n");
  app->client = NULL;
  app->gtv_source = NULL;
  free(app->gtv_label); app->gtv_label = NULL;
  qa_startup_application_clear_pending(app);
  /* connect */
  CHECK(route(app, "connect", none, 0, local));
  CHECK_STR(printed, "Usage: connect <address>; use qts://<address> for mixed-game servers\n");
  const char *server[1] = {"qts://"};
  route_raise c = {app, "connect", server, 1};
  CHECK(!qa_try(route_raise_body, &c, &caught) && strcmp(caught.message, "Connect has no prepared command owner") == 0);
  /* map / load */
  const char *bad_map[1] = {"../x"};
  CHECK(route(app, "map", bad_map, 1, local)); CHECK_STR(printed, "Invalid map name: ../x\n");
  CHECK(qa_startup_application_pending(app) == NULL);
  const char *e1m1[1] = {"e1m1"};
  CHECK(route(app, "map", e1m1, 1, local));
  pending = qa_startup_application_pending(app);
  CHECK(pending != NULL && pending->kind == QA_APP_STARTUP_ACTION_INITIAL);
  CHECK_STR(pending->options.map, "maps/e1m1.bsp"); CHECK_STR(pending->options.network_kind, "offline");
  CHECK(route(app, "devmap", e1m1, 1, local) == false);
  const char *slot[1] = {"s1"}, *outside[1] = {"/etc/passwd.sav"};
  CHECK(route(app, "load", slot, 1, local));
  pending = qa_startup_application_pending(app);
  CHECK(pending != NULL && pending->kind == QA_APP_STARTUP_ACTION_LOAD);
  CHECK_STR(pending->path, "/tmp/qa-startup-port-saves/s1.sav"); CHECK(pending->source_product == NULL);
  qa_startup_application_clear_pending(app);
  caller.kind = QA_COMMAND_ORIGIN_REMOTE_CLIENT;
  CHECK(route(app, "load", outside, 1, script) == false); /* a script whose caller is remote is filtered first */
  caller.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  CHECK(route(app, "load", outside, 1, script));
  CHECK(strstr(printed, "load of a path outside the save directory is only accepted from the local console") != NULL);
  CHECK(qa_startup_application_pending(app) == NULL);
  CHECK(route(app, "load", outside, 1, local));
  CHECK(qa_startup_application_pending(app) != NULL && qa_startup_application_pending(app)->kind == QA_APP_STARTUP_ACTION_LOAD);
  qa_startup_application_clear_pending(app);
  /* frontendCommand falls through to the TS message (no graphics: snd_restart/music/cd are not handled). */
  printed[0] = '\0';
  qa_startup_application_frontend_command(app, "snd_restart", none, 0, local);
  CHECK_STR(printed, "Cannot execute snd_restart without an active world.\n");
  const char *track[1] = {"2"};
  printed[0] = '\0';
  qa_startup_application_frontend_command(app, "music", track, 1, local);
  CHECK_STR(printed, "Cannot execute music without an active world.\n");
  printed[0] = '\0';
  qa_startup_application_frontend_command(app, "cd", track, 1, local);
  CHECK_STR(printed, "Cannot execute cd without an active world.\n");
  /* connect(connection) */
  qa_app_browser_connection connection = {"qw", "10.0.0.1:27500", false, NULL, 0};
  qa_app_options connected;
  qa_startup_application_connection_options(app, &connection, &connected);
  CHECK_STR(connected.product, "q1-quakeworld"); CHECK_STR(connected.map, "maps/e1m1.bsp");
  CHECK_STR(connected.network_kind, "qw-client"); CHECK_STR(connected.network_remote, "10.0.0.1:27500");
  CHECK_STR(connected.character_model, "player"); CHECK_STR(connected.rules, "standard"); CHECK(!connected.dedicated);
  qa_app_options_destroy(&connected);
  qa_app_browser_connection kex = {"q2", "10.0.0.2:5069", true, "q2-kex", 2023};
  qa_startup_application_connection_options(app, &kex, &connected);
  CHECK_STR(connected.product, "q2-rerelease-baseq2"); CHECK_STR(connected.network_kind, "q2-client");
  CHECK(connected.has_q2_protocol && strcmp(connected.q2_protocol.kind, "q2-kex") == 0 && connected.q2_protocol.version == 2023);
  CHECK(connected.movement == QA_GAME_FAMILY_Q2 && connected.character == QA_GAME_FAMILY_Q2);
  CHECK_STR(connected.character_model, "male");
  qa_app_options_destroy(&connected);
  /* quit */
  CHECK(route(app, "quit", none, 0, local)); CHECK(qa_startup_application_quit_requested(app));
  CHECK(bad_ctx == 0);
  qa_startup_application_close(app);
  qa_startup_application_destroy(app);
  qa_app_startup_selection_model_destroy(model);
  qa_app_options_destroy(&options);
  remove_tree(user_root);
  free(user_root);
  corpus_root = saved_root;
}

/* ---- StartupApplication members reachable without openGraphics: the TS private
 * constructor over a model with no installed content. Expected values from
 * build/agents/app-startup2/tmp/oracle/startup_unit.ts under bun:
 *   inputSeat null / input false / activeGame null
 *   readPixels "Startup menu is not visible"; step "Startup frame has no client"
 *   publishPendingSource with a staged disconnect: pending null, status and print
 *     "Retained client is unavailable"
 *   publishPendingSource with a staged q2-client connect: "Startup has no retained client"
 *   a staged record without a recorder is consumed without output
 *   dispatchApplicationRequest from a local seat with no client: "Ignoring application
 *     request from an inactive or retired client seat.\n", quit not requested
 *   dispatch midiinfo from the server console throws "Input command requires the retained client"
 *   dispatch god from the server console: "Cannot execute god without an active world.\n"
 *   requestQuit; step returns; close prints nothing; a second close returns. ---- */
typedef struct unit_call { qa_startup_application *app; int kind; qa_app_configuration_command_request request; } unit_call;
static void unit_body(void *ctx) {
  unit_call *call = ctx;
  switch (call->kind) {
    case 0: { size_t length = 0; free(qa_startup_application_read_pixels(call->app, &length)); break; }
    case 1: qa_startup_application_step(call->app); break;
    case 2: qa_startup_publish_pending_source(call->app); break;
    case 3: qa_startup_dispatch_application_request(call->app, &call->request); break;
    case 4: qa_startup_application_close(call->app); break;
    default: break;
  }
}
static bool unit_run(qa_startup_application *app, int kind, const qa_app_configuration_command_request *request, char *message) {
  unit_call call;
  memset(&call, 0, sizeof call);
  call.app = app; call.kind = kind;
  if (request != NULL) call.request = *request;
  qa_caught_error caught;
  message[0] = '\0';
  if (qa_try(unit_body, &call, &caught)) return true;
  memcpy(message, caught.message, sizeof caught.message);
  return false;
}
static void test_unit_members(void) {
  const char *saved_root = corpus_root;
  corpus_root = "/nonexistent/quake-test-corpus-is-not-configured";
  char *user_root = make_temp_root("startup-unit-");
  const char *argv[] = {"--content-root", corpus_root, "--user-content-root", user_root};
  qa_app_options options = parse_options(argv, 4);
  qa_app_startup_selection_model *model = qa_app_startup_selection_model_new(discover(false), &options);
  char saves[4200];
  snprintf(saves, sizeof saves, "%s/saves", user_root);
  qa_startup_application *app = qa_startup_application_create(model, sentinel_host(), saves, "menu");
  qa_seat_id seat;
  CHECK(!qa_startup_application_input_seat(app, &seat));
  qa_seat_input_event key;
  memset(&key, 0, sizeof key);
  key.kind = "key"; key.code = 13; key.down = true;
  CHECK(!qa_startup_application_input(app, &key));
  CHECK(qa_startup_application_active_game(app) == NULL);
  char message[sizeof ((qa_caught_error *)0)->message];
  CHECK(!unit_run(app, 0, NULL, message)); CHECK_STR(message, "Startup menu is not visible");
  CHECK(!unit_run(app, 1, NULL, message)); CHECK_STR(message, "Startup frame has no client");
  qa_command_context local = local_seat_context();
  const char *none[1] = {NULL};
  CHECK(route(app, "disconnect", none, 0, local));
  CHECK(qa_startup_application_pending(app)->kind == QA_APP_STARTUP_ACTION_FRONTEND);
  printed[0] = '\0';
  CHECK(unit_run(app, 2, NULL, message));
  CHECK(qa_startup_application_pending(app) == NULL);
  CHECK_STR(qa_startup_application_status(app), "Retained client is unavailable");
  CHECK_STR(printed, "Retained client is unavailable\n");
  /* A staged initial q2-client action goes through connectOptions. */
  const char *server[1] = {"10.0.0.1:27910"};
  app->has_pending = true;
  memset(&app->pending, 0, sizeof app->pending);
  app->pending.kind = QA_APP_STARTUP_ACTION_INITIAL;
  qa_app_startup_selection_options(model, &app->pending.options);
  app->pending.options.network_kind = "q2-client";
  free(app->pending.options.network_remote); app->pending.options.network_remote = strdup(server[0]);
  printed[0] = '\0';
  CHECK(unit_run(app, 2, NULL, message));
  CHECK_STR(qa_startup_application_status(app), "Startup has no retained client");
  CHECK_STR(printed, "Startup has no retained client\n");
  const char *demo[1] = {"demo1"};
  CHECK(route(app, "record", demo, 1, local));
  printed[0] = '\0';
  CHECK(unit_run(app, 2, NULL, message));
  CHECK(qa_startup_application_pending_recording(app) == NULL);
  CHECK_STR(printed, "");
  qa_app_configuration_command_request request;
  memset(&request, 0, sizeof request);
  request.target = "application"; request.name = "quit"; request.source = local;
  printed[0] = '\0';
  CHECK(unit_run(app, 3, &request, message));
  CHECK_STR(printed, "Ignoring application request from an inactive or retired client seat.\n");
  CHECK(!qa_startup_application_quit_requested(app));
  request.name = "midiinfo";
  request.source.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  CHECK(!unit_run(app, 3, &request, message)); CHECK_STR(message, "Input command requires the retained client");
  request.name = "god";
  printed[0] = '\0';
  CHECK(unit_run(app, 3, &request, message));
  CHECK_STR(printed, "Cannot execute god without an active world.\n");
  qa_startup_application_request_quit(app);
  CHECK(qa_startup_application_quit_requested(app));
  CHECK(unit_run(app, 1, NULL, message));
  printed[0] = '\0';
  CHECK(unit_run(app, 4, NULL, message)); CHECK_STR(printed, "");
  CHECK(unit_run(app, 4, NULL, message));
  CHECK(bad_ctx == 0);
  qa_startup_application_destroy(app);
  qa_app_startup_selection_model_destroy(model);
  qa_app_options_destroy(&options);
  remove_tree(user_root);
  free(user_root);
  corpus_root = saved_root;
}

/* ---- Q3BrowserView.getPing over fractional cl_maxPing (TS Math.max(100, maxPing())).
 * Expected (maxPing, elapsed, time) from build/agents/app-startup2/tmp/oracle/browser_view_threshold.ts:
 *   150.5 150 -> 0; 150.5 151 -> 151; 150 150 -> 150; NaN 150 -> 150; 99.5 99 -> 0;
 *   99.5 100 -> 100; -1 100 -> 100. Address "10.0.0.7:27960". ---- */
typedef struct view_mock { int magic; double now, max_ping; int bad; } view_mock;
#define VIEW_MAGIC 0x51b0
static view_mock *view_of(void *ctx) {
  view_mock *mock = ctx;
  if (mock == NULL || mock->magic != VIEW_MAGIC) { bad_ctx++; abort(); }
  return mock;
}
static bool vm_entry(void *ctx, const qa_q3_mirror_network_address *address, const qa_server_status **status_out) {
  (void)view_of(ctx); (void)address; *status_out = NULL; return false;
}
static void vm_release(void *ctx, qa_q3_mirror_discovery_handle handle) { (void)view_of(ctx); (void)handle; }
static bool vm_result(void *ctx, qa_q3_mirror_discovery_handle handle, qa_q3_mirror_request_result *out) {
  (void)view_of(ctx); (void)handle;
  memset(out, 0, sizeof *out);
  out->kind = QA_Q3_REQ_PENDING;
  return true;
}
static qa_q3_mirror_discovery_handle vm_request(void *ctx, const qa_q3_mirror_network_address *address, double now,
    qa_q3_discovery_request_kind kind) {
  (void)view_of(ctx); (void)address; (void)now; (void)kind; return 7;
}
static qa_q3_browser_list_state vm_list(void *ctx, int64_t source) {
  (void)view_of(ctx); (void)source;
  qa_q3_browser_list_state state;
  memset(&state, 0, sizeof state);
  return state;
}
static bool vm_resolve(void *ctx, const char *text, qa_q3_mirror_network_address *out) {
  (void)view_of(ctx); (void)text;
  memset(out, 0, sizeof *out);
  out->kind = QA_Q3_ADDR_IPV4;
  out->host[0] = 10; out->host[1] = 0; out->host[2] = 0; out->host[3] = 7;
  out->port = 27960;
  return true;
}
static void vm_assert(void *ctx) { (void)view_of(ctx); }
static double vm_now(void *ctx) { return view_of(ctx)->now; }
static double vm_max_ping(void *ctx) { return view_of(ctx)->max_ping; }
static double vm_resend(void *ctx) { (void)view_of(ctx); return 750; }
static void vm_print(void *ctx, const char *text) { (void)view_of(ctx); (void)text; }
static void test_browser_view_threshold(void) {
  static const double cases[][3] = {{150.5, 150, 0}, {150.5, 151, 151}, {150, 150, 150}, {NAN, 150, 150},
                                    {99.5, 99, 0}, {99.5, 100, 100}, {-1, 100, 100}};
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    view_mock mock = {VIEW_MAGIC, 1000, cases[i][0], 0};
    qa_q3_browser_view_options options;
    memset(&options, 0, sizeof options);
    options.browser.q3_core.entry_status = vm_entry;
    options.browser.q3_core.release_request = vm_release;
    options.browser.q3_core.request_result = vm_result;
    options.browser.q3_core.request = vm_request;
    options.browser.q3_core.ctx = &mock;
    options.browser.q3_list = vm_list;
    options.browser.resolve_q3 = vm_resolve;
    options.browser.assert_open = vm_assert;
    options.browser.ctx = &mock;
    options.now = vm_now; options.max_ping = vm_max_ping; options.status_resend_time = vm_resend;
    options.print = vm_print; options.print_ctx = &mock; options.ctx = &mock;
    qa_q3_browser_view *view = NULL;
    CHECK(qa_q3_browser_view_init(&view, &options));
    qa_q3_browser_view_ping(view, "10.0.0.7:27960");
    mock.now += cases[i][1];
    char *address = NULL;
    int64_t time = -1;
    CHECK(qa_q3_browser_view_get_ping(view, 0, 64, NULL, NULL, &address, &time));
    CHECK_STR(address, "10.0.0.7:27960");
    if ((double)time != cases[i][2]) { failures++; fprintf(stderr, "FAIL getPing maxPing %g elapsed %g: %lld, expected %g\n", cases[i][0], cases[i][1], (long long)time, cases[i][2]); }
    checks++;
    free(address);
    qa_q3_browser_view_close(view);
    qa_q3_browser_view_destroy(view);
  }
  CHECK(bad_ctx == 0);
}

/* ---- StartupMenu browser operations report failures in browser.status (TS
 * browserControls run(work)), never aborting. Expected text from
 * build/agents/app-startup2/tmp/oracle/browser_query_error.ts under bun:
 *   query of "[::1]:26000" on the q1 list: "Address family does not match selection";
 *   connection() with an empty address: "Invalid direct server address". ---- */
typedef struct menu_fixture { int magic; int connects; double now; } menu_fixture;
#define MENU_MAGIC 0x3e2a
static menu_fixture *menu_of(void *ctx) {
  menu_fixture *fixture = ctx;
  if (fixture == NULL || fixture->magic != MENU_MAGIC) { bad_ctx++; abort(); }
  return fixture;
}
static double menu_now(void *ctx) { return menu_of(ctx)->now; }
static void menu_play(void *ctx) { (void)menu_of(ctx); }
static void menu_load(void *ctx, const char *id) { (void)menu_of(ctx); (void)id; }
static const qa_app_startup_save_list *menu_saves(void *ctx) {
  static qa_app_startup_save_list empty;
  (void)menu_of(ctx);
  return &empty;
}
static void menu_refresh(void *ctx) { (void)menu_of(ctx); }
static void menu_quit(void *ctx) { (void)menu_of(ctx); }
static void menu_connect(void *ctx, qa_app_browser_connection connection) {
  menu_of(ctx)->connects++;
  qa_app_browser_connection_destroy(&connection);
}
static void click(qa_app_startup_menu *menu, qa_seat_id seat, double x, double y) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.seat = seat;
  event.kind = "mouse-motion"; event.position.x = x; event.position.y = y;
  (void)qa_app_startup_menu_input(menu, event);
  memset(&event, 0, sizeof event);
  event.seat = seat;
  event.kind = "mouse-button"; event.button = 1; event.down = true;
  (void)qa_app_startup_menu_input(menu, event);
  event.down = false;
  (void)qa_app_startup_menu_input(menu, event);
}
static void test_menu_browser_errors(void) {
  char *root = make_temp_root("startup-menu-browser-");
  qa_config_store config;
  CHECK(qa_config_store_init(&config, root));
  qa_app_startup_server_browser *browser = qa_app_startup_server_browser_open(&config);
  CHECK(browser != NULL);
  menu_fixture fixture = {MENU_MAGIC, 0, 0};
  char *user_root = make_temp_root("startup-menu-user-");
  startup_menu_fixture menu_owners;
  if (!startup_menu_fixture_open(&menu_owners, corpus_root, user_root, false)) {
    printf("skipped: menu browser errors (no q1-classic-id1)\n");
    qa_app_startup_server_browser_close(browser);
    qa_app_startup_server_browser_destroy(browser);
    qa_config_store_destroy(&config);
    remove_tree(root); free(root); remove_tree(user_root); free(user_root);
    return;
  }
  qa_seat_id seat = menu_owners.seat;
  qa_app_startup_menu_options options = startup_menu_fixture_options(&menu_owners, &fixture);
  options.now = menu_now;
  options.play = menu_play;
  options.load = menu_load;
  options.saves = menu_saves;
  options.refresh_saves = menu_refresh;
  options.quit = menu_quit;
  options.browser = browser;
  options.connect = menu_connect;
  qa_app_startup_menu *menu = qa_app_startup_menu_new(options);
  CHECK(menu != NULL);
  qa_app_startup_menu_ensure_active_menu(menu);
  qa_app_startup_menu_resume_server_browser(menu);
  CHECK_STR(qa_app_startup_server_browser_protocol(browser), "q1");
  qa_app_startup_server_browser_set_address(browser, "[::1]:26000");
  click(menu, seat, 100, 190); /* Query (64, 180, 160x30) */
  CHECK_STR(qa_app_startup_server_browser_status(browser), "Address family does not match selection");
  qa_app_startup_server_browser_set_address(browser, "");
  click(menu, seat, 100, 410); /* Connect (64, 396, 160x30) */
  CHECK_STR(qa_app_startup_server_browser_status(browser), "Invalid direct server address");
  CHECK(fixture.connects == 0);
  click(menu, seat, 450, 190); /* Favorite (416, 180, 160x30) */
  CHECK_STR(qa_app_startup_server_browser_status(browser), "Invalid direct server address");
  CHECK(bad_ctx == 0);
  qa_app_startup_menu_close(menu);
  qa_app_startup_menu_destroy(menu);
  startup_menu_fixture_close(&menu_owners);
  remove_tree(user_root);
  free(user_root);
  qa_app_startup_server_browser_close(browser);
  qa_app_startup_server_browser_destroy(browser);
  qa_config_store_destroy(&config);
  remove_tree(root);
  free(root);
}

static bool rows_equal(qa_app_startup_selection_model *m, char ***saved, size_t *saved_count) {
  /* Serialize rows (id=value:choices...) to compare snapshots. */
  size_t count = 0;
  const qa_app_startup_selection_row *rows = qa_app_startup_selection_rows(m, &count);
  char **lines = calloc(count + 1, sizeof *lines);
  for (size_t i = 0; i < count; i++) {
    size_t size = 256;
    for (size_t c = 0; c < rows[i].choice_count; c++) size += strlen(rows[i].choices[c].id) + strlen(rows[i].choices[c].label) + 8
        + (rows[i].choices[c].unavailable ? strlen(rows[i].choices[c].unavailable) : 0);
    lines[i] = calloc(1, size + strlen(rows[i].value));
    sprintf(lines[i], "%s=%s:", rows[i].id, rows[i].value);
    for (size_t c = 0; c < rows[i].choice_count; c++)
      sprintf(lines[i] + strlen(lines[i]), "%s|%s|%s;", rows[i].choices[c].id, rows[i].choices[c].label,
          rows[i].choices[c].unavailable ? rows[i].choices[c].unavailable : "null");
  }
  bool same = true;
  if (*saved == NULL) { *saved = lines; *saved_count = count; return true; }
  if (count != *saved_count) same = false;
  for (size_t i = 0; same && i < count; i++) if (strcmp(lines[i], (*saved)[i]) != 0) same = false;
  for (size_t i = 0; i < count; i++) free(lines[i]);
  free(lines);
  for (size_t i = 0; i < *saved_count; i++) free((*saved)[i]);
  free(*saved);
  return same;
}
/* TS "native campaign presets replace gameplay choices without changing the custom draft". */
static void test_native_presets(void) {
  if (!corpus_has("q2/baseq2/pak0.pak")) { printf("skipped: native presets\n"); return; }
  const char *argv[] = {"--content-root", corpus_root};
  qa_app_options options = parse_options(argv, 2);
  options.has_bot_skill = true; options.bot_skill = 5;
  options.server_profile_path = strdup("/custom-match.json");
  options.quakec_program = strdup("progs.dat");
  qa_app_startup_selection_model *model = qa_app_startup_selection_model_new(discover(false), &options);
  qa_app_startup_selection_prepare_maps(model);
  qa_app_startup_selection_select(model, "weapons", "q3-baseq3");
  qa_app_startup_selection_select(model, "grapple", "offhand");
  qa_app_startup_selection_select(model, "grappleStyle", "q2-ctf");
  qa_app_startup_selection_select(model, "grenades", "enabled");
  qa_app_startup_selection_select(model, "mode", "deathmatch");
  qa_app_startup_selection_select(model, "enemies", "custom");
  qa_app_startup_selection_select(model, "environment", "disabled");
  qa_app_startup_selection_select(model, "doppler", "disabled");
  qa_app_startup_selection_set_display(model, 1280, 720, 1.3);
  char **snapshot = NULL; size_t snapshot_count = 0;
  rows_equal(model, &snapshot, &snapshot_count);
  qa_app_options draft;
  qa_app_startup_selection_options(model, &draft);
  qa_app_startup_launch launch;
  qa_app_startup_selection_resolve_preset(model, "q1-rerelease-id1", true, 3, NULL, &launch);
  const char *native = product_content(model, "q1-rerelease-id1");
  CHECK_STR(launch.options.product, "q1-rerelease-id1"); CHECK_STR(launch.options.map, "maps/start.bsp");
  CHECK(launch.options.movement == QA_GAME_FAMILY_Q1 && launch.options.character == QA_GAME_FAMILY_Q1);
  CHECK_STR(launch.options.character_model, "player");
  CHECK(launch.options.skill == 3 && launch.options.seats == 1); CHECK_STR(launch.options.mode, "singleplayer");
  CHECK(launch.options.width == 1280 && launch.options.height == 720 && launch.options.gamma == 1.3);
  CHECK(!launch.options.has_bot_skill && launch.options.server_profile_path == NULL && launch.options.quakec_program == NULL);
  CHECK_STR(launch.recipe.movement.content, native); CHECK_STR(launch.recipe.character.definition.content, native);
  CHECK_STR(launch.recipe.character.appearance.content, native);
  bool netquake = false;
  for (size_t i = 0; i < launch.recipe.timing_count; i++)
    if (strcmp(launch.recipe.timing[i].provider, "q1:movement") == 0) netquake = launch.recipe.timing[i].clock.kind == QA_CLOCK_Q1_NETQUAKE;
  CHECK(netquake);
  CHECK(launch.recipe.campaign.is_campaign);
  CHECK_STR(launch.recipe.campaign.mission.content, native); CHECK_STR(launch.recipe.campaign.gamecode.content, native);
  CHECK(launch.recipe.weapons.count == 1); CHECK_STR(launch.recipe.weapons.values[0].provider, "q1:official");
  CHECK_STR(launch.recipe.weapons.values[0].content, native);
  CHECK(launch.recipe.enemies.kind == QA_ENEMY_SELECTION_MAP_DEFINED);
  CHECK(!launch.recipe.equipment.grapple.is_enabled && !launch.recipe.equipment.hand_grenades.is_enabled);
  CHECK_STR(launch.recipe.presentation.assets, native);
  CHECK(launch.recipe.presentation.environment.kind == QA_ENVIRONMENT_AUDIO_CONTENT && launch.recipe.presentation.doppler.is_source);
  qa_app_startup_launch_free(&launch);
  qa_app_options after;
  qa_app_startup_selection_options(model, &after);
  CHECK_STR(after.map, draft.map); CHECK_STR(after.mode, draft.mode); CHECK(after.width == draft.width);
  CHECK_STR(after.server_profile_path, draft.server_profile_path);
  qa_app_options_destroy(&after); qa_app_options_destroy(&draft);
  CHECK(rows_equal(model, &snapshot, &snapshot_count));
  call missing = {model, NULL, "q2-classic-lmctf", NULL, {0}, 6};
  CHECK(throws(missing, "official campaign preset unavailable"));
  call difficulty = {model, NULL, "q1-rerelease-id1", "", {NULL, 5, false, {0}}, 6};
  CHECK(throws(difficulty, "Invalid preset difficulty"));
  qa_app_startup_selection_model_destroy(model);
  qa_app_options_destroy(&options);
}
/* TS "native Q3 preset uses the authored training arena and bot difficulty". */
static void test_q3_preset(void) {
  if (!corpus_has("q3a/baseq3/pak0.pk3")) { printf("skipped: q3 preset\n"); return; }
  const char *argv[] = {"--content-root", corpus_root};
  qa_app_options options = parse_options(argv, 2);
  qa_app_startup_selection_model *model = qa_app_startup_selection_model_new(discover(false), &options);
  qa_app_startup_selection_prepare_maps(model);
  qa_app_startup_launch launch;
  qa_app_startup_selection_resolve_preset(model, "q3-baseq3", true, 5, NULL, &launch);
  CHECK_STR(launch.options.map, "maps/q3dm0.bsp"); CHECK_STR(launch.options.mode, "singleplayer");
  CHECK(launch.options.has_bot_skill && launch.options.bot_skill == 5);
  CHECK(launch.options.character == QA_GAME_FAMILY_Q3 && launch.options.movement == QA_GAME_FAMILY_Q3);
  CHECK_STR(launch.options.character_model, "sarge");
  CHECK(launch.recipe.campaign.is_campaign);
  qa_app_startup_launch_free(&launch);
  size_t preset_count = 0;
  const qa_app_startup_native_preset *presets = qa_app_startup_selection_presets(model, &preset_count);
  const qa_app_startup_native_preset *q3 = NULL, *pack = NULL;
  for (size_t i = 0; i < preset_count; i++) {
    if (strcmp(presets[i].choice.id, "q3-baseq3") == 0) q3 = &presets[i];
    if (strcmp(presets[i].choice.id, "q3-missionpack") == 0) pack = &presets[i];
  }
  CHECK(q3 != NULL && q3->difficulty_count == 5);
  const qa_catalog_discovered_product *missionpack = qa_content_catalog_product(qa_app_startup_selection_catalog(model), "q3-missionpack");
  if (missionpack->availability.kind == QA_CATALOG_INSTALLED) {
    CHECK(pack != NULL && pack->choice.unavailable == NULL);
    qa_app_startup_selection_resolve_preset(model, "q3-missionpack", true, 4, NULL, &launch);
    const qa_app_team_arena_skirmish *skirmish = launch.options.team_arena_skirmish;
    CHECK_STR(launch.options.map, "maps/mpteam1.bsp"); CHECK_STR(launch.options.character_model, "james");
    CHECK(launch.options.has_bot_skill && launch.options.bot_skill == 4);
    CHECK(skirmish != NULL && skirmish->game_type == 4 && skirmish->max_clients == 6);
    if (skirmish != NULL) { CHECK_STR(skirmish->player_team, "Red"); CHECK_STR(skirmish->player_head_model, "*james"); }
    CHECK_STR(launch.recipe.character.appearance.provider, "q3:model/james");
    qa_app_startup_launch_free(&launch);
    size_t team_count = 0;
    const qa_ui_choice *teams = qa_app_startup_selection_team_choices(model, &team_count);
    bool stroggs = false;
    for (size_t i = 0; i < team_count; i++) if (strcmp(teams[i].id, "stroggs") == 0) stroggs = true;
    CHECK(stroggs);
    qa_app_startup_selection_team_write(model, "player", "stroggs");
    qa_app_startup_selection_team_write(model, "opponent", "pagans");
    qa_app_startup_selection_select_server_profile(model, "/private/selected-server-profile.cfg");
    qa_app_startup_selection_resolve_preset(model, "q3-missionpack", true, 3, NULL, &launch);
    CHECK_STR(launch.options.server_profile_path, "/private/selected-server-profile.cfg");
    skirmish = launch.options.team_arena_skirmish;
    const char *red = NULL;
    for (size_t i = 0; skirmish != NULL && i < skirmish->cvar_count; i++) if (strcmp(skirmish->cvars[i].name, "g_redTeam") == 0) red = skirmish->cvars[i].value;
    CHECK_STR(red, "stroggs");
    CHECK(skirmish != NULL && skirmish->bot_count > 0 && strcmp(skirmish->bots[0].name, "Khan") == 0);
    qa_app_startup_launch_free(&launch);
    call write = {model, "player", "missing team", NULL, {0}, 4};
    CHECK(throws(write, "Unknown authored"));
    qa_app_startup_selection_select_server_profile(model, NULL);
    qa_app_options view;
    qa_app_startup_selection_options(model, &view);
    CHECK(view.server_profile_path == NULL);
    qa_app_options_destroy(&view);
  }
  qa_app_startup_selection_model_destroy(model);
  qa_app_options_destroy(&options);
}
/* TS "mixed match eligibility uses source rules on Q3 geometry before launch". */
static void test_mixed_eligibility(void) {
  if (!corpus_has("q3a/baseq3/pak0.pk3")) { printf("skipped: mixed eligibility\n"); return; }
  const char *argv[] = {"--content-root", corpus_root, "--game", "q2-rerelease-baseq2", "--mode", "deathmatch", "--rules", "deathball"};
  qa_app_options options = parse_options(argv, 8);
  qa_app_startup_selection_model *model = qa_app_startup_selection_model_new(discover(false), &options);
  qa_app_startup_selection_prepare_maps(model);
  qa_app_startup_selection_select(model, "mapProduct", "q3-baseq3");
  call resolve_call = {model, NULL, NULL, NULL, {0}, 5};
  CHECK(throws(resolve_call, "DeathBall map is missing:"));
  qa_app_startup_selection_select(model, "rules", "standard");
  (void)qa_app_startup_selection_prepare_map_choices(model, 0, 1);
  qa_app_options view;
  qa_app_startup_selection_options(model, &view);
  const qa_app_startup_selection_choice *map = choice_of(row_of(model, "map"), view.map);
  CHECK(map != NULL && map->unavailable == NULL);
  qa_app_options_destroy(&view);
  qa_app_startup_selection_model_destroy(model);
  qa_app_options_destroy(&options);
}

/* ---- FrontendPreferences (tests/settings/frontend-mouse.test.ts and the startup-gfx
 * oracle build/agents/startup-gfx/tmp/oracle/prefs.ts): bindings, the onChange timer,
 * saveGlobal/saveAudioBaseline bytes through real settings directories, and reload. ---- */
static int prefs_sentinel_dialect, prefs_sentinel_change, prefs_sentinel_output, prefs_sentinel_music;
static int prefs_bad_ctx, prefs_changes;
static const char *prefs_selected = "Speakers";
static qa_command_dialect prefs_dialect(void *ctx) {
  if (ctx != &prefs_sentinel_dialect) prefs_bad_ctx++;
  return QA_COMMAND_DIALECT_Q1_NETQUAKE;
}
static qa_command_dialect prefs_q3(void *ctx) {
  if (ctx != &prefs_sentinel_dialect) prefs_bad_ctx++;
  return QA_COMMAND_DIALECT_Q3;
}
static void prefs_changed(void *ctx) {
  if (ctx != &prefs_sentinel_change) prefs_bad_ctx++;
  prefs_changes++;
}
static qa_audio_output_format prefs_format_read(void *ctx) {
  if (ctx != &prefs_sentinel_output) prefs_bad_ctx++;
  qa_audio_output_format format = {48000, 2, 16};
  return format;
}
static const char *prefs_format_select(void *ctx, qa_audio_output_format format) {
  (void)format;
  if (ctx != &prefs_sentinel_output) prefs_bad_ctx++;
  return NULL;
}
static const char *prefs_output_selected(void *ctx) {
  if (ctx != &prefs_sentinel_output) prefs_bad_ctx++;
  return prefs_selected;
}
static const char *const *prefs_devices(void *ctx, size_t *out_count) {
  static const char *const names[] = {"Speakers", "Headphones"};
  if (ctx != &prefs_sentinel_output) prefs_bad_ctx++;
  *out_count = 2;
  return names;
}
static const char *prefs_select(void *ctx, const char *name) {
  if (ctx != &prefs_sentinel_output) prefs_bad_ctx++;
  prefs_selected = name;
  return NULL;
}
static void prefs_report(void *ctx, const char *message) {
  (void)message;
  if (ctx != &prefs_sentinel_output) prefs_bad_ctx++;
}
static qa_app_audio_music_preferences prefs_music(void *ctx) {
  if (ctx != &prefs_sentinel_music) prefs_bad_ctx++;
  qa_app_audio_music_preferences music = {true, NULL};
  music.menu_track = malloc(5);
  memcpy(music.menu_track, "auto", 5);
  return music;
}
static char *read_file_text(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  char *text = calloc(1, 8192);
  size_t length = fread(text, 1, 8191, file);
  text[length] = '\0';
  fclose(file);
  return text;
}
static const qa_ui_setting_binding *binding_of(const qa_ui_setting_bindings *bindings, const char *id) {
  for (size_t i = 0; i < bindings->count; i++)
    if (strcmp(bindings->items[i].id, id) == 0) return &bindings->items[i];
  return NULL;
}
static void test_frontend_preferences(void) {
  const char *tmp = getenv("TMPDIR");
  char root[512];
  snprintf(root, sizeof root, "%s/prefs-c-XXXXXX", tmp != NULL ? tmp : "/tmp");
  CHECK(mkdtemp(root) != NULL);
  char product_root[600], shared_root[600], bad_root[600], plain_root[600], path[700];
  snprintf(product_root, sizeof product_root, "%s/product", root);
  snprintf(shared_root, sizeof shared_root, "%s/shared", root);
  snprintf(bad_root, sizeof bad_root, "%s/bad", root);
  snprintf(plain_root, sizeof plain_root, "%s/plain", root);
  qa_config_store product, shared;
  CHECK(qa_config_store_init(&product, product_root));
  CHECK(qa_config_store_init(&shared, shared_root));
  qa_app_frontend_preferences *preferences = qa_app_frontend_preferences_create(prefs_dialect, &prefs_sentinel_dialect, &shared);
  qa_app_frontend_preferences_set_on_change(preferences, prefs_changed, &prefs_sentinel_change);
  qa_ui_audio_output_settings output;
  memset(&output, 0, sizeof output);
  output.ctx = &prefs_sentinel_output;
  output.has_format = true;
  output.format_read = prefs_format_read;
  output.format_select = prefs_format_select;
  output.selected = prefs_output_selected;
  output.devices = prefs_devices;
  output.select = prefs_select;
  output.report = prefs_report;
  qa_ui_setting_bindings bindings = qa_app_frontend_preferences_bindings(preferences, &output, prefs_music, &prefs_sentinel_music);
  /* TS: 18 bindings in this order, with these kinds, labels and reads. */
  static const char *const expected[][4] = {
      {"ui:input:controller-vibration", "toggle", "Controller vibration", "true"},
      {"ui:input:controller-vibration-strength", "slider", "Vibration strength", "1"},
      {"ui:audio:device", "choice", "Output device", "device:Speakers"},
      {"ui:audio:rate", "choice", "Output sample rate", "48000"},
      {"ui:audio:bits", "choice", "Output sample bits", "16"},
      {"ui:audio:channels", "choice", "Output channels", "2"},
      {"ui:audio:effects", "slider", "Effects volume", "0.7"},
      {"ui:audio:music", "slider", "Music volume", "0.25"},
      {"ui:input:sensitivity", "slider", "Mouse sensitivity", "3"},
      {"ui:input:mouse-yaw", "slider", "Horizontal sensitivity", "100"},
      {"ui:input:mouse-pitch", "slider", "Vertical sensitivity", "100"},
      {"ui:input:invert-mouse", "toggle", "Invert mouse", "false"},
      {"ui:input:always-run", "toggle", "Always run", "false"},
      {"ui:input:acceleration", "slider", "Mouse acceleration", "0"},
      {"ui:input:filter", "toggle", "Mouse smoothing", "false"},
      {"ui:input:lookspring", "toggle", "Look spring", "false"},
      {"ui:input:lookstrafe", "toggle", "Look strafe", "false"},
      {"ui:input:freelook", "toggle", "Free look", "true"},
  };
  CHECK(bindings.count == 18);
  for (size_t i = 0; i < bindings.count && i < 18; i++) {
    const qa_ui_setting_binding *b = &bindings.items[i];
    CHECK_STR(b->id, expected[i][0]);
    CHECK_STR(b->kind, expected[i][1]);
    CHECK_STR(b->label, expected[i][2]);
    char value[64];
    if (strcmp(b->kind, "toggle") == 0) snprintf(value, sizeof value, "%s", b->read_bool(b->ctx) ? "true" : "false");
    else if (strcmp(b->kind, "slider") == 0) snprintf(value, sizeof value, "%s", QA_JS_NUMBER(b->read_num(b->ctx)));
    else snprintf(value, sizeof value, "%s", b->read_str(b->ctx));
    CHECK_STR(value, expected[i][3]);
  }
  qa_app_frontend_preferences_load_baseline(preferences, &product);
  CHECK(qa_app_frontend_preferences_audio_baseline(preferences) == NULL);
  qa_app_frontend_preference_overrides effective = qa_app_frontend_preferences_effective_values(preferences);
  CHECK(effective.sensitivity == 3 && effective.pitch == 0.02199999988079071 && effective.yaw == 0.02199999988079071);
  CHECK(!effective.invert_mouse && effective.acceleration == 0 && !effective.filter && !effective.look_spring && !effective.look_strafe);
  CHECK(effective.free_look && effective.controller_vibration && effective.controller_vibration_strength == 1 && !effective.has_always_run);
  CHECK(!effective.has_effects_volume && !effective.has_music_volume);
  const qa_ui_setting_binding *b = binding_of(&bindings, "ui:input:sensitivity");
  b->write_num(b->ctx, 5.5);
  b = binding_of(&bindings, "ui:input:invert-mouse");
  b->write_bool(b->ctx, true);
  b = binding_of(&bindings, "ui:input:filter");
  b->write_bool(b->ctx, true);
  b = binding_of(&bindings, "ui:audio:effects");
  b->write_num(b->ctx, 0.4);
  b = binding_of(&bindings, "ui:input:controller-vibration-strength");
  b->write_num(b->ctx, 0.5);
  const qa_app_frontend_preference_overrides *values = qa_app_frontend_preferences_values(preferences);
  CHECK(values->has_sensitivity && values->sensitivity == 5.5 && values->has_invert_mouse && values->invert_mouse);
  CHECK(values->has_filter && values->filter && values->has_effects_volume && values->effects_volume == 0.4);
  CHECK(values->has_controller_vibration_strength && values->controller_vibration_strength == 0.5);
  CHECK(!values->has_pitch && !values->has_yaw && !values->has_music_volume && !values->has_controller_vibration);
  qa_app_frontend_preferences_pump(preferences);
  CHECK(prefs_changes == 0);
  usleep(150000);
  qa_app_frontend_preferences_pump(preferences);
  CHECK(prefs_changes == 1);
  qa_app_frontend_preferences_pump(preferences);
  CHECK(prefs_changes == 1);
  qa_app_frontend_preferences_save_global(preferences);
  snprintf(path, sizeof path, "%s/input/global-tuning.json", shared_root);
  char *text = read_file_text(path);
  CHECK_STR(text, "{\n  \"version\": 1,\n  \"mouse\": {\n    \"sensitivity\": 5.5,\n    \"acceleration\": 0,\n    \"filter\": true,\n"
                  "    \"yaw\": 0.02199999988079071,\n    \"pitch\": 0.02199999988079071,\n    \"side\": 0.800000011920929,\n"
                  "    \"forward\": 1,\n    \"lookSpring\": false,\n    \"lookStrafe\": false,\n    \"freeLook\": true,\n"
                  "    \"invertPitch\": true\n  },\n  \"rumble\": true,\n  \"rumbleStrength\": 0.5\n}\n");
  free(text);
  qa_app_frontend_preferences_save_audio_baseline(preferences, &product);
  snprintf(path, sizeof path, "%s/audio.json", product_root);
  text = read_file_text(path);
  CHECK_STR(text, "{\"version\":1,\"musicShuffle\":true,\"menuTrack\":\"auto\",\"outputFormat\":{\"sampleRate\":48000,\"channels\":2,"
                  "\"sampleBits\":16},\"deviceName\":\"Speakers\",\"effectsVolume\":0.4,\"musicVolume\":0.25}\n");
  free(text);
  const qa_app_audio_preferences *baseline = qa_app_frontend_preferences_audio_baseline(preferences);
  CHECK(baseline != NULL && baseline->device_name != NULL && strcmp(baseline->device_name, "Speakers") == 0);
  CHECK(baseline != NULL && baseline->effects_volume == 0.4 && baseline->music_volume == 0.25 && baseline->has_music_shuffle &&
        baseline->music_shuffle && baseline->has_menu_track && strcmp(baseline->menu_track, "auto") == 0 &&
        baseline->output_format.sample_rate == 48000 && baseline->output_format.channels == 2 && baseline->output_format.sample_bits == 16);
  qa_ui_setting_bindings_free(&bindings);
  qa_app_frontend_preferences_destroy(preferences);
  /* Reload round-trips both documents. */
  qa_app_frontend_preferences *reloaded = qa_app_frontend_preferences_create(prefs_dialect, &prefs_sentinel_dialect, &shared);
  qa_app_frontend_preferences_load_baseline(reloaded, &product);
  effective = qa_app_frontend_preferences_effective_values(reloaded);
  CHECK(effective.sensitivity == 5.5 && effective.pitch == 0.02199999988079071 && effective.yaw == 0.02199999988079071);
  CHECK(effective.invert_mouse && effective.acceleration == 0 && effective.filter && !effective.look_spring && !effective.look_strafe);
  CHECK(effective.free_look && effective.controller_vibration && effective.controller_vibration_strength == 0.5 && !effective.has_always_run);
  double effects = 0, music = 0;
  qa_app_frontend_preferences_audio_values(reloaded, &effects, &music);
  CHECK(effects == 0.4 && music == 0.25);
  baseline = qa_app_frontend_preferences_audio_baseline(reloaded);
  CHECK(baseline != NULL && baseline->has_music_shuffle && baseline->music_shuffle && baseline->has_menu_track &&
        baseline->device_name != NULL && strcmp(baseline->device_name, "Speakers") == 0 && baseline->effects_volume == 0.4);
  qa_app_frontend_preferences_destroy(reloaded);
  /* A foreign audio.json falls back to {}. */
  snprintf(path, sizeof path, "%s", bad_root);
  CHECK(mkdir(path, 0700) == 0);
  snprintf(path, sizeof path, "%s/audio.json", bad_root);
  FILE *file = fopen(path, "wb");
  fputs("{\"version\":1,\"deviceName\":null,\"effectsVolume\":0.5,\"musicVolume\":0.5,\"outputFormat\":{\"sampleRate\":7}}", file);
  fclose(file);
  qa_config_store bad_store, plain_store;
  CHECK(qa_config_store_init(&bad_store, bad_root));
  CHECK(qa_config_store_init(&plain_store, plain_root));
  qa_app_frontend_preferences *bad = qa_app_frontend_preferences_create(prefs_q3, &prefs_sentinel_dialect, NULL);
  qa_app_frontend_preferences_load_baseline(bad, &bad_store);
  CHECK(qa_app_frontend_preferences_audio_baseline(bad) == NULL);
  qa_app_frontend_preferences_audio_values(bad, &effects, &music);
  CHECK(effects == 0.7 && music == 0.25);
  /* No shared settings: saveGlobal writes nothing; an unchanged audio baseline writes nothing. */
  qa_app_frontend_preferences_save_global(bad);
  qa_app_frontend_preferences_save_audio_baseline(bad, &plain_store);
  snprintf(path, sizeof path, "%s/audio.json", plain_root);
  text = read_file_text(path);
  CHECK(text == NULL);
  free(text);
  qa_app_frontend_preferences_destroy(bad);
  qa_config_store_destroy(&bad_store);
  qa_config_store_destroy(&plain_store);
  qa_config_store_destroy(&product);
  qa_config_store_destroy(&shared);
  CHECK(prefs_bad_ctx == 0);
}

/* ---- StartupInputProfile (tests/first-playable/startup-input-profile.test.ts and the
 * startup-gfx oracle build/agents/startup-gfx/tmp/oracle/profile.ts). ---- */
static bool profile_ui(void *ctx, const qa_seat_input_event *event, qa_seat_input_focus focus) {
  (void)ctx; (void)event; (void)focus;
  return true;
}
typedef struct profile_seat {
  qa_identity_owner owner;
  qa_command_buffer *commands;
  qa_seat_input *input;
} profile_seat;
static void profile_seat_open(profile_seat *seat) {
  memset(seat, 0, sizeof *seat);
  CHECK(qa_identity_owner_create(&seat->owner, "startup-profile-test"));
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = seat->owner.session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = qa_identity_owner_seat(&seat->owner, 0);
  context.origin.client = qa_identity_owner_client(&seat->owner, 0, 0);
  qa_command_buffer_options buffer;
  memset(&buffer, 0, sizeof buffer);
  buffer.dialect = QA_COMMAND_DIALECT_Q3;
  buffer.context = context;
  seat->commands = qa_command_buffer_create(&buffer);
  qa_seat_input_options options;
  memset(&options, 0, sizeof options);
  options.seat = context.origin.seat;
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context = context;
  options.commands = seat->commands;
  options.ui_event = profile_ui;
  seat->input = qa_seat_input_create(&options);
  CHECK(seat->input != NULL);
}
static void profile_seat_close(profile_seat *seat) {
  qa_seat_input_destroy(seat->input);
  qa_command_buffer_destroy(seat->commands);
  qa_identity_owner_destroy(&seat->owner);
}
static void test_startup_input_profile(void) {
  const char *tmp = getenv("TMPDIR");
  char root[512], path[700];
  snprintf(root, sizeof root, "%s/profile-c-XXXXXX", tmp != NULL ? tmp : "/tmp");
  CHECK(mkdtemp(root) != NULL);
  snprintf(path, sizeof path, "%s/input/seat-1.json", root);
  qa_config_store settings;
  CHECK(qa_config_store_init(&settings, root));
  profile_seat seat;
  profile_seat_open(&seat);
  qa_input_binding authored;
  memset(&authored, 0, sizeof authored);
  authored.input.kind = QA_PHYSICAL_INPUT_KEY;
  authored.input.code = 119;
  authored.target.text = "echo authored";
  qa_seat_input_bind(seat.input, &authored);
  qa_app_startup_input_profile *retained = qa_app_startup_input_profile_retained(&settings, seat.input);
  CHECK(qa_app_startup_input_profile_input(retained) == seat.input);
  qa_app_startup_input_profile_save(retained, NULL, NULL, 0, false);
  char *text = read_file_text(path);
  CHECK(text == NULL); /* TS: absent */
  free(text);
  qa_app_frontend_preference_overrides values;
  memset(&values, 0, sizeof values);
  values.has_sensitivity = true; values.sensitivity = 6;
  values.has_always_run = true; values.always_run = true;
  values.has_controller_vibration = true; values.controller_vibration = false;
  const char *const history[] = {"map q3dm1", "echo hi"};
  qa_app_startup_input_profile_save(retained, &values, history, 2, true);
  /* TS writes this fresh document in its object-literal key order; the C writer uses the
   * parser order, so the reloaded values are compared. */
  qa_seat_settings saved;
  memset(&saved, 0, sizeof saved);
  CHECK(qa_config_store_load_seat(&settings, "input/seat-1.json", &saved));
  CHECK(saved.binding_count == 1 && saved.bindings[0].input.code == 119 && saved.bindings[0].target.text != NULL &&
        strcmp(saved.bindings[0].target.text, "echo authored") == 0);
  CHECK(saved.mouse.sensitivity == 6 && saved.has_always_run && saved.always_run && !saved.rumble && saved.rumble_strength == 1);
  CHECK(saved.history_count == 2 && strcmp(saved.history[0], "map q3dm1") == 0 && strcmp(saved.history[1], "echo hi") == 0);
  CHECK(saved.controller.kind == QA_CONTROLLER_AUTOMATIC);
  qa_seat_settings_free(&saved);
  qa_input_binding pad;
  memset(&pad, 0, sizeof pad);
  pad.input.kind = QA_PHYSICAL_INPUT_CONTROLLER_BUTTON;
  pad.input.device = 3;
  pad.input.button = 2;
  pad.target.text = "+attack";
  qa_seat_input_bind(seat.input, &pad);
  qa_app_startup_input_profile_save(retained, NULL, NULL, 0, false);
  static const char expected[] =
      "{\n"
      "  \"version\": 1,\n"
      "  \"bindings\": [\n"
      "    {\n"
      "      \"input\": {\n"
      "        \"kind\": \"key\",\n"
      "        \"code\": 119\n"
      "      },\n"
      "      \"target\": {\n"
      "        \"kind\": \"command\",\n"
      "        \"text\": \"echo authored\"\n"
      "      }\n"
      "    },\n"
      "    {\n"
      "      \"input\": {\n"
      "        \"kind\": \"controller-button\",\n"
      "        \"device\": 0,\n"
      "        \"button\": 2\n"
      "      },\n"
      "      \"target\": {\n"
      "        \"kind\": \"command\",\n"
      "        \"text\": \"+attack\"\n"
      "      }\n"
      "    }\n"
      "  ],\n"
      "  \"alwaysRun\": true,\n"
      "  \"gamepad\": {\n"
      "    \"move\": {\n"
      "      \"kind\": \"radial\",\n"
      "      \"deadzone\": 0.175,\n"
      "      \"exponent\": 2,\n"
      "      \"outerThreshold\": 0.02\n"
      "    },\n"
      "    \"look\": {\n"
      "      \"kind\": \"radial\",\n"
      "      \"deadzone\": 0.175,\n"
      "      \"exponent\": 2,\n"
      "      \"outerThreshold\": 0.02\n"
      "    },\n"
      "    \"swapSticks\": false,\n"
      "    \"yawDegreesPerSecond\": 240,\n"
      "    \"pitchDegreesPerSecond\": 130,\n"
      "    \"invertPitch\": false,\n"
      "    \"forwardSensitivity\": 1,\n"
      "    \"sideSensitivity\": 1,\n"
      "    \"triggerThreshold\": 0.2,\n"
      "    \"gyro\": {\n"
      "      \"enabled\": false,\n"
      "      \"yawAxis\": \"y\",\n"
      "      \"yawSensitivity\": 1,\n"
      "      \"pitchSensitivity\": 1\n"
      "    }\n"
      "  },\n"
      "  \"mouse\": {\n"
      "    \"sensitivity\": 6,\n"
      "    \"acceleration\": 0,\n"
      "    \"filter\": false,\n"
      "    \"yaw\": 0.022,\n"
      "    \"pitch\": 0.022,\n"
      "    \"side\": 0.8,\n"
      "    \"forward\": 1,\n"
      "    \"lookSpring\": false,\n"
      "    \"lookStrafe\": false,\n"
      "    \"freeLook\": true,\n"
      "    \"invertPitch\": false\n"
      "  },\n"
      "  \"history\": [\n"
      "    \"map q3dm1\",\n"
      "    \"echo hi\"\n"
      "  ],\n"
      "  \"rumble\": false,\n"
      "  \"rumbleStrength\": 1,\n"
      "  \"controller\": {\n"
      "    \"kind\": \"automatic\"\n"
      "  }\n"
      "}\n";
  text = read_file_text(path);
  CHECK_STR(text, expected);
  free(text);
  profile_seat reopen;
  profile_seat_open(&reopen);
  qa_app_startup_input_profile *reopened = qa_app_startup_input_profile_open(&settings, reopen.input, QA_COMMAND_DIALECT_Q3, NULL, 0);
  size_t count = 0;
  const qa_input_binding *bindings = qa_seat_input_bindings(reopen.input, &count);
  CHECK(count == 2 && bindings[0].input.kind == QA_PHYSICAL_INPUT_KEY && bindings[0].input.code == 119 &&
        bindings[1].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON && bindings[1].input.device == 0 && bindings[1].input.button == 2);
  qa_app_frontend_preference_overrides none;
  memset(&none, 0, sizeof none);
  qa_app_startup_input_profile_save(reopened, &none, NULL, 0, false);
  text = read_file_text(path);
  CHECK_STR(text, expected);
  free(text);
  qa_app_startup_input_profile_destroy(reopened);
  qa_app_startup_input_profile_destroy(retained);
  profile_seat_close(&reopen);
  profile_seat_close(&seat);
  qa_config_store_destroy(&settings);
}

/* ---- loadMenuFont / loadMenuTypography / StartupAudio over the installed corpus, against
 * the startup-gfx oracle build/agents/startup-gfx/tmp/oracle/font.ts. ---- */
static int audio_sentinel, audio_bad_ctx, audio_printed;
static void audio_print(void *ctx, const char *text) {
  (void)text;
  if (ctx != &audio_sentinel) audio_bad_ctx++;
  audio_printed++;
}
typedef struct corpus_mounts { qa_catalog_mount_list list; qa_mount_id *order; qa_content_mounted *mounted; } corpus_mounts;
static void corpus_mounts_open(corpus_mounts *m, qa_content_catalog *catalog, const char *id) {
  memset(m, 0, sizeof *m);
  CHECK(qa_content_catalog_mounts_for(catalog, id, &m->list));
  m->order = calloc(m->list.count + 1, sizeof *m->order);
  for (size_t i = 0; i < m->list.count; i++)
    m->order[i] = m->list.mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE ? m->list.mounts[i].as.archive.identity.id : m->list.mounts[i].as.loose.identity.id;
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:startup:font";
  plan.mounts = m->list.mounts;
  plan.mount_count = m->list.count;
  plan.default_order = m->order;
  plan.default_order_count = m->list.count;
  m->mounted = qa_open_mount_plan(&plan, NULL);
  CHECK(m->mounted != NULL);
}
static void corpus_mounts_close(corpus_mounts *m) {
  qa_content_mounted_close(m->mounted);
  qa_content_mounted_destroy(m->mounted);
  free(m->order);
  qa_content_catalog_mount_list_free(&m->list);
}
static void test_menu_font_and_audio(void) {
  static const struct {
    const char *id; bool atlas; double size, glyphs, line; size_t tracks; const char *first_track, *last_track;
  } rows[] = {
      {"q1:classic:id1:installed", false, 128, 256, 8, 0, NULL, NULL},
      {"q2:rerelease:baseq2:installed", true, 128, 256, 8, 37, "music/D_DDTBLU.ogg", "music/track79.ogg"},
      {"q3:classic:baseq3:installed", false, 256, 256, 16, 16, "music/fla22k_01_intro.wav", "music/win.wav"},
  };
  qa_content_catalog *catalog = discover(false);
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "font-oracle"));
  static const char token = 0;
  for (size_t r = 0; r < sizeof rows / sizeof rows[0]; r++) {
    const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, rows[r].id);
    CHECK(product != NULL);
    if (product == NULL) continue;
    corpus_mounts mounts;
    corpus_mounts_open(&mounts, catalog, rows[r].id);
    qa_scene_image_registry images;
    qa_renderer_resource_owner owner = {&token, identity.session, 0};
    qa_scene_image_registry_init(&images, owner, NULL);
    qa_app_menu_font_options options;
    memset(&options, 0, sizeof options);
    options.catalog = catalog;
    options.mounts = mounts.mounted;
    options.family = product->expectation.family;
    options.rerelease = product->expectation.edition != NULL && strcmp(product->expectation.edition, "rerelease") == 0;
    options.images = &images;
    qa_app_menu_font *font = qa_app_load_menu_font(&options);
    qa_text_font_selection selection = qa_app_menu_font_selection(font);
    CHECK(selection.is_atlas == rows[r].atlas);
    CHECK_STR(selection.classic->name, "conchars");
    CHECK(selection.classic->picture.image.image.width == rows[r].size && selection.classic->picture.image.image.height == rows[r].size);
    CHECK(selection.classic->glyph_count == rows[r].glyphs && selection.classic->line_height == rows[r].line);
    /* TS: body Montserrat 48 (line 57, 218 glyphs, 2 fallbacks), title NotoSans-Bold 72 (line 94, 302 glyphs, 1). */
    qa_app_menu_typography *typography = qa_app_load_menu_typography(catalog, &images, selection.classic, NULL);
    qa_text_font_selection body = qa_app_menu_typography_body(typography), title = qa_app_menu_typography_title(typography);
    CHECK(body.is_atlas && body.font->line_height == 57 && body.font->glyph_count == 218 && body.fallback_count == 2);
    CHECK(title.is_atlas && title.font->line_height == 94 && title.font->glyph_count == 302 && title.fallback_count == 1);
    CHECK(body.font->name != NULL && strncmp(body.font->name, "truetype:fonts/Montserrat-Regular.ttf:48:", 41) == 0);
    CHECK(title.font->name != NULL && strncmp(title.font->name, "truetype:fonts/NotoSans-Bold.ttf:72:", 36) == 0);
    qa_app_startup_audio_options audio_options;
    memset(&audio_options, 0, sizeof audio_options);
    audio_options.mounts = mounts.mounted;
    audio_options.source.content = product->id;
    audio_options.source.family = product->expectation.family;
    audio_options.source.edition = product->expectation.edition;
    audio_options.source.campaign = product->expectation.campaign;
    audio_options.seat = qa_identity_owner_seat(&identity, 0);
    audio_options.print = audio_print;
    audio_options.print_ctx = &audio_sentinel;
    audio_options.preferences.menu_track = "auto";
    qa_app_startup_audio *audio = qa_app_startup_audio_open(&audio_options);
    size_t track_count = 0;
    const char *const *tracks = qa_app_startup_audio_music_tracks(audio, &track_count);
    CHECK(track_count == rows[r].tracks);
    if (track_count > 0 && track_count == rows[r].tracks) {
      CHECK_STR(tracks[0], rows[r].first_track);
      CHECK_STR(tracks[track_count - 1], rows[r].last_track);
    }
    qa_audio_output_format format = qa_app_startup_audio_output_format(audio);
    CHECK(format.sample_rate == 44100 && format.channels == 2 && format.sample_bits == 16);
    qa_app_audio_music_preferences music = qa_app_startup_audio_music_preferences(audio);
    CHECK(!music.music_shuffle && music.menu_track != NULL && strcmp(music.menu_track, "auto") == 0);
    free(music.menu_track);
    qa_app_startup_audio_close(audio);
    qa_app_startup_audio_destroy(audio);
    qa_app_menu_typography_close(typography);
    qa_app_menu_font_close(font);
    qa_scene_image_registry_close(&images);
    qa_scene_image_registry_destroy(&images);
    corpus_mounts_close(&mounts);
  }
  CHECK(audio_printed == 0 && audio_bad_ctx == 0);
  qa_identity_owner_destroy(&identity);
  qa_content_catalog_destroy(catalog);
  free(catalog);
}

int main(void) {
  test_frontend_preferences();
  test_startup_input_profile();
  test_route_command();
  test_unit_members();
  test_browser_view_threshold();
  corpus_root = getenv("QTS_TEST_CORPUS");
  if (corpus_root != NULL && corpus_root[0] != '\0') {
    test_menu_browser_errors();
    test_independent_selections();
    test_q1_hosting();
    test_native_presets();
    test_q3_preset();
    test_mixed_eligibility();
    test_menu_font_and_audio();
  } else printf("skipped content rows: QTS_TEST_CORPUS unset\n");
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
