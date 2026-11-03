/* Port of src/app/bootstrap/component-scene.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/component_scene.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q3_client/visibility.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void copy_game_state(const qa_q3_source_game_state_record *source, qa_q3_source_game_state_record *out) {
  out->string_offsets = xcalloc(1024, sizeof *out->string_offsets);
  out->string_data = xcalloc(16000, 1);
  if (source->string_offsets != NULL) memcpy(out->string_offsets, source->string_offsets, 1024 * sizeof *out->string_offsets);
  if (source->string_data != NULL) memcpy(out->string_data, source->string_data, 16000);
  out->data_count = source->data_count;
}
/* The player state with its four slot arrays in one owned block based at stats. */
static qa_q3_player_state copy_player_state(const qa_q3_player_state *source) {
  qa_q3_player_state out = *source;
  const qa_double_view *views[4] = {&source->stats, &source->persistent, &source->powerups, &source->ammo};
  size_t total = 0;
  for (size_t i = 0; i < 4; i++) total += views[i]->count;
  double *block = xcalloc(total, sizeof *block);
  qa_double_view *targets[4] = {&out.stats, &out.persistent, &out.powerups, &out.ammo};
  size_t offset = 0;
  for (size_t i = 0; i < 4; i++) {
    if (views[i]->count > 0) memcpy(block + offset, views[i]->values, views[i]->count * sizeof *block);
    targets[i]->values = block + offset; targets[i]->count = views[i]->count;
    offset += views[i]->count;
  }
  return out;
}
/* The argv pointers and their text in one block: qa_qvm_scene_context_free
 * releases a command with one free(arguments). */
