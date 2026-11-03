/* Port of src/console/discovery.ts. GPL-2.0-or-later. */
#include "qa/console/discovery.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"

static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    memcpy(out, text, length);
    out[length] = '\0';
  }
  return out;
}

static char *format_text(const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  char *out = NULL;
  if (needed >= 0) {
    out = (char *)malloc((size_t)needed + 1);
    if (out != NULL) vsnprintf(out, (size_t)needed + 1, format, args);
  }
  va_end(args);
  return out;
}

static char **dup_strings(const char *const *values, size_t count) {
  if (count == 0) return NULL;
  char **out = (char **)calloc(count, sizeof *out);
  if (out == NULL) return NULL;
  for (size_t i = 0; i < count; i++) out[i] = dup_text(values[i]);
  return out;
}

static void free_strings(char **values, size_t count) {
  if (values == NULL) return;
  for (size_t i = 0; i < count; i++) free(values[i]);
  free(values);
}

void qa_console_discovery_lines_free(char **lines, size_t count) {
  free_strings(lines, count);
}

/* JSON.stringify of one string: quotes and control escapes. Bytes
 * >= 0x7f pass through (TS works on UTF-16 code units; C input is
 * bytes, normally UTF-8, which JSON.stringify would emit as the
 * decoded characters). */
static char *json_stringify(const char *text) {
  size_t length = strlen(text);
  size_t capacity = length * 6 + 3;
  char *out = (char *)malloc(capacity);
  if (out == NULL) return NULL;
  size_t at = 0;
  out[at++] = '"';
  for (size_t i = 0; i < length; i++) {
    unsigned char ch = (unsigned char)text[i];
    switch (ch) {
      case '"': out[at++] = '\\'; out[at++] = '"'; break;
      case '\\': out[at++] = '\\'; out[at++] = '\\'; break;
      case '\b': out[at++] = '\\'; out[at++] = 'b'; break;
      case '\f': out[at++] = '\\'; out[at++] = 'f'; break;
      case '\n': out[at++] = '\\'; out[at++] = 'n'; break;
      case '\r': out[at++] = '\\'; out[at++] = 'r'; break;
      case '\t': out[at++] = '\\'; out[at++] = 't'; break;
      default:
        if (ch < 0x20) {
          at += (size_t)snprintf(out + at, capacity - at, "\\u%04x", ch);
        } else {
          out[at++] = (char)ch;
        }
        break;
    }
  }
  out[at++] = '"';
  out[at] = '\0';
  return out;
}

const char *qa_console_discovery_kind_name(qa_console_discovery_kind kind) {
  switch (kind) {
    case QA_CONSOLE_ENTRY_COMMAND: return "command";
    case QA_CONSOLE_ENTRY_ALIAS: return "alias";
    case QA_CONSOLE_ENTRY_CVAR: return "cvar";
  }
  return "command";
}

static void entry_free_contents(qa_console_discovery_entry *entry) {
  free(entry->name);
  free(entry->summary);
  free(entry->usage);
  free_strings(entry->examples, entry->example_count);
  free_strings(entry->allowed_values, entry->allowed_value_count);
  free(entry->value);
  free(entry->reset_value);
  free(entry->latched_value);
  memset(entry, 0, sizeof *entry);
}

void qa_console_discovery_entries_free(qa_console_discovery_entry *entries,
                                       size_t count) {
  if (entries == NULL) return;
  for (size_t i = 0; i < count; i++) entry_free_contents(&entries[i]);
  free(entries);
}

static void fill_help(qa_console_discovery_entry *entry, const char *name,
                      const qa_command_documentation *documentation) {
  entry->name = dup_text(name);
  entry->summary =
      documentation != NULL ? dup_text(documentation->summary) : NULL;
  entry->usage = documentation != NULL ? dup_text(documentation->usage) : NULL;
  entry->examples = documentation != NULL
      ? dup_strings(documentation->examples, documentation->example_count)
      : NULL;
  entry->example_count =
      documentation != NULL ? documentation->example_count : 0;
  entry->has_allowed_values =
      documentation != NULL && documentation->allowed_values != NULL;
  entry->allowed_values = entry->has_allowed_values
      ? dup_strings(documentation->allowed_values,
                    documentation->allowed_value_count)
      : NULL;
  entry->allowed_value_count =
      entry->has_allowed_values ? documentation->allowed_value_count : 0;
}

