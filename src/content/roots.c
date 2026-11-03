/* Port of src/content/roots.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/content/roots.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"

static const char *const ANCHORS[3] = {"id1", "baseq2", "baseq3"};
static const char *const CANONICAL[3] = {"q1", "q2", "q3a"};

static char *copy_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}
/* TS toLowerCase on UTF-8 names: Unicode lowering (U+212A KELVIN SIGN lowers to 'k'). */
static char *lowered(const char *text) { return qa_js_to_lower_case(text); }
/* TS left.toLowerCase() === right (right already lowered). */
static bool ascii_iequal(const char *left, const char *right) { return qa_js_equals_ignore_case(left, right); }

/* Lexical absolute normalization (node:path resolve). */
static char *normalize_absolute(const char *joined) {
  size_t length = strlen(joined);
  char *copy = (char *)malloc(length + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, joined, length + 1);
  char **parts = (char **)malloc((length / 2 + 2) * sizeof *parts);
  if (parts == NULL) { free(copy); return NULL; }
  size_t count = 0;
  char *cursor = copy;
  while (*cursor != '\0') {
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') break;
    char *start = cursor;
    while (*cursor != '\0' && *cursor != '/') cursor++;
    size_t part_length = (size_t)(cursor - start);
    if (*cursor == '/') *cursor++ = '\0';
    if (part_length == 1 && start[0] == '.') continue;
    if (part_length == 2 && start[0] == '.' && start[1] == '.') {
      if (count > 0) count--;
      continue;
    }
    parts[count++] = start;
  }
  char *out = (char *)malloc(length + 2);
  if (out == NULL) { free(parts); free(copy); return NULL; }
  size_t at = 0;
  if (count == 0) out[at++] = '/';
  for (size_t i = 0; i < count; i++) {
    out[at++] = '/';
    size_t part_length = strlen(parts[i]);
    memcpy(out + at, parts[i], part_length);
    at += part_length;
  }
  out[at] = '\0';
  free(parts);
  free(copy);
  return out;
}
static char *resolve2(const char *first, const char *second) {
  char joined[PATH_MAX * 2];
  if (second != NULL && second[0] == '/') {
    (void)snprintf(joined, sizeof joined, "%s", second);
  } else if (first[0] == '/') {
    if (second != NULL) (void)snprintf(joined, sizeof joined, "%s/%s", first, second);
    else (void)snprintf(joined, sizeof joined, "%s", first);
  } else {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd) == NULL) return copy_text(first);
    if (second != NULL)
      (void)snprintf(joined, sizeof joined, "%s/%s/%s", cwd, first, second);
    else
      (void)snprintf(joined, sizeof joined, "%s/%s", cwd, first);
  }
  return normalize_absolute(joined);
}
static char *dirname_of(const char *path) {
  char *copy = copy_text(path);
  if (copy == NULL) return NULL;
  size_t length = strlen(copy);
  while (length > 1 && copy[length - 1] == '/') copy[--length] = '\0';
  char *slash = strrchr(copy, '/');
  if (slash == NULL) { memcpy(copy, ".", 2); return copy; }
  if (slash == copy) { copy[1] = '\0'; return copy; }
  *slash = '\0';
  return copy;
}
static const char *home_of(const char *home) {
  if (home != NULL) return home;
  const char *env = getenv("HOME");
  return env != NULL ? env : "";
}

const char *qa_game_family_directory(qa_game_family family) {
  return CANONICAL[family];
}
bool qa_game_family_for_directory(const char *name, qa_game_family *out) {
  char *lower = lowered(name);
  if (lower == NULL) return false;
  bool found = false;
  for (int family = 0; family < 3; family++) {
    if (strcmp(CANONICAL[family], lower) == 0) {
      *out = (qa_game_family)family;
      found = true;
      break;
    }
  }
  free(lower);
  return found;
}
char *qa_game_relative_path(const char *canonical_path, qa_game_family family) {
  const char *slash = strchr(canonical_path, '/');
  size_t head_length =
      slash != NULL ? (size_t)(slash - canonical_path) : strlen(canonical_path);
  char head[64] = "";
  if (head_length < sizeof head) {
    memcpy(head, canonical_path, head_length);
    head[head_length] = '\0';
    char *lower = lowered(head);
    bool matches = lower != NULL && strcmp(lower, CANONICAL[family]) == 0;
    free(lower);
    if (matches) return copy_text(slash != NULL ? slash + 1 : "");
  }
  return copy_text(canonical_path);
}

