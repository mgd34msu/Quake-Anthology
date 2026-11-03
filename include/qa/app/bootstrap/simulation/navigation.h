/* Port of src/app/bootstrap/simulation/navigation.ts:
 * botNavigationProfile and createApplicationBotNavigation (the
 * ApplicationBotNavigation selected bot navigation, its per-client
 * runtimes, the world entity/hazard bindings, and its checkpoint).
 *
 * TS takes SharedSimulation and LoadedApplicationContent; the C
 * SharedSimulation is not yet the composed type, so the members
 * navigation.ts reads are the two views below, typed with the real
 * lower-layer C types. Two SharedSimulation-level functions TS calls
 * from sibling modules have no C body yet and arrive through the
 * simulation view: applicationTrainConnections (bot-mover-
 * connections.ts) and predictApplicationBotMovement (bot-prediction.ts).
 *
 * C ownership: async createApplicationBotNavigation is synchronous.
 * A world's route prediction (begin_route) stays valid until the next
 * begin_route on the same world or the navigation's destroy; every
 * caller in the bots layer consumes it synchronously. Player identity
 * (TS object identity of MovementPredictionPlayer) is the `identity`
 * pointer of qa_app_sim_nav_player. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NAVIGATION_H
#define QA_APP_BOOTSTRAP_SIMULATION_NAVIGATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/player_movement.h"
#include "qa/app/bootstrap/simulation/q3/guest_movement.h"
#include "qa/bots/behavior/q3/navigation_services.h"
#include "qa/bots/behavior/q3/travel/types.h"
#include "qa/bots/navigation/construct.h"
#include "qa/bots/navigation/load.h"
#include "qa/bots/navigation/runtime.h"
#include "qa/content/mounts.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q2/base/entities/index.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/movers.h"
#include "qa/content/q2/missionpacks/entities/movers.h"
#include "qa/content/q3/base/records.h"
#include "qa/contracts/content.h"
#include "qa/persistence/value.h"
#include "qa/world/collision/index.h"
#include "qa/world/gameplay/inventory.h"

/* TS botNavigationProfile(player). */
qa_navigation_profile qa_app_sim_bot_navigation_profile(qa_app_sim_locomotion_player player);
/* TS LocomotionPlayer view of a MovementPredictionPlayer. */
qa_app_sim_locomotion_player qa_app_sim_locomotion_of(const qa_app_sim_movement_prediction_player *player);

/* TS MovementPredictionPlayer: object identity plus its live fields. */
typedef struct qa_app_sim_nav_player {
  const void *identity;
  qa_app_sim_movement_prediction_player value;
} qa_app_sim_nav_player;

/* TS Extract<SourceRuntime, {kind:"q2"}> members read here:
 * game, baseEntities, movers and product.expansions[].entities.movers
 * (expansion_movers[i] NULL when that expansion has no movers). */
typedef struct qa_app_sim_nav_q2_source {
  qa_q2_game_services *game;
  qa_q2_base_entity_module *base_entities;
  qa_q2_mover_module *movers;
  qa_q2mp_rogue_movers *const *expansion_movers; size_t expansion_count;
} qa_app_sim_nav_q2_source;
/* TS Q3SourceRuntime members read here: records and level.time. */
typedef struct qa_app_sim_nav_q3_source {
  qa_q3_base_entity_records *records;
  double level_time;
} qa_app_sim_nav_q3_source;

/* The SharedSimulation members navigation.ts reads. Every function
 * receives ctx. Borrowed arrays stay valid until the next call of the
 * same function. */
typedef struct qa_app_sim_navigation_simulation {
  void *ctx;
  const qa_executable_recipe *recipe;     /* simulation.recipe (identity) */
  qa_decoded_world world;                 /* simulation.options.world */
  qa_shared_scene_queries *scene;         /* simulation.scene */
  qa_shared_inventory_table *inventory;   /* simulation.inventory */
  double (*time_seconds)(void *ctx);      /* simulation.timeSeconds */
  /* simulation.players() */
  const qa_owned_actor *(*players)(void *ctx, size_t *out_count);
  /* simulation.movementPlayer(actor); false is TS null. */
  bool (*movement_player)(void *ctx, qa_owned_actor actor, qa_app_sim_nav_player *out);
  /* simulation.q3Guest(): false is TS null; out_host is the guest's
   * guestMovementProjection host. */
  bool (*q3_guest)(void *ctx, qa_app_q3_guest_movement_host *out_host);
  /* simulation.q3Guest().players() */
  const qa_app_q3_player *(*q3_guest_players)(void *ctx, size_t *out_count);
  /* simulation.q1Source()?.game; NULL is TS null. */
  qa_q1_services *(*q1_game)(void *ctx);
  bool (*q2_source)(void *ctx, qa_app_sim_nav_q2_source *out);
  bool (*q3_source)(void *ctx, qa_app_sim_nav_q3_source *out);
  /* The SharedSimulation view createPlayerMovementPrediction reads. */
  qa_app_sim_prediction_simulation (*prediction)(void *ctx);
  /* TS applicationTrainConnections(simulation, profile) (bot-mover-
   * connections.ts, not yet ported in C); the array is borrowed. */
  const qa_navigation_connection *(*train_connections)(void *ctx, qa_navigation_profile profile, size_t *out_count);
  /* TS predictApplicationBotMovement(simulation, player, query, navigation)
   * (bot-prediction.ts, not yet ported in C). */
  qa_bot_travel_prediction_result (*predict_movement)(void *ctx, const qa_app_sim_nav_player *player,
      qa_bot_movement_prediction query, qa_navigation_runtime *navigation);
} qa_app_sim_navigation_simulation;

