/*
 * Port of src/compat/qvm/cgame.ts. QvmCgameLifetime.current's
 * dropped Error becomes a message string (NULL when none) that is
 * re-raised through qa_range_error. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_CGAME_H
#define QA_COMPAT_QVM_CGAME_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/module.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/ui.h"

typedef struct qa_qvm_cgame_lifetime_state {
  double generation, server_message_number;
  const char *dropped; /* NULL is the TS null */
} qa_qvm_cgame_lifetime_state;

typedef struct qa_qvm_cgame_lifetime {
  void *ctx;
  void (*assert_current_operation)(void *ctx);
  qa_qvm_cgame_lifetime_state (*current)(void *ctx);
  void (*begin_loading)(void *ctx);
  void (*prime)(void *ctx, double generation);
} qa_qvm_cgame_lifetime;

typedef enum qa_qvm_cgame_phase_kind {
  QA_QVM_CGAME_CREATED,
  QA_QVM_CGAME_RETIRED,
  QA_QVM_CGAME_INITIALIZING,
  QA_QVM_CGAME_INITIALIZED
} qa_qvm_cgame_phase_kind;

typedef struct qa_qvm_cgame {
  qa_q3_api_identity api;
  qa_qvm_module module;
  qa_seat_id seat;
  qa_qvm_cgame_lifetime lifetime;
  qa_qvm_cgame_phase_kind phase_kind;
  double phase_generation;
} qa_qvm_cgame;

void qa_qvm_cgame_init_module(qa_qvm_cgame *cgame, qa_seat_id seat,
                              const qa_qvm_module_options *options, qa_qvm_cgame_lifetime lifetime);
void qa_qvm_cgame_destroy(qa_qvm_cgame *cgame);
bool qa_qvm_cgame_get_supports_input_events(const qa_qvm_cgame *cgame);
/* Fills the Q3CgameExports table (contracts/ui.h) for this instance. */
qa_q3_cgame_exports qa_qvm_cgame_as_exports(qa_qvm_cgame *cgame);

void qa_qvm_cgame_init(qa_qvm_cgame *cgame, double server_message_number,
                       double server_command_sequence, double client_number);
void qa_qvm_cgame_shutdown(qa_qvm_cgame *cgame);
void qa_qvm_cgame_retire(qa_qvm_cgame *cgame);
bool qa_qvm_cgame_console_command(qa_qvm_cgame *cgame, const char *const *arguments,
                                  size_t argument_count);
void qa_qvm_cgame_draw_active_frame(qa_qvm_cgame *cgame, double time, qa_stereo_view stereo,
                                    bool demo_playback);
qa_number_result qa_qvm_cgame_crosshair_player(qa_qvm_cgame *cgame);
qa_number_result qa_qvm_cgame_last_attacker(qa_qvm_cgame *cgame);
void qa_qvm_cgame_key_event(qa_qvm_cgame *cgame, double key, bool down);
void qa_qvm_cgame_mouse_event(qa_qvm_cgame *cgame, double dx, double dy);
void qa_qvm_cgame_event_handling(qa_qvm_cgame *cgame, qa_q3_cgame_event_handling mode);

#endif
