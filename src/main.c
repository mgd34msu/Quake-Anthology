/*
 * Port of src/main.ts. Command dispatch runs through the landed
 * options parser: --version/--help/--list-content and
 * --splash-child are fully wired. run/menu compose real phase
 * hosts for StartupApplication (graphical) and Application
 * (dedicated) over the landed catalog/settings/SDL/audio/input
 * services; composition steps whose TS service has only a
 * host-seam C port fail with a named missing-service error
 * (crash.log + exit 1, as in TS main's catch) instead of a stub.
 * GPL-2.0-or-later.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "qa/app/bootstrap/application.h"
#include "qa/app/bootstrap/frontend_preferences.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/splash.h"
#include "qa/app/bootstrap/startup.h"
#include "qa/app/bootstrap/startup_timing.h"
#include "qa/app/bootstrap/weapon_behavior_tool.h"
#include "qa/audio/engine.h"
#include "qa/content/archive.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/content/user_data.h"
#include "qa/foundation/bounded_queue.h"
#include "qa/foundation/support.h"
#include "qa/formats/images/png.h"
#include "qa/llm/settings.h"
#include "qa/platform/controller.h"
#include "qa/platform/sdl.h"
#include "qa/settings/config.h"
#include "qa/version.h"

/* ---- small helpers ---- */

static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (copy != NULL) memcpy(copy, text, length + 1);
  return copy;
}

/* Malloc'd "a/b"; either side may be empty. */
static char *join_path(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  bool slash = la > 0 && a[la - 1] != '/';
  char *out = malloc(la + (slash ? 1 : 0) + lb + 1);
  if (out == NULL) return NULL;
  memcpy(out, a, la);
  size_t at = la;
  if (slash) out[at++] = '/';
  memcpy(out + at, b, lb + 1);
  return out;
}

/* Malloc'd dirname (node:path dirname, trailing slashes ignored). */
static char *dir_of(const char *path) {
  size_t length = strlen(path);
  while (length > 1 && path[length - 1] == '/') length--;
  size_t slash = length;
  while (slash > 0 && path[slash - 1] != '/') slash--;
  if (slash == 0) return dup_text(".");
  if (slash == 1) return dup_text("/");
  char *out = malloc(slash);
  if (out == NULL) return NULL;
  memcpy(out, path, slash - 1);
  out[slash - 1] = '\0';
  return out;
}

static bool mkdir_p(const char *path) {
  char *copy = dup_text(path);
  if (copy == NULL) return false;
  for (char *p = copy + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      (void)mkdir(copy, 0777);
      *p = '/';
    }
  }
  bool ok = mkdir(copy, 0777) == 0 || errno == EEXIST;
  free(copy);
  return ok;
}

/* Malloc'd directory of the running executable (best effort). */
static char *exe_directory(void) {
  char buffer[4096];
  ssize_t length = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
  if (length <= 0) return dup_text(".");
  buffer[length] = '\0';
  return dir_of(buffer);
}

/* ---- recent output + crash log (TS writeCrashLog) ---- */

static qa_bounded_queue recent_output; /* char* chunks, capacity 256 */

static void output_push(const char *text) {
  char *copy = dup_text(text);
  if (copy == NULL) return;
  if (recent_output.count == recent_output.capacity) {
    const char *const *oldest = qa_bounded_queue_peek_oldest(&recent_output);
    if (oldest != NULL) free((void *)*oldest);
  }
  qa_bounded_queue_push(&recent_output, &copy);
}

/* TS host.print: application output collector. The composition
 * phases that produce output land in reconciliation; until then
 * the queue stays empty and crash logs carry an empty tail. */
static void print_text(const char *text) {
  output_push(text);
  fputs(text, stdout);
  fflush(stdout);
}

static void append_json_string(FILE *file, const char *text) {
  fputc('"', file);
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    switch (*p) {
      case '"': fputs("\\\"", file); break;
      case '\\': fputs("\\\\", file); break;
      case '\n': fputs("\\n", file); break;
      case '\r': fputs("\\r", file); break;
      case '\t': fputs("\\t", file); break;
      default:
        if (*p < 0x20) fprintf(file, "\\u%04x", *p);
        else fputc(*p, file);
    }
  }
  fputc('"', file);
}

static void write_crash_log(const char *message, int argc, char **argv) {
  char *user_root = qa_default_user_content_root();
  char *directory = user_root != NULL ? dir_of(user_root) : NULL;
  if (directory == NULL) { free(user_root); return; }
  (void)mkdir_p(directory);
  char *path = join_path(directory, "crash.log");
  FILE *file = path != NULL ? fopen(path, "a") : NULL;
  if (file != NULL) {
    struct timespec now;
    (void)clock_gettime(CLOCK_REALTIME, &now);
    struct tm utc;
    char stamp[40];
    if (gmtime_r(&now.tv_sec, &utc) != NULL) {
      size_t used = strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", &utc);
      (void)snprintf(stamp + used, sizeof stamp - used, ".%03ldZ",
                     now.tv_nsec / 1000000L);
    } else stamp[0] = '\0';
    fprintf(file, "\n=== Quake Anthology %s crash %s argv=[", qa_version, stamp);
    for (int i = 0; i < argc; i++) {
      if (i > 0) fputc(',', file);
      append_json_string(file, argv[i]);
    }
    fprintf(file, "]\n%s\n--- recent output ---\n", message);
    /* Tail of the bounded queue, last 32768 chars like TS. */
    size_t count = recent_output.count, total = 0;
    for (size_t i = 0; i < count; i++) {
      const char *const *chunk = qa_bounded_queue_at(&recent_output, i);
      if (chunk != NULL) total += strlen(*chunk);
    }
    size_t skip = total > 32768 ? total - 32768 : 0;
    for (size_t i = 0; i < count; i++) {
      const char *const *chunk = qa_bounded_queue_at(&recent_output, i);
      if (chunk == NULL) continue;
      size_t length = strlen(*chunk);
      if (skip >= length) { skip -= length; continue; }
      fputs(*chunk + skip, file);
      skip = 0;
    }
    fputc('\n', file);
    (void)fclose(file);
  }
  free(path);
  free(directory);
  free(user_root);
}

/* TS llmSettingsDirectory: dirname(defaultUserContentRoot())/settings. */
static char *llm_settings_directory(void) {
  char *user_root = qa_default_user_content_root();
  if (user_root == NULL) return NULL;
  char *parent = dir_of(user_root);
  free(user_root);
  if (parent == NULL) return NULL;
  char *out = join_path(parent, "settings");
  free(parent);
  return out;
}

