/* Tests for the weapon-behavior tool bodies
 * (src/app/bootstrap/weapon-behavior-tool.ts): help dispatch, the QVM
 * inspect report over a real loose mount, and the family guards.
 * GPL-2.0-or-later. */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/app/bootstrap/weapon_behavior_tool.h"
#include "qa/compat/qvm/image.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"

static int checks, failures;
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

typedef struct capture {
  char text[16384];
  size_t length;
} capture;
static void capture_print(void *ctx, const char *text) {
  capture *out = (capture *)ctx;
  size_t length = strlen(text);
  if (out->length + length >= sizeof out->text) length = sizeof out->text - 1 - out->length;
  memcpy(out->text + out->length, text, length);
  out->length += length;
  out->text[out->length] = '\0';
}

typedef struct fake_host {
  const char *family;
  const char *edition;
  const char *root;
  qa_content_mounted *mounted;
  int open_calls;
} fake_host;
static const void *fake_open_product(void *ctx,
                                     const qa_app_weapon_behavior_tool_command *command,
                                     const char **out_family, const char **out_edition,
                                     const char **out_expectation_id,
                                     const char **out_content_directory,
                                     const char **out_product_id) {
  (void)command;
  fake_host *host = (fake_host *)ctx;
  host->open_calls++;
  qa_content_mount mount;
  memset(&mount, 0, sizeof mount);
  mount.kind = QA_CONTENT_MOUNT_LOOSE;
  mount.as.loose.identity =
      qa_create_mount_identity("mount:test:loose", "q3:base:baseq3:installed", 0);
  mount.as.loose.root_path = host->root;
  qa_mount_id order[1] = {"mount:test:loose"};
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:test:1";
  plan.mounts = &mount;
  plan.mount_count = 1;
  plan.default_order = order;
  plan.default_order_count = 1;
  host->mounted = qa_open_mount_plan(&plan, NULL);
  *out_family = host->family;
  *out_edition = host->edition;
  *out_expectation_id = "q3-test-product";
  *out_content_directory = "baseq3";
  *out_product_id = "q3:base:baseq3:installed";
  return host->mounted;
}
static void fake_close_product(void *ctx, const void *content) {
  fake_host *host = (fake_host *)ctx;
  (void)content;
  qa_content_mounted_destroy(host->mounted);
  host->mounted = NULL;
}
static qa_app_weapon_behavior_tool_host make_host(fake_host *fake) {
  qa_app_weapon_behavior_tool_host host = {0};
  host.ctx = fake;
  host.open_product = fake_open_product;
  host.close_product = fake_close_product;
  return host;
}
static qa_app_weapon_behavior_tool_command make_command(const char *action) {
  qa_app_weapon_behavior_tool_command command = {0};
  command.action = action;
  command.product = (char *)"q3:base:baseq3:installed";
  command.corpus_root = (char *)"/tmp";
  command.user_content_root = (char *)"/tmp";
  return command;
}

static void put_i32(uint8_t *out, size_t offset, int32_t value) {
  out[offset] = (uint8_t)(value & 0xff);
  out[offset + 1] = (uint8_t)((value >> 8) & 0xff);
  out[offset + 2] = (uint8_t)((value >> 16) & 0xff);
  out[offset + 3] = (uint8_t)((value >> 24) & 0xff);
}
/* Minimal QVM: ENTER/LEAVE, ENTER/LEAVE (instructionCount 4,
 * OP_ENTER at indexes 0 and 2, bss 4096). */
static size_t build_qvm(uint8_t *out) {
  const struct { qa_qvm_opcode opcode; int32_t operand; } ops[] = {
      {QA_QVM_OP_ENTER, 16}, {QA_QVM_OP_LEAVE, 16},
      {QA_QVM_OP_ENTER, 8}, {QA_QVM_OP_LEAVE, 8}};
  uint8_t code[64];
  size_t code_length = 0;
  for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
    code[code_length++] = (uint8_t)ops[i].opcode;
    put_i32(code, code_length, ops[i].operand);
    code_length += 4;
  }
  const int32_t header[8] = {0x12721444, 4, 32, (int32_t)code_length,
                             32 + (int32_t)code_length, 0, 0, 4096};
  for (size_t i = 0; i < 8; i++) put_i32(out, i * 4, header[i]);
  memcpy(out + 32, code, code_length);
  return 32 + code_length;
}

