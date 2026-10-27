/* See include/qa/compat/qvm/legacy_bot_syscalls.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/legacy_bot_syscalls.h"

#include <stdlib.h>
#include <string.h>

/* Mirrored QvmGameImport values (TS abi.ts). */
enum {
  BOTLIB_USER_COMMAND = 211,
  BOTLIB_EA_GET_INPUT = 422,
  BOTLIB_AI_LOAD_CHARACTER = 500,
  BOTLIB_AI_ENTER_CHAT = 516,
  BOTLIB_AI_SET_CHAT_NAME = 524,
};

/* Mirrored BotActionFlag values (TS bots/behavior/library/actions.ts). */
enum {
  BOT_RESPAWN = 0x0000008,
  BOT_JUMP = 0x0000010,
  BOT_MOVE_UP = 0x0000020,
  BOT_CROUCH = 0x0000080,
  BOT_MOVE_DOWN = 0x0000100,
  BOT_MOVE_FORWARD = 0x0000200,
  BOT_MOVE_BACK = 0x0000800,
  BOT_MOVE_LEFT = 0x0001000,
  BOT_MOVE_RIGHT = 0x0002000,
  BOT_DELAYED_JUMP = 0x0008000,
  BOT_TALK = 0x0010000,
  BOT_GESTURE = 0x0020000,
  BOT_WALK = 0x0080000,
};

int32_t qa_qvm_legacy_bot_action_flags(int32_t flags) {
  int32_t result = flags & 3;
  static const int32_t fields[][2] = {
      {BOT_RESPAWN, 4}, {BOT_JUMP | BOT_MOVE_UP, 8}, {BOT_CROUCH | BOT_MOVE_DOWN, 16},
      {BOT_MOVE_FORWARD, 32}, {BOT_MOVE_BACK, 64}, {BOT_MOVE_LEFT, 128}, {BOT_MOVE_RIGHT, 256},
      {BOT_DELAYED_JUMP, 512}, {BOT_TALK, 1024}, {BOT_GESTURE, 2048}, {BOT_WALK, 4096},
  };
  for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++)
    if ((flags & fields[i][0]) != 0) result |= fields[i][1];
  return result;
}

static char *text(const qa_qvm_host_call *call, size_t index) {
  return qa_qvm_memory_read_string(call->guest, qa_qvm_host_call_word(call, index));
}

bool qa_qvm_legacy_bot_library_syscall(const qa_qvm_host_call *call,
                                       const qa_qvmy_bot_library_services *services, int32_t *out) {
  if (strcmp(call->role, "qagame") != 0) return false;
  if (!call->has_abi_profile || call->abi_profile == NULL ||
      strcmp(call->abi_profile, "q3-1.16n-base") != 0)
    return false;
  const qa_qvmy_bot_library *library = services->library;
  void *ctx = library->ctx;
  if (strcmp(call->kind, "extension") == 0) {
    char *item = NULL;
    switch (call->code) {
      case 402: item = text(call, 2); library->actions_use_item(ctx, qa_qvm_host_call_word(call, 1), item); break;
      case 403: item = text(call, 2); library->actions_drop_item(ctx, qa_qvm_host_call_word(call, 1), item); break;
      case 404: item = text(call, 2); library->actions_use_inventory(ctx, qa_qvm_host_call_word(call, 1), item); break;
      case 405: item = text(call, 2); library->actions_drop_inventory(ctx, qa_qvm_host_call_word(call, 1), item); break;
      default: return false;
    }
    free(item);
    *out = 0;
    return true;
  }
  switch (call->code) {
    case BOTLIB_AI_LOAD_CHARACTER: {
      char *name = text(call, 1);
      int32_t result = library->characters_load(ctx, name, qa_qvm_host_call_word(call, 2));
      free(name);
      *out = result;
      return true;
    }
    case BOTLIB_AI_SET_CHAT_NAME: {
      char *name = text(call, 2);
      library->chat_set_name(ctx, qa_qvm_host_call_word(call, 1), name);
      free(name);
      *out = 0;
      return true;
    }
    case BOTLIB_AI_ENTER_CHAT:
      library->chat_enter_chat(ctx, qa_qvm_host_call_word(call, 1), qa_qvm_host_call_word(call, 2),
                               qa_qvm_host_call_word(call, 3) == 1 ? 1 : 0, qa_qvm_host_call_word(call, 2));
      *out = 0;
      return true;
    case BOTLIB_EA_GET_INPUT: {
      uint8_t bytes[40];
      library->actions_get_input_bytes(ctx, qa_qvm_host_call_word(call, 1),
                                       qa_qvm_host_call_word_f32(call, 8), bytes);
      qa_qvm_memory_span target = qa_qvm_memory_get_span(call->guest, qa_qvm_host_call_word(call, 3), 40, 0);
      qa_qvm_memory_write_bytes(call->guest, target.byte_offset, bytes, 40);
      qa_qvm_writable_view view;
      qa_qvm_memory_data_view(call->guest, target.byte_offset, target.byte_length, &view);
      int32_t flags = qa_qvm_writable_view_get_int32(&view, 32, true);
      qa_qvm_writable_view_set_int32(&view, 32, qa_qvm_legacy_bot_action_flags(flags), true);
      *out = 0;
      return true;
    }
    case BOTLIB_USER_COMMAND: {
      qa_qvm_writable_view view;
      qa_qvm_memory_view(call->guest, qa_qvm_host_call_word(call, 2), 24, 0, &view);
      uint8_t buttons = qa_qvm_writable_view_get_uint8(&view, 4);
      qa_q3_wire_user_command command;
      command.server_time = qa_qvm_writable_view_get_int32(&view, 0, true);
      command.angles[0] = qa_qvm_writable_view_get_int32(&view, 8, true);
      command.angles[1] = qa_qvm_writable_view_get_int32(&view, 12, true);
      command.angles[2] = qa_qvm_writable_view_get_int32(&view, 16, true);
      command.buttons = (buttons & 31) | ((buttons & 128) == 0 ? 0 : 2048);
      command.weapon = qa_qvm_writable_view_get_uint8(&view, 5);
      command.forwardmove = qa_qvm_writable_view_get_int8(&view, 20);
      command.rightmove = qa_qvm_writable_view_get_int8(&view, 21);
      command.upmove = qa_qvm_writable_view_get_int8(&view, 22);
      services->user_command(services->ctx, qa_qvm_host_call_word(call, 1), &command);
      *out = 0;
      return true;
    }
    default: return false;
  }
}