/* copyFileSync with COPYFILE_EXCL; failures ignored by the caller. */
static void copy_file_exclusive(const char *from, const char *to) {
  int source = open(from, O_RDONLY);
  if (source < 0) return;
  int target = open(to, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (target < 0) { (void)close(source); return; }
  char buffer[8192];
  for (;;) {
    ssize_t read_count = read(source, buffer, sizeof buffer);
    if (read_count <= 0) break;
    ssize_t written = 0;
    while (written < read_count) {
      ssize_t step = write(target, buffer + written,
                           (size_t)(read_count - written));
      if (step <= 0) break;
      written += step;
    }
  }
  (void)close(source);
  (void)close(target);
}

static void migrate_legacy_llm_settings(const char *llm_directory) {
  char cwd[4096];
  const char *legacy = getcwd(cwd, sizeof cwd) != NULL ? cwd : ".";
  if (strcmp(legacy, llm_directory) == 0) return;
  if (!mkdir_p(llm_directory)) return;
  static const char *const names[] = {"llm.json", "chatgpt.key", "other.key",
                                      "other.service"};
  for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
    char *from = join_path(legacy, names[i]);
    char *to = join_path(llm_directory, names[i]);
    if (from != NULL && to != NULL) copy_file_exclusive(from, to);
    free(from);
    free(to);
  }
}

/* ---- splash host (SDL window owned by this process) ---- */

typedef struct splash_window {
  qa_sdl_window window;
  bool open;
  uint8_t *frame;
  size_t frame_length;
  bool stdin_closed;
} splash_window;

static bool splash_load_title(void *ctx, qa_png_image *out) {
  (void)ctx;
  char *exe_dir = exe_directory();
  char *candidates[3];
  candidates[0] = exe_dir != NULL ? join_path(exe_dir, "assets/ui/title.png") : NULL;
  candidates[1] = dup_text("assets/ui/title.png");
  candidates[2] = dup_text("../quake-anthology-ts/assets/ui/title.png");
  bool loaded = false;
  for (int i = 0; i < 3 && !loaded; i++) {
    if (candidates[i] == NULL) continue;
    FILE *file = fopen(candidates[i], "rb");
    if (file == NULL) continue;
    (void)fseek(file, 0, SEEK_END);
    long size = ftell(file);
    (void)fseek(file, 0, SEEK_SET);
    if (size > 0) {
      uint8_t *bytes = malloc((size_t)size);
      if (bytes != NULL && fread(bytes, 1, (size_t)size, file) == (size_t)size)
        loaded = qa_decode_png(bytes, (size_t)size, "assets/ui/title.png", out) != 0;
      free(bytes);
    }
    (void)fclose(file);
  }
  for (int i = 0; i < 3; i++) free(candidates[i]);
  free(exe_dir);
  return loaded;
}

static bool splash_open_window(void *ctx, double width, double height,
                               const uint8_t *rgba, size_t length) {
  splash_window *splash = ctx;
  qa_sdl_window_options options = {0};
  options.title = "Quake Anthology";
  options.width = width;
  options.height = height;
  options.backend = QA_SDL_BACKEND_CPU;
  if (!qa_sdl_window_open(&options, &splash->window)) return false;
  splash->open = true;
  free(splash->frame);
  splash->frame = NULL;
  splash->frame_length = 0;
  if (rgba != NULL && length > 0) {
    splash->frame = malloc(length);
    if (splash->frame != NULL) {
      memcpy(splash->frame, rgba, length);
      splash->frame_length = length;
    }
  }
  return true;
}

static bool splash_poll_quit(void *ctx) {
  splash_window *splash = ctx;
  if (splash->stdin_closed) return true;
  struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
  if (poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
    char buffer[256];
    ssize_t got = read(STDIN_FILENO, buffer, sizeof buffer);
    if (got <= 0) splash->stdin_closed = true;
  }
  if (splash->stdin_closed) return true;
  size_t count = 0;
  qa_sdl_event *events = qa_sdl_window_poll_events(&splash->window, &count);
  bool quit = false;
  for (size_t i = 0; i < count; i++)
    if (events[i].kind == QA_SDL_EVENT_QUIT) quit = true;
  free(events);
  return quit || qa_sdl_window_closed(&splash->window);
}

static void splash_present(void *ctx) {
  splash_window *splash = ctx;
  if (splash->frame != NULL)
    qa_sdl_window_present(&splash->window, splash->frame, splash->frame_length);
}

static void splash_close_window(void *ctx) {
  splash_window *splash = ctx;
  if (!splash->open) return;
  splash->open = false;
  qa_sdl_window_close(&splash->window);
  free(splash->frame);
  splash->frame = NULL;
  splash->frame_length = 0;
}

static void splash_sleep(void *ctx, double ms) {
  (void)ctx;
  if (ms <= 0) return;
  struct timespec delay = {(time_t)(ms / 1000.0),
                           (long)((ms - (double)(time_t)(ms / 1000.0) * 1000.0) * 1000000.0)};
  (void)nanosleep(&delay, NULL);
}

static void splash_write_stdout(void *ctx, const char *text) {
  (void)ctx;
  fputs(text, stdout);
  fflush(stdout);
}

