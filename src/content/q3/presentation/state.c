/* Port of src/content/q3/presentation/state.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/state.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

qa_q3_pres_client_player_entity qa_q3_pres_create_client_player_entity(void) {
  qa_q3_pres_client_player_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.pose = qa_q3_create_player_pose_state();
  entity.flag.base = qa_q3_create_lerp_frame();
  return entity;
}

void qa_q3_pres_client_entity_init(qa_q3_pres_client_entity *entity) {
  memset(entity, 0, sizeof *entity);
  qa_q3_entity_state_init(&entity->current_state);
  qa_q3_entity_state_init(&entity->next_state);
  entity->player = qa_q3_pres_create_client_player_entity();
}

bool qa_q3_pres_client_entity_list_push(qa_q3_pres_client_entity_list *list, qa_q3_pres_client_entity *entity) {
  if (list->count == list->capacity) {
    size_t capacity = list->capacity == 0 ? 16 : list->capacity * 2;
    qa_q3_pres_client_entity **items = realloc(list->items, capacity * sizeof *items);
    if (items == NULL) return false;
    list->items = items;
    list->capacity = capacity;
  }
  list->items[list->count++] = entity;
  return true;
}

void qa_q3_pres_client_entity_list_destroy(qa_q3_pres_client_entity_list *list) {
  free(list->items);
  memset(list, 0, sizeof *list);
}

void qa_q3_pres_client_game_static_state_init(qa_q3_pres_client_game_static_state *state,
    qa_q3_base_product product) {
  memset(state, 0, sizeof *state);
  state->product = product;
  for (int index = 0; index < QA_Q3_PRES_MAX_CLIENTS; index++) {
    qa_q3_pres_client_info_init(&state->client_info[index]);
  }
  for (int index = 0; index < QA_Q3_PRES_MAX_GAME_MODELS; index++) {
    state->game_models[index] = qa_q3_pres_default_model;
  }
  state->game_type = QA_Q3_BASE_GT_FFA;
}

void qa_q3_pres_client_game_static_state_destroy(qa_q3_pres_client_game_static_state *state) {
  for (int index = 0; index < QA_Q3_PRES_TEAM_CHAT_COUNT; index++) free(state->team_chat_msgs[index]);
  free(state->accept_voice);
  for (int index = 0; index < QA_Q3_PRES_MAX_CLIENTS; index++) {
    qa_q3_pres_client_info_destroy(&state->client_info[index]);
  }
  free(state->mapname);
  free(state->red_team);
  free(state->blue_team);
  free(state->vote_string);
  free(state->team_vote_string[0]);
  free(state->team_vote_string[1]);
  memset(state, 0, sizeof *state);
}

bool qa_q3_pres_client_game_state_init(qa_q3_pres_client_game_state *state, qa_q3_base_product product,
    double client_num, double processed_snapshot_num) {
  if (client_num != floor(client_num) || client_num < 0 || client_num >= 64) {
    qa_range_error("Invalid cgame client number");
  }
  if (processed_snapshot_num != floor(processed_snapshot_num) ||
      processed_snapshot_num < -2147483648.0 || processed_snapshot_num > 2147483647.0) {
    qa_range_error("Invalid initial snapshot number");
  }
  memset(state, 0, sizeof *state);
  state->product = product;
  state->client_num = client_num;
  state->processed_snapshot_num = processed_snapshot_num;
  state->entities = malloc(QA_Q3_PRES_MAX_ENTITIES * sizeof *state->entities);
  if (state->entities == NULL) return false;
  for (int index = 0; index < QA_Q3_PRES_MAX_ENTITIES; index++) {
    qa_q3_pres_client_entity_init(&state->entities[index]);
  }
  qa_q3_pres_client_entity_init(&state->predicted_player_entity);
  state->refdef = qa_q3_pres_create_refdef();
  state->test_model_entity = qa_q3_pres_create_model_entity_default();
  if (!qa_q3_base_player_state_slots_init(&state->predictable_events, 16, NULL, 0, false, NULL, NULL, NULL)) {
    free(state->entities);
    state->entities = NULL;
    return false;
  }
  if (!qa_q3_base_player_state_record_init(&state->predicted_player_state, product, 0, 0, 0, NULL)) {
    qa_q3_base_player_state_slots_destroy(&state->predictable_events);
    free(state->entities);
    state->entities = NULL;
    return false;
  }
  return true;
}

void qa_q3_pres_client_game_state_destroy(qa_q3_pres_client_game_state *state) {
  free(state->entities);
  qa_q3_pres_client_entity_list_destroy(&state->solid_entities);
  qa_q3_pres_client_entity_list_destroy(&state->trigger_entities);
  free(state->center_print);
  free(state->info_screen_text);
  free(state->spectator_list);
  free(state->killer_name);
  free(state->test_model_name);
  qa_q3_base_player_state_record_destroy(&state->predicted_player_state);
  qa_q3_base_player_state_slots_destroy(&state->predictable_events);
  memset(state, 0, sizeof *state);
}

qa_q3_pres_client_entity *qa_q3_pres_client_game_state_entity_at(qa_q3_pres_client_game_state *state, double number) {
  /* TS entities[number] is undefined outside integer table indices. */
  if (number != floor(number) || number < 0 || number >= QA_Q3_PRES_MAX_ENTITIES) {
    if (number == floor(number) && number >= -9007199254740991.0 && number <= 9007199254740991.0) {
      qa_range_error("Invalid cgame entity number %lld", (long long)number);
    }
    qa_range_error("Invalid cgame entity number %s", QA_JS_NUMBER(number));
  }
  return &state->entities[(size_t)number];
}
