/* Port of src/content/catalog/addons.ts. GPL-2.0-or-later. */
#include "qa/content/catalog/addons.h"

#include <errno.h>
#include <unistd.h>

#include "catalog_internal.h"

const char *const qa_quaddicted_catalog_url =
    "https://www.quaddicted.com/api/v1/?q=%2Btags%3A%22game%3Dquake%22%20"
    "%2Btags%3A%22game_mode%3Dsingleplayer%22&fl=sha256,tags,urls,bytes,install";

static bool is_record(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_OBJECT;
}
static bool is_array(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_ARRAY;
}

static void free_strings(char **values, size_t count) {
  for (size_t i = 0; i < count; i++) free(values[i]);
  free(values);
}

void qa_addon_package_free(qa_addon_package *package) {
  free(package->digest); free(package->sha256); free(package->title);
  free(package->filename); free(package->group); free(package->url);
  free_strings(package->tags, package->tag_count);
  free_strings(package->starts, package->start_count);
  free(package->game_directory);
  for (size_t i = 0; i < package->mapping_count; i++) {
    free(package->mappings[i].from); free(package->mappings[i].to);
  }
  free(package->mappings); free(package->unavailable);
  memset(package, 0, sizeof *package);
}

void qa_addon_package_list_free(qa_addon_package_list *list) {
  for (size_t i = 0; i < list->count; i++) qa_addon_package_free(&list->values[i]);
  free(list->values);
  list->values = NULL; list->count = 0;
}

void qa_addon_tags_free(char **values, size_t count) { free_strings(values, count); }

char **qa_addon_tags(const char *const *tags, size_t tag_count,
                     const char *name, size_t *out_count) {
  *out_count = 0;
  qa_catalog_string_list list = {0};
  size_t prefix = strlen(name);
  for (size_t i = 0; i < tag_count; i++) {
    const char *tag = tags[i];
    if (tag == NULL || strncmp(tag, name, prefix) != 0 || tag[prefix] != '=') continue;
    char *value = qa_catalog_dup(tag + prefix + 1);
    if (value == NULL || !qa_catalog_string_push(&list, value)) {
      free(value); qa_catalog_string_list_free(&list); return NULL;
    }
  }
  *out_count = list.count;
  return list.values;
}

static char **strings_value(const qa_save_value *value, size_t *out_count) {
  if (!is_array(value)) qa_catalog_error("Invalid Quaddicted string list");
  for (size_t i = 0; i < value->item_count; i++)
    if (value->items[i]->kind != QA_SAVE_STRING)
      qa_catalog_error("Invalid Quaddicted string list");
  qa_catalog_string_list list = {0};
  for (size_t i = 0; i < value->item_count; i++) {
    char *text = qa_catalog_dup(value->items[i]->text != NULL ? value->items[i]->text : "");
    if (text == NULL || !qa_catalog_string_push(&list, text)) {
      free(text); qa_catalog_string_list_free(&list); return NULL;
    }
  }
  *out_count = list.count;
  return list.values;
}

/* pathPrefix; *ok false means the TS containedFileParts throw was
 * caught by the caller (install try/catch). */
static char *path_prefix(const char *value, bool *ok) {
  *ok = true;
  const char *text = value;
  if (qa_catalog_starts_with(text, "{base}/")) text += 7;
  else if (qa_catalog_starts_with(text, "{base}")) text += 6;
  if (text[0] == '/') text++;
  char *path = qa_catalog_dup(text);
  if (path == NULL) { *ok = false; return NULL; }
  size_t length = strlen(path);
  if (length > 0 && path[length - 1] == '/') path[length - 1] = '\0';
  if (path[0] != '\0') {
    /* contained check inlined so the caller can catch it. */
    bool bad = strchr(path, '\\') != NULL || strchr(path, ':') != NULL;
    const char *part = path;
    while (!bad) {
      const char *slash = strchr(part, '/');
      size_t part_length = slash != NULL ? (size_t)(slash - part) : strlen(part);
      if (part_length == 0 || (part_length == 1 && part[0] == '.') ||
          (part_length == 2 && part[0] == '.' && part[1] == '.')) bad = true;
      if (slash == NULL) break;
      part = slash + 1;
    }
    if (bad) { free(path); *ok = false; return NULL; }
  }
  if (path[0] == '\0') return path;
  if (qa_catalog_ends_with(value, "/")) {
    char *joined = qa_catalog_format("%s/", path);
    free(path);
    if (joined == NULL) *ok = false;
    return joined;
  }
  return path;
}