static double splash_now_ms(void *ctx) {
  (void)ctx;
  struct timespec now;
  (void)clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

static qa_app_splash_host splash_host_for(splash_window *window) {
  qa_app_splash_host host = {0};
  host.ctx = window;
  host.open_window = splash_open_window;
  host.poll_quit = splash_poll_quit;
  host.present = splash_present;
  host.close_window = splash_close_window;
  host.sleep_ms = splash_sleep;
  host.write_stdout = splash_write_stdout;
  host.now_ms = splash_now_ms;
  host.load_title_image = splash_load_title;
  return host;
}

/* Parent-side splash: fork/exec self as --splash-child (TS Bun.spawn). */

typedef struct splash_child {
  pid_t pid;
  int stdin_fd;
} splash_child;

static bool splash_child_open(void *ctx, double width, double height,
                              const uint8_t *rgba, size_t length) {
  (void)rgba; (void)length;
  splash_child *child = ctx;
  int fds[2];
  if (pipe(fds) != 0) return false;
  pid_t pid = fork();
  if (pid < 0) { (void)close(fds[0]); (void)close(fds[1]); return false; }
  if (pid == 0) {
    (void)dup2(fds[0], STDIN_FILENO);
    (void)close(fds[0]);
    (void)close(fds[1]);
    (void)setenv("QTS_SPLASH_CHILD", "1", 1);
    char w[32], h[32];
    (void)snprintf(w, sizeof w, "%.0f", width);
    (void)snprintf(h, sizeof h, "%.0f", height);
    execl("/proc/self/exe", "quake-anthology", "--splash-child", w, h,
          (char *)NULL);
    _exit(127);
  }
  (void)close(fds[0]);
  child->pid = pid;
  child->stdin_fd = fds[1];
  return true;
}

static void splash_child_close(void *ctx) {
  splash_child *child = ctx;
  if (child->stdin_fd >= 0) (void)close(child->stdin_fd);
  child->stdin_fd = -1;
  if (child->pid > 0) (void)waitpid(child->pid, NULL, 0);
  child->pid = -1;
}

static bool splash_child_true(void *ctx) { (void)ctx; return true; }
static void splash_child_void(void *ctx) { (void)ctx; }
static void splash_child_sleep(void *ctx, double ms) { splash_sleep(ctx, ms); }
static bool splash_child_no_image(void *ctx, qa_png_image *out) {
  (void)ctx; (void)out; return false;
}

/* ---- catalog archive inspector (TS openArchive) ---- */

static bool inspect_archive(void *ctx, const char *path,
                            qa_archive_format format,
                            qa_catalog_archive *out, char **error_out) {
  (void)ctx;
  memset(out, 0, sizeof *out);
  if (error_out != NULL) *error_out = NULL;
  qa_archive_handle *handle = qa_open_archive(path, true, format);
  if (handle == NULL) {
    if (error_out != NULL) *error_out = dup_text("archive could not be opened");
    return false;
  }
  out->path = dup_text(path);
  out->format = handle->format;
  out->entry_count = handle->entry_count;
  if (handle->entry_count > 0) {
    out->entries = calloc(handle->entry_count, sizeof *out->entries);
    if (out->entries == NULL || out->path == NULL) {
      qa_catalog_archive_free(out);
      qa_archive_destroy(handle);
      return false;
    }
    for (size_t i = 0; i < handle->entry_count; i++) {
      out->entries[i].path = dup_text(handle->entries[i].path);
      out->entries[i].ordinal = handle->entries[i].ordinal;
      out->entries[i].byte_length = handle->entries[i].byte_length;
    }
  }
  qa_archive_destroy(handle);
  return true;
}

/* ---- options host ---- */

static void weapon_behavior_box_destroy(void *command) {
  qa_app_weapon_behavior_tool_command_free(
      (qa_app_weapon_behavior_tool_command *)command);
  free(command);
}

static void *parse_weapon_behavior(void *ctx, const char *const *argv,
                                   size_t argc, void (**destroy)(void *)) {
  (void)ctx;
  qa_app_weapon_behavior_tool_command *boxed = malloc(sizeof *boxed);
  if (boxed == NULL) return NULL;
  *boxed = qa_app_parse_weapon_behavior_tool(argv, argc);
  *destroy = weapon_behavior_box_destroy;
  return boxed;
}

static char *resolve_path_keep(void *ctx, const char *path) {
  (void)ctx; (void)path;
  return NULL; /* keep argument text (options.h contract) */
}

/* ---- list-content ---- */

static const char *availability_name(qa_catalog_availability_kind kind) {
  switch (kind) {
    case QA_CATALOG_INSTALLED: return "installed";
    case QA_CATALOG_MISSING: return "missing";
    case QA_CATALOG_UNRESOLVED: return "unresolved";
  }
  return "unresolved";
}

static int run_list_content(const char *corpus_root) {
  char *user_root = qa_default_user_content_root();
  qa_nightdive_addon_root *addons = NULL;
  size_t addon_count = 0;
  (void)qa_default_addon_content_roots(NULL, &addons, &addon_count);
  qa_discover_content_options options = {0};
  options.corpus_root = corpus_root;
  options.user_content_root = user_root;
  options.addon_content_roots =
      (const qa_catalog_nightdive_addon_root *)addons;
  options.addon_content_root_count = addon_count;
  options.discover_mods = true;
  options.inspector.inspect = inspect_archive;
  qa_content_catalog catalog = {0};
  int result = 1;
  if (qa_discover_installed_content(&options, &catalog)) {
    for (size_t i = 0; i < catalog.product_count; i++) {
      const qa_catalog_discovered_product *product = &catalog.products[i];
      printf("%s\t%s\t%s\n", product->expectation.id,
             availability_name(product->availability.kind),
             product->expectation.title);
    }
    result = 0;
  }
  qa_content_catalog_destroy(&catalog);
  qa_nightdive_addon_roots_free(addons, addon_count);
  free(user_root);
  return result;
}

/* ---- application composition (TS main.ts run/menu/dedicated) ----
 *
 * TS main opens StartupApplication (graphical) or Application
 * (dedicated), closes the splash, wires SIGINT/SIGTERM to
 * requestQuit, runs until finished, then closes the application
 * and finally the LLM settings. The landed startup.c/application.c
 * abort on a returned phase error, which would bypass that catch
 * (crash.log, ordered close, exit 1), so phases below record the
 * first error in the composition and return NULL; main fails on
 * the recorded error after open returns and never pumps a failed
 * composition. Phases over concrete landed services are real;
 * the phase that needs a host-seam-only TS service records a
 * named missing-service error. */

typedef enum composition_kind {
  COMPOSITION_STARTUP,
  COMPOSITION_DEDICATED
} composition_kind;

/* TS main.ts: dedicated ? openApplication : StartupApplication.open. */
static composition_kind composition_kind_for(bool dedicated) {
  return dedicated ? COMPOSITION_DEDICATED : COMPOSITION_STARTUP;
}

/* TS StartupApplication.open entry argument ("run" | "menu"). */
static const char *composition_entry_for(const char *kind) {
  return kind != NULL && strcmp(kind, "run") == 0 ? "run" : "menu";
}

/* TS main.ts: LLM settings open unless a dedicated non-menu run. */
static bool composition_llm_enabled(const char *kind, bool dedicated) {
  return !(strcmp(kind, "menu") != 0 && dedicated);
}

/* TS run() loop guard: not stopping, not finished, under frameLimit. */
static bool composition_should_continue(bool quit_requested, bool finished,
                                        bool has_frame_limit, double frame_limit,
                                        double frames) {
  if (quit_requested || finished) return false;
  return !has_frame_limit || frames < frame_limit;
}

/* Non-aborting qa_content_catalog_product match (id or expectation id). */
static const qa_catalog_discovered_product *composition_find_product(
    const qa_content_catalog *catalog, const char *id) {
  if (catalog == NULL || id == NULL) return NULL;
  for (size_t i = 0; i < catalog->product_count; i++)
    if (strcmp(catalog->products[i].id, id) == 0 ||
        strcmp(catalog->products[i].expectation.id, id) == 0)
      return &catalog->products[i];
  return NULL;
}

static const qa_catalog_discovered_product *composition_first_installed(
    const qa_content_catalog *catalog) {
  for (size_t i = 0; i < catalog->product_count; i++)
    if (catalog->products[i].availability.kind == QA_CATALOG_INSTALLED)
      return &catalog->products[i];
  return NULL;
}

/* Malloc'd lowercase hex of the text bytes (TS Buffer hex). */
static char *hex_encode(const char *text) {
  static const char digits[] = "0123456789abcdef";
  size_t length = strlen(text);
  char *out = malloc(length * 2 + 1);
  if (out == NULL) return NULL;
  for (size_t i = 0; i < length; i++) {
    out[i * 2] = digits[((unsigned char)text[i] >> 4) & 0xf];
    out[i * 2 + 1] = digits[(unsigned char)text[i] & 0xf];
  }
  out[length * 2] = '\0';
  return out;
}

typedef struct composition {
  const qa_app_options *options; /* borrowed; strings owned by the app */
  const char *kind;              /* "run" | "menu" */
  char *user_root;               /* owned default root, when substituted */
  char *save_directory;          /* owned */
  char *settings_directory;      /* owned */
  qa_nightdive_addon_root *addons; /* owned default roots, when substituted */
  size_t addon_count;
  qa_content_catalog catalog;
  bool catalog_open;
  const qa_catalog_discovered_product *selected;  /* into catalog */
  const qa_catalog_discovered_product *installed; /* into catalog */
  qa_config_store settings;
  bool settings_open;
  qa_global_input_tuning tuning; /* retained shared-settings baseline */
  bool has_tuning;
  qa_app_frontend_preferences *preferences;
  const char *renderer_kind; /* "cpu" | "gl" */
  qa_sdl_window window;
  bool window_open;
  qa_unified_audio *audio;
  qa_sdl_controllers *controllers;
  bool quit_requested;
  double frames;
  uint32_t random_state;
  char *error; /* owned first phase error; freed by main, not close */
} composition;

static void composition_set_error(composition *comp, const char *format, ...) {
  if (comp->error != NULL) return;
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length >= 0) {
    comp->error = malloc((size_t)length + 1);
    if (comp->error != NULL)
      (void)vsnprintf(comp->error, (size_t)length + 1, format, args);
  }
  va_end(args);
  if (comp->error == NULL) comp->error = dup_text("Out of memory");
}

