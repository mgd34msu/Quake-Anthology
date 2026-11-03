/* The real Application (qa_application, application.ts) opened on retail data: dedicated Q1 id1 e1m1, Q2 baseq2 base1 and
 * Q3 baseq3 q3dm1 are opened as src/main.ts opens --dedicated (openApplication), stepped, closed, and compared line by line
 * with the TS Application under bun (tests/app/oracles/application_open_real.ts): console output, recipe, source kind,
 * frame count, live actors, players and the last snapshot's counts. One graphical application (SDL_VIDEODRIVER=offscreen,
 * a real GL context) is opened for Q1 e1m1 and stepped a few frames.
 *
 * The corpus is QTS_TEST_CORPUS (default ~/qfiles); the user content root is $TMPDIR/application-open-real-<case>, wiped
 * before each side runs. APP_OPEN_REAL_BUN=0 skips the bun comparison (the C transcript is still printed with -v).
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/app/bootstrap/application_class.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/options.h"
#include "qa/foundation/support.h"
#include "qa/world/actors/registry.h"
#include "../../src/app/bootstrap/application_internal.h"

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define FRAMES 60
#define FRAME_MILLISECONDS 50
static bool verbose;

/* A growing text buffer (the host's console). */
typedef struct text { char *bytes; size_t length, capacity; } text;
static void text_append(text *t, const char *value) {
  size_t n = strlen(value);
  if (t->length + n + 1 > t->capacity) {
    size_t capacity = t->capacity == 0 ? 4096 : t->capacity;
    while (capacity < t->length + n + 1) capacity *= 2;
    t->bytes = realloc(t->bytes, capacity);
    if (t->bytes == NULL) abort();
    t->capacity = capacity;
  }
  memcpy(t->bytes + t->length, value, n + 1);
  t->length += n;
}
static void text_appendf(text *t, const char *format, ...) {
  char line[4096];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  text_append(t, line);
}

/* The host's ctx is a sentinel: every callback checks it is the context it was given. */
typedef struct host_state { unsigned magic; text console; } host_state;
#define HOST_MAGIC 0x51a7e11u
static host_state *host_of(void *ctx) {
  host_state *state = ctx;
  if (state == NULL || state->magic != HOST_MAGIC) abort();
  return state;
}
static void host_print(void *ctx, const char *value) { text_append(&host_of(ctx)->console, value); }
static void host_stage(void *ctx, const char *message) {
  text_appendf(&host_of(ctx)->console, "[stage] %s\n", message);
}

