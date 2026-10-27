/* Port of src/content/q2/rerelease/monsters/generate-tables.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/monsters/generate_tables.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/weapons/types.h"

/* ---- dynamic string ---- */
typedef struct str { char *data; size_t length, cap; } str;
static void str_init(str *s) { s->data = NULL; s->length = 0; s->cap = 0; }
static void str_push(str *s, const char *text, size_t length) {
  if (s->length + length + 1 > s->cap) {
    size_t cap = s->cap ? s->cap * 2 : 256;
    while (cap < s->length + length + 1) cap *= 2;
    char *grown = realloc(s->data, cap);
    if (!grown) qa_q2f_error("Out of memory generating monster tables");
    s->data = grown;
    s->cap = cap;
  }
  memcpy(s->data + s->length, text, length);
  s->length += length;
  s->data[s->length] = '\0';
}
static void str_puts(str *s, const char *text) { str_push(s, text, strlen(text)); }
static void str_putc(str *s, char c) { str_push(s, &c, 1); }
static char *dup_str(const char *text) {
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (!copy) qa_q2f_error("Out of memory generating monster tables");
  memcpy(copy, text, length + 1);
  return copy;
}
static char *dup_range(const char *start, size_t length) {
  char *copy = malloc(length + 1);
  if (!copy) qa_q2f_error("Out of memory generating monster tables");
  memcpy(copy, start, length);
  copy[length] = '\0';
  return copy;
}
static char *trim(const char *text) {
  while (isspace((unsigned char)*text)) text++;
  size_t length = strlen(text);
  while (length && isspace((unsigned char)text[length - 1])) length--;
  return dup_range(text, length);
}
static void free_parts(char **parts, size_t count) {
  for (size_t i = 0; i < count; i++) free(parts[i]);
  free(parts);
}

/* ---- TS helpers ---- */
static char *clean_source(const char *source) {
  str out;
  str_init(&out);
  for (size_t i = 0; source[i];) {
    if (source[i] == '/' && source[i + 1] == '*') {
      i += 2;
      while (source[i] && !(source[i] == '*' && source[i + 1] == '/')) i++;
      if (source[i]) i += 2;
    } else if (source[i] == '/' && source[i + 1] == '/') {
      while (source[i] && source[i] != '\n') i++;
    } else str_putc(&out, source[i++]);
  }
  return out.data ? out.data : dup_str("");
}
static size_t split_top(const char *source, char ***out) {
  size_t count = 0, cap = 0;
  char **parts = NULL;
  int depth = 0;
  const char *start = source;
  for (const char *p = source; ; p++) {
    if (*p == '{' || *p == '(' || *p == '[') depth++;
    if (*p == '}' || *p == ')' || *p == ']') depth--;
    if ((*p == ',' && depth == 0) || *p == '\0') {
      if (*p != '\0' || p > start) {
        char *raw = dup_range(start, (size_t)(p - start));
        char *part = trim(raw);
        free(raw);
        if (!(*p == '\0' && strcmp(part, "") == 0)) {
          if (count == cap) {
            cap = cap ? cap * 2 : 8;
            parts = realloc(parts, cap * sizeof *parts);
            if (!parts) qa_q2f_error("Out of memory generating monster tables");
          }
          parts[count++] = part;
        } else free(part);
      }
      if (*p == '\0') break;
      start = p + 1;
    }
  }
  *out = parts;
  return count;
}
static char *body_of(const char *source, const char *start) {
  int depth = 0;
  for (const char *p = start; *p; p++) {
    if (*p == '{') depth++;
    if (*p == '}' && --depth == 0) return dup_range(start + 1, (size_t)(p - start - 1));
  }
  (void)source;
  qa_q2f_error("Unclosed source initializer");
}

