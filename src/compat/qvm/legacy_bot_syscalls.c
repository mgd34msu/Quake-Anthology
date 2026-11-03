/* See include/qa/compat/qvm/legacy_bot_syscalls.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/legacy_bot_syscalls.h"

#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/bots/behavior/library/chat.h"

/* Landed QvmGameImport values (TS abi.ts). */
enum {
  BOTLIB_USER_COMMAND = QA_QVM_GAME_IMPORT_BOTLIB_USER_COMMAND,
  BOTLIB_EA_GET_INPUT = QA_QVM_GAME_IMPORT_BOTLIB_EA_GET_INPUT,
  BOTLIB_AI_LOAD_CHARACTER = QA_QVM_GAME_IMPORT_BOTLIB_AI_LOAD_CHARACTER,
  BOTLIB_AI_ENTER_CHAT = QA_QVM_GAME_IMPORT_BOTLIB_AI_ENTER_CHAT,
  BOTLIB_AI_SET_CHAT_NAME = QA_QVM_GAME_IMPORT_BOTLIB_AI_SET_CHAT_NAME,
};

/* BotActionFlag (bots/behavior/library/actions.h). */
enum {
  BOT_RESPAWN = QA_BOT_ACTION_RESPAWN,
  BOT_JUMP = QA_BOT_ACTION_JUMP,
  BOT_MOVE_UP = QA_BOT_ACTION_MOVE_UP,
  BOT_CROUCH = QA_BOT_ACTION_CROUCH,
  BOT_MOVE_DOWN = QA_BOT_ACTION_MOVE_DOWN,
  BOT_MOVE_FORWARD = QA_BOT_ACTION_MOVE_FORWARD,
  BOT_MOVE_BACK = QA_BOT_ACTION_MOVE_BACK,
  BOT_MOVE_LEFT = QA_BOT_ACTION_MOVE_LEFT,
  BOT_MOVE_RIGHT = QA_BOT_ACTION_MOVE_RIGHT,
  BOT_DELAYED_JUMP = QA_BOT_ACTION_DELAYED_JUMP,
  BOT_TALK = QA_BOT_ACTION_TALK,
  BOT_GESTURE = QA_BOT_ACTION_GESTURE,
  BOT_WALK = QA_BOT_ACTION_WALK,
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
  return qa_qvm_game_memory_read_string(QA_SHARED_GUEST(call), qa_qvm_host_call_word(call, index));
}

bool qa_qvm_legacy_bot_library_syscall(const qa_qvm_host_call *call,
                                       qa_qvm_bot_library_services *services, int32_t *out) {
  if (strcmp(call->role, "qagame") != 0) return false;
  if (!call->has_abi_profile || call->abi_profile == NULL ||
      strcmp(call->abi_profile, "q3-1.16n-base") != 0)
    return false;
  qa_bot_library_bots_a *library = services->library;
  if (strcmp(call->kind, "extension") == 0) {
    char *item = NULL;
    int32_t client = qa_qvm_host_call_word(call, 1);
    switch (call->code) {
      case 402: item = text(call, 2); qa_bot_action_use_item(&library->actions, client, item); break;
      case 403: item = text(call, 2); qa_bot_action_drop_item(&library->actions, client, item); break;
      case 404: item = text(call, 2); qa_bot_action_use_inventory(&library->actions, client, item); break;
      case 405: item = text(call, 2); qa_bot_action_drop_inventory(&library->actions, client, item); break;
      default: return false;
    }
    free(item);
    *out = 0;
    return true;
  }
  switch (call->code) {
    case BOTLIB_AI_LOAD_CHARACTER: {
      char *name = text(call, 1);
      int64_t result = qa_bot_character_load(&library->characters, name, qa_qvm_host_call_word(call, 2));
      free(name);
      *out = qa_qvm_host_word((double)result);
      return true;
    }
    case BOTLIB_AI_SET_CHAT_NAME: {
      int32_t handle = qa_qvm_host_call_word(call, 1);
      char *name = text(call, 2);
      /* Legacy arity has no client argument: TS passes undefined, so
       * the state's client stays as it was. */
      qa_chat_text_source source = {name, NULL, NULL};
      qa_bot_chat_library_set_name(&library->chat, handle, source, false, 0);
      free(name);
      *out = 0;
      return true;
    }
    case BOTLIB_AI_ENTER_CHAT:
      qa_bot_chat_library_enter_chat(&library->chat, qa_qvm_host_call_word(call, 1), qa_qvm_host_call_word(call, 2),
                                     qa_qvm_host_call_word(call, 3) == 1 ? 1 : 0, true, qa_qvm_host_call_word(call, 2));
      *out = 0;
      return true;
    case BOTLIB_EA_GET_INPUT: {
      const uint8_t *bytes = qa_bot_action_get_input_bytes(&library->actions, qa_qvm_host_call_word(call, 1),
                                                           qa_qvm_host_call_word_f32(call, 8));
      const qa_qvm_game_memory guest = qa_qvm_host_call_game_memory(call);
      const qa_qvm_game_region target = qa_qvm_game_memory_span(&guest, qa_qvm_host_call_word(call, 3), 40, 0);
      qa_qvm_game_memory_region_write(&guest, &target, bytes, 40);
      qa_qvm_writable_view view;
      qa_qvm_game_memory_region_view(&guest, &target, 0, -1, &view);
      int32_t flags = qa_qvm_writable_view_get_int32(&view, 32, true);
      qa_qvm_writable_view_set_int32(&view, 32, qa_qvm_legacy_bot_action_flags(flags), true);
      *out = 0;
      return true;
    }
    case BOTLIB_USER_COMMAND: {
      qa_qvm_writable_view view;
      qa_qvm_game_memory_view(QA_SHARED_GUEST(call), qa_qvm_host_call_word(call, 2), 24, 0, &view);
      uint8_t buttons = qa_qvm_writable_view_get_uint8(&view, 4);
      qa_qvmx_wire_user_command command;
      command.server_time = qa_qvm_writable_view_get_int32(&view, 0, true);
      command.angles[0] = qa_qvm_writable_view_get_int32(&view, 8, true);
      command.angles[1] = qa_qvm_writable_view_get_int32(&view, 12, true);
      command.angles[2] = qa_qvm_writable_view_get_int32(&view, 16, true);
      command.buttons = (buttons & 31) | ((buttons & 128) == 0 ? 0 : 2048);
      command.weapon = qa_qvm_writable_view_get_uint8(&view, 5);
      command.forwardmove = qa_qvm_writable_view_get_int8(&view, 20);
      command.rightmove = qa_qvm_writable_view_get_int8(&view, 21);
      command.upmove = qa_qvm_writable_view_get_int8(&view, 22);
      services->user_command(services->ctx, qa_qvm_host_call_word(call, 1), command);
      *out = 0;
      return true;
    }
    default: return false;
  }
}