static bool is_sha256(const char *text) {
  if (text == NULL || strlen(text) != 64) return false;
  for (size_t i = 0; i < 64; i++) {
    char c = text[i];
    if (!((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}

static bool valid_quaddicted_url(const char *url, const char *sha256) {
  char *prefix = qa_catalog_format(
      "https://www.quaddicted.com/files/by-sha256/%.2s/%s/", sha256, sha256);
  if (prefix == NULL) return false;
  bool ok = qa_catalog_starts_with(url, prefix);
  free(prefix);
  return ok;
}

static char *first_tag(char **values, size_t count) {
  return count > 0 ? values[0] : NULL;
}

bool qa_parse_addon_catalog(const qa_save_value *value, qa_addon_package_list *out) {
  memset(out, 0, sizeof *out);
  if (!is_array(value)) qa_catalog_error("Invalid Quaddicted catalog");
  size_t capacity = 0;
  for (size_t r = 0; r < value->item_count; r++) {
    const qa_save_value *row = value->items[r];
    if (!is_record(row)) qa_catalog_error("Invalid Quaddicted package");
    size_t tag_count = 0;
    char **tags = strings_value(qa_save_value_get(row, "tags"), &tag_count);
    if (tags == NULL && tag_count != 0) { qa_addon_package_list_free(out); return false; }
    size_t filename_count = 0;
    char **filenames = qa_addon_tags((const char *const *)tags, tag_count,
                                     "filename", &filename_count);
    const char *filename = first_tag(filenames, filename_count);
    bool wanted = qa_catalog_string_contains((const char *const *)tags, tag_count, "game=quake") &&
                  qa_catalog_string_contains((const char *const *)tags, tag_count, "game_mode=singleplayer") &&
                  filename != NULL && qa_catalog_ends_with(filename, ".zip");
    if (!wanted) {
      free_strings(tags, tag_count); free_strings(filenames, filename_count);
      continue;
    }
    const qa_save_value *sha_value = qa_save_value_get(row, "sha256");
    const qa_save_value *bytes_value = qa_save_value_get(row, "bytes");
    const char *sha256 = sha_value != NULL && sha_value->kind == QA_SAVE_STRING
                             ? sha_value->text : NULL;
    double bytes_number = bytes_value != NULL && bytes_value->kind == QA_SAVE_NUMBER
                              ? bytes_value->number : -1;
    bool safe = bytes_number >= 1 && bytes_number <= 9007199254740991.0 &&
                bytes_number == (double)(int64_t)bytes_number;
    if (!is_sha256(sha256) || !safe)
      qa_catalog_error("Invalid Quaddicted package identity: %s", filename);
    const qa_save_value *urls_value = qa_save_value_get(row, "urls");
    size_t url_count = 0;
    char **urls = NULL;
    if (urls_value == NULL || urls_value->kind == QA_SAVE_UNDEFINED ||
        urls_value->kind == QA_SAVE_NULL) {
      url_count = 0;
    } else {
      urls = strings_value(urls_value, &url_count);
    }
    char *url = NULL;
    for (size_t i = 0; i < url_count; i++) {
      if (strstr(urls[i], "://") == NULL)
        qa_catalog_error("Invalid URL: %s", urls[i]); /* TS new URL throws */
      if (valid_quaddicted_url(urls[i], sha256)) { url = qa_catalog_dup(urls[i]); break; }
    }
    free_strings(urls, url_count);
    if (url == NULL) {
      free_strings(tags, tag_count); free_strings(filenames, filename_count);
      continue;
    }
    qa_addon_package package = {0};
    size_t command_count = 0;
    char **commands = qa_addon_tags((const char *const *)tags, tag_count,
                                    "commandline", &command_count);
    char *commandline = qa_catalog_dup("");
    for (size_t i = 0; i < command_count; i++) {
      char *joined = i == 0 ? qa_catalog_dup(commands[i])
                            : qa_catalog_format("%s %s", commandline, commands[i]);
      free(commandline); commandline = joined;
    }
    free_strings(commands, command_count);
    package.game_directory = qa_catalog_dup("id1");
    if (commandline != NULL) {
      const char *cursor = commandline;
      while ((cursor = strstr(cursor, "-game")) != NULL) {
        bool boundary = cursor == commandline || isspace((unsigned char)cursor[-1]);
        const char *after = cursor + 5;
        if (boundary && isspace((unsigned char)*after)) {
          while (isspace((unsigned char)*after)) after++;
          const char *end = after;
          while (*end != '\0' && (isalnum((unsigned char)*end) || *end == '.' ||
                 *end == '_' || *end == '+' || *end == '-')) end++;
          if (end > after && (*end == '\0' || isspace((unsigned char)*end))) {
            free(package.game_directory);
            package.game_directory = qa_catalog_format("%.*s", (int)(end - after), after);
          }
          break;
        }
        cursor += 5;
      }
      const char *flags[] = {"-hipnotic", "-rogue", "-quoth"};
      for (size_t i = 0; i < 3; i++) {
        const char *hit = strstr(commandline, flags[i]);
        if (hit != NULL && (hit == commandline || isspace((unsigned char)hit[-1]))) {
          const char *end = hit + strlen(flags[i]);
          if (*end == '\0' || isspace((unsigned char)*end))
            package.unavailable = qa_catalog_dup(
                "Requires a source gameplay option not yet supported by this installer");
        }
      }
    }
    free(commandline);
    const qa_save_value *install = qa_save_value_get(row, "install");
    char *install_error = NULL;
    {
      qa_addon_mapping *mappings = NULL;
      size_t mapping_count = 0, mapping_capacity = 0;
      if (!is_record(install)) {
        install_error = qa_catalog_dup("No authored installation metadata");
      } else {
        const qa_save_value *mapping = qa_save_value_get(install, "extractmapping");
        if (is_record(mapping)) {
          for (size_t i = 0; i < mapping->entry_count && install_error == NULL; i++) {
            const qa_save_value *to = mapping->entries[i].value;
            if (to->kind != QA_SAVE_NULL && to->kind != QA_SAVE_STRING) {
              install_error = qa_catalog_dup("Unsupported extraction mapping");
              break;
            }
            bool ok = true;
            char *from = path_prefix(mapping->entries[i].key, &ok);
            char *target = NULL;
            if (ok && to->kind == QA_SAVE_STRING)
              target = path_prefix(to->text != NULL ? to->text : "", &ok);
            if (!ok) {
              free(from); free(target);
              install_error = qa_catalog_dup(
                  "File path must be relative and stay inside its storage directory");
              break;
            }
            if (mapping_count == mapping_capacity) {
              size_t next = mapping_capacity != 0 ? mapping_capacity * 2 : 4;
              qa_addon_mapping *grown =
                  (qa_addon_mapping *)realloc(mappings, next * sizeof *grown);
              if (grown == NULL) { free(from); free(target); install_error = qa_catalog_dup("out of memory"); break; }
              mappings = grown; mapping_capacity = next;
            }
            mappings[mapping_count].from = from;
            mappings[mapping_count].to = target;
            mapping_count++;
          }
        } else {
          const qa_save_value *extract = qa_save_value_get(install, "extract");
          if (extract != NULL && extract->kind == QA_SAVE_STRING) {
            bool ok = true;
            char *target = path_prefix(extract->text != NULL ? extract->text : "", &ok);
            if (!ok) {
              install_error = qa_catalog_dup(
                  "File path must be relative and stay inside its storage directory");
            } else {
              mappings = (qa_addon_mapping *)malloc(sizeof *mappings);
              if (mappings != NULL) {
                mappings[0].from = qa_catalog_dup("");
                mappings[0].to = target;
                mapping_count = 1;
              } else { free(target); install_error = qa_catalog_dup("out of memory"); }
            }
          } else {
            install_error = qa_catalog_dup("No authored extraction mapping");
          }
        }
      }
      package.mappings = mappings;
      package.mapping_count = mapping_count;
    }
    if (install_error != NULL) {
      /* TS catch overwrites unavailable (even a commandline one). */
      free(package.unavailable);
      package.unavailable = install_error;
    }
    package.digest = qa_create_content_digest(sha256);
    package.sha256 = qa_catalog_dup(sha256);
    size_t title_count = 0;
    char **titles = qa_addon_tags((const char *const *)tags, tag_count, "title", &title_count);
    package.title = qa_catalog_dup(first_tag(titles, title_count) != NULL
                                       ? first_tag(titles, title_count) : filename);
    free_strings(titles, title_count);
    package.filename = qa_catalog_dup(filename);
    size_t group_count = 0;
    char **groups = qa_addon_tags((const char *const *)tags, tag_count,
                                  "release_group", &group_count);
    if (first_tag(groups, group_count) != NULL) {
      package.group = qa_catalog_dup(first_tag(groups, group_count));
    } else {
      package.group = qa_catalog_dup(filename);
      size_t length = strlen(package.group);
      if (length >= 4) package.group[length - 4] = '\0';
    }
    free_strings(groups, group_count);
    package.bytes = (int64_t)bytes_number;
    package.url = url;
    package.tags = tags;
    package.tag_count = tag_count;
    size_t start_count = 0;
    char **starts = qa_addon_tags((const char *const *)tags, tag_count,
                                  "startmap", &start_count);
    qa_catalog_string_list kept = {0};
    for (size_t i = 0; i < start_count; i++) {
      const char *path = starts[i];
      bool ok = path[0] != '\0' && strstr(path, "..") == NULL;
      for (size_t j = 0; ok && path[j] != '\0'; j++) {
        char c = path[j];
        if (!(isalnum((unsigned char)c) || c == '_' || c == '+' || c == '.' ||
              c == '/' || c == '-')) ok = false;
      }
      if (ok) {
        char *dup = qa_catalog_dup(path);
        if (dup == NULL || !qa_catalog_string_push(&kept, dup)) { free(dup); }
      }
    }
    free_strings(starts, start_count);
    package.starts = kept.values;
    package.start_count = kept.count;
    free_strings(filenames, filename_count);
    if (out->count == capacity) {
      size_t next = capacity != 0 ? capacity * 2 : 8;
      qa_addon_package *grown =
          (qa_addon_package *)realloc(out->values, next * sizeof *grown);
      if (grown == NULL) { qa_addon_package_free(&package); qa_addon_package_list_free(out); return false; }
      out->values = grown; capacity = next;
    }
    out->values[out->count++] = package;
  }
  return true;
}

char *qa_addon_install_path(const qa_addon_package *package,
                            const char *member, bool *out_found) {
  if (out_found != NULL) *out_found = false;
  qa_catalog_contained_check(member);
  const qa_addon_mapping *best = NULL;
  size_t best_length = 0;
  bool have = false;
  for (size_t i = 0; i < package->mapping_count; i++) {
    const qa_addon_mapping *mapping = &package->mappings[i];
    bool match = mapping->from[0] == '\0' || qa_catalog_ends_with(mapping->from, "/")
                     ? qa_catalog_starts_with(member, mapping->from)
                     : strcmp(member, mapping->from) == 0;
    if (match && (!have || strlen(mapping->from) > best_length)) {
      best = mapping; best_length = strlen(mapping->from); have = true;
    }
  }
  if (!have || best->to == NULL) return NULL;
  char *path = qa_catalog_format("%s%s", best->to, member + best_length);
  if (path == NULL) return NULL;
  qa_catalog_contained_check(path);
  const char *slash = strchr(path, '/');
  char *first = slash != NULL ? qa_catalog_format("%.*s", (int)(slash - path), path)
                              : qa_catalog_dup(path);
  static const char *const roots[] = {"maps", "progs", "gfx", "sound",
                                      "music", "env", "textures"};
  bool prefixed = slash == NULL;
  for (size_t i = 0; i < 7 && !prefixed; i++)
    if (first != NULL && strcmp(first, roots[i]) == 0) prefixed = true;
  free(first);
  if (out_found != NULL) *out_found = true;
  if (!prefixed) return path;
  char *joined = qa_catalog_format("id1/%s", path);
  free(path);
  if (joined == NULL && out_found != NULL) *out_found = false;
  return joined;
}

/* localeCompare(..., numeric:true) approximation: digit runs
 * compare numerically (by value, then fewer leading zeros), other
 * bytes compare as unsigned bytes. */
static int numeric_compare(const char *left, const char *right) {
  size_t i = 0, j = 0;
  while (left[i] != '\0' && right[j] != '\0') {
    if (isdigit((unsigned char)left[i]) && isdigit((unsigned char)right[j])) {
      size_t i_end = i, j_end = j;
      while (isdigit((unsigned char)left[i_end])) i_end++;
      while (isdigit((unsigned char)right[j_end])) j_end++;
      size_t i_start = i, j_start = j;
      while (i_start + 1 < i_end && left[i_start] == '0') i_start++;
      while (j_start + 1 < j_end && right[j_start] == '0') j_start++;
      size_t i_length = i_end - i_start, j_length = j_end - j_start;
      if (i_length != j_length) return i_length < j_length ? -1 : 1;
      int compared = memcmp(left + i_start, right + j_start, i_length);
      if (compared != 0) return compared < 0 ? -1 : 1;
      i = i_end; j = j_end;
    } else {
      if (left[i] != right[j])
        return (unsigned char)left[i] < (unsigned char)right[j] ? -1 : 1;
      i++; j++;
    }
  }
  if (left[i] != '\0') return 1;
  if (right[j] != '\0') return -1;
  return 0;
}

typedef struct resolve_state {
  const qa_addon_package *catalog;
  size_t catalog_count;
  const qa_addon_package **result;
  size_t count;
  size_t capacity;
  qa_catalog_string_list visiting;
} resolve_state;

static bool list_contains_string(const qa_catalog_string_list *list, const char *text) {
  return qa_catalog_string_contains((const char *const *)list->values, list->count, text);
}

static void visit_package(resolve_state *state, const qa_addon_package *item) {
  for (size_t i = 0; i < state->count; i++)
    if (strcmp(state->result[i]->sha256, item->sha256) == 0) return;
  if (list_contains_string(&state->visiting, item->sha256))
    qa_catalog_error("Cyclic add-on dependency: %s", item->title);
  if (item->unavailable != NULL)
    qa_catalog_error("%s: %s", item->title, item->unavailable);
  (void)qa_catalog_string_push(&state->visiting, qa_catalog_dup(item->sha256));
  size_t dependency_count = 0;
  char **dependencies = qa_addon_tags((const char *const *)item->tags,
                                      item->tag_count, "dependency", &dependency_count);
  size_t expression_count = 0;
  char **expressions = qa_addon_tags((const char *const *)item->tags,
                                     item->tag_count, "depends", &expression_count);
  if (expression_count > 0 && dependency_count == 0) {
    for (size_t e = 0; e < expression_count; e++) {
      const char *expression = expressions[e];
      const char *cursor = expression;
      bool matched = false;
      while (*cursor != '\0') {
        if (*cursor != '\'') { cursor++; continue; }
        const char *name = cursor + 1, *scan = name;
        while (*scan != '\0' && (isalnum((unsigned char)*scan) || *scan == '_' ||
               *scan == '.' || *scan == '+' || *scan == '-')) scan++;
        if (scan == name) { cursor++; continue; }
        const char *operator = scan;
        size_t operator_length = 0;
        if (strncmp(operator, ">=", 2) == 0 || strncmp(operator, "<=", 2) == 0) operator_length = 2;
        else if (*operator == '=' || *operator == '>' || *operator == '<') operator_length = 1;
        else { cursor = scan; continue; }
        const char *version = operator + operator_length, *vend = version;
        while (*vend != '\0' && (isalnum((unsigned char)*vend) || *vend == '_' ||
               *vend == '.' || *vend == '+' || *vend == '-')) vend++;
        if (vend == version || *vend != '\'') { cursor = vend; continue; }
        matched = true;
        char *name_text = qa_catalog_format("%.*s", (int)(operator - name), name);
        char *version_text = qa_catalog_format("%.*s", (int)(vend - version), version);
        char op[3] = {operator[0], operator_length > 1 ? operator[1] : '\0', '\0'};
        const qa_addon_package *dependency = NULL;
        for (size_t c = 0; c < state->catalog_count && dependency == NULL; c++) {
          size_t provides_count = 0;
          char **provides = qa_addon_tags(
              (const char *const *)state->catalog[c].tags,
              state->catalog[c].tag_count, "provides", &provides_count);
          for (size_t p = 0; p < provides_count; p++) {
            const char *provided = provides[p];
            if (provided[0] == '\'') provided++;
            const char *equals = strchr(provided, '=');
            if (equals == NULL) continue;
            if ((size_t)(equals - provided) != strlen(name_text) ||
                strncmp(provided, name_text, strlen(name_text)) != 0) continue;
            char *provided_version = qa_catalog_dup(equals + 1);
            size_t vlength = strlen(provided_version);
            if (vlength > 0 && provided_version[vlength - 1] == '\'')
              provided_version[vlength - 1] = '\0';
            if (strchr(provided_version, '\'') != NULL ||
                strchr(provided_version, '=') != NULL) { free(provided_version); continue; }
            int compared = numeric_compare(provided_version, version_text);
            free(provided_version);
            bool ok = strcmp(op, "=") == 0 ? compared == 0
                      : strcmp(op, ">=") == 0 ? compared >= 0
                      : strcmp(op, "<=") == 0 ? compared <= 0
                      : strcmp(op, ">") == 0 ? compared > 0 : compared < 0;
            if (ok) { dependency = &state->catalog[c]; break; }
          }
          free_strings(provides, provides_count);
        }
        free(name_text); free(version_text);
        if (dependency == NULL)
          qa_catalog_error("Missing dependency: %s", expression);
        visit_package(state, dependency);
        cursor = vend + 1;
      }
      if (!matched)
        qa_catalog_error("Unsupported dependency requirement: %s", expression);
    }
  }
  for (size_t d = 0; d < dependency_count; d++) {
    const char *name = dependencies[d];
    const qa_addon_package *dependency = NULL;
    for (size_t c = 0; c < state->catalog_count; c++) {
      const qa_addon_package *candidate = &state->catalog[c];
      char *zip_name = qa_catalog_format("%s.zip", name);
      bool match = zip_name != NULL && strcmp(candidate->filename, zip_name) == 0;
      free(zip_name);
      if (match || strcmp(candidate->filename, name) == 0 ||
          strcmp(candidate->sha256, name) == 0) { dependency = candidate; break; }
    }
    if (dependency == NULL) qa_catalog_error("Missing dependency: %s", name);
    visit_package(state, dependency);
  }
  free_strings(dependencies, dependency_count);
  free_strings(expressions, expression_count);
  for (size_t i = 0; i < state->visiting.count; i++) {
    if (strcmp(state->visiting.values[i], item->sha256) == 0) {
      free(state->visiting.values[i]);
      memmove(&state->visiting.values[i], &state->visiting.values[i + 1],
              (state->visiting.count - i - 1) * sizeof(char *));
      state->visiting.count--;
      break;
    }
  }
  if (state->count == state->capacity) {
    size_t next = state->capacity != 0 ? state->capacity * 2 : 8;
    const qa_addon_package **grown = (const qa_addon_package **)realloc(
        state->result, next * sizeof *grown);
    if (grown == NULL) return;
    state->result = grown; state->capacity = next;
  }
  state->result[state->count++] = item;
}

const qa_addon_package **qa_resolve_addon_packages(
    const qa_addon_package *catalog, size_t catalog_count,
    const qa_addon_package *selected, size_t *out_count) {
  resolve_state state = {0};
  state.catalog = catalog;
  state.catalog_count = catalog_count;
  visit_package(&state, selected);
  qa_catalog_string_list_free(&state.visiting);
  *out_count = state.count;
  return state.result;
}

static bool managed_name(const char *path) {
  const char *leaf = qa_catalog_basename(path);
  if (strlen(leaf) != 44 || strncmp(leaf, "qd_", 3) != 0 || leaf[23] != '_')
    return false;
  for (size_t i = 3; i < 44; i++) {
    if (i == 23) continue;
    char c = leaf[i];
    if (!((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}

static char *read_file_bytes(const char *path, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  size_t capacity = 4096, length = 0;
  char *bytes = (char *)malloc(capacity);
  if (bytes == NULL) { (void)fclose(file); return NULL; }
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      char *grown = (char *)realloc(bytes, capacity);
      if (grown == NULL) { free(bytes); (void)fclose(file); return NULL; }
      bytes = grown;
    }
    size_t read = fread(bytes + length, 1, capacity - length, file);
    length += read;
    if (read == 0) break;
  }
  (void)fclose(file);
  *out_length = length;
  return bytes;
}

bool qa_managed_addon_hidden(const char *root, const char *directory) {
  if (!managed_name(directory)) return false;
  qa_catalog_contained_check(directory);
  char *path = qa_catalog_format("%s/.addons/removed/%s", root, directory);
  if (path == NULL) return false;
  int result = access(path, F_OK);
  int error = errno;
  free(path);
  if (result == 0) return true;
  if (error == ENOENT) return false;
  qa_catalog_error("%s", strerror(error));
}

void qa_nightdive_addon_list_free(qa_nightdive_addon_list *list) {
  for (size_t i = 0; i < list->count; i++) {
    free(list->values[i].id); free(list->values[i].game_directory);
    free(list->values[i].title);
  }
  free(list->values);
  list->values = NULL; list->count = 0;
}

static bool valid_game_directory(const char *text) {
  if (text == NULL || !isalnum((unsigned char)text[0])) return false;
  for (size_t i = 1; text[i] != '\0'; i++) {
    char c = text[i];
    if (!isalnum((unsigned char)c) && c != '.' && c != '_' && c != '+' && c != '-')
      return false;
  }
  return true;
}

static bool blank(const char *text) {
  for (size_t i = 0; text[i] != '\0'; i++)
    if (!isspace((unsigned char)text[i])) return false;
  return true;
}

bool qa_parse_nightdive_addons(const qa_save_value *value,
                               qa_nightdive_addon_list *out) {
  memset(out, 0, sizeof *out);
  if (!is_record(value)) return true;
  const qa_save_value *addons = qa_save_value_get(value, "addons");
  if (!is_array(addons)) return true;
  size_t capacity = 0;
  for (size_t i = 0; i < addons->item_count; i++) {
    const qa_save_value *row = addons->items[i];
    if (!is_record(row)) continue;
    const qa_save_value *directory = qa_save_value_get(row, "gamedir");
    const qa_save_value *id = qa_save_value_get(row, "id");
    const qa_save_value *name = qa_save_value_get(row, "name");
    if (directory == NULL || directory->kind != QA_SAVE_STRING ||
        !valid_game_directory(directory->text)) continue;
    if (out->count == capacity) {
      size_t next = capacity != 0 ? capacity * 2 : 8;
      qa_nightdive_addon *grown =
          (qa_nightdive_addon *)realloc(out->values, next * sizeof *grown);
      if (grown == NULL) { qa_nightdive_addon_list_free(out); return false; }
      out->values = grown; capacity = next;
    }
    qa_nightdive_addon *addon = &out->values[out->count++];
    memset(addon, 0, sizeof *addon);
    addon->id = qa_catalog_dup(id != NULL && id->kind == QA_SAVE_STRING &&
                                       id->text != NULL && id->text[0] != '\0'
                                   ? id->text : directory->text);
    addon->game_directory = qa_catalog_dup(directory->text);
    addon->title = qa_catalog_dup(name != NULL && name->kind == QA_SAVE_STRING &&
                                          name->text != NULL && !blank(name->text)
                                      ? name->text : directory->text);
  }
  return true;
}

bool qa_read_nightdive_addons(const char *root, qa_nightdive_addon_list *out) {
  memset(out, 0, sizeof *out);
  char *path = qa_catalog_join(root, "addons.json");
  if (path == NULL) return false;
  size_t length = 0;
  char *bytes = read_file_bytes(path, &length);
  free(path);
  if (bytes == NULL) return true; /* TS catch returns [] */
  qa_save_value *parsed = qa_parse_json_value((const uint8_t *)bytes, length, true);
  free(bytes);
  if (parsed == NULL) return true;
  bool ok = qa_parse_nightdive_addons(parsed, out);
  qa_save_value_free(parsed);
  return ok;
}

char *qa_managed_addon_title(const char *root, const char *content_directory) {
  if (!managed_name(content_directory)) return NULL;
  char *path = qa_catalog_format("%s/%s/.quaddicted.json", root, content_directory);
  if (path == NULL) return NULL;
  size_t length = 0;
  char *bytes = read_file_bytes(path, &length);
  free(path);
  if (bytes == NULL) qa_catalog_error("%s", strerror(errno));
  qa_save_value *value = qa_parse_json_value((const uint8_t *)bytes, length, false);
  free(bytes);
  if (value == NULL) return NULL;
  char *title = NULL;
  const qa_save_value *field =
      is_record(value) ? qa_save_value_get(value, "title") : NULL;
  if (field != NULL && field->kind == QA_SAVE_STRING)
    title = qa_catalog_dup(field->text != NULL ? field->text : "");
  qa_save_value_free(value);
  return title;
}
