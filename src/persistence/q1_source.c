/* Port of src/persistence/q1-source.ts. GPL-2.0-or-later. */
#include "qa/persistence/q1_source.h"

#include <ctype.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (copy != NULL) memcpy(copy, text, length + 1);
  return copy;
}

typedef struct view {
  const char *text;
  size_t length;
} view;

/* requestedPath.replace(/^maps\//, "").replace(/\.bsp$/, "") */
static view map_name(const char *requested_path) {
  const char *text = requested_path != NULL ? requested_path : "";
  size_t length = strlen(text);
  if (length >= 5 && memcmp(text, "maps/", 5) == 0) {
    text += 5;
    length -= 5;
  }
  if (length >= 4 && memcmp(text + length - 4, ".bsp", 4) == 0) length -= 4;
  return (view){text, length};
}

static bool is_free_slot(void *ctx, size_t slot) {
  qa_quakec_source *source = ctx;
  return source->slot_is_free(source->ctx, slot);
}

bool qa_capture_q1_source_save(qa_quakec_source *source, int format_version,
                               const char *game_directories, const char *comment,
                               const double *spawn_parameters,
                               size_t spawn_parameter_count,
                               const char *extension_text,
                               qa_q1_save_data *out) {
  memset(out, 0, sizeof *out);
  source->checkpoint(source->ctx);
  if (source->kind == NULL || strcmp(source->kind, "netquake") != 0 ||
      source->max_clients != 1 || source->mode == NULL ||
      strcmp(source->mode, "singleplayer") != 0)
    qa_error("Original Quake saves require a singleplayer NetQuake source");
  qa_q1_save_header header;
  memset(&header, 0, sizeof header);
  header.format_version = format_version;
  if (game_directories != NULL) {
    header.game_directories = dup_text(game_directories);
    if (header.game_directories == NULL) goto oom;
  }
  header.comment = dup_text(comment != NULL ? comment : "");
  if (header.comment == NULL) goto oom;
  for (char *c = header.comment; *c != '\0'; c++)
    if (isspace((unsigned char)*c)) *c = '_';
  if (spawn_parameter_count > 0) {
    header.spawn_parameters =
        malloc(spawn_parameter_count * sizeof *header.spawn_parameters);
    if (header.spawn_parameters == NULL) goto oom;
    memcpy(header.spawn_parameters, spawn_parameters,
           spawn_parameter_count * sizeof *header.spawn_parameters);
    header.spawn_parameter_count = spawn_parameter_count;
  }
  header.skill = source->skill;
  {
    view map = map_name(source->map_requested_path);
    header.map = malloc(map.length + 1);
    if (header.map == NULL) goto oom;
    memcpy(header.map, map.text, map.length);
    header.map[map.length] = '\0';
  }
  header.time = source->time_seconds(source->ctx);
  header.light_styles = calloc(64, sizeof *header.light_styles);
  if (header.light_styles == NULL) goto oom;
  header.light_style_count = 64;
  for (size_t i = 0; i < 64; i++) {
    const char *pattern = source->light_style(source->ctx, i);
    if (pattern == NULL || pattern[0] == '\0') pattern = "m";
    header.light_styles[i] = dup_text(pattern);
    if (header.light_styles[i] == NULL) goto oom;
  }
  header.extension_text = dup_text(extension_text != NULL ? extension_text : "");
  if (header.extension_text == NULL) goto oom;
  header.extension_length = strlen(header.extension_text);
  if (!qa_capture_q1_quakec_save(
          &source->machine, header,
          (qa_q1_quakec_is_free){source, is_free_slot}, out))
    goto oom;
  return true;
oom:
  free(header.game_directories);
  free(header.comment);
  free(header.spawn_parameters);
  free(header.map);
  if (header.light_styles != NULL) {
    for (size_t i = 0; i < header.light_style_count; i++)
      free(header.light_styles[i]);
    free(header.light_styles);
  }
  free(header.extension_text);
  return false;
}

typedef struct restore_host {
  qa_quakec_source *source;
  const qa_q1_save_data *save;
  qa_q1_source_finish finish;
} restore_host;

static void host_begin(void *ctx, size_t count) {
  restore_host *host = ctx;
  qa_quakec_source *source = host->source;
  const qa_q1_save_data *save = host->save;
  for (size_t slot = 0; slot < source->entity_count(source->ctx); slot++) {
    qa_owned_actor actor;
    if (!source->slot_actor(source->ctx, slot, &actor)) continue;
    source->unlink_body(source->ctx, actor);
    if ((double)slot > source->reserved_client_slots)
      source->release_actor(source->ctx, actor);
  }
  source->set_entity_count(source->ctx, count);
  for (size_t slot = 0; slot < count; slot++) {
    if (slot >= save->entity_count) qa_error("Missing source edict");
    const qa_q1_text_record *pairs = &save->entities[slot];
    if (pairs->pair_count == 0) {
      qa_source_time now = {QA_SOURCE_TIME_SECONDS, save->time};
      source->clear_freed_slot(source->ctx, slot, now);
    } else {
      qa_owned_actor actor =
          source->bind_existing_slot(source->ctx, slot, "quakec:edict");
      source->initialize_slot(source->ctx, slot, actor);
    }
  }
}
static void host_entity(void *ctx, size_t slot, bool is_free) {
  restore_host *host = ctx;
  if (!is_free) host->source->link_slot(host->source->ctx, slot);
}
static void host_finish(void *ctx, qa_q1_save_header header) {
  restore_host *host = ctx;
  qa_quakec_source *source = host->source;
  for (size_t style = 0; style < header.light_style_count; style++)
    source->emit_lightstyle(source->ctx, source->owner_content, style,
                            header.light_styles[style]);
  host->finish.fn(host->finish.ctx, header);
}

bool qa_restore_q1_source_save(qa_quakec_source *source,
                               const qa_q1_save_data *save,
                               qa_q1_source_finish finish,
                               qa_q1_unknown_save_fields *out) {
  if (source->kind == NULL || strcmp(source->kind, "netquake") != 0 ||
      source->max_clients != 1 || source->mode == NULL ||
      strcmp(source->mode, "singleplayer") != 0 ||
      source->loading(source->ctx))
    qa_error("Original Quake restoration requires a precached singleplayer "
             "NetQuake candidate");
  view map = map_name(source->map_requested_path);
  const char *saved_map = save->map != NULL ? save->map : "";
  if (strlen(saved_map) != map.length ||
      memcmp(saved_map, map.text, map.length) != 0 ||
      save->skill != source->skill || save->entity_count < 2 ||
      save->entities[0].pair_count == 0 || save->entities[1].pair_count == 0)
    qa_error("Original Quake save does not match the candidate map, skill "
             "or player");
  source->checkpoint(source->ctx);
  restore_host host = {source, save, finish};
  qa_q1_quakec_save_host adapter = {&host, host_begin, host_entity, host_finish};
  return qa_restore_q1_quakec_save(&source->machine, save, adapter, out);
}
