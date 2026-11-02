/* Port of src/content/catalog/index.ts. See index.h for the
 * injected services (archive inspector and mount-plan opener);
 * digestFile, game roots, and path lookup delegate to their
 * landed owners (src/content/mounts/index.ts, roots.ts,
 * mounts/paths.ts, user-data.ts). GPL-2.0-or-later. */
#include "qa/content/catalog/index.h"

#include <strings.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include "catalog_internal.h"
#include "qa/content/catalog/addons.h"
#include "qa/content/mounts.h"
#include "qa/content/roots.h"
#include "qa/content/user_data.h"

/* ------------------------------------------------------------------ */
/* Path and filesystem helpers (Node path/fs mirrors).                 */

static char *resolve_path(const char *left, const char *right) {
  char *joined = NULL;
  if (right != NULL && right[0] == '/') joined = qa_catalog_dup(right);
  else if (right == NULL || right[0] == '\0') joined = qa_catalog_dup(left != NULL ? left : "");
  else if (left == NULL || left[0] == '\0') joined = qa_catalog_dup(right);
  else joined = qa_catalog_join(left, right);
  if (joined == NULL) return NULL;
  char *absolute = joined;
  if (absolute[0] != '/') {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd) == NULL) { free(joined); return NULL; }
    absolute = qa_catalog_join(cwd, joined);
    free(joined);
    if (absolute == NULL) return NULL;
  }
  /* Lexically fold "." and ".." (Node resolve, no symlink pass). */
  qa_catalog_string_list parts = {0};
  char *copy = qa_catalog_dup(absolute);
  free(absolute);
  if (copy == NULL) return NULL;
  char *cursor = copy;
  while (*cursor != '\0') {
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') break;
    char *end = strchr(cursor, '/');
    size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
    if (length == 1 && cursor[0] == '.') {
      /* skip */
    } else if (length == 2 && cursor[0] == '.' && cursor[1] == '.') {
      if (parts.count > 0) free(parts.values[--parts.count]);
    } else {
      char *part = qa_catalog_format("%.*s", (int)length, cursor);
      if (part == NULL || !qa_catalog_string_push(&parts, part)) {
        free(part); free(copy); qa_catalog_string_list_free(&parts); return NULL;
      }
    }
    cursor = end != NULL ? end : cursor + length;
  }
  free(copy);
  char *out = qa_catalog_dup("/");
  for (size_t i = 0; i < parts.count; i++) {
    char *next = i == 0 ? qa_catalog_format("/%s", parts.values[i])
                        : qa_catalog_format("%s/%s", out, parts.values[i]);
    free(out);
    out = next;
    if (out == NULL) break;
  }
  qa_catalog_string_list_free(&parts);
  return out;
}

static char *relative_path(const char *from, const char *to) {
  char *a = resolve_path(NULL, from);
  char *b = resolve_path(NULL, to);
  if (a == NULL || b == NULL) { free(a); free(b); return NULL; }
  if (strcmp(a, b) == 0) { free(a); free(b); return qa_catalog_dup(""); }
  /* Catalog only uses this for hex mount names and loose map
   * relative paths, where `to` is under `from`. */
  size_t length = strlen(a);
  char *out = NULL;
  if (strncmp(b, a, length) == 0 && (b[length] == '/' || strcmp(a, "/") == 0))
    out = qa_catalog_dup(b + length + (strcmp(a, "/") == 0 ? 0 : 1));
  else
    out = qa_catalog_dup(b); /* outside: TS "../.." chain not needed there */
  free(a); free(b);
  return out;
}

static bool path_is_directory(const char *path) {
  struct stat info;
  return path != NULL && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}
static bool path_is_file(const char *path) {
  struct stat info;
  return path != NULL && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

typedef struct dir_entry {
  char *name;
  bool is_directory;
  bool is_file;
  bool is_symlink;
} dir_entry;
typedef struct dir_list {
  dir_entry *values;
  size_t count;
} dir_list;

static void dir_list_free(dir_list *list) {
  for (size_t i = 0; i < list->count; i++) free(list->values[i].name);
  free(list->values);
  list->values = NULL; list->count = 0;
}

/* readdir withFileTypes; false (error_out ENOENT-style) on failure. */
static bool read_directory(const char *path, dir_list *out, int *error_out) {
  memset(out, 0, sizeof *out);
  if (error_out != NULL) *error_out = 0;
  DIR *dir = opendir(path);
  if (dir == NULL) { if (error_out != NULL) *error_out = errno; return false; }
  size_t capacity = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    if (out->count == capacity) {
      size_t next = capacity != 0 ? capacity * 2 : 16;
      dir_entry *grown = (dir_entry *)realloc(out->values, next * sizeof *grown);
      if (grown == NULL) { (void)closedir(dir); dir_list_free(out); return false; }
      out->values = grown; capacity = next;
    }
    dir_entry *item = &out->values[out->count++];
    memset(item, 0, sizeof *item);
    item->name = qa_catalog_dup(entry->d_name);
    char *full = qa_catalog_join(path, entry->d_name);
    struct stat info;
    item->is_symlink = full != NULL && lstat(full, &info) == 0 && S_ISLNK(info.st_mode);
    item->is_directory = entry->d_type == DT_DIR;
    item->is_file = entry->d_type == DT_REG;
    if (entry->d_type == DT_UNKNOWN && full != NULL && lstat(full, &info) == 0) {
      item->is_directory = S_ISDIR(info.st_mode);
      item->is_file = S_ISREG(info.st_mode);
    }
    free(full);
  }
  (void)closedir(dir);
  return true;
}

static uint8_t *read_file_prefix(const char *path, size_t limit, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  uint8_t *bytes = (uint8_t *)malloc(limit != 0 ? limit : 1);
  if (bytes == NULL) { (void)fclose(file); return NULL; }
  *out_length = fread(bytes, 1, limit, file);
  (void)fclose(file);
  return bytes;
}

/* findContentPath (TS owner: src/content/mounts/paths.ts) with the
 * TS default case-insensitive comparison; malloc'd path or NULL
 * when missing. */
static char *find_content_path(const char *root, const char *path) {
  if (path == NULL || path[0] == '\0') {
    /* TS anchoredPath handles "" before calling; readdir of root
     * with an empty relative is only reached via addonPath. */
    char *absolute = resolve_path(NULL, root);
    if (absolute != NULL && !path_is_directory(absolute) && !path_is_file(absolute)) {
      free(absolute);
      return NULL;
    }
    return absolute;
  }
  char *found = NULL;
  if (!qa_find_content_path(root, path, QA_CONTENT_PATH_CASE_INSENSITIVE,
                            &found))
    return NULL;
  return found;
}

/* ------------------------------------------------------------------ */
/* Game roots (TS owner: src/content/roots.ts, qa/content/roots.h).   */

typedef qa_game_roots game_roots;

static void resolve_game_roots(const char *root, game_roots *out) {
  *out = qa_resolve_game_roots(root);
}

/* userProductDirectory (TS owner: src/content/user-data.ts):
 * pathWithinRoot(userContentRoot, contentDirectory). */
static char *user_product_directory(const char *user_root, const char *content_directory) {
  return qa_user_product_directory(user_root, content_directory);
}


/* ------------------------------------------------------------------ */
/* digestFile (TS owner: src/content/mounts/index.ts) is qa_digest_file
 * in qa/content/mounts.h; the catalog keeps its per-instance cache. */

static char *digest_file(qa_content_catalog *catalog, const char *path) {
  for (size_t i = 0; i < catalog->digest_count; i++)
    if (strcmp(catalog->digests[i].path, path) == 0)
      return qa_catalog_dup(catalog->digests[i].digest);
  char *digest = qa_digest_file(path);
  if (digest == NULL) return NULL;
  if (catalog->digest_count == catalog->digest_capacity) {
    size_t next = catalog->digest_capacity != 0 ? catalog->digest_capacity * 2 : 8;
    qa_catalog_digest_cache *grown = (qa_catalog_digest_cache *)realloc(
        catalog->digests, next * sizeof *grown);
    if (grown != NULL) { catalog->digests = grown; catalog->digest_capacity = next; }
  }
  if (catalog->digest_count < catalog->digest_capacity) {
    catalog->digests[catalog->digest_count].path = qa_catalog_dup(path);
    catalog->digests[catalog->digest_count].digest = qa_catalog_dup(digest);
    catalog->digest_count++;
  }
  return digest;
}

/* ------------------------------------------------------------------ */
/* Ownership helpers.                                                  */

void qa_catalog_archive_free(qa_catalog_archive *archive) {
  free(archive->path);
  for (size_t i = 0; i < archive->entry_count; i++) free(archive->entries[i].path);
  free(archive->entries);
  archive->path = NULL; archive->entries = NULL; archive->entry_count = 0;
}

static bool archive_dup(const qa_catalog_archive *source, qa_catalog_archive *out) {
  memset(out, 0, sizeof *out);
  out->path = qa_catalog_dup(source->path);
  out->format = source->format;
  if (source->entry_count > 0) {
    out->entries = (qa_catalog_archive_entry *)calloc(source->entry_count, sizeof *out->entries);
    if (out->entries == NULL) { qa_catalog_archive_free(out); return false; }
    out->entry_count = source->entry_count;
    for (size_t i = 0; i < source->entry_count; i++) {
      out->entries[i].path = qa_catalog_dup(source->entries[i].path);
      out->entries[i].ordinal = source->entries[i].ordinal;
      out->entries[i].byte_length = source->entries[i].byte_length;
    }
  }
  return out->path != NULL;
}

static void expectation_free(qa_catalog_product_expectation *expectation) {
  free((void *)expectation->id); free((void *)expectation->edition);
  free((void *)expectation->campaign); free((void *)expectation->title);
  free((void *)expectation->content_directory);
  free((void *)expectation->base_product);
  for (size_t i = 0; i < expectation->required_content_archive_count; i++)
    free((void *)expectation->required_content_archives[i]);
  free((void *)expectation->required_content_archives);
  for (size_t i = 0; i < expectation->required_program_count; i++)
    free((void *)expectation->required_programs[i]);
  free((void *)expectation->required_programs);
  free((void *)expectation->map_witness);
  free((void *)expectation->unresolved_reason);
}

static const char **dup_string_list(const char *const *values, size_t count) {
  if (count == 0) return NULL;
  const char **out = (const char **)calloc(count, sizeof *out);
  if (out == NULL) return NULL;
  for (size_t i = 0; i < count; i++) out[i] = qa_catalog_dup(values[i]);
  return out;
}

static bool expectation_dup(const qa_catalog_product_expectation *source,
                            qa_catalog_product_expectation *out) {
  memset(out, 0, sizeof *out);
  out->id = qa_catalog_dup(source->id);
  out->family = source->family;
  out->edition = qa_catalog_dup(source->edition);
  out->campaign = qa_catalog_dup(source->campaign);
  out->title = qa_catalog_dup(source->title);
  out->content_directory = qa_catalog_dup(source->content_directory);
  out->base_product = qa_catalog_dup(source->base_product);
  out->required_content_archives = dup_string_list(
      source->required_content_archives, source->required_content_archive_count);
  out->required_content_archive_count = source->required_content_archive_count;
  out->required_programs = dup_string_list(source->required_programs,
                                           source->required_program_count);
  out->required_program_count = source->required_program_count;
  out->map_witness = qa_catalog_dup(source->map_witness);
  out->unresolved_reason = qa_catalog_dup(source->unresolved_reason);
  return out->id != NULL && out->edition != NULL && out->campaign != NULL &&
         out->title != NULL && out->content_directory != NULL;
}

void qa_catalog_discovered_product_free(qa_catalog_discovered_product *product) {
  free(product->id);
  expectation_free(&product->expectation);
  for (size_t i = 0; i < product->availability.requirement_count; i++)
    free(product->availability.requirements[i]);
  free(product->availability.requirements);
  free(product->availability.reason);
  for (size_t i = 0; i < product->archive_count; i++)
    qa_catalog_archive_free(&product->archives[i]);
  free(product->archives);
  free(product->loose_root);
  if (product->user_content != NULL) {
    free(product->user_content->root); free(product->user_content->archives);
    free(product->user_content);
  }
  if (product->addon_content != NULL) {
    free(product->addon_content->root); free(product->addon_content->archives);
    free(product->addon_content);
  }
  for (size_t i = 0; i < product->map_count; i++) {
    free(product->maps[i].path); free(product->maps[i].source);
  }
  free(product->maps);
  for (size_t i = 0; i < product->diagnostic_count; i++) free(product->diagnostics[i]);
  free(product->diagnostics);
  memset(product, 0, sizeof *product);
}

/* ------------------------------------------------------------------ */
/* Remote content.                                                     */

static char *lower_dup_required(const char *text) { return qa_catalog_lower_dup(text); }

