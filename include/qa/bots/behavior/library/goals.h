/*
 * Port of src/bots/behavior/library/goals.ts (botlib/be_ai_goal.c).
 * C accommodations:
 *  - BotGoalLibrary: init selects defaults (owned BotMemory); the TS constructor
 *    options go through qa_bot_goal_library_configure. The struct address must stay
 *    stable. destroy releases C bookkeeping and the owned memory only.
 *  - GoalWorld, GoalWorldHost, GoalNavigation and SourcePickupGoals are fn+ctx
 *    records held by value, every leaf called with its own ctx.
 *  - ItemInfo is decoded by value from the configuration bytes.
 *  - loadItemConfig's thrown ScriptLanguageError becomes a NULL return with
 *    *error set to "path:line:column: message" (owned). A NULL error pointer
 *    means nothing catches it: the message is printed and the process aborts.
 *  - TS RetiredGoalSetup is thrown from inside script callbacks, so setup unwinds
 *    with longjmp and abandons the reader exactly as the TS throw does.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_GOALS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_GOALS_H
#include "qa/bots/behavior/library/bsp_entities.h"
#include "qa/bots/behavior/library/memory.h"
#include "qa/bots/behavior/library/script_sources.h"
#include "qa/bots/behavior/library/weights.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/preprocessor.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { QA_GOAL_FLAG_ITEM = 1, QA_GOAL_FLAG_ROAM = 2, QA_GOAL_FLAG_DROPPED = 4 };
enum {
  QA_GOAL_ERROR_NONE = 0,
  QA_GOAL_ERROR_CANNOT_LOAD_ITEM_WEIGHTS = 9,
  QA_GOAL_ERROR_CANNOT_LOAD_ITEM_CONFIG = 10
};
#define QA_MAX_GOAL_STATES 64
#define QA_MAX_GOAL_STACK 8
#define QA_MAX_AVOID_GOALS 256
#define QA_SOURCE_GOAL_NUMBER_MIN 0x40000000
#define QA_GOAL_BYTES 56
typedef struct qa_bot_goal {
  qa_vec3 origin;
  int64_t area;
  qa_vec3 mins, maxs;
  int64_t entity, number, flags, item_info;
} qa_bot_goal;
typedef struct qa_item_info_bots_a {
  char classname[32];
  char name[80];
  char model[80];
  int64_t model_index, type, index;
  float respawn_time;
  qa_vec3 mins, maxs;
  int64_t number;
} qa_item_info_bots_a;
typedef struct qa_item_config_bots_a qa_item_config_bots_a;
typedef enum qa_goal_severity {
  QA_GOAL_MESSAGE,
  QA_GOAL_WARNING,
  QA_GOAL_ERROR,
  QA_GOAL_FATAL
} qa_goal_severity;
typedef struct qa_goal_diagnostic {
  qa_goal_severity severity;
  char *message;
} qa_goal_diagnostic;
typedef void (*qa_goal_report_fn)(void *ctx, const qa_goal_diagnostic *diagnostic);
/** TS ItemConfigOptions. has_max_items false selects 256. */
typedef struct qa_item_config_options {
  qa_bot_memory *memory; /* NULL: the config owns a new BotMemory */
  bool has_max_items;
  double max_items;
  const qa_ui_script_preprocessor_options *preprocessor;
  qa_goal_report_fn report;
  void *report_ctx;
} qa_item_config_options;
bool qa_is_source_goal_number(double number);
bool qa_touching_goal(qa_vec3 origin, qa_bot_goal goal);
qa_item_config_bots_a *qa_load_item_config(const qa_bot_script_reader *resolver, const char *path,
                                           const qa_item_config_options *options, char **error);
const char *qa_item_config_path(const qa_item_config_bots_a *config);
size_t qa_item_config_count(const qa_item_config_bots_a *config);
void qa_item_config_item(const qa_item_config_bots_a *config, size_t index, qa_item_info_bots_a *out);
const qa_ui_script_diagnostic *qa_item_config_diagnostics(const qa_item_config_bots_a *config,
                                                          size_t *out_count);
/* TS ItemConfig.free(): releases the configuration bytes; the struct stays until destroy. */
void qa_item_config_free(qa_item_config_bots_a *config);
/* C struct release for configs obtained from qa_load_item_config. */
void qa_item_config_destroy(qa_item_config_bots_a *config);