typedef struct entry_map {
  char **keys;
  qa_console_discovery_entry *entries;
  size_t count;
  size_t capacity;
} entry_map;

static bool map_has(const entry_map *map, const char *key) {
  if (key == NULL) return false;
  for (size_t i = 0; i < map->count; i++) {
    if (strcmp(map->keys[i], key) == 0) return true;
  }
  return false;
}

/* Map.set semantics: an existing key keeps its position, value replaced. */
static qa_console_discovery_entry *map_push(entry_map *map, char *key) {
  if (key != NULL) {
    for (size_t i = 0; i < map->count; i++) {
      if (strcmp(map->keys[i], key) == 0) {
        free(key);
        entry_free_contents(&map->entries[i]);
        return &map->entries[i];
      }
    }
  }
  if (map->count == map->capacity) {
    size_t cap = map->capacity == 0 ? 16 : map->capacity * 2;
    char **keys = (char **)realloc(map->keys, cap * sizeof *keys);
    if (keys == NULL) return NULL;
    map->keys = keys;
    qa_console_discovery_entry *entries = (qa_console_discovery_entry *)realloc(
        map->entries, cap * sizeof *entries);
    if (entries == NULL) return NULL;
    map->entries = entries;
    map->capacity = cap;
  }
  map->keys[map->count] = key;
  qa_console_discovery_entry *entry = &map->entries[map->count++];
  memset(entry, 0, sizeof *entry);
  return entry;
}

static int compare_entries(const void *left, const void *right) {
  const qa_console_discovery_entry *a =
      (const qa_console_discovery_entry *)left;
  const qa_console_discovery_entry *b =
      (const qa_console_discovery_entry *)right;
  return qa_js_locale_compare(a->name != NULL ? a->name : "",
                              b->name != NULL ? b->name : "");
}

qa_console_discovery_entry *qa_query_console_entries(
    qa_command_buffer *commands, const qa_command_context *source,
    size_t *out_count) {
  entry_map map = {NULL, NULL, 0, 0};
  size_t count = 0;
  char **names = qa_command_buffer_registered_names(commands, &count);
  for (size_t i = 0; i < count; i++) {
    char *key = qa_ascii_fold(names[i]);
    qa_console_discovery_entry *entry = map_push(&map, key);
    if (entry != NULL) {
      entry->kind = QA_CONSOLE_ENTRY_COMMAND;
      fill_help(entry, names[i],
                qa_command_buffer_command_documentation(commands, names[i]));
    } else {
      free(key);
    }
    free(names[i]);
  }
  free(names);
  names = qa_command_buffer_alias_names(commands, &count);
  for (size_t i = 0; i < count; i++) {
    char *key = qa_ascii_fold(names[i]);
    if (key != NULL && !map_has(&map, key)) {
      qa_console_discovery_entry *entry = map_push(&map, key);
      if (entry != NULL) {
        entry->kind = QA_CONSOLE_ENTRY_ALIAS;
        fill_help(entry, names[i], NULL);
        free(entry->usage);
        entry->usage = dup_text(names[i]);
        char *value = qa_command_buffer_alias_value(commands, names[i]);
        entry->value = value != NULL ? value : dup_text("");
      } else {
        free(key);
      }
    } else {
      free(key);
    }
    free(names[i]);
  }
  free(names);
  qa_cvar_snapshot *snapshots =
      qa_command_buffer_cvar_snapshots(commands, source, &count);
  for (size_t i = 0; i < count; i++) {
    char *folded = qa_ascii_fold(snapshots[i].name);
    if (folded != NULL && !map_has(&map, folded)) {
      char *key = format_text("cvar:%s", snapshots[i].name);
      qa_console_discovery_entry *entry = map_push(&map, key);
      if (entry != NULL) {
        const qa_command_documentation *documentation =
            qa_command_buffer_cvar_documentation(commands, snapshots[i].name,
                                                 source);
        entry->kind = QA_CONSOLE_ENTRY_CVAR;
        fill_help(entry, snapshots[i].name, documentation);
        free(entry->usage);
        entry->usage = documentation != NULL && documentation->usage != NULL
            ? dup_text(documentation->usage)
            : format_text("%s [value]", snapshots[i].name);
        entry->value = dup_text(snapshots[i].value);
        entry->reset_value = dup_text(snapshots[i].reset_value);
        entry->latched_value = dup_text(snapshots[i].latched_value);
      } else {
        free(key);
      }
    }
    free(folded);
    qa_cvar_read_free(&snapshots[i]);
  }
  free(snapshots);
  free_strings(map.keys, map.count);
  if (map.count > 1) {
    qa_js_sort(map.entries, map.count, sizeof *map.entries, compare_entries);
  }
  *out_count = map.count;
  return map.entries;
}

