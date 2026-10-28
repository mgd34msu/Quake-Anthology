/* See include/qa/compat/qvm/syscalls.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/syscalls.h"

#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/legacy_bot_abi.h"
#include "qa/compat/qvm/math_syscalls.h"
#include "qa/compat/qvm/memory_syscalls.h"
#include "qa/compat/qvm/snap_vector_syscalls.h"
#include "qa/compat/qvm/vector_syscalls.h"
#include "qa/foundation/support.h"

int32_t qa_qvm_host_call_word(const qa_qvm_host_call *call, size_t index) {
  size_t offset = index * 4;
  const uint8_t *words = call->base.words;
  if (offset > call->base.words_length || call->base.words_length - offset < 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  uint32_t bits = (uint32_t)words[offset] | ((uint32_t)words[offset + 1] << 8) |
                  ((uint32_t)words[offset + 2] << 16) | ((uint32_t)words[offset + 3] << 24);
  return (int32_t)bits;
}

float qa_qvm_host_call_word_f32(const qa_qvm_host_call *call, size_t offset) {
  qa_qvm_host_call shifted = *call;
  int32_t bits_word;
  (void)shifted;
  if (offset % 4 != 0 || offset > call->base.words_length || call->base.words_length - offset < 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  bits_word = qa_qvm_host_call_word(call, offset / 4);
  float value;
  uint32_t bits = (uint32_t)bits_word;
  memcpy(&value, &bits, sizeof value);
  return value;
}

static bool modern(qa_qvm_abi_profile profile) {
  return profile == NULL || strcmp(profile, "q3-modern") == 0;
}

/* Landed decoders (TS abi.ts / legacy-bot-abi.ts, qa/compat/qvm/abi.h
 * and legacy_bot_abi.h); the enum values are the decoded words. */
static bool decode_game(int32_t word, int32_t *out) {
  qa_qvm_game_import decoded;
  if (!qa_qvm_decode_game_import((double)word, &decoded)) return false;
  *out = (int32_t)decoded;
  return true;
}

static bool decode_cgame(int32_t word, int32_t *out) {
  qa_qvm_cgame_import decoded;
  if (!qa_qvm_decode_cgame_import((double)word, &decoded)) return false;
  *out = (int32_t)decoded;
  return true;
}

static bool decode_ui(int32_t word, int32_t *out) {
  qa_qvm_ui_import decoded;
  if (!qa_qvm_decode_ui_import((double)word, &decoded)) return false;
  *out = (int32_t)decoded;
  return true;
}

static bool decode_legacy_game(int32_t code, int32_t *out) {
  qa_qvm_game_import decoded;
  if (!qa_qvm_decode_legacy_game_import((double)code, &decoded)) return false;
  *out = (int32_t)decoded;
  return true;
}

noreturn void qa_qvm_reject_syscall(const qa_qvm_host_call *call) {
  qa_range_error("Unbound %s QVM syscall %d", call->role, (int)call->code);
}

qa_qvm_host_result qa_qvm_reject_syscall_host(void *ctx, const qa_qvm_host_call *call) {
  (void)ctx;
  qa_qvm_reject_syscall(call);
}

void qa_qvm_create_system_call(qa_qvm_system_call *out, qa_qvm_role role, qa_qvm_host_fn host,
                               void *host_ctx, qa_qvm_command_arguments_fn command_arguments,
                               void *command_arguments_ctx, qa_qvm_abi_profile abi_profile) {
  out->role = role;
  out->host = host;
  out->host_ctx = host_ctx;
  out->command_arguments = command_arguments;
  out->command_arguments_ctx = command_arguments_ctx;
  out->abi_profile = abi_profile == NULL ? "q3-modern" : abi_profile;
}

static void classify(qa_qvm_role role, const qa_qvmy_syscall *call, qa_qvm_memory *guest,
                     const char *const *arguments, size_t argument_count, bool has_arguments,
                     qa_qvm_abi_profile abi_profile, qa_qvm_host_call *out) {
  memset(out, 0, sizeof *out);
  out->base = *call;
  out->guest = guest;
  out->has_abi_profile = true;
  out->abi_profile = abi_profile;
  out->command_arguments = arguments;
  out->command_argument_count = argument_count;
  out->has_command_arguments = has_arguments;
  out->role = role;
  int32_t word = qa_qvm_host_call_word(out, 0);
  int32_t code = 0;
  bool engine = false;
  if (strcmp(role, "qagame") == 0) {
    engine = modern(abi_profile) ? decode_game(word, &code) : decode_legacy_game(word, &code);
  } else if (strcmp(role, "cgame") == 0) {
    engine = decode_cgame(word, &code);
  } else if (strcmp(role, "ui") == 0) {
    if (modern(abi_profile)) engine = decode_ui(word, &code);
    else if (word >= 46 && word <= 49) engine = false;
    else engine = decode_ui(word >= 50 && word <= 58 ? word - 4 : word, &code);
  }
  out->kind = engine ? "engine" : "extension";
  out->code = engine ? code : word;
}

qa_qvm_host_result qa_qvm_system_call_invoke(const qa_qvm_system_call *system_call, const qa_qvmy_syscall *call) {
  qa_qvm_role role = system_call->role;
  qa_qvm_abi_profile abi_profile = system_call->abi_profile;
  if (!modern(abi_profile)) {
    qa_qvm_host_call probe;
    memset(&probe, 0, sizeof probe);
    probe.base = *call;
    int32_t trap = qa_qvm_host_call_word(&probe, 0);
    int32_t ignored = 0;
    bool supported;
    if (strcmp(role, "qagame") == 0) {
      supported = (trap >= 0 && trap <= 40) || (trap >= 100 && trap <= 106) || trap == 110 ||
                  trap == 111 || decode_legacy_game(trap, &ignored) || (trap >= 402 && trap <= 405);
    } else {
      supported = (trap >= 0 && trap <= 58) || (trap >= 100 && trap <= 106) ||
                  (strcmp(role, "cgame") == 0 ? (trap == 107 || trap == 108) : (trap == 110 || trap == 111));
    }
    if (!supported) qa_range_error("Legacy QVM ABI service %s/%d is not implemented", role, (int)trap);
  }
  qa_qvm_memory *current = call->guest;
  if (current->bytes != call->memory)
    qa_range_error("QVM syscall memory belongs to another allocation");
  const char *source_role = strcmp(role, "qagame") == 0 ? "game" : role;
  int32_t result = 0;
  const char *math_role = !modern(abi_profile) && strcmp(role, "ui") == 0 ? "game" : source_role;
  if (qa_qvm_memory_syscall(source_role, call->words, call->words_length, current, &result)) return result;
  if (qa_qvm_math_syscall(math_role, call->words, call->words_length, &result)) return result;
  if (qa_qvm_vector_syscall(source_role, call->words, call->words_length, current, &result)) return result;
  if (qa_qvm_snap_vector_syscall(source_role, call->words, call->words_length, current, &result)) return result;
  const char *const *arguments = NULL;
  size_t argument_count = 0;
  bool has_arguments = false;
  if (system_call->command_arguments != NULL)
    system_call->command_arguments(system_call->command_arguments_ctx, &arguments, &argument_count, &has_arguments);
  qa_qvm_host_call classified;
  classify(role, call, current, arguments, argument_count, has_arguments, abi_profile, &classified);
  qa_qvm_host_fn host = system_call->host != NULL ? system_call->host : qa_qvm_reject_syscall_host;
  return host(system_call->host_ctx, &classified);
}
