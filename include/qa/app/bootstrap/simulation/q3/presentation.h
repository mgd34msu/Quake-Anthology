/* Port of src/app/bootstrap/simulation/q3/presentation.ts.
 * Q3SourcePresentationState is the TS interface: rows hold copies of the
 * source EntityState (entity.s.copy()), PlayerState (client.ps.copy()) and
 * configstring text, taken from the real EntityPool and ConfigStringStore.
 * The state owns its arrays, player-state copies and strings; release it
 * with qa_app_q3_source_presentation_state_destroy. Model presentations
 * are written into a caller buffer; their paths are item/configstring
 * strings or process-lifetime "*N" inline-model names. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_PRESENTATION_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/utilities.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

/* TS Q3SourcePresentationState["entities"][number]. */
typedef struct qa_app_q3_source_presentation_entity {
  qa_actor_id actor;
  qa_q3_base_entity_state state;
  qa_vec3 origin;
  bool linked;
  double server_flags, single_client;
} qa_app_q3_source_presentation_entity;
/* TS Q3SourcePresentationState["clients"][number]; state is an owned copy. */
typedef struct qa_app_q3_source_presentation_client {
  qa_actor_id actor;
  double slot;
  qa_q3_base_player_state state;
} qa_app_q3_source_presentation_client;
/* TS Q3SourcePresentationState["configstrings"][number]; value is owned. */
typedef struct qa_app_q3_source_presentation_configstring {
  double index;
  char *value;
} qa_app_q3_source_presentation_configstring;
/* TS Q3SourcePresentationState. */
typedef struct qa_app_q3_source_presentation_state {
  qa_q3_base_product product;
  double time;
  qa_app_q3_source_presentation_entity *entities;
  size_t entity_count;
  qa_app_q3_source_presentation_client *clients;
  size_t client_count;
  qa_app_q3_source_presentation_configstring *configstrings;
  size_t configstring_count;
} qa_app_q3_source_presentation_state;
void qa_app_q3_source_presentation_state_destroy(qa_app_q3_source_presentation_state *state);

struct qa_app_q3_source_runtime;
/* TS q3SourcePresentationState(runtime). NULL only on OOM. */
qa_app_q3_source_presentation_state *qa_app_q3_source_presentation_state_of(struct qa_app_q3_source_runtime *runtime);
/* TS q3PoolPresentationState(pool, product, time, strings). NULL only on OOM. */
qa_app_q3_source_presentation_state *qa_app_q3_pool_presentation_state(qa_q3_entity_pool *pool,
    qa_q3_base_product product, double time, qa_q3_config_string_store strings);

/* TS q3SourceModels(runtime) / q3PoolModels(...). Returns the number of
 * presentations TS produces; rows beyond capacity are not written. */
size_t qa_app_q3_source_models(struct qa_app_q3_source_runtime *runtime, qa_app_simulation_presentation *out, size_t capacity);
size_t qa_app_q3_pool_models(qa_q3_entity_pool *pool, qa_q3_base_product product, double time, qa_content_id content,
    qa_q3_config_string_store strings, qa_app_simulation_presentation *out, size_t capacity);
#endif
