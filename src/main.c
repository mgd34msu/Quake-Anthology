/*
 * Port of src/main.ts. Command dispatch runs through the landed
 * options parser: --version/--help/--list-content and
 * --splash-child are fully wired; application execution (run/menu)
 * still needs the StartupApplication/Application composition
 * phases, which are host seams in the landed ports, so those kinds
 * fail explicitly after real parsing (tracked for reconciliation).
 * GPL-2.0-or-later.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
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

#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/splash.h"
#include "qa/app/bootstrap/startup_timing.h"
#include "qa/app/bootstrap/weapon_behavior_tool.h"
#include "qa/content/archive.h"
#include "qa/content/catalog/index.h"
#include "qa/content/user_data.h"
#include "qa/foundation/bounded_queue.h"
#include "qa/formats/images/png.h"
#include "qa/llm/settings.h"
#include "qa/platform/sdl.h"
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
    /* The tool's catalog/mount/inspect bodies are host seams in the
     * landed port; parsing above already validated the command. */
    const char *message =
        "weapon-behavior tool bodies are not yet wired "
        "(src/app/bootstrap/weapon-behavior-tool.ts host seams)";
    write_crash_log(message, argc, argv);
    fprintf(stderr, "%s\n", message);
    result = 1;
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
      bool dedicated_run =
          strcmp(command.kind, "menu") != 0 && command.options.dedicated;
      if (!dedicated_run) {
        qa_llm_settings_options llm_options = {0};
        llm_options.base_directory = llm_directory;
        llm_options.auth.open_browser = open_browser_url;
        llm = qa_llm_settings_open(&llm_options);
      }
    }
    qa_app_startup_stamp("llm-settings");
    const char *message =
        "application composition is not yet wired "
        "(StartupApplication/Application phase hosts; reconciliation)";
    write_crash_log(message, argc, argv);
    fprintf(stderr, "%s\n", message);
    if (llm != NULL) {
      qa_llm_settings_close(llm);
      qa_llm_settings_destroy(llm);
    }
    qa_app_splash_screen_close(splash);
    free(llm_directory);
    result = 1;
  }

  qa_app_application_command_destroy(&command);
  free(exe_dir);
  return result;
}



