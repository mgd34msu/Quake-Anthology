/*
 * Port of src/main.ts. Command dispatch runs through the landed
 * options parser: --version/--help/--list-content and
 * --splash-child are fully wired. Graphical run/menu open
 * StartupApplication (startup_application.h) and run/close it as TS
 * main does. Dedicated runs compose Application over the landed
 * catalog services and the concrete SharedSimulation builder;
 * composition steps whose TS service has only a host-seam C port
 * fail with a named missing-service error (crash.log + exit 1, as
 * in TS main's catch) instead of a stub.
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
#include "qa/app/bootstrap/audio/menu.h"
#include "qa/app/bootstrap/client_bootstrap.h"
#include "qa/app/bootstrap/configuration.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/frontend_preferences.h"
#include "qa/app/bootstrap/local_lobby.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/app/bootstrap/splash.h"
#include "qa/app/bootstrap/startup.h"
#include "qa/app/bootstrap/startup_application.h"
#include "qa/app/bootstrap/startup_audio.h"
#include "qa/app/bootstrap/startup_menu.h"
#include "qa/app/bootstrap/menu_art.h"
#include "qa/app/bootstrap/menu_font.h"
#include "qa/app/bootstrap/startup_selection.h"
#include "qa/render/scene/resources.h"
#include "qa/ui/common/assets.h"
#include "qa/app/bootstrap/startup_saves.h"
#include "qa/app/bootstrap/startup_timing.h"
#include "qa/app/bootstrap/weapon_behavior_tool.h"
#include "qa/audio/bank.h"
#include "qa/audio/engine.h"
#include "qa/compat/qvm/compatibility.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/common/skin.h"
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
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

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
    (void)snprintf(w, sizeof w, "%s", QA_JS_NUMBER(width));
    (void)snprintf(h, sizeof h, "%s", QA_JS_NUMBER(height));
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
 * and finally the LLM settings. The dedicated application.c host
 * aborts on a returned phase error, which would bypass that catch
 * (crash.log, ordered close, exit 1), so its phases record the
 * first error in the composition and return NULL; main fails on
 * the recorded error after open returns. StartupApplication raises
 * (qa_raise), caught by composition_run_startup into the same
 * first-error slot. */

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
  qa_catalog_mount_list mount_list; /* TS catalog.mountsFor(installed) */
  bool mount_list_open;
  qa_mount_id *mount_order;      /* owned plan defaultOrder storage */
  char *mount_plan_id;           /* owned */
  qa_open_mount_options mount_options;
  qa_content_mounted *mounted;
  qa_app_configuration_content *configuration; /* openInitialConfigurationContent */
  /* Game content recipe backing (TS loadApplicationContent):
   * preset + resolveLaunch results the recipe borrows from, and
   * the recipe mount plan opened over them. */
  qa_app_application_preset_result *recipe_preset;
  qa_resolve_launch_result *recipe_launch;
  qa_content_mounted *game_mounted;
  /* Retained resolveLaunch adapters: resolved recipe references
   * borrow their id/path/digest strings from the opened records
   * of the mounts that resolved them, so those mounts/readers
   * must outlive the recipe (composition_close destroys them). */
  struct composition_launch_adapter **launch_adapters;
  size_t launch_adapter_count, launch_adapter_cap;
  /* Concrete SharedSimulation built by dedicated_open over the
   * composed (options, actors, session); the shim host below
   * delegates to it once built. Owned by its host close. */
  qa_app_sim_shared_simulation_host concrete_host;
  qa_app_sim_concrete_simulation *concrete_state; /* borrowed */
  bool concrete_built;
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