bool qa_remote_content_selection_make(qa_remote_content_base base,
                                      const char *game_directory,
                                      qa_remote_content_selection *out) {
  memset(out, 0, sizeof *out);
  char *directory = lower_dup_required(game_directory);
  if (directory == NULL) return false;
  out->base = base;
  if (strcmp(base, "q1-quakeworld") == 0 &&
      (strcmp(directory, "qw") == 0 || strcmp(directory, "id1") == 0)) {
    free(directory);
    out->directory = qa_catalog_dup("qw");
    return out->directory != NULL;
  }
  if ((strcmp(base, "q2-classic-baseq2") == 0 ||
       strcmp(base, "q2-rerelease-baseq2") == 0) && directory[0] == '\0') {
    free(directory);
    out->directory = qa_catalog_dup("baseq2");
    return out->directory != NULL;
  }
  if (strcmp(base, "q3-baseq3") == 0 && directory[0] == '\0') {
    free(directory);
    out->directory = qa_catalog_dup("baseq3");
    return out->directory != NULL;
  }
  bool valid = directory[0] != '\0' && strcmp(directory, ".") != 0 &&
               strstr(directory, "..") == NULL;
  for (size_t i = 0; valid && directory[i] != '\0'; i++) {
    char c = directory[i];
    if (!isalnum((unsigned char)c) && c != '_' && c != '+' && c != '.' && c != '-')
      valid = false;
  }
  if (!valid) {
    free(directory);
    qa_catalog_error("Remote game directory must be a single safe directory name");
  }
  out->directory = directory;
  return true;
}

void qa_remote_content_selection_free(qa_remote_content_selection *selection) {
  free(selection->directory);
  selection->directory = NULL;
}

/* remoteExpectation; *out owns its strings (expectation_free). */
static bool remote_expectation(const qa_remote_content_selection *selection,
                               const qa_catalog_product_expectation *products,
                               size_t product_count,
                               qa_catalog_product_expectation *out) {
  qa_remote_content_selection context = {0};
  if (!qa_remote_content_selection_make(selection->base, selection->directory, &context))
    return false;
  const qa_catalog_product_expectation *base = NULL;
  for (size_t i = 0; i < product_count; i++)
    if (strcmp(products[i].id, context.base) == 0) { base = &products[i]; break; }
  if (base == NULL) {
    qa_remote_content_selection_free(&context);
    qa_catalog_error("Missing remote base product %s", selection->base);
  }
  char *parent = qa_catalog_dirname(base->content_directory);
  char *directory = qa_catalog_format("%s/%s", parent, context.directory);
  free(parent);
  const qa_catalog_product_expectation *known = NULL;
  for (size_t i = 0; i < product_count; i++) {
    if (qa_catalog_ieq(products[i].content_directory, directory) &&
        products[i].family == base->family &&
        strcmp(products[i].edition, base->edition) == 0) { known = &products[i]; break; }
  }
  if (known != NULL) {
    qa_catalog_string_list visited = {0};
    const qa_catalog_product_expectation *current = known;
    bool inherits = false;
    while (current != NULL &&
           !qa_catalog_string_contains((const char *const *)visited.values,
                                       visited.count, current->id)) {
      if (strcmp(current->id, base->id) == 0) { inherits = true; break; }
      (void)qa_catalog_string_push(&visited, qa_catalog_dup(current->id));
      const char *next_id = current->base_product;
      current = NULL;
      if (next_id != NULL)
        for (size_t i = 0; i < product_count; i++)
          if (strcmp(products[i].id, next_id) == 0) { current = &products[i]; break; }
    }
    qa_catalog_string_list_free(&visited);
    if (!inherits) {
      free(directory);
      qa_remote_content_selection_free(&context);
      qa_catalog_error("Remote product %s does not inherit %s", known->id, base->id);
    }
    bool ok = expectation_dup(known, out);
    free(directory);
    qa_remote_content_selection_free(&context);
    return ok;
  }
  memset(out, 0, sizeof *out);
  out->id = qa_catalog_format("%s-mod-%s", base->id, context.directory);
  out->family = base->family;
  out->edition = qa_catalog_dup(base->edition);
  out->campaign = qa_catalog_format("mod-%s", context.directory);
  out->title = qa_catalog_dup(context.directory);
  out->content_directory = directory;
  out->base_product = qa_catalog_dup(base->id);
  qa_remote_content_selection_free(&context);
  return out->id != NULL;
}

char *qa_remote_content_product(const qa_remote_content_selection *selection,
                                const qa_catalog_product_expectation *products,
                                size_t product_count) {
  if (products == NULL) {
    products = qa_expected_products;
    product_count = qa_expected_product_count;
  }
  qa_catalog_product_expectation expectation = {0};
  if (!remote_expectation(selection, products, product_count, &expectation))
    return NULL;
  char *id = qa_catalog_dup(expectation.id);
  expectation_free(&expectation);
  return id;
}

/* ------------------------------------------------------------------ */
/* Archive ordering.                                                   */

static bool archive_format_for(const char *name, qa_archive_format *out) {
  const char *extension = strrchr(name, '.');
  if (extension == NULL) return false;
  if (qa_catalog_ieq(extension, ".pak")) { *out = QA_ARCHIVE_FORMAT_PAK; return true; }
  if (qa_catalog_ieq(extension, ".pk3")) { *out = QA_ARCHIVE_FORMAT_PK3; return true; }
  if (qa_catalog_ieq(extension, ".kpf")) { *out = QA_ARCHIVE_FORMAT_KPF; return true; }
  if (qa_catalog_ieq(extension, ".zip") || qa_catalog_ieq(extension, ".pkz")) {
    *out = QA_ARCHIVE_FORMAT_ZIP; return true;
  }
  return false;
}

static int compare_archive_basenames(const void *left, const void *right) {
  qa_catalog_archive *const *a = (qa_catalog_archive *const *)left;
  qa_catalog_archive *const *b = (qa_catalog_archive *const *)right;
  return qa_catalog_source_compare(qa_catalog_basename((*a)->path),
                                   qa_catalog_basename((*b)->path));
}
static int compare_archive_basenames_lower(const void *left, const void *right) {
  qa_catalog_archive *const *a = (qa_catalog_archive *const *)left;
  qa_catalog_archive *const *b = (qa_catalog_archive *const *)right;
  char *la = qa_catalog_lower_dup(qa_catalog_basename((*a)->path));
  char *lb = qa_catalog_lower_dup(qa_catalog_basename((*b)->path));
  int result = qa_catalog_source_compare(la, lb);
  free(la); free(lb);
  return result;
}

typedef struct q2_pak_key {
  uint64_t number;
  char *suffix;
} q2_pak_key;

static void parse_q2_pak(const char *lower_name, q2_pak_key *out) {
  const char *tail = lower_name + 3;
  const char *cursor = tail;
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r' ||
         *cursor == '\f' || *cursor == '\v') cursor++;
  bool negative = false;
  const char *digits = cursor;
  if (*digits == '+' || *digits == '-') { negative = *digits == '-'; digits++; }
  if (!isdigit((unsigned char)*digits)) {
    out->number = 0;
    out->suffix = qa_catalog_dup(tail);
    return;
  }
  uint64_t magnitude = 0;
  const char *end = digits;
  bool overflow = false;
  while (isdigit((unsigned char)*end)) {
    uint64_t digit = (uint64_t)(*end - '0');
    if (magnitude > (UINT64_MAX - digit) / 10) overflow = true;
    else magnitude = magnitude * 10 + digit;
    end++;
  }
  if (overflow) out->number = UINT64_MAX;
  else out->number = negative ? (uint64_t)0 - magnitude : magnitude;
  out->suffix = qa_catalog_dup(end);
}

static int compare_q2_archives(const void *left, const void *right) {
  qa_catalog_archive *const *a_ptr = (qa_catalog_archive *const *)left;
  qa_catalog_archive *const *b_ptr = (qa_catalog_archive *const *)right;
  char *first = qa_catalog_lower_dup(qa_catalog_basename((*a_ptr)->path));
  char *second = qa_catalog_lower_dup(qa_catalog_basename((*b_ptr)->path));
  bool first_pak = qa_catalog_starts_with(first, "pak");
  bool second_pak = qa_catalog_starts_with(second, "pak");
  int result;
  if (!first_pak || !second_pak) {
    result = first_pak ? -1 : second_pak ? 1
                                          : qa_catalog_source_compare(first, second);
  } else {
    q2_pak_key a = {0}, b = {0};
    parse_q2_pak(first, &a);
    parse_q2_pak(second, &b);
    result = a.number < b.number ? -1 : a.number > b.number
                 ? 1 : qa_catalog_source_compare(a.suffix, b.suffix);
    free(a.suffix); free(b.suffix);
  }
  free(first); free(second);
  return result;
}

static void reverse_archives(qa_catalog_archive **values, size_t count) {
  for (size_t i = 0; i < count / 2; i++) {
    qa_catalog_archive *swap = values[i];
    values[i] = values[count - 1 - i];
    values[count - 1 - i] = swap;
  }
}

qa_catalog_archive **qa_order_game_archives(
    const qa_catalog_product_expectation *product,
    qa_catalog_archive **archives, size_t archive_count, size_t *out_count) {
  *out_count = 0;
  qa_catalog_archive **sorted = NULL;
  if (archive_count > 0) {
    sorted = (qa_catalog_archive **)malloc(archive_count * sizeof *sorted);
    if (sorted == NULL) return NULL;
    memcpy(sorted, archives, archive_count * sizeof *sorted);
    qsort(sorted, archive_count, sizeof *sorted, compare_archive_basenames);
  }
  qa_catalog_archive **result = NULL;
  size_t count = 0;
  if (archive_count > 0) {
    result = (qa_catalog_archive **)malloc(archive_count * sizeof *result);
    if (result == NULL) { free(sorted); return NULL; }
  }
  if (product->family == QA_GAME_FAMILY_Q3) {
    for (size_t i = 0; i < archive_count; i++)
      if (sorted[i]->format == QA_ARCHIVE_FORMAT_PK3) result[count++] = sorted[i];
    qsort(result, count, sizeof *result, compare_archive_basenames_lower);
    reverse_archives(result, count);
  } else if (product->family == QA_GAME_FAMILY_Q2) {
    for (size_t i = 0; i < archive_count; i++) {
      qa_catalog_archive *archive = archives[i];
      const char *extension = strrchr(archive->path, '.');
      if (archive->format == QA_ARCHIVE_FORMAT_PAK ||
          (archive->format == QA_ARCHIVE_FORMAT_ZIP && extension != NULL &&
           qa_catalog_ieq(extension, ".pkz")))
        result[count++] = archive;
    }
    qsort(result, count, sizeof *result, compare_q2_archives);
    reverse_archives(result, count);
  } else {
    size_t limit = archive_count + 1;
    for (size_t index = 0; index < limit; index++) {
      char *wanted = qa_catalog_format("pak%zu.pak", index);
      qa_catalog_archive *found = NULL;
      for (size_t i = 0; i < archive_count; i++)
        if (qa_catalog_ieq(qa_catalog_basename(sorted[i]->path), wanted)) {
          found = sorted[i]; break;
        }
      free(wanted);
      if (found != NULL) result[count++] = found;
      else break;
    }
    for (size_t i = 0; i < archive_count; i++)
      if (sorted[i]->format == QA_ARCHIVE_FORMAT_PK3 ||
          sorted[i]->format == QA_ARCHIVE_FORMAT_KPF)
        result[count++] = sorted[i];
    reverse_archives(result, count);
  }
  free(sorted);
  *out_count = count;
  return result;
}

/* ------------------------------------------------------------------ */
/* InstalledCatalog core.                                              */

void qa_content_catalog_init(qa_content_catalog *catalog, const char *corpus_root,
                             qa_catalog_discovered_product *products,
                             size_t product_count,
                             qa_catalog_archive *root_archives,
                             size_t root_archive_count, int64_t generation,
                             const char *user_content_root,
                             const qa_catalog_nightdive_addon_root *addon_roots,
                             size_t addon_root_count) {
  memset(catalog, 0, sizeof *catalog);
  catalog->corpus_root = (char *)corpus_root;
  catalog->products = products;
  catalog->product_count = product_count;
  catalog->root_archives = root_archives;
  catalog->root_archive_count = root_archive_count;
  catalog->generation = generation;
  catalog->user_content_root = (char *)user_content_root;
  catalog->addon_content_root_count = addon_root_count;
  if (addon_root_count > 0) {
    catalog->addon_content_roots = (qa_catalog_nightdive_addon_root *)malloc(
        addon_root_count * sizeof *catalog->addon_content_roots);
    if (catalog->addon_content_roots != NULL)
      memcpy(catalog->addon_content_roots, addon_roots,
             addon_root_count * sizeof *catalog->addon_content_roots);
  }
  for (size_t i = 0; i < product_count; i++) {
    for (size_t j = 0; j < i; j++) {
      if (strcmp(products[i].id, products[j].id) == 0 ||
          strcmp(products[i].id, products[j].expectation.id) == 0 ||
          strcmp(products[i].expectation.id, products[j].id) == 0 ||
          strcmp(products[i].expectation.id, products[j].expectation.id) == 0)
        qa_range_error("Duplicate catalog identity: %s", products[i].id);
    }
    if (strcmp(products[i].id, products[i].expectation.id) == 0 && i != (size_t)-1) {
      /* TS sets both keys to the same product; only cross-product
       * collisions count (the #byId.has checks run before either
       * set for this product). */
    }
  }
}