static const char composition_missing_menu[] =
    "Graphical startup composition is not yet wired: "
    "prepareInitialConfiguration/openInitialConfigurationContent "
    "(src/app/bootstrap/configuration.ts), ClientBootstrap "
    "(src/app/bootstrap/client-bootstrap.ts), StartupMenu "
    "(src/app/bootstrap/startup-menu.ts), StartupAudio "
    "(src/app/bootstrap/startup-audio.ts) and the NativeRenderer "
    "backend bundle factory (src/app/bootstrap/renderer.ts) have "
    "no concrete C hosts";
static const char composition_missing_game[] =
    "Application composition is not yet wired: "
    "loadApplicationContent and the Application.open "
    "world/session/simulation composition "
    "(src/app/bootstrap/application.ts, src/app/bootstrap/content.ts) "
    "have no concrete C hosts";
static const char composition_missing_lobby[] =
    "Local lobby composition is not yet wired: ApplicationLocalLobby "
    "(src/app/bootstrap/local-lobby.ts) over LocalLobbyService "
    "(src/network/services/online.ts) has no concrete C host";

/* TS createStartupSelection: discoverInstalledContent over options. */
static void composition_discover(composition *comp) {
  if (comp->error != NULL || comp->catalog_open) return;
  const char *user_root = comp->options->user_content_root;
  if (user_root == NULL) {
    comp->user_root = qa_default_user_content_root();
    user_root = comp->user_root;
  }
  const qa_catalog_nightdive_addon_root *addons =
      (const qa_catalog_nightdive_addon_root *)comp->options->addon_content_roots;
  size_t addon_count = comp->options->addon_content_root_count;
  if (addon_count == 0) {
    (void)qa_default_addon_content_roots(NULL, &comp->addons, &comp->addon_count);
    addons = (const qa_catalog_nightdive_addon_root *)comp->addons;
    addon_count = comp->addon_count;
  }
  qa_discover_content_options options = {0};
  options.corpus_root = comp->options->corpus_root;
  options.user_content_root = user_root;
  options.addon_content_roots = addons;
  options.addon_content_root_count = addon_count;
  options.discover_mods = true;
  options.inspector.inspect = inspect_archive;
  if (!qa_discover_installed_content(&options, &comp->catalog)) {
    composition_set_error(comp, "Out of memory");
    return;
  }
  comp->catalog_open = true;
}

static qa_command_dialect composition_dialect(void *ctx) {
  composition *comp = ctx;
  return qa_app_configuration_dialect_for(comp->options->movement, NULL);
}

/* Shared-settings part of TS FrontendPreferences.loadBaseline. */
static const char *composition_load_baseline(void *ctx,
                                             qa_app_frontend_preferences *prefs) {
  composition *comp = ctx;
  qa_global_input_tuning tuning;
  if (!qa_config_store_load_global_tuning(&comp->settings,
                                          "input/global-tuning.json", &tuning))
    return NULL;
  comp->tuning = tuning;
  comp->has_tuning = true;
  qa_app_frontend_mouse mouse = {0};
  mouse.sensitivity = tuning.mouse.sensitivity;
  mouse.pitch = tuning.mouse.pitch;
  mouse.yaw = tuning.mouse.yaw;
  mouse.invert_pitch = tuning.mouse.invert_pitch;
  mouse.acceleration = tuning.mouse.acceleration != 0;
  mouse.filter = tuning.mouse.filter;
  mouse.has_look_spring = true;
  mouse.look_spring = tuning.mouse.look_spring;
  mouse.has_look_strafe = true;
  mouse.look_strafe = tuning.mouse.look_strafe;
  mouse.free_look = tuning.mouse.free_look;
  qa_app_frontend_preferences_set_baselines(prefs, mouse, tuning.has_always_run,
                                            tuning.always_run, tuning.rumble,
                                            tuning.rumble_strength);
  return NULL;
}

/* TS FrontendPreferences.saveGlobal over the shared ConfigStore. */
static const char *composition_save_global(void *ctx,
                                           qa_app_frontend_preferences *prefs) {
  composition *comp = ctx;
  qa_global_input_tuning tuning = comp->has_tuning ? comp->tuning
                                                   : (qa_global_input_tuning){0};
  tuning.version = 1;
  if (!comp->has_tuning) tuning.mouse = qa_default_mouse_tuning;
  qa_app_frontend_preference_overrides effective =
      qa_app_frontend_preferences_effective_values(prefs);
  tuning.mouse.sensitivity = effective.sensitivity;
  tuning.mouse.pitch = effective.pitch;
  tuning.mouse.yaw = effective.yaw;
  tuning.mouse.invert_pitch = effective.invert_mouse;
  tuning.mouse.acceleration = effective.acceleration ? 1 : 0;
  tuning.mouse.filter = effective.filter;
  tuning.mouse.look_spring = effective.look_spring;
  tuning.mouse.look_strafe = effective.look_strafe;
  tuning.mouse.free_look = effective.free_look;
  if (effective.has_always_run) {
    tuning.has_always_run = true;
    tuning.always_run = effective.always_run;
  }
  tuning.rumble = effective.controller_vibration;
  tuning.rumble_strength = effective.controller_vibration_strength;
  if (!qa_config_store_save_global_tuning(&comp->settings,
                                          "input/global-tuning.json", &tuning))
    return "global input tuning could not be saved";
  comp->tuning = tuning;
  comp->has_tuning = true;
  return NULL;
}

/* FrontendPreferences + renderer text preference (TS openGraphics). */
static void composition_load_preferences(composition *comp) {
  if (comp->error != NULL) return;
  comp->settings_directory = join_path(comp->save_directory, "../settings");
  if (comp->settings_directory == NULL ||
      !qa_config_store_init(&comp->settings, comp->settings_directory)) {
    composition_set_error(comp, "Out of memory");
    return;
  }
  comp->settings_open = true;
  qa_app_frontend_preferences_host host = {0};
  host.ctx = comp;
  host.dialect = composition_dialect;
  host.has_shared_settings = true;
  host.load_baseline = composition_load_baseline;
  host.save_global = composition_save_global;
  comp->preferences = qa_app_frontend_preferences_create(host);
  if (comp->preferences == NULL) {
    composition_set_error(comp, "Out of memory");
    return;
  }
  (void)qa_app_frontend_preferences_load_baseline(comp->preferences);
  comp->renderer_kind = comp->options->renderer;
  if (comp->options->renderer_selection != NULL &&
      strcmp(comp->options->renderer_selection, "default") == 0) {
    size_t length = 0;
    char *text = qa_config_store_load_text(&comp->settings, "renderer", &length);
    if (text != NULL) {
      while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r' ||
                            text[length - 1] == ' ' || text[length - 1] == '\t'))
        text[--length] = '\0';
      if (strcmp(text, "cpu") == 0 || strcmp(text, "gl") == 0)
        comp->renderer_kind = strcmp(text, "cpu") == 0 ? "cpu" : "gl";
      free(text);
    }
  }
}

