/* Port of src/compat/qvm/client-cinematic-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_cinematic_syscalls.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static qa_rect read_rect(const qa_qvm_host_call *call) {
  qa_rect rect = {qa_qvm_host_call_word(call, 2), qa_qvm_host_call_word(call, 3),
                  qa_qvm_host_call_word(call, 4), qa_qvm_host_call_word(call, 5)};
  return rect;
}

bool qa_qvm_client_cinematic_syscall(const qa_qvm_host_call *call,
                                     qa_qvm_client_cinematic_services services,
                                     qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") == 0) return false;
  int32_t play = strcmp(call->role, "ui") == 0 ? 75 : 74;
  int32_t trap = call->code;
  if (trap < play || trap > play + 4) return false;
  const qa_qvm_client_cinematics *cinematics = &services.cinematics;
  if (trap == play) {
    char *path = qa_qvm_memory_read_string(call->guest, qa_qvm_host_call_word(call, 1));
    if (path == NULL) qa_range_error("out of memory");
    qa_rect rect = read_rect(call);
    int32_t bits = qa_qvm_host_call_word(call, 6);
    if (strcmp(call->role, "ui") == 0)
      services.developer_print(services.ctx, "UI_CIN_PlayCinematic\n");
    double result = cinematics->play_guest(cinematics->ctx, path, rect, bits);
    free(path);
    *out = (qa_qvm_host_result)result;
    return true;
  }
  int32_t handle = qa_qvm_host_call_word(call, 1);
  if (trap == play + 1) {
    *out = (qa_qvm_host_result)cinematics->stop_guest(cinematics->ctx, handle);
    return true;
  }
  if (trap == play + 2) {
    *out = (qa_qvm_host_result)cinematics->run_guest(cinematics->ctx, handle);
    return true;
  }
  if (trap == play + 3) cinematics->draw_guest(cinematics->ctx, handle, services.draw);
  else cinematics->set_extents(cinematics->ctx, handle, read_rect(call));
  *out = 0;
  return true;
}