typedef struct qa_goal_entity_info {
  int64_t type, model_index;
  qa_vec3 origin, last_visible_origin;
  double last_update_time;
} qa_goal_entity_info;
/** TS GoalWorldHost. bounds NULL is the TS null. trace returns its fraction. */
typedef struct qa_goal_world_host {
  void *ctx;
  double (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds, int64_t pass_entity,
                  int64_t mask);
  int64_t (*point_contents)(void *ctx, qa_vec3 point);
  int64_t (*next_entity)(void *ctx, int64_t after);
  qa_goal_entity_info (*entity_info)(void *ctx, int64_t entity);
} qa_goal_world_host;
typedef struct qa_goal_area_info {
  int64_t contents, reachable_area_count;
} qa_goal_area_info;
typedef struct qa_goal_drop_result {
  qa_vec3 origin;
  bool success;
} qa_goal_drop_result;
typedef struct qa_goal_reachable_position {
  int64_t area;
  qa_vec3 origin;
} qa_goal_reachable_position;
typedef struct qa_goal_route_request {
  int64_t area;
  qa_vec3 origin;
  int64_t goal_area, travel_flags;
} qa_goal_route_request;
typedef struct qa_goal_route_result {
  bool found;
  double travel_time;
} qa_goal_route_result;
/** TS GoalNavigation. */
typedef struct qa_goal_navigation {
  void *ctx;
  qa_goal_area_info (*area)(void *ctx, int64_t number);
  int64_t (*best_reachable_from_jump_pad_area)(void *ctx, qa_vec3 origin, qa_bounds bounds);
  qa_goal_drop_result (*drop_to_floor)(void *ctx, qa_vec3 origin, qa_bounds bounds);
  qa_goal_reachable_position (*best_reachable_area)(void *ctx, qa_vec3 origin, qa_bounds bounds);
  int64_t (*reachability_area)(void *ctx, qa_vec3 origin, int64_t client);
  qa_goal_route_result (*route)(void *ctx, const qa_goal_route_request *request);
} qa_goal_navigation;
/** TS SourcePickupGoal; name is borrowed for the callback's return. */
typedef struct qa_source_pickup_goal {
  qa_actor_id actor;
  int64_t entity;
  qa_vec3 origin;
  qa_bounds bounds;
  const char *name;
  double utility;
} qa_source_pickup_goal;
/** TS SourcePickupGoals. candidates returns an array borrowed for the call;
 * inspect false is TS null; owns_item_goal NULL is the absent member. */
typedef struct qa_source_pickup_goals {
  void *ctx;
  bool (*owns_item_goal)(void *ctx, int64_t client, int64_t entity);
  const qa_source_pickup_goal *(*candidates)(void *ctx, int64_t client, size_t *out_count);
  bool (*inspect)(void *ctx, int64_t client, qa_actor_id actor, qa_source_pickup_goal *out);
} qa_source_pickup_goals;
/** TS GoalWorld, copied by value into the library. */
typedef struct qa_goal_world {
  bool has_source_pickups;
  qa_source_pickup_goals source_pickups;
  qa_aas_bsp_entities *bsp_entities; /* borrowed; identity compared by rebind */
  bool has_navigation;
  qa_goal_navigation navigation;
  qa_goal_world_host host;
  int64_t (*point_area)(void *ctx, qa_vec3 origin);
  void *point_area_ctx;
} qa_goal_world;
/** TS GoalLibraryOptions; NULL/false is the TS undefined field. */
typedef struct qa_bot_goal_options {
  bool debug;
  qa_bot_memory *memory;
  const qa_bot_script_reader *resolver;
  qa_weight_config_store_bots_a *weight_store;
  void (*log_write)(void *ctx, const char *text);
  void *log_ctx;
  double (*clock)(void *ctx);
  void *clock_ctx;
  qa_bot_random random;
  double (*game_type)(void *ctx);
  void *game_type_ctx;
  double (*max_item_info_get)(void *ctx);
  void (*max_item_info_set)(void *ctx, double value);
  void *max_item_info_ctx;
  double (*max_level_items)(void *ctx);
  void *max_level_items_ctx;
  double (*dropped_weight)(void *ctx);
  void *dropped_weight_ctx;
  const qa_ui_script_preprocessor_options *preprocessor;
  qa_goal_report_fn report;
  void *report_ctx;
  bool (*developer)(void *ctx);
  void *developer_ctx;
} qa_bot_goal_options;
typedef enum qa_source_goal_status {
  QA_SOURCE_GOAL_NATIVE,
  QA_SOURCE_GOAL_AVAILABLE,
  QA_SOURCE_GOAL_UNAVAILABLE
} qa_source_goal_status;
/** TS `string | ((candidate) => boolean)` goal name query. */
typedef struct qa_goal_name_query {
  const char *text;
  bool (*matches)(void *ctx, const char *candidate);
  void *ctx;
} qa_goal_name_query;
static inline qa_goal_name_query qa_goal_name_text(const char *text) {
  qa_goal_name_query query = {text, NULL, NULL};
  return query;
}
typedef struct qa_avoid_goal_dump {
  int64_t number;
  float remaining;
} qa_avoid_goal_dump;
/** TS `BotGoal | Uint8Array` produced lazily by pushGoal's thunk. Fill goal and
 * return NULL, or return a borrowed record of *out_length bytes. */
