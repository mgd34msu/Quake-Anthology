/* Port of src/world/collision/q3/map-loader.ts. Loaded world/map are
 * loader-owned until clear/destroy. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_MAP_LOADER_H
#define QA_WORLD_COLLISION_Q3_MAP_LOADER_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/world/collision/q3/allocation.h"
#include "qa/world/collision/q3/counters.h"
#include "qa/world/collision/q3/map_resource.h"
#include "qa/world/collision/q3/world.h"
typedef struct qa_retained_collision_file { uint8_t *bytes; size_t byte_length; uint8_t *terminated_bytes; size_t terminated_length; } qa_retained_collision_file;
typedef struct qa_retained_file_reader {
  void *ctx;
  bool (*read_file_retained_sync)(void *ctx, const char *name, qa_retained_collision_file *out);
  void (*free_file)(void *ctx, qa_retained_collision_file file);
} qa_retained_file_reader;
typedef struct qa_collision_map_loader_options {
  void *ctx;
  qa_retained_file_reader (*files)(void *ctx);
  qa_hunk_accounting_profile (*memory)(void *ctx);
  qa_collision_world_profile debug;
  qa_collision_counters *counters;
  void (*developer_print)(void *ctx, const char *text);
} qa_collision_map_loader_options;
typedef struct qa_loaded_collision_map {
  qa_collision_map_data *map; qa_collision_world *world; double checksum;
} qa_loaded_collision_map;
typedef struct qa_collision_map_loader qa_collision_map_loader;
struct qa_collision_map_loader {
  qa_collision_map_loader_options options;
  char *name; qa_loaded_collision_map current; bool has_current;
  qa_collision_map_resource *resource; qa_q3_collision_box_model box_model;
  qa_collision_world world_storage;
};
void qa_collision_map_loader_init(qa_collision_map_loader *l, qa_collision_map_loader_options options);
void qa_collision_map_loader_destroy(qa_collision_map_loader *l);
qa_loaded_collision_map qa_collision_map_loader_load(qa_collision_map_loader *l, const char *name, bool client_load);
void qa_collision_map_loader_clear(qa_collision_map_loader *l);
#endif
