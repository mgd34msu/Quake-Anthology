/* Port of src/compat/q3/native/game-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/q3/native/game_syscalls.h"

#include <string.h>

#include "qa/compat/qvm/abi.h"

void qa_native_q3_game_call(const uint8_t *words, size_t words_length, const qa_qvm_game_memory *guest,
                            const char *const *command_arguments, size_t command_argument_count,
                            bool has_command_arguments, qa_qvm_host_call *out) {
  memset(out, 0, sizeof *out);
  out->base.words = words;
  out->base.words_length = words_length;
  out->guest = NULL;
  out->game_memory = *guest;
  out->has_abi_profile = true;
  out->abi_profile = "q3-modern";
  out->command_arguments = command_arguments;
  out->command_argument_count = command_argument_count;
  out->has_command_arguments = has_command_arguments;
  out->role = "qagame";
  const int32_t trap = qa_qvm_host_call_word(out, 0);
  qa_qvm_game_import code;
  if (qa_qvm_decode_game_import((double)trap, &code)) {
    out->kind = "engine";
    out->code = (int32_t)code;
  } else {
    out->kind = "extension";
    out->code = trap;
  }
}