typedef const uint8_t *(*qa_goal_push_fn)(void *ctx, qa_bot_goal *goal, size_t *out_length);
typedef struct qa_goal_state_cell {
  bool used;
  uint64_t serial; /* TS Map insertion order and object identity */
  qa_bot_memory_allocation allocation;
} qa_goal_state_cell;
typedef struct qa_goal_config_reference {
  int64_t id;
  qa_weight_config_bots_a *config;
} qa_goal_config_reference;
typedef struct qa_goal_index_reference {
  int64_t id;
  qa_bot_memory_allocation allocation;
} qa_goal_index_reference;
typedef struct qa_goal_source_binding {
  int64_t number;
  uint64_t serial; /* TS object identity */
  qa_actor_id actor;
  char *name;
  qa_bot_goal goal;
} qa_goal_source_binding;
typedef struct qa_goal_info_entity {
  bool live, is_camp;
  qa_bot_memory_allocation allocation;
} qa_goal_info_entity;
typedef struct qa_bot_goal_library {
  qa_bot_goal_options options;
  bool has_resolver;
  qa_bot_script_reader resolver;
  qa_ui_script_preprocessor_options preprocessor;
  qa_bot_memory owned_memory;
  qa_bot_memory *memory;
  int64_t next_id;
  qa_goal_config_reference *config_references;
  size_t config_reference_count, config_reference_capacity;
  qa_goal_index_reference *index_references;
  size_t index_reference_count, index_reference_capacity;
  qa_goal_state_cell states[QA_MAX_GOAL_STATES + 1];
  uint64_t next_serial;
  qa_goal_diagnostic *reported;
  size_t reported_count, reported_capacity;
  int64_t generation, setup_revision, map_revision;
  qa_item_config_bots_a *config;
  qa_item_config_bots_a **configs; /* every config struct, released by destroy */
  size_t config_count, config_capacity;
  int64_t configured_game_type;
  bool has_world;
  qa_goal_world world;
  qa_goal_source_binding *source_goals; /* TS Map in insertion order */
  size_t source_goal_count, source_goal_capacity;
  int64_t next_source_goal;
  bool has_level_item_heap;
  qa_bot_memory_allocation level_item_heap;
  uint32_t level_head, free_level_head, location_head, camp_head;
  int32_t initial_item_count;
  qa_goal_info_entity *info_entities; /* indexed by pointer ID */
  size_t info_entity_capacity;
  int64_t next_info_pointer;
  void *setup_frame; /* innermost running setup (private) */
} qa_bot_goal_library;
void qa_bot_goal_library_init(qa_bot_goal_library *l);
void qa_bot_goal_library_configure(qa_bot_goal_library *l, const qa_bot_goal_options *options);
void qa_bot_goal_library_destroy(qa_bot_goal_library *l);
const qa_goal_diagnostic *qa_bot_goal_diagnostics(const qa_bot_goal_library *l, size_t *out_count);
qa_item_config_bots_a *qa_bot_goal_item_config(const qa_bot_goal_library *l);
int64_t qa_bot_goal_game_type(const qa_bot_goal_library *l);
/* path NULL is the TS default "items.c". */
int64_t qa_bot_goal_setup(qa_bot_goal_library *l, const char *path);
int64_t qa_bot_goal_alloc_goal_state(qa_bot_goal_library *l, int64_t client);
void qa_bot_goal_free_goal_state(qa_bot_goal_library *l, int64_t handle);
int64_t qa_bot_goal_load_item_weights(qa_bot_goal_library *l, int64_t handle, const char *path);
int64_t qa_bot_goal_load_item_weights_fn(qa_bot_goal_library *l, int64_t handle,
                                         const char *(*read)(void *ctx), void *ctx);
void qa_bot_goal_free_item_weights(qa_bot_goal_library *l, int64_t handle);
qa_string_list qa_bot_goal_interbreed_goal_fuzzy_logic(qa_bot_goal_library *l, int64_t parent1,
                                                       int64_t parent2, int64_t child);
