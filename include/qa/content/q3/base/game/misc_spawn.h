/* Port of src/content/q3/base/game/misc-spawn.ts. GPL-2.0-or-later.
 * MissileRuntime fire calls and ItemRegistry are host fn+ctx: landed
 * missile.h/item_lifecycle.h expose no fire/register surface (mirrors,
 * TS owners ./missile.ts, ./item-lifecycle.ts; unique host names). */
#ifndef QA_CONTENT_Q3_BASE_GAME_MISC_SPAWN_H
#define QA_CONTENT_Q3_BASE_GAME_MISC_SPAWN_H
#include <stddef.h>
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/missile.h"
#include "qa/content/q3/base/game/spawn.h"
typedef void (*qa_q3_misc_fire_fn)(void *ctx, qa_q3_game_entity *entity, qa_vec3 origin, qa_q3_missile_direction direction);
typedef struct qa_q3_misc_spawn_host {
  void *ctx; qa_q3_entity_pool *pool; qa_q3_server_world_mirror world;
  qa_q3_game_product item_product, missile_product;
  double (*time)(void *ctx); double (*rand_value)(void *ctx); double (*crandom)(void *ctx);
  void (*warn)(void *ctx, const char *message);
  qa_q3_misc_fire_fn fire_grenade, fire_rocket, fire_plasma;
  void (*register_item)(void *ctx, const qa_q3_game_item_definition_mirror *item);
  const qa_q3_game_item_definition_mirror *(*find_item_for_weapon)(void *ctx, double weapon);
} qa_q3_misc_spawn_host;
typedef struct qa_q3_misc_spawn_runtime { qa_q3_misc_spawn_host host; } qa_q3_misc_spawn_runtime;
void qa_q3_misc_spawn_runtime_init(qa_q3_misc_spawn_runtime *runtime, qa_q3_misc_spawn_host host);
void qa_q3_misc_spawn_bind_save_callbacks(qa_q3_misc_spawn_runtime *runtime);
size_t qa_q3_misc_spawn_handlers(qa_q3_misc_spawn_host host, qa_q3_spawn_handler_entry *out, size_t out_cap);
size_t qa_q3_misc_spawn_runtime_handlers(qa_q3_misc_spawn_runtime *runtime, qa_q3_spawn_handler_entry *out, size_t out_cap);
#endif
