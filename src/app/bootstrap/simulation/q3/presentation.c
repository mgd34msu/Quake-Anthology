/* Port of src/app/bootstrap/simulation/q3/presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/presentation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include <math.h>

static qa_q3_base_product product_named(const char *product) {
  return product != NULL && strcmp(product, "missionpack") == 0 ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
}

void qa_app_q3_source_presentation_state_destroy(qa_app_q3_source_presentation_state *state) {
  if (state == NULL) return;
  for (size_t i = 0; i < state->client_count; i++) qa_q3_base_player_state_record_destroy(&state->clients[i].state);
  for (size_t i = 0; i < state->configstring_count; i++) free(state->configstrings[i].value);
  free(state->entities); free(state->clients); free(state->configstrings);
  free(state);
}

static qa_q3_config_string_store runtime_strings(qa_app_q3_source_runtime *runtime) {
  qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(runtime);
  qa_q3_config_string_store store = {host->configstrings_ctx, host->configstring_get, host->configstring_set};
  return store;
}

qa_app_q3_source_presentation_state *qa_app_q3_source_presentation_state_of(qa_app_q3_source_runtime *runtime) {
  qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(runtime);
  return qa_app_q3_pool_presentation_state(qa_app_q3_source_runtime_pool(runtime),
      product_named(qa_app_q3_source_runtime_options(runtime)->product), host->now(host->ctx), runtime_strings(runtime));
}

qa_app_q3_source_presentation_state *qa_app_q3_pool_presentation_state(qa_q3_entity_pool *pool,
    qa_q3_base_product product, double time, qa_q3_config_string_store strings) {
  qa_app_q3_source_presentation_state *out = calloc(1, sizeof *out);
  if (out == NULL) return NULL;
  out->product = product; out->time = time;
  size_t capacity = pool->num_entities > 0 ? (size_t)pool->num_entities : 1;
  out->entities = calloc(capacity, sizeof *out->entities);
  out->clients = calloc(capacity, sizeof *out->clients);
  out->configstrings = calloc(1024, sizeof *out->configstrings);
  if (out->entities == NULL || out->clients == NULL || out->configstrings == NULL) {
    qa_app_q3_source_presentation_state_destroy(out); return NULL;
  }
  for (double slot = 0; slot < pool->num_entities; slot++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool, slot);
    if (!qa_q3_game_entity_inuse(entity)) continue;
    qa_app_q3_source_presentation_entity *row = &out->entities[out->entity_count++];
    row->actor = qa_q3_game_entity_actor(entity).id;
    qa_q3_entity_state_record_copy(&row->state, &entity->s);
    row->origin = qa_q3_base_entity_shared_current_origin(&entity->r);
    row->linked = qa_q3_base_entity_shared_linked(&entity->r);
    row->server_flags = entity->r.sv_flags;
    row->single_client = entity->r.single_client;
    if (entity->client != NULL) {
      qa_app_q3_source_presentation_client *client = &out->clients[out->client_count];
      client->actor = qa_q3_game_entity_actor(entity).id;
      client->slot = slot;
      if (!qa_q3_base_player_state_copy(&client->state, &entity->client->ps)) {
        qa_app_q3_source_presentation_state_destroy(out); return NULL;
      }
      out->client_count++;
    }
  }
  for (double index = 0; index < 1024; index++) {
    const char *value = strings.get(strings.ctx, index);
    if (value == NULL || value[0] == '\0') continue;
    qa_app_q3_source_presentation_configstring *row = &out->configstrings[out->configstring_count];
    row->index = index;
    row->value = strdup(value);
    if (row->value == NULL) { qa_app_q3_source_presentation_state_destroy(out); return NULL; }
    out->configstring_count++;
  }
  return out;
}

static const char *missile_model(double weapon) {
  if (weapon == QA_Q3_WP_GRAPPLING_HOOK || weapon == QA_Q3_WP_ROCKET_LAUNCHER) return "models/ammo/rocket/rocket.md3";
  if (weapon == QA_Q3_WP_GRENADE_LAUNCHER) return "models/ammo/grenade1.md3";
  if (weapon == QA_Q3_WP_PROX_LAUNCHER) return "models/weaphits/proxmine.md3";
  if (weapon == QA_Q3_WP_NAILGUN) return "models/weaphits/nail.md3";
  if (weapon == QA_Q3_WP_BFG) return "models/weaphits/bfg.md3";
  return NULL;
}

/* TS "*" + entity.r.model.index: one interned name per inline model index,
 * live for the process, so presentations may keep the path. */
