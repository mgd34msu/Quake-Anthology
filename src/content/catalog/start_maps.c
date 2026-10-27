/* Port of src/content/catalog/start-maps.ts. JSON via the
 * persistence value tree (qa_parse_json_value). GPL-2.0-or-later. */
#include "qa/content/catalog/start_maps.h"

#include "catalog_internal.h"
#include "qa/persistence/value.h"

static bool is_record(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_OBJECT;
}

static const qa_save_value *record(const qa_save_value *value) {
  if (!is_record(value)) qa_catalog_error("Invalid mapdb.json object");
  return value;
}

static const char *string_field(const qa_save_value *value) {
  if (value == NULL || value->kind == QA_SAVE_UNDEFINED) return "";
  if (value->kind != QA_SAVE_STRING) qa_catalog_error("Invalid mapdb.json string");
  return value->text != NULL ? value->text : "";
}

static bool flag_field(const qa_save_value *value) {
  if (value == NULL || value->kind == QA_SAVE_UNDEFINED) return false;
  if (value->kind != QA_SAVE_BOOLEAN) qa_catalog_error("Invalid mapdb.json flag");
  return value->boolean;
}

static const qa_save_value *entries(const qa_save_value *value, size_t *count) {
  *count = 0;
  if (value == NULL || value->kind == QA_SAVE_UNDEFINED) return NULL;
  if (value->kind != QA_SAVE_ARRAY) qa_catalog_error("Invalid mapdb.json array");
  *count = value->item_count;
  return value;
}

static void free_episode(qa_authored_episode *episode) {
  if (episode == NULL) return;
  free(episode->id); free(episode->command); free(episode->name);
  free(episode->activity); free(episode);
}

static void free_start(qa_authored_start_map *start) {
  free(start->episode); free(start->bsp); free(start->path);
  free(start->title); free(start->start_items);
}

void qa_authored_starts_free(qa_authored_starts *starts) {
  free_episode(starts->episode);
  for (size_t i = 0; i < starts->start_count; i++) free_start(&starts->starts[i]);
  free(starts->starts);
  starts->episode = NULL; starts->starts = NULL; starts->start_count = 0;
}

bool qa_parse_authored_starts(const uint8_t *bytes, size_t length,
                              const char *campaign, qa_authored_starts *out) {
  memset(out, 0, sizeof *out);
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return false;
  const qa_save_value *database = record(value);
  const char *episode_id = strcmp(campaign, "ctf") == 0 ? "baseq2" : campaign;
  size_t count = 0;
  const qa_save_value *episodes = entries(qa_save_value_get(database, "episodes"), &count);
  for (size_t i = 0; i < count; i++) {
    const qa_save_value *row = record(episodes->items[i]);
    if (strcmp(string_field(qa_save_value_get(row, "id")), episode_id) != 0) continue;
    qa_authored_episode *episode = (qa_authored_episode *)calloc(1, sizeof *episode);
    if (episode == NULL) { qa_save_value_free(value); return false; }
    episode->id = qa_catalog_dup(string_field(qa_save_value_get(row, "id")));
    episode->command = qa_catalog_dup(string_field(qa_save_value_get(row, "command")));
    episode->name = qa_catalog_dup(string_field(qa_save_value_get(row, "name")));
    episode->activity = qa_catalog_dup(string_field(qa_save_value_get(row, "activity")));
    episode->needs_skill_select = flag_field(qa_save_value_get(row, "needsSkillSelect"));
    out->episode = episode;
    break;
  }
  const qa_save_value *maps = entries(qa_save_value_get(database, "maps"), &count);
  size_t capacity = 0;
  for (size_t i = 0; i < count; i++) {
    const qa_save_value *row = record(maps->items[i]);
    bool wanted = strcmp(campaign, "ctf") == 0
                      ? flag_field(qa_save_value_get(row, "ctf"))
                      : flag_field(qa_save_value_get(row, "sp"));
    if (strcmp(string_field(qa_save_value_get(row, "episode")), episode_id) != 0 || !wanted)
      continue;
    const char *bsp = string_field(qa_save_value_get(row, "bsp"));
    const char *last = strrchr(bsp, '+');
    last = last != NULL ? last + 1 : bsp;
    const char *name = last[0] == '*' ? last + 1 : last;
    bool bad = name[0] == '\0';
    for (size_t j = 0; name[j] != '\0' && !bad; j++)
      if (name[j] == '+' || name[j] == '*' || name[j] == '$' || name[j] == ';' ||
          isspace((unsigned char)name[j])) bad = true;
    if (bad) qa_catalog_error("Invalid mapdb.json start BSP: %s", bsp);
    char *path_input = qa_catalog_format("maps/%s.bsp", name);
    if (path_input == NULL) { qa_save_value_free(value); qa_authored_starts_free(out); return false; }
    char *path = qa_catalog_normalize_resource_path(path_input);
    free(path_input);
    if (path == NULL) { qa_save_value_free(value); qa_authored_starts_free(out); return false; }
    if (out->start_count == capacity) {
      size_t next = capacity != 0 ? capacity * 2 : 8;
      qa_authored_start_map *grown =
          (qa_authored_start_map *)realloc(out->starts, next * sizeof *grown);
      if (grown == NULL) { free(path); qa_save_value_free(value); qa_authored_starts_free(out); return false; }
      out->starts = grown; capacity = next;
    }
    qa_authored_start_map *start = &out->starts[out->start_count++];
    memset(start, 0, sizeof *start);
    start->episode = qa_catalog_dup(episode_id);
    start->bsp = qa_catalog_dup(bsp);
    start->path = path;
    start->title = qa_catalog_dup(string_field(qa_save_value_get(row, "title")));
    start->start_items = qa_catalog_dup(string_field(qa_save_value_get(row, "start_items")));
    start->singleplayer = flag_field(qa_save_value_get(row, "sp"));
    start->cooperative = flag_field(qa_save_value_get(row, "coop"));
    start->capture_the_flag = flag_field(qa_save_value_get(row, "ctf"));
  }
  qa_save_value_free(value);
  return true;
}