/* Remaining named gaps after the concrete wiring below. Content
 * (catalog product/require, preset + resolveLaunch, recipe mounts,
 * prepare_*), the Application identity/EngineSession composition
 * (application.c), the concrete SharedSimulation builder
 * (simulation/runtime qa_app_sim_shared_simulation_concrete_host,
 * built in dedicated_open over the composed options/actors/
 * session and reached through the composition shim host) are
 * constructed for real. The concrete builder itself does not
 * construct source-game runtimes (QuakeC/QVM/native guests) and
 * decodes only Q1 worlds from mounts, so recipes needing those
 * stop at the named errors below instead of stepping a fake
 * world; dedicated_open pre-checks them so they surface through
 * the crash-log path rather than the builder's aborts. */
static const char composition_missing_game[] =
    "Application game composition is not available for this "
    "recipe: the concrete SharedSimulation "
    "(src/app/bootstrap/simulation/runtime.ts) was not built";


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

/* ---- concrete bootstrap hosts over landed services ----
 *
 * The dedicated Application content host over the discovered
 * catalog and mounts. Hosts record comp->error and return NULL
 * for the crash-log reason above. */

/* Catalog product facts for content/configuration selectors. */
static const qa_catalog_discovered_product *composition_product_for(
    composition *comp, const char *id) {
  const qa_catalog_discovered_product *product =
      composition_find_product(&comp->catalog, id);
  if (product != NULL) return product;
  return comp->installed; /* provider content ids resolve to installed */
}

static bool composition_content_product(void *ctx, const char *id,
                                        qa_game_family *family,
                                        const char **edition,
                                        const char **campaign, bool *installed,
                                        bool *builtin) {
  composition *comp = ctx;
  const qa_catalog_discovered_product *product =
      composition_product_for(comp, id);
  if (product == NULL) return false;
  *family = product->expectation.family;
  *edition = product->expectation.edition;
  *campaign = product->expectation.campaign;
  *installed = product->availability.kind == QA_CATALOG_INSTALLED;
  *builtin = *installed; /* discovered catalog rows are builtin expectations */
  return true;
}

static bool composition_content_require(void *ctx, const char *id) {
  return composition_find_product(&((composition *)ctx)->catalog, id) != NULL;
}

/* Content host: open_configuration publishes catalog+mounts+source
 * selection; game-internal preparation steps keep the named
 * Application gap instead of pretending to run. */
static const char *composition_host_open_configuration(
    void *ctx, qa_app_configuration_content *content) {
  composition *comp = ctx;
  qa_app_configuration_content_set_catalog(content, &comp->catalog);
  qa_app_configuration_content_set_mounts(content, comp->mounted);
  qa_app_content_host host = {0};
  host.ctx = comp;
  host.product = composition_content_product;
  host.require_product = composition_content_require;
  qa_app_application_source_selection source =
      qa_app_application_source_selection_of(host, comp->options->product, NULL);
  qa_app_application_configuration_selection selection = {0};
  selection.source = source.source;
  selection.engine_behavior = source.source;
  selection.match = source.match;
  selection.combat = source.source;
  selection.movement = source.source;
  qa_app_configuration_content_set_selection(content, selection);
  return comp->error;
}

static const char *composition_for_content(void *ctx,
                                           qa_app_loaded_content *content,
                                           qa_content_id content_id,
                                           const void **out_mounts) {
  (void)content;
  (void)content_id;
  composition *comp = ctx;
  *out_mounts = comp->game_mounted != NULL ? comp->game_mounted : comp->mounted;
  return comp->error;
}

/* TS LoadedApplicationContent.prepareWeaponBehaviors: an empty
 * selection is a no-op in TS; a selected behavior needs the
 * weapon-behavior-selection preparer host, which is host-seam in
 * its slice (qa_app_prepare_configured_recipe's prepare callback),
 * so main names it instead of skipping it silently. */
static const char *composition_prepare_weapon_behaviors(
    void *ctx, qa_app_loaded_content *content) {
  composition *comp = ctx;
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  if (recipe->weapon_behavior_count == 0 && !comp->options->has_weapon_behavior)
    return NULL;
  return "Application content preparation requires "
         "prepareApplicationWeaponBehavior "
         "(src/app/bootstrap/weapon-behavior-selection.ts), whose "
         "mounted preparer host has no concrete C composition";
}