/* TS ignores _range and be_ai_goal.c never writes the file for save. */
void qa_bot_goal_mutate_goal_fuzzy_logic(qa_bot_goal_library *l, int64_t handle, double range);
void qa_bot_goal_save_goal_fuzzy_logic(qa_bot_goal_library *l, int64_t handle, const char *filename);
void qa_bot_goal_push_goal(qa_bot_goal_library *l, int64_t handle, const qa_bot_goal *goal);
void qa_bot_goal_push_goal_bytes(qa_bot_goal_library *l, int64_t handle, const uint8_t *bytes, size_t length);
void qa_bot_goal_push_goal_fn(qa_bot_goal_library *l, int64_t handle, qa_goal_push_fn produce, void *ctx);
void qa_bot_goal_pop_goal(qa_bot_goal_library *l, int64_t handle);
void qa_bot_goal_empty_goal_stack(qa_bot_goal_library *l, int64_t handle);
bool qa_bot_goal_get_top_goal(qa_bot_goal_library *l, int64_t handle, qa_bot_goal *out);
bool qa_bot_goal_get_second_goal(qa_bot_goal_library *l, int64_t handle, qa_bot_goal *out);
/* Live QA_GOAL_BYTES record, or NULL. */
uint8_t *qa_bot_goal_get_top_goal_bytes(qa_bot_goal_library *l, int64_t handle);
uint8_t *qa_bot_goal_get_second_goal_bytes(qa_bot_goal_library *l, int64_t handle);
/* Returns the stack depth; up to capacity goals land in out (out may be NULL). */
size_t qa_bot_goal_dump_goal_stack(qa_bot_goal_library *l, int64_t handle, qa_bot_goal *out, size_t capacity);
void qa_bot_goal_reset_avoid_goals(qa_bot_goal_library *l, int64_t handle);
void qa_bot_goal_reset_goal_state(qa_bot_goal_library *l, int64_t handle);
void qa_bot_goal_set_avoid_goal_time(qa_bot_goal_library *l, int64_t handle, int64_t number, double duration);
void qa_bot_goal_remove_from_avoid_goals(qa_bot_goal_library *l, int64_t handle, int64_t number);
float qa_bot_goal_avoid_goal_time(qa_bot_goal_library *l, int64_t handle, int64_t number);
size_t qa_bot_goal_dump_avoid_goals(qa_bot_goal_library *l, int64_t handle, qa_avoid_goal_dump *out, size_t capacity);
void qa_bot_goal_shutdown(qa_bot_goal_library *l);
void qa_bot_goal_free_info_entities(qa_bot_goal_library *l);
void qa_bot_goal_init_info_entities(qa_bot_goal_library *l, const qa_goal_world *world);
void qa_bot_goal_rebind_world(qa_bot_goal_library *l, const qa_goal_world *world);
/* world NULL is the TS default: the retained world. */
void qa_bot_goal_init_level_items(qa_bot_goal_library *l, const qa_goal_world *world);
/* Borrowed; "" when no goal has this number. */
const char *qa_bot_goal_goal_name(qa_bot_goal_library *l, int64_t number);
void qa_bot_goal_write_goal_name(qa_bot_goal_library *l, int64_t number,
                                 void (*found)(void *ctx, const char *name), void (*missing)(void *ctx),
                                 void *ctx);
/* prior NULL is the TS blank goal; false is TS null. */
bool qa_bot_goal_get_level_item_goal(qa_bot_goal_library *l, int64_t index, qa_goal_name_query name,
                                     const qa_bot_goal *prior, qa_bot_goal *out);
bool qa_bot_goal_get_map_location_goal(qa_bot_goal_library *l, qa_goal_name_query name,
                                       const qa_bot_goal *prior, qa_bot_goal *out);
bool qa_bot_goal_get_next_camp_spot_goal(qa_bot_goal_library *l, int64_t index, const qa_bot_goal *prior,
                                         int64_t *out_next, qa_bot_goal *out);
void qa_bot_goal_find_entity_for_level_item(qa_bot_goal_library *l, int64_t number);
void qa_bot_goal_update_entity_items(qa_bot_goal_library *l);
bool qa_bot_goal_choose_ltg_item(qa_bot_goal_library *l, int64_t handle, qa_vec3 origin,
                                 qa_weight_inventory inventory, int64_t travel_flags);
/* long_term_goal NULL is the TS null. */
bool qa_bot_goal_choose_nbg_item(qa_bot_goal_library *l, int64_t handle, qa_vec3 origin,
                                 qa_weight_inventory inventory, int64_t travel_flags,
                                 const qa_bot_goal *long_term_goal, double max_time);
bool qa_bot_goal_item_goal_in_vis_but_not_visible(qa_bot_goal_library *l, int64_t viewer, qa_vec3 eye,
                                                  qa_vec3 view_angles, const qa_bot_goal *goal);
qa_source_goal_status qa_bot_goal_source_goal_status(qa_bot_goal_library *l, int64_t client,
                                                     const qa_bot_goal *goal);
qa_save_value *qa_bot_goal_checkpoint(qa_bot_goal_library *l, qa_bot_memory_capture *memory);
/** TS restore's actor callback. */
typedef qa_actor_id (*qa_goal_restore_actor_fn)(void *ctx, qa_saved_actor_id saved);
void qa_bot_goal_restore(qa_bot_goal_library *l, const qa_save_value *value, qa_bot_memory_restored *memory,
                         const qa_goal_world *world, qa_goal_restore_actor_fn actor, void *actor_ctx);
#endif
