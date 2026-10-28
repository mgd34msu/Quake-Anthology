/* Port of src/app/bootstrap/component-scene.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/component_scene.h"
#include <stdlib.h>
#include <string.h>
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
qa_app_component_scene_context qa_app_select_component_scene(qa_app_component_scene_host host,
    const qa_app_component_scene_source *source, qa_actor_id viewer, double leaf_count) {
  (void)leaf_count; /* consumed by the host visibility selection */
  const qa_app_component_scene_client *player = NULL;
  for (size_t i = 0; i < source->client_count; i++)
    if (qa_same_actor(source->clients[i].actor, viewer)) player = &source->clients[i];
  if (!player) qa_range_error("Component snapshot has no admitted viewing player");
  qa_app_component_scene_context out = {0};
  uint8_t visible_mask[32] = {0};
  size_t mask_length = 0, visible_count = 0;
  out.visible_entities = host.select_visible(host.ctx, player->slot, source, &visible_count, visible_mask, &mask_length);
  out.visible_entity_count = visible_count;
  if (mask_length > 32) mask_length = 32;
  memcpy(out.area_mask, visible_mask, mask_length); /* new Uint8Array(32).set(mask) */
  out.revision = source->revision;
  out.game_state = source->game_state;
  out.game_state_revision = source->game_state_revision;
  out.server_time = source->server_time;
  out.player_state = player->state;
  out.server_command_sequence = source->command_count ? source->commands[source->command_count - 1].sequence : 0;
  out.actors = calloc(source->entity_count ? source->entity_count : 1, sizeof *out.actors);
  out.actor_count = source->entity_count;
  for (size_t i = 0; i < source->entity_count; i++)
    out.actors[i] = (qa_app_component_scene_context_actor){source->entities[i].actor, source->entities[i].number, source->entities[i].owned};
  out.commands = calloc(source->command_count ? source->command_count : 1, sizeof *out.commands);
  out.command_count = source->command_count;
  for (size_t i = 0; i < source->command_count; i++) {
    const qa_app_component_scene_command *command = &source->commands[i];
    out.commands[i].sequence = command->sequence;
    if (command->has_recipient && !qa_same_actor(command->recipient, viewer)) continue;
    qa_command_tokens tokens = {0};
    if (qa_tokenize_command(command->text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens)) {
      out.commands[i].arguments = tokens.argv;
      out.commands[i].argument_count = tokens.argc;
      free(tokens.args_text);
    }
  }
  return out;
}
void qa_app_component_scene_context_destroy(qa_app_component_scene_context *context) {
  if (!context) return;
  for (size_t i = 0; i < context->command_count; i++) {
    for (size_t j = 0; j < context->commands[i].argument_count; j++) free(context->commands[i].arguments[j]);
    free(context->commands[i].arguments);
  }
  free(context->actors);
  free(context->commands);
  *context = (qa_app_component_scene_context){0};
}
