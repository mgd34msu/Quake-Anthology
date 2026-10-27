/* Port of src/compat/qvm/client-state-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_state_syscalls.h"

#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/legacy_presentation.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static qa_qvmx_view guest_view(const qa_qvm_host_call *call, int32_t word, int64_t length,
                               qa_qvm_writable_view *storage) {
  qa_qvm_memory_view(call->guest, word, length, 0, storage);
  qa_qvmx_view view = {storage->bytes + storage->start, (size_t)storage->length};
  return view;
}

bool qa_qvm_client_state_syscall(const qa_qvm_host_call *call,
                                 qa_qvm_client_state_services services, qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0) return false;
  qa_qvm_abi_profile profile = call->has_abi_profile ? call->abi_profile : "q3-modern";
  qa_qvmx_memory memory = {call->guest->bytes, call->guest->length};
  if (strcmp(call->role, "ui") == 0 && call->code == QA_QVM_UI_IMPORT_UI_GETCONFIGSTRING) {
    int32_t index = qa_qvm_host_call_word(call, 1);
    int32_t pointer = qa_qvm_host_call_word(call, 2);
    int32_t size = qa_qvm_host_call_word(call, 3);
    if (index < 0 || index >= 1024) {
      *out = 0;
      return true;
    }
    const char *value = services.connection.game_state.get(
        services.connection.game_state.ctx, qa_qvm_legacy_configstring(index, profile));
    if (value == NULL) {
      if (size != 0) {
        qa_qvm_writable_view storage;
        qa_qvmx_view view = guest_view(call, pointer, 1, &storage);
        qa_qvmx_view_set_u8(&view, 0, 0);
      }
      *out = 0;
      return true;
    }
    qa_qvm_memory_write_string(call->guest, pointer, value, size);
    *out = 1;
    return true;
  }
  if (strcmp(call->role, "cgame") != 0) return false;
  switch (call->code) {
    case QA_QVM_CGAME_IMPORT_CG_GETGAMESTATE: {
      qa_qvm_writable_view storage;
      qa_qvmx_view view = guest_view(call, qa_qvm_host_call_word(call, 1), QA_QVM_GAME_STATE_BYTES, &storage);
      qa_qvmx_source_game_state_record record =
          services.connection.game_state.copy_source_record(services.connection.game_state.ctx);
      qa_qvm_write_game_state(&memory, view, &record, profile);
      *out = 0;
      return true;
    }
    case QA_QVM_CGAME_IMPORT_CG_GETCURRENTSNAPSHOTNUMBER: {
      qa_qvm_writable_view number_storage, time_storage;
      qa_qvmx_view number = guest_view(call, qa_qvm_host_call_word(call, 1), 4, &number_storage);
      qa_qvmx_view time = guest_view(call, qa_qvm_host_call_word(call, 2), 4, &time_storage);
      qa_qvmx_snapshot_current current = services.snapshots.current(services.snapshots.ctx);
      qa_qvmx_view_set_i32(&number, 0, qa_int32(current.number));
      qa_qvmx_view_set_i32(&time, 0, qa_int32(current.server_time));
      *out = 0;
      return true;
    }
    case QA_QVM_CGAME_IMPORT_CG_GETSNAPSHOT: {
      int32_t number = qa_qvm_host_call_word(call, 1);
      int32_t pointer = qa_qvm_host_call_word(call, 2);
      qa_qvmx_snapshot snapshot;
      if (!services.snapshots.read(services.snapshots.ctx, number, &snapshot)) {
        *out = 0;
        return true;
      }
      double ping;
      if (!services.snapshot_ping(services.ctx, number, &ping))
        qa_range_error("Retained snapshot has no source ping");
      qa_qvm_writable_view storage;
      qa_qvmx_view view = guest_view(call, pointer, (int64_t)qa_qvm_snapshot_bytes_for(profile), &storage);
      qa_qvm_write_snapshot(&memory, view, &snapshot, ping, profile);
      *out = 1;
      return true;
    }
    case QA_QVM_CGAME_IMPORT_CG_GETSERVERCOMMAND: {
      const char *const *argv = NULL;
      size_t count = 0;
      bool present =
          services.get_server_command(services.ctx, qa_qvm_host_call_word(call, 1), &argv, &count);
      *out = present ? 1 : 0;
      return true;
    }
    case QA_QVM_CGAME_IMPORT_CG_GETCURRENTCMDNUMBER:
      *out = (qa_qvm_host_result)services.connection.commands.get_current_number(
          services.connection.commands.ctx);
      return true;
    case QA_QVM_CGAME_IMPORT_CG_GETUSERCMD: {
      int32_t number = qa_qvm_host_call_word(call, 1);
      int32_t pointer = qa_qvm_host_call_word(call, 2);
      qa_qvmx_wire_user_command command;
      bool present = services.user_command == NULL
                         ? services.connection.commands.read(services.connection.commands.ctx,
                                                             number, &command)
                         : services.user_command(services.ctx, number, &command);
      if (!present) {
        *out = 0;
        return true;
      }
      qa_qvm_writable_view storage;
      qa_qvmx_view view = guest_view(call, pointer, QA_QVM_USER_COMMAND_BYTES, &storage);
      qa_qvm_write_user_command(view, &command, profile, NULL);
      *out = 1;
      return true;
    }
    case QA_QVM_CGAME_IMPORT_CG_SETUSERCMDVALUE: {
      int32_t weapon = qa_qvm_host_call_word(call, 1);
      float sensitivity = qa_qvm_host_call_word_f32(call, 8);
      services.set_user_command_value(services.ctx, weapon, sensitivity);
      *out = 0;
      return true;
    }
    default: return false;
  }
}