/* TS LoadedApplicationContent.prepareMods: empty selection is a
 * no-op; selected mods need prepareApplicationMods over the
 * simulation mounted-mod preparers (mod_selection.h prepare_mod
 * host), which no landed C module constructs for this root. */
static const char *composition_prepare_mods(void *ctx,
                                            qa_app_loaded_content *content,
                                            const char *purpose) {
  (void)purpose;
  composition *comp = ctx;
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  if (recipe->mod_count == 0 && comp->options->mod_count == 0) return NULL;
  return "Application content preparation requires "
         "prepareApplicationMods "
         "(src/app/bootstrap/mod-selection.ts), whose mounted-mod "
         "preparer host has no concrete C composition";
}

/* TS LoadedApplicationContent.prepareQvmGrapple: only an enabled
 * q3-qvm grapple selection prepares anything. */
static const char *composition_prepare_grapple(void *ctx,
                                               qa_app_loaded_content *content) {
  (void)ctx;
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  const qa_grapple_selection *grapple = &recipe->equipment.grapple;
  if (!grapple->is_enabled || grapple->mechanic == NULL ||
      strcmp(grapple->mechanic, "q3-qvm") != 0)
    return NULL;
  return "Application content preparation requires "
         "prepareApplicationQvmGrapple "
         "(src/app/bootstrap/qvm-grapple-selection.ts), whose QVM "
         "host has no concrete C composition";
}

/* qa_launch_mounted over qa_content_mounted: the resolveLaunch
 * services (TS openMountPlan + readQvmCompatibility) main supplies
 * as composition root, mirroring content.c's native adapter. */
