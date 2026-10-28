/* Port of src/app/bootstrap/simulation/q3/presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/presentation.h"
#include <stdio.h>
#include <string.h>
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/movement/q3/constants.h"

static qa_q3_base_product product_of(const char *product) {
  return product && strcmp(product, "missionpack") == 0 ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
}
qa_app_q3_source_presentation_state qa_app_q3_build_source_presentation_state(qa_app_q3_source_runtime_view runtime,
    qa_app_q3_source_presentation_state out) {
  return qa_app_q3_pool_presentation_state(runtime.pool, runtime.product, runtime.now(runtime.ctx), runtime.configstrings, out);
}
qa_app_q3_source_presentation_state qa_app_q3_pool_presentation_state(qa_q3_entity_pool *pool, const char *product,
    double time, qa_app_configstring_getter strings, qa_app_q3_source_presentation_state out) {
  out.product = product; out.time = time; out.entity_count = 0; out.client_count = 0; out.configstring_count = 0;
  for (double slot = 0; slot < pool->num_entities; slot++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool, slot);
    if (!qa_q3_game_entity_inuse(entity)) continue;
    if (out.entity_count < out.entity_capacity) {
      qa_app_q3_presentation_entity *row = &out.entities[out.entity_count++];
      row->actor = qa_q3_game_entity_actor(entity).id;
      row->state = entity->s;
      row->origin = entity->r.current_origin;
      row->linked = entity->r.linked;
      row->server_flags = entity->r.sv_flags;
      row->single_client = entity->r.single_client;
    }
    if (entity->client && out.client_count < out.client_capacity) {
      qa_app_q3_presentation_client *row = &out.clients[out.client_count++];
      row->actor = qa_q3_game_entity_actor(entity).id;
      row->slot = slot;
      row->state = entity->client->ps;
    }
  }
  for (double index = 0; index < 1024; index++) {
    const char *value = strings.get(strings.ctx, index);
    if (value && strcmp(value, "") != 0 && out.configstring_count < out.configstring_capacity) {
      qa_app_q3_presentation_configstring *row = &out.configstrings[out.configstring_count];
      row->index = index;
      if (out.configstring_storage) {
        strncpy(out.configstring_storage[out.configstring_count], value, 255);
        row->value = out.configstring_storage[out.configstring_count];
      } else row->value = value;
      out.configstring_count++;
    }
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
size_t qa_app_q3_source_models(qa_app_q3_source_runtime_view runtime, qa_app_simulation_presentation *out, size_t capacity) {
  return qa_app_q3_pool_models(runtime.pool, runtime.product, runtime.level_time, runtime.content, runtime.configstrings, out, capacity);
}
size_t qa_app_q3_pool_models(qa_q3_entity_pool *pool, const char *product, double time, const char *content,
    qa_app_configstring_getter strings, qa_app_simulation_presentation *out, size_t capacity) {
  size_t count = 0;
  static char inline_paths[256][32];
  size_t inline_cursor = 0;
  for (double slot = 0; slot < pool->num_entities; slot++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool, slot);
    qa_q3_game_entity_state_mirror *state = &entity->s;
    if (!qa_q3_game_entity_inuse(entity) || entity->client != NULL || !entity->r.linked
        || (((long long)entity->r.sv_flags) & QA_Q3_BASE_SVF_NOCLIENT) != 0
        || (((long long)state->e_flags) & 0x80) != 0) continue;
    const char *paths[4] = {NULL, NULL, NULL, NULL};
    size_t path_count = 0;
    if (state->e_type == QA_Q3_BASE_ET_ITEM) {
      const qa_q3_base_item_definition *item = qa_q3_base_item_at(product_of(product), state->modelindex);
      if (item) for (size_t i = 0; i < 4; i++) paths[path_count++] = item->world_models[i];
    } else if (state->e_type == QA_Q3_BASE_ET_MISSILE || state->e_type == QA_Q3_BASE_ET_GRAPPLE) {
      paths[path_count++] = missile_model(state->weapon);
    } else if (entity->r.model_kind == QA_Q3_BASE_MODEL_INLINE) {
      char *buffer = inline_paths[inline_cursor++ % 256];
      snprintf(buffer, 32, "*%lld", (long long)entity->r.model_index);
      paths[path_count++] = buffer;
    } else if (state->modelindex > 0) {
      paths[path_count++] = strings.get(strings.ctx, 32 + state->modelindex);
    }
    for (size_t i = 0; i < path_count && count < capacity; i++) {
      if (!paths[i] || strcmp(paths[i], "") == 0) continue;
      qa_app_simulation_presentation *row = &out[count++];
      memset(row, 0, sizeof *row);
      row->actor = qa_q3_game_entity_actor(entity).id;
      row->content = content; row->family = QA_GAME_FAMILY_Q3; row->render_owner = "source-client"; row->path = paths[i];
      row->frame = state->frame; row->old_frame = state->frame; row->skin = 0;
      row->effects = state->e_flags; row->render_flags = 0;
      qa_q3_base_trajectory pos = {state->pos.type, state->pos.time, state->pos.duration, state->pos.base, state->pos.delta};
      qa_q3_base_trajectory apos = {state->apos.type, state->apos.time, state->apos.duration, state->apos.base, state->apos.delta};
      row->origin = qa_q3_base_evaluate_trajectory(pos, time);
      row->angles = qa_q3_base_evaluate_trajectory(apos, time);
      row->scale = 1; row->visible = true; row->view_weapon = false;
    }
  }
  return count;
}
