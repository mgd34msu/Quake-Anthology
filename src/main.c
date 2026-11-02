/*
 * Port of src/main.ts. Command dispatch runs through the landed
 * options parser: --version/--help/--list-content and
 * --splash-child are fully wired. run/menu compose real phase
 * hosts for StartupApplication (graphical) and Application
 * (dedicated) over the landed catalog/settings/SDL/audio/input
 * services, the concrete StartupMenu, and the concrete
 * SharedSimulation builder; composition steps whose TS service
 * has only a host-seam C port fail with a named missing-service
 * error (crash.log + exit 1, as in TS main's catch) instead of
 * a stub.
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
#include "qa/app/bootstrap/startup_audio.h"
#include "qa/app/bootstrap/startup_menu.h"
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
  qa_identity_owner identity; /* TS StartupApplication identity/session */
  bool identity_open;
  qa_seat_id seat; /* primary local seat (index 0) */
  qa_catalog_mount_list mount_list; /* TS catalog.mountsFor(installed) */
  bool mount_list_open;
  qa_mount_id *mount_order;      /* owned plan defaultOrder storage */
  char *mount_plan_id;           /* owned */
  qa_open_mount_options mount_options;
  qa_content_mounted *mounted;   /* TS openMountPlan("startup","font") */
  qa_app_configuration_content *configuration; /* openInitialConfigurationContent */
  qa_sdl_window window;
  bool window_open;
  qa_sdl_window *active_window; /* renderer's window, when opened */
  qa_app_native_renderer *renderer; /* NativeRenderer over window */
  qa_sound_bank *bank;              /* SoundBank(mounted) */
  qa_app_startup_audio *startup_audio;
  qa_unified_audio *audio; /* borrowed engine of startup_audio, if any */
  qa_sdl_controllers *controllers;
  qa_native_ui_controller *ui;   /* borrowed StartupMenu controller */
  qa_app_startup_saves *saves;   /* StartupSaves over save_directory */
  qa_app_startup_menu *menu;     /* StartupMenu */
  qa_app_application *game;      /* TS pending/run-entry Application */
  bool pending_game;             /* StartupMenu play -> open game */
  double game_last_ms;           /* last qa_app_application_step stamp */
  bool game_clock_started;
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
  qa_app_local_accounts *accounts; /* localClientAccount registry */
  qa_online_account lobby_account; /* borrows registry id/name */
  qa_local_lobby_service *lobby_service; /* TS new LocalLobbyService() */
  qa_app_application_local_lobby lobby;
  bool lobby_open;
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

/* Remaining named gaps after the concrete wiring below. Content
 * (catalog product/require, preset + resolveLaunch, recipe mounts,
 * prepare_*), the Application identity/EngineSession composition
 * (application.c), the concrete SharedSimulation builder
 * (simulation/runtime qa_app_sim_shared_simulation_concrete_host,
 * built in dedicated_open over the composed options/actors/
 * session and reached through the composition shim host), the
 * graphical menu objects and the concrete StartupMenu are all
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
static const char composition_missing_remote[] =
    "Lobby join requires RemoteApplication composition: "
    "remote_application.h is host-seam only (open_services/connect/"
    "build_seats have no concrete C host) and qa_composition_identity "
    "carries no recipe for TS lobbyJoinOptions "
    "(src/app/bootstrap/startup.ts, "
    "src/app/bootstrap/remote-application.ts)";


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

/* ---- concrete bootstrap hosts over landed services ----
 *
 * TS openGraphics order (startup.ts): openInitialConfiguration-
 * Content, font mounts (catalog.mountsFor + openMountPlan),
 * NativeRenderer.open, SdlControllers.open, StartupAudio.open,
 * StartupMenu. The hosts below give those C modules real catalog,
 * mount, SDL-window, unified-audio and sound-bank services.
 * Hosts record comp->error and return NULL for the same
 * crash-log reason as the startup phases. */

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

static bool composition_configuration_product(void *ctx, const char *content_id,
                                              qa_game_family *family,
                                              const char **edition,
                                              const char **content_directory,
                                              const char **user_root,
                                              const char **base_product) {
  composition *comp = ctx;
  const qa_catalog_discovered_product *product =
      composition_product_for(comp, content_id);
  if (product == NULL) return false;
  *family = product->expectation.family;
  *edition = product->expectation.edition;
  *content_directory = product->expectation.content_directory;
  *user_root = product->user_content != NULL ? product->user_content->root : NULL;
  *base_product = product->expectation.base_product;
  return true;
}

