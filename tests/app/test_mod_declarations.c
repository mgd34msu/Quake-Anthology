/* Checks for src/content/mods/declaration_writer.c: the TS value of the three
 * gameplay-mod declaration types, byte for byte against bun JSON.stringify.
 *
 * tests/app/oracles/mod_declarations.tsv is written by
 * tests/app/oracles/mod_declarations.ts (see its header): every declaration the
 * TS tests read (captured by mod_declarations_capture.ts), synthetic
 * declarations that reach every optional member and union branch, and each
 * result read again. For every row the C reader reads the input and the writer's
 * JSON must equal the TS JSON.stringify of the TS reader's result.
 * GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mods/declaration.h"
#include "qa/content/mods/declaration_writer.h"
#include "qa/content/mounts.h"
#include "qa/contracts/identity.h"
#include "qa/persistence/value.h"
#include "qa/world/session/mods.h"
#include "../../src/app/bootstrap/simulation/shared_simulation_internal.h"

static int failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                       \
    }                                                                   \
  } while (0)

static char *read_file(const char *path, size_t *length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  size_t capacity = 1 << 16, used = 0;
  char *data = malloc(capacity);
  for (;;) {
    if (used == capacity) data = realloc(data, capacity *= 2);
    size_t got = fread(data + used, 1, capacity - used, file);
    if (got == 0) break;
    used += got;
  }
  fclose(file);
  data = realloc(data, used + 1);
  data[used] = '\0';
  *length = used;
  return data;
}

static void test_oracle_rows(void) {
  size_t length = 0;
  char *text = read_file("tests/app/oracles/mod_declarations.tsv", &length);
  CHECK(text != NULL);
  if (text == NULL) return;
  size_t rows = 0, matched = 0, nul_rows = 0, by_source[3] = {0, 0, 0}, runtimes[3] = {0, 0, 0};
  char *line = text;
  while (*line != '\0') {
    char *end = strchr(line, '\n');
    if (end != NULL) *end = '\0';
    char *input = strchr(line, '\t');
    char *expected = input != NULL ? strchr(input + 1, '\t') : NULL;
    CHECK(input != NULL && expected != NULL);
    if (input == NULL || expected == NULL) break;
    *input++ = '\0';
    *expected++ = '\0';
    rows++;
    by_source[strcmp(line, "fixture") == 0 ? 0 : strcmp(line, "synthetic") == 0 ? 1 : 2]++;
    qa_mod_declaration declaration = qa_parse_gameplay_mod_declaration((const uint8_t *)input, (size_t)(expected - 1 - input));
    runtimes[declaration.runtime]++;
    char *written = qa_gameplay_mod_declaration_json(declaration);
    /* C strings end at NUL: the C JSON parser cannot keep a "\u0000" the TS fixtures put in
     * names to test their rejection, so those rows must equal the TS text without it. */
    bool nul = strstr(expected, "\\u0000") != NULL;
    if (nul) {
      char *dst = expected;
      for (const char *src = expected; *src != '\0';) {
        if (strncmp(src, "\\u0000", 6) == 0) src += 6;
        else *dst++ = *src++;
      }
      *dst = '\0';
      nul_rows++;
    }
    if (strcmp(written, expected) == 0) {
      matched++;
    } else {
      size_t at = 0;
      while (written[at] != '\0' && written[at] == expected[at]) at++;
      size_t from = at > 60 ? at - 60 : 0;
      fprintf(stderr, "row %zu (%s) differs at byte %zu\n  C:  %.160s\n  TS: %.160s\n", rows, line, at, written + from,
              expected + from);
      failures++;
    }
    free(written);
    qa_mod_declaration_free(&declaration);
    if (end == NULL) break;
    line = end + 1;
  }
  printf("mod declarations: %zu/%zu byte-identical, %zu of them modulo a NUL the C string drops "
         "(fixture %zu, synthetic %zu, reread %zu; quakec %zu, qvm %zu, native %zu)\n",
         matched, rows, nul_rows, by_source[0], by_source[1], by_source[2], runtimes[0], runtimes[1], runtimes[2]);
  CHECK(rows >= 200);
  free(text);
}