typedef struct composition_launch_adapter {
  qa_content_mounted *mounted; bool owns_mounted;
  qa_mounted_content_reader *reader;
  uint8_t *last_bytes;
  composition *owner;
} composition_launch_adapter;
/* Retain an adapter (and its mount/reader) in the composition. */
static bool composition_launch_retain(composition_launch_adapter *adapter) {
  composition *comp = adapter->owner;
  if (comp == NULL) return true; /* unowned: close still retains nothing */
  if (comp->launch_adapter_count == comp->launch_adapter_cap) {
    size_t next = comp->launch_adapter_cap != 0 ? comp->launch_adapter_cap * 2 : 4;
    struct composition_launch_adapter **grown =
        realloc(comp->launch_adapters, next * sizeof *grown);
    if (grown == NULL) return false;
    comp->launch_adapters = grown;
    comp->launch_adapter_cap = next;
  }
  comp->launch_adapters[comp->launch_adapter_count++] = adapter;
  return true;
}
static bool composition_launch_open(void *ctx, const char *path,
                                    qa_launch_mount_predicate accept,
                                    void *accept_ctx,
                                    qa_catalog_opened_resource *out) {
  composition_launch_adapter *adapter = ctx;
  qa_content_opened_resource opened;
  bool found = adapter->reader
      ? qa_mounted_content_reader_open(adapter->reader, path,
                                       (qa_mount_accept_fn)accept, accept_ctx, &opened)
      : qa_content_mounted_open(adapter->mounted, path, (qa_mount_accept_fn)accept,
                                accept_ctx, &opened);
  if (!found) return false;
  free(adapter->last_bytes);
  adapter->last_bytes = opened.bytes;
  out->reference = *opened.reference;
  out->bytes = opened.bytes;
  out->byte_length = opened.byte_length;
  return true;
}
static bool composition_launch_resolve(void *ctx, const char *path,
                                       qa_resolved_resource_reference *out) {
  composition_launch_adapter *adapter = ctx;
  const qa_resolved_resource_reference *reference = NULL;
  bool found = adapter->reader
      ? qa_mounted_content_reader_resolve(adapter->reader, path, &reference)
      : qa_content_mounted_resolve(adapter->mounted, path, &reference);
  if (!found) return false;
  *out = *reference;
  return true;
}
static void composition_launch_close(void *ctx);
static bool composition_launch_borrow(void *ctx, qa_mount_plan_id id,
                                      const qa_mount_id *order, size_t order_count,
                                      qa_launch_mounted *out) {
  composition_launch_adapter *adapter = ctx;
  qa_mounted_content_reader *reader = qa_content_mounted_borrow_ordered_reader(
      adapter->mounted, id, order, order_count, NULL, 0);
  if (reader == NULL) return false;
  composition_launch_adapter *scoped = calloc(1, sizeof *scoped);
  if (scoped == NULL) { qa_mounted_content_reader_destroy(reader); return false; }
  scoped->mounted = adapter->mounted;
  scoped->reader = reader;
  scoped->owner = adapter->owner;
  if (!composition_launch_retain(scoped)) {
    qa_mounted_content_reader_destroy(reader);
    free(scoped);
    return false;
  }
  memset(out, 0, sizeof *out);
  out->ctx = scoped;
  out->plan = qa_mounted_content_reader_plan(reader);
  out->open = composition_launch_open;
  out->resolve = composition_launch_resolve;
  out->borrow_ordered = composition_launch_borrow;
  out->close = composition_launch_close;
  return true;
}
static void composition_launch_close(void *ctx) {
  /* Retained by the composition (see launch_adapters): closing
   * here would free the opened records recipe references borrow
   * their strings from. composition_close destroys adapters. */
  (void)ctx;
}
static bool composition_launch_open_plan(void *ctx,
                                         const qa_resolved_mount_plan *plan,
                                         const void *options,
                                         qa_launch_mounted *out) {
  qa_content_mounted *mounted =
      qa_open_mount_plan(plan, (const qa_open_mount_options *)options);
  if (mounted == NULL) return false;
  composition_launch_adapter *adapter = calloc(1, sizeof *adapter);
  if (adapter == NULL) { qa_content_mounted_destroy(mounted); return false; }
  adapter->mounted = mounted;
  adapter->owns_mounted = true;
  adapter->owner = ctx;
  if (!composition_launch_retain(adapter)) {
    qa_content_mounted_destroy(mounted);
    free(adapter);
    return false;
  }
  memset(out, 0, sizeof *out);
  out->ctx = adapter;
  out->plan = qa_content_mounted_plan(mounted);
  out->open = composition_launch_open;
  out->resolve = composition_launch_resolve;
  out->borrow_ordered = composition_launch_borrow;
  out->close = composition_launch_close;
  return true;
}
static const char *composition_launch_qvm_compat(void *ctx,
                                                 const qa_launch_mounted *mounted,
                                                 const char *artifact_path,
                                                 qa_content_digest digest,
                                                 const char *role) {
  (void)ctx;
  composition_launch_adapter *adapter = mounted->ctx;
  qa_qvm_abi_profile profile = NULL;
  qa_qvm_module_path_digest module = {artifact_path, digest};
  if (!qa_qvm_read_compatibility(adapter->mounted, module, role, &profile))
    return NULL;
  return profile;
}

/* TS loadApplicationContent over the composition catalog:
 * catalog.require(product), applicationPreset + resolveLaunch,
 * openMountPlan(recipe.mounts), then prepareWeaponBehaviors/
 * prepareMods/prepareQvmGrapple (content.ts order). The recipe
 * and mounts are published on the loaded content for
 * application.c's session/simulation composition. Idempotent
 * against the graphical phases, which already discovered and
 * selected. Errors are recorded in comp and NULL is returned,
 * because content.c would abort on a returned phase error and
 * bypass main's crash-log path. */
