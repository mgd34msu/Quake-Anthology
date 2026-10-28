/* Port of src/compat/qvm/bot-library-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/bot_library_syscalls.h"

#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/legacy_bot_syscalls.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

double qa_qvm_string_contains(const char *text, const char *part, bool case_sensitive) {
  if (text == NULL || part == NULL) return -1;
  size_t text_length = strlen(text), part_length = strlen(part);
  if (part_length == 0) return 0;
  for (size_t i = 0; i + part_length <= text_length; i++) {
    bool match = true;
    for (size_t j = 0; j < part_length; j++) {
      char a = text[i + j], b = part[j];
      if (!case_sensitive) {
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
      }
      match = match && a == b;
    }
    if (match) return (double)i;
  }
  return -1;
}

static bool chat_white_space(uint8_t byte) {
  if ((byte >= 65 && byte <= 90) || (byte >= 97 && byte <= 122) || (byte >= 48 && byte <= 57))
    return false;
  return strchr("()?:'/,.[]-_+=", byte) == NULL;
}

static int64_t chat_byte(bool has_bytes, qa_qvm_memory_span bytes, qa_qvm_memory *memory,
                         int64_t offset) {
  if (!has_bytes) qa_range_error("bot chat buffer requires a nonnull pointer");
  if (offset < 0 || offset + 1 > bytes.byte_length)
    qa_range_error("bot chat buffer exceeds allocation");
  return memory->bytes[bytes.byte_offset + offset];
}

void qa_qvm_unify_white_spaces_in_place(qa_qvm_memory *memory, bool has_bytes,
                                        qa_qvm_memory_span bytes,
                                        qa_qvmx_chat_buffer_writes writes) {
  int64_t pointer = 0, old = 0;
  while (chat_byte(has_bytes, bytes, memory, pointer) != 0) {
    while (chat_byte(has_bytes, bytes, memory, pointer) != 0 &&
           chat_white_space((uint8_t)chat_byte(has_bytes, bytes, memory, pointer)))
      pointer++;
    if (pointer > old) {
      if (old > 0 && chat_byte(has_bytes, bytes, memory, pointer) != 0) {
        qa_qvmx_chat_span destination = {bytes.byte_offset + old, 1};
        old++;
        qa_qvm_writable_view view = writes.view(writes.ctx, destination);
        qa_qvm_writable_view_set_uint8(&view, 0, 32);
      }
      if (pointer > old) {
        int64_t end = pointer;
        while (chat_byte(has_bytes, bytes, memory, end) != 0) end++;
        qa_qvmx_chat_span destination = {bytes.byte_offset + old, end + 1 - pointer};
        qa_qvmx_chat_span source = {bytes.byte_offset + pointer, end + 1 - pointer};
        writes.copy(writes.ctx, destination, source);
      }
    }
    while (chat_byte(has_bytes, bytes, memory, pointer) != 0 &&
           !chat_white_space((uint8_t)chat_byte(has_bytes, bytes, memory, pointer)))
      pointer++;
    old = pointer;
  }
}

bool qa_qvm_touching_goal(qa_vec3 origin, qa_qvmx_bot_goal goal) {
  qa_vec3 min = {goal.mins.x - 15 + goal.origin.x, goal.mins.y - 15 + goal.origin.y,
                 goal.mins.z - 32 + goal.origin.z};
  qa_vec3 max = {goal.maxs.x + 15 + goal.origin.x, goal.maxs.y + 15 + goal.origin.y,
                 goal.maxs.z + 24 + goal.origin.z};
  if (origin.x < min.x || origin.x > max.x) return false;
  if (origin.y < min.y || origin.y > max.y) return false;
  if (origin.z < min.z || origin.z > max.z) return false;
  return true;
}

typedef struct writes_ctx { qa_qvm_memory *guest; } writes_ctx;

static qa_qvm_writable_view writes_view(void *ctx, qa_qvmx_chat_span span) {
  writes_ctx *writes = ctx;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(writes->guest, span.byte_offset, span.byte_length, &view);
  return view;
}
static void writes_copy(void *ctx, qa_qvmx_chat_span destination, qa_qvmx_chat_span source) {
  writes_ctx *writes = ctx;
  /* source bytes are the live guest range (chatSpan of guest spans). */
  memmove(writes->guest->bytes + destination.byte_offset,
          writes->guest->bytes + source.byte_offset, (size_t)source.byte_length);
}
static void writes_clear(void *ctx, qa_qvmx_chat_span destination, int64_t start) {
  writes_ctx *writes = ctx;
  qa_qvm_memory_fill_bytes(writes->guest, destination.byte_offset + start,
                           destination.byte_length - start, 0);
}