void qa_content_catalog_destroy(qa_content_catalog *catalog) {
  for (size_t i = 0; i < catalog->digest_count; i++) {
    free(catalog->digests[i].path); free(catalog->digests[i].digest);
  }
  free(catalog->digests);
  if (catalog->owned) {
    free(catalog->corpus_root);
    free(catalog->user_content_root);
    for (size_t i = 0; i < catalog->product_count; i++)
      qa_catalog_discovered_product_free(&catalog->products[i]);
    free(catalog->products);
    for (size_t i = 0; i < catalog->root_archive_count; i++)
      qa_catalog_archive_free(&catalog->root_archives[i]);
    free(catalog->root_archives);
    for (size_t i = 0; i < catalog->addon_content_root_count; i++)
      free(catalog->addon_content_root_strings != NULL
               ? catalog->addon_content_root_strings[i] : NULL);
    free(catalog->addon_content_root_strings);
  }
  free(catalog->addon_content_roots);
  memset(catalog, 0, sizeof *catalog);
}

const qa_catalog_discovered_product *qa_content_catalog_product(
    const qa_content_catalog *catalog, const char *id) {
  for (size_t i = 0; i < catalog->product_count; i++)
    if (strcmp(catalog->products[i].id, id) == 0 ||
        strcmp(catalog->products[i].expectation.id, id) == 0)
      return &catalog->products[i];
  qa_range_error("Unknown requested content or mod: %s", id);
}

const qa_catalog_discovered_product *qa_content_catalog_require(
    const qa_content_catalog *catalog, const char *id) {
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, id);
  if (product->availability.kind == QA_CATALOG_MISSING) {
    qa_catalog_string_list joined = {0};
    char *text = qa_catalog_dup("");
    for (size_t i = 0; i < product->availability.requirement_count; i++) {
      char *next = i == 0 ? qa_catalog_dup(product->availability.requirements[i])
                          : qa_catalog_format("%s, %s", text,
                                              product->availability.requirements[i]);
      free(text); text = next;
    }
    (void)joined;
    qa_catalog_error("Content %s requires: %s", id, text != NULL ? text : "");
  }
  if (product->availability.kind == QA_CATALOG_UNRESOLVED)
    qa_catalog_error("Content %s is unresolved: %s", id,
                     product->availability.reason != NULL
                         ? product->availability.reason : "");
  return product;
}

typedef struct directory_view {
  const char *root; /* borrowed, nullable */
  qa_catalog_archive **archives;
  size_t archive_count;
} directory_view;

/* productDirectories: [user?, corpus, addon?] */
static size_t product_directories(const qa_catalog_discovered_product *product,
                                  directory_view views[3],
                                  qa_catalog_archive **corpus_out) {
  size_t count = 0;
  const qa_catalog_directory_content *user = product->user_content;
  const qa_catalog_directory_content *addon = product->addon_content;
  size_t corpus_count = 0;
  for (size_t i = 0; i < product->archive_count; i++) {
    qa_catalog_archive *archive = &product->archives[i];
    bool excluded = false;
    if (user != NULL)
      for (size_t j = 0; j < user->archive_count; j++)
        if (user->archives[j] == archive) excluded = true;
    if (addon != NULL)
      for (size_t j = 0; j < addon->archive_count; j++)
        if (addon->archives[j] == archive) excluded = true;
    if (!excluded) corpus_out[corpus_count++] = archive;
  }
  if (user != NULL) {
    views[count].root = user->root;
    views[count].archives = user->archives;
    views[count].archive_count = user->archive_count;
    count++;
  }
  views[count].root = product->loose_root;
  views[count].archives = corpus_out;
  views[count].archive_count = corpus_count;
  count++;
  if (addon != NULL) {
    views[count].root = addon->root;
    views[count].archives = addon->archives;
    views[count].archive_count = addon->archive_count;
    count++;
  }
  return count;
}

typedef struct map_winner_list {
  const qa_catalog_content_map **values;
  size_t count;
  size_t capacity;
} map_winner_list;

static bool map_winner_add(map_winner_list *list, const qa_catalog_content_map *map) {
  for (size_t i = 0; i < list->count; i++)
    if (qa_catalog_ieq(list->values[i]->path, map->path)) return true;
  if (list->count == list->capacity) {
    size_t next = list->capacity != 0 ? list->capacity * 2 : 8;
    const qa_catalog_content_map **grown = (const qa_catalog_content_map **)realloc(
        list->values, next * sizeof *grown);
    if (grown == NULL) return false;
    list->values = grown; list->capacity = next;
  }
  list->values[list->count++] = map;
  return true;
}

static bool maps_visit(const qa_content_catalog *catalog,
                       const qa_catalog_discovered_product *product,
                       qa_catalog_string_list *visited,
                       map_winner_list *winners) {
  if (qa_catalog_string_contains((const char *const *)visited->values,
                                 visited->count, product->id))
    qa_catalog_error("Cyclic base content dependency: %s", product->id);
  if (!qa_catalog_string_push(visited, qa_catalog_dup(product->id))) return false;
  qa_catalog_archive **corpus = NULL;
  if (product->archive_count > 0) {
    corpus = (qa_catalog_archive **)malloc(product->archive_count * sizeof *corpus);
    if (corpus == NULL) return false;
  }
  directory_view views[3];
  size_t view_count = product_directories(product, views, corpus);
  for (size_t v = 0; v < view_count; v++) {
    size_t ordered_count = 0;
    qa_catalog_archive **ordered = qa_order_game_archives(
        &product->expectation, views[v].archives, views[v].archive_count,
        &ordered_count);
    for (size_t a = 0; a < ordered_count; a++) {
      qa_catalog_archive *archive = ordered[a];
      if (archive->format == QA_ARCHIVE_FORMAT_PAK) {
        for (size_t m = 0; m < product->map_count; m++)
          if (strcmp(product->maps[m].source, archive->path) == 0)
            if (!map_winner_add(winners, &product->maps[m])) { free(ordered); free(corpus); return false; }
      } else {
        for (size_t m = product->map_count; m-- > 0;)
          if (strcmp(product->maps[m].source, archive->path) == 0)
            if (!map_winner_add(winners, &product->maps[m])) { free(ordered); free(corpus); return false; }
      }
    }
    free(ordered);
    for (size_t m = 0; m < product->map_count; m++) {
      const qa_catalog_content_map *map = &product->maps[m];
      if (map->has_member_index || views[v].root == NULL) continue;
      char *resolved = resolve_path(views[v].root, map->path);
      bool same = resolved != NULL && strcmp(resolved, map->source) == 0;
      free(resolved);
      if (same && !map_winner_add(winners, map)) { free(corpus); return false; }
    }
  }
  free(corpus);
  if (product->expectation.base_product != NULL) {
    const qa_catalog_discovered_product *base =
        qa_content_catalog_product(catalog, product->expectation.base_product);
    return maps_visit(catalog, base, visited, winners);
  }
  return true;
}

const qa_catalog_content_map **qa_content_catalog_maps_for(
    const qa_content_catalog *catalog, const char *id, size_t *out_count) {
  *out_count = 0;
  map_winner_list winners = {0};
  qa_catalog_string_list visited = {0};
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, id);
  if (!maps_visit(catalog, product, &visited, &winners)) {
    free(winners.values);
    qa_catalog_string_list_free(&visited);
    return NULL;
  }
  qa_catalog_string_list_free(&visited);
  *out_count = winners.count;
  return winners.values;
}

/* ------------------------------------------------------------------ */
/* Mounts and mount plans.                                             */

static bool mount_list_push_string(qa_catalog_mount_list *list, char *owned) {
  char **grown = (char **)realloc(list->strings,
                                  (list->string_count + 1) * sizeof *grown);
  if (grown == NULL) return false;
  list->strings = grown;
  list->strings[list->string_count++] = owned;
  return true;
}

static char *hex_encode(qa_catalog_mount_list *list, const char *text) {
  static const char digits[] = "0123456789abcdef";
  size_t length = strlen(text);
  char *out = (char *)malloc(length * 2 + 1);
  if (out == NULL) return NULL;
  for (size_t i = 0; i < length; i++) {
    out[i * 2] = digits[((unsigned char)text[i] >> 4) & 15];
    out[i * 2 + 1] = digits[(unsigned char)text[i] & 15];
  }
  out[length * 2] = '\0';
  if (!mount_list_push_string(list, out)) { free(out); return NULL; }
  return out;
}

static bool mount_list_push(qa_catalog_mount_list *list, qa_content_mount mount,
                            size_t *capacity) {
  if (list->count == *capacity) {
    size_t next = *capacity != 0 ? *capacity * 2 : 8;
    qa_content_mount *grown =
        (qa_content_mount *)realloc(list->mounts, next * sizeof *grown);
    if (grown == NULL) return false;
    list->mounts = grown; *capacity = next;
  }
  list->mounts[list->count++] = mount;
  return true;
}

void qa_content_catalog_mount_list_free(qa_catalog_mount_list *list) {
  free(list->mounts);
  for (size_t i = 0; i < list->string_count; i++) free(list->strings[i]);
  free(list->strings);
  memset(list, 0, sizeof *list);
}

typedef struct mounts_state {
  qa_content_catalog *catalog;
  qa_catalog_mount_list out;
  size_t capacity;
  qa_catalog_string_list paths;
  qa_catalog_string_list visited;
} mounts_state;

static bool mounts_add_archive(mounts_state *state,
                               const qa_catalog_discovered_product *product,
                               qa_catalog_archive *archive) {
  if (qa_catalog_string_contains((const char *const *)state->paths.values,
                                 state->paths.count, archive->path)) return true;
  if (!qa_catalog_string_push(&state->paths, qa_catalog_dup(archive->path)))
    return false;
  char *relative = relative_path(state->catalog->corpus_root, archive->path);
  if (relative == NULL) return false;
  char *name = hex_encode(&state->out, relative);
  free(relative);
  if (name == NULL) return false;
  char *mount_id = qa_create_mount_id(product->expectation.id, name);
  char *digest = digest_file(state->catalog, archive->path);
  char *path_copy = qa_catalog_dup(archive->path);
  if (mount_id == NULL || digest == NULL || path_copy == NULL ||
      !mount_list_push_string(&state->out, mount_id) ||
      !mount_list_push_string(&state->out, digest) ||
      !mount_list_push_string(&state->out, path_copy)) {
    return false;
  }
  qa_content_mount mount = {0};
  mount.kind = QA_CONTENT_MOUNT_ARCHIVE;
  mount.as.archive.identity = qa_create_mount_identity(
      mount_id, product->id, state->catalog->generation);
  mount.as.archive.archive_path = path_copy;
  mount.as.archive.archive_digest = digest;
  mount.as.archive.format = archive->format;
  return mount_list_push(&state->out, mount, &state->capacity);
}

static bool mounts_visit(mounts_state *state, qa_content_id content) {
  const qa_catalog_discovered_product *product =
      qa_content_catalog_require(state->catalog, content);
  if (qa_catalog_string_contains((const char *const *)state->visited.values,
                                 state->visited.count, product->id))
    qa_catalog_error("Cyclic base content dependency: %s", content);
  if (!qa_catalog_string_push(&state->visited, qa_catalog_dup(product->id)))
    return false;
  qa_catalog_archive **corpus = NULL;
  if (product->archive_count > 0) {
    corpus = (qa_catalog_archive **)malloc(product->archive_count * sizeof *corpus);
    if (corpus == NULL) return false;
  }
  directory_view views[3];
  size_t view_count = product_directories(product, views, corpus);
  for (size_t v = 0; v < view_count; v++) {
    size_t ordered_count = 0;
    qa_catalog_archive **ordered = qa_order_game_archives(
        &product->expectation, views[v].archives, views[v].archive_count,
        &ordered_count);
    for (size_t a = 0; a < ordered_count; a++)
      if (!mounts_add_archive(state, product, ordered[a])) {
        free(ordered); free(corpus); return false;
      }
    free(ordered);
    if (views[v].root != NULL &&
        !qa_catalog_string_contains((const char *const *)state->paths.values,
                                    state->paths.count, views[v].root)) {
      if (!qa_catalog_string_push(&state->paths, qa_catalog_dup(views[v].root))) {
        free(corpus); return false;
      }
      char *relative = relative_path(state->catalog->corpus_root, views[v].root);
      if (relative == NULL) { free(corpus); return false; }
      char *hex = hex_encode(&state->out, relative);
      free(relative);
      if (hex == NULL) { free(corpus); return false; }
      char *name = qa_catalog_format("loose-%s", hex);
      char *root_copy = qa_catalog_dup(views[v].root);
      if (name == NULL || !mount_list_push_string(&state->out, name) ||
          root_copy == NULL || !mount_list_push_string(&state->out, root_copy)) {
        free(corpus); return false;
      }
      char *mount_id = qa_create_mount_id(product->expectation.id, name);
      if (mount_id == NULL || !mount_list_push_string(&state->out, mount_id)) {
        free(corpus); return false;
      }
      qa_content_mount mount = {0};
      mount.kind = QA_CONTENT_MOUNT_LOOSE;
      mount.as.loose.identity = qa_create_mount_identity(
          mount_id, product->id, state->catalog->generation);
      mount.as.loose.root_path = root_copy;
      if (!mount_list_push(&state->out, mount, &state->capacity)) {
        free(corpus); return false;
      }
    }
  }
  free(corpus);
  if (product->expectation.base_product != NULL) {
    const qa_catalog_discovered_product *base =
        qa_content_catalog_product(state->catalog, product->expectation.base_product);
    if (!mounts_visit(state, base->id)) return false;
  }
  bool rerelease_root = (product->expectation.base_product == NULL &&
                         strcmp(product->expectation.edition, "rerelease") == 0);
  if (!rerelease_root) {
    const char *directory = product->expectation.content_directory;
    const char *slash = strchr(directory, '/');
    if (slash != NULL) {
      const char *second = slash + 1;
      const char *end = strchr(second, '/');
      size_t length = end != NULL ? (size_t)(end - second) : strlen(second);
      rerelease_root = length == strlen("rerelease") &&
                       strncmp(second, "rerelease", length) == 0;
    }
  }
  if (rerelease_root) {
    char *fallback = resolve_path(state->catalog->corpus_root,
                                  product->expectation.content_directory);
    char *root = qa_catalog_dirname(product->loose_root != NULL
                                        ? product->loose_root : fallback);
    free(fallback);
    if (root == NULL) return false;
    size_t matched = 0;
    for (size_t i = 0; i < state->catalog->root_archive_count; i++) {
      char *parent = qa_catalog_dirname(state->catalog->root_archives[i].path);
      bool same = parent != NULL && strcmp(parent, root) == 0;
      free(parent);
      if (same) {
        matched++;
        if (!mounts_add_archive(state, product, &state->catalog->root_archives[i])) {
          free(root); return false;
        }
      }
    }
    if (matched == 0) {
      const char *kpf = product->expectation.family == QA_GAME_FAMILY_Q1
                            ? "quakeex.kpf" : "q2game.kpf";
      for (size_t i = 0; i < state->catalog->root_archive_count; i++) {
        if (qa_catalog_ieq(qa_catalog_basename(state->catalog->root_archives[i].path), kpf))
          if (!mounts_add_archive(state, product, &state->catalog->root_archives[i])) {
            free(root); return false;
          }
      }
    }
    free(root);
  }
  return true;
}