static const char *inline_model_path(double index) {
  static char **names;
  static size_t count;
  if (!(index >= 0 && index == trunc(index))) qa_range_error("Q3 inline model index is not a model number: %s", QA_JS_NUMBER(index));
  size_t at = (size_t)index;
  if (at >= count) {
    size_t grown = at + 1 > count * 2 ? at + 1 : count * 2;
    char **resized = realloc(names, grown * sizeof *resized);
    if (resized == NULL) qa_range_error("Out of memory");
    memset(resized + count, 0, (grown - count) * sizeof *resized);
    names = resized; count = grown;
  }
  if (names[at] == NULL) {
    char text[32];
    snprintf(text, sizeof text, "*%zu", at);
    names[at] = strdup(text);
    if (names[at] == NULL) qa_range_error("Out of memory");
  }
  return names[at];
}

size_t qa_app_q3_source_models(qa_app_q3_source_runtime *runtime, qa_app_simulation_presentation *out, size_t capacity) {
  const qa_app_q3_source_options *options = qa_app_q3_source_runtime_options(runtime);
  return qa_app_q3_pool_models(qa_app_q3_source_runtime_pool(runtime), product_named(options->product),
      qa_app_q3_source_runtime_level_time(runtime), options->recipe.map.entities.content, runtime_strings(runtime), out, capacity);
}

size_t qa_app_q3_pool_models(qa_q3_entity_pool *pool, qa_q3_base_product product, double time, qa_content_id content,
    qa_q3_config_string_store strings, qa_app_simulation_presentation *out, size_t capacity) {
  size_t count = 0;
  for (double slot = 0; slot < pool->num_entities; slot++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool, slot);
    const qa_q3_entity_state_record *state = &entity->s;
    if (!qa_q3_game_entity_inuse(entity) || entity->client != NULL || !qa_q3_base_entity_shared_linked(&entity->r)
        || (qa_js_to_int32(entity->r.sv_flags) & QA_Q3_BASE_SVF_NOCLIENT) != 0
        || (qa_js_to_int32(state->e_flags) & 0x80) != 0) continue;
    const char *paths[4] = {NULL, NULL, NULL, NULL};
    size_t path_count = 0;
    if (state->e_type == QA_Q3_BASE_ET_ITEM) {
      const qa_q3_base_item_definition *item = qa_q3_base_item_at(product, state->modelindex);
      for (size_t i = 0; i < 4; i++) paths[path_count++] = item->world_models[i];
    } else if (state->e_type == QA_Q3_BASE_ET_MISSILE || state->e_type == QA_Q3_BASE_ET_GRAPPLE) {
      paths[path_count++] = missile_model(state->weapon);
    } else if (entity->r.model.kind == QA_Q3_BASE_MODEL_INLINE) {
      paths[path_count++] = inline_model_path(entity->r.model.index);
    } else if (state->modelindex > 0) {
      paths[path_count++] = strings.get(strings.ctx, 32 + state->modelindex);
    }
    for (size_t i = 0; i < path_count; i++) {
      if (paths[i] == NULL || paths[i][0] == '\0') continue;
      if (count < capacity) {
        qa_app_simulation_presentation *row = &out[count];
        memset(row, 0, sizeof *row);
        row->actor = qa_q3_game_entity_actor(entity).id;
        row->content = content; row->family = QA_GAME_FAMILY_Q3; row->render_owner = "source-client"; row->path = paths[i];
        row->frame = state->frame; row->old_frame = state->frame; row->skin = 0;
        row->effects = state->e_flags; row->render_flags = 0;
        row->origin = qa_q3_base_evaluate_trajectory(state->pos, time);
        row->angles = qa_q3_base_evaluate_trajectory(state->apos, time);
        row->scale = 1; row->visible = true; row->view_weapon = false;
      }
      count++;
    }
  }
  return count;
}
