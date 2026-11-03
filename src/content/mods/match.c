/* Port of src/content/mods/match.ts. GPL-2.0-or-later. */
#include "qa/content/mods/match.h"
#include "qa/content/mods/declaration.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *nullable_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_is_null(&f) ? NULL : qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static bool nullable_equal(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

qa_source_team_value *qa_read_source_team_values(const qa_save_reader *reader,
                                                 size_t *out_count) {
  size_t count = qa_save_reader_list_count(reader);
  qa_source_team_value *values =
      count > 0 ? (qa_source_team_value *)qa_mod_declaration_malloc(count * sizeof *values) : NULL;
  if (count > 0 && values == NULL) return NULL;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(reader, i);
    qa_save_reader vf = field(&entry, "value");
    values[i].value = qa_save_reader_finite(&vf);
    qa_save_reader_destroy(&vf);
    values[i].team = nullable_string(&entry, "team");
    qa_save_reader_destroy(&entry);
  }
  /* TS: validateSourceMatchField in try/catch -> reader.fail(msg). */
  bool invalid = count == 0;
  for (size_t i = 0; i < count && !invalid; i++) {
    if (!isfinite(values[i].value) ||
        (values[i].team != NULL && values[i].team[0] == '\0'))
      invalid = true;
    for (size_t j = i + 1; j < count && !invalid; j++)
      if (values[j].value == values[i].value ||
          nullable_equal(values[j].team, values[i].team))
        invalid = true;
  }
  if (invalid)
    qa_save_reader_fail(
        reader,
        "Team projection requires distinct original values and shared identities");
  *out_count = count;
  return values;
}

qa_source_team_alias *qa_read_source_team_aliases(const qa_save_reader *reader,
                                                  size_t *out_count) {
  size_t count = qa_save_reader_list_count(reader);
  qa_source_team_alias *values =
      count > 0 ? (qa_source_team_alias *)qa_mod_declaration_malloc(count * sizeof *values) : NULL;
  if (count > 0 && values == NULL) return NULL;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(reader, i);
    values[i].source = nullable_string(&entry, "source");
    values[i].team = nullable_string(&entry, "team");
    qa_save_reader_destroy(&entry);
  }
  bool invalid = false;
  for (size_t i = 0; i < count && !invalid; i++) {
    if ((values[i].source != NULL && values[i].source[0] == '\0') ||
        (values[i].team != NULL && values[i].team[0] == '\0'))
      invalid = true;
    for (size_t j = i + 1; j < count && !invalid; j++)
      if (nullable_equal(values[j].source, values[i].source) ||
          nullable_equal(values[j].team, values[i].team))
        invalid = true;
  }
  if (invalid)
    qa_save_reader_fail(reader,
                        "team aliases require distinct original and shared identities");
  *out_count = count;
  return values;
}

qa_source_primary_match qa_read_source_primary_match(const qa_save_reader *reader,
                                                     double client_bytes) {
  qa_save_reader score_reader = field(reader, "score");
  double score = qa_save_reader_integer_min(&score_reader, 0);
  qa_save_reader teams_reader = field(reader, "teams");
  size_t count = qa_save_reader_list_count(&teams_reader);
  qa_source_team_command_entry *teams = count > 0
      ? (qa_source_team_command_entry *)qa_mod_declaration_calloc(count, sizeof *teams) : NULL;
  if (count > 0 && teams == NULL) qa_save_reader_fail(&teams_reader, "out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&teams_reader, i);
    teams[i].alias.source = nullable_string(&entry, "source");
    teams[i].alias.team = nullable_string(&entry, "team");
    qa_save_reader args = field(&entry, "arguments");
    size_t acount = qa_save_reader_list_count(&args);
    const char **avals =
        acount > 0 ? (const char **)qa_mod_declaration_malloc(acount * sizeof *avals) : NULL;
    if (acount > 0 && avals == NULL) qa_save_reader_fail(&args, "out of memory");
    for (size_t j = 0; j < acount; j++) {
      qa_save_reader arg = item(&args, j);
      avals[j] = qa_save_reader_string(&arg);
      qa_save_reader_destroy(&arg);
    }
    qa_save_reader_destroy(&args);
    teams[i].arguments.values = avals;
    teams[i].arguments.count = acount;
    qa_save_reader_destroy(&entry);
  }
  if (fmod(score, 4) != 0 || score + 4 > client_bytes)
    qa_save_reader_fail(&score_reader, "score is outside the original client record");
  bool invalid = false;
  for (size_t i = 0; i < count && !invalid; i++) {
    if (teams[i].arguments.count == 0) invalid = true;
    for (size_t j = 0; j < teams[i].arguments.count && !invalid; j++) {
      const char *arg = teams[i].arguments.values[j];
      if (arg[0] == '\0' || strpbrk(arg, "\r\n") != NULL) invalid = true;
    }
    for (size_t j = i + 1; j < count && !invalid; j++)
      if (nullable_equal(teams[j].alias.source, teams[i].alias.source) ||
          nullable_equal(teams[j].alias.team, teams[i].alias.team))
        invalid = true;
  }
  if (invalid)
    qa_save_reader_fail(&teams_reader,
                        "team changes require distinct identities and complete original command arguments");
  qa_save_reader_destroy(&score_reader);
  qa_save_reader_destroy(&teams_reader);
  qa_source_primary_match result = {score, teams, count};
  return result;
}

