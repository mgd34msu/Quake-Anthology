/*
 * Port of the persistence slice-a cases: the q1.ts save codec behind
 * tests/persistence/q1-source.test.ts (the integration restore itself
 * needs the unported app/compat modules, so the text codec is covered
 * directly), plus mods.ts reader cases derived from the TS codecs
 * (no dedicated TS mods.test; native-weapon.test.ts covers the blocked
 * native-weapon.ts). TypeScript SaveFormatError throws run in a child
 * process and must abort. GPL-2.0-or-later.
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

#include "qa/persistence/mods.h"
#include "qa/persistence/q1.h"

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

static int text_is(const char *got, const char *want) {
  return got != NULL && strcmp(got, want) == 0;
}

/* --- q1.ts --- */

/* Valid header through the light styles, then `tail`. */
static size_t make_fixture(char *buffer, size_t size, int version, const char *tail) {
  size_t length = 0;
  length += (size_t)snprintf(buffer + length, size - length, "%d\n", version);
  if (version == 6)
    length += (size_t)snprintf(buffer + length, size - length, "id1;hipnotic\n");
  length += (size_t)snprintf(buffer + length, size - length, "\"Quake save\"\n");
  length += (size_t)snprintf(buffer + length, size - length,
                             "0 0.5 -1 1e2 0x10 0 0 0 0 0 0 0 0 0 0 0\n");
  length += (size_t)snprintf(buffer + length, size - length, "1.95\ne1m1\n12.5\n");
  for (int i = 0; i < 64; i++)
    length += (size_t)snprintf(buffer + length, size - length, "%s\n", i == 0 ? "m" : "z");
  length += (size_t)snprintf(buffer + length, size - length, "%s", tail);
  return length;
}

static void test_q1_decode(void) {
  static char buffer[8192];
  size_t length = make_fixture(buffer, sizeof buffer, 5,
      "// globals next\n"
      "{\n\"mapname\" \"e1m1\"\n\"serverflags\" \"0\"\n}\n"
      "{\n}\n"
      "{ \"classname\" \"worldspawn\" \"wad\" \"gfx/base.wad\" }\n"
      "//ext\nblah");
  qa_q1_save_data save;
  CHECK(qa_decode_q1_save((const uint8_t *)buffer, length, &save));
  CHECK(save.format_version == 5);
  CHECK(save.game_directories == NULL);
  CHECK(text_is(save.comment, "Quake save")); /* quoted token keeps spaces */
  CHECK(save.spawn_parameter_count == 16);
  CHECK(save.spawn_parameters[0] == 0 && save.spawn_parameters[1] == 0.5 &&
        save.spawn_parameters[2] == -1 && save.spawn_parameters[3] == 100 &&
        save.spawn_parameters[4] == 16); /* TS Number("0x10") */
  CHECK(save.skill == 2); /* Math.trunc(1.95 + 0.1) */
  CHECK(text_is(save.map, "e1m1"));
  CHECK(save.time == 12.5);
  CHECK(save.light_style_count == 64);
  CHECK(text_is(save.light_styles[0], "m") && text_is(save.light_styles[63], "z"));
  CHECK(save.global_count == 2);
  CHECK(text_is(save.globals[0].key, "mapname") && text_is(save.globals[0].value, "e1m1"));
  CHECK(text_is(save.globals[1].key, "serverflags"));
  CHECK(save.entity_count == 2);
  CHECK(save.entities[0].pair_count == 0); /* empty record: free slot */
  CHECK(save.entities[1].pair_count == 2);
  CHECK(text_is(save.entities[1].pairs[0].key, "classname") &&
        text_is(save.entities[1].pairs[0].value, "worldspawn"));
  /* Extension starts where the post-record whitespace scan stopped;
   * the comment that skip() consumed is included verbatim. */
  CHECK(save.extension_length == strlen("//ext\nblah"));
  CHECK(text_is(save.extension_text, "//ext\nblah"));
  qa_q1_save_data_free(&save);

  length = make_fixture(buffer, sizeof buffer, 6, "{ }\n");
  CHECK(qa_decode_q1_save((const uint8_t *)buffer, length, &save));
  CHECK(save.format_version == 6);
  CHECK(text_is(save.game_directories, "id1;hipnotic"));
  CHECK(save.entity_count == 0);
  CHECK(save.extension_length == 0);
  qa_q1_save_data_free(&save);
}

