/* Port of src/compat/qvm/bot-library-syscalls.ts over the real BotLibrary.
 * GPL-2.0-or-later. */
#include "qa/compat/qvm/bot_library_syscalls.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/bots/behavior/library/chat.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/legacy_bot_syscalls.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

double qa_qvm_string_contains(const char *text, const char *part, bool case_sensitive) {
  return (double)qa_string_contains_bots_a(text, part, case_sensitive);
}

static qa_bot_goal goal_from_record(qa_qvmx_bot_goal record) {
  qa_bot_goal goal;
  memset(&goal, 0, sizeof goal);
  goal.origin = record.origin;
  goal.area = (int64_t)record.area;
  goal.mins = record.mins;
  goal.maxs = record.maxs;
  goal.entity = (int64_t)record.entity;
  goal.number = (int64_t)record.number;
  goal.flags = (int64_t)record.flags;
  goal.item_info = (int64_t)record.item_info;
  return goal;
}
static qa_qvmx_bot_goal goal_to_record(const qa_bot_goal *goal) {
  qa_qvmx_bot_goal record;
  memset(&record, 0, sizeof record);
  record.origin = goal->origin;
  record.area = (double)goal->area;
  record.mins = goal->mins;
  record.maxs = goal->maxs;
  record.entity = (double)goal->entity;
  record.number = (double)goal->number;
  record.flags = (double)goal->flags;
  record.item_info = (double)goal->item_info;
  return record;
}

bool qa_qvm_touching_goal(qa_vec3 origin, qa_qvmx_bot_goal goal) {
  return qa_touching_goal(origin, goal_from_record(goal));
}

typedef struct dispatch {
  const qa_qvm_host_call *call;
  const qa_qvm_game_memory *guest;
} dispatch;

static int32_t wint(const dispatch *d, size_t index) { return qa_qvm_host_call_word(d->call, index); }
static float wfloat(const dispatch *d, size_t index) { return qa_qvm_host_call_word_f32(d->call, index * 4); }
/* TS text(index): guest.readString(int(index)). Malloc'd; caller frees. */
static char *wtext(const dispatch *d, size_t index) {
  char *text = qa_qvm_game_memory_read_string(d->guest, wint(d, index));
  if (text == NULL) qa_range_error("Out of memory");
  return text;
}
/* TS nullableText(index): NULL when the word is 0. */
static char *wnullable(const dispatch *d, size_t index) { return wint(d, index) == 0 ? NULL : wtext(d, index); }
static qa_vec3 wvector(const dispatch *d, size_t index) {
  qa_qvm_writable_view view;
  qa_qvm_game_memory_view(d->guest, wint(d, index), 12, 0, &view);
  qa_vec3 v;
  v.x = qa_qvm_writable_view_get_float32(&view, 0, true);
  v.y = qa_qvm_writable_view_get_float32(&view, 4, true);
  v.z = qa_qvm_writable_view_get_float32(&view, 8, true);
  return v;
}
/* TS goal(index): readQvmBotGoal(guest.view(int(index), QVM_BOT_GOAL_BYTES)). */
static qa_qvmx_view goal_view(const dispatch *d, size_t index) {
  qa_qvm_writable_view storage;
  qa_qvm_game_memory_view(d->guest, wint(d, index), QA_QVM_BOT_GOAL_BYTES, 0, &storage);
  qa_qvmx_view view = {storage.bytes + storage.start, (size_t)storage.length};
  return view;
}
static qa_bot_goal wgoal(const dispatch *d, size_t index) { return goal_from_record(qa_qvm_read_bot_goal(goal_view(d, index))); }
static qa_chat_text_source source_text(const char *text) {
  qa_chat_text_source source = {text, NULL, NULL};
  return source;
}