/* Product selection from options. require_installed mirrors
 * catalog.require; otherwise the startup menu may select a
 * missing product but still needs one installed charset product
 * (TS openGraphics). */
static void composition_select_product(composition *comp, bool require_installed) {
  if (comp->error != NULL) return;
  comp->selected = composition_find_product(&comp->catalog, comp->options->product);
  if (comp->selected == NULL) {
    composition_set_error(comp, "Unknown requested content or mod: %s",
                          comp->options->product);
    return;
  }
  if (require_installed) {
    const qa_catalog_availability *availability = &comp->selected->availability;
    if (availability->kind == QA_CATALOG_MISSING) {
      composition_set_error(comp, "Content %s requires: %s", comp->options->product,
                            availability->requirement_count > 0
                                ? availability->requirements[0] : "");
      /* First requirement only in the short form above is lossy;
       * rebuild the exact joined list when more are present. */
      if (availability->requirement_count > 1) {
        free(comp->error);
        comp->error = NULL;
        size_t total = strlen("Content  requires: ") +
                       strlen(comp->options->product);
        for (size_t i = 0; i < availability->requirement_count; i++)
          total += strlen(availability->requirements[i]) + 2;
        comp->error = malloc(total + 1);
        if (comp->error != NULL) {
          int used = snprintf(comp->error, total + 1, "Content %s requires: ",
                              comp->options->product);
          for (size_t i = 0; i < availability->requirement_count; i++)
            used += snprintf(comp->error + used, total + 1 - (size_t)used,
                             "%s%s", i > 0 ? ", " : "",
                             availability->requirements[i]);
        }
      }
      return;
    }
    if (availability->kind == QA_CATALOG_UNRESOLVED) {
      composition_set_error(comp, "Content %s is unresolved: %s",
                            comp->options->product,
                            availability->reason != NULL ? availability->reason : "");
      return;
    }
    comp->installed = comp->selected;
    return;
  }
  comp->installed = comp->selected->availability.kind == QA_CATALOG_INSTALLED
                        ? comp->selected
                        : composition_first_installed(&comp->catalog);
  if (comp->installed == NULL)
    composition_set_error(comp,
                          "No installed game data found in %s; a game charset "
                          "is required for the startup menu",
                          comp->options->corpus_root);
}

/* Ordered composition teardown (TS StartupApplication.close /
 * Application.closeOwned service order, landed subset). Idempotent;
 * comp->error stays owned by main. */
static void composition_close(composition *comp) {
  if (comp->audio != NULL) {
    qa_unified_audio_destroy(comp->audio);
    comp->audio = NULL;
  }
  if (comp->controllers != NULL) {
    qa_sdl_controllers_close(comp->controllers);
    comp->controllers = NULL;
  }
  if (comp->window_open) {
    comp->window_open = false;
    qa_sdl_window_close(&comp->window);
  }
  if (comp->preferences != NULL) {
    qa_app_frontend_preferences_destroy(comp->preferences);
    comp->preferences = NULL;
  }
  if (comp->settings_open) {
    comp->settings_open = false;
    qa_config_store_destroy(&comp->settings);
  }
  if (comp->catalog_open) {
    comp->catalog_open = false;
    comp->selected = NULL;
    comp->installed = NULL;
    qa_content_catalog_destroy(&comp->catalog);
  }
  qa_nightdive_addon_roots_free(comp->addons, comp->addon_count);
  comp->addons = NULL;
  comp->addon_count = 0;
  free(comp->user_root);
  comp->user_root = NULL;
  free(comp->settings_directory);
  comp->settings_directory = NULL;
  free(comp->save_directory);
  comp->save_directory = NULL;
}

/* ---- StartupApplication phase host (graphical run/menu) ---- */

static const char *comp_phase_discover(void *ctx) {
  composition_discover(ctx);
  return NULL;
}
static const char *comp_phase_preferences(void *ctx) {
  composition_load_preferences(ctx);
  return NULL;
}
static const char *comp_phase_splash(void *ctx) {
  (void)ctx; /* TS main opened the splash before StartupApplication.open */
  return NULL;
}
static const char *comp_phase_select(void *ctx) {
  composition_select_product(ctx, false);
  return NULL;
}
static const char *comp_phase_renderer(void *ctx) {
  composition *comp = ctx;
  if (comp->error != NULL) return NULL;
  qa_sdl_window_options options = {0};
  options.title = "Quake Anthology";
  options.width = comp->options->width;
  options.height = comp->options->height;
  options.hidden = comp->options->hidden;
  options.backend = comp->renderer_kind != NULL &&
                            strcmp(comp->renderer_kind, "gl") == 0
                        ? QA_SDL_BACKEND_GL
                        : QA_SDL_BACKEND_CPU;
  if (!qa_sdl_window_open(&options, &comp->window)) {
    composition_set_error(comp,
                          "SDL window could not be opened for the %s renderer "
                          "(src/platform/sdl.ts)",
                          comp->renderer_kind != NULL ? comp->renderer_kind : "cpu");
    return NULL;
  }
  comp->window_open = true;
  return NULL;
}
static double comp_audio_now(void *ctx) {
  (void)ctx;
  return splash_now_ms(NULL);
}
static double comp_audio_random(void *ctx) {
  composition *comp = ctx;
  comp->random_state = comp->random_state * 1664525u + 1013904223u;
  return (double)(comp->random_state >> 8) / 16777216.0;
}
static const char *comp_phase_audio(void *ctx) {
  composition *comp = ctx;
  if (comp->error != NULL) return NULL;
  qa_unified_audio_options options = {0};
  options.milliseconds = comp_audio_now;
  options.random = comp_audio_random;
  options.random_ctx = comp;
  comp->audio = qa_unified_audio_create(&options);
  if (comp->audio == NULL) composition_set_error(comp, "Out of memory");
  return NULL;
}
static const char *comp_phase_input(void *ctx) {
  composition *comp = ctx;
  if (comp->error != NULL) return NULL;
  comp->controllers = qa_sdl_controllers_open();
  if (comp->controllers == NULL)
    composition_set_error(comp,
                          "SdlControllers.open failed "
                          "(src/platform/controller.ts)");
  return NULL;
}
static const char *comp_phase_menu_or_game(void *ctx, bool run_entry) {
  composition *comp = ctx;
  if (comp->error != NULL) return NULL;
  composition_set_error(comp, "%s",
                        run_entry ? composition_missing_game
                                  : composition_missing_menu);
  return NULL;
}
static const char *comp_phase_lobby(void *ctx) {
  composition *comp = ctx;
  if (comp->error != NULL) return NULL;
  composition_set_error(comp, "%s", composition_missing_lobby);
  return NULL;
}
static const char *comp_pump_frame(void *ctx) {
  composition *comp = ctx;
  if (comp->window_open) {
    size_t count = 0;
    qa_sdl_event *events = qa_sdl_window_poll_events(&comp->window, &count);
    for (size_t i = 0; i < count; i++)
      if (events[i].kind == QA_SDL_EVENT_QUIT) comp->quit_requested = true;
    free(events);
    if (qa_sdl_window_closed(&comp->window)) comp->quit_requested = true;
  }
  if (comp->audio != NULL) {
    qa_unified_audio_update_music(comp->audio);
    (void)qa_unified_audio_pump(comp->audio, false, 0, 0);
  }
  if (comp->preferences != NULL)
    qa_app_frontend_preferences_pump(comp->preferences, splash_now_ms(NULL));
  comp->frames += 1;
  return comp->error;
}
static const char *comp_selection_error(void *ctx) {
  return ((composition *)ctx)->error;
}
static bool comp_has_selection(void *ctx) {
  return ((composition *)ctx)->selected != NULL;
}
static bool comp_has_listening_server(void *ctx) {
  (void)ctx;
  return false;
}
static const char *comp_hosted_game(void *ctx) {
  (void)ctx;
  return NULL;
}
static void comp_persist_preferences(void *ctx) {
  composition *comp = ctx;
  if (comp->preferences == NULL) return;
  const char *error = qa_app_frontend_preferences_save_global(comp->preferences);
  if (error != NULL) {
    char message[512];
    (void)snprintf(message, sizeof message, "Could not save controls: %s\n", error);
    print_text(message);
  }
}
static void comp_print(void *ctx, const char *text) {
  (void)ctx;
  print_text(text);
}
static void comp_command(void *ctx, const char *name, const char *const *args,
                         size_t count, const qa_command_context *source) {
  (void)args;
  (void)count;
  (void)source;
  if (name != NULL && strcmp(name, "quit") == 0)
    ((composition *)ctx)->quit_requested = true;
}
static bool comp_handle_event(void *ctx, const qa_sdl_event *event) {
  if (event != NULL && event->kind == QA_SDL_EVENT_QUIT) {
    ((composition *)ctx)->quit_requested = true;
    return true;
  }
  return false;
}
static void comp_capture(void *ctx, const void *request) {
  (void)ctx;
  (void)request;
}
static int comp_frame_count(void *ctx) {
  return (int)((composition *)ctx)->frames;
}
static const char *comp_close_services(void *ctx) {
  composition_close(ctx);
  return NULL;
}
static double comp_frame_index(void *ctx) {
  return ((composition *)ctx)->frames;
}