static void test_q1_roundtrip(void) {
  static char buffer[8192];
  size_t length = make_fixture(buffer, sizeof buffer, 5,
      "{\n\"mapname\" \"e1m1\"\n}\n{ \"k\" \"v\" }\nEXT");
  qa_q1_save_data save;
  CHECK(qa_decode_q1_save((const uint8_t *)buffer, length, &save));
  save.comment[5] = '_'; /* decoded "Quake save" is not an encode token */
  size_t encoded_length = 0;
  uint8_t *encoded = qa_encode_q1_save(&save, &encoded_length);
  CHECK(encoded != NULL);
  qa_q1_save_data back;
  CHECK(qa_decode_q1_save(encoded, encoded_length, &back));
  CHECK(back.format_version == 5 && text_is(back.map, "e1m1") && back.time == 12.5);
  CHECK(back.skill == 2);
  CHECK(back.spawn_parameters[1] == 0.5 && back.spawn_parameters[3] == 100);
  CHECK(back.global_count == 1 && text_is(back.globals[0].value, "e1m1"));
  CHECK(back.entity_count == 1 && back.entities[0].pair_count == 1 &&
        text_is(back.entities[0].pairs[0].key, "k"));
  CHECK(text_is(back.extension_text, "EXT"));
  qa_q1_save_data_free(&back);
  free(encoded);
  qa_q1_save_data_free(&save);
}

static qa_q1_save_data make_encode_save(double *spawn, char **styles,
                                        qa_q1_text_pair *globals,
                                        qa_q1_text_record *entities) {
  qa_q1_save_data save;
  memset(&save, 0, sizeof save);
  save.format_version = 5;
  save.comment = (char *)"QSAVE";
  save.spawn_parameters = spawn;
  save.spawn_parameter_count = 16;
  save.skill = 1.9;
  save.map = (char *)"start";
  save.time = 3.25;
  save.light_styles = styles;
  save.light_style_count = 64;
  save.globals = globals;
  save.global_count = 1;
  save.entities = entities;
  save.entity_count = 1;
  save.extension_text = (char *)"";
  save.extension_length = 0;
  return save;
}

static void test_q1_encode_text(void) {
  double spawn[16] = {0};
  char *styles[64];
  for (int i = 0; i < 64; i++) styles[i] = (char *)"m";
  styles[0] = (char *)"";   /* TS: style || "m" */
  styles[1] = (char *)"abc";
  qa_q1_text_pair globals[] = {{(char *)"mapname", (char *)"start"}};
  qa_q1_text_pair pairs[] = {{(char *)"classname", (char *)"worldspawn"}};
  qa_q1_text_record entities[] = {{pairs, 1}};
  qa_q1_save_data save = make_encode_save(spawn, styles, globals, entities);
  size_t length = 0;
  uint8_t *encoded = qa_encode_q1_save(&save, &length);
  CHECK(encoded != NULL);
  static char want[8192];
  size_t w = 0;
  w += (size_t)snprintf(want + w, sizeof want - w, "5\nQSAVE\n");
  for (int i = 0; i < 16; i++) w += (size_t)snprintf(want + w, sizeof want - w, "0.000000\n");
  w += (size_t)snprintf(want + w, sizeof want - w, "1\nstart\n3.250000\nm\nabc\n");
  for (int i = 2; i < 64; i++) w += (size_t)snprintf(want + w, sizeof want - w, "m\n");
  w += (size_t)snprintf(want + w, sizeof want - w,
                        "{\n\"mapname\" \"start\"\n}\n{\n\"classname\" \"worldspawn\"\n}\n");
  CHECK(length == w && memcmp(encoded, want, w) == 0);
  free(encoded);
}

