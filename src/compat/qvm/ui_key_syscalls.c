/* See include/qa/compat/qvm/ui_key_syscalls.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/ui_key_syscalls.h"

#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"

/* Landed QvmUiImport / QvmUiExport values (TS abi.ts). */
enum {
  UI_GET_CDKEY = QA_QVM_UI_IMPORT_UI_GET_CDKEY,
  UI_SET_CDKEY = QA_QVM_UI_IMPORT_UI_SET_CDKEY,
  UI_VERIFY_CDKEY = QA_QVM_UI_IMPORT_UI_VERIFY_CDKEY,
  UI_HASUNIQUECDKEY = QA_QVM_UI_EXPORT_UI_HASUNIQUECDKEY,
};

typedef struct write_target {
  qa_qvm_memory *guest;
  int64_t offset;
} write_target;

static void copy_write(void *ctx, const uint8_t *bytes, size_t length) {
  write_target *target = ctx;
  qa_qvm_memory_write_bytes(target->guest, target->offset, bytes, length);
}

static void set_byte_write(void *ctx, size_t index, uint8_t value) {
  write_target *target = ctx;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(target->guest, target->offset + (int64_t)index, 1, &view);
  qa_qvm_writable_view_set_uint8(&view, 0, value);
}

static void assert_validate(void *ctx) {
  const qa_qvm_ui_key_services *services = ctx;
  services->assert_current(services->ctx);
}

bool qa_qvm_ui_key_syscall(const qa_qvm_host_call *call, const qa_qvm_ui_key_services *services, int32_t *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "ui") != 0) return false;
  services->assert_current(services->ctx);
  switch (call->code) {
    case UI_GET_CDKEY:
    case UI_SET_CDKEY: {
      int32_t pointer = qa_qvm_host_call_word(call, 1);
      bool read = call->code == UI_GET_CDKEY;
      int32_t args[10] = {UI_HASUNIQUECDKEY, 0, 0, 0, 0, 0, 0, 0, 0, 0};
      int32_t unique = call->base.invoke_async(call->base.call_ctx, args, 10, 0,
                                               assert_validate, (void *)services);
      services->assert_current(services->ctx);
      if (read) {
        const char *directory = services->game_directory(services->ctx);
        qa_qvm_memory_span destination = qa_qvm_memory_get_span(call->guest, pointer, 17, 0);
        write_target target = {call->guest, destination.byte_offset};
        qa_q3_key_ui_writes writes = {&target, copy_write, set_byte_write};
        qa_q3_cd_key_state_read_ui(services->keys, unique, directory,
                                   call->guest->bytes + destination.byte_offset, 17, &writes);
      } else {
        qa_qvm_memory_span source = qa_qvm_memory_get_span(call->guest, pointer, 16, 0);
        services->write_ui(services->ctx, unique, services->game_directory(services->ctx),
                           call->guest->bytes + source.byte_offset, 16);
      }
      services->assert_current(services->ctx);
      *out = 0;
      return true;
    }
    case UI_VERIFY_CDKEY: {
      char *key = qa_qvm_memory_read_string(call->guest, qa_qvm_host_call_word(call, 1));
      if (strlen(key) != 16) {
        free(key);
        *out = 0;
        return true;
      }
      int32_t checksum_word = qa_qvm_host_call_word(call, 2);
      qa_qvm_memory_span span;
      char *checksum = NULL;
      if (qa_qvm_memory_pointer(call->guest, checksum_word, &span))
        checksum = qa_qvm_memory_read_string(call->guest, checksum_word);
      bool valid = qa_validate_q3_cd_key(key, checksum);
      free(key);
      free(checksum);
      *out = valid ? 1 : 0;
      return true;
    }
    default: return false;
  }
}
