/* Port of src/app/bootstrap/q3-client/qvm-scalars.ts.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include "qa/app/bootstrap/q3_client/qvm_scalars.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <time.h>
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/memory.h"
#include "qa/input/key_codes.h"
#include "qa/input/keys.h"
#include "qa/input/mouse_buttons.h"
#include "qa/platform/sdl.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static int32_t word_i32(const qa_qvm_host_call *call, size_t offset) {
  int32_t v;
  memcpy(&v, call->base.words + offset, sizeof v);
  return v;
}
static const char *action_command(qa_input_action action) {
  struct entry { const char *action; const char *command; };
  static const struct entry table[] = {
    {"attack", "+attack"}, {"jump", "+moveup"}, {"forward", "+forward"}, {"back", "+back"},
    {"move-left", "+moveleft"}, {"move-right", "+moveright"}, {"move-up", "+moveup"},
    {"move-down", "+movedown"}, {"use", "+use"}, {"crouch", "+movedown"}, {"walk", "+speed"},
    {"scores", "+scores"}, {"next-weapon", "weapnext"}, {"previous-weapon", "weapprev"}, {"menu", "togglemenu"}};
  for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
    if (is(action, table[i].action)) return table[i].command;
  return "";
}
static const char *binding_command_text(const qa_input_binding_target *target) {
  return !target->is_action ? target->text : action_command(target->action);
}
static qa_physical_input physical(int32_t key) {
  qa_physical_input input;
  memset(&input, 0, sizeof input);
  if (key >= QA_KEY_MOUSE1 && key <= QA_KEY_MOUSE5) {
    input.kind = QA_PHYSICAL_INPUT_MOUSE_BUTTON;
    input.button = qa_physical_mouse_button(key - QA_KEY_MOUSE1 + 1);
  } else {
    input.kind = QA_PHYSICAL_INPUT_KEY;
    input.code = key;
  }
  return input;
}
static bool ieq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return false;
  while (*a != '\0' && *b != '\0') {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    a++; b++;
  }
  return *a == '\0' && *b == '\0';
}
bool qa_app_qvm_client_input_syscall(const qa_qvm_host_call *call, qa_app_qvm_client_input input,
                                     qa_qvm_host_result *out) {
  if (!is(call->kind, "engine") || (!is(call->role, "ui") && !is(call->role, "cgame"))) return false;
  bool ui = is(call->role, "ui");
  qa_qvm_memory *guest = call->guest;
  int32_t code = call->code;
  if (code == (ui ? QA_QVM_UI_IMPORT_UI_KEY_ISDOWN : QA_QVM_CGAME_IMPORT_CG_KEY_ISDOWN)) {
    qa_physical_input target = physical(word_i32(call, 4));
    *out = input.is_down(input.state_ctx, &target) ? 1 : 0;
    return true;
  }
  if (ui) {
    if (code == QA_QVM_UI_IMPORT_UI_KEY_GETBINDINGBUF) {
      qa_physical_input target = physical(word_i32(call, 4));
      const qa_input_binding_target *binding = input.seat.binding(input.seat.ctx, &target);
      qa_qvm_memory_write_string(guest, word_i32(call, 8),
                                 binding == NULL ? "" : binding_command_text(binding), word_i32(call, 12));
      *out = 0;
      return true;
    }
    if (code == QA_QVM_UI_IMPORT_UI_KEY_SETBINDING) {
      char *text = qa_qvm_memory_read_string(guest, word_i32(call, 8));
      qa_input_binding binding;
      binding.input = physical(word_i32(call, 4));
      binding.target.is_action = false;
      binding.target.action = NULL;
      binding.target.text = text;
      input.seat.bind(input.seat.ctx, &binding);
      free(text);
      *out = 0;
      return true;
    }
    if (code == QA_QVM_UI_IMPORT_UI_KEY_CLEARSTATES) {
      input.clear_states(input.state_ctx);
      *out = 0;
      return true;
    }
  } else if (code == QA_QVM_CGAME_IMPORT_CG_KEY_GETKEY) {
    char *name = qa_qvm_memory_read_string(guest, word_i32(call, 4));
    size_t count = 0;
    const qa_input_binding *bindings = input.seat.bindings(input.seat.ctx, &count);
    qa_qvm_host_result result = -1;
    for (size_t i = 0; i < count; i++) {
      if (!ieq(binding_command_text(&bindings[i].target), name)) continue;
      if (bindings[i].input.kind == QA_PHYSICAL_INPUT_KEY) result = (qa_qvm_host_result)bindings[i].input.code;
      else if (bindings[i].input.kind == QA_PHYSICAL_INPUT_MOUSE_BUTTON)
        result = (qa_qvm_host_result)(QA_KEY_MOUSE1 + qa_quake_mouse_button(bindings[i].input.button) - 1);
      break;
    }
    free(name);
    *out = result;
    return true;
  }
  return false;
}
static qa_vec3 read_vector(qa_qvm_memory *guest, int32_t pointer) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(guest, pointer, 12, 0, &view);
  float values[3];
  memcpy(values, view.bytes, sizeof values);
  qa_vec3 out = {values[0], values[1], values[2]};
  return out;
}
static void write_vector(qa_qvm_memory *guest, int32_t pointer, qa_vec3 value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(guest, pointer, 12, 0, &view);
  float values[3] = {(float)value.x, (float)value.y, (float)value.z};
  memcpy(view.bytes, values, sizeof values);
}
void qa_app_qvm_application_scalars_init(qa_app_qvm_application_scalars *scalars,
                                         qa_app_qvm_application_scalar_options options) {
  scalars->options = options;
}
bool qa_app_qvm_application_scalars_dispatch(qa_app_qvm_application_scalars *scalars,
    const qa_qvm_host_call *call, void (*update_screen)(void *ctx), void *update_screen_ctx,
    qa_qvm_host_result *out) {
  qa_app_qvm_application_scalar_options *o = &scalars->options;
  o->assert_current(o->assert_ctx);
  if (!is(call->kind, "engine") || (!is(call->role, "ui") && !is(call->role, "cgame"))) return false;
  bool ui = is(call->role, "ui");
  int32_t code = call->code;
  qa_qvm_memory *guest = call->guest;
  qa_app_qvm_display_options display = {o->backend_stencil_bits, o->driver, o->gl_config,
      o->media, o->resources, o->viewport_ctx, o->viewport, o->assert_ctx, o->assert_current};
  if (qa_app_qvm_display_syscall(call, display, out)) return true;
  if (ui && code == QA_QVM_UI_IMPORT_UI_GETCLIENTSTATE) {
    int32_t pointer = word_i32(call, 4);
    qa_app_qvm_client_state_info state = o->client_state(o->client_state_ctx);
    qa_qvm_memory_span span = qa_qvm_memory_get_span(guest, pointer, 3084, 0);
    qa_qvm_memory_fill_bytes(guest, span.byte_offset, span.byte_length, 0);
    qa_qvm_writable_view view;
    qa_qvm_memory_view(guest, pointer, 3084, 0, &view);
    int32_t fields[3] = {(int32_t)state.phase, (int32_t)state.connect_packet_count, (int32_t)state.client_number};
    memcpy(view.bytes, fields, sizeof fields);
    qa_qvm_memory_write_string(guest, pointer + 12, state.server_name, 1024);
    qa_qvm_memory_write_string(guest, pointer + 2060, state.message, 1024);
    *out = 0;
    return true;
  }
  if (code == (ui ? QA_QVM_UI_IMPORT_UI_MEMORY_REMAINING : QA_QVM_CGAME_IMPORT_CG_MEMORY_REMAINING)) {
    struct sysinfo info;
    double free_memory = sysinfo(&info) == 0 ? (double)info.freeram * info.mem_unit : 0;
    *out = (qa_qvm_host_result)(free_memory > 0x7fffffff ? 0x7fffffff : free_memory);
    return true;
  }
  if (code == (ui ? QA_QVM_UI_IMPORT_UI_UPDATESCREEN : QA_QVM_CGAME_IMPORT_CG_UPDATESCREEN)) {
    update_screen(update_screen_ctx);
    o->assert_current(o->assert_ctx);
    *out = 0;
    return true;
  }
  if (qa_app_qvm_client_input_syscall(call, o->input, out)) return true;
  if (code == (ui ? QA_QVM_UI_IMPORT_UI_KEY_GETCATCHER : QA_QVM_CGAME_IMPORT_CG_KEY_GETCATCHER)) {
    *out = (qa_qvm_host_result)o->key_catcher_get(o->key_catcher_ctx);
    return true;
  }
  if (code == (ui ? QA_QVM_UI_IMPORT_UI_KEY_SETCATCHER : QA_QVM_CGAME_IMPORT_CG_KEY_SETCATCHER)) {
    o->key_catcher_set(o->key_catcher_ctx, word_i32(call, 4));
    *out = 0;
    return true;
  }
  if (ui) {
    if (code == QA_QVM_UI_IMPORT_UI_KEY_KEYNUMTOSTRINGBUF) {
      char *text = qa_keynum_to_string(word_i32(call, 4));
      qa_qvm_memory_write_string(guest, word_i32(call, 8), text != NULL ? text : "", word_i32(call, 12));
      free(text);
      *out = 0;
      return true;
    }
    if (code == QA_QVM_UI_IMPORT_UI_KEY_GETOVERSTRIKEMODE) {
      *out = o->overstrike_get(o->local_ctx) ? 1 : 0;
      return true;
    }
    if (code == QA_QVM_UI_IMPORT_UI_KEY_SETOVERSTRIKEMODE) {
      o->overstrike_set(o->local_ctx, word_i32(call, 4) != 0);
      *out = 0;
      return true;
    }
    if (code == QA_QVM_UI_IMPORT_UI_GETCLIPBOARDDATA) {
      size_t length = 0;
      uint8_t *bytes = qa_read_sdl_clipboard(&length);
      char *text = malloc(length + 1);
      if (text == NULL) { free(bytes); *out = 0; return true; }
      if (bytes != NULL && length > 0) memcpy(text, bytes, length);
      text[length] = '\0';
      qa_qvm_memory_write_string(guest, word_i32(call, 4), text, word_i32(call, 8));
      free(text);
      free(bytes);
      *out = 0;
      return true;
    }
  }
  if (code == (ui ? QA_QVM_UI_IMPORT_UI_REAL_TIME : QA_QVM_CGAME_IMPORT_CG_REAL_TIME)) {
    time_t now = time(NULL);
    int32_t pointer = word_i32(call, 4);
    if (pointer != 0) {
      struct tm local;
      localtime_r(&now, &local);
      struct tm january = local, july = local;
      january.tm_mon = 0; january.tm_mday = 1;
      july.tm_mon = 6; july.tm_mday = 1;
      (void)mktime(&january);
      (void)mktime(&july);
      long offset_min = -(long)(local.tm_gmtoff / 60);
      long max_offset = january.tm_gmtoff > july.tm_gmtoff ? -(long)(january.tm_gmtoff / 60) : -(long)(july.tm_gmtoff / 60);
      /* JS getTimezoneOffset comparison: smaller offset = DST. */
      int dst = offset_min < max_offset ? 1 : 0;
      /* yday from UTC date difference equals local tm_yday here. */
      int32_t fields[9] = {local.tm_sec, local.tm_min, local.tm_hour, local.tm_mday, local.tm_mon,
                           local.tm_year, local.tm_wday, local.tm_yday, dst};
      qa_qvm_writable_view view;
      qa_qvm_memory_view(guest, pointer, 36, 0, &view);
      memcpy(view.bytes, fields, sizeof fields);
    }
    *out = (qa_qvm_host_result)(int32_t)now;
    return true;
  }
  if (!ui && code == QA_QVM_CGAME_IMPORT_CG_R_LIGHTFORPOINT) {
    qa_lighting_sample light = o->light_for_point(o->light_ctx, read_vector(guest, word_i32(call, 4)));
    write_vector(guest, word_i32(call, 8), light.ambient_light);
    write_vector(guest, word_i32(call, 12), light.directed_light);
    write_vector(guest, word_i32(call, 16), light.light_dir);
    *out = 1;
    return true;
  }
  return false;
}