bool qa_content_catalog_mounts_for(qa_content_catalog *catalog,
                                   qa_content_id id,
                                   qa_catalog_mount_list *out) {
  memset(out, 0, sizeof *out);
  mounts_state state = {0};
  state.catalog = catalog;
  bool ok = mounts_visit(&state, id);
  *out = state.out;
  qa_catalog_string_list_free(&state.paths);
  qa_catalog_string_list_free(&state.visited);
  if (!ok) qa_content_catalog_mount_list_free(out);
  return ok;
}

static const char *mount_physical_key(const qa_content_mount *mount) {
  return mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? mount->as.archive.archive_path
                                                 : mount->as.loose.root_path;
}
static qa_mount_id mount_identity_id(const qa_content_mount *mount) {
  return mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? mount->as.archive.identity.id
                                                 : mount->as.loose.identity.id;
}

bool qa_content_catalog_create_mount_plan(
    qa_content_catalog *catalog,
    const qa_catalog_mount_plan_selection *selection,
    qa_catalog_mount_plan_result *out) {
  memset(out, 0, sizeof *out);
  const qa_catalog_discovered_product *geometry =
      qa_content_catalog_require(catalog, selection->geometry);
  const qa_catalog_discovered_product *assets =
      qa_content_catalog_require(catalog, selection->assets);
  const char *required_rules_assets = NULL;
  if (selection->rules != NULL) {
    const qa_catalog_discovered_product *rules =
        qa_content_catalog_require(catalog, selection->rules);
    if (rules->expectation.family == QA_GAME_FAMILY_Q2 &&
        strcmp(rules->expectation.edition, "rerelease") == 0) {
      required_rules_assets = rules->id;
      if (!selection->explicit_presentation) assets = rules;
    }
  }
  qa_catalog_mount_list asset_mounts = {0}, geometry_mounts = {0}, extra = {0};
  bool ok = qa_content_catalog_mounts_for(catalog, assets->id, &asset_mounts) &&
            qa_content_catalog_mounts_for(catalog, geometry->id, &geometry_mounts);
  qa_content_catalog_mount_list_free(&extra);
  qa_catalog_mount_list additional = {0};
  size_t additional_capacity = 0;
  if (ok && required_rules_assets != NULL)
    ok = qa_content_catalog_mounts_for(catalog, required_rules_assets, &additional);
  /* Concatenate remaining additional mounts under one owner. */
  qa_catalog_mount_list rest = {0};
  size_t rest_capacity = 0;
  for (size_t i = 0; ok && i < selection->additional_count; i++) {
    qa_catalog_mount_list part = {0};
    if (!qa_content_catalog_mounts_for(catalog, selection->additional[i], &part)) {
      ok = false; break;
    }
    for (size_t m = 0; m < part.count; m++)
      ok = ok && mount_list_push(&rest, part.mounts[m], &rest_capacity);
    for (size_t s = 0; s < part.string_count; s++)
      ok = ok && mount_list_push_string(&rest, part.strings[s]);
    free(part.mounts); free(part.strings);
    (void)additional_capacity;
  }
  if (!ok) {
    qa_content_catalog_mount_list_free(&asset_mounts);
    qa_content_catalog_mount_list_free(&geometry_mounts);
    qa_content_catalog_mount_list_free(&additional);
    qa_content_catalog_mount_list_free(&rest);
    return false;
  }
  /* Unique by physical key in asset, geometry, additional order;
   * the result takes over all mount/string backing. */
  qa_catalog_mount_list unique = {0};
  size_t unique_capacity = 0;
  const qa_catalog_mount_list *groups[4] = {&asset_mounts, &geometry_mounts,
                                            &additional, &rest};
  for (size_t g = 0; g < 4; g++) {
    for (size_t m = 0; m < groups[g]->count; m++) {
      bool seen = false;
      for (size_t u = 0; u < unique.count; u++)
        if (strcmp(mount_physical_key(&unique.mounts[u]),
                   mount_physical_key(&groups[g]->mounts[m])) == 0) seen = true;
      if (!seen) (void)mount_list_push(&unique, groups[g]->mounts[m], &unique_capacity);
    }
    for (size_t s = 0; s < groups[g]->string_count; s++)
      (void)mount_list_push_string(&unique, groups[g]->strings[s]);
  }
  /* TS order(first): canonical ids of `first` (by physical key),
   * then the remaining unique mounts, as a set. */
  qa_mount_id *default_order = NULL, *geometry_order = NULL;
  size_t default_count = 0, geometry_count = 0;
  if (unique.count > 0) {
    default_order = (qa_mount_id *)malloc(unique.count * sizeof *default_order);
    geometry_order = (qa_mount_id *)malloc(unique.count * sizeof *geometry_order);
    const qa_catalog_mount_list *firsts[2] = {&asset_mounts, &geometry_mounts};
    qa_mount_id **orders[2] = {&default_order, &geometry_order};
    size_t *counts[2] = {&default_count, &geometry_count};
    for (size_t o = 0; o < 2; o++) {
      qa_mount_id *order = *orders[o];
      size_t count = 0;
      for (size_t pass = 0; pass < 2; pass++) {
        const qa_content_mount *list = pass == 0 ? firsts[o]->mounts : unique.mounts;
        size_t list_count = pass == 0 ? firsts[o]->count : unique.count;
        for (size_t m = 0; m < list_count; m++) {
          const char *key = mount_physical_key(&list[m]);
          for (size_t u = 0; u < unique.count; u++) {
            if (strcmp(mount_physical_key(&unique.mounts[u]), key) != 0) continue;
            qa_mount_id id = mount_identity_id(&unique.mounts[u]);
            bool seen = false;
            for (size_t s = 0; s < count; s++) if (order[s] == id) seen = true;
            if (!seen && order != NULL) order[count++] = id;
          }
        }
      }
      *counts[o] = count;
    }
  }
  for (size_t g = 0; g < 4; g++) {
    free((void *)groups[g]->mounts);
    free((void *)groups[g]->strings);
  }
  out->mounts = unique;
  out->default_order = default_order;
  out->plan.id = selection->id;
  out->plan.mounts = unique.mounts;
  out->plan.mount_count = unique.count;
  out->plan.default_order = default_order;
  out->plan.default_order_count = default_count;
  if (strcmp(assets->id, geometry->id) != 0) {
    out->prefix_orders = (qa_prefix_mount_order *)malloc(sizeof *out->prefix_orders);
    out->prefix_mounts = geometry_order;
    if (out->prefix_orders != NULL) {
      out->prefix_orders[0].prefix = "maps/";
      out->prefix_orders[0].mounts = geometry_order;
      out->prefix_orders[0].mount_count = geometry_count;
      out->plan.prefix_orders = out->prefix_orders;
      out->plan.prefix_order_count = 1;
    }
  } else {
    free(geometry_order);
  }
  return true;
}

void qa_catalog_mount_plan_result_free(qa_catalog_mount_plan_result *result) {
  qa_content_catalog_mount_list_free(&result->mounts);
  free(result->default_order);
  free(result->prefix_orders);
  free(result->prefix_mounts);
  memset(result, 0, sizeof *result);
}

static bool catalog_plan_for(qa_content_catalog *catalog, const char *content,
                             qa_resolved_mount_plan *plan,
                             qa_catalog_mount_list *mounts,
                             qa_mount_id **order_out, char **id_out) {
  if (!qa_content_catalog_mounts_for(catalog, content, mounts)) return false;
  qa_mount_id *order = NULL;
  if (mounts->count > 0) {
    order = (qa_mount_id *)malloc(mounts->count * sizeof *order);
    if (order == NULL) return false;
    for (size_t i = 0; i < mounts->count; i++)
      order[i] = mount_identity_id(&mounts->mounts[i]);
  }
  char *id = qa_catalog_format("mount-plan:catalog:%lld",
                               (long long)catalog->generation);
  plan->id = id;
  plan->mounts = mounts->mounts;
  plan->mount_count = mounts->count;
  plan->default_order = order;
  plan->default_order_count = mounts->count;
  plan->prefix_orders = NULL;
  plan->prefix_order_count = 0;
  *order_out = order;
  *id_out = id;
  return id != NULL;
}

uint8_t *qa_content_catalog_read(qa_content_catalog *catalog,
                                 qa_catalog_mount_plan_opener opener,
                                 qa_content_id content, const char *path,
                                 size_t *out_length) {
  *out_length = 0;
  qa_catalog_mount_list mounts = {0};
  qa_mount_id *order = NULL;
  char *id = NULL;
  qa_resolved_mount_plan plan = {0};
  if (!catalog_plan_for(catalog, content, &plan, &mounts, &order, &id)) {
    qa_content_catalog_mount_list_free(&mounts); free(order); free(id);
    return NULL;
  }
  qa_catalog_mounted_reader opened = {0};
  if (opener.open_plan == NULL || !opener.open_plan(opener.ctx, &plan, &opened))
    qa_catalog_error("Required resource is missing: %s/%s", content, path);
  qa_catalog_opened_resource resource = {0};
  bool found = opened.open(opened.ctx, path, &resource);
  if (!found) {
    if (opened.close != NULL) opened.close(opened.ctx);
    qa_catalog_error("Required resource is missing: %s/%s", content, path);
  }
  uint8_t *copy = (uint8_t *)malloc(resource.byte_length != 0 ? resource.byte_length : 1);
  if (copy != NULL) {
    memcpy(copy, resource.bytes, resource.byte_length);
    *out_length = resource.byte_length;
  }
  if (opened.close != NULL) opened.close(opened.ctx);
  qa_content_catalog_mount_list_free(&mounts);
  free(order); free(id);
  return copy;
}

void qa_authored_start_catalog_free(qa_authored_start_catalog *catalog) {
  qa_authored_starts view = {catalog->episode, catalog->starts, catalog->start_count};
  qa_authored_starts_free(&view);
  catalog->episode = NULL; catalog->starts = NULL; catalog->start_count = 0;
}

bool qa_content_catalog_authored_starts_for(
    qa_content_catalog *catalog, qa_catalog_mount_plan_opener opener,
    const char *id, qa_authored_start_catalog *out, bool *out_found) {
  memset(out, 0, sizeof *out);
  *out_found = false;
  const qa_catalog_discovered_product *product =
      qa_content_catalog_require(catalog, id);
  if (product->expectation.family != QA_GAME_FAMILY_Q2 ||
      strcmp(product->expectation.edition, "rerelease") != 0) return true;
  qa_catalog_mount_list mounts = {0};
  qa_mount_id *order = NULL;
  char *plan_id = NULL;
  qa_resolved_mount_plan plan = {0};
  if (!catalog_plan_for(catalog, product->id, &plan, &mounts, &order, &plan_id)) {
    qa_content_catalog_mount_list_free(&mounts); free(order); free(plan_id);
    return false;
  }
  qa_catalog_mounted_reader opened = {0};
  if (opener.open_plan == NULL || !opener.open_plan(opener.ctx, &plan, &opened)) {
    qa_content_catalog_mount_list_free(&mounts); free(order); free(plan_id);
    return true;
  }
  qa_catalog_opened_resource resource = {0};
  bool found = opened.open(opened.ctx, "mapdb.json", &resource);
  qa_authored_starts starts = {0};
  bool ok = true;
  if (found) {
    ok = qa_parse_authored_starts(resource.bytes, resource.byte_length,
                                  product->expectation.campaign, &starts);
    if (ok) {
      out->resource = resource.reference;
      out->episode = starts.episode;
      out->starts = starts.starts;
      out->start_count = starts.start_count;
      *out_found = true;
    }
  }
  if (opened.close != NULL) opened.close(opened.ctx);
  qa_content_catalog_mount_list_free(&mounts);
  free(order); free(plan_id);
  return ok;
}