static qa_app_startup_host composition_startup_host(composition *comp) {
  qa_app_startup_host host = {0};
  host.ctx = comp;
  host.discover_content = comp_phase_discover;
  host.load_preferences = comp_phase_preferences;
  host.open_splash = comp_phase_splash;
  host.select_product = comp_phase_select;
  host.open_renderer = comp_phase_renderer;
  host.open_audio = comp_phase_audio;
  host.open_input = comp_phase_input;
  host.open_menu_or_game = comp_phase_menu_or_game;
  host.open_lobby = comp_phase_lobby;
  host.pump_frame = comp_pump_frame;
  host.selection_error = comp_selection_error;
  host.has_selection = comp_has_selection;
  host.has_listening_server = comp_has_listening_server;
  host.hosted_game = comp_hosted_game;
  host.persist_preferences = comp_persist_preferences;
  host.print = comp_print;
  host.command = comp_command;
  host.handle_event = comp_handle_event;
  host.capture = comp_capture;
  host.frame_count = comp_frame_count;
  host.close_services = comp_close_services;
  host.frame_index = comp_frame_index;
  return host;
}

/* ---- Application phase host (dedicated) ---- */

static const char *dedicated_open(void *ctx, qa_app_application *application) {
  (void)application;
  composition *comp = ctx;
  composition_discover(comp);
  composition_select_product(comp, true);
  if (comp->error == NULL)
    composition_set_error(comp, "%s", composition_missing_game);
  return NULL;
}
static const char *dedicated_step(void *ctx, qa_app_application *application,
                                  double elapsed, const void **out_output) {
  (void)application;
  (void)elapsed;
  composition *comp = ctx;
  if (out_output != NULL) *out_output = NULL;
  if (comp->error != NULL) return comp->error;
  comp->frames += 1;
  return NULL;
}
static const char *dedicated_close(void *ctx, qa_app_application *application) {
  (void)application;
  composition_close(ctx);
  return NULL;
}
static const char *dedicated_missing2(void *ctx, const char *a, const char *b) {
  (void)ctx;
  (void)a;
  (void)b;
  return composition_missing_game;
}
static const char *dedicated_missing_request(
    void *ctx, qa_app_configuration_command_request request) {
  (void)ctx;
  (void)request;
  return composition_missing_game;
}
static const char *dedicated_missing0(void *ctx) {
  (void)ctx;
  return composition_missing_game;
}
static const char *dedicated_read_resource(void *ctx, const char *path,
                                           uint8_t **out_bytes, size_t *out_count,
                                           bool *found) {
  (void)ctx;
  (void)path;
  *out_bytes = NULL;
  *out_count = 0;
  *found = false;
  return composition_missing_game;
}
static bool dedicated_input(void *ctx, const qa_seat_input_event *event) {
  (void)ctx;
  (void)event;
  return false;
}
static void dedicated_pump_input(void *ctx) { (void)ctx; }
static const char *dedicated_advance(void *ctx, bool *advanced) {
  *advanced = false;
  return ((composition *)ctx)->error;
}
static const char *dedicated_flush(void *ctx) {
  (void)ctx;
  return NULL;
}
static bool dedicated_false(void *ctx) {
  (void)ctx;
  return false;
}
static const char *dedicated_read_pixels(void *ctx, uint8_t **out_bytes,
                                         size_t *out_count) {
  (void)ctx;
  *out_bytes = NULL;
  *out_count = 0;
  return "Dedicated applications have no framebuffer";
}

static qa_app_application_host composition_dedicated_host(composition *comp) {
  qa_app_application_host host = {0};
  host.ctx = comp;
  host.print = comp_print;
  host.save_directory = comp->save_directory;
  host.open = dedicated_open;
  host.step = dedicated_step;
  host.close = dedicated_close;
  host.change_level = dedicated_missing2;
  host.save_game = dedicated_missing2;
  host.load_game = dedicated_missing2;
  host.execute_request = dedicated_missing_request;
  host.return_to_lobby = dedicated_missing0;
  host.read_client_resource = dedicated_read_resource;
  host.input = dedicated_input;
  host.pump_client_input = dedicated_pump_input;
  host.advance_client_startup = dedicated_advance;
  host.flush_client_commands = dedicated_flush;
  host.capture_blocks_transition = dedicated_false;
  host.video_restart_pending = dedicated_false;
  host.read_pixels = dedicated_read_pixels;
  return host;
}

