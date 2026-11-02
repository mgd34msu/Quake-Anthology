/*
 * Tests for src/compat/qvm/mod-provider.ts (validateQvmMod and
 * validateQvmModCheckpoint). The TS provider execution cases
 * (tests/compat/qvm/mod-pickup-stores.test.ts) drive a full session
 * world; the asset-free deterministic surface is the declaration
 * and checkpoint validation exercised here, mirroring the error
 * cases in mod-provider.ts. GPL-2.0-or-later.
 */
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

#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/mod_provider.h"
#include "qa/persistence/value.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    checks++;                                                          \
    if (!(cond)) {                                                     \
      failures++;                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
    }                                                                  \
  } while (0)

#define CHECK_ABORTS(...)                                              \
  do {                                                                 \
    checks++;                                                          \
    pid_t pid = fork();                                                \
    if (pid == 0) {                                                    \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                               \
      __VA_ARGS__;                                                     \
      _exit(0);                                                        \
    }                                                                  \
    int status = 0;                                                    \
    waitpid(pid, &status, 0);                                          \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {       \
      failures++;                                                      \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,    \
              __LINE__, #__VA_ARGS__);                                 \
    }                                                                  \
  } while (0)

#define DIGEST \
  "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

static qa_qvm_instruction instructions[] = {
    {.opcode = QA_QVM_OP_ENTER, .operand_width = 4, .operand = 0},
    {.opcode = QA_QVM_OP_LEAVE, .operand_width = 4, .operand = 0},
    {.opcode = QA_QVM_OP_ENTER, .operand_width = 4, .operand = 0},
};

static qa_qvmy_artifact make_artifact(void) {
  qa_qvmy_artifact artifact;
  memset(&artifact, 0, sizeof artifact);
  artifact.module.id = "q3:test:game";
  artifact.module.artifact_path = "vm/qagame.qvm";
  artifact.module.digest = DIGEST;
  artifact.module.revision = "1";
  artifact.role = "qagame";
  artifact.has_abi_profile = true;
  artifact.abi_profile = "q3-modern";
  artifact.instructions = instructions;
  artifact.instruction_count = 3;
  artifact.data_length = 4096;
  artifact.literal_length = 0;
  artifact.bss_length = 4096;
  artifact.allocated_data_length = 8192;
  return artifact;
}

static qa_qvm_mod_actor_field fields[] = {
    {.offset = 0, .binding = "health", .encoding = "int32"},
    {.offset = 16, .binding = "origin"},
};
static qa_qvm_mod_actor_record records[] = {
    {"ents", 256, 64, 4, fields, 2},
};
static qa_qvm_mod_source_call initialize_calls[] = {
    {0, NULL, 0, NULL, 0, "void"},
};
static qa_qvm_mod_callback callbacks[] = {
    {{"think:test", "actor.think", "observe", NULL}, {0, NULL, 0, NULL, 0, "void"}},
};

static qa_qvm_mod_callback_declaration make_declaration(void) {
  qa_qvm_mod_callback_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  declaration.version = 1;
  declaration.runtime = "qvm";
  declaration.program.path = "vm/qagame.qvm";
  declaration.program.digest = DIGEST;
  declaration.abi_profile = "q3-modern";
  declaration.has_spawn_entities = true;
  declaration.spawn_entities = "";
  declaration.actor_records = records;
  declaration.actor_record_count = 1;
  declaration.initialize = initialize_calls;
  declaration.initialize_count = 1;
  declaration.callbacks = callbacks;
  declaration.callback_count = 1;
  return declaration;
}