static bool is_directory(const char *path) {
  struct stat info;
  return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static int compare_child_names(const void *left, const void *right) {
  const char *a = *(char *const *)left;
  const char *b = *(char *const *)right;
  /* TS name.toLowerCase(): Unicode lowering of the UTF-8 directory name. */
  char *lower_a = qa_js_to_lower_case(a), *lower_b = qa_js_to_lower_case(b);
  int order = 0;
  if (lower_a != NULL && lower_b != NULL) order = strcmp(lower_a, lower_b);
  free(lower_a);
  free(lower_b);
  if (order != 0) return order;
  return strcmp(a, b);
}

/* Sorted child directory names (malloc'd); empty on any failure. */
static bool child_directories(const char *directory, char ***out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  DIR *dir = opendir(directory);
  if (dir == NULL) return true;
  char **names = NULL;
  size_t count = 0, capacity = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    bool keep = false;
    if (entry->d_type == DT_DIR) keep = true;
    else if (entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN) {
      char *child = resolve2(directory, entry->d_name);
      keep = child != NULL && is_directory(child) &&
             (entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN);
      if (entry->d_type == DT_UNKNOWN) {
        /* Dirent.isDirectory() may be false for unknown types; stat
         * follows the TS `isDirectory() || isSymbolicLink() && ...`. */
        struct stat info;
        keep = child != NULL && lstat(child, &info) == 0 &&
               (S_ISDIR(info.st_mode) ||
                (S_ISLNK(info.st_mode) && is_directory(child)));
      }
      free(child);
    }
    if (!keep) continue;
    if (count == capacity) {
      size_t next = capacity == 0 ? 16 : capacity * 2;
      char **grown = (char **)realloc(names, next * sizeof *grown);
      if (grown == NULL) {
        (void)closedir(dir);
        qa_content_free_string_list(names, count);
        return false;
      }
      names = grown;
      capacity = next;
    }
    names[count] = copy_text(entry->d_name);
    if (names[count] == NULL) {
      (void)closedir(dir);
      qa_content_free_string_list(names, count);
      return false;
    }
    count++;
  }
  (void)closedir(dir);
  if (count > 1) qa_js_sort(names, count, sizeof *names, compare_child_names);
  *out = names;
  *out_count = count;
  return true;
}

static char *child_directory(const char *directory, const char *lowered_name) {
  char **names = NULL;
  size_t count = 0;
  if (!child_directories(directory, &names, &count))
    qa_range_error("out of memory");
  char *first_match = NULL;
  char *result = NULL;
  for (size_t i = 0; i < count; i++) {
    if (!ascii_iequal(names[i], lowered_name)) continue;
    if (first_match == NULL) first_match = names[i];
    if (strcmp(names[i], lowered_name) == 0) { result = names[i]; break; }
  }
  if (result == NULL) result = first_match;
  char *out = result != NULL ? copy_text(result) : NULL;
  qa_content_free_string_list(names, count);
  return out;
}

static char *anchored_game_root(const char *root, qa_game_family family) {
  char *absolute = resolve2(root, NULL);
  if (absolute == NULL) qa_range_error("out of memory");
  const char *anchor = ANCHORS[family];
  char *canonical = child_directory(absolute, CANONICAL[family]);
  if (canonical != NULL) {
    char *canonical_path = resolve2(absolute, canonical);
    char *found = canonical_path != NULL
                      ? child_directory(canonical_path, anchor)
                      : NULL;
    bool has_anchor = found != NULL;
    free(found);
    if (has_anchor) {
      free(canonical);
      free(absolute);
      return canonical_path;
    }
    free(canonical_path);
  }
  free(canonical);
  char *direct = child_directory(absolute, anchor);
  if (direct != NULL) {
    free(direct);
    return absolute;
  }
  char **children = NULL;
  size_t child_count = 0;
  if (!child_directories(absolute, &children, &child_count))
    qa_range_error("out of memory");
  char *result = NULL;
  for (size_t i = 0; i < child_count; i++) {
    if (ascii_iequal(children[i], CANONICAL[family])) continue;
    char *child_path = resolve2(absolute, children[i]);
    char *found = child_path != NULL ? child_directory(child_path, anchor) : NULL;
    bool has_anchor = found != NULL;
    free(found);
    if (has_anchor) {
      result = child_path;
      break;
    }
    free(child_path);
  }
  qa_content_free_string_list(children, child_count);
  free(absolute);
  return result;
}

