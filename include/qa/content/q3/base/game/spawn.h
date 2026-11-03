/* Port of src/content/q3/base/game/spawn.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_SPAWN_H
#define QA_CONTENT_Q3_BASE_GAME_SPAWN_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/memory.h"
#include "qa/content/q3/base/game/state.h"
enum { QA_Q3_MAX_SPAWN_VARS = 64, QA_Q3_MAX_SPAWN_VARS_CHARS = 4096 };
typedef struct qa_q3_spawn_pair { char *key, *value; } qa_q3_spawn_pair;
typedef struct qa_q3_spawn_variables { qa_q3_spawn_pair *entries; size_t count, character_count; } qa_q3_spawn_variables;
/* Takes ownership copies of pairs (strings duplicated). */
void qa_q3_spawn_variables_init(qa_q3_spawn_variables *vars, const qa_q3_spawn_pair *entries, size_t count);
void qa_q3_spawn_variables_destroy(qa_q3_spawn_variables *vars);
typedef struct qa_q3_spawn_value_string { bool present; const char *value; } qa_q3_spawn_value_string;
typedef struct qa_q3_spawn_value_number { bool present; double value; } qa_q3_spawn_value_number;
typedef struct qa_q3_spawn_value_vector { bool present; qa_vec3 value; } qa_q3_spawn_value_vector;
qa_q3_spawn_value_string qa_q3_spawn_variables_string(const qa_q3_spawn_variables *vars, const char *key, const char *default_value);
qa_q3_spawn_value_number qa_q3_spawn_variables_int(const qa_q3_spawn_variables *vars, const char *key, const char *default_value);
qa_q3_spawn_value_number qa_q3_spawn_variables_float(const qa_q3_spawn_variables *vars, const char *key, const char *default_value);
qa_q3_spawn_value_vector qa_q3_spawn_variables_vector(const qa_q3_spawn_variables *vars, const char *key, const char *default_value);
/* Malloc'd result (NULL=OOM); TS newSpawnString via GameMemory. */
char *qa_q3_new_spawn_string(const char *value, qa_q3_game_memory *memory);
typedef struct qa_q3_spawn_parser { const char *text; const char *name; size_t offset, line, column; } qa_q3_spawn_parser;
void qa_q3_spawn_parser_init(qa_q3_spawn_parser *parser, const char *text, const char *name);
/* Returns false at EOF (TS null); parse errors abort. Caller destroys out. */
bool qa_q3_spawn_parser_next(qa_q3_spawn_parser *parser, qa_q3_spawn_variables *out);
bool qa_q3_parse_spawn_field(const char *key, const char *value, qa_q3_game_entity *entity, qa_q3_game_memory *memory);
typedef void (*qa_q3_spawn_handler_fn)(void *ctx, qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables);
typedef struct qa_q3_spawn_handler_entry { const char *classname; qa_q3_spawn_handler_fn fn; void *ctx; } qa_q3_spawn_handler_entry;
typedef struct qa_q3_spawn_context {
  qa_q3_entity_pool *pool; qa_q3_game_memory *memory; qa_q3_base_product product; double game_type;
  const qa_q3_spawn_handler_entry *handlers; size_t handler_count;
  void *ctx; void (*spawn_item)(void *ctx, qa_q3_game_entity *entity, const qa_q3_base_item_definition *item, const qa_q3_spawn_variables *variables);
  void (*warn)(void *ctx, const char *message);
} qa_q3_spawn_context;
typedef enum qa_q3_spawn_filter { QA_Q3_SPAWN_NOTSINGLE, QA_Q3_SPAWN_NOTTEAM, QA_Q3_SPAWN_NOTFREE, QA_Q3_SPAWN_NOTTA, QA_Q3_SPAWN_NOTQ3A, QA_Q3_SPAWN_GAMETYPE } qa_q3_spawn_filter;
const char *qa_q3_spawn_filter_name(qa_q3_spawn_filter filter);
typedef enum qa_q3_spawn_outcome_kind { QA_Q3_SPAWN_DISPATCHED, QA_Q3_SPAWN_FILTERED, QA_Q3_SPAWN_UNKNOWN } qa_q3_spawn_outcome_kind;
typedef enum qa_q3_spawn_route { QA_Q3_ROUTE_ITEM, QA_Q3_ROUTE_HANDLER } qa_q3_spawn_route;
typedef struct qa_q3_spawn_outcome {
  qa_q3_spawn_outcome_kind kind; qa_q3_spawn_route route; qa_q3_game_entity *entity; double slot;
  char *classname; /* owned copy; NULL when the entity has none or the spawn was filtered */
  qa_q3_spawn_filter reason;
} qa_q3_spawn_outcome;
/* The outcome outlives the entity, which an unknown spawn has already freed. */
qa_q3_spawn_outcome qa_q3_spawn_entity(const qa_q3_spawn_variables *variables, qa_q3_spawn_context *context);
void qa_q3_spawn_outcome_destroy(qa_q3_spawn_outcome *outcome);
typedef struct qa_q3_worldspawn_context {
  qa_q3_entity_pool *pool; double start_time; const char *motd; double restarted, do_warmup, warmup_time;
  void *ctx; void (*set_configstring)(void *ctx, double index, const char *value);
  void (*set_cvar)(void *ctx, const char *name, const char *value); void (*log)(void *ctx, const char *message);
} qa_q3_worldspawn_context;
void qa_q3_spawn_world(const qa_q3_spawn_variables *variables, qa_q3_worldspawn_context *context);
typedef struct qa_q3_spawn_map_context { qa_q3_spawn_context spawn; qa_q3_worldspawn_context world; } qa_q3_spawn_map_context;
typedef struct qa_q3_spawn_report { qa_q3_spawn_variables world_variables; qa_q3_spawn_outcome *outcomes; size_t outcome_count; } qa_q3_spawn_report;
void qa_q3_spawn_report_destroy(qa_q3_spawn_report *report);
/* source NULL = "<spawn entities>". */
qa_q3_spawn_report qa_q3_spawn_entities(const char *text, qa_q3_spawn_map_context *context, const char *source);
#endif