static void test_q1_errors(void) {
  static char buffer[8192];
  qa_q1_save_data save;
  size_t length = 0;
  CHECK_ABORTS(qa_decode_q1_save((const uint8_t *)"", 0, &save)); /* truncated */
  CHECK_ABORTS(qa_decode_q1_save((const uint8_t *)"7\n", 2, &save)); /* version */
  CHECK_ABORTS(qa_decode_q1_save((const uint8_t *)"5\n\"open", 7, &save)); /* unterminated */
  CHECK_ABORTS(qa_decode_q1_save((const uint8_t *)"5\nQSAVE\nabc\n", 12, &save)); /* numeric */
  length = make_fixture(buffer, sizeof buffer, 5, "x\n");
  CHECK_ABORTS(qa_decode_q1_save((const uint8_t *)buffer, length, &save)); /* brace */
  length = make_fixture(buffer, sizeof buffer, 5, "{ \"k\" }\n");
  CHECK_ABORTS(qa_decode_q1_save((const uint8_t *)buffer, length, &save)); /* no value */

  double spawn[16] = {0};
  char *styles[64];
  for (int i = 0; i < 64; i++) styles[i] = (char *)"m";
  qa_q1_text_pair globals[] = {{(char *)"k", (char *)"v"}};
  qa_q1_text_record entities[] = {{NULL, 0}};
  qa_q1_save_data good = make_encode_save(spawn, styles, globals, entities);
  qa_q1_save_data bad = good;
  bad.spawn_parameter_count = 3;
  CHECK_ABORTS(qa_encode_q1_save(&bad, &length));
  bad = good;
  bad.comment = (char *)"has space";
  CHECK_ABORTS(qa_encode_q1_save(&bad, &length));
  bad = good;
  spawn[0] = NAN;
  CHECK_ABORTS(qa_encode_q1_save(&bad, &length));
  spawn[0] = 0;
  bad = good;
  globals[0].key = (char *)"ba\"d";
  CHECK_ABORTS(qa_encode_q1_save(&bad, &length));
  globals[0].key = (char *)"k";
}

/* --- mods.ts --- */

#define DIGEST "sha256:" \
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

static qa_save_value *obj(void) { return qa_save_value_object(); }
static void set_string(qa_save_value *object, const char *key, const char *value) {
  CHECK(qa_save_value_set(object, key, qa_save_value_string(value)));
}
static void set_number(qa_save_value *object, const char *key, double value) {
  CHECK(qa_save_value_set(object, key, qa_save_value_number(value)));
}

static qa_save_value *make_identity(const char *product, const char *id) {
  qa_save_value *identity = obj();
  qa_save_value *selection = obj();
  set_string(selection, "product", product);
  set_string(selection, "id", id);
  CHECK(qa_save_value_set(identity, "selection", selection));
  qa_save_value *source = obj();
  set_string(source, "provider", "content:catalog");
  set_string(source, "content", "q1:classic:id1:1.06");
  CHECK(qa_save_value_set(identity, "source", source));
  set_string(identity, "declarationDigest", DIGEST);
  CHECK(qa_save_value_set(identity, "modules", qa_save_value_array()));
  qa_save_value *providers = qa_save_value_array();
  qa_save_value *provider = obj();
  set_string(provider, "provider", "mod:state");
  set_string(provider, "schema", "mod:checkpoint");
  set_number(provider, "version", 2);
  CHECK(qa_save_value_push(providers, provider));
  CHECK(qa_save_value_set(identity, "providers", providers));
  return identity;
}

static qa_save_value *make_session(qa_save_value *first, qa_save_value *second) {
  qa_save_value *root = obj();
  set_number(root, "version", 1);
  qa_save_value *mods = qa_save_value_array();
  qa_save_value *entries[] = {first, second};
  for (size_t i = 0; i < 2; i++) {
    if (entries[i] == NULL) continue;
    qa_save_value *entry = obj();
    CHECK(qa_save_value_set(entry, "identity", entries[i]));
    qa_save_value *state = obj();
    CHECK(qa_save_value_set(state, "guests", qa_save_value_array()));
    qa_save_value *providers = qa_save_value_array();
    qa_save_value *provider = obj();
    set_string(provider, "provider", "mod:state");
    set_string(provider, "schema", "mod:checkpoint");
    set_number(provider, "version", 3);
    static const uint8_t bytes[] = {1, 2, 3};
    CHECK(qa_save_value_set(provider, "bytes", qa_save_value_bytes(bytes, 3)));
    CHECK(qa_save_value_push(providers, provider));
    CHECK(qa_save_value_set(state, "providers", providers));
    CHECK(qa_save_value_set(entry, "state", state));
    CHECK(qa_save_value_push(mods, entry));
  }
  CHECK(qa_save_value_set(root, "mods", mods));
  return root;
}