/* ---- numeric constants map ---- */
typedef struct const_entry { char *name; double value; } const_entry;
typedef struct const_map { const_entry *entries; size_t count, cap; } const_map;
static bool map_get(const const_map *map, const char *name, double *out) {
  for (size_t i = 0; i < map->count; i++)
    if (strcmp(map->entries[i].name, name) == 0) { *out = map->entries[i].value; return true; }
  return false;
}
static void map_set(const_map *map, const char *name, double value) {
  for (size_t i = 0; i < map->count; i++)
    if (strcmp(map->entries[i].name, name) == 0) { map->entries[i].value = value; return; }
  if (map->count == map->cap) {
    map->cap = map->cap ? map->cap * 2 : 16;
    map->entries = realloc(map->entries, map->cap * sizeof *map->entries);
    if (!map->entries) qa_q2f_error("Out of memory generating monster tables");
  }
  map->entries[map->count].name = dup_str(name);
  map->entries[map->count].value = value;
  map->count++;
}
static bool plain_number(const char *text, double *out) {
  if (!*text) return false;
  const char *p = text;
  if (*p == '-') p++;
  bool digits = false, dot = false;
  for (; *p; p++) {
    if (isdigit((unsigned char)*p)) digits = true;
    else if (*p == '.' && !dot) dot = true;
    else return false;
  }
  if (!digits) return false;
  *out = strtod(text, NULL);
  return true;
}
static double numeric_value(const char *source, const const_map *values) {
  char *raw = trim(source);
  /* replace /(?<=[\d.])f\b/g with "" */
  str cleaned;
  str_init(&cleaned);
  for (size_t i = 0; raw[i]; i++) {
    if (raw[i] == 'f' && i > 0 && (isdigit((unsigned char)raw[i - 1]) || raw[i - 1] == '.')
        && !isalnum((unsigned char)raw[i + 1]) && raw[i + 1] != '_') continue;
    str_putc(&cleaned, raw[i]);
  }
  free(raw);
  char *text = cleaned.data ? cleaned.data : dup_str("");
  double found;
  if (map_get(values, text, &found)) { free(text); return found; }
  if (plain_number(text, &found)) { free(text); return found; }
  /* binary: left op right-literal, op the last of + * - that splits. */
  size_t length = strlen(text);
  for (size_t split = length; split-- > 1;) {
    char op = text[split];
    if (op != '+' && op != '*' && op != '-') continue;
    char *right_text = trim(text + split + 1);
    double right;
    bool ok = plain_number(right_text, &right);
    free(right_text);
    if (!ok) continue;
    char *left_text = dup_range(text, split);
    double left = numeric_value(left_text, values);
    free(left_text);
    free(text);
    return op == '+' ? left + right : op == '-' ? left - right : left * right;
  }
  qa_q2f_error("Unsupported source number %s", source);
}
static bool has_prefix(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}
static bool word_char(char c) { return isalnum((unsigned char)c) || c == '_'; }
static void read_constants(const char *source, const_map *result) {
  /* #define NAME value */
  for (const char *p = source; (p = strstr(p, "#define")) != NULL; p += 7) {
    if (!isspace((unsigned char)p[7])) continue;
    const char *name_at = p + 7;
    while (isspace((unsigned char)*name_at)) name_at++;
    const char *name_end = name_at;
    while (word_char(*name_end)) name_end++;
    if (name_end == name_at) continue;
    const char *value_at = name_end;
    while (*value_at == ' ' || *value_at == '\t') value_at++;
    if (value_at == name_end) continue;
    const char *line_end = strchr(value_at, '\n');
    if (!line_end) line_end = value_at + strlen(value_at);
    char *name = dup_range(name_at, (size_t)(name_end - name_at));
    if (has_prefix(name, "FRAME_") || has_prefix(name, "MODEL_SCALE")) {
      char *value = dup_range(value_at, (size_t)(line_end - value_at));
      map_set(result, name, numeric_value(value, result));
      free(value);
    }
    free(name);
  }
  /* enum { ... } */
  for (const char *p = source; (p = strstr(p, "enum")) != NULL; p += 4) {
    const char *open = strchr(p, '{');
    if (!open) break;
    bool braces_only = true;
    for (const char *q = p + 4; q < open; q++) if (*q == '{' || *q == '}') braces_only = false;
    if (!braces_only) continue;
    char *body = NULL;
    {
      int depth = 0;
      const char *end = open;
      for (; *end; end++) {
        if (*end == '{') depth++;
        if (*end == '}' && --depth == 0) break;
      }
      if (*end != '}') continue;
      body = dup_range(open + 1, (size_t)(end - open - 1));
    }
    char **fields = NULL;
    size_t field_count = split_top(body, &fields);
    double next = 0;
    for (size_t i = 0; i < field_count; i++) {
      char *eq = strchr(fields[i], '=');
      char *name = trim(eq ? (char *)dup_range(fields[i], (size_t)(eq - fields[i])) : fields[i]);
      if (eq) free((void *)name), name = dup_range(fields[i], (size_t)(eq - fields[i])), name = trim(name);
      bool frame_or_flash = has_prefix(name, "FRAME_") || has_prefix(name, "MZ2_");
      bool all_word = *name != '\0';
      for (const char *q = name; *q; q++) if (!word_char(*q)) all_word = false;
      if (frame_or_flash && all_word) {
        if (eq) {
          char *value = trim(eq + 1);
          next = numeric_value(value, result);
          free(value);
        }
        map_set(result, name, next++);
      }
      free(name);
    }
    free_parts(fields, field_count);
    free(body);
  }
}
static void format_number(double value, char *buf, size_t size) {
  if (value == trunc(value) && fabs(value) < 1e15) (void)snprintf(buf, size, "%lld", (long long)value);
  else (void)snprintf(buf, size, "%g", value);
}
static void json_string(str *out, const char *text) {
  str_putc(out, '"');
  str_puts(out, text);
  str_putc(out, '"');
}
static char *encode_actions(const char *source, const const_map *values) {
  if (!source || strcmp(source, "nullptr") == 0 || strcmp(source, "NULL") == 0) return dup_str("[]");
  bool word = *source != '\0';
  for (const char *p = source; *p; p++) if (!word_char(*p)) word = false;
  if (word) {
    str out;
    str_init(&out);
    str_puts(&out, "[");
    json_string(&out, source);
    str_puts(&out, "]");
    return out.data;
  }
  if (has_prefix(source, "[]")) {
    const char *open = strchr(source, '{');
    if (!open) qa_q2f_error("Unsupported source frame action %s", source);
    char *inner = body_of(source, open);
    str out;
    str_init(&out);
    str_puts(&out, "[");
    bool first = true;
    const char *statement = inner;
    for (;;) {
      const char *semi = strchr(statement, ';');
      char *raw = semi ? dup_range(statement, (size_t)(semi - statement)) : dup_str(statement);
      char *stmt = trim(raw);
      free(raw);
      if (strcmp(stmt, "") != 0) {
        if (!first) str_puts(&out, ", ");
        first = false;
        size_t length = strlen(stmt);
        if (length > 6 && strcmp(stmt + length - 6, "(self)") == 0 && strchr(stmt, '(') == stmt + length - 6) {
          char *name = dup_range(stmt, length - 6);
          bool ok = *name != '\0';
          for (const char *q = name; *q; q++) if (!word_char(*q)) ok = false;
          if (!ok) qa_q2f_error("Unsupported source frame statement %s", stmt);
          json_string(&out, name);
          free(name);
        } else if (has_prefix(stmt, "self->monsterinfo.nextframe")) {
          const char *eq = strchr(stmt, '=');
          if (!eq) qa_q2f_error("Unsupported source frame statement %s", stmt);
          char *frame_text = trim(eq + 1);
          char number[64];
          format_number(numeric_value(frame_text, values), number, sizeof number);
          str_puts(&out, "{\"kind\":\"nextframe\",\"frame\":");
          str_puts(&out, number);
          str_puts(&out, "}");
          free(frame_text);
        } else qa_q2f_error("Unsupported source frame statement %s", stmt);
      }
      free(stmt);
      if (!semi) break;
      statement = semi + 1;
    }
    str_puts(&out, "]");
    free(inner);
    return out.data;
  }
  qa_q2f_error("Unsupported source frame action %s", source);
}