/* JSON.stringify's spellings the writer relies on. */
static void test_stringify(void) {
  qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "negative zero", qa_save_value_number(-0.0));
  qa_save_value_set(o, "tiny", qa_save_value_number(1e-7));
  qa_save_value_set(o, "big", qa_save_value_number(1e21));
  qa_save_value_set(o, "nan", qa_save_value_number(NAN));
  qa_save_value_set(o, "inf", qa_save_value_number(-INFINITY));
  qa_save_value_set(o, "third", qa_save_value_number(0.1 + 0.2));
  /* "a\"\\\n\t\x01\x7f" then U+D800 as WTF-8 then U+00E9. */
  qa_save_value_set(o, "text", qa_save_value_string("a\"\\\n\t\x01\x7f\xed\xa0\x80\xc3\xa9"));
  qa_save_value_set(o, "skipped", qa_save_value_undefined());
  qa_save_value *a = qa_save_value_array();
  qa_save_value_push(a, qa_save_value_undefined());
  qa_save_value_push(a, qa_save_value_boolean(false));
  qa_save_value_push(a, qa_save_value_null());
  qa_save_value_set(o, "list", a);
  size_t length = 0;
  char *text = qa_json_stringify_value(o, &length);
  const char *expected =
      "{\"negative zero\":0,\"tiny\":1e-7,\"big\":1e+21,\"nan\":null,\"inf\":null,\"third\":0.30000000000000004,"
      "\"text\":\"a\\\"\\\\\\n\\t\\u0001\x7f\\ud800\xc3\xa9\",\"list\":[null,false,null]}";
  CHECK(strcmp(text, expected) == 0);
  CHECK(length == strlen(expected));
  if (strcmp(text, expected) != 0) fprintf(stderr, "  C:  %s\n  TS: %s\n", text, expected);
  free(text);
  qa_save_value_free(o);
}

/* An optional list the TS reader keeps is written even when empty; one it
 * leaves out is not. */
static void test_present_lists(void) {
  static const char present[] =
      "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"progs.dat\",\"digest\":\"sha256:"
      "abababababababababababababababababababababababababababababababab\"},\"actorFields\":[],\"callbacks\":[],"
      "\"initialize\":[],\"cvars\":[],\"protection\":[]}";
  static const char absent[] =
      "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"progs.dat\",\"digest\":\"sha256:"
      "abababababababababababababababababababababababababababababababab\"},\"actorFields\":[],\"callbacks\":[]}";
  qa_mod_declaration declaration = qa_parse_gameplay_mod_declaration((const uint8_t *)present, sizeof present - 1);
  char *written = qa_gameplay_mod_declaration_json(declaration);
  CHECK(strcmp(written, present) == 0);
  free(written);
  qa_mod_declaration_free(&declaration);
  declaration = qa_parse_gameplay_mod_declaration((const uint8_t *)absent, sizeof absent - 1);
  written = qa_gameplay_mod_declaration_json(declaration);
  CHECK(strcmp(written, absent) == 0);
  free(written);
  qa_mod_declaration_free(&declaration);
}