/* TS chatWrites(guest): region writes through the guest memory (write observers see them). */
static void writes_set_byte(void *ctx, uint8_t *destination, size_t length, size_t index, uint8_t value) {
  const qa_qvm_game_memory *guest = ctx;
  const qa_qvm_game_region region = {destination, length, false};
  qa_qvm_writable_view view;
  qa_qvm_game_memory_region_view(guest, &region, 0, -1, &view);
  qa_qvm_writable_view_set_uint8(&view, (int64_t)index, value);
}
static void writes_copy(void *ctx, uint8_t *destination, const uint8_t *source, size_t length) {
  const qa_qvm_game_memory *guest = ctx;
  const qa_qvm_game_region region = {destination, length, false};
  qa_qvm_game_memory_region_write(guest, &region, source, length);
}
static void writes_clear(void *ctx, uint8_t *destination, size_t length, size_t start) {
  const qa_qvm_game_memory *guest = ctx;
  const qa_qvm_game_region region = {destination, length, false};
  qa_qvm_game_memory_region_fill(guest, &region, (int64_t)start, (int64_t)length - (int64_t)start, 0);
}
static qa_chat_buffer_writes chat_writes(const qa_qvm_game_memory *guest) {
  qa_chat_buffer_writes writes;
  writes.set_byte = writes_set_byte;
  writes.copy = writes_copy;
  writes.clear = writes_clear;
  writes.ctx = (void *)guest;
  return writes;
}

