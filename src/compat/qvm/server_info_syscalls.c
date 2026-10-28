/* See include/qa/compat/qvm/server_info_syscalls.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/server_info_syscalls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/legacy_presentation.h"
#include "qa/core/common_error.h"

/* Landed QvmGameImport values (TS abi.ts). */
enum {
  G_SET_CONFIGSTRING = QA_QVM_GAME_IMPORT_G_SET_CONFIGSTRING,
  G_GET_CONFIGSTRING = QA_QVM_GAME_IMPORT_G_GET_CONFIGSTRING,
  G_GET_SERVERINFO = QA_QVM_GAME_IMPORT_G_GET_SERVERINFO,
};

static void capacity(int32_t size, const char *operation) {
  if (size < 1) {
    char message[128];
    snprintf(message, sizeof message, "%s: bufferSize == %d", operation, (int)size);
    qa_common_error(QA_COMMON_ERROR_DROP, message);
  }
}

static void config_index(int32_t index, const char *operation) {
  if (index < 0 || index >= 1024) {
    char message[128];
    snprintf(message, sizeof message, "%s: bad index %d\n", operation, (int)index);
    qa_common_error(QA_COMMON_ERROR_DROP, message);
  }
}

bool qa_qvm_server_information_syscall(const qa_qvm_host_call *call,
                                       const qa_qvm_server_information_services *services, int32_t *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") != 0) return false;
  switch (call->code) {
    case G_SET_CONFIGSTRING: {
      config_index(qa_qvm_host_call_word(call, 1), "SV_SetConfigstring");
      int32_t mapped = qa_qvm_legacy_configstring(qa_qvm_host_call_word(call, 1), services->abi_profile);
      if (qa_qvm_host_call_word(call, 2) == 0) {
        services->configstring_set(services->configstrings_ctx, mapped, "");
      } else {
        char *value = qa_qvm_memory_read_string(call->guest, qa_qvm_host_call_word(call, 2));
        services->configstring_set(services->configstrings_ctx, mapped, value);
        free(value);
      }
      *out = 0;
      return true;
    }
    case G_GET_CONFIGSTRING: {
      capacity(qa_qvm_host_call_word(call, 3), "SV_GetConfigstring");
      config_index(qa_qvm_host_call_word(call, 1), "SV_GetConfigstring");
      int32_t mapped = qa_qvm_legacy_configstring(qa_qvm_host_call_word(call, 1), services->abi_profile);
      const char *value = services->configstring_get(services->configstrings_ctx, mapped);
      qa_qvm_memory_write_string(call->guest, qa_qvm_host_call_word(call, 2), value,
                                 qa_qvm_host_call_word(call, 3));
      *out = 0;
      return true;
    }
    case G_GET_SERVERINFO: {
      capacity(qa_qvm_host_call_word(call, 2), "SV_GetServerinfo");
      char *info = qa_cvar_registry_info_string(services->cvars, QA_CVAR_FLAG_SERVER_INFO, -1);
      qa_qvm_memory_write_string(call->guest, qa_qvm_host_call_word(call, 1), info != NULL ? info : "",
                                 qa_qvm_host_call_word(call, 2));
      free(info);
      *out = 0;
      return true;
    }
    default: return false;
  }
}