/* ---- retail: a gameplay mod recipe through the native loader ---- */
static const char *tmp_root(void) {
  const char *tmp = getenv("TMPDIR");
  return tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp";
}
typedef struct lines { char **items; size_t count; } lines;
static void emit(lines *out, const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *line = NULL;
  if (vasprintf(&line, format, args) < 0) abort();
  va_end(args);
  out->items = realloc(out->items, (out->count + 1) * sizeof *out->items);
  out->items[out->count++] = line;
}
static void write_text(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  CHECK(file != NULL);
  if (file == NULL) return;
  fputs(text, file);
  fclose(file);
}
static int sentinel_identity, sentinel_prompt, sentinel_frame;
static bool player_identity(void *ctx, qa_client_id client, double *out_seat, const char **out_social_id) {
  (void)client;
  if (ctx != &sentinel_identity) abort();
  *out_seat = 0;
  *out_social_id = "";
  return true;
}
static bool prompt_supported(void *ctx, qa_client_id client) {
  (void)client;
  if (ctx != &sentinel_prompt) abort();
  return false;
}
static void next_frame(void *ctx) {
  if (ctx != &sentinel_frame) abort();
}
static qa_save_value *selection_tree(qa_mod_selection selection) {
  qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "product", qa_save_value_string(selection.product));
  qa_save_value_set(o, "id", qa_save_value_string(selection.id));
  return o;
}
/* JSON.stringify(recipe.mods): discoverGameplayMods' key order. */
static char *recipe_mods_json(const qa_executable_recipe *recipe) {
  qa_save_value *mods = qa_save_value_array();
  for (size_t i = 0; i < recipe->mod_count; i++) {
    const qa_resolved_gameplay_mod *mod = recipe->mods[i];
    CHECK(!mod->read_order);
    qa_save_value *o = qa_save_value_object();
    qa_save_value_set(o, "selection", selection_tree(mod->selection));
    qa_save_value_set(o, "title", qa_save_value_string(mod->title));
    qa_save_value_set(o, "sourceTitle", qa_save_value_string(mod->source_title));
    qa_save_value *source = qa_save_value_object();
    qa_save_value_set(source, "provider", qa_save_value_string(mod->source.provider));
    qa_save_value_set(source, "content", qa_save_value_string(mod->source.content));
    qa_save_value_set(o, "source", source);
    qa_save_value *requires = qa_save_value_array();
    for (size_t r = 0; r < mod->requires_count; r++) qa_save_value_push(requires, selection_tree(mod->requires[r]));
    qa_save_value_set(o, "requires", requires);
    qa_save_value *conflicts = qa_save_value_array();
    for (size_t r = 0; r < mod->conflicts_count; r++) qa_save_value_push(conflicts, selection_tree(mod->conflicts[r]));
    qa_save_value_set(o, "conflicts", conflicts);
    qa_save_value_set(o, "declaration", qa_gameplay_mod_declaration_value(mod->declaration));
    qa_save_value_set(o, "declarationDigest", qa_save_value_string(mod->declaration_digest));
    qa_save_value_push(mods, o);
  }
  char *text = qa_json_stringify_value(mods, NULL);
  qa_save_value_free(mods);
  return text;
}
/* The oracle steps 10 frames; a diagnostic build may lower it. */
#ifndef RETAIL_FRAMES
#define RETAIL_FRAMES 10
#endif
static const char *const retail_expected[] = {
  "recipe-mods [{\"selection\":{\"product\":\"q1-classic-id1\",\"id\":\"count\"},\"title\":\"Count\",\"sourceTitle\":\"Quake (Q1)\","
  "\"source\":{\"provider\":\"q1:official\",\"content\":\"q1:classic:id1:installed\"},\"requires\":[],\"conflicts\":[],"
  "\"declaration\":{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"progs.dat\","
  "\"digest\":\"sha256:f2619787f9aa0f057246eea1665b622b4691b5c5a800b1a46133d1fe8b771580\"},"
  "\"actorFields\":[{\"field\":\"health\",\"binding\":\"health\"}],\"callbacks\":[],\"cvars\":[{\"name\":\"count_mod\",\"value\":\"7\"}]},"
  "\"declarationDigest\":\"sha256:5dc7b9dd5ef0f7983b2cebc5f957a81dc0df841d657819af13c1dc81d70e9578\"}]",
  "prepared 1",
  "identity q1-classic-id1/count q1:official q1:classic:id1:installed sha256:5dc7b9dd5ef0f7983b2cebc5f957a81dc0df841d657819af13c1dc81d70e9578",
  "module mod:q1-classic-id1%2Fcount progs.dat sha256:f2619787f9aa0f057246eea1665b622b4691b5c5a800b1a46133d1fe8b771580 "
  "sha256:5dc7b9dd5ef0f7983b2cebc5f957a81dc0df841d657819af13c1dc81d70e9578",
  "description Count addition 0 0",
  "mods-enabled q1-classic-id1/count",
  "checkpoint-module mod:q1-classic-id1%2Fcount",
};
static void test_retail_mod_recipe(void) {
  const char *corpus = getenv("QTS_TEST_CORPUS");
  char home[512];
  if (corpus == NULL || corpus[0] == '\0') {
    snprintf(home, sizeof home, "%s/qfiles", getenv("HOME") ? getenv("HOME") : "");
    corpus = home;
  }
  char pak[1024];
  snprintf(pak, sizeof pak, "%s/Quake/Id1", corpus);
  struct stat info;
  if (stat(pak, &info) != 0) {
    printf("skip retail mod recipe: no Quake/Id1 under %s\n", corpus);
    return;
  }
  char user_root[1024], directory[1100], path[1300], command_text[2200];
  snprintf(user_root, sizeof user_root, "%s/mod-retail-c", tmp_root());
  snprintf(command_text, sizeof command_text, "rm -rf '%s' && mkdir -p '%s/q1/id1'", user_root, user_root);
  if (system(command_text) != 0) { CHECK(false); return; }
  snprintf(directory, sizeof directory, "%s/q1/id1", user_root);
  /* The retail progs.dat digest, read through the product's mounts. */
  qa_content_catalog catalog;
  memset(&catalog, 0, sizeof catalog);
  qa_discover_content_options discover;
  memset(&discover, 0, sizeof discover);
  discover.corpus_root = corpus;
  discover.user_content_root = user_root;
  discover.discover_mods = true;
  discover.inspector = qa_app_default_archive_inspector();
  CHECK(qa_discover_installed_content(&discover, &catalog));
  qa_app_product_mounts *mounts = qa_app_open_product_mounts(&catalog,
      qa_content_catalog_require(&catalog, "q1-classic-id1")->id, "oracle", false);
  qa_content_opened_resource program = {0};
  CHECK(qa_content_mounted_open(qa_app_product_mounts_mounted(mounts), "progs.dat", NULL, NULL, &program));
  char digest[128];
  snprintf(digest, sizeof digest, "%s", program.reference != NULL ? program.reference->digest : "");
  free(program.bytes);
  qa_app_product_mounts_destroy(mounts);
  qa_content_catalog_destroy(&catalog);
  /* JSON.stringify of the oracle's declaration and gameplay-mods.json. */
  char declaration[1024];
  snprintf(declaration, sizeof declaration,
           "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"progs.dat\",\"digest\":\"%s\"},"
           "\"actorFields\":[{\"field\":\"health\",\"binding\":\"health\"}],\"callbacks\":[],"
           "\"cvars\":[{\"name\":\"count_mod\",\"value\":\"7\"}]}", digest);
  snprintf(path, sizeof path, "%s/count-mod.json", directory);
  write_text(path, declaration);
  snprintf(path, sizeof path, "%s/gameplay-mods.json", directory);
  write_text(path, "{\"version\":1,\"components\":[{\"id\":\"count\",\"title\":\"Count\",\"purpose\":\"addition\","
                   "\"callbacks\":\"count-mod.json\",\"requires\":[],\"conflicts\":[]}]}");
  const char *argv[] = {"--content-root", corpus, "--user-content-root", user_root, "--game", "q1-classic-id1",
                        "--map", "e1m1", "--mod", "q1-classic-id1/count", "--dedicated"};
  qa_content_root_search search = {corpus, NULL};
  qa_app_application_command command = qa_app_parse_application_command(argv, sizeof argv / sizeof argv[0], search, NULL);
  CHECK(strcmp(command.kind, "run") == 0);
  qa_app_content_host host;
  memset(&host, 0, sizeof host);
  qa_app_options launch = command.options;
  memset(&command.options, 0, sizeof command.options);
  qa_app_loaded_content *content = qa_app_load_application_content_native(host, launch, NULL, false, NULL, NULL, NULL);
  CHECK(content != NULL);
  if (content == NULL) { qa_app_application_command_destroy(&command); return; }
  lines out = {NULL, 0};
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  char *mods_json = recipe_mods_json(recipe);
  emit(&out, "recipe-mods %s", mods_json);
  free(mods_json);
  size_t prepared_count = 0;
  const void *const *prepared = qa_app_loaded_content_prepared_mods(content, &prepared_count);
  emit(&out, "prepared %zu", prepared_count);
  for (size_t i = 0; i < prepared_count; i++) {
    const qa_prepared_mod *mod = prepared[i];
    const qa_mod_identity *identity = &mod->identity;
    emit(&out, "identity %s/%s %s %s %s", identity->selection.product, identity->selection.id, identity->source.provider,
         identity->source.content, identity->declaration_digest);
    for (size_t m = 0; m < identity->module_count; m++)
      emit(&out, "module %s %s %s %s", identity->modules[m].id, identity->modules[m].artifact_path, identity->modules[m].digest,
           identity->modules[m].revision);
    emit(&out, "description %s %s %zu %zu", mod->description.title, mod->description.purpose, mod->description.requires_count,
         mod->description.conflicts_count);
  }
  qa_identity_owner identity;
  if (!qa_identity_owner_create(&identity, "mod-retail")) abort();
  qa_app_application_world world = qa_app_loaded_content_world(content);
  qa_app_simulation_options options;
  memset(&options, 0, sizeof options);
  options.dedicated = true;
  options.identity = identity;
  options.recipe = *recipe;
  options.world = &world;
  options.mounts = qa_app_loaded_content_mounts(content);
  options.prepared_mods = prepared;
  options.prepared_mod_count = prepared_count;
  qa_mod_selection *enabled = calloc(recipe->mod_count + 1, sizeof *enabled);
  for (size_t i = 0; i < recipe->mod_count; i++) enabled[i] = recipe->mods[i]->selection;
  qa_app_sim_mod_selections selections = {enabled, recipe->mod_count};
  options.enabled_mods = &selections;
  options.skill = launch.skill;
  options.mode = launch.mode;
  options.seed = launch.seed;
  options.max_clients = 1;
  options.has_player_colors = true;
  options.prompt_ctx = &sentinel_prompt;
  options.prompt_supported = prompt_supported;
  options.identity_ctx = &sentinel_identity;
  options.player_identity = player_identity;
  qa_app_shared_simulation *sim = qa_app_shared_simulation_load(options, next_frame, &sentinel_frame);
  (void)qa_app_shared_sim_admit_player(sim, qa_identity_owner_client(&identity, 0, 0));
  for (int i = 0; i < RETAIL_FRAMES; i++) {
    qa_input_batch batch = {100, NULL, 0};
    (void)qa_app_shared_sim_step(sim, batch);
  }
  if (sim->mod_owner == NULL) {
    emit(&out, "mods-enabled none");
  } else {
    size_t count = 0;
    const qa_mod_selection *active = qa_session_mods_enabled(sim->mod_owner, &count);
    char joined[512] = "";
    for (size_t i = 0; i < count; i++) {
      char row[160];
      snprintf(row, sizeof row, "%s%s/%s", i == 0 ? "" : ",", active[i].product, active[i].id);
      strncat(joined, row, sizeof joined - strlen(joined) - 1);
    }
    emit(&out, "mods-enabled %s", joined);
    qa_mod_checkpoint *checkpoint = NULL;
    qa_session_mods_checkpoint(sim->mod_owner, &checkpoint, &count);
    for (size_t i = 0; i < count; i++) {
      char modules[512] = "";
      for (size_t m = 0; m < checkpoint[i].identity.module_count; m++) {
        char row[200];
        snprintf(row, sizeof row, "%s%s", m == 0 ? "" : ",", checkpoint[i].identity.modules[m].id);
        strncat(modules, row, sizeof modules - strlen(modules) - 1);
      }
      emit(&out, "checkpoint-module %s", modules);
    }
    free(checkpoint);
  }
  size_t expected_count = sizeof retail_expected / sizeof retail_expected[0];
  CHECK(out.count == expected_count);
  for (size_t i = 0; i < out.count; i++) {
    const char *want = i < expected_count ? retail_expected[i] : "<end>";
    if (strcmp(want, out.items[i]) != 0) {
      fprintf(stderr, "retail line %zu\n  ts: %s\n  c:  %s\n", i + 1, want, out.items[i]);
      failures++;
    }
    free(out.items[i]);
  }
  free(out.items);
  printf("retail mod recipe: %zu lines compared with bun\n", expected_count);
  qa_app_shared_sim_close(sim);
  qa_app_shared_simulation_destroy(sim);
  free(enabled);
  qa_identity_owner_destroy(&identity);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  qa_app_application_command_destroy(&command);
}

int main(void) {
  test_stringify();
  test_present_lists();
  test_oracle_rows();
  test_retail_mod_recipe();
  if (failures != 0) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("test_mod_declarations: ok\n");
  return 0;
}