qa_console_discovery_entry *qa_find_console_entries(
    qa_command_buffer *commands, const char *text,
    const qa_command_context *source, size_t *out_count) {
  char *query = qa_ascii_fold(text);
  size_t count = 0;
  qa_console_discovery_entry *all =
      qa_query_console_entries(commands, source, &count);
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    qa_console_discovery_entry *entry = &all[i];
    char *joined = format_text("%s %s %s",
                               entry->name != NULL ? entry->name : "",
                               entry->summary != NULL ? entry->summary : "",
                               entry->usage != NULL ? entry->usage : "");
    char *folded = joined != NULL ? qa_ascii_fold(joined) : NULL;
    bool match = query != NULL && folded != NULL &&
                 strstr(folded, query) != NULL;
    free(joined);
    free(folded);
    if (match) {
      if (kept != i) all[kept] = *entry;
      kept++;
    } else {
      entry_free_contents(entry);
    }
  }
  free(query);
  *out_count = kept;
  if (kept == 0) {
    free(all);
    return NULL;
  }
  return all;
}

static bool lines_push(char ***lines, size_t *count, size_t *capacity,
                       char *line) {
  if (*count == *capacity) {
    size_t cap = *capacity == 0 ? 8 : *capacity * 2;
    char **grown = (char **)realloc(*lines, cap * sizeof *grown);
    if (grown == NULL) {
      free(line);
      return false;
    }
    *lines = grown;
    *capacity = cap;
  }
  (*lines)[(*count)++] = line;
  return true;
}

static char *trim_end_dup(const char *text) {
  size_t length = strlen(text);
  while (length > 0) {
    char ch = text[length - 1];
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\v' &&
        ch != '\f') {
      break;
    }
    length--;
  }
  return dup_range(text, length);
}