/* ------------------------------------------------------------------ */
/* Discovery.                                                          */

static char *anchored_path(const game_roots *roots, const char *corpus_root,
                           qa_game_family family, const char *canonical_path) {
  const char *game_root = roots->roots[family];
  if (game_root != NULL) {
    char *relative = qa_game_relative_path(canonical_path, family);
    if (relative != NULL && relative[0] == '\0') {
      free(relative);
      return qa_catalog_dup(game_root);
    }
    char *found = relative != NULL ? find_content_path(game_root, relative) : NULL;
    free(relative);
    if (found != NULL) return found;
  }
  return find_content_path(corpus_root, canonical_path);
}

typedef struct inspect_cache {
  qa_catalog_archive_inspector inspector;
  qa_catalog_archive *archives;
  size_t count;
  size_t capacity;
  char **errors; /* parallel: malloc'd message or NULL markers via flags */
  bool *failed;
} inspect_cache;

static bool inspect_cached(inspect_cache *cache, const char *path,
                           qa_archive_format format, qa_catalog_archive *out,
                           char **error_out) {
  *error_out = NULL;
  for (size_t i = 0; i < cache->count; i++) {
    if (!cache->failed[i] && strcmp(cache->archives[i].path, path) == 0)
      return archive_dup(&cache->archives[i], out);
    if (cache->failed[i] && cache->archives[i].path != NULL &&
        strcmp(cache->archives[i].path, path) == 0) {
      *error_out = qa_catalog_dup(cache->errors[i] != NULL ? cache->errors[i] : "inspect failed");
      return false;
    }
  }
  qa_catalog_archive archive = {0};
  char *error = NULL;
  bool ok = cache->inspector.inspect != NULL &&
            cache->inspector.inspect(cache->inspector.ctx, path, format,
                                     &archive, &error);
  if (cache->count == cache->capacity) {
    size_t next = cache->capacity != 0 ? cache->capacity * 2 : 8;
    qa_catalog_archive *grown_archives =
        (qa_catalog_archive *)realloc(cache->archives, next * sizeof *grown_archives);
    char **grown_errors = (char **)realloc(cache->errors, next * sizeof *grown_errors);
    bool *grown_failed = (bool *)realloc(cache->failed, next * sizeof *grown_failed);
    if (grown_archives == NULL || grown_errors == NULL || grown_failed == NULL) {
      if (ok) *out = archive;
      else { *error_out = error; }
      return ok;
    }
    cache->archives = grown_archives; cache->errors = grown_errors;
    cache->failed = grown_failed; cache->capacity = next;
  }
  size_t slot = cache->count++;
  cache->failed[slot] = !ok;
  cache->errors[slot] = ok ? NULL : error;
  if (ok) {
    cache->archives[slot] = archive;
    return archive_dup(&archive, out);
  }
  memset(&cache->archives[slot], 0, sizeof archive);
  cache->archives[slot].path = qa_catalog_dup(path);
  *error_out = qa_catalog_dup(error != NULL ? error : "inspect failed");
  return false;
}

static void inspect_cache_free(inspect_cache *cache) {
  for (size_t i = 0; i < cache->count; i++) {
    qa_catalog_archive_free(&cache->archives[i]);
    free(cache->errors[i]);
  }
  free(cache->archives); free(cache->errors); free(cache->failed);
}

typedef struct expectation_list {
  qa_catalog_product_expectation *values;
  size_t count;
  size_t capacity;
} expectation_list;

static bool expectation_push(expectation_list *list,
                             qa_catalog_product_expectation expectation) {
  if (list->count == list->capacity) {
    size_t next = list->capacity != 0 ? list->capacity * 2 : 8;
    qa_catalog_product_expectation *grown = (qa_catalog_product_expectation *)realloc(
        list->values, next * sizeof *grown);
    if (grown == NULL) return false;
    list->values = grown; list->capacity = next;
  }
  list->values[list->count++] = expectation;
  return true;
}

static void expectation_list_free(expectation_list *list) {
  for (size_t i = 0; i < list->count; i++) expectation_free(&list->values[i]);
  free(list->values);
  memset(list, 0, sizeof *list);
}

static bool valid_mod_name(const char *name, bool allow_empty_first) {
  (void)allow_empty_first;
  if (name == NULL || !isalnum((unsigned char)name[0])) return false;
  for (size_t i = 1; name[i] != '\0'; i++) {
    char c = name[i];
    if (!isalnum((unsigned char)c) && c != '.' && c != '_' && c != '+' && c != '-')
      return false;
  }
  return true;
}

typedef struct inspected_mod {
  qa_catalog_string_list archive_names;
  qa_catalog_string_list program_names; /* lower-cased */
} inspected_mod;

static bool inspect_mod_directory(const char *physical,
                                  const qa_catalog_product_expectation *base,
                                  inspect_cache *cache, inspected_mod *out,
                                  bool *found) {
  memset(out, 0, sizeof *out);
  *found = false;
  dir_list members = {0};
  if (!read_directory(physical, &members, NULL)) return true;
  bool has_content = false;
  for (size_t i = 0; i < members.count; i++) {
    qa_archive_format format;
    if (members.values[i].is_file && archive_format_for(members.values[i].name, &format))
      has_content = true;
    if (members.values[i].is_directory &&
        (qa_catalog_ieq(members.values[i].name, "maps") ||
         qa_catalog_ieq(members.values[i].name, "models") ||
         qa_catalog_ieq(members.values[i].name, "vm")))
      has_content = true;
    if (members.values[i].is_file) {
      char *lower = qa_catalog_lower_dup(members.values[i].name);
      if (lower != NULL && (strcmp(lower, "qwprogs.dat") == 0 ||
                            strcmp(lower, "progs.dat") == 0 ||
                            strcmp(lower, "qprogs.dat") == 0 ||
                            (qa_catalog_starts_with(lower, "game") &&
                             (qa_catalog_ends_with(lower, ".dll") ||
                              qa_catalog_ends_with(lower, ".so")))))
        has_content = true;
      free(lower);
    }
  }
  if (!has_content) { dir_list_free(&members); return true; }
  for (size_t i = 0; i < members.count; i++) {
    if (!members.values[i].is_file) continue;
    qa_archive_format format;
    if (!archive_format_for(members.values[i].name, &format)) continue;
    if (!qa_catalog_string_push(&out->archive_names,
                                qa_catalog_dup(members.values[i].name))) {
      dir_list_free(&members); return false;
    }
  }
  for (size_t i = 0; i < members.count; i++) {
    if (!members.values[i].is_file) continue;
    char *lower = qa_catalog_lower_dup(members.values[i].name);
    if (lower == NULL || !qa_catalog_string_push(&out->program_names, lower)) {
      free(lower); dir_list_free(&members); return false;
    }
  }
  if (base->family == QA_GAME_FAMILY_Q3) {
    char *qvm = find_content_path(physical, "vm/qagame.qvm");
    if (qvm != NULL) {
      free(qvm);
      (void)qa_catalog_string_push(&out->program_names, qa_catalog_dup("vm/qagame.qvm"));
    }
  }
  for (size_t i = 0; i < members.count; i++) {
    qa_archive_format format;
    if (!members.values[i].is_file ||
        !archive_format_for(members.values[i].name, &format)) continue;
    if (base->family == QA_GAME_FAMILY_Q2 &&
        format != QA_ARCHIVE_FORMAT_PAK &&
        !(format == QA_ARCHIVE_FORMAT_ZIP &&
          qa_catalog_ieq(strrchr(members.values[i].name, '.'), ".pkz"))) continue;
    char *full = qa_catalog_join(physical, members.values[i].name);
    if (full == NULL) continue;
    char *resolved = resolve_path(NULL, full);
    free(full);
    if (resolved == NULL) continue;
    qa_catalog_archive archive = {0};
    char *error = NULL;
    if (inspect_cached(cache, resolved, format, &archive, &error)) {
      for (size_t e = 0; e < archive.entry_count; e++) {
        char *lower = qa_catalog_lower_dup(archive.entries[e].path);
        if (lower != NULL) (void)qa_catalog_string_push(&out->program_names, lower);
      }
      qa_catalog_archive_free(&archive);
    }
    free(error);
    free(resolved);
  }
  dir_list_free(&members);
  *found = true;
  return true;
}

static bool string_list_contains(const qa_catalog_string_list *list, const char *text) {
  return qa_catalog_string_contains((const char *const *)list->values, list->count, text);
}

static bool mod_expectations(const char *directory, const char *name,
                             const char *content_directory, const char *title,
                             const qa_catalog_product_expectation *base,
                             const qa_catalog_product_expectation *products,
                             size_t product_count,
                             const qa_catalog_product_expectation *known,
                             size_t known_count, const inspected_mod *inspected,
                             expectation_list *out) {
  const qa_catalog_product_expectation *variants[2];
  size_t variant_count = 0;
  variants[variant_count++] = base;
  const qa_catalog_product_expectation *quakeworld = NULL;
  if (base->family == QA_GAME_FAMILY_Q1) {
    for (size_t i = 0; i < product_count; i++) {
      char *parent = qa_catalog_dirname(products[i].content_directory);
      bool same = strcmp(products[i].edition, "quakeworld") == 0 &&
                  parent != NULL && strcmp(parent, directory) == 0;
      free(parent);
      if (same) { quakeworld = &products[i]; break; }
    }
  }
  if (quakeworld != NULL && string_list_contains(&inspected->program_names, "qwprogs.dat"))
    variants[variant_count++] = quakeworld;
  const char *q2_library = strcmp(base->edition, "rerelease") == 0
                               ? "game_x64.dll" : "gamex86.dll";
  bool q2_windows = base->family == QA_GAME_FAMILY_Q2 &&
                    string_list_contains(&inspected->program_names, q2_library);
  bool q2_other = false;
  if (base->family == QA_GAME_FAMILY_Q2) {
    for (size_t i = 0; i < inspected->program_names.count; i++) {
      const char *program = inspected->program_names.values[i];
      if (qa_catalog_starts_with(program, "game") && strchr(program, '/') == NULL &&
          (qa_catalog_ends_with(program, ".dll") || qa_catalog_ends_with(program, ".so")))
        q2_other = true;
    }
  }
  for (size_t v = 0; v < variant_count; v++) {
    const qa_catalog_product_expectation *variant = variants[v];
    bool known_edition = false;
    for (size_t i = 0; i < known_count; i++)
      if (strcmp(known[i].edition, variant->edition) == 0) known_edition = true;
    char *id = qa_catalog_format("%s-%s-%s", qa_game_family_name(variant->family),
                                 variant->edition, name);
    bool duplicate = false;
    for (size_t i = 0; i < product_count; i++)
      if (id != NULL && strcmp(products[i].id, id) == 0) duplicate = true;
    if (known_edition || duplicate) { free(id); continue; }
    qa_catalog_product_expectation expectation = {0};
    expectation.id = id;
    expectation.family = variant->family;
    expectation.edition = qa_catalog_dup(variant->edition);
    expectation.campaign = qa_catalog_dup(name);
    expectation.title =
        title != NULL ? qa_catalog_dup(title)
        : strcmp(variant->edition, "quakeworld") == 0
            ? qa_catalog_format("%s (QuakeWorld)", name)
            : qa_catalog_dup(name);
    expectation.content_directory = qa_catalog_dup(content_directory);
    expectation.base_product = qa_catalog_dup(variant->id);
    if (inspected->archive_names.count > 0) {
      const char **required = (const char **)calloc(
          inspected->archive_names.count, sizeof *required);
      if (required == NULL) { expectation_free(&expectation); return false; }
      for (size_t i = 0; i < inspected->archive_names.count; i++)
        required[i] = qa_catalog_format("%s/%s", content_directory,
                                        inspected->archive_names.values[i]);
      expectation.required_content_archives = required;
      expectation.required_content_archive_count = inspected->archive_names.count;
    }
    const char *program = NULL;
    if (strcmp(variant->edition, "quakeworld") == 0) program = "qwprogs.dat";
    else if (q2_windows) program = q2_library;
    else if (variant->family == QA_GAME_FAMILY_Q1 &&
             string_list_contains(&inspected->program_names, "progs.dat"))
      program = "progs.dat";
    else if (variant->family == QA_GAME_FAMILY_Q3 &&
             string_list_contains(&inspected->program_names, "vm/qagame.qvm"))
      program = "vm/qagame.qvm";
    if (program != NULL) {
      const char **required = (const char **)calloc(1, sizeof *required);
      if (required == NULL) { expectation_free(&expectation); return false; }
      required[0] = qa_catalog_dup(program);
      expectation.required_programs = required;
      expectation.required_program_count = 1;
    }
    if (q2_other && !q2_windows) {
      expectation.unresolved_reason = qa_catalog_format(
          "This Quake II add-on needs %s; its installed game module is unsupported.",
          strcmp(base->edition, "rerelease") == 0
              ? "a Windows x64 game_x64.dll" : "a Windows i386 gamex86.dll");
    }
    if (!expectation_push(out, expectation)) {
      expectation_free(&expectation);
      return false;
    }
  }
  return true;
}