static char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) qa_q2f_error("Cannot open %s", path);
  str contents;
  str_init(&contents);
  char buffer[8192];
  size_t read_count;
  while ((read_count = fread(buffer, 1, sizeof buffer, file)) > 0) str_push(&contents, buffer, read_count);
  fclose(file);
  return contents.data ? contents.data : dup_str("");
}
static void write_file(const char *path, const char *contents) {
  FILE *file = fopen(path, "wb");
  if (!file) qa_q2f_error("Cannot write %s", path);
  fwrite(contents, 1, strlen(contents), file);
  fclose(file);
}
static char *join_path(const char *root, const char *leaf) {
  str path;
  str_init(&path);
  str_puts(&path, root);
  str_puts(&path, "/");
  str_puts(&path, leaf);
  return path.data;
}

typedef struct string_list { char **items; size_t count, cap; } string_list;
static void list_push(string_list *list, char *item) {
  if (list->count == list->cap) {
    list->cap = list->cap ? list->cap * 2 : 8;
    list->items = realloc(list->items, list->cap * sizeof *list->items);
    if (!list->items) qa_q2f_error("Out of memory generating monster tables");
  }
  list->items[list->count++] = item;
}
typedef struct array_entry { char *name; string_list rows; } array_entry;

void qa_q2rm_generate_monster_table(const char *root, const char *output_dir, const char *species) {
  char leaf[256];
  (void)snprintf(leaf, sizeof leaf, "m_%s.cpp", species);
  char *source_path = join_path(root, leaf);
  char *raw_source = read_file(source_path);
  free(source_path);
  char *source = clean_source(raw_source);
  free(raw_source);
  /* #include "m_*.h" */
  const char *include_at = strstr(source, "#include \"m_");
  if (!include_at) qa_q2f_error("%s: missing frame header", species);
  const char *header_at = include_at + strlen("#include \"");
  const char *header_end = strchr(header_at, '"');
  if (!header_end) qa_q2f_error("%s: missing frame header", species);
  char *header = dup_range(header_at, (size_t)(header_end - header_at));
  char *header_path = join_path(root, header);
  char *raw_header = read_file(header_path);
  free(header_path);
  char *header_source = clean_source(raw_header);
  free(raw_header);
  const_map values = {NULL, 0, 0};
  read_constants(header_source, &values);
  free(header_source);
  /* constexpr float|int|int32_t NAME = value; */
  for (const char *p = source; (p = strstr(p, "constexpr")) != NULL; p += 9) {
    const char *q = p + 9;
    while (isspace((unsigned char)*q)) q++;
    if (!has_prefix(q, "float") && !has_prefix(q, "int32_t") && !has_prefix(q, "int")) continue;
    q += has_prefix(q, "float") ? 5 : has_prefix(q, "int32_t") ? 7 : 3;
    if (!isspace((unsigned char)*q)) continue;
    while (isspace((unsigned char)*q)) q++;
    const char *name_end = q;
    while (word_char(*name_end)) name_end++;
    if (name_end == q) continue;
    const char *eq = name_end;
    while (isspace((unsigned char)*eq)) eq++;
    if (*eq != '=') continue;
    const char *semi = strchr(eq, ';');
    if (!semi) continue;
    char *value_raw = dup_range(eq + 1, (size_t)(semi - eq - 1));
    char *value = trim(value_raw);
    free(value_raw);
    bool simple = *value != '\0';
    for (const char *r = value; *r; r++)
      if (!isdigit((unsigned char)*r) && *r != '-' && *r != '.' && *r != 'f') simple = false;
    if (simple) {
      char *name = dup_range(q, (size_t)(name_end - q));
      map_set(&values, name, numeric_value(value, &values));
      free(name);
    }
    free(value);
  }
  /* mframe_t NAME[...] = { rows } */
  array_entry *arrays = NULL;
  size_t array_count = 0, array_cap = 0;
  for (const char *p = source; (p = strstr(p, "mframe_t")) != NULL; p += 8) {
    const char *q = p + 8;
    if (!isspace((unsigned char)*q)) continue;
    while (isspace((unsigned char)*q)) q++;
    const char *name_end = q;
    while (word_char(*name_end)) name_end++;
    if (name_end == q || *name_end != '[') continue;
    const char *open = strchr(name_end, '{');
    if (!open) continue;
    char *inner = body_of(source, open);
    char **rows = NULL;
    size_t row_count = split_top(inner, &rows);
    free(inner);
    string_list encoded = {NULL, 0, 0};
    for (size_t r = 0; r < row_count; r++) {
      const char *row_open = strchr(rows[r], '{');
      if (!row_open) qa_q2f_error("Unsupported frame AI %s", rows[r]);
      char *row_body = body_of(rows[r], row_open);
      char **fields = NULL;
      size_t field_count = split_top(row_body, &fields);
      free(row_body);
      const char *ai = field_count > 0 ? fields[0] : "nullptr";
      const char *distance = field_count > 1 ? fields[1] : "0";
      const char *callback = field_count > 2 ? fields[2] : NULL;
      const char *lerp = field_count > 3 ? fields[3] : "-1";
      bool ai_word = *ai != '\0';
      for (const char *s = ai; *s; s++) if (!word_char(*s)) ai_word = false;
      if (!ai_word) qa_q2f_error("Unsupported frame AI %s", ai);
      const char *kind = (strcmp(ai, "nullptr") == 0 || strcmp(ai, "NULL") == 0) ? "none"
          : has_prefix(ai, "ai_") ? ai + 3 : ai;
      bool known = strcmp(kind, "stand") == 0 || strcmp(kind, "walk") == 0 || strcmp(kind, "run") == 0
          || strcmp(kind, "charge") == 0 || strcmp(kind, "move") == 0 || strcmp(kind, "soldier_move") == 0
          || strcmp(kind, "turn") == 0 || strcmp(kind, "none") == 0;
      str encoded_ai;
      str_init(&encoded_ai);
      if (known) json_string(&encoded_ai, kind);
      else {
        str_puts(&encoded_ai, "{\"kind\":\"source\",\"name\":");
        json_string(&encoded_ai, ai);
        str_puts(&encoded_ai, "}");
      }
      char *actions = encode_actions(callback, &values);
      char dist_text[64], lerp_text[64];
      format_number(numeric_value(distance, &values), dist_text, sizeof dist_text);
      format_number(numeric_value(lerp, &values), lerp_text, sizeof lerp_text);
      str row;
      str_init(&row);
      str_puts(&row, "    { ai: ");
      str_puts(&row, encoded_ai.data);
      str_puts(&row, ", distance: Math.fround(");
      str_puts(&row, dist_text);
      str_puts(&row, "), actions: ");
      str_puts(&row, actions);
      str_puts(&row, ", lerpFrame: ");
      str_puts(&row, lerp_text);
      str_puts(&row, " },");
      list_push(&encoded, row.data);
      free(encoded_ai.data);
      free(actions);
      free_parts(fields, field_count);
    }
    free_parts(rows, row_count);
    if (array_count == array_cap) {
      array_cap = array_cap ? array_cap * 2 : 8;
      arrays = realloc(arrays, array_cap * sizeof *arrays);
      if (!arrays) qa_q2f_error("Out of memory generating monster tables");
    }
    arrays[array_count].name = dup_range(q, (size_t)(name_end - q));
    arrays[array_count].rows = encoded;
    array_count++;
  }
  /* MMOVE_T(NAME) = { ... } */
  string_list moves = {NULL, 0, 0};
  for (const char *p = source; (p = strstr(p, "MMOVE_T(")) != NULL; p += 8) {
    const char *name_at = p + 8;
    const char *name_end = name_at;
    while (word_char(*name_end)) name_end++;
    const char *open = strchr(name_end, '{');
    if (!open || name_end == name_at) continue;
    char *inner = body_of(source, open);
    char **fields = NULL;
    size_t field_count = split_top(inner, &fields);
    free(inner);
    if (field_count < 3) qa_q2f_error("Incomplete source move");
    char *name = dup_range(name_at, (size_t)(name_end - name_at));
    const string_list *table = NULL;
    for (size_t a = 0; a < array_count; a++)
      if (strcmp(arrays[a].name, fields[2]) == 0) table = &arrays[a].rows;
    if (!table) qa_q2f_error("%s: unknown frame array %s", name, fields[2]);
    double start_frame = numeric_value(fields[0], &values);
    double finish_frame = numeric_value(fields[1], &values);
    if ((double)table->count < finish_frame - start_frame + 1)
      qa_q2f_error("%s: incomplete frame table", name);
    const char *end = field_count > 3 ? fields[3] : "nullptr";
    const char *scale_text = "0";
    char *scale_owned = NULL;
    for (size_t f = 4; f < field_count; f++) {
      if (has_prefix(fields[f], ".sidestep_scale")) {
        const char *eq = strchr(fields[f], '=');
        scale_owned = eq ? trim(eq + 1) : dup_str("0");
        scale_text = scale_owned;
      }
    }
    char start_text[64], finish_text[64], scale_out[64];
    format_number(start_frame, start_text, sizeof start_text);
    format_number(finish_frame, finish_text, sizeof finish_text);
    format_number(numeric_value(scale_text, &values), scale_out, sizeof scale_out);
    str move;
    str_init(&move);
    str_puts(&move, "  { name: ");
    json_string(&move, name);
    str_puts(&move, ", firstFrame: ");
    str_puts(&move, start_text);
    str_puts(&move, ", lastFrame: ");
    str_puts(&move, finish_text);
    str_puts(&move, ", end: ");
    if (strcmp(end, "nullptr") == 0) str_puts(&move, "null");
    else json_string(&move, end);
    str_puts(&move, ", sidestepScale: ");
    str_puts(&move, scale_out);
    str_puts(&move, ", frames: [\n");
    for (size_t r = 0; r < table->count; r++) {
      str_puts(&move, table->items[r]);
      str_puts(&move, "\n");
    }
    str_puts(&move, "  ] },");
    list_push(&moves, move.data);
    free(scale_owned);
    free(name);
    free_parts(fields, field_count);
  }
  if (moves.count == 0) qa_q2f_error("%s: no source moves", species);
  str output;
  str_init(&output);
  char header_line[512];
  (void)snprintf(header_line, sizeof header_line,
                 "// Generated from rerelease/m_%s.cpp and %s. ZeniMax Media, GPL-2.0.\n", species, header);
  str_puts(&output, header_line);
  str_puts(&output, "import type { MonsterMove } from \"../../../foundation/monsters/types.ts\";\n\nexport const ");
  str_puts(&output, species);
  str_puts(&output, "Frame = {\n");
  for (size_t i = 0; i < values.count; i++) {
    if (!has_prefix(values.entries[i].name, "FRAME_")) continue;
    char number[64];
    format_number(values.entries[i].value, number, sizeof number);
    str_puts(&output, "  ");
    str_puts(&output, values.entries[i].name + 6);
    str_puts(&output, ": ");
    str_puts(&output, number);
    str_puts(&output, ",\n");
  }
  str_puts(&output, "};\n\nexport const ");
  str_puts(&output, species);
  str_puts(&output, "Moves: readonly MonsterMove[] = [\n");
  for (size_t i = 0; i < moves.count; i++) {
    str_puts(&output, moves.items[i]);
    str_puts(&output, "\n");
  }
  str_puts(&output, "];\n");
  char out_leaf[256];
  (void)snprintf(out_leaf, sizeof out_leaf, "%s.ts", species);
  char *out_path = join_path(output_dir, out_leaf);
  write_file(out_path, output.data);
  free(out_path);
  printf("%s: %zu original moves\n", species, moves.count);
  /* leak build-time allocations like the TS process would GC. */
  (void)arrays;
  (void)source;
  (void)header;
}