static const char *composition_open_loaded(void *ctx,
                                           qa_app_loaded_content *content) {
  composition *comp = ctx;
  composition_discover(comp);
  if (comp->installed == NULL) composition_select_product(comp, true);
  if (comp->error != NULL) return NULL;
  if (comp->recipe_launch == NULL) {
    comp->recipe_preset = calloc(1, sizeof *comp->recipe_preset);
    comp->recipe_launch = calloc(1, sizeof *comp->recipe_launch);
    if (comp->recipe_preset == NULL || comp->recipe_launch == NULL ||
        !qa_app_application_preset_for_catalog(&comp->catalog, comp->options,
                                               NULL, NULL, comp->recipe_preset)) {
      composition_set_error(comp, "Out of memory");
      return NULL;
    }
    qa_resolve_launch_options launch = {0};
    launch.choice = qa_preset_choice(comp->recipe_preset->preset.id);
    launch.preset = &comp->recipe_preset->preset;
    launch.catalog = &comp->catalog;
    launch.mounts_options = &comp->mount_options;
    launch.services.ctx = comp;
    launch.services.open_mount_plan = composition_launch_open_plan;
    launch.services.read_qvm_compatibility = composition_launch_qvm_compat;
    if (!qa_resolve_launch(&launch, comp->recipe_launch)) {
      composition_set_error(comp, "Out of memory");
      return NULL;
    }
    comp->game_mounted = qa_open_mount_plan(&comp->recipe_launch->recipe.mounts,
                                            &comp->mount_options);
    if (comp->game_mounted == NULL) {
      composition_set_error(comp, "Out of memory");
      return NULL;
    }
  }
  qa_app_loaded_content_set_catalog(content, &comp->catalog);
  qa_app_loaded_content_set_mounts(content, comp->game_mounted);
  qa_app_loaded_content_set_recipe(content, comp->recipe_launch->recipe);
  const char *error = composition_prepare_weapon_behaviors(comp, content);
  if (error == NULL) error = composition_prepare_mods(comp, content, "gameplay");
  if (error == NULL) error = composition_prepare_grapple(comp, content);
  if (error != NULL) composition_set_error(comp, "%s", error);
  return NULL;
}

/* SharedSimulation shim host: application.c loads the simulation
 * over this bundle before host.open runs, but the concrete
 * builder (qa_app_sim_shared_simulation_concrete_host) needs the
 * composed (options, actors, session) that only exist after that
 * load. dedicated_open therefore builds the concrete pipeline
 * over the loaded simulation's own fields and binds it; these
 * callbacks delegate from then on. Reaching one unbuilt means
 * the composition error path was bypassed, so it aborts with the
 * named gap instead of stepping a fake world. */
static qa_simulation_output composition_sim_step(void *ctx, qa_input_batch input) {
  composition *comp = ctx;
  if (!comp->concrete_built) qa_range_error("%s", composition_missing_game);
  return comp->concrete_host.step(comp->concrete_host.ctx, input);
}
static qa_save_image composition_sim_checkpoint(void *ctx) {
  composition *comp = ctx;
  if (!comp->concrete_built) qa_range_error("%s", composition_missing_game);
  return comp->concrete_host.checkpoint(comp->concrete_host.ctx);
}
static void composition_sim_close(void *ctx) {
  composition *comp = ctx;
  if (!comp->concrete_built) return;
  comp->concrete_built = false;
  comp->concrete_state = NULL;
  comp->concrete_host.close(comp->concrete_host.ctx);
}
static qa_app_sim_shared_simulation_host composition_simulation_host(
    composition *comp) {
  qa_app_sim_shared_simulation_host host = {0};
  host.ctx = comp;
  host.step = composition_sim_step;
  host.checkpoint = composition_sim_checkpoint;
  host.close = composition_sim_close;
  return host;
}

static qa_app_content_host composition_content_host(composition *comp) {
  qa_app_content_host host = {0};
  host.ctx = comp;
  host.product = composition_content_product;
  host.require_product = composition_content_require;
  host.open_loaded = composition_open_loaded;
  host.open_configuration = composition_host_open_configuration;
  host.for_content = composition_for_content;
  host.prepare_mods = composition_prepare_mods;
  host.prepare_grapple = composition_prepare_grapple;
  host.prepare_weapon_behaviors = composition_prepare_weapon_behaviors;
  return host;
}