char *qa_game_root_in(const char *root, qa_game_family family) {
  char *anchored = anchored_game_root(root, family);
  if (anchored != NULL) return anchored;
  char *resolved = resolve2(root, NULL);
  if (resolved == NULL) qa_range_error("out of memory");
  char *canonical = child_directory(resolved, CANONICAL[family]);
  free(resolved);
  if (canonical == NULL) return NULL;
  char *out = resolve2(root, canonical);
  free(canonical);
  return out;
}

qa_game_roots qa_resolve_game_roots(const char *root) {
  qa_game_roots roots;
  for (int family = 0; family < 3; family++)
    roots.roots[family] = qa_game_root_in(root, (qa_game_family)family);
  return roots;
}
void qa_game_roots_free(qa_game_roots *roots) {
  for (int family = 0; family < 3; family++) {
    free(roots->roots[family]);
    roots->roots[family] = NULL;
  }
}
bool qa_has_game_anchor(const char *root) {
  for (int family = 0; family < 3; family++) {
    char *anchored = anchored_game_root(root, (qa_game_family)family);
    if (anchored != NULL) {
      free(anchored);
      return true;
    }
  }
  return false;
}

void qa_content_free_string_list(char **list, size_t count) {
  if (list == NULL) return;
  for (size_t i = 0; i < count; i++) free(list[i]);
  free(list);
}

static bool push_unique(char ***list, size_t *count, size_t *capacity, char *value) {
  for (size_t i = 0; i < *count; i++) {
    if (strcmp((*list)[i], value) == 0) {
      free(value);
      return true;
    }
  }
  if (*count == *capacity) {
    size_t next = *capacity == 0 ? 8 : *capacity * 2;
    char **grown = (char **)realloc(*list, next * sizeof *grown);
    if (grown == NULL) { free(value); return false; }
    *list = grown;
    *capacity = next;
  }
  (*list)[(*count)++] = value;
  return true;
}

static char *read_entire_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  size_t capacity = 4096, length = 0;
  char *buffer = (char *)malloc(capacity);
  if (buffer == NULL) { (void)fclose(file); return NULL; }
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      char *grown = (char *)realloc(buffer, capacity);
      if (grown == NULL) { free(buffer); (void)fclose(file); return NULL; }
      buffer = grown;
    }
    size_t read = fread(buffer + length, 1, capacity - length, file);
    length += read;
    if (read == 0) break;
  }
  (void)fclose(file);
  char *shrunk = (char *)realloc(buffer, length + 1);
  if (shrunk != NULL) buffer = shrunk;
  buffer[length] = '\0';
  return buffer;
}

