/* Port of src/compat/qvm/game-data.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_GAME_DATA_H
#define QA_COMPAT_QVM_GAME_DATA_H

#include <stdbool.h>

#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/shared_entity_record.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/protocol.h"

typedef struct qa_qvm_game_data_state {
  double entities_word, num_entities, entity_stride, clients_word, client_stride;
} qa_qvm_game_data_state;

typedef struct qa_qvm_game_data {
  qa_qvm_memory *memory; /* borrowed */
  qa_qvm_abi_profile abi_profile;
  bool has_entities;
  int64_t entities;
  int64_t entity_stride, count;
  bool has_clients;
  int64_t clients;
  int64_t client_stride, client_count;
  /* Cached borrows by offset, as the TS Map (small linear table). */
  int64_t cached_offsets[1024];
  qa_qvm_shared_entity cached_entities[1024];
  size_t cached_count;
} qa_qvm_game_data;

/* abi_profile NULL means "q3-modern". */
void qa_qvm_game_data_init(qa_qvm_game_data *data, qa_qvm_memory *memory,
                           qa_qvm_abi_profile abi_profile);
void qa_qvm_game_data_set_client_count(qa_qvm_game_data *data, double count);
double qa_qvm_game_data_get_num_clients(const qa_qvm_game_data *data);
double qa_qvm_game_data_get_num_entities(const qa_qvm_game_data *data);
double qa_qvm_game_data_get_entity_stride_bytes(const qa_qvm_game_data *data);
double qa_qvm_game_data_get_client_stride_bytes(const qa_qvm_game_data *data);
void qa_qvm_game_data_entity_bytes(qa_qvm_game_data *data, double number,
                                   qa_qvm_writable_view *out);
void qa_qvm_game_data_client_bytes(qa_qvm_game_data *data, double number,
                                   qa_qvm_writable_view *out);
void qa_qvm_game_data_public_entity_bytes(qa_qvm_game_data *data, double number,
                                          qa_qvm_writable_view *out);
void qa_qvm_game_data_public_player_bytes(qa_qvm_game_data *data, double number,
                                          qa_qvm_writable_view *out);
void qa_qvm_game_data_clear(qa_qvm_game_data *data);
qa_qvm_game_data_state qa_qvm_game_data_checkpoint(const qa_qvm_game_data *data);
void qa_qvm_game_data_restore(qa_qvm_game_data *data, qa_qvm_game_data_state state);
void qa_qvm_game_data_locate(qa_qvm_game_data *data, double entities_word, double num_entities,
                             double entity_stride, double clients_word, double client_stride);
/* Borrowed cached entity (stable until the table is relocated). */
qa_qvm_shared_entity *qa_qvm_game_data_entity(qa_qvm_game_data *data, double number);
qa_qvm_shared_entity *qa_qvm_game_data_entity_from_pointer(qa_qvm_game_data *data, double word);
double qa_qvm_game_data_number_from_pointer(qa_qvm_game_data *data, double word);
/* Slot arrays of the returned state point at caller storage. */
void qa_qvm_game_data_copy_player_state(qa_qvm_game_data *data, double number,
                                        qa_q3_player_state *out, double stats[16],
                                        double persistent[16], double powerups[16],
                                        double ammo[16]);
void qa_qvm_game_data_write_player_state(qa_qvm_game_data *data, double number,
                                         const qa_q3_player_state *state);
double qa_qvm_game_data_player_ping(qa_qvm_game_data *data, double number);
void qa_qvm_game_data_set_player_ping(qa_qvm_game_data *data, double number, double ping);

#endif