static bool discover_mods(const char *root,
                          const qa_catalog_product_expectation *products,
                          size_t product_count, inspect_cache *cache,
                          const game_roots *roots, expectation_list *out) {
  /* Roots: first baseProduct==null product per content parent. */
  for (size_t p = 0; p < product_count; p++) {
    const qa_catalog_product_expectation *base = &products[p];
    if (base->base_product != NULL) continue;
    char *directory = qa_catalog_dirname(base->content_directory);
    bool seen = false;
    for (size_t q = 0; q < p; q++) {
      if (products[q].base_product != NULL) continue;
      char *other = qa_catalog_dirname(products[q].content_directory);
      if (other != NULL && directory != NULL && strcmp(other, directory) == 0) seen = true;
      free(other);
      if (seen) break;
    }
    if (seen) { free(directory); continue; }
    char *installed = anchored_path(roots, root, base->family, directory);
    if (installed == NULL) { free(directory); continue; }
    dir_list entries = {0};
    if (!read_directory(installed, &entries, NULL)) {
      free(directory); free(installed); continue;
    }
    for (size_t e = 0; e < entries.count; e++) {
      const char *entry_name = entries.values[e].name;
      char *content_directory = qa_catalog_format("%s/%s", directory, entry_name);
      bool skip = !entries.values[e].is_directory ||
                  qa_catalog_ieq(entry_name, "rerelease");
      const qa_catalog_product_expectation *known[64];
      size_t known_count = 0;
      if (!skip) {
        for (size_t i = 0; i < product_count && known_count < 64; i++)
          if (qa_catalog_ieq(products[i].content_directory, content_directory))
            known[known_count++] = &products[i];
        if (known_count != 0) {
          bool quakeworld = false;
          for (size_t i = 0; i < known_count; i++)
            if (strcmp(known[i]->edition, "quakeworld") == 0) quakeworld = true;
          if (base->family != QA_GAME_FAMILY_Q1 || quakeworld) skip = true;
        }
      }
      if (!skip && qa_managed_addon_hidden(root, content_directory)) skip = true;
      if (!skip) {
        char *physical = resolve_path(installed, entry_name);
        inspected_mod inspected = {0};
        bool found = false;
        bool ok = physical != NULL &&
                  inspect_mod_directory(physical, base, cache, &inspected, &found);
        free(physical);
        if (ok && found) {
          if (!valid_mod_name(entry_name, false))
            qa_catalog_error("Mod directory needs a valid content identity: %s",
                             content_directory);
          char *addon_title = qa_managed_addon_title(root, content_directory);
          /* Flatten known pointers into a value list view. */
          qa_catalog_product_expectation known_values[64];
          for (size_t i = 0; i < known_count; i++) known_values[i] = *known[i];
          (void)mod_expectations(directory, entry_name, content_directory,
                                 addon_title, base, products, product_count,
                                 known_values, known_count, &inspected, out);
          free(addon_title);
        }
        qa_catalog_string_list_free(&inspected.archive_names);
        qa_catalog_string_list_free(&inspected.program_names);
      }
      free(content_directory);
    }
    dir_list_free(&entries);
    free(installed);
    free(directory);
  }
  return true;
}

void qa_catalog_addon_directory_list_free(qa_catalog_addon_directory_list *list) {
  for (size_t i = 0; i < list->count; i++) {
    free(list->values[i].name); free(list->values[i].content_directory);
    free(list->values[i].physical); free(list->values[i].title);
  }
  free(list->values);
  memset(list, 0, sizeof *list);
}

const qa_catalog_addon_directory *qa_catalog_addon_directory_find(
    const qa_catalog_addon_directory_list *list, const char *content_directory) {
  for (size_t i = 0; i < list->count; i++)
    if (qa_catalog_ieq(list->values[i].content_directory, content_directory))
      return &list->values[i];
  return NULL;
}

static bool addon_directory_push(qa_catalog_addon_directory_list *list,
                                 qa_catalog_addon_directory directory,
                                 size_t *capacity) {
  if (qa_catalog_addon_directory_find(list, directory.content_directory) != NULL) {
    free(directory.name); free(directory.content_directory);
    free(directory.physical); free(directory.title);
    return true;
  }
  if (list->count == *capacity) {
    size_t next = *capacity != 0 ? *capacity * 2 : 8;
    qa_catalog_addon_directory *grown = (qa_catalog_addon_directory *)realloc(
        list->values, next * sizeof *grown);
    if (grown == NULL) return false;
    list->values = grown; *capacity = next;
  }
  list->values[list->count++] = directory;
  return true;
}

static bool excluded_addon_name(const char *lower) {
  return strcmp(lower, "id1") == 0 || strcmp(lower, "baseq2") == 0 ||
         strcmp(lower, "baseq3") == 0 || strcmp(lower, "rerelease") == 0 ||
         strcmp(lower, "save") == 0;
}

bool qa_discover_addon_directories(
    const qa_catalog_nightdive_addon_root *roots, size_t root_count,
    qa_catalog_addon_directory_list *out) {
  memset(out, 0, sizeof *out);
  size_t capacity = 0;
  for (size_t r = 0; r < root_count; r++) {
    qa_game_family family = roots[r].family;
    const char *root = roots[r].root;
    if (family == QA_GAME_FAMILY_Q3) continue;
    const char *prefix = family == QA_GAME_FAMILY_Q1 ? "q1/rerelease" : "q2/rerelease";
    qa_nightdive_addon_list titles = {0};
    (void)qa_read_nightdive_addons(root, &titles);
    dir_list entries = {0};
    if (!read_directory(root, &entries, NULL)) {
      qa_nightdive_addon_list_free(&titles);
      continue;
    }
    for (size_t e = 0; e < entries.count; e++) {
      char *lower = qa_catalog_lower_dup(entries.values[e].name);
      bool skip = lower == NULL || excluded_addon_name(lower) ||
                  !valid_mod_name(entries.values[e].name, false);
      char *physical = resolve_path(root, entries.values[e].name);
      bool is_dir = entries.values[e].is_directory ||
                    (entries.values[e].is_symlink && path_is_directory(physical));
      if (!skip && is_dir) {
        qa_catalog_addon_directory directory = {0};
        directory.family = family;
        directory.name = qa_catalog_dup(entries.values[e].name);
        directory.content_directory =
            qa_catalog_format("%s/%s", prefix, entries.values[e].name);
        directory.physical = qa_catalog_dup(physical);
        for (size_t t = 0; t < titles.count; t++)
          if (qa_catalog_ieq(titles.values[t].game_directory, lower))
            directory.title = qa_catalog_dup(titles.values[t].title);
        if (!addon_directory_push(out, directory, &capacity)) {
          free(lower); free(physical);
          dir_list_free(&entries);
          qa_nightdive_addon_list_free(&titles);
          return false;
        }
      }
      free(lower); free(physical);
    }
    dir_list_free(&entries);
    qa_nightdive_addon_list_free(&titles);
  }
  return true;
}

static bool discover_sibling_directories(
    const game_roots *const *root_sets, size_t root_set_count,
    qa_catalog_addon_directory_list *out) {
  memset(out, 0, sizeof *out);
  size_t capacity = 0;
  for (size_t r = 0; r < root_set_count; r++) {
    for (int f = 0; f < 2; f++) {
      qa_game_family family = (qa_game_family)f;
      const char *game_root = root_sets[r]->roots[family];
      if (game_root == NULL) continue;
      const char *canonical = qa_game_family_directory(family);
      char *rerelease_root = find_content_path(game_root, "rerelease");
      const char *trees[2] = {game_root, rerelease_root};
      for (size_t t = 0; t < 2; t++) {
        if (trees[t] == NULL) continue;
        char *other = t == 0 ? qa_catalog_format("%s/rerelease", canonical)
                             : qa_catalog_dup(canonical);
        dir_list entries = {0};
        if (read_directory(trees[t], &entries, NULL)) {
          for (size_t e = 0; e < entries.count; e++) {
            char *lower = qa_catalog_lower_dup(entries.values[e].name);
            bool skip = lower == NULL || excluded_addon_name(lower) ||
                        !valid_mod_name(entries.values[e].name, false);
            char *physical = resolve_path(trees[t], entries.values[e].name);
            bool is_dir = entries.values[e].is_directory ||
                          (entries.values[e].is_symlink && path_is_directory(physical));
            if (!skip && is_dir) {
              qa_catalog_addon_directory directory = {0};
              directory.family = family;
              directory.name = qa_catalog_dup(entries.values[e].name);
              directory.content_directory =
                  qa_catalog_format("%s/%s", other, entries.values[e].name);
              directory.physical = qa_catalog_dup(physical);
              directory.title = NULL;
              (void)addon_directory_push(out, directory, &capacity);
            }
            free(lower); free(physical);
          }
        }
        dir_list_free(&entries);
        free(other);
      }
      free(rerelease_root);
    }
  }
  return true;
}

static bool discover_addon_mods(const qa_catalog_addon_directory_list *addons,
                                const qa_catalog_product_expectation *products,
                                size_t product_count, inspect_cache *cache,
                                expectation_list *out) {
  for (size_t a = 0; a < addons->count; a++) {
    const qa_catalog_addon_directory *addon = &addons->values[a];
    char *parent = qa_catalog_dirname(addon->content_directory);
    const qa_catalog_product_expectation *base = NULL;
    for (size_t i = 0; i < product_count; i++) {
      if (products[i].base_product != NULL || products[i].family != addon->family)
        continue;
      char *candidate = qa_catalog_dirname(products[i].content_directory);
      bool same = candidate != NULL && parent != NULL && strcmp(candidate, parent) == 0;
      free(candidate);
      if (same) { base = &products[i]; break; }
    }
    free(parent);
    if (base == NULL) continue;
    qa_catalog_product_expectation known_values[64];
    size_t known_count = 0;
    for (size_t i = 0; i < product_count && known_count < 64; i++)
      if (qa_catalog_ieq(products[i].content_directory, addon->content_directory))
        known_values[known_count++] = products[i];
    if (known_count != 0) {
      bool quakeworld = false;
      for (size_t i = 0; i < known_count; i++)
        if (strcmp(known_values[i].edition, "quakeworld") == 0) quakeworld = true;
      if (addon->family != QA_GAME_FAMILY_Q1 || quakeworld) continue;
    }
    inspected_mod inspected = {0};
    bool found = false;
    (void)inspect_mod_directory(addon->physical, base, cache, &inspected, &found);
    if (found) {
      char *directory = qa_catalog_dirname(addon->content_directory);
      (void)mod_expectations(directory, addon->name, addon->content_directory,
                             addon->title, base, products, product_count,
                             known_values, known_count, &inspected, out);
      free(directory);
    }
    qa_catalog_string_list_free(&inspected.archive_names);
    qa_catalog_string_list_free(&inspected.program_names);
  }
  return true;
}

typedef struct map_list {
  qa_catalog_content_map *values;
  size_t count;
  size_t capacity;
} map_list;

static bool map_push_owned(map_list *list, char *path, char *source,
                           bool has_member, int64_t member) {
  if (list->count == list->capacity) {
    size_t next = list->capacity != 0 ? list->capacity * 2 : 8;
    qa_catalog_content_map *grown =
        (qa_catalog_content_map *)realloc(list->values, next * sizeof *grown);
    if (grown == NULL) return false;
    list->values = grown; list->capacity = next;
  }
  qa_catalog_content_map *map = &list->values[list->count++];
  map->path = path; map->source = source;
  map->has_member_index = has_member; map->member_index = member;
  return true;
}

static void map_list_free(map_list *list) {
  for (size_t i = 0; i < list->count; i++) {
    free(list->values[i].path); free(list->values[i].source);
  }
  free(list->values);
  memset(list, 0, sizeof *list);
}

static bool map_list_extend(map_list *list, map_list *other) {
  for (size_t i = 0; i < other->count; i++) {
    if (!map_push_owned(list, other->values[i].path, other->values[i].source,
                        other->values[i].has_member_index,
                        other->values[i].member_index)) return false;
  }
  free(other->values);
  memset(other, 0, sizeof *other);
  return true;
}

static bool is_bsp_path(const char *path) {
  return qa_catalog_ends_with(path, ".bsp") || qa_catalog_ends_with(path, ".BSP") ||
         (strlen(path) >= 4 && tolower((unsigned char)path[strlen(path) - 3]) == 'b' &&
          tolower((unsigned char)path[strlen(path) - 2]) == 's' &&
          tolower((unsigned char)path[strlen(path) - 1]) == 'p' &&
          path[strlen(path) - 4] == '.');
}

static bool archive_maps(const qa_catalog_archive *archives, size_t count,
                         map_list *out) {
  for (size_t a = 0; a < count; a++) {
    for (size_t e = 0; e < archives[a].entry_count; e++) {
      const char *path = archives[a].entries[e].path;
      if (strncasecmp(path, "maps/", 5) != 0 || !is_bsp_path(path)) continue;
      if (!map_push_owned(out, qa_catalog_dup(path),
                          qa_catalog_dup(archives[a].path), true,
                          archives[a].entries[e].ordinal)) return false;
    }
  }
  return true;
}

