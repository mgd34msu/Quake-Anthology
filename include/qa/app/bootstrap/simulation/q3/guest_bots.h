/* Port of src/app/bootstrap/simulation/q3/guest-bots.ts. BotLibrary
 * is the host table below over landed bots/behavior; pointer/bytes
 * helpers are host seams. User-command syscall runs synchronously
 * (TS returns a Promise). Navigation memoization identity uses the
 * selected library pointer. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_BOTS_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_BOTS_H
#include <stdint.h>
#include "qa/app/bootstrap/simulation/q3/guest_records.h"
#include "qa/app/bootstrap/simulation/q3/guest_spatial.h"
#include "qa/bots/behavior/q3/navigation.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/bot_navigation_records.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/persistence/value.h"

typedef struct qa_app_q3_bot_entity_snapshot {
  qa_qvm_bot_entity_update state;
  double slot;
  double other_entity, frame, time;
} qa_app_q3_bot_entity_snapshot;

typedef struct qa_app_q3_bot_library {
  void *ctx;
  bool (*is_initialized)(void *ctx);
  double (*setup)(void *ctx);
  double (*shutdown)(void *ctx);
  void (*close)(void *ctx);
  double (*start_frame)(void *ctx, float time);
  double (*load_map)(void *ctx, const char *name);
  void (*update_entity)(void *ctx, double entity, qa_app_q3_bot_entity_snapshot state);
  const char *(*client_command)(void *ctx, double client, const char *text); /* NULL = TS undefined */
  double (*predict_time)(void *ctx, qa_vec3 start, qa_vec3 end, double client, double travel_flags);
  double (*visible_enemies)(void *ctx);
  double (*next_obituary)(void *ctx, double attacker, double victim, double mod);
  void *(*library_handle)(void *ctx); /* memoization identity */
  qa_source_bot_navigation_bots_a *(*source_navigation)(void *ctx); /* borrowed */
  bool (*aas_value)(void *ctx, const char *entities, int32_t *epair, const char *key, char *out, size_t out_size);
  bool (*aas_vector)(void *ctx, const char *entities, int32_t *epair, const char *key, qa_vec3 *out);
  qa_save_value *(*capture)(void *ctx);
  void (*restore)(void *ctx, const qa_save_value *value);
} qa_app_q3_bot_library;

typedef struct qa_app_q3_bot_clients {
  void *ctx;
  double (*allocate_client)(void *ctx);
  void (*free_client)(void *ctx, double slot);
  double (*snapshot_entity)(void *ctx, double slot, double sequence);
  const char *(*console_message)(void *ctx, double slot); /* NULL = TS null */
  void (*user_command)(void *ctx, double slot, qa_q3_base_user_command command);
} qa_app_q3_bot_clients;

typedef struct qa_app_q3_guest_bot_options {
  qa_app_q3_bot_library library;
  void *selected; /* borrowed SelectedBotLibrary handle */
  qa_app_q3_guest_records *records;
  qa_app_q3_guest_spatial *spatial;
  const char *entities;
  const char *map_name;
  qa_app_q3_bot_clients clients;
  void *host_ctx;
  /* memory helpers over the live QVM call */
  void (*output_struct)(void *ctx, const qa_qvm_host_call *call, double index, double bytes, const void *value, const char *what);
  void (*output_string)(void *ctx, const qa_qvm_host_call *call, double index, const char *value);
  void (*structure)(void *ctx, const qa_qvm_host_call *call, double index, double bytes, void *value, const char *what);
  const char *(*required_string)(void *ctx, const qa_qvm_host_call *call, double index, const char *what);
  double (*word_float)(void *ctx, const qa_qvm_host_call *call, double index);
} qa_app_q3_guest_bot_options;

typedef struct qa_app_q3_guest_bots qa_app_q3_guest_bots;
qa_app_q3_guest_bots *qa_app_q3_guest_bots_create(qa_app_q3_guest_bot_options options);
void qa_app_q3_guest_bots_destroy(qa_app_q3_guest_bots *bots);
/* false return + result untouched = TS null (another owner). */
bool qa_app_q3_guest_bots_syscall(qa_app_q3_guest_bots *bots, const qa_qvm_host_call *call, qa_qvm_host_result *out);
qa_save_value *qa_app_q3_guest_bots_checkpoint(qa_app_q3_guest_bots *bots);
void qa_app_q3_guest_bots_restore(qa_app_q3_guest_bots *bots, const qa_save_value *value);
void qa_app_q3_guest_bots_close(qa_app_q3_guest_bots *bots);
#endif