static void test_validate_basics(void) {
  qa_qvmy_artifact artifact = make_artifact();
  qa_qvm_mod_callback_declaration declaration = make_declaration();
  qa_qvm_validate_mod(&artifact, &declaration);

  qa_qvm_mod_callback_declaration wrong = declaration;
  wrong.program.digest =
      "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  CHECK_ABORTS(qa_qvm_validate_mod(&artifact, &wrong));

  wrong = declaration;
  wrong.abi_profile = "q3-legacy-client";
  CHECK_ABORTS(qa_qvm_validate_mod(&artifact, &wrong));

  qa_qvm_mod_actor_record bad_records[] = {{"ents", 256, 64, 0, fields, 2}};
  wrong = declaration;
  wrong.actor_records = bad_records;
  CHECK_ABORTS(qa_qvm_validate_mod(&artifact, &wrong));

  qa_qvm_mod_actor_record overlap_records[] = {
      {"ents", 256, 64, 4, fields, 2},
      {"more", 288, 64, 4, fields, 2},
  };
  wrong = declaration;
  wrong.actor_records = overlap_records;
  wrong.actor_record_count = 2;
  CHECK_ABORTS(qa_qvm_validate_mod(&artifact, &wrong));

  qa_qvm_mod_actor_field wide_fields[] = {
      {.offset = 60, .binding = "origin"},
  };
  qa_qvm_mod_actor_record wide_records[] = {{"ents", 256, 64, 4, wide_fields, 1}};
  wrong = declaration;
  wrong.actor_records = wide_records;
  CHECK_ABORTS(qa_qvm_validate_mod(&artifact, &wrong));

  qa_qvm_mod_actor_field dup_fields[] = {
      {.offset = 0, .binding = "health", .encoding = "int32"},
      {.offset = 4, .binding = "health", .encoding = "int32"},
  };
  qa_qvm_mod_actor_record dup_records[] = {{"ents", 256, 64, 4, dup_fields, 2}};
  wrong = declaration;
  wrong.actor_records = dup_records;
  CHECK_ABORTS(qa_qvm_validate_mod(&artifact, &wrong));

  qa_qvm_mod_actor_field const_fields[] = {
      {.offset = 0, .binding = "constant", .encoding = "int32", .constant_value = 3e9},
  };
  qa_qvm_mod_actor_record const_records[] = {{"ents", 256, 64, 4, const_fields, 1}};
  wrong = declaration;
  wrong.actor_records = const_records;
  CHECK_ABORTS(qa_qvm_validate_mod(&artifact, &wrong));

  wrong = declaration;
  wrong.entity_record = "missing";
  CHECK_ABORTS(qa_qvm_validate_mod(&artifact, &wrong));

  qa_qvm_mod_source_call bad_calls[] = {{1, NULL, 0, NULL, 0, "void"}};
  wrong = declaration;
  wrong.initialize = bad_calls;
  CHECK_ABORTS(qa_qvm_validate_mod(&artifact, &wrong));

  qa_qvm_mod_callback dup_callbacks[] = {
      callbacks[0],
      callbacks[0],
  };
  wrong = declaration;
  wrong.callbacks = dup_callbacks;
  wrong.callback_count = 2;
  CHECK_ABORTS(qa_qvm_validate_mod(&artifact, &wrong));

  qa_qvm_mod_callback void_callbacks[] = {
      {{"think:test", "actor.think", "replace", "boolean"},
       {0, NULL, 0, NULL, 0, "void"}},
  };
  wrong = declaration;
  wrong.callbacks = void_callbacks;
  CHECK_ABORTS(qa_qvm_validate_mod(&artifact, &wrong));
}

static qa_save_value *host_tree(void) {
  qa_save_value *root = qa_save_value_object();
  qa_save_value_set(root, "version", qa_save_value_number(1));
  qa_save_value_set(root, "nextSlot", qa_save_value_number(0));
  qa_save_value_set(root, "projections", qa_save_value_array());
  qa_save_value *defaults = qa_save_value_array();
  qa_save_value *entry = qa_save_value_object();
  uint8_t zeros[256] = {0};
  qa_save_value_set(entry, "id", qa_save_value_string("ents"));
  qa_save_value_set(entry, "bytes", qa_save_value_bytes(zeros, sizeof zeros));
  qa_save_value_push(defaults, entry);
  qa_save_value_set(root, "defaults", defaults);
  qa_save_value_set(root, "configstrings", qa_save_value_array());
  return root;
}

static qa_guest_checkpoint make_checkpoint(const qa_qvmy_artifact *artifact,
                                           uint8_t **out_bytes, size_t *out_length,
                                           uint8_t *data) {
  qa_save_value *tree = host_tree();
  *out_bytes = qa_encode_checkpoint_value(tree, out_length);
  qa_save_value_free(tree);
  qa_guest_checkpoint checkpoint;
  memset(&checkpoint, 0, sizeof checkpoint);
  checkpoint.kind = QA_GUEST_CHECKPOINT_QVM;
  checkpoint.module = artifact->module;
  checkpoint.q3_api = qa_qvm_api("qagame", "q3-modern");
  checkpoint.data.bytes = data;
  checkpoint.data.length = (size_t)artifact->allocated_data_length;
  checkpoint.program_stack = (double)artifact->allocated_data_length;
  checkpoint.host_state.module = artifact->module;
  checkpoint.host_state.format = "qvm:mod-host-v1";
  checkpoint.host_state.bytes.bytes = *out_bytes;
  checkpoint.host_state.bytes.length = *out_length;
  return checkpoint;
}

static void test_validate_checkpoint(void) {
  qa_qvmy_artifact artifact = make_artifact();
  qa_qvm_mod_callback_declaration declaration = make_declaration();
  uint8_t *data = calloc(1, 8192);
  uint8_t *bytes = NULL;
  size_t length = 0;
  qa_guest_checkpoint checkpoint = make_checkpoint(&artifact, &bytes, &length, data);
  CHECK(bytes != NULL);
  qa_qvm_validate_mod_checkpoint(&artifact, &declaration, &checkpoint);

  qa_guest_checkpoint wrong = checkpoint;
  wrong.module.digest =
      "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  CHECK_ABORTS(qa_qvm_validate_mod_checkpoint(&artifact, &declaration, &wrong));

  wrong = checkpoint;
  wrong.program_stack = 0;
  CHECK_ABORTS(qa_qvm_validate_mod_checkpoint(&artifact, &declaration, &wrong));

  wrong = checkpoint;
  wrong.host_state.format = "qvm:other-v1";
  CHECK_ABORTS(qa_qvm_validate_mod_checkpoint(&artifact, &declaration, &wrong));

  free(bytes);
  free(data);
}

int main(void) {
  test_validate_basics();
  test_validate_checkpoint();
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d checks passed\n", checks);
  return 0;
}
