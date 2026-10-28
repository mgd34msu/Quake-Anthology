/* Port of src/app/bootstrap/component-scene.ts. Visibility selection
 * (q3-client/visibility) and scene publications are host/landed
 * types; admitted-player gating, area-mask copy and per-recipient
 * command tokenization are kept here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_COMPONENT_SCENE_H
#define QA_APP_BOOTSTRAP_COMPONENT_SCENE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/identity.h"
/* QvmModScenePublication rows read here. */
typedef struct qa_app_component_scene_client {
  qa_actor_id actor; double slot;
  const void *state; /* borrowed player state */
} qa_app_component_scene_client;
typedef struct qa_app_component_scene_entity {
  qa_actor_id actor; double number; bool owned;
  const void *bounds; /* borrowed */
} qa_app_component_scene_entity;
typedef struct qa_app_component_scene_command {
  double sequence;
  bool has_recipient; qa_actor_id recipient;
  const char *text;
} qa_app_component_scene_command;
typedef struct qa_app_component_scene_source {
  double revision, game_state_revision, server_time;
  const void *game_state;
  const qa_app_component_scene_client *clients; size_t client_count;
  const qa_app_component_scene_entity *entities; size_t entity_count;
  const qa_app_component_scene_command *commands; size_t command_count;
} qa_app_component_scene_source;
/* Command arguments owned by the context; free with _destroy. */
typedef struct qa_app_component_scene_context_command {
  double sequence;
  char **arguments; size_t argument_count;
} qa_app_component_scene_context_command;
typedef struct qa_app_component_scene_context_actor { qa_actor_id actor; double slot; bool owned; } qa_app_component_scene_context_actor;
typedef struct qa_app_component_scene_context {
  double revision, game_state_revision, server_time, server_command_sequence;
  const void *game_state, *player_state;
  uint8_t area_mask[32];
  const void *visible_entities; size_t visible_entity_count;
  qa_app_component_scene_context_actor *actors; size_t actor_count;
  qa_app_component_scene_context_command *commands; size_t command_count;
} qa_app_component_scene_context;
typedef struct qa_app_component_scene_host {
  void *ctx;
  /* selectApplicationQ3Snapshot: fills visible entities + area mask. */
  const void *(*select_visible)(void *ctx, double client_number, const qa_app_component_scene_source *source,
                                size_t *out_count, uint8_t out_area_mask[32], size_t *out_area_mask_length);
  void (*print)(void *ctx, const char *text);
} qa_app_component_scene_host;
/* Aborts "Component snapshot has no admitted viewing player". */
qa_app_component_scene_context qa_app_select_component_scene(qa_app_component_scene_host host,
    const qa_app_component_scene_source *source, qa_actor_id viewer, double leaf_count);
void qa_app_component_scene_context_destroy(qa_app_component_scene_context *context);
#endif