static size_t composition_configuration_q1_products(void *ctx,
                                                    const char **directories,
                                                    const char **user_roots,
                                                    size_t capacity) {
  composition *comp = ctx;
  size_t count = 0;
  for (size_t i = 0; i < comp->catalog.product_count && count < capacity; i++) {
    const qa_catalog_discovered_product *product = &comp->catalog.products[i];
    if (product->expectation.family != QA_GAME_FAMILY_Q1) continue;
    if (product->expectation.edition != NULL &&
        strcmp(product->expectation.edition, "quakeworld") == 0) continue;
    directories[count] = product->expectation.content_directory;
    user_roots[count] =
        product->user_content != NULL ? product->user_content->root : NULL;
    count++;
  }
  return count;
}

/* TS catalog.mountsFor(product.id) + openMountPlan("startup","font")
 * for the graphical path; ("application", hex) shape is covered by
 * the same landed calls for dedicated content loading. */
static void composition_open_mounts(composition *comp,
                                    const qa_catalog_discovered_product *product) {
  if (comp->error != NULL || comp->mounted != NULL || product == NULL) return;
  if (!qa_content_catalog_mounts_for(&comp->catalog, product->id,
                                     &comp->mount_list)) {
    composition_set_error(comp, "Out of memory");
    return;
  }
  comp->mount_list_open = true;
  comp->mount_order = calloc(comp->mount_list.count > 0 ? comp->mount_list.count : 1,
                             sizeof *comp->mount_order);
  if (comp->mount_order == NULL) {
    composition_set_error(comp, "Out of memory");
    return;
  }
  for (size_t i = 0; i < comp->mount_list.count; i++) {
    const qa_content_mount *mount = &comp->mount_list.mounts[i];
    comp->mount_order[i] = mount->kind == QA_CONTENT_MOUNT_ARCHIVE
                               ? mount->as.archive.identity.id
                               : mount->as.loose.identity.id;
  }
  comp->mount_plan_id = qa_create_mount_plan_id("startup", "font");
  if (comp->mount_plan_id == NULL) {
    composition_set_error(comp, "Out of memory");
    return;
  }
  qa_resolved_mount_plan plan = {0};
  plan.id = comp->mount_plan_id;
  plan.mounts = comp->mount_list.mounts;
  plan.mount_count = comp->mount_list.count;
  plan.default_order = comp->mount_order;
  plan.default_order_count = comp->mount_list.count;
  comp->mounted = qa_open_mount_plan(&plan, &comp->mount_options);
  if (comp->mounted == NULL) composition_set_error(comp, "Out of memory");
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

static qa_app_configuration_host composition_configuration_host(
    composition *comp) {
  qa_app_configuration_host host = {0};
  host.ctx = comp;
  host.product = composition_configuration_product;
  host.q1_products = composition_configuration_q1_products;
  return host;
}

/* TS openInitialConfigurationContent(options, undefined, catalog). */
static void composition_open_configuration(composition *comp) {
  if (comp->error != NULL || comp->configuration != NULL) return;
  qa_app_options options = *comp->options; /* configuration.c borrows it */
  comp->configuration = qa_app_open_initial_configuration_content(
      composition_configuration_host(comp), composition_content_host(comp),
      options, NULL, false);
  if (comp->configuration == NULL && comp->error == NULL)
    composition_set_error(comp, "Out of memory");
}

/* Destroy the nested run-entry/lobby Application, if open. Its
 * host close does not touch composition services (borrowed-mode
 * sharing, as in TS Application.openBorrowed). */
static void composition_close_game(composition *comp) {
  if (comp->game == NULL) return;
  qa_app_application_destroy(comp->game);
  comp->game = NULL;
  comp->game_clock_started = false;
}

/* Ordered composition teardown (TS StartupApplication.close /
 * Application.closeOwned service order, landed subset). Idempotent;
 * comp->error stays owned by main. */
static void composition_close(composition *comp) {
  composition_close_game(comp);
  if (comp->lobby_open) {
    comp->lobby_open = false;
    (void)qa_app_application_local_lobby_close(&comp->lobby);
    qa_app_application_local_lobby_destroy(&comp->lobby);
  }
  qa_local_lobby_service_free(comp->lobby_service);
  comp->lobby_service = NULL;
  if (comp->accounts != NULL) {
    qa_app_local_accounts_destroy(comp->accounts);
    comp->accounts = NULL;
  }
  if (comp->menu != NULL) {
    qa_app_startup_menu_destroy(comp->menu); /* owns its controller */
    comp->menu = NULL;
    comp->ui = NULL; /* borrowed controller died with the menu */
  }
  if (comp->ui != NULL) { /* backstop when menu creation failed midway */
    qa_native_ui_controller_destroy(comp->ui);
    comp->ui = NULL;
  }
  if (comp->saves != NULL) {
    qa_app_startup_saves_destroy(comp->saves);
    comp->saves = NULL;
  }
  if (comp->startup_audio != NULL) {
    comp->audio = NULL; /* engine freed by startup-audio destroy */
    qa_app_startup_audio_destroy(comp->startup_audio);
    comp->startup_audio = NULL;
  }
  if (comp->audio != NULL) {
    qa_unified_audio_destroy(comp->audio);
    comp->audio = NULL;
  }
  if (comp->bank != NULL) {
    qa_sound_bank_destroy(comp->bank);
    comp->bank = NULL;
  }
  if (comp->controllers != NULL) {
    qa_sdl_controllers_close(comp->controllers);
    comp->controllers = NULL;
  }
  if (comp->renderer != NULL) {
    qa_app_native_renderer_destroy(comp->renderer); /* closes the window */
    comp->renderer = NULL;
    comp->active_window = NULL;
    comp->window_open = false;
  }
  if (comp->window_open) {
    comp->window_open = false;
    qa_sdl_window_close(&comp->window);
  }
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
  if (comp->identity_open) {
    comp->identity_open = false;
    qa_identity_owner_destroy(&comp->identity);
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
  composition *comp = ctx;
  composition_select_product(comp, false);
  if (comp->error != NULL) return NULL;
  /* TS constructor identity/session + primary seat, then
   * openInitialConfigurationContent and the font mount plan. */
  if (!qa_identity_owner_create(&comp->identity, "startup")) {
    composition_set_error(comp, "Out of memory");
    return NULL;
  }
  comp->identity_open = true;
  comp->seat = qa_identity_owner_seat(&comp->identity, 0);
  composition_open_mounts(comp, comp->installed);
  composition_open_configuration(comp);
  return NULL;
}

static const char *comp_phase_renderer(void *ctx) {
  composition *comp = ctx;
  if (comp->error != NULL) return NULL;
  /* Concrete SDL window + cpu/gl backend factory from renderer.c
   * (qa_app_open_native_backend_bundle, TS openBackend). */
  qa_app_native_renderer_host host = {0};
  host.open_bundle = qa_app_open_native_backend_bundle;
  qa_renderer_resource_owner owner = {0};
  owner.identity = comp; /* unique live address = TS owner symbol */
  owner.session = comp->identity.session;
  comp->renderer = qa_app_native_renderer_open(
      host, comp->renderer_kind != NULL ? comp->renderer_kind : "cpu",
      comp->options->width, comp->options->height, comp->options->hidden,
      comp->options->gamma, false, owner, NULL);
  if (comp->renderer == NULL && comp->error == NULL)
    composition_set_error(comp, "Out of memory");
  if (comp->renderer != NULL)
    comp->active_window = qa_app_native_renderer_window(comp->renderer);
  return NULL;
}
static double comp_audio_now(void *ctx) {
  (void)ctx;
  /* TS StartupAudio: milliseconds = trunc(performance.now()) --
   * process-relative integer ms (the mixer allocation clock
   * rejects fractional or out-of-int32 times). */
  static double epoch = -1;
  double now = splash_now_ms(NULL);
  if (epoch < 0) epoch = now;
  return (double)(int64_t)(now - epoch);
}
static double comp_audio_random(void *ctx) {
  composition *comp = ctx;
  comp->random_state = comp->random_state * 1664525u + 1013904223u;
  return (double)(comp->random_state >> 8) / 16777216.0;
}
static void comp_print(void *ctx, const char *text); /* defined below */

/* StartupAudio host over the landed unified audio engine. */
static qa_sound_family comp_sound_family(const char *family) {
  qa_sound_family parsed = QA_SOUND_FAMILY_Q1;
  if (family != NULL) (void)qa_sound_family_parse(family, &parsed);
  return parsed;
}
static qa_unified_audio *comp_audio_create_engine(
    void *ctx, const qa_app_startup_audio_preferences *preferences) {
  composition *comp = ctx;
  qa_unified_audio_options options = {0};
  options.milliseconds = comp_audio_now;
  options.random = comp_audio_random;
  options.random_ctx = comp;
  if (preferences->has_output_format) {
    options.has_output_format = true;
    options.output_format = preferences->output_format;
  }
  qa_unified_audio *engine = qa_unified_audio_create(&options);
  if (engine == NULL) qa_range_error("Out of memory");
  return engine;
}
static void comp_audio_destroy_engine(void *ctx, qa_unified_audio *engine) {
  (void)ctx;
  qa_unified_audio_destroy(engine);
}
static qa_audio_output_format comp_audio_output_format(void *ctx,
                                                       qa_unified_audio *engine) {
  (void)ctx;
  return qa_unified_audio_output_format(engine);
}
static const void *comp_audio_selected_output(void *ctx,
                                              qa_unified_audio *engine) {
  (void)ctx;
  return qa_unified_audio_selected_output(engine);
}
static void comp_audio_select_output(void *ctx, qa_unified_audio *engine,
                                     const void *output,
                                     qa_audio_output_format format, bool restart) {
  (void)ctx;
  (void)qa_unified_audio_select_output(engine, (const char *)output, &format,
                                       restart);
}
static void comp_audio_set_listeners(void *ctx, qa_unified_audio *engine,
                                     qa_seat_id seat) {
  (void)ctx;
  qa_audio_listener listener = {0};
  listener.seat = seat;
  listener.gain = 1;
  qa_unified_audio_set_listeners(engine, &listener, 1);
}
static void comp_audio_set_effects_volume(void *ctx, qa_unified_audio *engine,
                                          double volume) {
  (void)ctx;
  qa_unified_audio_set_effects_volume(engine, volume);
}
static void comp_audio_open_device(void *ctx, qa_unified_audio *engine,
                                   const char *device_name, bool *out_failed,
                                   const char **out_error) {
  (void)ctx;
  qa_sdl_audio_open_result result =
      qa_unified_audio_open_device(engine, device_name, 0);
  *out_failed = result != QA_SDL_AUDIO_OPEN_OK;
  *out_error = *out_failed ? "Audio output unavailable" : NULL;
}
static void comp_audio_play_ui(void *ctx, qa_unified_audio *engine,
                               const void *sound, const char *family,
                               qa_seat_id seat) {
  (void)ctx;
  qa_play_sound request = {0};
  request.sound = (const qa_sound_asset *)sound;
  request.family = comp_sound_family(family);
  request.origin.kind = QA_SOUND_ORIGIN_LOCAL;
  request.audience.kind = QA_AUDIO_AUDIENCE_SEAT;
  request.audience.seat = seat;
  request.volume = 1;
  (void)qa_unified_audio_play(engine, &request);
}
static void comp_audio_update_music(void *ctx, qa_unified_audio *engine) {
  (void)ctx;
  qa_unified_audio_update_music(engine);
}
static void comp_audio_pump(void *ctx, qa_unified_audio *engine) {
  (void)ctx;
  (void)qa_unified_audio_pump(engine, false, 0, 0);
}
static void comp_audio_close_engine(void *ctx, qa_unified_audio *engine) {
  (void)ctx;
  qa_unified_audio_close(engine);
}
static const void *comp_audio_bank_register(void *ctx, const char *family,
                                            const char *event) {
  composition *comp = ctx;
  if (comp->bank == NULL) return NULL;
  qa_sound_family parsed = comp_sound_family(family);
  const qa_sound_asset *asset = NULL;
  if (!qa_sound_bank_register(comp->bank,
                              qa_app_menu_sound_path(
                                  comp->installed != NULL
                                      ? comp->installed->expectation.family
                                      : QA_GAME_FAMILY_Q1,
                                  event),
                              parsed, &asset))
    return NULL;
  return asset;
}
static void comp_audio_bank_clear(void *ctx) {
  composition *comp = ctx;
  if (comp->bank != NULL) qa_sound_bank_clear(comp->bank);
}

static const char *comp_phase_audio(void *ctx) {
  composition *comp = ctx;
  if (comp->error != NULL) return NULL;
  comp->bank = qa_sound_bank_create(comp->mounted);
  if (comp->bank == NULL) {
    composition_set_error(comp, "Out of memory");
    return NULL;
  }
  qa_app_startup_audio_host host = {0};
  host.ctx = comp;
  host.create_engine = comp_audio_create_engine;
  host.destroy_engine = comp_audio_destroy_engine;
  host.engine_output_format = comp_audio_output_format;
  host.engine_selected_output = comp_audio_selected_output;
  host.engine_select_output = comp_audio_select_output;
  host.engine_set_listeners = comp_audio_set_listeners;
  host.engine_set_effects_volume = comp_audio_set_effects_volume;
  host.engine_open_device = comp_audio_open_device;
  host.engine_play_ui = comp_audio_play_ui;
  host.engine_update_music = comp_audio_update_music;
  host.engine_pump = comp_audio_pump;
  host.engine_close = comp_audio_close_engine;
  host.bank_register = comp_audio_bank_register;
  host.bank_clear = comp_audio_bank_clear;
  qa_app_startup_audio_options options = {0};
  options.family = qa_game_family_name(comp->installed->expectation.family);
  options.source.content = comp->installed->id;
  options.source.family = comp->installed->expectation.family;
  options.source.edition = comp->installed->expectation.edition;
  options.source.campaign = comp->installed->expectation.campaign;
  options.bank = comp->bank;
  options.mounts = comp->mounted;
  options.seat = comp->seat;
  options.print = comp_print;
  options.print_ctx = comp;
  comp->startup_audio = qa_app_startup_audio_open(host, options);
  if (comp->startup_audio == NULL) {
    composition_set_error(comp, "Out of memory");
    return NULL;
  }
  comp->audio = qa_app_startup_audio_engine(comp->startup_audio);
  /* TS audio.openOutput(preferences.audioBaseline.deviceName ?? null). */
  qa_app_startup_audio_open_output(comp->startup_audio, NULL, comp_print, comp);
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
/* Concrete StartupMenu (qa_app_startup_menu_new, TS `new
 * StartupMenu`): it owns its NativeUiController and registers the
 * real menu tree, so the legacy host-delegation gate is gone.
 * Model callbacks (presets/rows/select/monsters/hosting) stay
 * unset: the C StartupSelectionModel is itself host-seam
 * (startup_selection.h presets/rows/resolve), and the concrete
 * menu treats absent model rows as TS safeRows()=[]. That model
 * composition is the remaining named menu gap, not faked here.
 * Saves, sound, play and quit are wired to real services below. */
static void composition_open_game(composition *comp); /* below */

static double comp_menu_measure(void *ctx, const char *text, double scale) {
  (void)ctx;
  return (double)strlen(text) * 8 * scale;
}
static void comp_menu_sound(void *ctx, qa_ui_sound sound) {
  composition *comp = ctx;
  if (comp->startup_audio != NULL) qa_app_startup_audio_sound(comp->startup_audio, sound);
}
static const qa_app_startup_save_list *comp_menu_saves(void *ctx) {
  composition *comp = ctx;
  return comp->saves != NULL ? qa_app_startup_saves_list(comp->saves) : NULL;
}
static void comp_menu_refresh_saves(void *ctx) {
  composition *comp = ctx;
  if (comp->saves != NULL) qa_app_startup_saves_refresh(comp->saves);
}
static void comp_menu_quit(void *ctx) {
  ((composition *)ctx)->quit_requested = true;
}
static void comp_menu_play(void *ctx) {
  /* TS play sets pending {kind:"play"}; comp_pump_frame stages it. */
  ((composition *)ctx)->pending_game = true;
}

static void comp_menu_inspect_save(void *ctx, const char *path,
                                   qa_app_startup_save_detail *detail) {
  (void)ctx;
  (void)path;
  (void)detail; /* seeds kept: rows report unavailable until save parsing wires */
}
static void comp_menu_prepare_saves(void *ctx) {
  composition *comp = ctx;
  qa_app_startup_saves_host host = {0};
  host.ctx = comp;
  host.inspect_pak = comp_menu_inspect_save;
  host.inspect_sav = comp_menu_inspect_save;
  comp->saves = qa_app_startup_saves_create(host, comp->save_directory, NULL, 0);
  if (comp->saves == NULL) composition_set_error(comp, "Out of memory");
}


static const char *comp_phase_menu_or_game(void *ctx, bool run_entry) {
  composition *comp = ctx;
  if (comp->error != NULL) return NULL;
  if (run_entry) {
    /* TS entry "run": openGraphics stages pending {kind:"initial"}
     * and advance() opens the game Application over the loaded
     * content (Application.openBorrowed in TS; the C owned-path
     * qa_app_open_application composition here). */
    composition_open_game(comp);
    return NULL;
  }
  comp_menu_prepare_saves(comp);
  if (comp->error != NULL) return NULL;
  qa_app_startup_menu_options options = {0};
  options.ctx = comp;
  options.seat = comp->seat;
  options.skin_font = "resource:startup:font";
  options.now = splash_now_ms;
  options.sound = comp_menu_sound;
  options.measure = comp_menu_measure;
  options.play = comp_menu_play;
  options.saves = comp_menu_saves;
  options.refresh_saves = comp_menu_refresh_saves;
  options.quit = comp_menu_quit;
  comp->menu = qa_app_startup_menu_new(options);
  if (comp->menu == NULL && comp->error == NULL)
    composition_set_error(comp, "Out of memory");
  if (comp->menu != NULL) {
    comp->ui = qa_app_startup_menu_controller(comp->menu); /* borrowed */
    (void)qa_app_startup_menu_open(comp->menu); /* ensures main menu */
  }
  return NULL;
}

/* Local lobby: account registry + ApplicationLocalLobby are real;
 * transitions mirror TS startup.ts (host/join launch a game, so
 * they keep the named Application gap; leave/completed match TS).
 * The phase stops at the LocalLobbyService allocator gap. */
static void comp_random_bytes(void *ctx, unsigned char out[16]) {
  composition *comp = ctx;
  FILE *file = fopen("/dev/urandom", "rb");
  if (file != NULL) {
    size_t got = fread(out, 1, 16, file);
    (void)fclose(file);
    if (got == 16) return;
  }
  for (int i = 0; i < 16; i++) {
    comp->random_state = comp->random_state * 1664525u + 1013904223u;
    out[i] = (unsigned char)(comp->random_state >> 24);
  }
}
static const char *comp_lobby_host(void *ctx, const qa_lobby *lobby,
                                   qa_net_address *endpoint_out,
                                   qa_wire_selection *wire_out) {
  (void)lobby;
  (void)endpoint_out;
  (void)wire_out;
  composition *comp = ctx;
  /* TS host: stageLobbyLaunch({kind:"lobby-host"}) opens the game
   * Application, then publishes game.networkAddress/networkWire.
   * The C Application composition opens for real here; it exposes
   * no listening-server accessor (application.h), so the publish
   * step fails with TS's own message and the staged game closes. */
  if (comp->selected == NULL)
    return "Select a game before starting the lobby";
  composition_open_game(comp);
  if (comp->error != NULL) return comp->error;
  composition_close_game(comp);
  return "The selected game did not open a listening server";
}
static const char *comp_lobby_join(void *ctx, const qa_lobby *lobby) {
  composition *comp = ctx;
  /* TS lobbyJoinOptions guards that are representable in C, in
   * order; the launch itself is a RemoteApplication (below). */
  if (lobby->has_endpoint && lobby->endpoint.kind == QA_NET_LOOPBACK)
    return "This lobby endpoint requires an in-process transport";
  bool member = false;
  for (size_t i = 0; i < lobby->member_count; i++)
    if (lobby->members[i].account.id != NULL &&
        comp->lobby_account.id != NULL &&
        strcmp(lobby->members[i].account.id, comp->lobby_account.id) == 0)
      member = true;
  if (!member) return "The local client is not a member of this lobby";
  return composition_missing_remote;
}
static const char *comp_lobby_leave(void *ctx, const qa_lobby *lobby) {
  (void)lobby;
  composition *comp = ctx;
  /* TS leave re-stages the frontend when a lobby source is live. */
  composition_close_game(comp);
  return NULL;
}
static const char *comp_lobby_completed(void *ctx, const qa_lobby *lobby) {
  (void)ctx;
  (void)lobby;
  return NULL; /* TS completed: async () => undefined */
}
static const char *comp_phase_lobby(void *ctx) {
  composition *comp = ctx;
  if (comp->error != NULL) return NULL;
  comp->accounts = qa_app_local_accounts_create(comp_random_bytes, comp);
  if (comp->accounts == NULL) {
    composition_set_error(comp, "Out of memory");
    return NULL;
  }
  const qa_app_local_account *account =
      qa_app_local_client_account(comp->accounts, &comp->identity);
  comp->lobby_account.id = account->id;
  comp->lobby_account.name = account->name;
  qa_app_local_lobby_transitions transitions = {0};
  transitions.ctx = comp;
  transitions.host = comp_lobby_host;
  transitions.join = comp_lobby_join;
  transitions.leave = comp_lobby_leave;
  transitions.completed = comp_lobby_completed;
  /* host.localLobbies ?? new LocalLobbyService() */
  comp->lobby_service = qa_local_lobby_service_new();
  if (comp->lobby_service == NULL) {
    composition_set_error(comp, "Out of memory");
    return NULL;
  }
  qa_app_application_local_lobby_init(&comp->lobby, comp->lobby_service,
                                      &comp->lobby_account, transitions);
  comp->lobby_open = true;
  return NULL;
}
static const char *comp_pump_frame(void *ctx) {
  composition *comp = ctx;
  qa_sdl_window *window = comp->active_window != NULL
                              ? comp->active_window
                              : (comp->window_open ? &comp->window : NULL);
  if (window != NULL) {
    size_t count = 0;
    qa_sdl_event *events = qa_sdl_window_poll_events(window, &count);
    for (size_t i = 0; i < count; i++)
      if (events[i].kind == QA_SDL_EVENT_QUIT) comp->quit_requested = true;
    free(events);
    if (qa_sdl_window_closed(window)) comp->quit_requested = true;
  }
  if (comp->startup_audio != NULL) {
    qa_app_startup_audio_pump(comp->startup_audio);
  } else if (comp->audio != NULL) {
    qa_unified_audio_update_music(comp->audio);
    (void)qa_unified_audio_pump(comp->audio, false, 0, 0);
  }
  if (comp->menu != NULL) qa_app_startup_menu_pump(comp->menu);
  /* TS advance(): a staged play/initial source opens, then the
   * live game steps with real elapsed time through its session
   * (qa_app_application_step; host.step is unused once a
   * simulation is attached). A finished run-entry game finishes
   * the startup; a finished menu game returns to the frontend. */
  if (comp->pending_game && comp->game == NULL) {
    comp->pending_game = false;
    composition_open_game(comp);
  }
  if (comp->game != NULL && comp->error == NULL) {
    double now = splash_now_ms(NULL);
    if (!comp->game_clock_started) {
      comp->game_clock_started = true;
      comp->game_last_ms = now;
    } else {
      double elapsed = now - comp->game_last_ms;
      comp->game_last_ms = now;
      if (elapsed > 0) {
        const void *output = NULL;
        const char *error =
            qa_app_application_step(comp->game, elapsed, &output);
        if (error != NULL) composition_set_error(comp, "%s", error);
      }
    }
    if (comp->game != NULL && qa_app_application_finished(comp->game)) {
      bool run_entry = comp->kind != NULL && strcmp(comp->kind, "run") == 0;
      composition_close_game(comp);
      if (run_entry) comp->quit_requested = true;
    }
  }
  if (comp->lobby_open) {
    const char *lobby_error = qa_app_application_local_lobby_poll(&comp->lobby);
    if (lobby_error != NULL) composition_set_error(comp, "%s", lobby_error);
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
    if (recipe->execution[i].kind != QA_EXECUTION_MODULE_TYPESCRIPT) {
      composition_set_error(comp,
                            "Concrete SharedSimulation does not construct "
                            "the source game for execution module %s "
                            "(src/app/bootstrap/simulation/runtime.ts: "
                            "QuakeC/QVM/native guests are not constructed)",
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
  if (simulation->options.world == NULL) {
    /* From mounts the concrete builder decodes Q1 BSP only; Q2/Q3
     * need the decoded world through options.world, which
     * application.c compose_world does not populate. */
    const qa_catalog_discovered_product *geometry_product =
        composition_find_product(&comp->catalog, recipe->map.geometry_content);
    if (geometry_product == NULL ||
        geometry_product->expectation.family != QA_GAME_FAMILY_Q1 ||
        simulation->options.mounts == NULL) {
      composition_set_error(comp,
                            "Concrete SharedSimulation requires a decoded "
                            "world for this map: qa_app_simulation_options.world "
                            "is not populated by Application compose_world "
                            "from the loaded content, and only Q1 geometry "
                            "decodes from mounts");
      return NULL;
    }
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

/* Deep copy of the owned option strings/arrays (the field set
 * qa_app_options_destroy releases), so a nested game Application
 * can own its options while the StartupApplication keeps the
 * originals (TS shares the immutable options object). False only
 * on OOM; a partially built clone is destroyed before returning. */
static bool composition_clone_options(const qa_app_options *in,
                                      qa_app_options *out) {
  *out = *in;
  out->network_transport_relay = dup_text(in->network_transport_relay);
  out->startup_commands = NULL;
  out->remote_content.directory = dup_text(in->remote_content.directory);
  out->server_profile_path = dup_text(in->server_profile_path);
  out->corpus_root = dup_text(in->corpus_root);
  out->user_content_root = dup_text(in->user_content_root);
  out->addon_content_roots = NULL;
  out->product = dup_text(in->product);
  out->map_product = dup_text(in->map_product);
  out->map = dup_text(in->map);
  out->quakec_program = dup_text(in->quakec_program);
  out->q2_game_library = dup_text(in->q2_game_library);
  out->weapon_behavior_product = dup_text(in->weapon_behavior_product);
  out->weapon_behavior_id = dup_text(in->weapon_behavior_id);
  out->mods = NULL;
  out->movement_product = dup_text(in->movement_product);
  out->character_model = dup_text(in->character_model);
  out->character_skin = dup_text(in->character_skin);
  out->network_host = dup_text(in->network_host);
  out->network_remote = dup_text(in->network_remote);
  bool ok = (in->network_transport_relay == NULL || out->network_transport_relay != NULL) &&
      (in->remote_content.directory == NULL || out->remote_content.directory != NULL) &&
      (in->server_profile_path == NULL || out->server_profile_path != NULL) &&
      (in->corpus_root == NULL || out->corpus_root != NULL) &&
      (in->user_content_root == NULL || out->user_content_root != NULL) &&
      (in->product == NULL || out->product != NULL) &&
      (in->map_product == NULL || out->map_product != NULL) &&
      (in->map == NULL || out->map != NULL) &&
      (in->quakec_program == NULL || out->quakec_program != NULL) &&
      (in->q2_game_library == NULL || out->q2_game_library != NULL) &&
      (in->weapon_behavior_product == NULL || out->weapon_behavior_product != NULL) &&
      (in->weapon_behavior_id == NULL || out->weapon_behavior_id != NULL) &&
      (in->movement_product == NULL || out->movement_product != NULL) &&
      (in->character_model == NULL || out->character_model != NULL) &&
      (in->character_skin == NULL || out->character_skin != NULL) &&
      (in->network_host == NULL || out->network_host != NULL) &&
      (in->network_remote == NULL || out->network_remote != NULL);
  if (ok && in->startup_command_count > 0) {
    out->startup_commands =
        calloc(in->startup_command_count, sizeof *out->startup_commands);
    ok = out->startup_commands != NULL;
    for (size_t i = 0; ok && i < in->startup_command_count; i++) {
      out->startup_commands[i] = dup_text(in->startup_commands[i]);
      ok = in->startup_commands[i] == NULL || out->startup_commands[i] != NULL;
      if (!ok) out->startup_command_count = i;
    }
  }
  if (ok && in->addon_content_root_count > 0) {
    out->addon_content_roots =
        calloc(in->addon_content_root_count, sizeof *out->addon_content_roots);
    ok = out->addon_content_roots != NULL;
    for (size_t i = 0; ok && i < in->addon_content_root_count; i++) {
      out->addon_content_roots[i].family = in->addon_content_roots[i].family;
      out->addon_content_roots[i].root = dup_text(in->addon_content_roots[i].root);
      ok = in->addon_content_roots[i].root == NULL ||
           out->addon_content_roots[i].root != NULL;
      if (!ok) out->addon_content_root_count = i + 1;
    }
  }
  if (ok && in->mod_count > 0) {
    out->mods = calloc(in->mod_count, sizeof *out->mods);
    ok = out->mods != NULL;
    for (size_t i = 0; ok && i < in->mod_count; i++) {
      out->mods[i].product = dup_text(in->mods[i].product);
      out->mods[i].id = dup_text(in->mods[i].id);
      ok = (in->mods[i].product == NULL || out->mods[i].product != NULL) &&
           (in->mods[i].id == NULL || out->mods[i].id != NULL);
      if (!ok) out->mod_count = i + 1;
    }
  }
  if (!ok) qa_app_options_destroy(out);
  return ok;
}

/* TS stageLobbyLaunch/advance game open: qa_app_open_application
 * over a cloned options set (the nested Application owns them)
 * with the shared composition bundles. Failures stay in
 * comp->error (first error wins); a failed open is destroyed by
 * composition_close_game during composition_close. */
static void composition_open_game(composition *comp) {
  if (comp->error != NULL || comp->game != NULL) return;
  qa_app_options cloned;
  if (!composition_clone_options(comp->options, &cloned)) {
    composition_set_error(comp, "Out of memory");
    return;
  }
  comp->game = qa_app_open_application(
      cloned, composition_application_host(comp, false), NULL, false, false,
      (qa_app_frontend_preference_overrides){0});
  if (comp->game == NULL) {
    qa_app_options_destroy(&cloned); /* open did not take ownership */
    if (comp->error == NULL) composition_set_error(comp, "Out of memory");
  }
  comp->game_clock_started = false;
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

/* The QC/QVM/native bodies live in weapon_behavior_tool.c and
 * call the landed content/compat/guest services directly over
 * the qa_content_mounted* from weapon_open_product; the host
 * run_* fields stay NULL (weapon_behavior_tool.h contract). */

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