/* The LoadedApplicationContent members navigation.ts reads. */
typedef struct qa_app_sim_navigation_content {
  void *ctx;
  const qa_executable_recipe *recipe;      /* content.recipe (identity) */
  qa_content_mounted *mounts;              /* content.mounts (read) */
  /* content.forContent(content); borrowed. */
  qa_content_mounted *(*for_content)(void *ctx, qa_content_id content);
} qa_app_sim_navigation_content;

/* TS ApplicationBotNavigationCheckpoint. Owned arrays. */
typedef struct qa_app_sim_bot_navigation_client_checkpoint {
  int64_t client; bool reusable; qa_navigation_runtime_checkpoint runtime;
} qa_app_sim_bot_navigation_client_checkpoint;
typedef struct qa_app_sim_bot_navigation_checkpoint {
  double version;
  qa_navigation_runtime_checkpoint base;
  qa_app_sim_bot_navigation_client_checkpoint *clients; size_t client_count;
} qa_app_sim_bot_navigation_checkpoint;
void qa_app_sim_bot_navigation_checkpoint_destroy(qa_app_sim_bot_navigation_checkpoint *checkpoint);
/* The TS object form of a NavigationRuntimeCheckpoint / application
 * checkpoint (what TS hands to the save encoder or restoreCheckpoint).
 * Owned; NULL only on OOM. */
qa_save_value *qa_app_sim_navigation_runtime_checkpoint_value(const qa_navigation_runtime_checkpoint *checkpoint);
qa_save_value *qa_app_sim_bot_navigation_checkpoint_value(const qa_app_sim_bot_navigation_checkpoint *checkpoint);

typedef struct qa_app_sim_application_bot_navigation qa_app_sim_application_bot_navigation;
/* TS createApplicationBotNavigation. Both views are borrowed and must
 * outlive the navigation. */
qa_app_sim_application_bot_navigation *qa_app_sim_create_application_bot_navigation(
    qa_app_sim_navigation_content content, qa_app_sim_navigation_simulation simulation);
void qa_app_sim_application_bot_navigation_destroy(qa_app_sim_application_bot_navigation *navigation);
/* TS SelectedBotNavigation surface. The struct is owned by the
 * navigation; its runtime field follows restartRound. */
const qa_selected_bot_navigation_bots_a *qa_app_sim_application_bot_navigation_selected(qa_app_sim_application_bot_navigation *navigation);
qa_navigation_runtime *qa_app_sim_application_bot_navigation_runtime(qa_app_sim_application_bot_navigation *navigation);
qa_navigation_runtime *qa_app_sim_application_bot_navigation_for_client(qa_app_sim_application_bot_navigation *navigation, int64_t client);
qa_bounds qa_app_sim_application_bot_navigation_crouched_bounds(const qa_app_sim_application_bot_navigation *navigation);
/* TS travelWeapon: always null (false). */
bool qa_app_sim_application_bot_navigation_travel_weapon(qa_app_sim_application_bot_navigation *navigation,
    int64_t client, const char *mode, int64_t *out);
qa_bot_travel_prediction_result qa_app_sim_application_bot_navigation_predict_client_movement(
    qa_app_sim_application_bot_navigation *navigation, qa_bot_movement_prediction query);
void qa_app_sim_application_bot_navigation_restart_round(qa_app_sim_application_bot_navigation *navigation);
/* TS checkpoint(); false only on OOM. */
bool qa_app_sim_application_bot_navigation_checkpoint(qa_app_sim_application_bot_navigation *navigation,
    qa_app_sim_bot_navigation_checkpoint *out);
/* TS restoreCheckpoint(value, remapClient?): remap NULL is the
 * identity. Malformed input raises SaveFormatError. */
void qa_app_sim_application_bot_navigation_restore_checkpoint(qa_app_sim_application_bot_navigation *navigation,
    const qa_save_value *value, double (*remap_client)(void *ctx, double saved), void *remap_ctx);
#endif