/* ---- SIGINT/SIGTERM -> requestQuit (TS main.ts stop handler) ---- */

static volatile sig_atomic_t composition_signal_quit;
static qa_app_application *composition_signal_application;

static void composition_signal_handler(int signum) {
  (void)signum;
  composition_signal_quit = 1;
  if (composition_signal_application != NULL)
    qa_app_application_request_quit(composition_signal_application);
}

static void composition_install_signals(struct sigaction *old_int,
                                        struct sigaction *old_term) {
  composition_signal_quit = 0;
  struct sigaction action = {0};
  action.sa_handler = composition_signal_handler;
  (void)sigemptyset(&action.sa_mask);
  (void)sigaction(SIGINT, &action, old_int);
  (void)sigaction(SIGTERM, &action, old_term);
}

static void composition_restore_signals(const struct sigaction *old_int,
                                        const struct sigaction *old_term) {
  (void)sigaction(SIGINT, old_int, NULL);
  (void)sigaction(SIGTERM, old_term, NULL);
  composition_signal_application = NULL;
}

/* TS StartupApplication.open default saveDirectory. */
static char *default_save_directory(void) {
  const char *home = getenv("HOME");
  char *share = join_path(home != NULL ? home : ".", ".local/share/quake-typescript");
  if (share == NULL) return NULL;
  char *out = join_path(share, "saves");
  free(share);
  return out;
}

/* ---- weapon-behavior tool host (TS runWeaponBehaviorTool) ---- */

typedef struct weapon_host_state {
  qa_content_catalog catalog;
  bool catalog_open;
  qa_catalog_mount_list mounts;
  bool mounts_open;
  qa_mount_id *order;
  char *plan_id;
  qa_open_mount_options mount_options;
  qa_content_mounted *mounted;
} weapon_host_state;

static void weapon_print(void *ctx, const char *text) {
  (void)ctx;
  fputs(text, stdout);
  fflush(stdout);
}

/* discoverInstalledContent + catalog.require + mountsFor +
 * openMountPlan("weapon-authoring", hex(product.id)). require
 * aborts with the exact TS message, as the landed tool does. */
static const void *weapon_open_product(
    void *ctx, const qa_app_weapon_behavior_tool_command *command,
    const char **out_family, const char **out_edition,
    const char **out_expectation_id, const char **out_content_directory,
    const char **out_product_id) {
  weapon_host_state *state = ctx;
  qa_discover_content_options options = {0};
  options.corpus_root = command->corpus_root;
  options.user_content_root = command->user_content_root;
  options.discover_mods = true;
  options.inspector.inspect = inspect_archive;
  if (!qa_discover_installed_content(&options, &state->catalog))
    qa_range_error("Out of memory");
  state->catalog_open = true;
  const qa_catalog_discovered_product *product =
      qa_content_catalog_require(&state->catalog, command->product);
  if (!qa_content_catalog_mounts_for(&state->catalog, product->id, &state->mounts))
    qa_range_error("Out of memory");
  state->mounts_open = true;
  state->order = calloc(state->mounts.count > 0 ? state->mounts.count : 1,
                         sizeof *state->order);
  if (state->order == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < state->mounts.count; i++) {
    const qa_content_mount *mount = &state->mounts.mounts[i];
    state->order[i] = mount->kind == QA_CONTENT_MOUNT_ARCHIVE
                          ? mount->as.archive.identity.id
                          : mount->as.loose.identity.id;
  }
  char *hex = hex_encode(product->id);
  if (hex == NULL) qa_range_error("Out of memory");
  state->plan_id = qa_create_mount_plan_id("weapon-authoring", hex);
  free(hex);
  if (state->plan_id == NULL) qa_range_error("Out of memory");
  qa_resolved_mount_plan plan = {0};
  plan.id = state->plan_id;
  plan.mounts = state->mounts.mounts;
  plan.mount_count = state->mounts.count;
  plan.default_order = state->order;
  plan.default_order_count = state->mounts.count;
  state->mounted = qa_open_mount_plan(&plan, &state->mount_options);
  if (state->mounted == NULL) qa_range_error("Out of memory");
  *out_family = qa_game_family_name(product->expectation.family);
  *out_edition = product->expectation.edition;
  *out_expectation_id = product->expectation.id;
  *out_content_directory = product->expectation.content_directory;
  *out_product_id = product->id;
  return state->mounted;
}

static void weapon_close_product(void *ctx, const void *content) {
  (void)content;
  weapon_host_state *state = ctx;
  if (state->mounted != NULL) {
    qa_content_mounted_destroy(state->mounted);
    state->mounted = NULL;
  }
  free(state->order);
  state->order = NULL;
  free(state->plan_id);
  state->plan_id = NULL;
  if (state->mounts_open) {
    state->mounts_open = false;
    qa_content_catalog_mount_list_free(&state->mounts);
  }
  if (state->catalog_open) {
    state->catalog_open = false;
    qa_content_catalog_destroy(&state->catalog);
  }
}

static const char *weapon_run_qc(void *ctx,
                                 const qa_app_weapon_behavior_tool_command *command,
                                 const void *content,
                                 void (*print)(void *, const char *), void *print_ctx) {
  (void)ctx;
  (void)command;
  (void)content;
  (void)print;
  (void)print_ctx;
  return "Weapon-behavior QC body is not yet wired: loadQcProgram, "
         "inspectQcTrajectoryBindings and resolveQcWeaponBehavior "
         "(src/app/bootstrap/weapon-behavior-tool.ts, "
         "src/content/catalog/weapon-behaviors.ts, "
         "src/compat/qc/program.ts) have no composition host in src/main.c";
}
static const char *weapon_run_qvm(void *ctx,
                                  const qa_app_weapon_behavior_tool_command *command,
                                  const void *content,
                                  void (*print)(void *, const char *), void *print_ctx) {
  (void)ctx;
  (void)command;
  (void)content;
  (void)print;
  (void)print_ctx;
  return "Weapon-behavior QVM body is not yet wired: parseQvm, "
         "discoverQvmWeaponBehaviors and loadQvmWeaponBehavior "
         "(src/app/bootstrap/weapon-behavior-tool.ts, "
         "src/content/catalog/qvm-weapon-behaviors.ts, "
         "src/compat/qvm/image.ts) have no composition host in src/main.c";
}
static const char *weapon_run_native(void *ctx,
                                     const qa_app_weapon_behavior_tool_command *command,
                                     const void *content,
                                     void (*print)(void *, const char *), void *print_ctx) {
  (void)ctx;
  (void)command;
  (void)content;
  (void)print;
  (void)print_ctx;
  return "Weapon-behavior native body is not yet wired: parsePe, "
         "discoverNativeWeaponBehaviors and loadNativeWeaponBehavior "
         "(src/app/bootstrap/weapon-behavior-tool.ts, "
         "src/content/catalog/native-weapon-behaviors.ts, "
         "src/guest/pe/index.ts) have no composition host in src/main.c";
}

/* ---- main ---- */