static qa_save_reader reader_of(const qa_save_value *value) {
  qa_save_reader reader;
  CHECK(qa_save_reader_init(&reader, value, "save"));
  return reader;
}

static void test_mods(void) {
  qa_save_value *tree = make_identity("q1", "id1");
  qa_save_reader reader = reader_of(tree);
  qa_mod_identity identity = qa_read_mod_identity(&reader);
  qa_save_reader_destroy(&reader);
  CHECK(text_is(identity.selection.product, "q1"));
  CHECK(text_is(identity.selection.id, "id1"));
  CHECK(text_is(identity.source.provider, "content:catalog"));
  CHECK(text_is(identity.source.content, "q1:classic:id1:1.06"));
  CHECK(text_is(identity.declaration_digest, DIGEST));
  CHECK(identity.module_count == 0);
  CHECK(identity.provider_count == 1);
  CHECK(text_is(identity.providers[0].provider, "mod:state"));
  CHECK(text_is(identity.providers[0].schema, "mod:checkpoint"));
  CHECK(identity.providers[0].version == 2);
  free((void *)identity.modules);
  free((void *)identity.providers);
  qa_save_value_free(tree);

  tree = make_session(make_identity("q1", "id1"), make_identity("q2", "baseq2"));
  reader = reader_of(tree);
  struct qa_mod_session_checkpoint session = qa_read_mod_session(&reader);
  qa_save_reader_destroy(&reader);
  CHECK(session.version == 1);
  CHECK(session.mod_count == 2);
  CHECK(text_is(session.mods[0].identity.selection.product, "q1"));
  CHECK(text_is(session.mods[1].identity.selection.id, "baseq2"));
  CHECK(session.mods[0].state.guest_count == 0);
  CHECK(session.mods[0].state.provider_count == 1);
  CHECK(session.mods[0].state.providers[0].version == 3);
  CHECK(session.mods[0].state.providers[0].bytes.length == 3);
  CHECK(session.mods[0].state.providers[0].bytes.bytes[2] == 3);
  for (size_t i = 0; i < session.mod_count; i++) {
    free((void *)session.mods[i].identity.modules);
    free((void *)session.mods[i].identity.providers);
    free((void *)session.mods[i].state.guests);
    free((void *)session.mods[i].state.providers);
  }
  free((void *)session.mods);
  qa_save_value_free(tree);
}

static void test_mods_errors(void) {
  qa_save_value *tree = make_identity("-bad", "id1");
  qa_save_reader reader = reader_of(tree);
  CHECK_ABORTS(qa_read_mod_identity(&reader)); /* invalid mod selection */
  qa_save_reader_destroy(&reader);
  qa_save_value_free(tree);

  tree = make_session(make_identity("q1", "id1"), make_identity("q1", "id1"));
  reader = reader_of(tree);
  CHECK_ABORTS(qa_read_mod_session(&reader)); /* duplicate saved mod selection */
  qa_save_reader_destroy(&reader);
  qa_save_value_free(tree);

  /* version literal(1) is read before the mods list. */
  qa_save_value *bad_root = obj();
  set_number(bad_root, "version", 2);
  reader = reader_of(bad_root);
  CHECK_ABORTS(qa_read_mod_session(&reader));
  qa_save_reader_destroy(&reader);
  qa_save_value_free(bad_root);
}

int main(void) {
  test_q1_decode();
  test_q1_roundtrip();
  test_q1_encode_text();
  test_q1_errors();
  test_mods();
  test_mods_errors();
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d checks passed\n", checks);
  return 0;
}