static void *read_nullable(const qa_save_reader *reader, const char *name,
                            void (*read)(void *, const qa_save_reader *, void *),
                            void *ctx, size_t size) {
  qa_save_reader f = field(reader, name);
  if (qa_save_reader_is_null(&f)) {
    qa_save_reader_destroy(&f);
    return NULL;
  }
  void *storage = qa_mod_declaration_malloc(size == 0 ? 1 : size);
  if (storage == NULL) qa_save_reader_fail(&f, "out of memory");
  read(ctx, &f, storage);
  qa_save_reader_destroy(&f);
  return storage;
}

qa_source_objective_declaration *qa_read_source_objectives(
    const qa_save_reader *reader,
    void (*read_scalar)(void *ctx, const qa_save_reader *reader, void *out),
    void *scalar_ctx, size_t scalar_size,
    void (*read_reference)(void *ctx, const qa_save_reader *reader, void *out),
    void *reference_ctx, size_t reference_size,
    void (*read_call)(void *ctx, const qa_save_reader *reader, void *out),
    void *call_ctx, size_t call_size, size_t *out_count) {
  static const char *const roles[] = {"owned", "borrowed"};
  size_t count = qa_save_reader_list_count(reader);
  qa_source_objective_declaration *decls = count > 0
      ? (qa_source_objective_declaration *)qa_mod_declaration_calloc(count, sizeof *decls) : NULL;
  if (count > 0 && decls == NULL) return NULL;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(reader, i);
    qa_save_reader state = field(&entry, "state");
    qa_save_reader values_reader = field(&state, "values");
    size_t vcount = qa_save_reader_list_count(&values_reader);
    qa_source_objective_value *values = vcount > 0
        ? (qa_source_objective_value *)qa_mod_declaration_malloc(vcount * sizeof *values) : NULL;
    if (vcount > 0 && values == NULL) qa_save_reader_fail(&values_reader, "out of memory");
    for (size_t j = 0; j < vcount; j++) {
      qa_save_reader row = item(&values_reader, j);
      qa_save_reader vf = field(&row, "value");
      values[j].value = qa_save_reader_finite(&vf);
      qa_save_reader_destroy(&vf);
      qa_save_reader sf = field(&row, "stage");
      values[j].stage = qa_save_reader_string(&sf);
      qa_save_reader_destroy(&sf);
      qa_save_reader cf = field(&row, "complete");
      values[j].complete = qa_save_reader_boolean(&cf);
      qa_save_reader_destroy(&cf);
      qa_save_reader_destroy(&row);
    }
    bool invalid = vcount == 0;
    for (size_t j = 0; j < vcount && !invalid; j++) {
      if (values[j].stage[0] == '\0') invalid = true;
      for (size_t k = j + 1; k < vcount && !invalid; k++)
        if (values[k].value == values[j].value ||
            strcmp(values[k].stage, values[j].stage) == 0)
          invalid = true;
    }
    if (invalid)
      qa_save_reader_fail(&state,
                          "objective stages require distinct source values and shared names");
    qa_save_reader id_reader = field(&entry, "id");
    decls[i].id = qa_namespaced(&id_reader);
    qa_save_reader_destroy(&id_reader);
    {
      qa_save_reader storage = field(&state, "storage");
      void *scalar = qa_mod_declaration_malloc(scalar_size == 0 ? 1 : scalar_size);
      if (scalar == NULL) qa_save_reader_fail(&storage, "out of memory");
      read_scalar(scalar_ctx, &storage, scalar);
      qa_save_reader_destroy(&storage);
      decls[i].state_storage = scalar;
    }
    decls[i].state_values = values;
    decls[i].state_value_count = vcount;
    decls[i].carrier = read_nullable(&entry, "carrier", read_reference,
                                     reference_ctx, reference_size);
    decls[i].target = read_nullable(&entry, "target", read_reference,
                                    reference_ctx, reference_size);
    qa_save_reader role_reader = field(&entry, "role");
    const char *role = qa_save_reader_choice_string(&role_reader, roles, 2);
    qa_save_reader_destroy(&role_reader);
    if (strcmp(role, "owned") == 0) {
      decls[i].is_owned = true;
      qa_save_reader f = field(&entry, "campaignGate");
      decls[i].campaign_gate = qa_save_reader_boolean(&f);
      qa_save_reader_destroy(&f);
      f = field(&entry, "botGoal");
      decls[i].bot_goal = qa_save_reader_boolean(&f);
      qa_save_reader_destroy(&f);
      decls[i].change = read_nullable(&entry, "change", read_call, call_ctx, call_size);
    } else {
      decls[i].is_owned = false;
      qa_save_reader f = field(&entry, "writable");
      decls[i].writable = qa_save_reader_boolean(&f);
      qa_save_reader_destroy(&f);
    }
    qa_save_reader_destroy(&values_reader);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&entry);
  }
  for (size_t i = 0; i < count; i++)
    for (size_t j = i + 1; j < count; j++)
      if (strcmp(decls[j].id, decls[i].id) == 0)
        qa_save_reader_fail(reader, "duplicate objective channel");
  *out_count = count;
  return decls;
}
