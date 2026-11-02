/*
 * Port of src/compat/qvm/server-game-syscalls.ts. The TS
 * QvmGameData is the landed qa_qvm_game_data (game_data.h); the
 * client-game and entity-token delegates mirror
 * qvmClientGameSyscall (TS owner client-game-syscalls.ts) and
 * qvmEntityTokenSyscall (TS owner entity-tokens.ts) as callbacks the
 * caller binds to the owning slice. False = another trap owner.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_SERVER_GAME_SYSCALLS_H
#define QA_COMPAT_QVM_SERVER_GAME_SYSCALLS_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/game_data.h"
#include "qa/compat/qvm/server_info_syscalls.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/compat/qvm/trace_record.h"
#include "qa/contracts/math.h"
#include "qa/network/q3/message.h"

/* TS QvmServerTraceQuery: shape kind "box" | "capsule" (exact). */
typedef struct qa_qvm_server_trace_query {
  qa_vec3 start;
  qa_vec3 end;
  const char *shape_kind;
  qa_bounds shape_bounds;
  int32_t pass_entity_num;
  int32_t mask;
} qa_qvm_server_trace_query;

/* TS QvmServerSpatialOperations. area_entities fills out (up to
 * capacity) and returns the full result count. */
typedef struct qa_qvm_server_spatial_operations {
  void *ctx;
  qa_qvm_trace_record (*trace)(void *ctx, const qa_qvm_server_trace_query *input);
  int32_t (*point_contents)(void *ctx, qa_vec3 point, int32_t pass_entity_num);
  size_t (*area_entities)(void *ctx, qa_bounds bounds, int32_t maximum, int32_t *out, size_t capacity);
  bool (*entity_contact)(void *ctx, qa_bounds bounds, int32_t slot, bool capsule);
  void (*set_brush_model)(void *ctx, int32_t slot, const char *name);
  void (*adjust_area_portal_state)(void *ctx, int32_t slot, bool open);
  bool (*in_pvs)(void *ctx, qa_vec3 first, qa_vec3 second, bool ignore_portals);
  bool (*areas_connected)(void *ctx, int32_t first, int32_t second);
  void (*link)(void *ctx, int32_t slot);
  void (*unlink)(void *ctx, int32_t slot);
} qa_qvm_server_spatial_operations;

/* Mirrored delegate service bundles passed to the qvm-a syscalls. */
typedef struct qa_qvmy_client_game_services {
  qa_qvm_abi_profile abi_profile;
  int32_t max_clients;
  void *ctx;
  const char *(*get_userinfo)(void *ctx, int32_t slot);
  void (*set_userinfo)(void *ctx, int32_t slot, const char *value);
  qa_q3_wire_user_command (*get_user_command)(void *ctx, int32_t slot);
  void (*drop_client)(void *ctx, int32_t slot, const char *reason);
  void (*send_server_command)(void *ctx, int32_t slot, const char *text);
} qa_qvmy_client_game_services;

typedef bool (*qa_qvmy_client_game_syscall_fn)(void *ctx, const qa_qvm_host_call *call,
                                               const qa_qvmy_client_game_services *services, int32_t *out);
typedef bool (*qa_qvmy_entity_token_syscall_fn)(void *ctx, const qa_qvm_host_call *call,
                                                const char *token, bool ended, int32_t *out);

/* TS QvmServerGameServices. entity_token writes a borrowed token. */
typedef struct qa_qvm_server_game_services {
  qa_qvm_game_data *data;
  qa_cvar_registry *cvars;
  void *configstrings_ctx;
  const char *(*configstring_get)(void *ctx, int32_t index);
  void (*configstring_set)(void *ctx, int32_t index, const char *value);
  int32_t max_clients;
  const qa_qvm_server_spatial_operations *spatial;
  void *ctx;
  const char *(*get_userinfo)(void *ctx, int32_t slot);
  void (*set_userinfo)(void *ctx, int32_t slot, const char *value);
  qa_q3_wire_user_command (*get_user_command)(void *ctx, int32_t slot);
  void (*drop_client)(void *ctx, int32_t slot, const char *reason);
  void (*send_server_command)(void *ctx, int32_t slot, const char *text);
  void (*entity_token)(void *ctx, const char **out_token, bool *out_ended);
  qa_qvmy_client_game_syscall_fn client_game_syscall;
  void *client_game_ctx;
  qa_qvmy_entity_token_syscall_fn entity_token_syscall;
  void *entity_token_ctx;
} qa_qvm_server_game_services;

bool qa_qvm_server_game_syscall(const qa_qvm_host_call *call,
                                const qa_qvm_server_game_services *services, int32_t *out);

#endif
