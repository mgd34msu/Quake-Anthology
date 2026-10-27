/* Port of src/compat/qvm/client-game-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_game_syscalls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"

static void client(double slot, const qa_qvm_client_game_services *services, const char *operation) {
  if (slot < 0 || slot >= services->max_clients) {
    char message[128];
    if (strcmp(operation, "SV_GetUsercmd") == 0)
      snprintf(message, sizeof message, "%s: bad clientNum:%d", operation, (int)slot);
    else
      snprintf(message, sizeof message, "%s: bad index %d\n", operation, (int)slot);
    qa_common_error(QA_COMMON_ERROR_DROP, message);
  }
}

bool qa_qvm_client_game_syscall(const qa_qvm_host_call *call,
                                qa_qvm_client_game_services services, qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") != 0) return false;
  qa_qvm_memory *guest = call->guest;
  switch (call->code) {
    case QA_QVM_GAME_IMPORT_G_DROP_CLIENT: {
      int32_t number = qa_qvm_host_call_word(call, 1);
      if (number < 0 || number >= services.max_clients) {
        *out = 0;
        return true;
      }
      char *reason = qa_qvm_memory_read_string(guest, qa_qvm_host_call_word(call, 2));
      if (reason == NULL) qa_range_error("out of memory");
      services.drop_client(services.ctx, number, reason);
      free(reason);
      *out = 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_G_SEND_SERVER_COMMAND: {
      int32_t number = qa_qvm_host_call_word(call, 1);
      if (number != -1 && (number < 0 || number >= services.max_clients)) {
        *out = 0;
        return true;
      }
      char *text = qa_qvm_memory_read_string(guest, qa_qvm_host_call_word(call, 2));
      if (text == NULL) qa_range_error("out of memory");
      services.send_server_command(services.ctx, number, text);
      free(text);
      *out = 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_G_GET_USERINFO: {
      int32_t size = qa_qvm_host_call_word(call, 3);
      if (size < 1) {
        char message[96];
        snprintf(message, sizeof message, "SV_GetUserinfo: bufferSize == %d", (int)size);
        qa_common_error(QA_COMMON_ERROR_DROP, message);
      }
      int32_t slot = qa_qvm_host_call_word(call, 1);
      client(slot, &services, "SV_GetUserinfo");
      qa_qvm_memory_write_string(guest, qa_qvm_host_call_word(call, 2),
                                 services.get_userinfo(services.ctx, slot), size);
      *out = 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_G_SET_USERINFO: {
      int32_t slot = qa_qvm_host_call_word(call, 1);
      client(slot, &services, "SV_SetUserinfo");
      int32_t pointer = qa_qvm_host_call_word(call, 2);
      if (pointer == 0) {
        services.set_userinfo(services.ctx, slot, "");
      } else {
        char *value = qa_qvm_memory_read_string(guest, pointer);
        if (value == NULL) qa_range_error("out of memory");
        services.set_userinfo(services.ctx, slot, value);
        free(value);
      }
      *out = 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_G_GET_USERCMD: {
      int32_t slot = qa_qvm_host_call_word(call, 1);
      client(slot, &services, "SV_GetUsercmd");
      qa_qvm_writable_view writable;
      qa_qvm_memory_view(guest, qa_qvm_host_call_word(call, 2), QA_QVM_USER_COMMAND_BYTES, 0,
                         &writable);
      qa_qvmx_view view = {writable.bytes + writable.start, (size_t)writable.length};
      qa_qvmx_wire_user_command command = services.get_user_command(services.ctx, slot);
      qa_qvm_write_user_command(view, &command, services.abi_profile, NULL);
      *out = 0;
      return true;
    }
    default: return false;
  }
}