static bool loose_walk(const char *root, const char *directory, map_list *out) {
  dir_list entries = {0};
  if (!read_directory(directory, &entries, NULL)) return true;
  for (size_t i = 0; i < entries.count; i++) {
    char *full = qa_catalog_join(directory, entries.values[i].name);
    char *path = full != NULL ? resolve_path(NULL, full) : NULL;
    free(full);
    if (path == NULL) continue;
    if (entries.values[i].is_directory) {
      if (!loose_walk(root, path, out)) { free(path); dir_list_free(&entries); return false; }
      free(path);
    } else if (entries.values[i].is_file && is_bsp_path(entries.values[i].name)) {
      char *relative = relative_path(root, path);
      if (relative == NULL || !map_push_owned(out, relative, path, false, 0)) {
        free(relative); free(path); dir_list_free(&entries); return false;
      }
    } else {
      free(path);
    }
  }
  dir_list_free(&entries);
  return true;
}

static int compare_content_maps(const void *left, const void *right) {
  const qa_catalog_content_map *a = (const qa_catalog_content_map *)left;
  const qa_catalog_content_map *b = (const qa_catalog_content_map *)right;
  return qa_catalog_source_compare(a->path, b->path);
}

static bool loose_maps(const char *root, map_list *out) {
  char *maps_root = find_content_path(root, "maps");
  if (maps_root == NULL) return true;
  bool ok = loose_walk(root, maps_root, out);
  free(maps_root);
  if (ok && out->count > 1)
    qsort(out->values, out->count, sizeof *out->values, compare_content_maps);
  return ok;
}

typedef struct archive_list {
  qa_catalog_archive *values;
  size_t count;
  size_t capacity;
} archive_list;

static bool archive_push(archive_list *list, qa_catalog_archive archive) {
  if (list->count == list->capacity) {
    size_t next = list->capacity != 0 ? list->capacity * 2 : 8;
    qa_catalog_archive *grown =
        (qa_catalog_archive *)realloc(list->values, next * sizeof *grown);
    if (grown == NULL) return false;
    list->values = grown; list->capacity = next;
  }
  list->values[list->count++] = archive;
  return true;
}

static void collect_archives(const char *directory, inspect_cache *cache,
                             archive_list *out, qa_catalog_string_list *diagnostics) {
  dir_list entries = {0};
  int error = 0;
  if (!read_directory(directory, &entries, &error)) return;
  for (size_t i = 0; i < entries.count; i++) {
    qa_archive_format format;
    if (!entries.values[i].is_file ||
        !archive_format_for(entries.values[i].name, &format)) continue;
    char *full = resolve_path(directory, entries.values[i].name);
    if (full == NULL) continue;
    qa_catalog_archive archive = {0};
    char *inspect_error = NULL;
    if (inspect_cached(cache, full, format, &archive, &inspect_error)) {
      (void)archive_push(out, archive);
    } else {
      char *message = qa_catalog_format("%s: %s", entries.values[i].name,
                                        inspect_error != NULL ? inspect_error : "");
      if (message != NULL) (void)qa_catalog_string_push(diagnostics, message);
    }
    free(inspect_error);
    free(full);
  }
  dir_list_free(&entries);
}

static bool mod_description(const qa_catalog_product_expectation *product,
                            const char *const *roots, const game_roots *root_sets,
                            size_t root_count, char **title_out) {
  *title_out = NULL;
  if (product->family != QA_GAME_FAMILY_Q3) return true;
  for (size_t i = 0; i < root_count; i++) {
    char *wanted = qa_catalog_format("%s/description.txt", product->content_directory);
    char *path = anchored_path(&root_sets[i], roots[i], product->family, wanted);
    free(wanted);
    if (path == NULL) continue;
    size_t length = 0;
    uint8_t *bytes = read_file_prefix(path, 48, &length);
    free(path);
    if (bytes == NULL) continue; /* TS open would throw; first hit wins */
    if (length == 0) { free(bytes); return true; }
    size_t end = length;
    for (size_t j = 0; j < length; j++)
      if (bytes[j] == 0) { end = j; break; }
    *title_out = qa_catalog_format("%.*s", (int)end, (const char *)bytes);
    free(bytes);
    return *title_out != NULL;
  }
  return true;
}