/* Ordered composition teardown (TS StartupApplication.close /
 * Application.closeOwned service order, landed subset). Idempotent;
 * comp->error stays owned by main. */
static void composition_close(composition *comp) {
  if (comp->configuration != NULL) {
    qa_app_configuration_content_destroy(comp->configuration);
    comp->configuration = NULL;
  }
  if (comp->mounted != NULL) {
    qa_content_mounted_destroy(comp->mounted);
    comp->mounted = NULL;
  }
  if (comp->game_mounted != NULL) {
    qa_content_mounted_destroy(comp->game_mounted);
    comp->game_mounted = NULL;
  }
  if (comp->recipe_launch != NULL) {
    qa_resolve_launch_result_free(comp->recipe_launch);
    free(comp->recipe_launch);
    comp->recipe_launch = NULL;
  }
  if (comp->recipe_preset != NULL) {
    qa_app_application_preset_result_free(comp->recipe_preset);
    free(comp->recipe_preset);
    comp->recipe_preset = NULL;
  }
  for (size_t i = 0; i < comp->launch_adapter_count; i++) {
    struct composition_launch_adapter *adapter = comp->launch_adapters[i];
    if (adapter->reader) qa_mounted_content_reader_destroy(adapter->reader);
    if (adapter->owns_mounted && adapter->mounted)
      qa_content_mounted_destroy(adapter->mounted);
    free(adapter->last_bytes);
    free(adapter);
  }
  free(comp->launch_adapters);
  comp->launch_adapters = NULL;
  comp->launch_adapter_count = 0;
  comp->launch_adapter_cap = 0;
  free(comp->mount_order);
  comp->mount_order = NULL;
  free(comp->mount_plan_id);
  comp->mount_plan_id = NULL;
  if (comp->mount_list_open) {
    comp->mount_list_open = false;
    qa_content_catalog_mount_list_free(&comp->mount_list);
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

static void comp_print(void *ctx, const char *text) {
  (void)ctx;
  print_text(text);
}

/* ---- Application host (dedicated + nested graphical game) ----
 *
 * application.c owns the TS Application.openSource owned-path
 * composition: with has_content it loads content through
 * composition_open_loaded (catalog require + mountsFor +
 * openMountPlan + prepare_*), then composes identity/EngineSession
 * and, with has_simulation, loadSimulation over the bundle below
 * and attaches it to the session. host.open therefore has no
 * composition left to do (TS dedicated open only opens its
 * console; graphical services were opened by the startup phases);
 * it records the named simulation gap when no concrete factory
 * exists, after the real content/session composition has run. */
/* Whether the recipe carries provider timing (players.c aborts
 * without it inside the concrete builder). */
static bool composition_recipe_has_timing(const qa_executable_recipe *recipe,
                                          const char *provider) {
  if (provider == NULL) return false;
  for (size_t i = 0; i < recipe->timing_count; i++)
    if (recipe->timing[i].provider != NULL &&
        strcmp(recipe->timing[i].provider, provider) == 0)
      return true;
  return false;
}

static const char *dedicated_open(void *ctx, qa_app_application *application) {
  composition *comp = ctx;
  if (comp->error != NULL) return NULL;
  qa_app_sim_shared_simulation *simulation =
      qa_app_application_simulation(application);
  if (simulation == NULL) {
    composition_set_error(comp, "%s", composition_missing_game);
    return NULL;
  }
  /* Pre-check the concrete builder's aborting guards (runtime.c
   * c_validate_options / c_resolve_world) so unsupported recipes
   * fail through the crash-log path with precise causes. */
  const qa_executable_recipe *recipe = &simulation->options.recipe;
  if (recipe->weapons.count != 1) {
    composition_set_error(comp,
                          "This source arsenal requires one selected "
                          "weapon provider");
    return NULL;
  }
  for (size_t i = 0; i < recipe->execution_count; i++) {
    if (recipe->execution[i].kind != QA_EXECUTION_MODULE_TYPESCRIPT &&
        recipe->execution[i].kind != QA_EXECUTION_MODULE_QUAKEC) {
      composition_set_error(comp,
                            "Concrete SharedSimulation does not construct "
                            "the source game for execution module %s "
                            "(src/app/bootstrap/simulation/runtime.ts: "
                            "QVM/native guests are not constructed)",
                            recipe->execution[i].owner.provider != NULL
                                ? recipe->execution[i].owner.provider : "");
      return NULL;
    }
  }
  if (!composition_recipe_has_timing(recipe, recipe->map.entities.provider) ||
      !composition_recipe_has_timing(recipe, recipe->movement.provider)) {
    composition_set_error(comp, "Recipe has no timing for %s",
                          !composition_recipe_has_timing(
                              recipe, recipe->map.entities.provider)
                              ? recipe->map.entities.provider
                              : recipe->movement.provider);
    return NULL;
  }
  comp->concrete_host = qa_app_sim_shared_simulation_concrete_host(
      simulation->options, simulation->actors, simulation->session,
      &comp->concrete_state);
  qa_app_sim_concrete_simulation_bind(comp->concrete_state, simulation);
  comp->concrete_built = true;
  return NULL; /* comp keeps any error; main owns the crash path */
}
/* Legacy phase step: unused whenever a simulation is attached
 * (application.c steps the session instead). Without one it must
 * surface the composition error, never advance a fake world. */
static const char *dedicated_step(void *ctx, qa_app_application *application,
                                  double elapsed, const void **out_output) {
  (void)application;
  (void)elapsed;
  composition *comp = ctx;
  if (out_output != NULL) *out_output = NULL;
  if (comp->error != NULL) return comp->error;
  return composition_missing_game;
}
static const char *dedicated_close(void *ctx, qa_app_application *application) {
  (void)application;
  composition_close(ctx);
  return NULL;
}
/* Nested graphical game: composition services belong to the
 * StartupApplication (TS borrowed client), so closing the game
 * must not tear them down; composition_close owns them. */
static const char *composition_game_close(void *ctx,
                                          qa_app_application *application) {
  (void)ctx;
  (void)application;
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

/* TS openApplication host: owned-path bundles are always present
 * (has_content over the composition catalog, has_simulation over
 * the concrete-builder shim). own_services selects whether closing
 * the application also closes the composition (dedicated main)
 * or leaves StartupApplication services alone (nested game). */
static qa_app_application_host composition_application_host(composition *comp,
                                                            bool own_services) {
  qa_app_application_host host = {0};
  host.ctx = comp;
  host.print = comp_print;
  host.save_directory = comp->save_directory;
  host.has_content = true;
  host.content = composition_content_host(comp);
  /* Session-driven stepping: application.c loads the simulation
   * over the shim bundle and steps the EngineSession, so the
   * legacy host.step phase is unused while a simulation is
   * attached; it remains as the error-surfacing fallback. */
  host.has_simulation = true;
  host.simulation = composition_simulation_host(comp);
  host.step = dedicated_step;
  host.open = dedicated_open;
  host.close = own_services ? dedicated_close : composition_game_close;
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

static qa_app_application_host composition_dedicated_host(composition *comp) {
  return composition_application_host(comp, true);
}

/* ---- SIGINT/SIGTERM -> requestQuit (TS main.ts stop handler) ---- */

static volatile sig_atomic_t composition_signal_quit;
static qa_app_application *composition_signal_application;

static qa_startup_application *composition_signal_startup;
static void composition_signal_handler(int signum) {
  (void)signum;
  composition_signal_quit = 1;
  if (composition_signal_application != NULL)
    qa_app_application_request_quit(composition_signal_application);
  if (composition_signal_startup != NULL)
    qa_startup_application_request_quit(composition_signal_startup);
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
  composition_signal_startup = NULL;
}

/* ---- graphical run/menu (TS main.ts StartupApplication path) ---- */
typedef struct startup_main {
  const qa_app_options *options;
  const char *entry;               /* "run" | "menu" */
  qa_llm_settings_service *llm;    /* NULL is TS undefined */
  qa_app_splash_screen **splash;
  qa_llm_command_requester requester;
  qa_startup_application_host host;
  qa_startup_application *application;
} startup_main;
/* TS host.llm as the LlmCommandRequester the console reads. */
static char *startup_llm_request(void *ctx, const qa_llm_request_input *input, char **out_error) {
  qa_llm_error error;
  memset(&error, 0, sizeof error);
  char *text = NULL;
  if (qa_llm_settings_request(ctx, input, &text, &error)) return text;
  *out_error = dup_text(error.message);
  return NULL;
}
static void startup_llm_prime(void *ctx, const char *instructions) {
  qa_llm_error error;
  memset(&error, 0, sizeof error);
  (void)qa_llm_settings_prime(ctx, instructions, &error);
}
static void startup_open_body(void *ctx) {
  startup_main *run = ctx;
  run->application = qa_startup_application_open(run->options, run->host, NULL, run->entry);
}
static void startup_run_body(void *ctx) { qa_startup_application_run(((startup_main *)ctx)->application); }
static void startup_close_body(void *ctx) { qa_startup_application_close(((startup_main *)ctx)->application); }
/* open, close the splash, SIGINT/SIGTERM -> requestQuit, run(), then close();
 * a close failure after a run failure is printed without replacing it (APP-006). */
static void composition_run_startup(startup_main *run, composition *comp) {
  memset(&run->host, 0, sizeof run->host);
  run->host.base.print = comp_print;
  if (run->llm != NULL) {
    run->requester.ctx = run->llm;
    run->requester.request = startup_llm_request;
    run->requester.prime = startup_llm_prime;
    run->host.base.llm_settings = run->llm;
    run->host.base.llm_requester = &run->requester;
  }
  qa_caught_error caught;
  if (!qa_try(startup_open_body, run, &caught)) {
    composition_set_error(comp, "%s", caught.message);
    return;
  }
  qa_app_splash_screen_close(*run->splash);
  *run->splash = NULL;
  struct sigaction old_int, old_term;
  composition_install_signals(&old_int, &old_term);
  composition_signal_startup = run->application;
  qa_caught_error run_error;
  bool failed = !qa_try(startup_run_body, run, &run_error);
  composition_restore_signals(&old_int, &old_term);
  if (failed) composition_set_error(comp, "%s", run_error.message);
  qa_caught_error close_error;
  if (!qa_try(startup_close_body, run, &close_error)) {
    if (!failed) composition_set_error(comp, "%s", close_error.message);
    else fprintf(stderr, "Shutdown after failure also failed: %s\n", close_error.message);
  }
  qa_startup_application_destroy(run->application);
  run->application = NULL;
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

/* The QC/QVM/native bodies live in weapon_behavior_tool.c and
 * call the landed content/compat/guest services directly over
 * the qa_content_mounted* from weapon_open_product; the host
 * run_* fields stay NULL (weapon_behavior_tool.h contract). */

/* ---- main ---- */

static bool parse_positive_integer(const char *text, double *out) {
  if (text == NULL || *text == '\0') return false;
  double value = qa_js_string_to_number(text); /* Number(argv[i]), then Number.isInteger and > 0 */
  if (!isfinite(value) || value != trunc(value) || value <= 0) return false;
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
    /* run_qc/run_qvm/run_native stay NULL: tool bodies are direct. */
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
      /* TS StartupApplication.open(command.options, host, undefined, command.kind),
       * then run() and close(). The application copies the options. */
      startup_main run;
      memset(&run, 0, sizeof run);
      run.options = &options;
      run.entry = composition_entry_for(command.kind);
      run.llm = llm;
      run.splash = &splash;
      composition_run_startup(&run, &comp);
      qa_app_options_destroy(&options);
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