/* TS inventory(index)(item): guest.view(int(index), 4, item * 4).getInt32. */
typedef struct inventory_ctx { const dispatch *d; size_t index; } inventory_ctx;
static double inventory_read(void *ctx, int64_t item) {
  inventory_ctx *inventory = ctx;
  qa_qvm_writable_view view;
  qa_qvm_game_memory_view(inventory->d->guest, wint(inventory->d, inventory->index), 4, item * 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}
static qa_weight_inventory inventory_of(inventory_ctx *ctx) {
  qa_weight_inventory inventory;
  memset(&inventory, 0, sizeof inventory);
  inventory.read = inventory_read;
  inventory.ctx = ctx;
  return inventory;
}

/* TS () => guest.pointer(int(index)) for replaceSynonymsInPlace. */
typedef struct pointer_ctx { const dispatch *d; size_t index; } pointer_ctx;
static uint8_t *pointer_bytes(void *ctx, size_t *out_length) {
  pointer_ctx *pointer = ctx;
  qa_qvm_game_region region;
  if (!qa_qvm_game_memory_pointer(pointer->d->guest, wint(pointer->d, pointer->index), &region)) {
    *out_length = 0;
    return NULL;
  }
  *out_length = region.length;
  return region.bytes;
}
/* TS () => guest.span(int(index), QVM_BOT_GOAL_BYTES) for pushGoal. */
static const uint8_t *push_goal_bytes(void *ctx, qa_bot_goal *goal, size_t *out_length) {
  (void)goal; /* the produced record is bytes, so the goal form stays unused (TS Uint8Array branch) */
  pointer_ctx *pointer = ctx;
  const qa_qvm_game_region region = qa_qvm_game_memory_span(pointer->d->guest, wint(pointer->d, pointer->index), QA_QVM_BOT_GOAL_BYTES, 0);
  *out_length = region.length;
  return region.bytes;
}
/* TS () => text(index), read once per call. */
typedef struct text_ctx { const dispatch *d; size_t index; char *text; } text_ctx;
static const char *text_read(void *ctx) {
  text_ctx *text = ctx;
  free(text->text);
  text->text = wtext(text->d, text->index);
  return text->text;
}

/* TS message => guest.writeString(int(2), message, int(3)). */
static void chat_message_write(void *ctx, const char *message) {
  const dispatch *d = ctx;
  qa_qvm_game_memory_write_string(d->guest, wint(d, 2), message, wint(d, 3));
}

/* geneticSelection's request: ranks read through guest.view(int(2), 4, index * 4). */
static void genetic_write(void *ctx, const char *target, int64_t value) {
  const dispatch *d = ctx;
  size_t index = strcmp(target, "parent1") == 0 ? 3 : strcmp(target, "parent2") == 0 ? 4 : 5;
  qa_qvm_writable_view view;
  qa_qvm_game_memory_view(d->guest, wint(d, index), 4, 0, &view);
  qa_qvm_writable_view_set_int32(&view, 0, qa_js_to_int32((double)value), true);
}

/* BOTLIB_AI_MATCH_VARIABLE closures over view = guest.view(int(1), 328). */
typedef struct match_ctx { const dispatch *d; qa_qvm_writable_view view; } match_ctx;
static qa_chat_match_variable match_at(void *ctx, int64_t index) {
  match_ctx *match = ctx;
  qa_chat_match_variable variable;
  variable.offset = qa_qvm_writable_view_get_int8(&match->view, 264 + index * 8);
  variable.length = qa_qvm_writable_view_get_int32(&match->view, 268 + index * 8, true);
  return variable;
}
static void match_write(void *ctx, int64_t offset, int64_t capacity) {
  match_ctx *match = ctx;
  char *text = qa_qvm_game_memory_read_string(match->d->guest, (int64_t)wint(match->d, 1) + offset);
  if (text == NULL) qa_range_error("Out of memory");
  qa_qvm_game_memory_write_string(match->d->guest, wint(match->d, 3), text, capacity);
  free(text);
}
static void match_clear(void *ctx) {
  match_ctx *match = ctx;
  qa_qvm_writable_view view;
  qa_qvm_game_memory_view(match->d->guest, wint(match->d, 3), 1, 0, &view);
  qa_qvm_writable_view_set_uint8(&view, 0, 0);
}

static qa_qvm_host_result word(double value) { return qa_qvm_host_word(value); }

bool qa_qvm_bot_library_syscall(const qa_qvm_host_call *call, qa_qvm_bot_library_services *services,
                                qa_qvm_host_result *out) {
  int32_t legacy = 0;
  if (qa_qvm_legacy_bot_library_syscall(call, services, &legacy)) {
    *out = legacy;
    return true;
  }
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") != 0) return false;
  const qa_qvm_game_memory guest_memory = qa_qvm_host_call_game_memory(call);
  const qa_qvm_game_memory *guest = &guest_memory;
  dispatch storage = {call, guest};
  const dispatch *d = &storage;
  qa_bot_library_bots_a *library = services->library;
  qa_chat_buffer_writes writes = chat_writes(guest);
  char *text = NULL, *text2 = NULL;
  *out = 0;
  switch (call->code) {
    case QA_QVM_GAME_IMPORT_G_BOT_ALLOCATE_CLIENT:
      *out = word(services->allocate_client(services->ctx));
      return true;
    case QA_QVM_GAME_IMPORT_G_BOT_FREE_CLIENT:
      services->free_client(services->ctx, wint(d, 1));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_SETUP:
      *out = word(services->setup(services->ctx));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_SHUTDOWN:
      *out = word(services->shutdown(services->ctx));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_LIBVAR_SET:
      text = wtext(d, 1);
      text2 = wtext(d, 2);
      qa_bot_lib_vars_set(&library->variables, text, text2);
      break;
    case QA_QVM_GAME_IMPORT_BOTLIB_LIBVAR_GET: {
      int32_t target = wint(d, 2);
      text = wtext(d, 1);
      const char *value = qa_bot_lib_vars_get_string(&library->variables, text);
      qa_qvm_game_memory_write_string(guest, target, value, wint(d, 3));
      break;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_PC_ADD_GLOBAL_DEFINE:
      text = wtext(d, 1);
      *out = qa_ui_script_global_defines_add(library->globals, text) ? 1 : 0;
      break;
    case QA_QVM_GAME_IMPORT_BOTLIB_START_FRAME: {
      int64_t result = qa_bot_library_start_frame_bots_a(library, wfloat(d, 1));
      if (services->started_frame != NULL) services->started_frame(services->ctx);
      *out = word((double)result);
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_LOAD_MAP:
      text = wtext(d, 1);
      *out = word(services->load_map(services->ctx, text));
      break;
    case QA_QVM_GAME_IMPORT_BOTLIB_UPDATENTITY:
      *out = word(services->update_entity(services->ctx, wint(d, 1), call, wint(d, 2)));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_GET_SNAPSHOT_ENTITY:
      *out = word(services->snapshot_entity(services->ctx, wint(d, 1), wint(d, 2)));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_GET_CONSOLE_MESSAGE: {
      char *message = services->console_message(services->ctx, wint(d, 1));
      if (message == NULL) return true;
      qa_cleanup cleanup;
      qa_cleanup_push(&cleanup, free, message);
      qa_qvm_game_memory_write_string(guest, wint(d, 2), message, wint(d, 3));
      qa_cleanup_pop(&cleanup);
      free(message);
      *out = 1;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_USER_COMMAND: {
      int32_t client = wint(d, 1);
      qa_qvm_writable_view view;
      qa_qvm_game_memory_view(guest, wint(d, 2), QA_QVM_USER_COMMAND_BYTES, 0, &view);
      qa_qvmx_view record = {view.bytes + view.start, (size_t)view.length};
      services->user_command(services->ctx, client,
                             qa_qvm_read_user_command(record, call->has_abi_profile ? call->abi_profile : "q3-modern"));
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_SAY:
      text = wtext(d, 2);
      qa_bot_action_say(&library->actions, wint(d, 1), text);
      break;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_SAY_TEAM:
      text = wtext(d, 2);
      qa_bot_action_say_team(&library->actions, wint(d, 1), text);
      break;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_COMMAND:
      text = wtext(d, 2);
      qa_bot_action_command(&library->actions, wint(d, 1), text);
      break;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_ACTION: qa_bot_action_action(&library->actions, wint(d, 1), wint(d, 2)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_GESTURE: qa_bot_action_gesture(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_TALK: qa_bot_action_talk(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_ATTACK: qa_bot_action_attack(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_USE: qa_bot_action_use(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_RESPAWN: qa_bot_action_respawn(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_CROUCH: qa_bot_action_crouch(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_UP: qa_bot_action_move_up(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_DOWN: qa_bot_action_move_down(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_FORWARD: qa_bot_action_move_forward(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_BACK: qa_bot_action_move_back(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_LEFT: qa_bot_action_move_left(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_RIGHT: qa_bot_action_move_right(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_SELECT_WEAPON:
      qa_bot_action_select_weapon(&library->actions, wint(d, 1), wint(d, 2));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_JUMP: qa_bot_action_jump(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_DELAYED_JUMP: qa_bot_action_delayed_jump(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE: {
      int32_t client = wint(d, 1);
      qa_vec3 direction = wvector(d, 2);
      qa_bot_action_move(&library->actions, client, direction, wfloat(d, 3));
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_VIEW: {
      int32_t client = wint(d, 1);
      qa_bot_action_view(&library->actions, client, wvector(d, 2));
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_END_REGULAR:
      qa_bot_action_end_regular(&library->actions, wint(d, 1), wfloat(d, 2));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_GET_INPUT: {
      const qa_qvm_game_region span = qa_qvm_game_memory_span(guest, wint(d, 3), 40, 0);
      const uint8_t *bytes = qa_bot_action_get_input_bytes(&library->actions, wint(d, 1), wfloat(d, 2));
      qa_qvm_game_memory_region_write(guest, &span, bytes, 40);
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_RESET_INPUT: qa_bot_action_reset_input(&library->actions, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_LOAD_CHARACTER:
      text = wtext(d, 1);
      *out = word((double)qa_bot_character_load(&library->characters, text, wfloat(d, 2)));
      break;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FREE_CHARACTER: qa_bot_character_free(&library->characters, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHARACTERISTIC_FLOAT:
      *out = (int32_t)qa_float32_to_bits(qa_bot_character_float(&library->characters, wint(d, 1), wint(d, 2)));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHARACTERISTIC_BFLOAT:
      *out = (int32_t)qa_float32_to_bits(
          qa_bot_character_bounded_float(&library->characters, wint(d, 1), wint(d, 2), wfloat(d, 3), wfloat(d, 4)));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHARACTERISTIC_INTEGER:
      *out = qa_bot_character_integer(&library->characters, wint(d, 1), wint(d, 2));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHARACTERISTIC_BINTEGER:
      *out = qa_bot_character_bounded_integer(&library->characters, wint(d, 1), wint(d, 2), wint(d, 3), wint(d, 4));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHARACTERISTIC_STRING: {
      int32_t target = wint(d, 3);
      const char *value = qa_bot_character_string(&library->characters, wint(d, 1), wint(d, 2));
      qa_qvm_game_memory_write_string(guest, target, value, wint(d, 4));
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_ALLOC_CHAT_STATE:
      *out = word((double)qa_bot_chat_library_allocate(&library->chat));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FREE_CHAT_STATE: qa_bot_chat_library_free(&library->chat, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_QUEUE_CONSOLE_MESSAGE: {
      int32_t handle = wint(d, 1), type = wint(d, 2);
      text = wtext(d, 3);
      qa_bot_chat_library_queue_console_message(&library->chat, handle, type, source_text(text));
      break;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_REMOVE_CONSOLE_MESSAGE:
      qa_bot_chat_library_remove_console_message(&library->chat, wint(d, 1), wint(d, 2));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_NUM_CONSOLE_MESSAGE:
      *out = word((double)qa_bot_chat_library_num_console_messages(&library->chat, wint(d, 1)));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_NEXT_CONSOLE_MESSAGE: {
      qa_console_chat_message message;
      if (!qa_bot_chat_library_next_console_message(&library->chat, wint(d, 1), &message)) return true;
      qa_qvm_writable_view view;
      qa_qvm_game_memory_view(guest, wint(d, 2), 276, 0, &view);
      qa_qvm_writable_view_set_int32(&view, 0, message.handle, true);
      qa_qvm_writable_view_set_float32(&view, 4, message.time, true);
      qa_qvm_writable_view_set_int32(&view, 8, message.type, true);
      qa_qvm_game_memory_write_string(guest, (int64_t)wint(d, 2) + 12, message.message, 256);
      qa_qvm_writable_view_set_int32(&view, 268, 0, true);
      qa_qvm_writable_view_set_int32(&view, 272, 0, true);
      *out = message.handle;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_INITIAL_CHAT:
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_REPLY_CHAT: {
      bool reply = call->code == QA_QVM_GAME_IMPORT_BOTLIB_AI_REPLY_CHAT;
      int32_t handle = wint(d, 1);
      text = reply ? wtext(d, 2) : wnullable(d, 2);
      int32_t first = wint(d, 3), second = reply ? wint(d, 4) : 0;
      size_t start = reply ? 5 : 4;
      char *texts[8];
      qa_chat_text_source variables[8];
      for (size_t i = 0; i < 8; i++) {
        texts[i] = wnullable(d, start + i);
        variables[i] = source_text(texts[i]);
      }
      qa_chat_text_source name = source_text(text);
      if (reply) *out = qa_bot_chat_library_reply_chat(&library->chat, handle, name, first, second, variables) ? 1 : 0;
      else qa_bot_chat_library_initial_chat(&library->chat, handle, name, first, variables);
      for (size_t i = 0; i < 8; i++) free(texts[i]);
      break;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_NUM_INITIAL_CHATS: {
      int32_t handle = wint(d, 1);
      text = wnullable(d, 2);
      *out = word((double)qa_bot_chat_library_num_initial_chats(&library->chat, handle, source_text(text)));
      break;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHAT_LENGTH:
      *out = word((double)qa_bot_chat_library_chat_length(&library->chat, wint(d, 1)));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_ENTER_CHAT:
      qa_bot_chat_library_enter_chat(&library->chat, wint(d, 1), wint(d, 2), wint(d, 3), false, 0);
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_CHAT_MESSAGE:
      qa_bot_chat_library_write_chat_message(&library->chat, wint(d, 1), chat_message_write, (void *)d);
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_STRING_CONTAINS:
      text = wnullable(d, 1);
      text2 = wnullable(d, 2);
      *out = word(qa_qvm_string_contains(text, text2, wint(d, 3) != 0));
      break;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_UNIFY_WHITE_SPACES: {
      qa_qvm_game_region span;
      bool present = qa_qvm_game_memory_pointer(guest, wint(d, 1), &span);
      qa_unify_white_spaces_in_place_bots_a(present ? span.bytes : NULL, present ? span.length : 0, &writes);
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_REPLACE_SYNONYMS: {
      pointer_ctx pointer = {d, 1};
      qa_bot_chat_library_replace_synonyms_in_place(&library->chat, pointer_bytes, &pointer, wint(d, 2), &writes);
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_LOAD_CHAT_FILE: {
      int32_t handle = wint(d, 1);
      text = wtext(d, 2);
      text2 = wtext(d, 3);
      *out = qa_bot_chat_library_load_chat_file(&library->chat, handle, source_text(text), source_text(text2)) ? 0 : 8;
      break;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_SET_CHAT_GENDER:
      qa_bot_chat_library_set_gender(&library->chat, wint(d, 1), wint(d, 2));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_SET_CHAT_NAME: {
      int32_t handle = wint(d, 1);
      text = wtext(d, 2);
      qa_bot_chat_library_set_name(&library->chat, handle, source_text(text), true, wint(d, 3));
      break;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_ALLOC_GOAL_STATE:
      *out = word((double)qa_bot_goal_alloc_goal_state(&library->goals, wint(d, 1)));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FREE_GOAL_STATE: qa_bot_goal_free_goal_state(&library->goals, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_RESET_GOAL_STATE: qa_bot_goal_reset_goal_state(&library->goals, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_RESET_AVOID_GOALS: qa_bot_goal_reset_avoid_goals(&library->goals, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_PUSH_GOAL: {
      pointer_ctx pointer = {d, 2};
      qa_bot_goal_push_goal_fn(&library->goals, wint(d, 1), push_goal_bytes, &pointer);
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_POP_GOAL: qa_bot_goal_pop_goal(&library->goals, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_EMPTY_GOAL_STACK: qa_bot_goal_empty_goal_stack(&library->goals, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_TOP_GOAL:
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_SECOND_GOAL: {
      const uint8_t *result = call->code == QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_TOP_GOAL
                                  ? qa_bot_goal_get_top_goal_bytes(&library->goals, wint(d, 1))
                                  : qa_bot_goal_get_second_goal_bytes(&library->goals, wint(d, 1));
      if (result == NULL) return true;
      const qa_qvm_game_region span = qa_qvm_game_memory_span(guest, wint(d, 2), QA_QVM_BOT_GOAL_BYTES, 0);
      qa_qvm_game_memory_region_write(guest, &span, result, QA_QVM_BOT_GOAL_BYTES);
      *out = 1;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GOAL_NAME: {
      int32_t target = wint(d, 2);
      const char *name = qa_bot_goal_goal_name(&library->goals, wint(d, 1));
      qa_qvm_game_memory_write_string(guest, target, name, wint(d, 3));
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_AVOID_GOAL_TIME:
      *out = (int32_t)qa_float32_to_bits(qa_bot_goal_avoid_goal_time(&library->goals, wint(d, 1), wint(d, 2)));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_SET_AVOID_GOAL_TIME:
      qa_bot_goal_set_avoid_goal_time(&library->goals, wint(d, 1), wint(d, 2), wfloat(d, 3));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_REMOVE_FROM_AVOID_GOALS:
      qa_bot_goal_remove_from_avoid_goals(&library->goals, wint(d, 1), wint(d, 2));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_LOAD_ITEM_WEIGHTS: {
      text_ctx filename = {d, 2, NULL};
      int64_t result = qa_bot_goal_load_item_weights_fn(&library->goals, wint(d, 1), text_read, &filename);
      free(filename.text);
      *out = word((double)result);
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FREE_ITEM_WEIGHTS: qa_bot_goal_free_item_weights(&library->goals, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHOOSE_LTG_ITEM: {
      inventory_ctx inventory = {d, 3};
      int32_t handle = wint(d, 1);
      qa_vec3 origin = wvector(d, 2);
      *out = qa_bot_goal_choose_ltg_item(&library->goals, handle, origin, inventory_of(&inventory), wint(d, 4)) ? 1 : 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHOOSE_NBG_ITEM: {
      inventory_ctx inventory = {d, 3};
      int32_t handle = wint(d, 1);
      qa_vec3 origin = wvector(d, 2);
      int32_t travel_flags = wint(d, 4);
      qa_bot_goal long_term;
      bool has_long_term = wint(d, 5) != 0;
      if (has_long_term) long_term = wgoal(d, 5);
      *out = qa_bot_goal_choose_nbg_item(&library->goals, handle, origin, inventory_of(&inventory), travel_flags,
                                         has_long_term ? &long_term : NULL, wfloat(d, 6))
                 ? 1
                 : 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_TOUCHING_GOAL: {
      qa_vec3 origin = wvector(d, 1);
      *out = qa_touching_goal(origin, wgoal(d, 2)) ? 1 : 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_ITEM_GOAL_IN_VIS_BUT_NOT_VISIBLE: {
      int32_t viewer = wint(d, 1);
      qa_vec3 eye = wvector(d, 2), angles = wvector(d, 3);
      qa_bot_goal goal = wgoal(d, 4);
      *out = qa_bot_goal_item_goal_in_vis_but_not_visible(&library->goals, viewer, eye, angles, &goal) ? 1 : 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_INIT_LEVEL_ITEMS: qa_bot_goal_init_level_items(&library->goals, NULL); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_UPDATE_ENTITY_ITEMS: qa_bot_goal_update_entity_items(&library->goals); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_ALLOC_WEAPON_STATE:
      *out = word((double)qa_weapon_ai_allocate_state(&library->weapons));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FREE_WEAPON_STATE: qa_weapon_ai_free_state(&library->weapons, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_RESET_WEAPON_STATE: qa_weapon_ai_reset_state(&library->weapons, wint(d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_LOAD_WEAPON_WEIGHTS: {
      text_ctx filename = {d, 2, NULL};
      qa_weapon_load_result result = qa_weapon_ai_load_weights_fn(&library->weapons, wint(d, 1), text_read, &filename);
      free(filename.text);
      *out = (int32_t)result;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHOOSE_BEST_FIGHT_WEAPON: {
      inventory_ctx inventory = {d, 2};
      *out = word((double)qa_weapon_ai_choose_best_fight_weapon(&library->weapons, wint(d, 1), inventory_of(&inventory)));
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_WEAPON_INFO: {
      size_t length = 0;
      const uint8_t *bytes = qa_weapon_ai_weapon_info_bytes(&library->weapons, wint(d, 1), wint(d, 2), &length);
      if (bytes != NULL) {
        const qa_qvm_game_region span = qa_qvm_game_memory_span(guest, wint(d, 3), (int64_t)length, 0);
        qa_qvm_game_memory_region_write(guest, &span, bytes, length);
      }
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_LEVEL_ITEM_GOAL:
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_MAP_LOCATION_GOAL: {
      bool location = call->code == QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_MAP_LOCATION_GOAL;
      size_t output = location ? 2 : 3;
      qa_bot_goal result;
      bool found;
      if (location) {
        text = wtext(d, 1);
        qa_bot_goal prior = wgoal(d, output);
        found = qa_bot_goal_get_map_location_goal(&library->goals, qa_goal_name_text(text), &prior, &result);
      } else {
        int32_t index = wint(d, 1);
        text = wtext(d, 2);
        qa_bot_goal prior = wgoal(d, output);
        found = qa_bot_goal_get_level_item_goal(&library->goals, index, qa_goal_name_text(text), &prior, &result);
      }
      if (!found) {
        *out = location ? 0 : -1;
        break;
      }
      qa_qvmx_bot_goal record = goal_to_record(&result);
      qa_qvm_write_bot_goal(goal_view(d, output), &record, location ? "location" : "level-item");
      *out = location ? 1 : word((double)result.number);
      break;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_NEXT_CAMP_SPOT_GOAL: {
      int32_t index = wint(d, 1);
      qa_bot_goal prior = wgoal(d, 2), result;
      int64_t next = 0;
      if (!qa_bot_goal_get_next_camp_spot_goal(&library->goals, index, &prior, &next, &result)) return true;
      qa_qvmx_bot_goal record = goal_to_record(&result);
      qa_qvm_write_bot_goal(goal_view(d, 2), &record, NULL);
      *out = word((double)next);
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_DUMP_AVOID_GOALS: (void)qa_bot_goal_dump_avoid_goals(&library->goals, wint(d, 1), NULL, 0); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_DUMP_GOAL_STACK: (void)qa_bot_goal_dump_goal_stack(&library->goals, wint(d, 1), NULL, 0); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_INTERBREED_GOAL_FUZZY_LOGIC: {
      qa_string_list warnings = qa_bot_goal_interbreed_goal_fuzzy_logic(&library->goals, wint(d, 1), wint(d, 2), wint(d, 3));
      qa_string_list_destroy(&warnings);
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_MUTATE_GOAL_FUZZY_LOGIC:
      qa_bot_goal_mutate_goal_fuzzy_logic(&library->goals, wint(d, 1), wfloat(d, 2));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_SAVE_GOAL_FUZZY_LOGIC: {
      int32_t handle = wint(d, 1);
      text = wtext(d, 2);
      qa_bot_goal_save_goal_fuzzy_logic(&library->goals, handle, text);
      break;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GENETIC_PARENTS_AND_CHILD_SELECTION: {
      /* TS reads ranks through the request lazily; the C selection takes them up front. A count
       * above 256 or below 0 never reads a rank in TS, so only 0..256 ranks are read here. */
      int32_t count = wint(d, 1);
      double ranks[256];
      size_t read = count < 0 ? 0 : count > 256 ? 0 : (size_t)count;
      for (size_t i = 0; i < read; i++) {
        qa_qvm_writable_view view;
        qa_qvm_game_memory_view(guest, wint(d, 2), 4, (int64_t)i * 4, &view);
        ranks[i] = qa_qvm_writable_view_get_float32(&view, 0, true);
      }
      qa_genetic_selection_result result = qa_bot_library_genetic_selection_bots_a(
          library, read == 0 ? NULL : ranks, count < 0 ? 0 : (size_t)count, genetic_write, (void *)d);
      *out = result.kind == QA_GENETIC_SELECTED ? 1 : 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FIND_MATCH: {
      qa_qvm_writable_view view;
      qa_qvm_game_memory_view(guest, wint(d, 2), 328, 0, &view);
      const qa_qvm_game_region string = qa_qvm_game_memory_span(guest, wint(d, 2), 256, 0);
      qa_chat_match_buffer buffer;
      memset(&buffer, 0, sizeof buffer);
      buffer.bytes = string.bytes;
      buffer.length = string.length;
      buffer.writes = &writes;
      /* The TS buffer's type, subtype and variables are accessors over the record; C
       * stages them and stores the ones the library wrote back through the view. */
      buffer.type = qa_qvm_writable_view_get_int32(&view, 256, true);
      buffer.subtype = qa_qvm_writable_view_get_int32(&view, 260, true);
      qa_chat_match_buffer before;
      for (int i = 0; i < 8; i++) {
        buffer.variables[i].offset = qa_qvm_writable_view_get_int8(&view, 264 + i * 8);
        buffer.variables[i].length = qa_qvm_writable_view_get_int32(&view, 268 + i * 8, true);
      }
      before = buffer;
      text = wtext(d, 1);
      int32_t context = wint(d, 3);
      bool found = qa_bot_chat_library_find_match_into(&library->chat, source_text(text), context, &buffer);
      if (buffer.type != before.type) qa_qvm_writable_view_set_int32(&view, 256, buffer.type, true);
      if (buffer.subtype != before.subtype) qa_qvm_writable_view_set_int32(&view, 260, buffer.subtype, true);
      for (int i = 0; i < 8; i++) {
        if (buffer.variables[i].offset != before.variables[i].offset)
          qa_qvm_writable_view_set_int8(&view, 264 + i * 8, qa_js_to_int32((double)buffer.variables[i].offset));
        if (buffer.variables[i].length != before.variables[i].length)
          qa_qvm_writable_view_set_int32(&view, 268 + i * 8, qa_js_to_int32((double)buffer.variables[i].length), true);
      }
      *out = found ? 1 : 0;
      break;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_MATCH_VARIABLE: {
      match_ctx match;
      match.d = d;
      qa_qvm_game_memory_view(guest, wint(d, 1), 328, 0, &match.view);
      qa_bot_chat_library_write_match_variable(&library->chat, match_at, wint(d, 2), wint(d, 4), match_write, match_clear, &match);
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_PC_LOAD_SOURCE:
      text = wtext(d, 1);
      *out = word((double)qa_bot_script_sources_load(&library->sources, text));
      break;
    case QA_QVM_GAME_IMPORT_BOTLIB_PC_FREE_SOURCE:
      *out = qa_bot_script_sources_free(&library->sources, wint(d, 1)) ? 1 : 0;
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_PC_READ_TOKEN: {
      qa_ui_source_token_memory *token = NULL;
      bool was_read = false;
      if (!qa_bot_script_sources_read_token_result(&library->sources, wint(d, 1), &token, &was_read)) return true;
      qa_qvm_writable_view view;
      qa_qvm_game_memory_view(guest, wint(d, 2), QA_QVM_SCRIPT_TOKEN_BYTES, 0, &view);
      qa_qvm_write_script_token_memory(&view, token);
      *out = was_read ? 1 : 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_PC_SOURCE_FILE_AND_LINE: {
      char filename[QA_BOT_MAX_SOURCE_PATH + 1];
      int64_t line = 0;
      if (!qa_bot_script_sources_file_and_line(&library->sources, wint(d, 1), filename, &line)) return true;
      qa_qvm_game_memory_write_string(guest, wint(d, 2), filename, 128);
      qa_qvm_writable_view view;
      qa_qvm_game_memory_view(guest, wint(d, 3), 4, 0, &view);
      qa_qvm_writable_view_set_int32(&view, 0, qa_js_to_int32((double)line), true);
      *out = 1;
      return true;
    }
    default:
      return false;
  }
  free(text);
  free(text2);
  return true;
}
