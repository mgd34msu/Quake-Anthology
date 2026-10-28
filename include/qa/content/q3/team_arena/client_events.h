/* Port of src/content/q3/team-arena/client-events.ts. GPL-2.0-or-later.
 * WeaponRuntime/ClientSpawnRuntime/PersonalPortalRuntime/DropItemContext
 * services become fn+ctx callbacks; ps.eventSequence is the mirror's
 * generic ring counter and ps.generic1 has no mirror field (the
 * harvester cube count is passed through drops.generic1 accessors). */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_CLIENT_EVENTS_H
#define QA_CONTENT_Q3_TEAM_ARENA_CLIENT_EVENTS_H
#include <stdbool.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/world.h"
#include "qa/contracts/identity.h"

/* TS SpawnPose returned by selectSpawnPoint. */
typedef struct qa_q3_ta_spawn_pose { qa_vec3 origin, angles; } qa_q3_ta_spawn_pose;

/* TS ClientEventsContext services (+ product/combat/personalPortal). */
typedef struct qa_q3_ta_client_events_context {
  qa_q3_base_product product;
  qa_q3_combat_context *combat;
  qa_q3_base_server_world *world; /* borrowed */
  qa_q3_teleport_context teleport; /* for qa_q3_teleport_player */
  double dmflags;
  void *ctx;
  void (*fire_weapon)(void *ctx, qa_q3_game_entity *entity);
  void (*start_kamikaze)(void *ctx, qa_q3_game_entity *entity);
  qa_q3_ta_spawn_pose (*select_spawn_point)(void *ctx, qa_vec3 origin);
  qa_q3_game_entity *(*drop_item)(void *ctx, qa_q3_game_entity *entity,
      const qa_q3_base_item_definition *item, double angle);
  void (*drop_portal_source)(void *ctx, qa_q3_game_entity *entity);
  void (*drop_portal_destination)(void *ctx, qa_q3_game_entity *entity);
  /* TS optional primaryAttackAllowed: NULL fn is TS undefined. */
  bool (*primary_attack_allowed)(void *ctx, qa_actor_id actor);
  /* TS ps.generic1 accessors (no mirror field). */
  double (*generic1)(void *ctx, qa_q3_game_client *client);
  void (*set_generic1)(void *ctx, qa_q3_game_client *client, double value);
} qa_q3_ta_client_events_context;

void qa_q3_ta_client_events(qa_q3_ta_client_events_context *context, qa_q3_game_entity *entity,
    double old_event_sequence);

/* TS Q3HoldableContext: teleport closure as fn+ctx. */
typedef struct qa_q3_ta_holdable_context {
  qa_q3_base_product product;
  qa_q3_combat_context *combat;
  void *ctx;
  void (*teleport)(void *ctx, qa_q3_game_entity *entity);
  void (*start_kamikaze)(void *ctx, qa_q3_game_entity *entity);
  void (*drop_portal_source)(void *ctx, qa_q3_game_entity *entity);
  void (*drop_portal_destination)(void *ctx, qa_q3_game_entity *entity);
} qa_q3_ta_holdable_context;
void qa_q3_ta_use_holdable(qa_q3_ta_holdable_context *context, qa_q3_game_entity *entity, double event);

/* TS Pick<ClientEventsContext, "drops" | "product" | "combat"> (+ generic1). */
typedef struct qa_q3_ta_teleport_objectives_context {
  qa_q3_base_product product;
  qa_q3_combat_context *combat;
  void *ctx;
  qa_q3_game_entity *(*drop_item)(void *ctx, qa_q3_game_entity *entity,
      const qa_q3_base_item_definition *item, double angle);
  double (*generic1)(void *ctx, qa_q3_game_client *client);
  void (*set_generic1)(void *ctx, qa_q3_game_client *client, double value);
} qa_q3_ta_teleport_objectives_context;
void qa_q3_ta_drop_teleport_objectives(qa_q3_ta_teleport_objectives_context *context, qa_q3_game_entity *entity);
#endif
