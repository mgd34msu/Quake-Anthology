/* Port of src/app/bootstrap/addon-library.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/addon_library.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/persistence/value.h"
const char qa_app_addon_library_stop_label[] = "Cancel download";
const char qa_app_addon_library_create_label[] = "Import Quake ZIP";
typedef struct { char *directory, *group, *sha256, *title, *start; } installed_t;
struct qa_app_addon_library {
  qa_app_addon_library_options options;
  qa_app_addon_package *catalog; size_t catalog_count;
  char **catalog_strings; size_t catalog_string_count;
  installed_t *installed; size_t installed_count;
  const qa_app_addon_package *selected;
  const installed_t *selected_local;
  char *message;
  bool pending, closed, cancelled;
};
static char *dup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
const char *qa_app_addon_content_parent(const char *edition) {
  return edition && strcmp(edition, "rerelease") == 0 ? "q1/rerelease" : "q1";
}
char *qa_app_addon_product(const char *edition, const char *directory) {
  size_t cap = strlen(directory) + 32;
  char *product = malloc(cap);
  snprintf(product, cap, "q1-%s-%s", edition ? edition : "classic", directory);
  return product;
}
qa_app_addon_library *qa_app_addon_library_create(qa_app_addon_library_options options) {
  qa_app_addon_library *library = calloc(1, sizeof *library);
  if (!library) return NULL;
  library->options = options;
  library->message = dup("Quaddicted — community Quake add-ons");
  return library;
}
static void free_installed(qa_app_addon_library *library) {
  for (size_t i = 0; i < library->installed_count; i++) {
    free(library->installed[i].directory); free(library->installed[i].group);
    free(library->installed[i].sha256); free(library->installed[i].title); free(library->installed[i].start);
  }
  free(library->installed);
  library->installed = NULL;
  library->installed_count = 0;
  library->selected_local = NULL;
}
void qa_app_addon_library_destroy(qa_app_addon_library *library) {
  if (!library) return;
  free_installed(library);
  free(library->catalog);
  for (size_t i = 0; i < library->catalog_string_count; i++) free(library->catalog_strings[i]);
  free(library->catalog_strings);
  free(library->message);
  free(library);
}
static void set_message(qa_app_addon_library *library, const char *text) {
  free(library->message);
  library->message = dup(text);
}
char *qa_app_addon_library_scope(const qa_app_addon_library *library) {
  char buf[4096];
  if (library->selected_local) snprintf(buf, sizeof buf, "installed:%s", library->selected_local->directory);
  else if (library->selected) snprintf(buf, sizeof buf, "package:%s", library->selected->sha256);
  else snprintf(buf, sizeof buf, "catalog");
  return dup(buf);
}
const char *qa_app_addon_library_status(const qa_app_addon_library *library) { return library->message; }
static const installed_t *find_installed_group(const qa_app_addon_library *library, const char *group) {
  for (size_t i = 0; i < library->installed_count; i++)
    if (strcmp(library->installed[i].group, group) == 0) return &library->installed[i];
  return NULL;
}
static void push_entry(qa_app_library_entry **entries, size_t *count, size_t *cap,
                       const char *id, const char *label, const char *detail, const char *unavailable) {
  if (*count == *cap) { *cap = *cap ? *cap * 2 : 16; *entries = realloc(*entries, *cap * sizeof **entries); }
  (*entries)[(*count)++] = (qa_app_library_entry){dup(id), dup(label), dup(detail), dup(unavailable)};
}
qa_app_library_entry *qa_app_addon_library_entries(qa_app_addon_library *library, size_t *out_count) {
  qa_app_library_entry *entries = NULL;
  size_t count = 0, cap = 0;
  const installed_t *local = library->selected_local;
  if (local) {
    push_entry(&entries, &count, &cap, "local-play", "Play", local->title, NULL);
    push_entry(&entries, &count, &cap, "local-remove", "Remove installed add-on", local->title, NULL);
    push_entry(&entries, &count, &cap, "back", "Back to add-ons", NULL, NULL);
    *out_count = count;
    return entries;
  }
  const qa_app_addon_package *selected = library->selected;
  if (selected) {
    const installed_t *installed = find_installed_group(library, selected->group);
    if (installed) {
      push_entry(&entries, &count, &cap, "play", "Play", installed->title, NULL);
      push_entry(&entries, &count, &cap, "remove", "Remove installed add-on", "Only this managed copy", NULL);
    }
    if (!installed || strcmp(installed->sha256, selected->sha256) != 0) {
      char detail[4096];
      snprintf(detail, sizeof detail, "%s — %.0f KiB", selected->title, ceil(selected->bytes / 1024));
      push_entry(&entries, &count, &cap, "install", installed ? "Update" : "Install", detail, selected->unavailable);
    }
    push_entry(&entries, &count, &cap, "back", "Back to add-ons", NULL, NULL);
    *out_count = count;
    return entries;
  }
  for (size_t i = 0; i < library->catalog_count; i++) {
    bool is_installed = find_installed_group(library, library->catalog[i].group) != NULL;
    push_entry(&entries, &count, &cap, library->catalog[i].sha256, library->catalog[i].title, is_installed ? "Installed" : "Quaddicted", NULL);
  }
  for (size_t i = 0; i < library->installed_count; i++) {
    bool listed = false;
    for (size_t j = 0; j < count; j++) if (strcmp(entries[j].id, library->installed[i].sha256) == 0) listed = true;
    if (listed) continue;
    char id[8192];
    snprintf(id, sizeof id, "local:%s", library->installed[i].directory);
    /* TS unshifts each unlisted installed add-on. */
    qa_app_library_entry *shifted = malloc((count + 1) * sizeof *shifted);
    shifted[0] = (qa_app_library_entry){dup(id), dup(library->installed[i].title), dup("Installed — play"), NULL};
    memcpy(shifted + 1, entries, count * sizeof *entries);
    free(entries);
    entries = shifted;
    count++;
    cap = count;
  }
  *out_count = count;
  return entries;
}
void qa_app_library_entries_free(qa_app_library_entry *entries, size_t count) {
  if (!entries) return;
  for (size_t i = 0; i < count; i++) {
    free(entries[i].id); free(entries[i].label); free(entries[i].detail); free(entries[i].unavailable);
  }
  free(entries);
}
static bool managed_name(const char *name) {
  if (strncmp(name, "qd_", 3) != 0) return false;
  size_t i = 3;
  int hex = 0;
  while (name[i] && ((name[i] >= 'a' && name[i] <= 'f') || (name[i] >= '0' && name[i] <= '9'))) { i++; hex++; }
  if (hex != 20 || name[i] != '_') return false;
  i++;
  hex = 0;
  while (name[i] && ((name[i] >= 'a' && name[i] <= 'f') || (name[i] >= '0' && name[i] <= '9'))) { i++; hex++; }
  return hex == 20 && name[i] == '\0';
}
static void read_installed(qa_app_addon_library *library) {
  free_installed(library);
  char root[8192];
  snprintf(root, sizeof root, "%s/%s", library->options.root, qa_app_addon_content_parent(library->options.edition));
  /* ensureDirectory: recursive mkdir of the content parent. */
  char make[8192];
  snprintf(make, sizeof make, "%s", library->options.root);
  mkdir(make, 0777);
  const char *parent = qa_app_addon_content_parent(library->options.edition);
  char *copy = dup(parent);
  for (char *part = strtok(copy, "/"); part; part = strtok(NULL, "/")) {
    strncat(make, "/", sizeof make - strlen(make) - 1);
    strncat(make, part, sizeof make - strlen(make) - 1);
    mkdir(make, 0777);
  }
  free(copy);
  DIR *dir = opendir(root);
  if (!dir) return;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (!managed_name(entry->d_name)) continue;
    char disk[16384];
    snprintf(disk, sizeof disk, "%s/%s", root, entry->d_name);
    struct stat st;
    if (stat(disk, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    char relative[8192];
    snprintf(relative, sizeof relative, "%s/%s", parent, entry->d_name);
    if (library->options.host.hidden && library->options.host.hidden(library->options.host.ctx, relative)) continue;
    char file_path[16384];
    snprintf(file_path, sizeof file_path, "%s/%s/.quaddicted.json", root, entry->d_name);
    FILE *file = fopen(file_path, "rb");
    if (!file) continue;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    size_t read = size > 0 ? fread(text, 1, (size_t)size, file) : 0;
    text[read] = '\0';
    fclose(file);
    qa_save_value *value = qa_parse_json_value((const uint8_t *)text, read, true);
    free(text);
    if (!value) continue;
    const qa_save_value *sha = qa_save_value_get(value, "sha256");
    const qa_save_value *title = qa_save_value_get(value, "title");
    const qa_save_value *group = qa_save_value_get(value, "group");
    const qa_save_value *start = qa_save_value_get(value, "start");
    bool sha_ok = sha && sha->kind == QA_SAVE_STRING && strlen(sha->text) == 64;
    if (sha_ok) for (const char *p = sha->text; *p; p++) if (!((*p >= 'a' && *p <= 'f') || (*p >= '0' && *p <= '9'))) sha_ok = false;
    bool start_ok = start && (start->kind == QA_SAVE_NULL || start->kind == QA_SAVE_STRING);
    if (sha_ok && title && title->kind == QA_SAVE_STRING && group && group->kind == QA_SAVE_STRING && start_ok) {
      library->installed = realloc(library->installed, (library->installed_count + 1) * sizeof *library->installed);
      library->installed[library->installed_count++] = (installed_t){dup(entry->d_name), dup(group->text), dup(sha->text),
          dup(title->text), start->kind == QA_SAVE_STRING ? dup(start->text) : NULL};
    }
    qa_save_value_free(value);
  }
  closedir(dir);
}
typedef const char *(*operation_fn)(qa_app_addon_library *library, void *ctx);
static void run_operation(qa_app_addon_library *library, operation_fn operation, void *ctx) {
  if (library->closed || library->pending) return;
  library->cancelled = false;
  library->pending = true;
  const char *error = operation(library, ctx);
  library->pending = false;
  if (error && !library->closed) set_message(library, error);
}
static const char *refresh_operation(qa_app_addon_library *library, void *unused) {
  (void)unused;
  read_installed(library);
  set_message(library, "Reading Quaddicted catalog…");
  qa_app_addon_package *packages = NULL;
  size_t count = 0;
  bool used_cache = false;
  const char *error = library->options.host.refresh_catalog(library->options.host.ctx, &packages, &count, &used_cache);
  if (error) {
    if (library->closed) return NULL;
    return error;
  }
  free(library->catalog);
  library->catalog = packages;
  library->catalog_count = count;
  if (used_cache) set_message(library, "Quaddicted unavailable — showing cached catalog");
  else {
    char text[256];
    snprintf(text, sizeof text, "Quaddicted — %zu community packages", count);
    set_message(library, text);
  }
  return NULL;
}
void qa_app_addon_library_refresh(qa_app_addon_library *library) { run_operation(library, refresh_operation, NULL); }
void qa_app_addon_library_stop(qa_app_addon_library *library) {
  library->cancelled = true;
  if (library->options.host.cancel) library->options.host.cancel(library->options.host.ctx);
}
static const char *launch_operation(qa_app_addon_library *library, void *ctx) {
  const installed_t *local = ctx;
  char *product = qa_app_addon_product(library->options.edition, local->directory);
  library->options.host.launch(library->options.host.ctx, product, local->start);
  free(product);
  return NULL;
}
static const char *remove_operation(qa_app_addon_library *library, void *ctx) {
  installed_t *local = ctx;
  const char *error = library->options.host.remove_installed(library->options.host.ctx, local->directory);
  if (error) return error;
  char text[4096];
  snprintf(text, sizeof text, "Removed %s", local->title);
  library->selected_local = NULL;
  read_installed(library);
  set_message(library, text);
  return NULL;
}
static const char *install_operation(qa_app_addon_library *library, void *ctx) {
  const qa_app_addon_package *package = ctx;
  const char *error = library->options.host.install(library->options.host.ctx, package);
  if (error) return error;
  read_installed(library);
  char text[4096];
  snprintf(text, sizeof text, "Installed %s — ready to play", package->title);
  set_message(library, text);
  return NULL;
}
void qa_app_addon_library_activate(qa_app_addon_library *library, const char *id) {
  if (library->pending) return;
  if (strcmp(id, "back") == 0) { library->selected = NULL; library->selected_local = NULL; return; }
  if (strcmp(id, "local-play") == 0 && library->selected_local) {
    run_operation(library, launch_operation, (void *)library->selected_local);
    return;
  }
  if (strcmp(id, "local-remove") == 0 && library->selected_local) {
    run_operation(library, remove_operation, (void *)library->selected_local);
    return;
  }
  for (size_t i = 0; i < library->installed_count; i++) {
    char local_id[8192];
    snprintf(local_id, sizeof local_id, "local:%s", library->installed[i].directory);
    if (strcmp(local_id, id) == 0) { library->selected_local = &library->installed[i]; return; }
  }
  const qa_app_addon_package *selected = library->selected;
  if (!selected) {
    for (size_t i = 0; i < library->catalog_count; i++)
      if (strcmp(library->catalog[i].sha256, id) == 0) library->selected = &library->catalog[i];
    if (!library->selected) library->selected = NULL;
    return;
  }
  const installed_t *installed = find_installed_group(library, selected->group);
  if (strcmp(id, "play") == 0 && installed) run_operation(library, launch_operation, (void *)installed);
  else if (strcmp(id, "install") == 0) run_operation(library, install_operation, (void *)selected);
  else if (strcmp(id, "remove") == 0 && installed) run_operation(library, remove_operation, (void *)installed);
}
static const char *import_operation(qa_app_addon_library *library, void *ctx) {
  return library->options.host.import_local(library->options.host.ctx, ctx);
}
void qa_app_addon_library_create_submit(qa_app_addon_library *library, const char *path) {
  run_operation(library, import_operation, (void *)path);
}
void qa_app_addon_library_settle(qa_app_addon_library *library) { (void)library; /* synchronous */ }
void qa_app_addon_library_suspend(qa_app_addon_library *library) {
  library->cancelled = true;
  if (library->options.host.cancel) library->options.host.cancel(library->options.host.ctx);
}
void qa_app_addon_library_close(qa_app_addon_library *library) {
  if (!library || library->closed) return;
  library->closed = true;
  if (library->options.host.cancel) library->options.host.cancel(library->options.host.ctx);
}
