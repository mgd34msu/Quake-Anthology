/* Port of src/compat/qvm/common-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/common_syscalls.h"

#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static char *read_string(const qa_qvm_host_call *call, size_t word_index) {
  char *text = qa_qvm_game_memory_read_string(QA_SHARED_GUEST(call), qa_qvm_host_call_word(call, word_index));
  if (text == NULL) qa_range_error("out of memory");
  return text;
}

bool qa_qvm_common_syscall(const qa_qvm_host_call *call, qa_qvm_common_services services,
                           qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, services.role) != 0) return false;
  if (services.cvar_table != NULL
          ? qa_qvm_cvar_table_syscall(call->role, call->base.words, call->base.words_length, QA_SHARED_GUEST(call),
                                      services.cvar_table, out)
          : qa_qvm_cvar_syscall(call->role, call->base.words, call->base.words_length, QA_SHARED_GUEST(call),
                                services.cvars, out))
    return true;
  int32_t trap = call->code;
  bool ui = strcmp(services.role, "ui") == 0;
  bool game = strcmp(services.role, "qagame") == 0;
  if (trap == (ui ? QA_QVM_UI_IMPORT_UI_PRINT
                  : game ? QA_QVM_GAME_IMPORT_G_PRINT : QA_QVM_CGAME_IMPORT_CG_PRINT)) {
    char *text = read_string(call, 1);
    services.print(services.ctx, text);
    free(text);
    *out = 0;
    return true;
  }
  if (trap == (ui ? QA_QVM_UI_IMPORT_UI_ERROR
                  : game ? QA_QVM_GAME_IMPORT_G_ERROR : QA_QVM_CGAME_IMPORT_CG_ERROR)) {
    char *text = read_string(call, 1);
    if (strlen(text) > 4095) text[4095] = '\0';
    /* TS throws CommonError("drop", text); the C raise aborts. */
    qa_common_error(QA_COMMON_ERROR_DROP, text);
  }
  if (trap == (ui ? QA_QVM_UI_IMPORT_UI_MILLISECONDS
                  : game ? QA_QVM_GAME_IMPORT_G_MILLISECONDS
                         : QA_QVM_CGAME_IMPORT_CG_MILLISECONDS)) {
    *out = qa_int32(services.milliseconds(services.ctx));
    return true;
  }
  size_t argv_count = 0;
  const char *const *argv = NULL;
  if (call->has_command_arguments) {
    argv = call->command_arguments;
    argv_count = call->command_argument_count;
  } else {
    argv = services.arguments(services.ctx, &argv_count);
  }
  if (trap == (ui ? QA_QVM_UI_IMPORT_UI_ARGC
                  : game ? QA_QVM_GAME_IMPORT_G_ARGC : QA_QVM_CGAME_IMPORT_CG_ARGC)) {
    *out = (qa_qvm_host_result)argv_count;
    return true;
  }
  if (trap == (ui ? QA_QVM_UI_IMPORT_UI_ARGV
                  : game ? QA_QVM_GAME_IMPORT_G_ARGV : QA_QVM_CGAME_IMPORT_CG_ARGV)) {
    int32_t index = qa_qvm_host_call_word(call, 1);
    const char *value = index >= 0 && index < (int64_t)argv_count ? argv[index] : "";
    qa_qvm_game_memory_write_string(QA_SHARED_GUEST(call), qa_qvm_host_call_word(call, 2), value,
                               qa_qvm_host_call_word(call, 3));
    *out = 0;
    return true;
  }
  if (strcmp(services.role, "cgame") == 0) {
    switch (trap) {
      case QA_QVM_CGAME_IMPORT_CG_ARGS: {
        size_t total = 0;
        for (size_t i = 1; i < argv_count; i++) total += strlen(argv[i]) + 1;
        if (total >= 1024 && argv_count > 1) {
          /* Joined length excludes the final separator slot counted above. */
          if (total - 1 >= 1024) qa_range_error("Cmd_Args exceeds its source buffer");
        }
        char *value = malloc(total + 1);
        if (value == NULL) qa_range_error("out of memory");
        value[0] = '\0';
        for (size_t i = 1; i < argv_count; i++) {
          if (i > 1) strcat(value, " ");
          strcat(value, argv[i]);
        }
        if (strlen(value) >= 1024) {
          free(value);
          qa_range_error("Cmd_Args exceeds its source buffer");
        }
        qa_qvm_game_memory_write_string(QA_SHARED_GUEST(call), qa_qvm_host_call_word(call, 1), value,
                                   qa_qvm_host_call_word(call, 2));
        free(value);
        *out = 0;
        return true;
      }
      case QA_QVM_CGAME_IMPORT_CG_SENDCONSOLECOMMAND: {
        char *text = read_string(call, 1);
        services.commands.append(services.commands.ctx, text);
        free(text);
        *out = 0;
        return true;
      }
      case QA_QVM_CGAME_IMPORT_CG_ADDCOMMAND: {
        char *text = read_string(call, 1);
        services.commands.register_command(services.commands.ctx, text);
        free(text);
        *out = 0;
        return true;
      }
      case QA_QVM_CGAME_IMPORT_CG_REMOVECOMMAND: {
        char *text = read_string(call, 1);
        services.commands.remove_command(services.commands.ctx, text);
        free(text);
        *out = 0;
        return true;
      }
      case QA_QVM_CGAME_IMPORT_CG_SENDCLIENTCOMMAND: {
        char *text = read_string(call, 1);
        services.commands.reliable(services.commands.ctx, text);
        free(text);
        *out = 0;
        return true;
      }
      default: return false;
    }
  }
  if (game && trap == QA_QVM_GAME_IMPORT_G_REAL_TIME) {
    int32_t pointer = qa_qvm_host_call_word(call, 1);
    qa_qvm_calendar calendar;
    double result = services.real_time(services.ctx, pointer != 0,
                                       pointer != 0 ? &calendar : NULL);
    if (pointer != 0) {
      qa_qvm_writable_view output;
      qa_qvm_game_memory_view(QA_SHARED_GUEST(call), pointer, 36, 0, &output);
      double values[9] = {calendar.second, calendar.minute, calendar.hour, calendar.day,
                          calendar.month,  calendar.year,   calendar.weekday,
                          calendar.year_day, calendar.is_dst};
      for (int i = 0; i < 9; i++)
        qa_qvm_writable_view_set_int32(&output, i * 4, qa_int32(values[i]), true);
    }
    *out = qa_int32(result);
    return true;
  }
  if (trap != (game ? QA_QVM_GAME_IMPORT_G_SEND_CONSOLE_COMMAND
                    : QA_QVM_UI_IMPORT_UI_CMD_EXECUTETEXT))
    return false;
  int32_t when = qa_qvm_host_call_word(call, 1);
  int32_t pointer = qa_qvm_host_call_word(call, 2);
  switch (when) {
    case 0: {
      if (pointer == 0) {
        services.commands.execute_now(services.commands.ctx, NULL);
      } else {
        char *text = read_string(call, 2);
        services.commands.execute_now(services.commands.ctx, text);
        free(text);
      }
      *out = 0;
      return true;
    }
    case 1: {
      char *text = read_string(call, 2);
      services.commands.insert(services.commands.ctx, text);
      free(text);
      *out = 0;
      return true;
    }
    case 2: {
      char *text = read_string(call, 2);
      services.commands.append(services.commands.ctx, text);
      free(text);
      *out = 0;
      return true;
    }
    default: qa_common_error(QA_COMMON_ERROR_FATAL, "Cbuf_ExecuteText: bad exec_when");
  }
}
