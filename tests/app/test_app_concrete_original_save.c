/*
 * prepareApplicationSave (src/app/bootstrap/original-save.ts) on retail id1 e1m1 over the real catalog, content loader
 * and SharedSimulation: the scenario of tests/app/oracles/app_concrete_original_save.ts, whose bun transcript
 * (app_concrete_original_save.txt) this test reproduces line for line. A QuakeC world is stepped 30 frames, captured
 * as an original v5 save under <tmp>/q1-classic-id1/manual.sav, and prepared (contextual, explicit, ambiguous and
 * mismatched products, a missing file, a broken KEX archive and a shared save). The capture line compares the encoded
 * v5 bytes with the TS capture. Needs QTS_TEST_CORPUS. GPL-2.0-or-later.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/sha.h>

#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/original_save.h"
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/q1.h"
#include "qa/persistence/save_image.h"
#include "../../src/app/bootstrap/simulation/shared_simulation_internal.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)

static char transcript[16384];
static void out(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *format, ...) {
  char line[4096];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  strncat(transcript, line, sizeof transcript - strlen(transcript) - 2);
  strcat(transcript, "\n");
}
static void sha16(const uint8_t *bytes, size_t length, char text[17]) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(bytes, length, digest);
  for (int i = 0; i < 8; i++) snprintf(text + i * 2, 3, "%02x", digest[i]);
}
static void write_file(const char *path, const uint8_t *bytes, size_t length) {
  FILE *file = fopen(path, "wb");
  if (file == NULL || fwrite(bytes, 1, length, file) != length) { perror(path); abort(); }
  fclose(file);
}
static const char *family_name(qa_game_family family) {
  return family == QA_GAME_FAMILY_Q1 ? "q1" : family == QA_GAME_FAMILY_Q2 ? "q2" : family == QA_GAME_FAMILY_Q3 ? "q3" : "?";
}

static char tmp_root[1024];
static qa_app_options parse(const char *corpus, const char *user) {
  const char *argv[] = {"--content-root", corpus, "--user-content-root", user, "--game", "q1-classic-id1", "--map", "e1m1",
      "--progs", "progs.dat", "--movement", "q1", "--character", "q1", "--dedicated", "--skill", "2"};
  qa_content_root_search search = {corpus, NULL};
  qa_app_application_command command = qa_app_parse_application_command(argv, sizeof argv / sizeof *argv, search, NULL);
  if (strcmp(command.kind, "run") != 0) { fprintf(stderr, "expected run\n"); abort(); }
  qa_app_options options = command.options;
  memset(&command.options, 0, sizeof command.options);
  qa_app_application_command_destroy(&command);
  return options;
}

static void show_options(const qa_app_options *o) {
  out("options product %s map %s skill %s mode %s movement %s character %s", o->product, o->map, QA_JS_NUMBER(o->skill), o->mode,
      family_name(o->movement), family_name(o->character));
  out("options model %s seats %s rules %s progs %s network %s", o->character_model, QA_JS_NUMBER(o->seats), o->rules ? o->rules : "-",
      o->quakec_program ? o->quakec_program : "-", o->network_kind);
  bool explicit_rules = o->explicit_skill || o->explicit_mode || o->explicit_capacity;
  char startup[32];
  if (o->startup_command_count == 0) snprintf(startup, sizeof startup, "-");
  else snprintf(startup, sizeof startup, "%zu", o->startup_command_count);
  out("options mapProduct %s movementProduct %s q2 %s startup %s authored %s explicitRules %s", o->map_product ? o->map_product : "-",
      o->movement_product ? o->movement_product : "-", o->q2_game_library ? o->q2_game_library : "-", startup,
      o->authored_campaign_start ? "true" : "-", explicit_rules ? "true" : "-");
}
static void show_image(const qa_save_image *image) {
  out("image entities %s %s map %s", image->recipe.map.entities.content, image->recipe.map.entities.provider,
      image->recipe.map.geometry.requested_path);
  out("image time %s %s actors %zu bodies %zu guests %zu", image->frame.time.kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds",
      QA_JS_NUMBER(image->frame.time.value), image->actor_count, image->body_count, image->guest_count);
  char providers[4096] = "";
  for (size_t i = 0; i < image->provider_count; i++) {
    char entry[256];
    snprintf(entry, sizeof entry, "%s%s@%s", i == 0 ? "" : ",", image->providers[i].schema, QA_JS_NUMBER(image->providers[i].version));
    strncat(providers, entry, sizeof providers - strlen(providers) - 1);
  }
  out("image providers %s", providers);
}

typedef struct attempt_call {
  const qa_app_options *options;
  const qa_content_catalog *catalog;
  const char *path;
  const char *source;
  qa_app_prepared_application_save prepared;
  void *backing;
} attempt_call;
static void attempt_body(void *ctx) {
  attempt_call *call = ctx;
  qa_app_prepare_application_save_native(call->options, call->catalog, call->path, call->source, &call->prepared, &call->backing);
}
/* message.split(tmp).join("<tmp>") */
static void replace_tmp(const char *message, char *outbuf, size_t capacity) {
  size_t used = 0, root = strlen(tmp_root);
  for (const char *p = message; *p != '\0' && used + 6 < capacity;) {
    if (strncmp(p, tmp_root, root) == 0) { memcpy(outbuf + used, "<tmp>", 5); used += 5; p += root; }
    else outbuf[used++] = *p++;
  }
  outbuf[used] = '\0';
}
static void attempt(const char *label, const qa_app_options *options, const qa_content_catalog *catalog, const char *path,
    const char *source) {
  attempt_call call;
  memset(&call, 0, sizeof call);
  call.options = options; call.catalog = catalog; call.path = path; call.source = source;
  qa_caught_error caught;
  if (qa_try(attempt_body, &call, &caught)) {
    out("%s ok", label);
    show_options(&call.prepared.options);
    show_image(&call.prepared.image);
    qa_app_prepared_application_save_free(&call.prepared, call.backing);
  } else {
    char message[2048];
    replace_tmp(caught.message, message, sizeof message);
    out("%s error %s", label, message);
    CHECK(call.backing == NULL);
  }
}