/* Legacy adapters onto the qa_qvmx_bot_library mirror. */
typedef struct legacy_ctx { qa_qvm_bot_library_services *services; } legacy_ctx;
static void legacy_use_item(void *ctx, int32_t client, const char *item) {
  legacy_ctx *l = ctx;
  l->services->library.actions_use_item(l->services->library.ctx, client, item);
}
static void legacy_drop_item(void *ctx, int32_t client, const char *item) {
  legacy_ctx *l = ctx;
  l->services->library.actions_drop_item(l->services->library.ctx, client, item);
}
static void legacy_use_inventory(void *ctx, int32_t client, const char *item) {
  legacy_ctx *l = ctx;
  l->services->library.actions_use_inventory(l->services->library.ctx, client, item);
}
static void legacy_drop_inventory(void *ctx, int32_t client, const char *item) {
  legacy_ctx *l = ctx;
  l->services->library.actions_drop_inventory(l->services->library.ctx, client, item);
}
static void legacy_get_input(void *ctx, int32_t client, float think_time, uint8_t out[40]) {
  legacy_ctx *l = ctx;
  l->services->library.actions_get_input_bytes(l->services->library.ctx, client, think_time, out);
}
static int32_t legacy_character_load(void *ctx, const char *name, int32_t skill) {
  legacy_ctx *l = ctx;
  return (int32_t)l->services->library.characters_load(l->services->library.ctx, name, (float)skill);
}
static void legacy_set_name(void *ctx, int32_t state, const char *name) {
  legacy_ctx *l = ctx;
  /* legacy arity lacks the client argument; pass 0 as the TS overload does. */
  l->services->library.chat_set_name(l->services->library.ctx, state, name, 0);
}
static void legacy_enter_chat(void *ctx, int32_t state, int32_t type, int32_t team, int32_t subtype) {
  legacy_ctx *l = ctx;
  l->services->library.chat_enter_chat_legacy(l->services->library.ctx, state, type, team, subtype);
}
static void legacy_user_command(void *ctx, int32_t client, const qa_q3_wire_user_command *command) {
  legacy_ctx *l = ctx;
  qa_qvmx_wire_user_command converted;
  converted.server_time = command->server_time;
  converted.angles[0] = command->angles[0];
  converted.angles[1] = command->angles[1];
  converted.angles[2] = command->angles[2];
  converted.forwardmove = command->forwardmove;
  converted.rightmove = command->rightmove;
  converted.upmove = command->upmove;
  converted.buttons = command->buttons;
  converted.weapon = command->weapon;
  l->services->user_command(l->services->ctx, client, converted);
}

typedef struct dispatch {
  const qa_qvm_host_call *call;
  qa_qvm_bot_library_services *services;
  qa_qvmx_bot_library *library;
} dispatch;

static int32_t wint(dispatch *d, size_t index) { return qa_qvm_host_call_word(d->call, index); }
static float wfloat(dispatch *d, size_t index) {
  return qa_qvm_host_call_word_f32(d->call, index * 4);
}
static char *wtext(dispatch *d, size_t index) {
  char *text = qa_qvm_memory_read_string(d->call->guest, wint(d, index));
  if (text == NULL) qa_range_error("out of memory");
  return text;
}
static qa_vec3 wvector(dispatch *d, size_t index) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(d->call->guest, wint(d, index), 12, 0, &view);
  qa_vec3 v = {qa_qvm_writable_view_get_float32(&view, 0, true),
               qa_qvm_writable_view_get_float32(&view, 4, true),
               qa_qvm_writable_view_get_float32(&view, 8, true)};
  return v;
}