char **qa_console_entry_help(const qa_console_discovery_entry *entry,
                             size_t *out_count) {
  char **lines = NULL;
  size_t count = 0;
  size_t capacity = 0;
  (void)lines_push(&lines, &count, &capacity,
                   format_text("%s (%s)",
                               entry->name != NULL ? entry->name : "",
                               qa_console_discovery_kind_name(entry->kind)));
  (void)lines_push(&lines, &count, &capacity,
                   dup_text(entry->summary != NULL
                                ? entry->summary
                                : "No description registered."));
  (void)lines_push(&lines, &count, &capacity,
                   format_text("Usage: %s",
                               entry->usage != NULL ? entry->usage
                                                     : "not documented"));
  if (entry->kind == QA_CONSOLE_ENTRY_CVAR) {
    char *value = json_stringify(entry->value != NULL ? entry->value : "");
    char *reset =
        json_stringify(entry->reset_value != NULL ? entry->reset_value : "");
    (void)lines_push(&lines, &count, &capacity,
                     format_text("Current: %s", value != NULL ? value : "\"\""));
    (void)lines_push(&lines, &count, &capacity,
                     format_text("Default: %s", reset != NULL ? reset : "\"\""));
    free(value);
    free(reset);
    if (entry->latched_value != NULL) {
      char *latched = json_stringify(entry->latched_value);
      (void)lines_push(
          &lines, &count, &capacity,
          format_text("Pending: %s", latched != NULL ? latched : "\"\""));
      free(latched);
    }
  } else if (entry->kind == QA_CONSOLE_ENTRY_ALIAS) {
    char *trimmed = trim_end_dup(entry->value != NULL ? entry->value : "");
    (void)lines_push(&lines, &count, &capacity,
                     format_text("Expands to: %s",
                                 trimmed != NULL ? trimmed : ""));
    free(trimmed);
  }
  if (entry->has_allowed_values) {
    size_t length = 0;
    for (size_t i = 0; i < entry->allowed_value_count; i++) {
      length += strlen(entry->allowed_values[i]) + 2;
    }
    char *joined = (char *)malloc(length + 1);
    if (joined != NULL) {
      size_t at = 0;
      for (size_t i = 0; i < entry->allowed_value_count; i++) {
        if (i > 0) {
          joined[at++] = ',';
          joined[at++] = ' ';
        }
        size_t item = strlen(entry->allowed_values[i]);
        memcpy(joined + at, entry->allowed_values[i], item);
        at += item;
      }
      joined[at] = '\0';
      (void)lines_push(&lines, &count, &capacity,
                       format_text("Allowed values: %s", joined));
      free(joined);
    }
  }
  for (size_t i = 0; i < entry->example_count; i++) {
    (void)lines_push(&lines, &count, &capacity,
                     format_text("Example: %s", entry->examples[i]));
  }
  *out_count = count;
  return lines;
}

static void print_format(qa_console_discovery_registration *registration,
                         const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (needed >= 0) {
    char *text = (char *)malloc((size_t)needed + 1);
    if (text != NULL) {
      vsnprintf(text, (size_t)needed + 1, format, args);
      registration->print(registration->print_ctx, text);
      free(text);
    }
  }
  va_end(args);
}

static bool valid_page_text(const char *text, int64_t *out_page) {
  if (text[0] < '1' || text[0] > '9') return false;
  int64_t value = 0;
  for (size_t i = 0; text[i] != '\0'; i++) {
    if (text[i] < '0' || text[i] > '9') return false;
    int digit = text[i] - '0';
    if (value > (QA_MAX_SAFE_INTEGER - digit) / 10) return false;
    value = value * 10 + digit;
  }
  *out_page = value;
  return true;
}

static void find_handler(void *ctx, qa_command_invocation *command) {
  qa_console_discovery_registration *registration =
      (qa_console_discovery_registration *)ctx;
  const char *query = command->argc > 1 ? command->argv[1] : NULL;
  const char *page_text = command->argc > 2 ? command->argv[2] : "1";
  int64_t page = 0;
  if (query == NULL || command->argc > 3 ||
      !valid_page_text(page_text, &page)) {
    registration->print(registration->print_ctx,
                        "Usage: find <text> [page]\nExample: find mouse\n");
    return;
  }
  size_t count = 0;
  qa_console_discovery_entry *entries = qa_find_console_entries(
      registration->commands, query, &command->source, &count);
  int64_t pages = count == 0 ? 1 : (int64_t)((count + 29) / 30);
  if (page > pages) {
    print_format(registration, "Page %lld is out of range; %lld page(s).\n",
                 (long long)page, (long long)pages);
    qa_console_discovery_entries_free(entries, count);
    return;
  }
  size_t begin = (size_t)(page - 1) * 30;
  size_t end = begin + 30;
  if (end > count) end = count;
  for (size_t i = begin; i < end; i++) {
    if (entries[i].summary == NULL) {
      print_format(registration, "%s (%s)\n", entries[i].name,
                   qa_console_discovery_kind_name(entries[i].kind));
    } else {
      print_format(registration, "%s (%s) - %s\n", entries[i].name,
                   qa_console_discovery_kind_name(entries[i].kind),
                   entries[i].summary);
    }
  }
  print_format(registration,
               "%zu match(es), page %lld/%lld. Use help <name> for details.\n",
               count, (long long)page, (long long)pages);
  if (page < pages) {
    char *quoted = json_stringify(query);
    print_format(registration, "Next page: find %s %lld\n",
                 quoted != NULL ? quoted : "\"\"", (long long)(page + 1));
    free(quoted);
  }
  qa_console_discovery_entries_free(entries, count);
}