static bool steam_apps_directories(const char *home, char ***out, size_t *out_count) {
  const char *resolved_home = home_of(home);
  const char *suffixes[3] = {".local/share/Steam", ".steam/steam",
                             ".var/app/com.valvesoftware.Steam/.local/share/Steam"};
  char **apps = NULL;
  size_t count = 0, capacity = 0;
  for (int index = 0; index < 3; index++) {
    char *steam_root = resolve2(resolved_home, suffixes[index]);
    if (steam_root == NULL) goto oom;
    char *steamapps = resolve2(steam_root, "steamapps");
    if (steamapps == NULL || !push_unique(&apps, &count, &capacity, steamapps)) {
      free(steam_root);
      goto oom;
    }
    char *vdf_path = resolve2(steam_root, "steamapps/libraryfolders.vdf");
    free(steam_root);
    if (vdf_path == NULL) goto oom;
    char *text = read_entire_file(vdf_path);
    free(vdf_path);
    if (text == NULL) continue; /* This Steam root is not installed. */
    /* TS /"path"\s+"([^"]+)"/g: at least one whitespace and a nonempty
     * value; a failed attempt resumes the scan one byte later. */
    const char *search = text, *cursor;
    while ((cursor = strstr(search, "\"path\"")) != NULL) {
      search = cursor + 1;
      cursor += strlen("\"path\"");
      const char *spaces = cursor;
      while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
             *cursor == '\r' || *cursor == '\v' || *cursor == '\f')
        cursor++;
      if (cursor == spaces || *cursor != '"') continue;
      cursor++;
      const char *end = strchr(cursor, '"');
      if (end == NULL || end == cursor) continue;
      search = end + 1;
      size_t library_length = (size_t)(end - cursor);
      char *library = (char *)malloc(library_length + 1);
      if (library == NULL) { free(text); goto oom; }
      memcpy(library, cursor, library_length);
      library[library_length] = '\0';
      /* TS: library.replaceAll("\\\\", "/") (escaped backslash pairs). */
      char *flattened = (char *)malloc(library_length + 1);
      if (flattened == NULL) { free(library); free(text); goto oom; }
      size_t at = 0;
      for (size_t i = 0; i < library_length; i++) {
        if (library[i] == '\\' && i + 1 < library_length && library[i + 1] == '\\') {
          flattened[at++] = '/';
          i++;
        } else {
          flattened[at++] = library[i];
        }
      }
      flattened[at] = '\0';
      free(library);
      char *library_apps = resolve2(flattened, "steamapps");
      free(flattened);
      if (library_apps == NULL ||
          !push_unique(&apps, &count, &capacity, library_apps)) {
        free(text);
        goto oom;
      }
    }
    free(text);
  }
  *out = apps;
  *out_count = count;
  return true;
oom:
  qa_content_free_string_list(apps, count);
  return false;
}

bool qa_steam_common_directories(const char *home, char ***out, size_t *out_count) {
  char **apps = NULL;
  size_t app_count = 0;
  if (!steam_apps_directories(home, &apps, &app_count)) return false;
  char **common = NULL;
  size_t count = 0;
  for (size_t i = 0; i < app_count; i++) {
    char *directory = resolve2(apps[i], "common");
    if (directory == NULL) {
      qa_content_free_string_list(apps, app_count);
      qa_content_free_string_list(common, count);
      return false;
    }
    if (is_directory(directory)) {
      char **grown = (char **)realloc(common, (count + 1) * sizeof *grown);
      if (grown == NULL) {
        free(directory);
        qa_content_free_string_list(apps, app_count);
        qa_content_free_string_list(common, count);
        return false;
      }
      common = grown;
      common[count++] = directory;
    } else {
      free(directory);
    }
  }
  qa_content_free_string_list(apps, app_count);
  *out = common;
  *out_count = count;
  return true;
}

