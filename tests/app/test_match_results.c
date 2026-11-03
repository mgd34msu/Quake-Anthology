/* Checks for src/app/bootstrap/match_results.c (match-results.ts),
 * qa_app_shared_sim_q2_match_simulation and the player-progress.ts drift
 * (ranked-match events, version 2, all(), lenient open).
 *
 * The retail case reproduces tests/app/oracles/match_results.ts (the TS
 * local-match-rankings Q1 case without the Application) on a real Q1 dm1
 * deathmatch SharedSimulation and compares the printed lines with the bun
 * output recorded below (QTS_TEST_CORPUS, default ~/qfiles; skipped when the
 * corpus has no q1/id1/pak0.pak). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/match_results.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/player_progress.h"
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "../../src/app/bootstrap/simulation/shared_simulation_internal.h"

static int failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                       \
    }                                                                   \
  } while (0)
#define CHECK_STR(a, b) CHECK((a) != NULL && strcmp((a), (b)) == 0)

static const char *tmp_root(void) {
  const char *tmp = getenv("TMPDIR");
  return tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp";
}

/* ---- userinfoIdentity ---- */
static void check_identity(const char *userinfo, const char *expected) {
  char *identity = qa_app_match_userinfo_identity(userinfo);
  if (expected == NULL) CHECK(identity == NULL);
  else CHECK_STR(identity, expected);
  free(identity);
}
static void test_userinfo_identity(void) {
  check_identity(NULL, NULL);
  check_identity("", NULL);
  check_identity("\\name\\Player", "Player");
  check_identity("\\name\\  Player \\cl_guid\\ABC", "cl_guid:ABC");
  check_identity("\\name\\Player\\social_id\\ 42 \\cl_guid\\ABC", "social_id:42");
  check_identity("\\name\\   ", NULL);
  check_identity("\\social_id\\\\name\\x", "x");
  /* Map.set: the last pair with a key wins. */
  check_identity("\\name\\first\\name\\second", "second");
  /* A dangling key without a value is not a pair. */
  check_identity("\\name", NULL);
  /* U+00A0 is JS white space for trim. */
  check_identity("\\name\\" "\xc2\xa0" "Bob" "\xc2\xa0", "Bob");
}

/* ---- player-progress.ts drift ---- */
static void write_text(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  CHECK(file != NULL);
  if (file == NULL) return;
  fputs(text, file);
  fclose(file);
}
static char *read_text(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  char *text = calloc(1, 65536);
  size_t length = fread(text, 1, 65535, file);
  text[length] = '\0';
  fclose(file);
  return text;
}
static void test_player_progress(void) {
  char path[1024];
  snprintf(path, sizeof path, "%s/match-results-progress.json", tmp_root());
  remove(path);
  /* A version 1 file with an invalid event opens empty (TS catch), as torn text does. */
  write_text(path, "{\"version\":1,\"events\":[{\"kind\":\"match-completed\",\"source\":\"q1\",\"participant\":\"a\",\"event\":\"e\"}]}");
  qa_app_player_progress_store *store = qa_app_player_progress_store_open(path);
  size_t count = 99;
  (void)qa_app_player_progress_store_all(store, &count);
  CHECK(count == 0);
  qa_app_player_progress_store_destroy(store);
  write_text(path, "{torn");
  store = qa_app_player_progress_store_open(path);
  (void)qa_app_player_progress_store_all(store, &count);
  CHECK(count == 0);
  qa_app_player_progress_event ranked = { "ranked-match", "q2", "Host", "m1:maps/q2dm1.bsp", NULL, "maps/q2dm1.bsp", 7, true, "q2:official" };
  CHECK(qa_app_player_progress_store_record(store, ranked));
  qa_app_player_progress_event level = { "level-completed", "q1", "local-seat:0", "e1m1", "ignored", "maps/e1m1.bsp", 0, false, NULL };
  CHECK(qa_app_player_progress_store_record(store, level));
  const qa_app_player_progress_event *const *all = qa_app_player_progress_store_all(store, &count);
  CHECK(count == 2);
  /* The views stay valid while later records grow the table. */
  const qa_app_player_progress_event *first = all[0];
  for (int i = 0; i < 20; i++) {
    char event[32];
    snprintf(event, sizeof event, "match-%d", i);
    qa_app_player_progress_event match = { "match-completed", "q3", "local-seat:0", event, NULL, "maps/q3dm1.bsp", i, false, NULL };
    CHECK(qa_app_player_progress_store_record(store, match));
  }
  CHECK_STR(first->rules, "q2:official");
  CHECK(first->won);
  qa_app_player_progress_store_destroy(store);
  /* JSON.stringify({ version: 2, events }) in decoded key order (level-completed drops award). */
  char *text = read_text(path);
  static const char expected_prefix[] = "{\"version\":2,\"events\":[{\"kind\":\"ranked-match\",\"source\":\"q2\",\"participant\":\"Host\","
      "\"event\":\"m1:maps/q2dm1.bsp\",\"map\":\"maps/q2dm1.bsp\",\"score\":7,\"won\":true,\"rules\":\"q2:official\"},"
      "{\"kind\":\"level-completed\",\"source\":\"q1\",\"participant\":\"local-seat:0\",\"event\":\"e1m1\",\"map\":\"maps/e1m1.bsp\"},";
  CHECK(text != NULL && strncmp(text, expected_prefix, sizeof expected_prefix - 1) == 0);
  free(text);
  store = qa_app_player_progress_store_open(path);
  all = qa_app_player_progress_store_all(store, &count);
  CHECK(count == 22);
  CHECK_STR(all[0]->kind, "ranked-match");
  CHECK(all[0]->score == 7 && all[0]->won);
  CHECK(all[1]->award == NULL);
  qa_app_player_progress_store_destroy(store);
  remove(path);
}