bool qa_discover_installed_content(const qa_discover_content_options *options,
                                   qa_content_catalog *out) {
  memset(out, 0, sizeof *out);
  char *corpus_root = resolve_path(NULL, options->corpus_root);
  char *user_root = options->user_content_root != NULL
                        ? resolve_path(NULL, options->user_content_root) : NULL;
  double generation_number = options->has_generation ? options->generation : 0;
  if (!(generation_number >= 0 && generation_number <= 9007199254740991.0 &&
        generation_number == (double)(int64_t)generation_number)) {
    free(corpus_root); free(user_root);
    qa_range_error("Catalog generation must be a nonnegative integer");
  }
  int64_t generation = (int64_t)generation_number;
  const qa_catalog_product_expectation *stock = options->products;
  size_t stock_count = options->product_count;
  if (stock == NULL) { stock = qa_expected_products; stock_count = qa_expected_product_count; }
  expectation_list expected = {0};
  for (size_t i = 0; i < stock_count; i++) {
    qa_catalog_product_expectation copy = {0};
    if (!expectation_dup(&stock[i], &copy) || !expectation_push(&expected, copy)) {
      expectation_free(&copy);
      expectation_list_free(&expected);
      free(corpus_root); free(user_root);
      return false;
    }
  }
  qa_catalog_string_list remote_overlays = {0};
  bool has_remote = options->remote_content != NULL;
  if (has_remote) {
    qa_catalog_product_expectation remote = {0};
    if (!remote_expectation(options->remote_content, stock, stock_count, &remote)) {
      expectation_list_free(&expected); free(corpus_root); free(user_root);
      return false;
    }
    if (strcmp(remote.id, options->remote_content->base) != 0)
      (void)qa_catalog_string_push(&remote_overlays, qa_catalog_dup(remote.id));
    bool present = false;
    for (size_t i = 0; i < stock_count; i++)
      if (strcmp(stock[i].id, remote.id) == 0) present = true;
    if (present) expectation_free(&remote);
    else (void)expectation_push(&expected, remote);
  }
  if (has_remote && strcmp(options->remote_content->base, "q1-quakeworld") == 0)
    (void)qa_catalog_string_push(&remote_overlays,
                                 qa_catalog_dup(options->remote_content->base));
  inspect_cache cache = {0};
  cache.inspector = options->inspector;
  game_roots corpus_roots, user_roots;
  resolve_game_roots(corpus_root, &corpus_roots);
  if (user_root != NULL) resolve_game_roots(user_root, &user_roots);
  else { memset(&user_roots, 0, sizeof user_roots); for (int i = 0; i < 3; i++) user_roots.roots[i] = qa_catalog_dup(corpus_roots.roots[i] != NULL ? corpus_roots.roots[i] : ""); for (int i = 0; i < 3; i++) if (corpus_roots.roots[i] == NULL) { free(user_roots.roots[i]); user_roots.roots[i] = NULL; } }
  expectation_list corpus_mods = {0}, user_mods = {0}, addon_mods = {0};
  if (options->discover_mods)
    (void)discover_mods(corpus_root, expected.values, expected.count, &cache,
                        &corpus_roots, &corpus_mods);
  if (options->discover_mods && user_root != NULL) {
    expectation_list combined = {0};
    for (size_t i = 0; i < expected.count; i++) {
      qa_catalog_product_expectation copy = {0};
      if (expectation_dup(&expected.values[i], &copy)) (void)expectation_push(&combined, copy);
    }
    for (size_t i = 0; i < corpus_mods.count; i++) {
      qa_catalog_product_expectation copy = {0};
      if (expectation_dup(&corpus_mods.values[i], &copy)) (void)expectation_push(&combined, copy);
    }
    (void)discover_mods(user_root, combined.values, combined.count, &cache,
                        &user_roots, &user_mods);
    expectation_list_free(&combined);
  }
  qa_catalog_addon_directory_list siblings = {0}, steam = {0}, addon_directories = {0};
  {
    const game_roots *sets[2] = {&corpus_roots, &user_roots};
    size_t set_count = user_root == NULL ? 1 : 2;
    if (user_root != NULL) sets[0] = &user_roots, sets[1] = &corpus_roots;
    (void)discover_sibling_directories(sets, set_count, &siblings);
  }
  (void)qa_discover_addon_directories(options->addon_content_roots,
                                      options->addon_content_root_count, &steam);
  size_t addon_capacity = 0;
  for (size_t i = 0; i < siblings.count; i++) {
    qa_catalog_addon_directory copy = {0};
    copy.family = siblings.values[i].family;
    copy.name = qa_catalog_dup(siblings.values[i].name);
    copy.content_directory = qa_catalog_dup(siblings.values[i].content_directory);
    copy.physical = qa_catalog_dup(siblings.values[i].physical);
    copy.title = qa_catalog_dup(siblings.values[i].title);
    (void)addon_directory_push(&addon_directories, copy, &addon_capacity);
  }
  for (size_t i = 0; i < steam.count; i++) {
    qa_catalog_addon_directory copy = {0};
    copy.family = steam.values[i].family;
    copy.name = qa_catalog_dup(steam.values[i].name);
    copy.content_directory = qa_catalog_dup(steam.values[i].content_directory);
    copy.physical = qa_catalog_dup(steam.values[i].physical);
    copy.title = qa_catalog_dup(steam.values[i].title);
    (void)addon_directory_push(&addon_directories, copy, &addon_capacity);
  }
  for (size_t i = 0; i < steam.count; i++) {
    for (size_t j = 0; j < addon_directories.count; j++) {
      if (qa_catalog_ieq(addon_directories.values[j].content_directory,
                         steam.values[i].content_directory) &&
          addon_directories.values[j].title == NULL && steam.values[i].title != NULL)
        addon_directories.values[j].title = qa_catalog_dup(steam.values[i].title);
    }
  }
  qa_catalog_addon_directory_list_free(&siblings);
  qa_catalog_addon_directory_list_free(&steam);
  if (options->discover_mods) {
    expectation_list combined = {0};
    const expectation_list *groups[3] = {&expected, &corpus_mods, &user_mods};
    for (size_t g = 0; g < 3; g++)
      for (size_t i = 0; i < groups[g]->count; i++) {
        qa_catalog_product_expectation copy = {0};
        if (expectation_dup(&groups[g]->values[i], &copy))
          (void)expectation_push(&combined, copy);
      }
    (void)discover_addon_mods(&addon_directories, combined.values, combined.count,
                              &cache, &addon_mods);
    expectation_list_free(&combined);
  }
  qa_catalog_string_list addon_mod_ids = {0}, user_mod_ids = {0};
  for (size_t i = 0; i < addon_mods.count; i++)
    (void)qa_catalog_string_push(&addon_mod_ids, qa_catalog_dup(addon_mods.values[i].id));
  for (size_t i = 0; i < user_mods.count; i++)
    (void)qa_catalog_string_push(&user_mod_ids, qa_catalog_dup(user_mods.values[i].id));
  for (size_t i = 0; i < remote_overlays.count; i++)
    (void)qa_catalog_string_push(&user_mod_ids, qa_catalog_dup(remote_overlays.values[i]));
  expectation_list expectations = {0};
  {
    const expectation_list *groups[4] = {&expected, &corpus_mods, &user_mods, &addon_mods};
    const char *description_roots[2] = {corpus_root, user_root};
    game_roots description_sets[2];
    description_sets[0] = corpus_roots;
    description_sets[1] = user_roots;
    size_t description_count = user_root == NULL ? 1 : 2;
    if (user_root != NULL) {
      description_roots[0] = user_root; description_roots[1] = corpus_root;
      description_sets[0] = user_roots;
      description_sets[1] = corpus_roots;
    }
    for (size_t g = 0; g < 4; g++) {
      for (size_t i = 0; i < groups[g]->count; i++) {
        qa_catalog_product_expectation copy = {0};
        if (!expectation_dup(&groups[g]->values[i], &copy)) continue;
        bool is_stock = false;
        for (size_t s = 0; s < stock_count; s++)
          if (strcmp(stock[s].id, copy.id) == 0) is_stock = true;
        if (!is_stock) {
          char *title = NULL;
          (void)mod_description(&copy, description_roots, description_sets,
                                description_count, &title);
          if (title != NULL) {
            free((void *)copy.title);
            copy.title = title;
          }
        }
        (void)expectation_push(&expectations, copy);
      }
    }
    (void)description_sets;
  }
  expectation_list_free(&expected);
  expectation_list_free(&corpus_mods);
  expectation_list_free(&user_mods);
  expectation_list_free(&addon_mods);
  qa_catalog_discovered_product *products = NULL;
  if (expectations.count > 0)
    products = (qa_catalog_discovered_product *)calloc(expectations.count,
                                                       sizeof *products);
  archive_list root_archives = {0};
  size_t product_count = 0;
  for (size_t e = 0; e < expectations.count; e++) {
    qa_catalog_product_expectation *expectation = &expectations.values[e];
    char *normalized = qa_catalog_normalize_resource_path(expectation->content_directory);
    free(normalized);
    qa_content_identity identity = {0};
    identity.family = expectation->family;
    identity.edition = expectation->edition;
    identity.package = expectation->campaign;
    identity.revision = "installed";
    char *content_id = qa_create_content_id(identity);
    char *root_path = anchored_path(&corpus_roots, corpus_root, expectation->family,
                                    expectation->content_directory);
    char *loose_root = root_path != NULL && path_is_directory(root_path) ? root_path : NULL;
    if (loose_root == NULL) free(root_path);
    archive_list found = {0};
    qa_catalog_string_list diagnostics = {0};
    if (loose_root != NULL) {
      collect_archives(loose_root, &cache, &found, &diagnostics);
      if (strcmp(expectation->edition, "rerelease") == 0) {
        const char *kpf = expectation->family == QA_GAME_FAMILY_Q1
                              ? "QuakeEX.kpf" : "Q2Game.kpf";
        char *parent = qa_catalog_dirname(loose_root);
        char *kpf_path = parent != NULL ? find_content_path(parent, kpf) : NULL;
        free(parent);
        if (kpf_path != NULL) {
          bool present = false;
          for (size_t i = 0; i < root_archives.count; i++)
            if (strcmp(root_archives.values[i].path, kpf_path) == 0) present = true;
          if (!present) {
            qa_catalog_archive archive = {0};
            char *error = NULL;
            if (inspect_cached(&cache, kpf_path, QA_ARCHIVE_FORMAT_KPF, &archive, &error)) {
              (void)archive_push(&root_archives, archive);
            } else {
              char *message = qa_catalog_format("%s: %s", kpf, error != NULL ? error : "");
              if (message != NULL) (void)qa_catalog_string_push(&diagnostics, message);
            }
            free(error);
          }
          free(kpf_path);
        }
      }
    }
    char *user_product_root = NULL;
    if (user_root != NULL && strcmp(user_root, corpus_root) != 0) {
      user_product_root = anchored_path(&user_roots, user_root, expectation->family,
                                        expectation->content_directory);
      if (user_product_root == NULL)
        user_product_root = user_product_directory(user_root,
                                                   expectation->content_directory);
    }
    archive_list user_archives = {0};
    qa_catalog_string_list user_diagnostics = {0};
    if (user_product_root != NULL && path_is_directory(user_product_root))
      collect_archives(user_product_root, &cache, &user_archives, &user_diagnostics);
    const qa_catalog_addon_directory *addon = NULL;
    if (loose_root == NULL)
      addon = qa_catalog_addon_directory_find(&addon_directories,
                                              expectation->content_directory);
    archive_list addon_archives = {0};
    qa_catalog_string_list addon_diagnostics = {0};
    if (addon != NULL)
      collect_archives(addon->physical, &cache, &addon_archives, &addon_diagnostics);
    map_list corpus_maps = {0}, addon_maps = {0}, all_maps = {0};
    (void)archive_maps(found.values, found.count, &corpus_maps);
    if (loose_root != NULL) (void)loose_maps(loose_root, &corpus_maps);
    (void)archive_maps(addon_archives.values, addon_archives.count, &addon_maps);
    if (addon != NULL) (void)loose_maps(addon->physical, &addon_maps);
    (void)archive_maps(user_archives.values, user_archives.count, &all_maps);
    if (user_product_root != NULL) (void)loose_maps(user_product_root, &all_maps);
    (void)map_list_extend(&all_maps, &corpus_maps);
    map_list corpus_maps_copy = {0};
    (void)archive_maps(found.values, found.count, &corpus_maps_copy);
    if (loose_root != NULL) (void)loose_maps(loose_root, &corpus_maps_copy);
    (void)map_list_extend(&all_maps, &addon_maps);
    bool is_user_mod = qa_catalog_string_contains(
        (const char *const *)user_mod_ids.values, user_mod_ids.count, expectation->id);
    bool is_addon_mod = qa_catalog_string_contains(
        (const char *const *)addon_mod_ids.values, addon_mod_ids.count, expectation->id);
    bool is_remote_overlay = qa_catalog_string_contains(
        (const char *const *)remote_overlays.values, remote_overlays.count,
        expectation->id);
    qa_catalog_string_list requirements = {0};
    for (size_t i = 0; i < diagnostics.count; i++)
      (void)qa_catalog_string_push(&requirements, qa_catalog_dup(diagnostics.values[i]));
    if (is_user_mod)
      for (size_t i = 0; i < user_diagnostics.count; i++)
        (void)qa_catalog_string_push(&requirements,
                                     qa_catalog_dup(user_diagnostics.values[i]));
    if (is_addon_mod)
      for (size_t i = 0; i < addon_diagnostics.count; i++)
        (void)qa_catalog_string_push(&requirements,
                                     qa_catalog_dup(addon_diagnostics.values[i]));
    if (!is_remote_overlay) {
      for (size_t i = 0; i < expectation->required_content_archive_count; i++) {
        const char *archive = expectation->required_content_archives[i];
        char *first = qa_catalog_dup(archive);
        char *slash = first != NULL ? strchr(first, '/') : NULL;
        if (slash != NULL) *slash = '\0';
        qa_game_family archive_family = expectation->family;
        (void)qa_game_family_for_directory(first != NULL ? first : "", &archive_family);
        char *path = anchored_path(&corpus_roots, corpus_root, archive_family, archive);
        if (path == NULL && is_user_mod && user_root != NULL)
          path = anchored_path(&user_roots, user_root, archive_family, archive);
        if (path == NULL && (addon != NULL || is_addon_mod)) {
          for (size_t a = 0; a < addon_directories.count && path == NULL; a++) {
            const char *key = addon_directories.values[a].content_directory;
            size_t key_length = strlen(key);
            char *lower_archive = qa_catalog_lower_dup(archive);
            char *lower_key = qa_catalog_lower_dup(key);
            bool under = lower_archive != NULL && lower_key != NULL &&
                         strncmp(lower_archive, lower_key, key_length) == 0 &&
                         (lower_archive[key_length] == '/' ||
                          lower_archive[key_length] == '\0');
            if (under) {
              const char *rest = archive + key_length;
              if (rest[0] == '/') rest++;
              path = rest[0] != '\0'
                         ? find_content_path(addon_directories.values[a].physical, rest)
                         : qa_catalog_dup(addon_directories.values[a].physical);
            }
            free(lower_archive); free(lower_key);
          }
        }
        if (path == NULL || !path_is_file(path))
          (void)qa_catalog_string_push(&requirements, qa_catalog_dup(archive));
        free(path);
        free(first);
      }
    }
    if (loose_root == NULL && !is_user_mod && addon == NULL)
      (void)qa_catalog_string_push(&requirements,
                                   qa_catalog_dup(expectation->content_directory));
    if (!is_remote_overlay && expectation->map_witness != NULL) {
      const map_list *witness = (is_user_mod || addon != NULL) ? &all_maps
                                                               : &corpus_maps_copy;
      bool seen = false;
      for (size_t i = 0; i < witness->count; i++)
        if (qa_catalog_ieq(witness->values[i].path, expectation->map_witness))
          seen = true;
      if (!seen)
        (void)qa_catalog_string_push(&requirements,
                                     qa_catalog_dup(expectation->map_witness));
    }
    map_list_free(&corpus_maps_copy);
    qa_catalog_discovered_product *product = &products[product_count++];
    product->id = content_id;
    (void)expectation_dup(expectation, &product->expectation);
    if (expectation->unresolved_reason != NULL) {
      product->availability.kind = QA_CATALOG_UNRESOLVED;
      product->availability.reason = qa_catalog_dup(expectation->unresolved_reason);
    } else if (requirements.count > 0) {
      product->availability.kind = QA_CATALOG_MISSING;
      product->availability.requirements = requirements.values;
      product->availability.requirement_count = requirements.count;
      requirements.values = NULL; requirements.count = 0;
    } else {
      product->availability.kind = QA_CATALOG_INSTALLED;
    }
    qa_catalog_string_list_free(&requirements);
    size_t total_archives = user_archives.count + found.count + addon_archives.count;
    if (total_archives > 0) {
      product->archives = (qa_catalog_archive *)calloc(total_archives,
                                                       sizeof *product->archives);
      size_t slot = 0;
      for (size_t i = 0; i < user_archives.count; i++)
        product->archives[slot++] = user_archives.values[i];
      for (size_t i = 0; i < found.count; i++)
        product->archives[slot++] = found.values[i];
      for (size_t i = 0; i < addon_archives.count; i++)
        product->archives[slot++] = addon_archives.values[i];
      product->archive_count = slot;
    }
    free(user_archives.values); free(found.values); free(addon_archives.values);
    product->loose_root = loose_root;
    if (user_product_root != NULL) {
      product->user_content =
          (qa_catalog_directory_content *)calloc(1, sizeof *product->user_content);
      if (product->user_content != NULL) {
        product->user_content->root = user_product_root;
        if (user_archives.count > 0) {
          product->user_content->archives = (qa_catalog_archive **)malloc(
              user_archives.count * sizeof *product->user_content->archives);
          for (size_t i = 0; i < user_archives.count; i++)
            product->user_content->archives[i] = &product->archives[i];
          product->user_content->archive_count = user_archives.count;
        }
      } else {
        free(user_product_root);
      }
    }
    if (addon != NULL) {
      product->addon_content =
          (qa_catalog_directory_content *)calloc(1, sizeof *product->addon_content);
      if (product->addon_content != NULL) {
        product->addon_content->root = qa_catalog_dup(addon->physical);
        if (addon_archives.count > 0) {
          size_t offset = user_archives.count + found.count;
          product->addon_content->archives = (qa_catalog_archive **)malloc(
              addon_archives.count * sizeof *product->addon_content->archives);
          for (size_t i = 0; i < addon_archives.count; i++)
            product->addon_content->archives[i] = &product->archives[offset + i];
          product->addon_content->archive_count = addon_archives.count;
        }
      }
    }
    product->maps = all_maps.values;
    product->map_count = all_maps.count;
    qa_catalog_string_list all_diagnostics = {0};
    for (size_t i = 0; i < diagnostics.count; i++)
      (void)qa_catalog_string_push(&all_diagnostics, qa_catalog_dup(diagnostics.values[i]));
    for (size_t i = 0; i < user_diagnostics.count; i++)
      (void)qa_catalog_string_push(&all_diagnostics, qa_catalog_dup(user_diagnostics.values[i]));
    for (size_t i = 0; i < addon_diagnostics.count; i++)
      (void)qa_catalog_string_push(&all_diagnostics, qa_catalog_dup(addon_diagnostics.values[i]));
    product->diagnostics = all_diagnostics.values;
    product->diagnostic_count = all_diagnostics.count;
    qa_catalog_string_list_free(&diagnostics);
    qa_catalog_string_list_free(&user_diagnostics);
    qa_catalog_string_list_free(&addon_diagnostics);
  }
  expectation_list_free(&expectations);
  /* Checked base products. */
  for (size_t i = 0; i < product_count; i++) {
    qa_catalog_discovered_product *product = &products[i];
    if (product->availability.kind != QA_CATALOG_INSTALLED ||
        product->expectation.base_product == NULL) continue;
    const qa_catalog_discovered_product *base = NULL;
    for (size_t j = 0; j < product_count; j++)
      if (strcmp(products[j].expectation.id, product->expectation.base_product) == 0)
        base = &products[j];
    if (base == NULL || base->availability.kind != QA_CATALOG_INSTALLED) {
      product->availability.kind = QA_CATALOG_MISSING;
      free(product->availability.reason);
      product->availability.reason = NULL;
      product->availability.requirements = (char **)malloc(sizeof(char *));
      if (product->availability.requirements != NULL) {
        product->availability.requirements[0] = qa_catalog_format(
            "base product %s", product->expectation.base_product);
        product->availability.requirement_count = 1;
      }
    }
  }
  qa_catalog_string_list_free(&remote_overlays);
  qa_catalog_string_list_free(&addon_mod_ids);
  qa_catalog_string_list_free(&user_mod_ids);
  qa_catalog_addon_directory_list_free(&addon_directories);
  inspect_cache_free(&cache);
  qa_content_catalog_init(out, corpus_root, products, product_count,
                          root_archives.values, root_archives.count, generation,
                          user_root, options->addon_content_roots,
                          options->addon_content_root_count);
  out->owned = true;
  out->corpus_root = corpus_root;
  out->user_content_root = user_root;
  if (options->addon_content_root_count > 0) {
    out->addon_content_root_strings =
        (char **)calloc(options->addon_content_root_count, sizeof(char *));
    if (out->addon_content_root_strings != NULL && out->addon_content_roots != NULL) {
      for (size_t i = 0; i < options->addon_content_root_count; i++) {
        out->addon_content_root_strings[i] =
            qa_catalog_dup(options->addon_content_roots[i].root);
        out->addon_content_roots[i].root = out->addon_content_root_strings[i];
      }
    }
  }
  qa_game_roots_free(&corpus_roots);
  qa_game_roots_free(&user_roots);
  return products != NULL || product_count == 0;
}