static bool parse_positive_integer(const char *text, double *out) {
  if (text == NULL || *text == '\0') return false;
  char *end = NULL;
  double value = strtod(text, &end);
  if (end == text || *end != '\0') return false;
  if (value != (double)(int64_t)value || value <= 0) return false;
  *out = value;
  return true;
}

static bool open_browser_url(void *ctx, const char *url,
                             qa_llm_abort_signal *signal,
                             qa_llm_error *error) {
  (void)ctx;
  (void)error;
  if (signal != NULL && signal->aborted) return true;
  qa_open_sdl_url(url);
  return true;
}

int main(int argc, char **argv) {
  (void)print_text; /* host.print collector; wired with composition */
  (void)qa_bounded_queue_init(&recent_output, 256, sizeof(char *));
  if (argc > 1 && strcmp(argv[1], "--splash-child") == 0) {
    double width = 0, height = 0;
    if (argc < 4 || !parse_positive_integer(argv[2], &width) ||
        !parse_positive_integer(argv[3], &height))
      return 1;
    splash_window window = {0};
    return qa_app_run_splash_child(splash_host_for(&window), width, height);
  }

  qa_content_root_search search = {0};
  char *exe_dir = exe_directory();
  search.own_directory = exe_dir;
  search.home_directory = getenv("HOME");
  qa_app_options_host options_host = {0};
  options_host.parse_weapon_behavior_tool = parse_weapon_behavior;
  options_host.resolve_path = resolve_path_keep;
  qa_app_application_command command = qa_app_parse_application_command(
      (const char *const *)(argv + 1), argc > 0 ? (size_t)(argc - 1) : 0,
      search, &options_host);

  int result = 1;
  if (strcmp(command.kind, "version") == 0) {
    printf("Quake Anthology %s\n", qa_version);
    result = 0;
  } else if (strcmp(command.kind, "help") == 0) {
    fputs(qa_app_application_help, stdout);
    result = 0;
  } else if (strcmp(command.kind, "list-content") == 0) {
    result = run_list_content(command.corpus_root);
  } else if (strcmp(command.kind, "weapon-behavior") == 0) {
    weapon_host_state state = {0};
    qa_app_weapon_behavior_tool_host host = {0};
    host.ctx = &state;
    host.open_product = weapon_open_product;
    host.close_product = weapon_close_product;
    host.run_qc = weapon_run_qc;
    host.run_qvm = weapon_run_qvm;
    host.run_native = weapon_run_native;
    qa_app_run_weapon_behavior_tool(
        host, *(qa_app_weapon_behavior_tool_command *)command.weapon_behavior_command,
        weapon_print, NULL);
    result = 0;
  } else { /* run | menu */
    splash_child child = {.pid = -1, .stdin_fd = -1};
    qa_app_splash_screen *splash = NULL;
    if (!command.options.dedicated && !command.options.hidden) {
      qa_app_splash_host host = {0};
      host.ctx = &child;
      host.open_window = splash_child_open;
      host.poll_quit = splash_child_true;
      host.present = splash_child_void;
      host.close_window = splash_child_close;
      host.sleep_ms = splash_child_sleep;
      host.write_stdout = splash_write_stdout;
      host.now_ms = splash_now_ms;
      host.load_title_image = splash_child_no_image;
      splash = qa_app_open_splash(host, command.options.width,
                                  command.options.height);
    }
    qa_app_startup_stamp("splash");
    char *llm_directory = llm_settings_directory();
    qa_llm_settings_service *llm = NULL;
    if (llm_directory != NULL) {
      migrate_legacy_llm_settings(llm_directory);
      if (composition_llm_enabled(command.kind, command.options.dedicated)) {
        qa_llm_settings_options llm_options = {0};
        llm_options.base_directory = llm_directory;
        llm_options.auth.open_browser = open_browser_url;
        llm = qa_llm_settings_open(&llm_options);
      }
    }
    qa_app_startup_stamp("llm-settings");
    /* TS host print is print_text (recent-output queue + stdout);
     * both composition hosts route through comp_print to it. */
    composition comp = {0};
    comp.kind = command.kind;
    comp.save_directory = default_save_directory();
    comp.random_state = 0x9e3779b9u;
    if (comp.save_directory == NULL) composition_set_error(&comp, "Out of memory");
    /* Options ownership transfers to the opened application
     * (its destroy frees them); comp borrows the same strings. */
    qa_app_options options = command.options;
    memset(&command.options, 0, sizeof command.options);
    comp.options = &options;
    if (composition_kind_for(options.dedicated) == COMPOSITION_DEDICATED) {
      qa_app_application *application = qa_app_open_application(
          options, composition_dedicated_host(&comp), NULL, false, false,
          (qa_app_frontend_preference_overrides){0});
      if (application == NULL && comp.error == NULL)
        composition_set_error(&comp, "Out of memory");
      if (application != NULL && comp.error == NULL) {
        struct sigaction old_int, old_term;
        composition_signal_application = application;
        composition_install_signals(&old_int, &old_term);
        double previous = splash_now_ms(NULL);
        while (composition_should_continue(
            composition_signal_quit != 0,
            qa_app_application_finished(application), false, 0, 0)) {
          double now = splash_now_ms(NULL);
          double elapsed = now - previous;
          if (elapsed < 4) {
            splash_sleep(NULL, 4 - elapsed);
            continue;
          }
          previous = now;
          const void *output = NULL;
          const char *error =
              qa_app_application_step(application, elapsed, &output);
          if (error != NULL) {
            composition_set_error(&comp, "%s", error);
            break;
          }
        }
        composition_restore_signals(&old_int, &old_term);
      }
      if (application != NULL) qa_app_application_destroy(application);
    } else {
      qa_app_startup_application *application =
          qa_app_startup_application_open(composition_startup_host(&comp), options,
                                          comp.save_directory,
                                          composition_entry_for(command.kind));
      if (application == NULL && comp.error == NULL)
        composition_set_error(&comp, "Out of memory");
      if (application != NULL && comp.error == NULL) {
        qa_app_splash_screen_close(splash);
        splash = NULL;
        struct sigaction old_int, old_term;
        composition_install_signals(&old_int, &old_term);
        while (composition_should_continue(
            comp.quit_requested || composition_signal_quit != 0, false,
            options.has_frame_limit, options.frame_limit,
            qa_app_startup_application_frame_count(application))) {
          qa_app_startup_application_pump(application);
          if (comp.error != NULL) break;
          splash_sleep(NULL, 8); /* TS run(): 8 ms with no game/remote */
        }
        composition_restore_signals(&old_int, &old_term);
      }
      if (application != NULL) qa_app_startup_application_destroy(application);
    }
    composition_close(&comp); /* idempotent backstop if open failed early */
    comp.options = NULL; /* application destroy freed the strings */
    if (comp.error != NULL) {
      write_crash_log(comp.error, argc, argv);
      fprintf(stderr, "%s\n", comp.error);
      result = 1;
    } else {
      result = 0;
    }
    free(comp.error);
    if (llm != NULL) {
      qa_llm_settings_close(llm);
      qa_llm_settings_destroy(llm);
    }
    qa_app_splash_screen_close(splash);
    free(llm_directory);
  }

  qa_app_application_command_destroy(&command);
  free(exe_dir);
  return result;
}



