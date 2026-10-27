/* Port of src/content/q3/base/game/utilities.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_UTILITIES_H
#define QA_CONTENT_Q3_BASE_GAME_UTILITIES_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/state.h"
/* TS TemporaryVector: fround-staging accessors. */
typedef struct qa_q3_temporary_vector { double x, y, z; } qa_q3_temporary_vector;
void qa_q3_temporary_vector_set(qa_q3_temporary_vector *v, double x, double y, double z);
typedef struct qa_q3_game_utility_scratch {
  qa_q3_temporary_vector vectors[8]; uint8_t string_bytes[8 * 32]; int vector_index, string_index;
  void *print_ctx; void (*print)(void *ctx, const char *message);
} qa_q3_game_utility_scratch;
void qa_q3_game_utility_scratch_init(qa_q3_game_utility_scratch *s, void *ctx, void (*print)(void *ctx, const char *message));
qa_q3_temporary_vector *qa_q3_game_utility_scratch_tv(qa_q3_game_utility_scratch *s, double x, double y, double z);
/* Returns ring buffer (32 bytes) holding vtos string. */
uint8_t *qa_q3_game_utility_scratch_vtos(qa_q3_game_utility_scratch *s, qa_vec3 vector);
/* Mirror of ../bot-debug.ts BotDebugPolygons.create (TS owner unported). */
typedef struct qa_q3_bot_debug_polygons_mirror {
  void *ctx; double (*create)(void *ctx, double color, double count, const qa_vec3 *points);
} qa_q3_bot_debug_polygons_mirror;
double qa_q3_debug_line(qa_vec3 start, qa_vec3 end, double color, qa_q3_bot_debug_polygons_mirror polygons);
typedef struct qa_q3_config_string_store {
  void *ctx; const char *(*get)(void *ctx, double index); void (*set)(void *ctx, double index, const char *value);
} qa_q3_config_string_store;
typedef struct qa_q3_config_string_registry { qa_q3_config_string_store store; } qa_q3_config_string_registry;
void qa_q3_config_string_registry_init(qa_q3_config_string_registry *r, qa_q3_config_string_store store);
double qa_q3_config_string_registry_find(qa_q3_config_string_registry *r, const char *name, double start, double maximum, bool create);
double qa_q3_config_string_registry_model_index(qa_q3_config_string_registry *r, const char *name);
double qa_q3_config_string_registry_sound_index(qa_q3_config_string_registry *r, const char *name);
/* TS EntityStringField keys. */
typedef enum qa_q3_entity_string_field { QA_Q3_FIELD_CLASSNAME, QA_Q3_FIELD_MODEL, QA_Q3_FIELD_MODEL2, QA_Q3_FIELD_MESSAGE,
  QA_Q3_FIELD_TARGET, QA_Q3_FIELD_TARGETNAME, QA_Q3_FIELD_TEAM, QA_Q3_FIELD_TARGET_SHADER_NAME, QA_Q3_FIELD_TARGET_SHADER_NEW_NAME } qa_q3_entity_string_field;
const char *qa_q3_entity_string_field_get(qa_q3_game_entity *entity, qa_q3_entity_string_field field);
qa_q3_game_entity *qa_q3_find_entity(qa_q3_entity_pool *pool, qa_q3_game_entity *after, qa_q3_entity_string_field field, const char *match);
typedef struct qa_q3_target_selection_context {
  qa_q3_entity_pool *pool; void *ctx; double (*random_int)(void *ctx); void (*warn)(void *ctx, const char *message);
} qa_q3_target_selection_context;
qa_q3_game_entity *qa_q3_pick_target(qa_q3_target_selection_context context, const char *target_name);
typedef struct qa_q3_target_use_context {
  qa_q3_entity_pool *pool; double time; void *ctx;
  void (*remap_shader)(void *ctx, const char *old_name, const char *new_name, double time_seconds);
  void (*warn)(void *ctx, const char *message);
} qa_q3_target_use_context;
void qa_q3_use_targets(qa_q3_target_use_context context, qa_q3_game_entity *entity, const qa_q3_use_participant *activator);
typedef void (*qa_q3_team_command_send_fn)(void *ctx, double client_num, const char *command);
void qa_q3_team_command(qa_q3_entity_pool *pool, double team, const char *command, qa_q3_team_command_send_fn send, void *send_ctx);
typedef struct qa_q3_move_direction_result { qa_vec3 direction, angles; } qa_q3_move_direction_result;
qa_q3_move_direction_result qa_q3_move_direction(qa_vec3 angles);
#endif