int main(void) {
  const char *corpus = getenv("QTS_TEST_CORPUS");
  if (corpus == NULL) { printf("skip: QTS_TEST_CORPUS not set\n0 checks, 0 failures\n"); return 0; }
  const char *base = getenv("TMPDIR");
  snprintf(tmp_root, sizeof tmp_root, "%s/app-concrete-original-save-XXXXXX", base != NULL && base[0] != '\0' ? base : "/tmp");
  if (mkdtemp(tmp_root) == NULL) { perror("mkdtemp"); return 1; }
  char user[1200], directory[1200], path[1300], ambiguous[1200], ambiguous_path[1300], shared_path[1300], absent[1300], broken[1300];
  snprintf(user, sizeof user, "%s/user", tmp_root);
  snprintf(directory, sizeof directory, "%s/q1-classic-id1", tmp_root);
  snprintf(path, sizeof path, "%s/manual.sav", directory);
  snprintf(ambiguous, sizeof ambiguous, "%s/saves", tmp_root);
  snprintf(ambiguous_path, sizeof ambiguous_path, "%s/manual.sav", ambiguous);
  snprintf(shared_path, sizeof shared_path, "%s/shared.sav", tmp_root);
  snprintf(absent, sizeof absent, "%s/absent.sav", tmp_root);
  snprintf(broken, sizeof broken, "%s/broken.pak.gz", tmp_root);
  if (mkdir(user, 0777) != 0 || mkdir(directory, 0777) != 0 || mkdir(ambiguous, 0777) != 0) { perror("mkdir"); return 1; }

  qa_app_options options = parse(corpus, user);
  qa_app_options launch_options = parse(corpus, user); /* the loader takes over the copy it is given */
  qa_app_loaded_content *content = qa_app_load_application_content_native((qa_app_content_host){0}, launch_options, NULL, false,
      NULL, NULL, NULL);
  CHECK(content != NULL);
  qa_identity_owner identity;
  if (!qa_identity_owner_create(&identity, "original-save-oracle")) abort();
  qa_app_application_world world = qa_app_loaded_content_world(content);
  qa_app_simulation_options sim_options;
  memset(&sim_options, 0, sizeof sim_options);
  sim_options.identity = identity;
  sim_options.recipe = *qa_app_loaded_content_recipe(content);
  sim_options.world = &world;
  sim_options.mounts = qa_app_loaded_content_mounts(content);
  sim_options.prepared_quakec = qa_app_loaded_content_prepared_quakec(content);
  sim_options.dedicated = true;
  sim_options.skill = options.skill; sim_options.mode = options.mode; sim_options.seed = options.seed;
  sim_options.max_clients = 1;
  qa_app_shared_simulation *sim = qa_app_shared_simulation_create(sim_options);
  (void)qa_app_shared_sim_admit_player(sim, qa_identity_owner_client(&identity, 0, 0));
  for (int frame = 0; frame < 30; frame++) (void)qa_app_shared_sim_step(sim, (qa_input_batch){100, NULL, 0});
  qa_q1_save_data data;
  memset(&data, 0, sizeof data);
  CHECK(qa_app_shared_sim_capture_original_save(sim, 5, NULL, "manual", &data));
  size_t length = 0;
  uint8_t *bytes = qa_encode_q1_save(&data, &length);
  char digest[17];
  sha16(bytes, length, digest);
  out("capture time %s skill %s map %s sha %s", QA_JS_NUMBER(data.time), QA_JS_NUMBER(data.skill), data.map, digest);
  write_file(path, bytes, length);
  write_file(ambiguous_path, bytes, length);
  free(bytes);
  qa_q1_save_data_free(&data);
  qa_app_shared_sim_save shared;
  memset(&shared, 0, sizeof shared);
  qa_app_shared_sim_checkpoint(sim, &shared);
  uint8_t *shared_bytes = NULL;
  size_t shared_length = 0;
  CHECK(qa_encode_save_image(&shared.image, &shared_bytes, &shared_length));
  write_file(shared_path, shared_bytes, shared_length);
  free(shared_bytes);
  qa_app_shared_sim_save_free(&shared);
  qa_app_shared_sim_close(sim);
  qa_app_shared_simulation_destroy(sim);
  qa_identity_owner_destroy(&identity);
  qa_app_loaded_content_close(content);
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(content);

  /* The launch options carry fields the original import drops. */
  qa_app_options launch = options;
  launch.authored_campaign_start = true;
  char *startup[] = {"echo hi"};
  launch.startup_commands = startup; launch.startup_command_count = 1;
  launch.explicit_skill = true;
  launch.map_product = "q1-classic-id1";
  attempt("contextual", &launch, catalog, path, NULL);
  attempt("explicit", &options, catalog, ambiguous_path, "q1-classic-id1");
  attempt("ambiguous", &options, catalog, ambiguous_path, NULL);
  attempt("mismatch", &options, catalog, path, "q2-classic-baseq2");
  attempt("missing", &options, catalog, absent, NULL);
  static const uint8_t junk[] = {1, 2, 3};
  write_file(broken, junk, sizeof junk);
  attempt("kex", &options, catalog, broken, NULL);
  attempt("shared", &launch, catalog, shared_path, NULL);
  qa_app_loaded_content_destroy(content);
  qa_app_options_destroy(&options);

  char clear[1400];
  snprintf(clear, sizeof clear, "rm -rf '%s'", tmp_root);
  if (system(clear) != 0) perror(clear);
  FILE *file = fopen("tests/app/oracles/app_concrete_original_save.txt", "r");
  CHECK(file != NULL);
  if (file != NULL) {
    static char expected[16384];
    size_t read = fread(expected, 1, sizeof expected - 1, file);
    expected[read] = '\0';
    fclose(file);
    CHECK(strcmp(expected, transcript) == 0);
    if (strcmp(expected, transcript) != 0) fprintf(stderr, "expected:\n%s\nactual:\n%s\n", expected, transcript);
  }
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