static const char *const *argument_block(const char *const *argv, size_t argc) {
  size_t text = 0;
  for (size_t i = 0; i < argc; i++) text += strlen(argv[i]) + 1;
  char **block = xcalloc(1, (argc == 0 ? 1 : argc) * sizeof(char *) + text);
  char *cursor = (char *)(block + (argc == 0 ? 1 : argc));
  for (size_t i = 0; i < argc; i++) {
    size_t length = strlen(argv[i]) + 1;
    memcpy(cursor, argv[i], length);
    block[i] = cursor; cursor += length;
  }
  return (const char *const *)block;
}
/* slot => bounds.get(slot) ?? null over the publication rows. */
static bool publication_bounds(void *ctx, double number, qa_bounds *out) {
  const qa_qvm_mod_scene_publication *source = ctx;
  for (size_t r = source->entity_count; r > 0; r--)
    if (source->entities[r - 1].state->number == number) { *out = source->entities[r - 1].bounds; return true; }
  return false;
}
void qa_app_select_component_scene(const qa_qvm_mod_scene_publication *source, qa_actor_id viewer,
    qa_shared_scene_queries *queries, double leaf_count, void (*print)(void *ctx, const char *text), void *print_ctx,
    qa_qvm_scene_context *out) {
  const qa_qvm_scene_client *player = NULL;
  for (size_t i = 0; i < source->client_count && player == NULL; i++)
    if (qa_same_actor(source->clients[i].actor, viewer)) player = &source->clients[i];
  if (player == NULL) qa_raise("Error", 0, 0, "Component snapshot has no admitted viewing player");
  /* TS new Map(entities.map(row => [number, bounds])): the last row of a number wins. */
  qa_app_q3_snapshot_source_entity *rows = xcalloc(source->entity_count, sizeof *rows);
  qa_q3_entity_state_record *records = xcalloc(source->entity_count, sizeof *records);
  for (size_t i = 0; i < source->entity_count; i++) {
    qa_q3_entity_state_init(&records[i]);
    records[i].number = (int32_t)source->entities[i].state->number;
    rows[i].state = &records[i]; rows[i].linked = source->entities[i].linked;
    rows[i].server_flags = source->entities[i].server_flags; rows[i].single_client = source->entities[i].single_client;
  }
  qa_q3_player_state_record selector;
  memset(&selector, 0, sizeof selector);
  selector.client_num = (int32_t)player->slot; selector.origin = player->state.origin;
  selector.viewheight = (int32_t)player->state.view_height;
  qa_app_q3_snapshot_selection *selection = qa_app_select_application_q3_snapshot(&selector, rows, source->entity_count,
      queries, publication_bounds, (void *)source, leaf_count, print, print_ctx);
  if (selection == NULL) qa_range_error("Out of memory");
  const qa_q3_visible_entities *visible = qa_app_q3_snapshot_selection_visible(selection);
  memset(out, 0, sizeof *out);
  out->revision = (int64_t)source->revision;
  copy_game_state(source->game_state, &out->game_state);
  out->game_state_revision = (int64_t)source->game_state_revision;
  out->snapshot.server_time = source->server_time;
  out->snapshot.flags = 0;
  uint8_t *area_mask = xcalloc(32, 1);
  memcpy(area_mask, visible->area_mask, visible->area_mask_len < 32 ? visible->area_mask_len : 32);
  out->snapshot.area_mask = area_mask; out->snapshot.area_mask_length = 32;
  out->snapshot.player_state = copy_player_state(&player->state);
  qa_qvm_entity_state_fields *entities = xcalloc(visible->entity_count, sizeof *entities);
  for (size_t i = 0; i < visible->entity_count; i++) {
    double number = visible->entities[i]->number;
    bool found = false;
    for (size_t r = source->entity_count; r > 0 && !found; r--)
      if (source->entities[r - 1].state->number == number) { entities[i] = *source->entities[r - 1].state; found = true; }
    if (!found) entities[i].number = number;
  }
  out->snapshot.entities = entities; out->snapshot.entity_count = visible->entity_count;
  out->snapshot.server_command_sequence = source->command_count == 0 ? 0 : source->commands[source->command_count - 1].sequence;
  qa_app_q3_snapshot_selection_destroy(selection);
  free(records); free(rows);
  out->actors = xcalloc(source->entity_count, sizeof *out->actors);
  out->actor_count = source->entity_count;
  for (size_t i = 0; i < source->entity_count; i++) {
    out->actors[i].actor = source->entities[i].actor;
    out->actors[i].slot = (int64_t)source->entities[i].state->number;
    out->actors[i].owned = source->entities[i].owned;
  }
  out->commands = xcalloc(source->command_count, sizeof *out->commands);
  out->command_count = source->command_count;
  for (size_t i = 0; i < source->command_count; i++) {
    const qa_qvm_scene_command *command = &source->commands[i];
    out->commands[i].sequence = (int64_t)command->sequence;
    if (command->recipient != NULL && !qa_same_actor(*command->recipient, viewer)) {
      out->commands[i].arguments = argument_block(NULL, 0); continue;
    }
    qa_command_tokens tokens;
    memset(&tokens, 0, sizeof tokens);
    if (!qa_tokenize_command(command->text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens)) qa_range_error("Out of memory");
    out->commands[i].arguments = argument_block((const char *const *)tokens.argv, tokens.argc);
    out->commands[i].argument_count = tokens.argc;
    qa_command_tokens_destroy(&tokens);
  }
}
void qa_app_copy_component_scene(const qa_qvm_scene_context *source, qa_qvm_scene_context *out) {
  memset(out, 0, sizeof *out);
  out->revision = source->revision;
  copy_game_state(&source->game_state, &out->game_state);
  out->game_state_revision = source->game_state_revision;
  out->snapshot = source->snapshot;
  uint8_t *area_mask = xcalloc(source->snapshot.area_mask_length, 1);
  if (source->snapshot.area_mask_length > 0) memcpy(area_mask, source->snapshot.area_mask, source->snapshot.area_mask_length);
  out->snapshot.area_mask = area_mask;
  out->snapshot.player_state = copy_player_state(&source->snapshot.player_state);
  qa_qvm_entity_state_fields *entities = xcalloc(source->snapshot.entity_count, sizeof *entities);
  if (source->snapshot.entity_count > 0) memcpy(entities, source->snapshot.entities, source->snapshot.entity_count * sizeof *entities);
  out->snapshot.entities = entities;
  out->actors = xcalloc(source->actor_count, sizeof *out->actors);
  if (source->actor_count > 0) memcpy(out->actors, source->actors, source->actor_count * sizeof *out->actors);
  out->actor_count = source->actor_count;
  out->commands = xcalloc(source->command_count, sizeof *out->commands);
  out->command_count = source->command_count;
  for (size_t i = 0; i < source->command_count; i++) {
    out->commands[i].sequence = source->commands[i].sequence;
    out->commands[i].arguments = argument_block(source->commands[i].arguments, source->commands[i].argument_count);
    out->commands[i].argument_count = source->commands[i].argument_count;
  }
  if (source->baseline != NULL) {
    out->baseline = xcalloc(1, sizeof *out->baseline);
    qa_app_copy_component_scene(source->baseline, out->baseline);
  }
}