void qa_q2rm_generate_tables(const char *output_dir, int argc, char **argv) {
  const char *source_root = getenv("QTS_Q2_RERELEASE_SOURCE");
  if (!source_root)
    qa_q2f_error("Set QTS_Q2_RERELEASE_SOURCE to the Quake II rerelease DLL source directory");
  for (int i = 2; i < argc; i++) qa_q2rm_generate_monster_table(source_root, output_dir, argv[i]);
  char *game_path = join_path(source_root, "game.h");
  char *raw_game = read_file(game_path);
  free(game_path);
  char *game_source = clean_source(raw_game);
  free(raw_game);
  const_map values = {NULL, 0, 0};
  read_constants(game_source, &values);
  free(game_source);
  str output;
  str_init(&output);
  str_puts(&output, "// Original rerelease/game.h muzzle-flash enum. ZeniMax Media, GPL-2.0.\nexport const rereleaseFlash = {\n");
  for (size_t i = 0; i < values.count; i++) {
    if (!has_prefix(values.entries[i].name, "MZ2_")) continue;
    char number[64];
    format_number(values.entries[i].value, number, sizeof number);
    str_puts(&output, "  ");
    str_puts(&output, values.entries[i].name + 4);
    str_puts(&output, ": ");
    str_puts(&output, number);
    str_puts(&output, ",\n");
  }
  str_puts(&output, "};\n");
  char *out_path = join_path(output_dir, "flashes.ts");
  write_file(out_path, output.data);
  free(out_path);
}
