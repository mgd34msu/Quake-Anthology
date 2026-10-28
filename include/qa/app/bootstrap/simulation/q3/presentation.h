/* Port of src/app/bootstrap/simulation/q3/presentation.ts over landed
 * entity pool/game mirrors. SimulationPresentation mirrors
 * simulation/types.ts (sibling slice) with the fields set here.
 * Result arrays are caller buffers. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_PRESENTATION_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_PRESENTATION_H
#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/shared/definitions.h"

typedef struct qa_app_q3_presentation_entity {
  qa_actor_id actor;
  qa_q3_game_entity_state_mirror state;
  qa_vec3 origin;
  bool linked;
  double server_flags, single_client;
} qa_app_q3_presentation_entity;
typedef struct qa_app_q3_presentation_client {
  qa_actor_id actor;
  double slot;
  qa_q3_game_player_state_mirror state;
} qa_app_q3_presentation_client;
typedef struct qa_app_q3_presentation_configstring { double index; const char *value; } qa_app_q3_presentation_configstring;
typedef struct qa_app_q3_source_presentation_state {
  const char *product;
  double time;
  qa_app_q3_presentation_entity *entities;
  size_t entity_capacity, entity_count;
  qa_app_q3_presentation_client *clients;
  size_t client_capacity, client_count;
  qa_app_q3_presentation_configstring *configstrings;
  size_t configstring_capacity, configstring_count;
  char (*configstring_storage)[256];
} qa_app_q3_source_presentation_state;

typedef struct qa_app_configstring_getter {
  void *ctx;
  const char *(*get)(void *ctx, double index);
} qa_app_configstring_getter;

/* TS Q3SourceRuntime surface read by the runtime helpers. */
typedef struct qa_app_q3_source_runtime_view {
  qa_q3_entity_pool *pool;
  const char *product;
  double (*now)(void *ctx);
  double level_time;
  const char *content;
  void *ctx;
  qa_app_configstring_getter configstrings;
} qa_app_q3_source_runtime_view;

qa_app_q3_source_presentation_state qa_app_q3_build_source_presentation_state(qa_app_q3_source_runtime_view runtime,
    qa_app_q3_source_presentation_state out);
qa_app_q3_source_presentation_state qa_app_q3_pool_presentation_state(qa_q3_entity_pool *pool, const char *product,
    double time, qa_app_configstring_getter strings, qa_app_q3_source_presentation_state out);

/* qa_app_simulation_presentation: landed simulation/types.h. */
size_t qa_app_q3_source_models(qa_app_q3_source_runtime_view runtime, qa_app_simulation_presentation *out, size_t capacity);
size_t qa_app_q3_pool_models(qa_q3_entity_pool *pool, const char *product, double time, const char *content,
    qa_app_configstring_getter strings, qa_app_simulation_presentation *out, size_t capacity);
#endif