static const char *corpus_root(void) {
  const char *root = getenv("QTS_TEST_CORPUS");
  if (root != NULL && root[0] != '\0') return root;
  static char home[512];
  snprintf(home, sizeof home, "%s/qfiles", getenv("HOME") != NULL ? getenv("HOME") : "");
  return home;
}
static void user_root(const char *name, char *out, size_t capacity) {
  const char *tmp = getenv("TMPDIR");
  snprintf(out, capacity, "%s/application-open-real-%s", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp", name);
}
static bool wipe(const char *path) {
  char command[1200];
  snprintf(command, sizeof command, "rm -rf '%s'", path);
  return system(command) == 0 && mkdir(path, 0777) == 0;
}

static const char *source_kind(qa_ss_source_kind kind) {
  switch (kind) {
    case QA_SS_SOURCE_LOADING: return "loading";
    case QA_SS_SOURCE_QUAKEC: return "quakec";
    case QA_SS_SOURCE_Q1: return "q1";
    case QA_SS_SOURCE_Q3: return "q3";
    case QA_SS_SOURCE_Q3_QVM: return "q3-qvm";
    case QA_SS_SOURCE_Q2_NATIVE: return "q2-native";
    case QA_SS_SOURCE_Q2: return "q2";
  }
  return "?";
}

typedef struct open_case { const char *name; const char *argv[8]; size_t argc; } open_case;
static const open_case cases[] = {
  {"q1", {"--game", "q1-classic-id1", "--map", "e1m1"}, 4},
  {"q2", {"--game", "q2-classic-baseq2", "--map", "base1"}, 4},
  {"q3", {"--game", "q3-baseq3", "--map", "q3dm1"}, 4},
};

/* The application's run, in the oracle's format: "console <line>" rows, then the summary rows. */
typedef struct open_run { const open_case *spec; bool dedicated; size_t frames; text out; host_state host; } open_run;
static void js_number(double value, char *out, size_t capacity) {
  if (value == (double)(long long)value) snprintf(out, capacity, "%lld", (long long)value);
  else snprintf(out, capacity, "%.17g", value);
}
static void run_body(void *ctx) {
  open_run *run = ctx;
  char root[1024];
  user_root(run->spec->name, root, sizeof root);
  if (!wipe(root)) { perror(root); abort(); }
  const char *argv[16];
  size_t argc = 0;
  argv[argc++] = "--content-root"; argv[argc++] = corpus_root();
  argv[argc++] = "--user-content-root"; argv[argc++] = root;
  for (size_t i = 0; i < run->spec->argc; i++) argv[argc++] = run->spec->argv[i];
  if (run->dedicated) argv[argc++] = "--dedicated";
  else argv[argc++] = "--hidden";
  qa_content_root_search search = {corpus_root(), NULL};
  qa_app_application_command command = qa_app_parse_application_command(argv, argc, search, NULL);
  if (strcmp(command.kind, "run") != 0) { fprintf(stderr, "%s: expected a run command\n", run->spec->name); abort(); }
  qa_application_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &run->host;
  host.print = host_print;
  qa_application_loading loading;
  memset(&loading, 0, sizeof loading);
  if (!run->dedicated) { /* the graphical run reports its loading stages (sentinel ctx) */
    loading.ctx = &run->host;
    loading.stage = host_stage;
    host.loading = &loading;
  }
  qa_application *app = qa_application_open(command.options, host, NULL, NULL, NULL);
  CHECK(app != NULL);
  char snapshot[256] = "";
  for (size_t frame = 0; frame < run->frames; frame++) {
    qa_simulation_output output = qa_application_step(app, FRAME_MILLISECONDS);
    snprintf(snapshot, sizeof snapshot, "snapshot frame %lld actors %zu bodies %zu inventories %zu events %zu",
        (long long)output.snapshot.frame.frame, output.snapshot.actor_count, output.snapshot.body_count,
        output.snapshot.inventory_count, output.event_count);
  }
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(qa_application_content(app));
  qa_app_shared_simulation *simulation = qa_application_simulation(app);
  text summary = {0};
  text_appendf(&summary, "recipe %s %s %s %s %s\n", recipe->engine_behavior.content, recipe->map.geometry.requested_path,
      recipe->map.entities.provider, recipe->movement.provider, recipe->character.appearance.provider);
  text_appendf(&summary, "source %s\n", source_kind(simulation->source.kind));
  char frames[40];
  js_number(qa_application_frame_count(app), frames, sizeof frames);
  text_appendf(&summary, "frames %s finished %s\n", frames, qa_application_finished(app) ? "true" : "false");
  size_t actors = 0, players = 0;
  (void)qa_session_actor_registry_observations(qa_app_shared_sim_actors(simulation), &actors);
  (void)qa_app_shared_sim_players(simulation, &players);
  text_appendf(&summary, "actors %zu\nplayers %zu\n%s\n", actors, players, snapshot);
  if (!run->dedicated) {
    /* GraphicalApplication: the renderer, input, audio and one seat presentation per local seat. */
    CHECK(app->graphical != NULL);
    if (app->graphical != NULL) {
      CHECK(app->graphical->renderer != NULL && app->graphical->input != NULL && app->graphical->audio != NULL);
      CHECK(app->graphical->presentation_count == 1);
      CHECK(app->capture != NULL && app->owned_video_restart != NULL);
      text_appendf(&summary, "graphical presentations %zu q3 %zu native-q2 %zu\n", app->graphical->presentation_count,
          app->graphical->q3_count, app->graphical->native_q2_count);
    }
  }
  qa_application_close(app);
  qa_application_destroy(app);
  qa_app_application_command_destroy(&command);
  /* console rows: the printed text split on "\n" (a trailing empty row dropped), then the summary. */
  const char *cursor = run->host.console.bytes != NULL ? run->host.console.bytes : "";
  while (*cursor != '\0') {
    const char *end = strchr(cursor, '\n');
    size_t length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
    text_append(&run->out, "console ");
    char *line = strndup(cursor, length);
    text_append(&run->out, line);
    free(line);
    text_append(&run->out, "\n");
    cursor += length + (end != NULL ? 1 : 0);
  }
  text_append(&run->out, summary.bytes);
  free(summary.bytes);
}

/* Runs one side under qa_try: a raised error fails the case with its message instead of aborting the binary. */
static bool run_case(open_run *run) {
  memset(&run->host, 0, sizeof run->host);
  run->host.magic = HOST_MAGIC;
  qa_caught_error caught;
  if (qa_try(run_body, run, &caught)) return true;
  fprintf(stderr, "FAIL %s: %s: %s\n", run->spec->name, caught.name, caught.message);
  failures++;
  return false;
}

/* The bun oracle's transcript for a case (NULL when bun is unavailable or disabled). */
static char *oracle(const open_case *spec) {
  const char *disabled = getenv("APP_OPEN_REAL_BUN");
  if (disabled != NULL && strcmp(disabled, "0") == 0) return NULL;
  char root[1024], command[4096], here[1024];
  user_root(spec->name, root, sizeof root);
  if (getcwd(here, sizeof here) == NULL) return NULL;
  snprintf(command, sizeof command,
      "cd '%s/../quake-anthology-ts' && bun '%s/tests/app/oracles/application_open_real.ts' %s '%s' '%s' %d %d 2>/dev/null",
      here, here, spec->name, corpus_root(), root, FRAMES, FRAME_MILLISECONDS);
  FILE *pipe = popen(command, "r");
  if (pipe == NULL) return NULL;
  text out = {0};
  char buffer[4096];
  size_t n;
  while ((n = fread(buffer, 1, sizeof buffer - 1, pipe)) > 0) { buffer[n] = '\0'; text_append(&out, buffer); }
  int status = pclose(pipe);
  if (status != 0 || out.bytes == NULL) {
    fprintf(stderr, "%s: bun oracle failed (status %d)\n", spec->name, status);
    free(out.bytes);
    return NULL;
  }
  return out.bytes;
}
/* Line by line: reports the first differing lines (up to 20) and the line counts. */
static bool compare(const char *name, const char *c, const char *ts) {
  size_t differences = 0, line = 0;
  const char *a = c, *b = ts;
  while (*a != '\0' || *b != '\0') {
    const char *ea = strchr(a, '\n'), *eb = strchr(b, '\n');
    size_t la = ea == NULL ? strlen(a) : (size_t)(ea - a), lb = eb == NULL ? strlen(b) : (size_t)(eb - b);
    line++;
    if (la != lb || memcmp(a, b, la) != 0) {
      if (differences < 20) fprintf(stderr, "%s line %zu\n  C : %.*s\n  TS: %.*s\n", name, line, (int)la, a, (int)lb, b);
      differences++;
    }
    a += la + (ea != NULL ? 1 : 0);
    b += lb + (eb != NULL ? 1 : 0);
  }
  printf("%s: %zu lines, %zu differing\n", name, line, differences);
  return differences == 0;
}

int main(int argc, char **argv) {
  verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
  const char *only = argc > 1 && argv[argc - 1][0] != '-' ? argv[argc - 1] : NULL;
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    const open_case *spec = &cases[i];
    if (only != NULL && strcmp(only, spec->name) != 0 && strcmp(only, "graphical") != 0) continue;
    if (only != NULL && strcmp(only, "graphical") == 0) break;
    open_run run;
    memset(&run, 0, sizeof run);
    run.spec = spec;
    run.dedicated = true;
    run.frames = FRAMES;
    bool ran = run_case(&run);
    if (verbose && run.out.bytes != NULL) printf("---- C %s ----\n%s", spec->name, run.out.bytes);
    char *expected = ran ? oracle(spec) : NULL;
    if (expected != NULL) {
      if (verbose) printf("---- TS %s ----\n%s", spec->name, expected);
      CHECK(compare(spec->name, run.out.bytes, expected));
    }
    free(expected);
    free(run.out.bytes);
    free(run.host.console.bytes);
  }
  if (only == NULL || strcmp(only, "graphical") == 0) {
    /* One graphical application: Q1 e1m1 on the offscreen SDL video driver (a real GL context), a few frames. */
    setenv("SDL_VIDEODRIVER", "offscreen", 0); /* an explicit SDL_VIDEODRIVER wins */
    if (getenv("SDL_AUDIODRIVER") == NULL) setenv("SDL_AUDIODRIVER", "dummy", 1);
    open_run run;
    memset(&run, 0, sizeof run);
    run.spec = &cases[0];
    run.dedicated = false;
    run.frames = 5;
    bool ran = run_case(&run);
    CHECK(ran);
    if (run.out.bytes != NULL) printf("---- C graphical %s ----\n%s", run.spec->name, run.out.bytes);
    free(run.out.bytes);
    free(run.host.console.bytes);
  }
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
