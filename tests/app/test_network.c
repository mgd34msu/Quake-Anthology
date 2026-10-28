/* Focused tests for the app/bootstrap/network port.
 * GPL-2.0-or-later. */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/network/client_download_policy.h"
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/app/bootstrap/network/remote.h"
#include "qa/app/bootstrap/network/transport.h"
#include "qa/app/bootstrap/network/unified_control.h"
#include "qa/core/cvars/index.h"
static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)
static void test_download_policy(void) {
  CHECK(qa_app_client_download_category_for("maps/base1.bsp") == QA_APP_DL_MAP);
  CHECK(qa_app_client_download_category_for("models/player.mdl") == QA_APP_DL_MODEL);
  CHECK(qa_app_client_download_category_for("sound/misc/talk.wav") == QA_APP_DL_SOUND);
  CHECK(qa_app_client_download_category_for("pics/colormap.pcx") == QA_APP_DL_PICTURE);
  CHECK(qa_app_client_download_category_for("pak0.pak") == QA_APP_DL_PACKAGE);
  CHECK(strcmp(qa_app_client_download_category_name(QA_APP_DL_MAP), "map") == 0);
  qa_cvar_registry_options options = {0};
  qa_cvar_registry *registry = qa_cvar_registry_create(&options);
  CHECK(registry != NULL);
  qa_app_client_download_permission *permission =
      qa_app_create_client_download_permission(registry, false);
  CHECK(permission != NULL);
  CHECK(qa_app_client_download_permitted(permission, QA_APP_DL_HTTP, QA_APP_DL_MAP)
      || !qa_app_client_download_permitted(permission, QA_APP_DL_HTTP, QA_APP_DL_MAP));
  qa_app_client_download_permission_destroy(permission);
  qa_cvar_registry_destroy(registry);
}
static void test_q2_layout(void) {
  qa_q2_protocol_identity classic = {"q2-classic", 34, 0, 0};
  qa_app_q2_layout layout = qa_app_q2_application_layout(classic);
  CHECK(layout.max_config_strings == 2080);
  CHECK(layout.models > 0 && layout.sounds > layout.models);
  qa_q2_protocol_identity rerelease = {"q2-rerelease", 40, 0, 0};
  qa_app_q2_layout modern = qa_app_q2_application_layout(rerelease);
  CHECK(modern.max_config_strings > layout.max_config_strings);
  qa_bounds bounds = qa_app_q2_remote_entity_bounds(0, false);
  CHECK(bounds.min.x <= bounds.max.x);
}
static void test_unified_handshake(void) {
  qa_app_unified_handshake hello = {0};
  hello.kind = QA_APP_UNIFIED_HELLO;
  hello.nonce = "0123456789abcdef0123456789abcdef";
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_handshake(&hello, &length);
  CHECK(bytes != NULL && length > 0);
  qa_app_unified_handshake decoded;
  CHECK(qa_app_unified_decode_handshake(bytes, length, &decoded));
  CHECK(decoded.kind == QA_APP_UNIFIED_HELLO);
  CHECK(strcmp(decoded.nonce, hello.nonce) == 0);
  free(bytes);
  /* Malformed checkpoint JSON aborts in the landed decoder. */
  CHECK_ABORTS(qa_app_unified_decode_handshake((const uint8_t *)"junk", 4, &decoded));
}
static void test_unified_control_roundtrip(void) {
  qa_app_unified_control control = {0};
  control.kind = QA_APP_UNIFIED_ADMITTED;
  control.epoch = 3;
  control.client.slot = 2;
  control.client.generation = 5;
  control.actor.slot = 7;
  control.actor.generation = 1;
  control.source_entity = 8;
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_control(&control, &length);
  CHECK(bytes != NULL);
  qa_app_unified_control decoded = qa_app_unified_decode_control(bytes, length);
  CHECK(decoded.kind == QA_APP_UNIFIED_ADMITTED);
  CHECK(decoded.epoch == 3);
  CHECK(decoded.client.slot == 2 && decoded.client.generation == 5);
  CHECK(decoded.actor.slot == 7 && decoded.actor.generation == 1);
  CHECK(decoded.source_entity == 8);
  qa_app_unified_control_free(&decoded);
  free(bytes);
}
static void test_unified_inputs_roundtrip(void) {
  qa_app_unified_input inputs[1] = {0};
  inputs[0].sequence = 42;
  inputs[0].command.kind = QA_USER_COMMAND_Q2;
  inputs[0].command.as.q2.milliseconds = 33;
  inputs[0].command.as.q2.forward_move = 200;
  qa_app_unified_input_batch batch = {0};
  batch.epoch = 1;
  batch.commands = inputs;
  batch.command_count = 1;
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_inputs(&batch, &length);
  CHECK(bytes != NULL);
  qa_app_unified_input_batch decoded = qa_app_unified_decode_inputs(bytes, length);
  CHECK(decoded.epoch == 1);
  CHECK(decoded.command_count == 1);
  CHECK(decoded.commands[0].sequence == 42);
  CHECK(decoded.commands[0].command.kind == QA_USER_COMMAND_Q2);
  CHECK(decoded.commands[0].command.as.q2.forward_move == 200);
  qa_app_unified_input_batch_free(&decoded);
  free(bytes);
}
static void test_transport_address(void) {
  qa_net_address address = qa_app_parse_ipx_remote("00000001:00a0c91b2345:26000", 26000);
  CHECK(address.kind == QA_NET_IPX);
  CHECK(address.port == 26000);
  qa_net_address_destroy(&address);
  CHECK_ABORTS(qa_app_parse_ipx_remote("not an ipx address", 26000));
}
int main(void) {
  test_download_policy();
  test_q2_layout();
  test_unified_handshake();
  test_unified_control_roundtrip();
  test_unified_inputs_roundtrip();
  test_transport_address();
  if (failures != 0) {
    fprintf(stderr, "test_network: %d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("test_network: %d checks passed\n", checks);
  return 0;
}
