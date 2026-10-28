/* Port of src/app/bootstrap/network/qw-types.ts. QuakeWorld
 * messages are landed payloads forwarded as borrowed opaque
 * pointers. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_QW_TYPES_H
#define QA_APP_BOOTSTRAP_NETWORK_QW_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
typedef enum qa_app_qw_download_category {
  QA_APP_QW_DL_SOUND, QA_APP_QW_DL_MODEL, QA_APP_QW_DL_SKIN
} qa_app_qw_download_category;
typedef enum qa_app_qw_download_request_result {
  QA_APP_QW_DL_AVAILABLE, QA_APP_QW_DL_WAITING, QA_APP_QW_DL_SKIPPED
} qa_app_qw_download_request_result;
typedef enum qa_app_qw_download_receive_result {
  QA_APP_QW_DL_RX_WAITING, QA_APP_QW_DL_RX_COMPLETE, QA_APP_QW_DL_RX_MISSING
} qa_app_qw_download_receive_result;
typedef struct qa_app_qw_downloads {
  void *ctx;
  qa_app_qw_download_request_result (*request)(void *ctx, const char *path, qa_app_qw_download_category category);
  qa_app_qw_download_receive_result (*receive)(void *ctx, const void *result);
  void (*close)(void *ctx);
} qa_app_qw_downloads;
typedef struct qa_app_qw_skins_host {
  void *ctx;
  const char *const *(*names)(void *ctx, size_t *out_count);
  void (*loading)(void *ctx, bool value);
  void (*prepare)(void *ctx);
} qa_app_qw_skins_host;
typedef struct qa_app_qw_client_host {
  void *ctx;
  bool has_downloads; qa_app_qw_downloads downloads;
  bool has_skins; qa_app_qw_skins_host skins;
  void (*prediction_sent)(void *ctx, double sequence, const qa_qw_user_command *command, double now_milliseconds);
  void (*prediction_acknowledged)(void *ctx, double sequence, double now_milliseconds);
  void (*server_data)(void *ctx, const void *data);
  double (*game_state)(void *ctx, const void *data, const char *const *models, size_t model_count,
      const char *const *sounds, size_t sound_count);
  void (*receive)(void *ctx, const void *const *messages, size_t count, double now_milliseconds);
  qa_qw_user_command (*command)(void *ctx, const void *actor_command);
  bool (*take_spectator_teleport)(void *ctx, qa_vec3 *out); /* NULL fn/false = TS null */
  void (*disconnected)(void *ctx, const char *reason);
  void (*print)(void *ctx, const char *text);
} qa_app_qw_client_host;
#endif