bool qa_nightdive_addon_directories(const char *home,
                                    qa_nightdive_addon_root **out,
                                    size_t *out_count) {
  const char *resolved_home = home_of(home);
  qa_nightdive_addon_root *found = NULL;
  size_t count = 0, capacity = 0;
  char **apps = NULL;
  size_t app_count = 0;
  if (!steam_apps_directories(home, &apps, &app_count)) return false;
  const struct {
    qa_game_family family;
    const char *suffix;
    const char *base;
  } specs[4] = {
      {QA_GAME_FAMILY_Q1, "compatdata/2310/pfx/drive_c/users/steamuser/Saved Games/Nightdive Studios/Quake", NULL},
      {QA_GAME_FAMILY_Q2, "compatdata/2320/pfx/drive_c/users/steamuser/Saved Games/Nightdive Studios/Quake II", NULL},
      {QA_GAME_FAMILY_Q1, "Saved Games/Nightdive Studios/Quake", ""},
      {QA_GAME_FAMILY_Q2, "Saved Games/Nightdive Studios/Quake II", ""}};
  for (int spec_index = 0; spec_index < 4; spec_index++) {
    size_t bases = specs[spec_index].base != NULL ? 1 : app_count;
    for (size_t base_index = 0; base_index < bases; base_index++) {
      const char *base = specs[spec_index].base != NULL
                             ? resolved_home
                             : apps[base_index];
      char *absolute = resolve2(base, specs[spec_index].suffix);
      if (absolute == NULL) goto oom;
      if (!is_directory(absolute)) { free(absolute); continue; }
      bool seen = false;
      for (size_t i = 0; i < count; i++)
        if (strcmp(found[i].root, absolute) == 0) seen = true;
      if (seen) { free(absolute); continue; }
      if (count == capacity) {
        size_t next = capacity == 0 ? 4 : capacity * 2;
        qa_nightdive_addon_root *grown =
            (qa_nightdive_addon_root *)realloc(found, next * sizeof *grown);
        if (grown == NULL) { free(absolute); goto oom; }
        found = grown;
        capacity = next;
      }
      found[count].family = specs[spec_index].family;
      found[count].root = absolute;
      count++;
    }
  }
  qa_content_free_string_list(apps, app_count);
  *out = found;
  *out_count = count;
  return true;
oom:
  qa_content_free_string_list(apps, app_count);
  qa_nightdive_addon_roots_free(found, count);
  return false;
}
void qa_nightdive_addon_roots_free(qa_nightdive_addon_root *roots, size_t count) {
  if (roots == NULL) return;
  for (size_t i = 0; i < count; i++) free(roots[i].root);
  free(roots);
}

bool qa_content_root_candidates(qa_content_root_search search, bool steam_only,
                                char ***out, size_t *out_count) {
  const char *home =
      search.home_directory != NULL ? search.home_directory : home_of(NULL);
  char **steam = NULL;
  size_t steam_count = 0;
  if (!qa_steam_common_directories(home, &steam, &steam_count)) return false;
  if (steam_only) {
    *out = steam;
    *out_count = steam_count;
    return true;
  }
  char cwd[PATH_MAX] = "";
  const char *own_raw = search.own_directory;
  if (own_raw == NULL) {
    if (getcwd(cwd, sizeof cwd) == NULL) cwd[0] = '\0';
    own_raw = cwd;
  }
  char *own = resolve2(own_raw, NULL);
  if (own == NULL) {
    qa_content_free_string_list(steam, steam_count);
    return false;
  }
  char *parent = dirname_of(own);
  if (parent == NULL) {
    free(own);
    qa_content_free_string_list(steam, steam_count);
    return false;
  }
  char **list = NULL;
  size_t count = 0, capacity = 0;
  if (!push_unique(&list, &count, &capacity, own) ||
      !push_unique(&list, &count, &capacity, parent)) {
    qa_content_free_string_list(list, count);
    qa_content_free_string_list(steam, steam_count);
    return false;
  }
  for (size_t i = 0; i < steam_count; i++) {
    if (!push_unique(&list, &count, &capacity, steam[i])) {
      /* Remaining steam strings stay owned by the steam list shell. */
      for (size_t j = i + 1; j < steam_count; j++) free(steam[j]);
      free(steam);
      qa_content_free_string_list(list, count);
      return false;
    }
    steam[i] = NULL;
  }
  free(steam);
  *out = list;
  *out_count = count;
  return true;
}

char *qa_resolve_default_content_root(qa_content_root_search search,
                                      bool steam_only) {
  char **candidates = NULL;
  size_t count = 0;
  if (!qa_content_root_candidates(search, steam_only, &candidates, &count))
    return NULL;
  for (size_t i = 0; i < count; i++) {
    if (qa_has_game_anchor(candidates[i])) {
      char *out = candidates[i];
      candidates[i] = NULL;
      qa_content_free_string_list(candidates, count);
      return out;
    }
  }
  if (count > 0) {
    char *out = candidates[0];
    candidates[0] = NULL;
    qa_content_free_string_list(candidates, count);
    return out;
  }
  qa_content_free_string_list(candidates, count);
  const char *home =
      search.home_directory != NULL ? search.home_directory : home_of(NULL);
  return resolve2(home, ".local/share/Steam/steamapps/common");
}