static void help_handler(void *ctx, qa_command_invocation *command) {
  qa_console_discovery_registration *registration =
      (qa_console_discovery_registration *)ctx;
  const char *name = command->argc > 1 ? command->argv[1] : NULL;
  if (name == NULL || command->argc != 2) {
    registration->print(
        registration->print_ctx,
        "Usage: help <name>\nUse find <text> to search commands and settings.\n");
    return;
  }
  size_t count = 0;
  qa_console_discovery_entry *entries = qa_query_console_entries(
      registration->commands, &command->source, &count);
  const qa_console_discovery_entry *found = NULL;
  for (size_t i = 0; i < count && found == NULL; i++) {
    if (entries[i].name != NULL && strcmp(entries[i].name, name) == 0) {
      found = &entries[i];
    }
  }
  if (found == NULL) {
    char *folded_name = qa_ascii_fold(name);
    for (size_t i = 0; i < count && found == NULL; i++) {
      char *folded = qa_ascii_fold(entries[i].name != NULL ? entries[i].name : "");
      if (folded != NULL && folded_name != NULL &&
          strcmp(folded, folded_name) == 0) {
        found = &entries[i];
      }
      free(folded);
    }
    free(folded_name);
  }
  if (found == NULL) {
    char *quoted = json_stringify(name);
    print_format(registration,
                 "No command, setting, or alias named %s. Use find <text>.\n",
                 quoted != NULL ? quoted : "\"\"");
    free(quoted);
  } else {
    size_t line_count = 0;
    char **lines = qa_console_entry_help(found, &line_count);
    size_t length = 0;
    for (size_t i = 0; i < line_count; i++) length += strlen(lines[i]) + 1;
    char *joined = (char *)malloc(length + 1);
    if (joined != NULL) {
      size_t at = 0;
      for (size_t i = 0; i < line_count; i++) {
        size_t item = strlen(lines[i]);
        memcpy(joined + at, lines[i], item);
        at += item;
        joined[at++] = '\n';
      }
      joined[at] = '\0';
      registration->print(registration->print_ctx, joined);
      free(joined);
    }
    qa_console_discovery_lines_free(lines, line_count);
  }
  qa_console_discovery_entries_free(entries, count);
}

qa_console_discovery_registration *qa_register_discovery_commands(
    qa_command_buffer *commands, qa_command_print_fn print, void *print_ctx) {
  qa_console_discovery_registration *registration =
      (qa_console_discovery_registration *)calloc(1, sizeof *registration);
  if (registration == NULL) return NULL;
  registration->commands = commands;
  registration->print = print;
  registration->print_ctx = print_ctx;
  static const char *const find_examples[] = {"find r_", "find mouse"};
  static const char *const help_examples[] = {"help echo"};
  static const qa_command_documentation find_doc = {
      "Search command and setting names and descriptions.",
      "find <text> [page]", find_examples, 2, NULL, 0};
  static const qa_command_documentation help_doc = {
      "Show usage and available documentation for a command, setting, or alias.",
      "help <name>", help_examples, 1, NULL, 0};
  if (qa_command_buffer_register_engine(commands, "find", find_handler,
                                        registration, &find_doc)) {
    registration->names[registration->name_count++] = "find";
  }
  if (qa_command_buffer_register_engine(commands, "help", help_handler,
                                        registration, &help_doc)) {
    registration->names[registration->name_count++] = "help";
  }
  return registration;
}

void qa_console_discovery_registration_remove(
    qa_console_discovery_registration *registration) {
  if (registration == NULL) return;
  for (size_t i = 0; i < registration->name_count; i++) {
    (void)qa_command_buffer_unregister(registration->commands,
                                       registration->names[i], NULL, NULL,
                                       false);
  }
  free(registration);
}