typedef struct inventory_ctx { dispatch *d; size_t index; } inventory_ctx;
static double inventory_get(void *ctx, double item) {
  inventory_ctx *inventory = ctx;
  qa_qvm_writable_view view;
  qa_qvm_memory_view(inventory->d->call->guest, wint(inventory->d, inventory->index), 4,
                     (int64_t)item * 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}

typedef struct span_ctx { dispatch *d; size_t index; } span_ctx;
static qa_qvm_memory_span span_get(void *ctx) {
  span_ctx *span = ctx;
  return qa_qvm_memory_get_span(span->d->call->guest, wint(span->d, span->index),
                                QA_QVM_BOT_GOAL_BYTES, 0);
}
static qa_qvm_memory_span pointer_get(void *ctx) {
  span_ctx *span = ctx;
  qa_qvm_memory_span result;
  if (!qa_qvm_memory_pointer(span->d->call->guest, wint(span->d, span->index), &result))
    qa_range_error("bot chat buffer requires a nonnull pointer");
  return result;
}
typedef struct text_ctx { dispatch *d; size_t index; char *text; } text_ctx;
static const char *text_get(void *ctx) {
  text_ctx *text = ctx;
  if (text->text == NULL) text->text = wtext(text->d, text->index);
  return text->text;
}

typedef struct chat_write_ctx { dispatch *d; } chat_write_ctx;
static void chat_message_write(void *ctx, const char *message) {
  chat_write_ctx *write = ctx;
  qa_qvm_memory_write_string(write->d->call->guest, wint(write->d, 2), message, wint(write->d, 3));
}

typedef struct genetic_ctx { dispatch *d; } genetic_ctx;
static float genetic_rank(void *ctx, double index) {
  genetic_ctx *genetic = ctx;
  qa_qvm_writable_view view;
  qa_qvm_memory_view(genetic->d->call->guest, wint(genetic->d, 2), 4, (int64_t)index * 4, &view);
  return qa_qvm_writable_view_get_float32(&view, 0, true);
}
static void genetic_write(void *ctx, const char *target, double value) {
  genetic_ctx *genetic = ctx;
  size_t index = strcmp(target, "parent1") == 0 ? 3 : strcmp(target, "parent2") == 0 ? 4 : 5;
  qa_qvm_writable_view view;
  qa_qvm_memory_view(genetic->d->call->guest, wint(genetic->d, index), 4, 0, &view);
  qa_qvm_writable_view_set_int32(&view, 0, qa_int32(value), true);
}

typedef struct capture_ctx { qa_qvm_writable_view *view; } capture_ctx;
static void match_capture(void *ctx, double index, double *out_offset, double *out_length) {
  capture_ctx *capture = ctx;
  *out_offset = qa_qvm_writable_view_get_int8(capture->view, 264 + (int64_t)index * 8);
  *out_length = qa_qvm_writable_view_get_int32(capture->view, 268 + (int64_t)index * 8, true);
}
typedef struct match_write_ctx { dispatch *d; } match_write_ctx;
static void match_write(void *ctx, double offset, double capacity) {
  match_write_ctx *write = ctx;
  char *text = qa_qvm_memory_read_string(write->d->call->guest, wint(write->d, 1) + (int32_t)offset);
  if (text == NULL) qa_range_error("out of memory");
  qa_qvm_memory_write_string(write->d->call->guest, wint(write->d, 3), text, (int64_t)capacity);
  free(text);
}
static void match_clear(void *ctx) {
  match_write_ctx *write = ctx;
  qa_qvm_writable_view view;
  qa_qvm_memory_view(write->d->call->guest, wint(write->d, 3), 1, 0, &view);
  qa_qvm_writable_view_set_uint8(&view, 0, 0);
}

static qa_qvmx_view goal_view(dispatch *d, size_t index, qa_qvm_writable_view *storage) {
  qa_qvm_memory_view(d->call->guest, wint(d, index), QA_QVM_BOT_GOAL_BYTES, 0, storage);
  qa_qvmx_view view = {storage->bytes + storage->start, (size_t)storage->length};
  return view;
}

bool qa_qvm_bot_library_syscall(const qa_qvm_host_call *call, qa_qvm_bot_library_services *services,
                                qa_qvm_host_result *out) {
  legacy_ctx lctx = {services};
  qa_qvmy_bot_library legacy_library;
  memset(&legacy_library, 0, sizeof legacy_library);
  legacy_library.ctx = &lctx;
  legacy_library.actions_use_item = legacy_use_item;
  legacy_library.actions_drop_item = legacy_drop_item;
  legacy_library.actions_use_inventory = legacy_use_inventory;
  legacy_library.actions_drop_inventory = legacy_drop_inventory;
  legacy_library.actions_get_input_bytes = legacy_get_input;
  legacy_library.characters_load = legacy_character_load;
  legacy_library.chat_set_name = legacy_set_name;
  legacy_library.chat_enter_chat = legacy_enter_chat;
  qa_qvmy_bot_library_services legacy_services = {&legacy_library, &lctx, legacy_user_command};
  int32_t legacy_out = 0;
  if (qa_qvm_legacy_bot_library_syscall(call, &legacy_services, &legacy_out)) {
    *out = legacy_out;
    return true;
  }
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") != 0) return false;
  dispatch d = {call, services, &services->library};
  qa_qvmx_bot_library *library = d.library;
  void *lib = library->ctx;
  qa_qvm_memory *guest = call->guest;
  writes_ctx wctx = {guest};
  qa_qvmx_chat_buffer_writes writes = {&wctx, writes_view, writes_copy, writes_clear};
  char *text = NULL, *text2 = NULL;
  const char *variables[8];
  char *variable_texts[8];
  switch (call->code) {
    case QA_QVM_GAME_IMPORT_G_BOT_ALLOCATE_CLIENT:
      *out = (qa_qvm_host_result)services->allocate_client(services->ctx); return true;
    case QA_QVM_GAME_IMPORT_G_BOT_FREE_CLIENT:
      services->free_client(services->ctx, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_SETUP:
      *out = (qa_qvm_host_result)services->setup(services->ctx); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_SHUTDOWN:
      *out = (qa_qvm_host_result)services->shutdown(services->ctx); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_LIBVAR_SET:
      text = wtext(&d, 1); text2 = wtext(&d, 2);
      library->variables_set(lib, text, text2); free(text); free(text2); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_LIBVAR_GET:
      text = wtext(&d, 1);
      qa_qvm_memory_write_string(guest, wint(&d, 2), library->variables_get_string(lib, text), wint(&d, 3));
      free(text); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_PC_ADD_GLOBAL_DEFINE:
      text = wtext(&d, 1); *out = library->globals_add(lib, text) ? 1 : 0; free(text); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_START_FRAME:
      *out = (qa_qvm_host_result)library->start_frame(lib, wfloat(&d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_LOAD_MAP:
      text = wtext(&d, 1); *out = (qa_qvm_host_result)services->load_map(services->ctx, text);
      free(text); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_UPDATENTITY:
      *out = (qa_qvm_host_result)services->update_entity(services->ctx, wint(&d, 1), call, wint(&d, 2));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_GET_SNAPSHOT_ENTITY:
      *out = (qa_qvm_host_result)services->snapshot_entity(services->ctx, wint(&d, 1), wint(&d, 2));
      return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_GET_CONSOLE_MESSAGE: {
      const char *message = services->console_message(services->ctx, wint(&d, 1));
      if (message == NULL) { *out = 0; return true; }
      qa_qvm_memory_write_string(guest, wint(&d, 2), message, wint(&d, 3)); *out = 1; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_USER_COMMAND: {
      qa_qvm_writable_view storage;
      qa_qvm_memory_view(guest, wint(&d, 2), QA_QVM_USER_COMMAND_BYTES, 0, &storage);
      qa_qvmx_view view = {storage.bytes + storage.start, (size_t)storage.length};
      services->user_command(services->ctx, wint(&d, 1),
                             qa_qvm_read_user_command(view, call->has_abi_profile ? call->abi_profile : "q3-modern"));
      *out = 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_SAY:
      text = wtext(&d, 2); library->actions_say(lib, wint(&d, 1), text); free(text); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_SAY_TEAM:
      text = wtext(&d, 2); library->actions_say_team(lib, wint(&d, 1), text); free(text); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_COMMAND:
      text = wtext(&d, 2); library->actions_command(lib, wint(&d, 1), text); free(text); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_ACTION:
      library->actions_action(lib, wint(&d, 1), wint(&d, 2)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_GESTURE: library->actions_gesture(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_TALK: library->actions_talk(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_ATTACK: library->actions_attack(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_USE: library->actions_use(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_RESPAWN: library->actions_respawn(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_CROUCH: library->actions_crouch(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_UP: library->actions_move_up(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_DOWN: library->actions_move_down(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_FORWARD: library->actions_move_forward(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_BACK: library->actions_move_back(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_LEFT: library->actions_move_left(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE_RIGHT: library->actions_move_right(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_SELECT_WEAPON:
      library->actions_select_weapon(lib, wint(&d, 1), wint(&d, 2)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_JUMP: library->actions_jump(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_DELAYED_JUMP: library->actions_delayed_jump(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_MOVE:
      library->actions_move(lib, wint(&d, 1), wvector(&d, 2), wfloat(&d, 3)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_VIEW:
      library->actions_view(lib, wint(&d, 1), wvector(&d, 2)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_END_REGULAR:
      library->actions_end_regular(lib, wint(&d, 1), wfloat(&d, 2)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_GET_INPUT: {
      qa_qvm_memory_span span = qa_qvm_memory_get_span(guest, wint(&d, 3), 40, 0);
      uint8_t input_bytes[40];
      library->actions_get_input_bytes(lib, wint(&d, 1), wfloat(&d, 2), input_bytes);
      qa_qvm_memory_write_bytes(guest, span.byte_offset, input_bytes, 40); *out = 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_EA_RESET_INPUT:
      library->actions_reset_input(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_LOAD_CHARACTER:
      text = wtext(&d, 1); *out = (qa_qvm_host_result)library->characters_load(lib, text, wfloat(&d, 2));
      free(text); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FREE_CHARACTER:
      library->characters_free(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHARACTERISTIC_FLOAT:
      *out = (int32_t)qa_float32_to_bits(library->characters_float(lib, wint(&d, 1), wint(&d, 2))); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHARACTERISTIC_BFLOAT:
      *out = (int32_t)qa_float32_to_bits(library->characters_bounded_float(lib, wint(&d, 1), wint(&d, 2), wfloat(&d, 3), wfloat(&d, 4))); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHARACTERISTIC_INTEGER:
      *out = (qa_qvm_host_result)library->characters_integer(lib, wint(&d, 1), wint(&d, 2)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHARACTERISTIC_BINTEGER:
      *out = (qa_qvm_host_result)library->characters_bounded_integer(lib, wint(&d, 1), wint(&d, 2), wint(&d, 3), wint(&d, 4)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHARACTERISTIC_STRING:
      qa_qvm_memory_write_string(guest, wint(&d, 3), library->characters_string(lib, wint(&d, 1), wint(&d, 2)), wint(&d, 4));
      *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_ALLOC_CHAT_STATE:
      *out = (qa_qvm_host_result)library->chat_allocate(lib); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FREE_CHAT_STATE:
      library->chat_free(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_QUEUE_CONSOLE_MESSAGE:
      text = wtext(&d, 3); library->chat_queue_console_message(lib, wint(&d, 1), wint(&d, 2), text);
      free(text); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_REMOVE_CONSOLE_MESSAGE:
      library->chat_remove_console_message(lib, wint(&d, 1), wint(&d, 2)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_NUM_CONSOLE_MESSAGE:
      *out = (qa_qvm_host_result)library->chat_num_console_messages(lib, wint(&d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_NEXT_CONSOLE_MESSAGE: {
      qa_qvmx_console_chat_message message;
      if (!library->chat_next_console_message(lib, wint(&d, 1), &message)) { *out = 0; return true; }
      qa_qvm_writable_view view;
      qa_qvm_memory_view(guest, wint(&d, 2), 276, 0, &view);
      qa_qvm_writable_view_set_int32(&view, 0, qa_int32(message.handle), true);
      qa_qvm_writable_view_set_float32(&view, 4, qa_float32(message.time), true);
      qa_qvm_writable_view_set_int32(&view, 8, qa_int32(message.type), true);
      qa_qvm_memory_write_string(guest, wint(&d, 2) + 12, message.message, 256);
      qa_qvm_writable_view_set_int32(&view, 268, 0, true);
      qa_qvm_writable_view_set_int32(&view, 272, 0, true);
      *out = qa_int32(message.handle); return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_INITIAL_CHAT: {
      for (int i = 0; i < 8; i++) {
        variable_texts[i] = wint(&d, 4 + i) == 0 ? NULL : wtext(&d, 4 + i);
        variables[i] = variable_texts[i];
      }
      text = wint(&d, 2) == 0 ? NULL : wtext(&d, 2);
      library->chat_initial_chat(lib, wint(&d, 1), text, wint(&d, 3), variables);
      free(text); for (int i = 0; i < 8; i++) free(variable_texts[i]);
      *out = 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_REPLY_CHAT: {
      for (int i = 0; i < 8; i++) {
        variable_texts[i] = wint(&d, 5 + i) == 0 ? NULL : wtext(&d, 5 + i);
        variables[i] = variable_texts[i];
      }
      text = wtext(&d, 2);
      *out = library->chat_reply_chat(lib, wint(&d, 1), text, wint(&d, 3), wint(&d, 4), variables) ? 1 : 0;
      free(text); for (int i = 0; i < 8; i++) free(variable_texts[i]);
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_NUM_INITIAL_CHATS:
      text = wint(&d, 2) == 0 ? NULL : wtext(&d, 2);
      *out = (qa_qvm_host_result)library->chat_num_initial_chats(lib, wint(&d, 1), text);
      free(text); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHAT_LENGTH:
      *out = (qa_qvm_host_result)library->chat_chat_length(lib, wint(&d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_ENTER_CHAT:
      library->chat_enter_chat(lib, wint(&d, 1), wint(&d, 2), wint(&d, 3)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_CHAT_MESSAGE: {
      chat_write_ctx write = {&d};
      library->chat_write_chat_message(lib, wint(&d, 1), chat_message_write, &write);
      *out = 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_STRING_CONTAINS:
      text = wint(&d, 1) == 0 ? NULL : wtext(&d, 1);
      text2 = wint(&d, 2) == 0 ? NULL : wtext(&d, 2);
      *out = qa_int32(qa_qvm_string_contains(text, text2, wint(&d, 3) != 0));
      free(text); free(text2); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_UNIFY_WHITE_SPACES: {
      qa_qvm_memory_span span;
      bool has = qa_qvm_memory_pointer(guest, wint(&d, 1), &span);
      qa_qvm_unify_white_spaces_in_place(guest, has, span, writes); *out = 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_REPLACE_SYNONYMS: {
      span_ctx span = {&d, 1};
      qa_qvm_bot_span_fn source = {&span, pointer_get};
      library->chat_replace_synonyms_in_place(lib, source, wint(&d, 2), writes);
      *out = 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_LOAD_CHAT_FILE:
      text = wtext(&d, 2); text2 = wtext(&d, 3);
      *out = library->chat_load_chat_file(lib, wint(&d, 1), text, text2) ? 0 : 8;
      free(text); free(text2); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_SET_CHAT_GENDER:
      library->chat_set_gender(lib, wint(&d, 1), wint(&d, 2)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_SET_CHAT_NAME:
      text = wtext(&d, 2); library->chat_set_name(lib, wint(&d, 1), text, wint(&d, 3));
      free(text); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_ALLOC_GOAL_STATE:
      *out = (qa_qvm_host_result)library->goals_alloc_goal_state(lib, wint(&d, 1)); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FREE_GOAL_STATE:
      library->goals_free_goal_state(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_RESET_GOAL_STATE:
      library->goals_reset_goal_state(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_RESET_AVOID_GOALS:
      library->goals_reset_avoid_goals(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_PUSH_GOAL: {
      span_ctx span = {&d, 2};
      qa_qvm_bot_span_fn goal = {&span, span_get};
      library->goals_push_goal(lib, wint(&d, 1), goal); *out = 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_POP_GOAL:
      library->goals_pop_goal(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_EMPTY_GOAL_STACK:
      library->goals_empty_goal_stack(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_TOP_GOAL:
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_SECOND_GOAL: {
      uint8_t goal_bytes[QA_QVM_BOT_GOAL_BYTES];
      bool found = call->code == QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_TOP_GOAL
                       ? library->goals_get_top_goal_bytes(lib, wint(&d, 1), goal_bytes)
                       : library->goals_get_second_goal_bytes(lib, wint(&d, 1), goal_bytes);
      if (!found) { *out = 0; return true; }
      qa_qvm_memory_span span = qa_qvm_memory_get_span(guest, wint(&d, 2), QA_QVM_BOT_GOAL_BYTES, 0);
      qa_qvm_memory_write_bytes(guest, span.byte_offset, goal_bytes, QA_QVM_BOT_GOAL_BYTES);
      *out = 1; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GOAL_NAME:
      qa_qvm_memory_write_string(guest, wint(&d, 2), library->goals_goal_name(lib, wint(&d, 1)), wint(&d, 3));
      *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_AVOID_GOAL_TIME:
      *out = (int32_t)qa_float32_to_bits(library->goals_avoid_goal_time(lib, wint(&d, 1), wint(&d, 2))); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_SET_AVOID_GOAL_TIME:
      library->goals_set_avoid_goal_time(lib, wint(&d, 1), wint(&d, 2), wfloat(&d, 3)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_REMOVE_FROM_AVOID_GOALS:
      library->goals_remove_from_avoid_goals(lib, wint(&d, 1), wint(&d, 2)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_LOAD_ITEM_WEIGHTS: {
      text_ctx filename = {&d, 2, NULL};
      qa_qvm_bot_text_fn fn = {&filename, text_get};
      *out = (qa_qvm_host_result)library->goals_load_item_weights(lib, wint(&d, 1), fn);
      free(filename.text); return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FREE_ITEM_WEIGHTS:
      library->goals_free_item_weights(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHOOSE_LTG_ITEM: {
      inventory_ctx inventory = {&d, 3};
      qa_qvm_bot_inventory_fn fn = {&inventory, inventory_get};
      *out = library->goals_choose_ltg_item(lib, wint(&d, 1), wvector(&d, 2), fn, wint(&d, 4)) ? 1 : 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHOOSE_NBG_ITEM: {
      inventory_ctx inventory = {&d, 3};
      qa_qvm_bot_inventory_fn fn = {&inventory, inventory_get};
      qa_qvmx_bot_goal ltg;
      const qa_qvmx_bot_goal *ltg_pointer = NULL;
      if (wint(&d, 5) != 0) {
        qa_qvm_writable_view storage;
        ltg = qa_qvm_read_bot_goal(goal_view(&d, 5, &storage));
        ltg_pointer = &ltg;
      }
      *out = library->goals_choose_nbg_item(lib, wint(&d, 1), wvector(&d, 2), fn, wint(&d, 4), ltg_pointer, wfloat(&d, 6)) ? 1 : 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_TOUCHING_GOAL: {
      qa_qvm_writable_view storage;
      *out = qa_qvm_touching_goal(wvector(&d, 1), qa_qvm_read_bot_goal(goal_view(&d, 2, &storage))) ? 1 : 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_ITEM_GOAL_IN_VIS_BUT_NOT_VISIBLE: {
      qa_qvm_writable_view storage;
      *out = library->goals_item_goal_in_vis_but_not_visible(lib, wint(&d, 1), wvector(&d, 2), wvector(&d, 3), qa_qvm_read_bot_goal(goal_view(&d, 4, &storage))) ? 1 : 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_INIT_LEVEL_ITEMS:
      library->goals_init_level_items(lib); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_UPDATE_ENTITY_ITEMS:
      library->goals_update_entity_items(lib); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_ALLOC_WEAPON_STATE:
      *out = (qa_qvm_host_result)library->weapons_allocate_state(lib); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FREE_WEAPON_STATE:
      library->weapons_free_state(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_RESET_WEAPON_STATE:
      library->weapons_reset_state(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_LOAD_WEAPON_WEIGHTS: {
      text_ctx filename = {&d, 2, NULL};
      qa_qvm_bot_text_fn fn = {&filename, text_get};
      *out = (qa_qvm_host_result)library->weapons_load_weights(lib, wint(&d, 1), fn);
      free(filename.text); return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_CHOOSE_BEST_FIGHT_WEAPON: {
      inventory_ctx inventory = {&d, 2};
      qa_qvm_bot_inventory_fn fn = {&inventory, inventory_get};
      *out = (qa_qvm_host_result)library->weapons_choose_best_fight_weapon(lib, wint(&d, 1), fn);
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_WEAPON_INFO: {
      const uint8_t *bytes = NULL; size_t length = 0;
      if (library->weapons_weapon_info_bytes(lib, wint(&d, 1), wint(&d, 2), &bytes, &length)) {
        qa_qvm_memory_span span = qa_qvm_memory_get_span(guest, wint(&d, 3), (int64_t)length, 0);
        qa_qvm_memory_write_bytes(guest, span.byte_offset, bytes, length);
      }
      *out = 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_LEVEL_ITEM_GOAL:
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_MAP_LOCATION_GOAL: {
      bool location = call->code == QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_MAP_LOCATION_GOAL;
      size_t output = location ? 2 : 3;
      qa_qvm_writable_view storage;
      qa_qvmx_view view = goal_view(&d, output, &storage);
      qa_qvmx_bot_goal current = qa_qvm_read_bot_goal(view);
      qa_qvmx_bot_goal result = current;
      bool found;
      if (location) {
        text = wtext(&d, 1);
        found = library->goals_get_map_location_goal(lib, text, &result); free(text);
      } else {
        text = wtext(&d, 2);
        found = library->goals_get_level_item_goal(lib, wint(&d, 1), text, &result); free(text);
      }
      if (!found) { *out = 0; return true; }
      qa_qvm_write_bot_goal(view, &result, location ? "location" : "level-item");
      *out = location ? 1 : qa_int32(result.number); return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GET_NEXT_CAMP_SPOT_GOAL: {
      qa_qvm_writable_view storage;
      qa_qvmx_view view = goal_view(&d, 2, &storage);
      qa_qvmx_bot_goal result = qa_qvm_read_bot_goal(view);
      double next = 0;
      if (!library->goals_get_next_camp_spot_goal(lib, wint(&d, 1), &result, &next)) { *out = 0; return true; }
      qa_qvm_write_bot_goal(view, &result, NULL); *out = qa_int32(next); return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_DUMP_AVOID_GOALS:
      library->goals_dump_avoid_goals(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_DUMP_GOAL_STACK:
      library->goals_dump_goal_stack(lib, wint(&d, 1)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_INTERBREED_GOAL_FUZZY_LOGIC:
      library->goals_interbreed_goal_fuzzy_logic(lib, wint(&d, 1), wint(&d, 2), wint(&d, 3)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_MUTATE_GOAL_FUZZY_LOGIC:
      library->goals_mutate_goal_fuzzy_logic(lib, wint(&d, 1), wfloat(&d, 2)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_SAVE_GOAL_FUZZY_LOGIC:
      text = wtext(&d, 2); library->goals_save_goal_fuzzy_logic(lib, wint(&d, 1), text);
      free(text); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_GENETIC_PARENTS_AND_CHILD_SELECTION: {
      genetic_ctx genetic = {&d};
      qa_qvmx_genetic_selection selection = {(double)wint(&d, 1), &genetic, genetic_rank, genetic_write};
      *out = library->genetic_selection(lib, selection) ? 1 : 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_FIND_MATCH: {
      qa_qvm_writable_view view;
      qa_qvm_memory_view(guest, wint(&d, 2), 328, 0, &view);
      qa_qvm_memory_span string_span = qa_qvm_memory_get_span(guest, wint(&d, 2), 256, 0);
      qa_qvmx_chat_match_buffer buffer;
      buffer.view = view;
      buffer.string.byte_offset = string_span.byte_offset;
      buffer.string.byte_length = string_span.byte_length;
      buffer.writes = writes;
      text = wtext(&d, 1);
      *out = library->chat_find_match_into(lib, text, wint(&d, 3), &buffer) ? 1 : 0;
      free(text); return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_AI_MATCH_VARIABLE: {
      qa_qvm_writable_view view;
      qa_qvm_memory_view(guest, wint(&d, 1), 328, 0, &view);
      capture_ctx capture = {&view};
      match_write_ctx write = {&d};
      library->chat_write_match_variable(lib, match_capture, &capture, wint(&d, 2), wint(&d, 4),
                                         match_write, &write, match_clear, &write);
      *out = 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_PC_LOAD_SOURCE:
      text = wtext(&d, 1); *out = (qa_qvm_host_result)library->sources_load_source_handle(lib, text);
      free(text); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_PC_FREE_SOURCE:
      *out = library->sources_free_source_handle(lib, wint(&d, 1)) ? 1 : 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_PC_READ_TOKEN: {
      qa_ui_source_token_memory *token = NULL; bool was_read = false;
      if (!library->sources_read_token(lib, wint(&d, 1), &token, &was_read)) { *out = 0; return true; }
      qa_qvm_writable_view view;
      qa_qvm_memory_view(guest, wint(&d, 2), QA_QVM_SCRIPT_TOKEN_BYTES, 0, &view);
      qa_qvm_write_script_token_memory(&view, token); *out = was_read ? 1 : 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_PC_SOURCE_FILE_AND_LINE: {
      const char *filename = NULL; double line = 0;
      if (!library->sources_source_file_and_line(lib, wint(&d, 1), &filename, &line)) { *out = 0; return true; }
      qa_qvm_memory_write_string(guest, wint(&d, 2), filename, 128);
      qa_qvm_writable_view view;
      qa_qvm_memory_view(guest, wint(&d, 3), 4, 0, &view);
      qa_qvm_writable_view_set_int32(&view, 0, qa_int32(line), true);
      *out = 1; return true;
    }
    default: return false;
  }
}