/* ---- ApplicationMatchRankings without a result ---- */
static void bind_run(void *ctx) { (void)qa_app_match_rankings_bind(ctx, 256, "x"); }
static void test_rankings_bind(void) {
  qa_app_match_rankings *rankings = qa_app_match_rankings_create();
  const qa_online_account *a = qa_app_match_rankings_bind(rankings, 0, "Host");
  const qa_online_account *b = qa_app_match_rankings_bind(rankings, 3, "Host");
  CHECK(a == b);
  CHECK(a != NULL && strncmp(a->id, "account:", 8) == 0);
  qa_caught_error caught;
  CHECK(!qa_try(bind_run, rankings, &caught));
  CHECK_STR(caught.name, "RangeError");
  CHECK_STR(caught.message, "Ranking slot is out of range");
  qa_app_match_rankings_unbind(rankings, -1);
  qa_app_match_rankings_unbind(rankings, 0.5);
  size_t count = 9;
  qa_ranking_entry *entries = qa_app_match_rankings_settle(rankings, &count);
  CHECK(count == 0);
  qa_ranking_entries_destroy(entries, count);
  CHECK(qa_app_match_rankings_dropped_results(rankings) == 0);
  qa_app_match_rankings_destroy(rankings);
}

/* ---- the retail Q1 case ---- */
typedef struct lines { char **items; size_t count; } lines;
static void emit(lines *out, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void emit(lines *out, const char *format, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  out->items = realloc(out->items, (out->count + 1) * sizeof *out->items);
  out->items[out->count++] = strdup(buffer);
}
static void emit_result(lines *out, const char *label, const qa_app_match_result *result, bool uuid) {
  if (result == NULL) { emit(out, "%s null", label); return; }
  char match[512];
  if (uuid) {
    bool shaped = strlen(result->match) > 36 && result->match[8] == '-' && result->match[14] == '4' && result->match[36] == ':';
    snprintf(match, sizeof match, "%s%s", shaped ? "<uuid>" : "?", result->match + (strlen(result->match) > 36 ? 36 : 0));
  } else snprintf(match, sizeof match, "%s", result->match);
  emit(out, "%s %s %s %s %s %zu", label, result->family, result->rules, match, result->map, result->player_count);
  for (size_t i = 0; i < result->player_count; i++)
    emit(out, "%s-player %s %s %s %s", label, QA_JS_NUMBER(result->players[i].slot), QA_JS_NUMBER(result->players[i].score),
        result->players[i].won ? "true" : "false", result->players[i].identity != NULL ? result->players[i].identity : "null");
}
static int sentinel_identity, sentinel_prompt, sentinel_frame;
static bool player_identity(void *ctx, qa_client_id client, double *out_seat, const char **out_social_id) {
  (void)client;
  if (ctx != &sentinel_identity) abort();
  *out_seat = 0; *out_social_id = "";
  return true;
}
static bool prompt_supported(void *ctx, qa_client_id client) {
  (void)client;
  if (ctx != &sentinel_prompt) abort();
  return false;
}
static void next_frame(void *ctx) { if (ctx != &sentinel_frame) abort(); }
static const char *const expected_lines[] = {
  "playing null",
  "ended-after 2",
  "ended q1 q1:official probe:maps/dm1.bsp maps/dm1.bsp 1",
  "ended-player 0 4 true Player 1",
  "observed q1 q1:official <uuid>:maps/dm1.bsp maps/dm1.bsp 1",
  "observed-player 0 4 true Player 1",
  "observe-again null",
  "settled 1 1 4",
  "standing 1 1 4 score=4",
  "dropped 0",
  "restarted 1 1 4",
  "progress ranked-match q1 Host maps/dm1.bsp 4 true q1:official",
};
static void step_empty(qa_app_shared_simulation *sim) {
  qa_input_batch batch = { 100, NULL, 0 };
  (void)qa_app_shared_sim_step(sim, batch);
}
static bool match_ended(qa_app_shared_simulation *sim) {
  qa_app_match_result result;
  bool ended = qa_app_match_result_of(sim, "probe", NULL, &result);
  if (ended) qa_app_match_result_free(&result);
  return ended;
}
static void test_retail_q1(void) {
  const char *corpus = getenv("QTS_TEST_CORPUS");
  char home[512];
  if (corpus == NULL || corpus[0] == '\0') { snprintf(home, sizeof home, "%s/qfiles", getenv("HOME") ? getenv("HOME") : ""); corpus = home; }
  /* The corpus keeps Quake under Quake/id1 (retail install) or q1/id1 (logical layout). */
  char pak[1024], logical[1024];
  snprintf(pak, sizeof pak, "%s/Quake", corpus);
  snprintf(logical, sizeof logical, "%s/q1", corpus);
  struct stat info;
  if (stat(pak, &info) != 0 && stat(logical, &info) != 0) { printf("skip retail Q1 case: no id1 under %s\n", corpus); return; }
  char user_root[1024], clear[1100];
  snprintf(user_root, sizeof user_root, "%s/match-results-user", tmp_root());
  snprintf(clear, sizeof clear, "rm -rf '%s'", user_root);
  if (system(clear) != 0 || mkdir(user_root, 0777) != 0) { CHECK(false); return; }
  const char *argv[] = { "--content-root", corpus, "--user-content-root", user_root, "--game", "q1-classic-id1", "--map", "dm1",
    "--movement", "q1", "--character", "q1", "--mode", "deathmatch", "--dedicated" };
  qa_content_root_search search = { corpus, NULL };
  qa_app_application_command command = qa_app_parse_application_command(argv, sizeof argv / sizeof argv[0], search, NULL);
  CHECK_STR(command.kind, "run");
  qa_app_content_host host;
  memset(&host, 0, sizeof host);
  qa_app_options launch = command.options;
  memset(&command.options, 0, sizeof command.options);
  qa_app_loaded_content *content = qa_app_load_application_content_native(host, launch, NULL, false, NULL, NULL, NULL);
  CHECK(content != NULL);
  if (content == NULL) { qa_app_application_command_destroy(&command); return; }
  qa_identity_owner identity;
  if (!qa_identity_owner_create(&identity, "match-results")) abort();
  qa_app_application_world world = qa_app_loaded_content_world(content);
  qa_app_simulation_options options;
  memset(&options, 0, sizeof options);
  options.dedicated = true;
  options.identity = identity;
  options.recipe = *qa_app_loaded_content_recipe(content);
  options.world = &world;
  options.mounts = qa_app_loaded_content_mounts(content);
  options.skill = launch.skill; options.mode = launch.mode; options.seed = launch.seed;
  options.max_clients = 1;
  options.has_player_colors = true;
  options.prompt_ctx = &sentinel_prompt; options.prompt_supported = prompt_supported;
  options.identity_ctx = &sentinel_identity; options.player_identity = player_identity;
  static const void *const no_prepared_mods[1] = { NULL };
  options.prepared_mods = no_prepared_mods; options.prepared_mod_count = 0;
  qa_app_shared_simulation *sim = qa_app_shared_simulation_load(options, next_frame, &sentinel_frame);
  qa_client_id client = qa_identity_owner_client(&identity, 0, 0);
  qa_app_sim_player_admission admission = qa_app_shared_sim_admit_player(sim, client);
  lines out = { NULL, 0 };
  for (int i = 0; i < 10; i++) step_empty(sim);
  qa_app_match_result result;
  if (qa_app_match_result_of(sim, "probe", NULL, &result)) { emit_result(&out, "playing", &result, false); qa_app_match_result_free(&result); }
  else emit_result(&out, "playing", NULL, false);
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(sim->source.q1_cvars, "fraglimit", "3", true, &snapshot)) qa_cvar_read_free(&snapshot);
  qa_source_match_player_result player = qa_app_shared_sim_primary_match_player(sim, admission.actor);
  CHECK(player.has_value);
  if (player.has_value) player.value.set_score(player.value.ctx, 4);
  int frames = 0;
  for (; frames < 200 && !match_ended(sim); frames++) step_empty(sim);
  emit(&out, "ended-after %d", frames);
  if (qa_app_match_result_of(sim, "probe", NULL, &result)) { emit_result(&out, "ended", &result, false); qa_app_match_result_free(&result); }
  else emit_result(&out, "ended", NULL, false);
  /* q2MatchCompleted over the access struct: a Q1 source is not a Q2 match. */
  qa_app_q2_match_simulation access = qa_app_shared_sim_q2_match_simulation(sim);
  CHECK(access.ctx == sim);
  CHECK(!access.has_q2_source(access.ctx) && !access.has_q2_native(access.ctx));
  CHECK_STR(access.map_path, "maps/dm1.bsp");
  CHECK(!qa_app_q2_match_completed(&access, NULL));
  char progress_path[1100];
  snprintf(progress_path, sizeof progress_path, "%s/player-progress.json", user_root);
  qa_app_match_rankings *rankings = qa_app_match_rankings_create();
  qa_app_player_progress_store *store = qa_app_player_progress_store_open(progress_path);
  qa_app_match_rankings_attach(rankings, store);
  (void)qa_app_match_rankings_bind(rankings, 0, "Host");
  emit_result(&out, "observed", qa_app_match_rankings_observe(rankings, sim, NULL), true);
  emit(&out, "observe-again %s", qa_app_match_rankings_observe(rankings, sim, NULL) == NULL ? "null" : "result");
  size_t count = 0;
  qa_ranking_entry *entries = qa_app_match_rankings_settle(rankings, &count);
  for (size_t i = 0; i < count; i++)
    emit(&out, "settled %s %s %s", QA_JS_NUMBER(entries[i].matches), QA_JS_NUMBER(entries[i].wins), QA_JS_NUMBER(entries[i].score));
  qa_ranking_entries_destroy(entries, count);
  entries = qa_app_match_rankings_standings(rankings, sim->recipe.match.provider, &count);
  for (size_t i = 0; i < count; i++) {
    char statistics[256] = "";
    for (size_t s = 0; s < entries[i].statistic_count; s++) {
      char row[96];
      snprintf(row, sizeof row, "%s%s=%s", s == 0 ? "" : ",", entries[i].statistics[s].key, QA_JS_NUMBER(entries[i].statistics[s].value));
      strncat(statistics, row, sizeof statistics - strlen(statistics) - 1);
    }
    emit(&out, "standing %s %s %s %s", QA_JS_NUMBER(entries[i].matches), QA_JS_NUMBER(entries[i].wins), QA_JS_NUMBER(entries[i].score), statistics);
  }
  qa_ranking_entries_destroy(entries, count);
  emit(&out, "dropped %s", QA_JS_NUMBER(qa_app_match_rankings_dropped_results(rankings)));
  qa_app_match_rankings *restarted = qa_app_match_rankings_create();
  qa_app_player_progress_store *reopened = qa_app_player_progress_store_open(progress_path);
  qa_app_match_rankings_attach(restarted, reopened);
  entries = qa_app_match_rankings_standings(restarted, sim->recipe.match.provider, &count);
  for (size_t i = 0; i < count; i++)
    emit(&out, "restarted %s %s %s", QA_JS_NUMBER(entries[i].matches), QA_JS_NUMBER(entries[i].wins), QA_JS_NUMBER(entries[i].score));
  qa_ranking_entries_destroy(entries, count);
  const qa_app_player_progress_event *const *events = qa_app_player_progress_store_all(reopened, &count);
  for (size_t i = 0; i < count; i++)
    emit(&out, "progress %s %s %s %s %s %s %s", events[i]->kind, events[i]->source, events[i]->participant,
        events[i]->map != NULL ? events[i]->map : "-", QA_JS_NUMBER(events[i]->score), events[i]->won ? "true" : "false",
        events[i]->rules != NULL ? events[i]->rules : "-");
  size_t expected_count = sizeof expected_lines / sizeof expected_lines[0];
  CHECK(out.count == expected_count);
  for (size_t i = 0; i < out.count; i++) {
    const char *want = i < expected_count ? expected_lines[i] : "<end>";
    if (strcmp(want, out.items[i]) != 0) { fprintf(stderr, "line %zu\n  ts: %s\n  c:  %s\n", i + 1, want, out.items[i]); failures++; }
    free(out.items[i]);
  }
  free(out.items);
  qa_app_match_rankings_destroy(restarted);
  qa_app_player_progress_store_destroy(reopened);
  qa_app_match_rankings_destroy(rankings);
  qa_app_player_progress_store_destroy(store);
  qa_app_shared_sim_close(sim);
  qa_app_shared_simulation_destroy(sim);
  qa_identity_owner_destroy(&identity);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  qa_app_application_command_destroy(&command);
}

int main(void) {
  test_userinfo_identity();
  test_player_progress();
  test_rankings_bind();
  test_retail_q1();
  if (failures != 0) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
  printf("test_match_results: ok\n");
  return 0;
}
