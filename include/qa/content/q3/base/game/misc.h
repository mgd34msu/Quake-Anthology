/* Port of src/content/q3/base/game/misc.ts. GPL-2.0-or-later.
 * ServerWorld/snapshot/setClientViewAngle mirrors: TS owners ../world.ts,
 * ../shared/snapshot-state.ts, ../../team-arena/client-spawn.ts (landed
 * real types are record-shaped; game mirror stays). directionToByte
 * delegates to landed shared/direction_byte.h. */
#ifndef QA_CONTENT_Q3_BASE_GAME_MISC_H
#define QA_CONTENT_Q3_BASE_GAME_MISC_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/utilities.h"
/* Mirror of ../world.ts ServerWorld link/unlink used here. */
typedef struct qa_q3_server_world_mirror {
  void *ctx; void (*link)(void *ctx, qa_q3_game_entity *entity); void (*unlink_number)(void *ctx, double number);
} qa_q3_server_world_mirror;
typedef struct qa_q3_kill_box_host {
  void *ctx; size_t (*area_actors)(void *ctx, qa_bounds bounds, double max_count, qa_actor_id *out, size_t out_cap);
  bool (*has_linked_bounds)(void *ctx, qa_actor_id actor); bool (*is_player)(void *ctx, qa_actor_id actor);
  qa_q3_damage_participant (*participant)(void *ctx, qa_actor_id actor);
} qa_q3_kill_box_host;
void qa_q3_kill_box(qa_q3_game_combat_context *context, qa_q3_kill_box_host host, qa_q3_game_entity *entity);
typedef struct qa_q3_teleport_context { qa_q3_game_combat_context *combat; qa_q3_kill_box_host kill_box; qa_q3_server_world_mirror world; } qa_q3_teleport_context;
void qa_q3_teleport_player(qa_q3_teleport_context context, qa_q3_game_entity *player, qa_vec3 origin, qa_vec3 angles);
typedef struct qa_q3_portal_context { qa_q3_target_selection_context selection; qa_q3_server_world_mirror world; double time; } qa_q3_portal_context;
void qa_q3_locate_camera(qa_q3_portal_context context, qa_q3_game_entity *entity);
void qa_q3_spawn_portal_surface(qa_q3_portal_context context, qa_q3_game_entity *entity);
void qa_q3_spawn_portal_camera(qa_q3_server_world_mirror world, qa_q3_game_entity *entity, double roll);
void qa_q3_bind_portal_save_callbacks(qa_q3_portal_context context);
/* Mirror of shared/direction-byte.ts directionToByte. */
double qa_q3_direction_to_byte_mirror(qa_vec3 direction);
#endif
