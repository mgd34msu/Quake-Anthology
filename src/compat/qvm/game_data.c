/* Port of src/compat/qvm/game-data.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/game_data.h"

#include <math.h>
#include <string.h>

#include "qa/compat/qvm/player_record.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static void check_int32(double value) {
  if (!(isfinite(value) && value == trunc(value)) || value < -2147483648.0 || value > 2147483647.0)
    qa_range_error("Game-data indexes and strides require signed 32-bit words");
}
static bool is_integer(double v) { return isfinite(v) && v == trunc(v); }
static bool modern(qa_qvm_abi_profile p) { return p == NULL || strcmp(p, "q3-modern") == 0; }

void qa_qvm_game_data_init(qa_qvm_game_data *data, qa_qvm_memory *memory,
                           qa_qvm_abi_profile abi_profile) {
  memset(data, 0, sizeof *data);
  data->memory = memory;
  data->abi_profile = abi_profile == NULL ? "q3-modern" : abi_profile;
  data->client_count = 64;
}

void qa_qvm_game_data_set_client_count(qa_qvm_game_data *data, double count) {
  if (!is_integer(count) || count < 1 || count > 64)
    qa_range_error("Q3 client count must be within 1..64");
  data->client_count = (int64_t)count;
}
double qa_qvm_game_data_get_num_clients(const qa_qvm_game_data *data) {
  return (double)data->client_count;
}
double qa_qvm_game_data_get_num_entities(const qa_qvm_game_data *data) { return (double)data->count; }
double qa_qvm_game_data_get_entity_stride_bytes(const qa_qvm_game_data *data) {
  return (double)data->entity_stride;
}
double qa_qvm_game_data_get_client_stride_bytes(const qa_qvm_game_data *data) {
  return (double)data->client_stride;
}

static void make_view(qa_qvm_game_data *data, int64_t offset, int64_t size,
                      qa_qvm_writable_view *out) {
  if (offset < 0 || offset > (int64_t)data->memory->length - size)
    qa_range_error("Game-data record exceeds the interpreter allocation");
  qa_qvm_memory_data_view(data->memory, offset, size, out);
}

static int64_t indexed(qa_qvm_game_data *data, bool has_base, int64_t base, int64_t stride,
                       double number) {
  if (!has_base) qa_range_error("Game-data table has a null source pointer");
  check_int32(number);
  double displacement = (double)stride * number;
  check_int32(displacement);
  int64_t offset = base + (int64_t)displacement;
  if (offset < 0 || offset > (int64_t)data->memory->length)
    qa_range_error("Game-data pointer arithmetic leaves the interpreter allocation");
  return offset;
}

static int64_t entity_offset(qa_qvm_game_data *data, double number) {
  if (!is_integer(number) || number < 0 || number >= (double)data->count)
    qa_range_error("Entity slot is outside located game data");
  return indexed(data, data->has_entities, data->entities, data->entity_stride, number);
}

static int64_t client_offset(qa_qvm_game_data *data, double number) {
  if (!is_integer(number) || number < 0 || number >= (double)data->client_count)
    qa_range_error("Client slot is outside configured game data");
  int64_t offset = indexed(data, data->has_clients, data->clients, data->client_stride, number);
  qa_qvm_writable_view view;
  make_view(data, offset, data->client_stride, &view);
  return offset;
}

void qa_qvm_game_data_entity_bytes(qa_qvm_game_data *data, double number,
                                   qa_qvm_writable_view *out) {
  make_view(data, entity_offset(data, number), data->entity_stride, out);
}
void qa_qvm_game_data_client_bytes(qa_qvm_game_data *data, double number,
                                   qa_qvm_writable_view *out) {
  make_view(data, client_offset(data, number), data->client_stride, out);
}
void qa_qvm_game_data_public_entity_bytes(qa_qvm_game_data *data, double number,
                                          qa_qvm_writable_view *out) {
  make_view(data, entity_offset(data, number), qa_qvm_shared_entity_bytes(data->abi_profile), out);
}
void qa_qvm_game_data_public_player_bytes(qa_qvm_game_data *data, double number,
                                          qa_qvm_writable_view *out) {
  make_view(data, client_offset(data, number), qa_qvm_player_state_bytes(data->abi_profile), out);
}

void qa_qvm_game_data_clear(qa_qvm_game_data *data) {
  data->has_entities = false;
  data->has_clients = false;
  data->entity_stride = 0;
  data->client_stride = 0;
  data->count = 0;
}

qa_qvm_game_data_state qa_qvm_game_data_checkpoint(const qa_qvm_game_data *data) {
  qa_qvm_game_data_state state;
  state.entities_word = !data->has_entities ? 0
                        : data->entities == 0 ? (double)data->memory->length
                                              : (double)data->entities;
  state.num_entities = (double)data->count;
  state.entity_stride = (double)data->entity_stride;
  state.clients_word = !data->has_clients ? 0
                       : data->clients == 0 ? (double)data->memory->length
                                             : (double)data->clients;
  state.client_stride = (double)data->client_stride;
  return state;
}

void qa_qvm_game_data_restore(qa_qvm_game_data *data, qa_qvm_game_data_state state) {
  if (state.entities_word == 0 && state.clients_word == 0 && state.num_entities == 0 &&
      state.entity_stride == 0 && state.client_stride == 0) {
    qa_qvm_game_data_clear(data);
    return;
  }
  qa_qvm_game_data_locate(data, state.entities_word, state.num_entities, state.entity_stride,
                          state.clients_word, state.client_stride);
}

static bool word_offset(qa_qvm_game_data *data, double word, int64_t *out) {
  qa_qvm_memory_span span;
  if (!qa_qvm_memory_pointer(data->memory, (int64_t)word, &span)) return false;
  *out = span.byte_offset;
  return true;
}

void qa_qvm_game_data_locate(qa_qvm_game_data *data, double entities_word, double num_entities,
                             double entity_stride, double clients_word, double client_stride) {
  int64_t entities = 0, clients = 0;
  bool has_entities = word_offset(data, entities_word, &entities);
  bool has_clients = word_offset(data, clients_word, &clients);
  check_int32(num_entities);
  check_int32(entity_stride);
  check_int32(client_stride);
  if (num_entities < 0 || num_entities > 1024)
    qa_range_error("Q3 wire entity capacity is 1024 slots");
  if (entity_stride < qa_qvm_shared_entity_bytes(data->abi_profile) ||
      (int64_t)entity_stride % 4 != 0 || client_stride < qa_qvm_player_state_bytes(data->abi_profile) ||
      (int64_t)client_stride % 4 != 0)
    qa_range_error("Game-data stride is undersized or unaligned");
  if (!has_entities || !has_clients || entities % 4 != 0 || clients % 4 != 0)
    qa_range_error("Game-data tables require aligned nonnull pointers");
  qa_qvm_writable_view view;
  make_view(data, entities, (int64_t)(num_entities * entity_stride), &view);
  make_view(data, clients, (int64_t)client_stride, &view);
  data->has_entities = true;
  data->entities = entities;
  data->entity_stride = (int64_t)entity_stride;
  data->count = (int64_t)num_entities;
  data->has_clients = true;
  data->clients = clients;
  data->client_stride = (int64_t)client_stride;
}

static qa_qvm_shared_entity *entity_at(qa_qvm_game_data *data, int64_t offset) {
  for (size_t i = 0; i < data->cached_count; i++)
    if (data->cached_offsets[i] == offset) return &data->cached_entities[i];
  qa_qvm_writable_view view;
  make_view(data, offset, qa_qvm_shared_entity_bytes(data->abi_profile), &view);
  if (data->cached_count >= 1024) qa_range_error("out of memory");
  size_t slot = data->cached_count++;
  data->cached_offsets[slot] = offset;
  qa_qvm_borrow_shared_entity(&view, data->abi_profile, &data->cached_entities[slot]);
  return &data->cached_entities[slot];
}

qa_qvm_shared_entity *qa_qvm_game_data_entity(qa_qvm_game_data *data, double number) {
  return entity_at(data, entity_offset(data, number));
}

qa_qvm_shared_entity *qa_qvm_game_data_entity_from_pointer(qa_qvm_game_data *data, double word) {
  return qa_qvm_game_data_entity(data, qa_qvm_game_data_number_from_pointer(data, word));
}

double qa_qvm_game_data_number_from_pointer(qa_qvm_game_data *data, double word) {
  int64_t offset = 0;
  bool has_offset = word_offset(data, word, &offset);
  if (!has_offset || !data->has_entities)
    qa_range_error("Entity numbering requires nonnull source pointers");
  if (data->entity_stride == 0) qa_range_error("Entity numbering requires a nonzero source stride");
  double number = (double)(offset - data->entities) / (double)data->entity_stride;
  (void)entity_offset(data, number);
  return number;
}

void qa_qvm_game_data_copy_player_state(qa_qvm_game_data *data, double number,
                                        qa_q3_player_state *out, double stats[16],
                                        double persistent[16], double powerups[16],
                                        double ammo[16]) {
  qa_qvm_writable_view view;
  make_view(data, client_offset(data, number), qa_qvm_player_state_bytes(data->abi_profile), &view);
  /* qvm-b reads slot arrays into the out views. */
  out->stats.values = stats;
  out->stats.count = 16;
  out->persistent.values = persistent;
  out->persistent.count = 16;
  out->powerups.values = powerups;
  out->powerups.count = 16;
  out->ammo.values = ammo;
  out->ammo.count = 16;
  qa_qvm_read_player_state(&view, data->abi_profile, out);
}

void qa_qvm_game_data_write_player_state(qa_qvm_game_data *data, double number,
                                         const qa_q3_player_state *state) {
  qa_qvm_writable_view view;
  make_view(data, client_offset(data, number), qa_qvm_player_state_bytes(data->abi_profile), &view);
  qa_qvm_write_player_state(&view, state, data->abi_profile, "preserve-private");
}

double qa_qvm_game_data_player_ping(qa_qvm_game_data *data, double number) {
  qa_qvm_writable_view view;
  make_view(data, client_offset(data, number) + (modern(data->abi_profile) ? 452 : 440), 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}

void qa_qvm_game_data_set_player_ping(qa_qvm_game_data *data, double number, double ping) {
  int64_t offset = client_offset(data, number);
  qa_qvm_writable_view view;
  make_view(data, offset + (modern(data->abi_profile) ? 452 : 440), 4, &view);
  qa_qvm_writable_view_set_int32(&view, 0, qa_int32(ping), true);
}