static bool write_file(const char *path, const uint8_t *bytes, size_t length) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) return false;
  bool ok = fwrite(bytes, 1, length, file) == length;
  fclose(file);
  return ok;
}

int main(void) {
  char root_template[] = "/tmp/qa-weapon-tool-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  char empty_template[] = "/tmp/qa-weapon-tool-empty-XXXXXX";
  char *empty = mkdtemp(empty_template);
  CHECK(empty != NULL);
  if (root == NULL || empty == NULL) return 1;
  char vm_dir[512], qvm_path[512];
  snprintf(vm_dir, sizeof vm_dir, "%s/vm", root);
  mkdir(vm_dir, 0777);
  snprintf(qvm_path, sizeof qvm_path, "%s/vm/qagame.qvm", root);
  uint8_t qvm[256];
  size_t qvm_length = build_qvm(qvm);
  CHECK(write_file(qvm_path, qvm, qvm_length));

  /* help: prints the tool help without opening a product. */
  {
    fake_host fake = {"q3", "base", root, NULL, 0};
    capture out = {0};
    qa_app_run_weapon_behavior_tool(make_host(&fake), make_command("help"),
                                    capture_print, &out);
    CHECK(fake.open_calls == 0);
    CHECK(strncmp(out.text, "Usage:\n  quake-typescript weapon-behavior inspect",
                  48) == 0);
  }

  /* QVM inspect over the loose mount (declared document absent). */
  {
    fake_host fake = {"q3", "base", root, NULL, 0};
    capture out = {0};
    qa_app_run_weapon_behavior_tool(make_host(&fake), make_command("inspect"),
                                    capture_print, &out);
    CHECK(fake.open_calls == 1);
    CHECK(strstr(out.text, "\"product\": \"q3-test-product\"") != NULL);
    CHECK(strstr(out.text, "\"artifact\": \"vm/qagame.qvm\"") != NULL);
    CHECK(strstr(out.text, "\"digest\": \"sha256:") != NULL);
    CHECK(strstr(out.text, "\"instructionCount\": 4") != NULL);
    CHECK(strstr(out.text, "\"dataBytes\": 4096") != NULL);
    CHECK(strstr(out.text, "\"instructionIndex\": 0") != NULL);
    CHECK(strstr(out.text, "\"instructionIndex\": 2") != NULL);
    CHECK(strstr(out.text, "\"instructionIndex\": 1") == NULL);
    CHECK(strstr(out.text, "\"declared\": []") != NULL);
    CHECK(out.length > 2 && strcmp(out.text + out.length - 2, "}\n") == 0);
  }

  /* Guards abort with the TS Errors. */
  {
    fake_host fake = {"q2", "classic", root, NULL, 0};
    capture out = {0};
    CHECK_ABORTS(qa_app_run_weapon_behavior_tool(
        make_host(&fake), make_command("inspect"), capture_print, &out));
  }
  {
    fake_host fake = {"q1", "base", root, NULL, 0};
    capture out = {0};
    qa_app_weapon_behavior_tool_command command = make_command("declare-qvm");
    command.profile = (char *)"profile.json";
    CHECK_ABORTS(qa_app_run_weapon_behavior_tool(make_host(&fake), command,
                                                 capture_print, &out));
  }
  {
    /* QC inspect with no progs.dat mounted: missing-artifact Error. */
    fake_host fake = {"q1", "base", empty, NULL, 0};
    capture out = {0};
    CHECK_ABORTS(qa_app_run_weapon_behavior_tool(
        make_host(&fake), make_command("inspect"), capture_print, &out));
  }
  {
    /* QVM inspect with no qagame.qvm mounted. */
    fake_host fake = {"q3", "base", empty, NULL, 0};
    capture out = {0};
    CHECK_ABORTS(qa_app_run_weapon_behavior_tool(
        make_host(&fake), make_command("inspect"), capture_print, &out));
  }

  printf("weapon_behavior_tool: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
