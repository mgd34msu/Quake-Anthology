/* Port of src/content/q3/presentation/console.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/console.h"
#include "qa/network/common/js_number.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
static const char *COMMON[] = {"testgun", "testmodel", "nextframe", "prevframe", "nextskin", "prevskin", "viewpos",
    "+scores", "-scores", "+zoom", "-zoom", "sizeup", "sizedown", "weapnext", "weapprev", "weapon", "tell_target",
    "tell_attacker", "vtell_target", "vtell_attacker", "tcmd"};
static const char *MISSION[] = {"loadhud", "nextTeamMember", "prevTeamMember", "nextOrder", "confirmOrder",
    "denyOrder", "taskOffense", "taskDefense", "taskPatrol", "taskCamp", "taskFollow", "taskRetrieve", "taskEscort",
    "taskSuicide", "taskOwnFlag", "tauntKillInsult", "tauntPraise", "tauntTaunt", "tauntDeathInsult", "tauntGauntlet",
    "spWin", "spLose", "scoresDown", "scoresUp"};
static const char *FORWARDED[] = {"kill", "say", "say_team", "tell", "vsay", "vsay_team", "vtell", "vtaunt", "vosay",
    "vosay_team", "votell", "give", "god", "notarget", "noclip", "team", "follow", "levelshot", "addbot",
    "setviewpos", "callvote", "vote", "callteamvote", "teamvote", "stats", "teamtask", "loaddefered"};
const char *qa_q3_pres_console_cvar_name(qa_q3_pres_console_cvar cvar) {
  switch (cvar) {
    case QA_Q3_PRES_CONSOLE_CVAR_VIEWSIZE: return "cg_viewsize";
    case QA_Q3_PRES_CONSOLE_CVAR_CAMERA_ORBIT: return "cg_cameraOrbit";
    case QA_Q3_PRES_CONSOLE_CVAR_CURRENT_SELECTED_PLAYER: return "cg_currentSelectedPlayer";
  }
  return "cg_viewsize";
}
const char *const *qa_q3_pres_client_console_command_names(qa_q3_base_product product, size_t *out_count) {
  /* Retained per-product lists (never freed), matching TS readonly arrays. */
  static const char **base_list, **mission_list;
  static size_t base_count, mission_count;
  bool mission = product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  const char ***slot = mission ? &mission_list : &base_list;
  size_t *count_slot = mission ? &mission_count : &base_count;
  if (!*slot) {
    size_t total = sizeof COMMON / sizeof *COMMON + (mission ? sizeof MISSION / sizeof *MISSION : 0) + 2 +
                   sizeof FORWARDED / sizeof *FORWARDED;
    const char **list = malloc(total * sizeof *list);
    if (!list) {
      *out_count = 0;
      return NULL;
    }
    size_t n = 0;
    for (size_t i = 0; i < sizeof COMMON / sizeof *COMMON; i++) list[n++] = COMMON[i];
    if (mission) for (size_t i = 0; i < sizeof MISSION / sizeof *MISSION; i++) list[n++] = MISSION[i];
    list[n++] = "startOrbit";
    list[n++] = "loaddeferred";
    for (size_t i = 0; i < sizeof FORWARDED / sizeof *FORWARDED; i++) list[n++] = FORWARDED[i];
    *slot = list;
    *count_slot = n;
  }
  *out_count = *count_slot;
  return *slot;
}
static void fold_into(const char *input, char *out, size_t capacity) {
  size_t i = 0;
  for (; input[i] && i + 1 < capacity; i++) out[i] = (char)tolower((unsigned char)input[i]);
  out[i] = '\0';
}
static bool is_local_command(qa_q3_base_product product, const char *folded) {
  char candidate[64];
  for (size_t i = 0; i < sizeof COMMON / sizeof *COMMON; i++) {
    fold_into(COMMON[i], candidate, sizeof candidate);
    if (strcmp(candidate, folded) == 0) return true;
  }
  if (product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    for (size_t i = 0; i < sizeof MISSION / sizeof *MISSION; i++) {
      fold_into(MISSION[i], candidate, sizeof candidate);
      if (strcmp(candidate, folded) == 0) return true;
    }
  }
  return strcmp(folded, "startorbit") == 0 || strcmp(folded, "loaddeferred") == 0;
}
void qa_q3_pres_client_console_runtime_init(qa_q3_pres_client_console_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_game_static_state *static_state,
    qa_q3_pres_client_console_host host, qa_q3_pres_client_game_state *view_state,
    qa_q3_pres_client_game_state *weapons_state) {
  if (state->product != static_state->product || view_state != state || weapons_state != state)
    qa_range_error("Console services must share canonical cgame state");
  runtime->state = state;
  runtime->static_state = static_state;
  runtime->host = host;
  runtime->closed = false;
}
static void runtime_open(qa_q3_pres_client_console_runtime *runtime) {
  if (runtime->closed) qa_range_error("Cgame console runtime is closed");
}
void qa_q3_pres_client_console_runtime_initialize_commands(qa_q3_pres_client_console_runtime *runtime) {
  runtime_open(runtime);
  size_t count = 0;
  const char *const *names = qa_q3_pres_client_console_command_names(runtime->state->product, &count);
  for (size_t i = 0; i < count; i++) runtime->host.add_command(runtime->host.ctx, names[i]);
}
void qa_q3_pres_client_console_runtime_dispose(qa_q3_pres_client_console_runtime *runtime) {
  runtime->closed = true;
  (void)runtime->host.clear_test_model(runtime->host.view_ctx);
  (void)runtime->host.reset_clients(runtime->host.clients_ctx);
}
bool qa_q3_pres_client_console_runtime_handles(qa_q3_pres_client_console_runtime *runtime, const char *name) {
  runtime_open(runtime);
  char folded[128];
  fold_into(name, folded, sizeof folded);
  return is_local_command(runtime->state->product, folded);
}
double qa_q3_pres_client_console_runtime_crosshair_player(qa_q3_pres_client_console_runtime *runtime) {
  return runtime->state->time > qa_int32(runtime->state->crosshair_client_time + 1000)
             ? -1
             : runtime->state->crosshair_client_num;
}
double qa_q3_pres_client_console_runtime_last_attacker(qa_q3_pres_client_console_runtime *runtime) {
  if (runtime->state->attacker_time == 0) return -1;
  if (!runtime->state->snap) qa_range_error("CG_LastAttacker requires cg.snap");
  return qa_q3_base_player_state_slots_get(&runtime->state->snap->player_state.persistant, QA_Q3_BASE_PERS_ATTACKER);
}
static const char *argument(const char *const *argv, size_t argc, size_t index, char *buffer, size_t size) {
  const char *value = index < argc ? argv[index] : "";
  size_t length = strlen(value);
  if (length >= size) length = size - 1;
  memcpy(buffer, value, length);
  buffer[length] = '\0';
  return buffer;
}
static void set_cvar(qa_q3_pres_client_console_runtime *runtime, const char *name, const char *value) {
  runtime->host.cvar_set(runtime->host.cvars_ctx, name, value);
}
static void immediate(qa_q3_pres_client_console_runtime *runtime, const char *name, char *out, size_t size) {
  const char *value = "";
  (void)runtime->host.cvar_get(runtime->host.cvars_ctx, name, &value);
  snprintf(out, size, "%.1023s", value ? value : "");
}
static void require_hud(qa_q3_pres_client_console_runtime *runtime) {
  if (!runtime->host.hud_available)
    qa_range_error("Cgame HUD unavailable: %s", runtime->host.hud_reason ? runtime->host.hud_reason : "");
}
static void require_orders(qa_q3_pres_client_console_runtime *runtime) {
  if (!runtime->host.orders_available)
    qa_range_error("Cgame team orders unavailable: %s",
                   runtime->host.orders_reason ? runtime->host.orders_reason : "");
}
static double vm_cvar_integer(qa_q3_pres_client_console_runtime *runtime, qa_q3_pres_console_cvar name) {
  qa_cvar_snapshot snapshot = runtime->host.read_vm_cvar(runtime->host.ctx, name);
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
static void scores_down(qa_q3_pres_client_console_runtime *runtime) {
  qa_q3_pres_client_game_state *state = runtime->state;
  if (state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK)
    (void)runtime->host.build_spectator_string(runtime->host.commands_ctx);
  if (qa_int32(state->scores_request_time + 2000) < state->time) {
    state->scores_request_time = state->time;
    runtime->host.send_client_command(runtime->host.ctx, "score");
    if (!state->show_scores) {
      state->show_scores = true;
      state->num_scores = 0;
    }
  } else state->show_scores = true;
}
static void next_order(qa_q3_pres_client_console_runtime *runtime) {
  if (!runtime->state->snap) qa_range_error("CG_NextOrder requires cg.snap");
  double client_num = runtime->state->snap->player_state.client_num;
  if (client_num < 0 || client_num >= 64)
    qa_range_error("Console source array index %s outside 64", QA_JS_NUMBER(client_num));
  qa_q3_pres_client_info *client = &runtime->static_state->client_info[(size_t)client_num];
  double selected_index = vm_cvar_integer(runtime, QA_Q3_PRES_CONSOLE_CVAR_CURRENT_SELECTED_PLAYER);
  if (!(selected_index >= 0 && selected_index < 32) || selected_index != trunc(selected_index)) /* sortedTeamPlayers has 32 slots */
    qa_range_error("Console source array index %s outside 32", QA_JS_NUMBER(selected_index));
  if (!client->team_leader && runtime->state->sorted_team_players[(size_t)selected_index] != client_num) return;
  qa_q3_pres_client_game_static_state *cgs = runtime->static_state;
  if (cgs->current_order < 7) {
    cgs->current_order = qa_int32(cgs->current_order + 1);
    if (cgs->current_order == 5) {
      require_orders(runtime);
      if (!runtime->host.other_team_has_flag(runtime->host.orders_ctx)) cgs->current_order++;
    }
    if (cgs->current_order == 6) {
      require_orders(runtime);
      if (!runtime->host.your_team_has_flag(runtime->host.orders_ctx)) cgs->current_order++;
    }
  } else cgs->current_order = 1;
  cgs->order_pending = true;
  cgs->order_time = qa_int32(runtime->state->time + 3000);
}
static void task_command(qa_q3_pres_client_console_runtime *runtime, const char *voice, double task_value) {
  char buffer[256];
  snprintf(buffer, sizeof buffer, "cmd vsay_team %s\n", voice);
  runtime->host.send_console_command(runtime->host.ctx, buffer);
  qa_q3_game_format_arg args[1] = {qa_q3_game_format_number(task_value)};
  char *text = qa_q3_game_format("teamtask %d\n", args, 1, 1024);
  if (text) {
    runtime->host.send_client_command(runtime->host.ctx, text);
    free(text);
  }
}
static void reset_adapter(void *ctx, qa_q3_pres_client_entity *entity) {
  qa_q3_pres_client_console_runtime *runtime = ctx;
  runtime->host.reset_player_entity(runtime->host.ctx, entity);
}
static bool dispatch(qa_q3_pres_client_console_runtime *runtime, const char *name, const char *const *argv,
                     size_t argc) {
  qa_q3_pres_client_game_state *state = runtime->state;
  qa_q3_pres_client_game_static_state *cgs = runtime->static_state;
  qa_q3_pres_client_console_host *host = &runtime->host;
  char buffer[1024];
  if (strcmp(name, "testgun") == 0 || strcmp(name, "testmodel") == 0) {
    const char *model_name = argc < 2 ? NULL : argument(argv, argc, 1, buffer, sizeof buffer);
    char number_buffer[128];
    double parsed = 0;
    const double *back_lerp = NULL;
    if (argc == 3) {
      parsed = qa_game_atof(argument(argv, argc, 2, number_buffer, sizeof number_buffer));
      back_lerp = &parsed;
    }
    return strcmp(name, "testgun") == 0 ? host->test_gun(host->view_ctx, model_name, back_lerp)
                                        : host->test_model(host->view_ctx, model_name, back_lerp);
  }
  if (strcmp(name, "nextframe") == 0) { host->next_model_frame(host->view_ctx); return true; }
  if (strcmp(name, "prevframe") == 0) { host->previous_model_frame(host->view_ctx); return true; }
  if (strcmp(name, "nextskin") == 0) { host->next_model_skin(host->view_ctx); return true; }
  if (strcmp(name, "prevskin") == 0) { host->previous_model_skin(host->view_ctx); return true; }
  if (strcmp(name, "+zoom") == 0) { host->zoom_down(host->view_ctx); return true; }
  if (strcmp(name, "-zoom") == 0) { host->zoom_up(host->view_ctx); return true; }
  if (strcmp(name, "weapnext") == 0) { host->next_weapon(host->weapons_ctx); return true; }
  if (strcmp(name, "weapprev") == 0) { host->previous_weapon(host->weapons_ctx); return true; }
  if (strcmp(name, "weapon") == 0) {
    host->select_weapon(host->weapons_ctx, qa_game_atoi(argument(argv, argc, 1, buffer, sizeof buffer)));
    return true;
  }
  if (strcmp(name, "viewpos") == 0) {
    qa_q3_game_format_arg args[4] = {qa_q3_game_format_number(qa_qvm_float_to_int(state->refdef.view_origin.x)),
        qa_q3_game_format_number(qa_qvm_float_to_int(state->refdef.view_origin.y)),
        qa_q3_game_format_number(qa_qvm_float_to_int(state->refdef.view_origin.z)),
        qa_q3_game_format_number(qa_qvm_float_to_int(state->refdef_view_angles.y))};
    char *text = qa_q3_game_format("(%i %i %i) : %i\n", args, 4, 1024);
    if (!text) return false;
    host->print(host->ctx, text);
    free(text);
    return true;
  }
  if (strcmp(name, "sizeup") == 0 || strcmp(name, "sizedown") == 0) {
    double left = vm_cvar_integer(runtime, QA_Q3_PRES_CONSOLE_CVAR_VIEWSIZE);
    double right = (strcmp(name, "sizeup") == 0 ? 10 : -10);
    double value = qa_int32(left +
                            right);
    qa_q3_game_format_arg args[1] = {qa_q3_game_format_number(value)};
    char *text = qa_q3_game_format("%i", args, 1, 1024);
    if (!text) return false;
    set_cvar(runtime, "cg_viewsize", text);
    free(text);
    return true;
  }
  if (strcmp(name, "+scores") == 0) { scores_down(runtime); return true; }
  if (strcmp(name, "-scores") == 0) {
    if (state->show_scores) {
      state->show_scores = false;
      state->score_fade_time = state->time;
    }
    return true;
  }
  if (strcmp(name, "tcmd") == 0) {
    double target = qa_q3_pres_client_console_runtime_crosshair_player(runtime);
    if (target != 0) {
      char small[4];
      qa_q3_game_format_arg args[2] = {qa_q3_game_format_number(target),
          qa_q3_game_format_number(qa_game_atoi(argument(argv, argc, 1, small, sizeof small)))};
      char *text = qa_q3_game_format("gc %i %i", args, 2, 1024);
      if (!text) return false;
      host->send_console_command(host->ctx, text);
      free(text);
    }
    return true;
  }
  if (!strcmp(name, "tell_target") || !strcmp(name, "tell_attacker") || !strcmp(name, "vtell_target") ||
      !strcmp(name, "vtell_attacker")) {
    bool is_target = strstr(name, "target") != NULL;
    double target = is_target ? qa_q3_pres_client_console_runtime_crosshair_player(runtime)
                              : qa_q3_pres_client_console_runtime_last_attacker(runtime);
    if (target == -1) return true;
    char joined[1024];
    joined[0] = '\0';
    size_t used = 0;
    for (size_t i = 1; i < argc; i++) {
      size_t piece = strlen(argv[i]);
      if (used + piece + 1 >= 1024) qa_range_error("Cmd_Args exceeds MAX_STRING_CHARS");
      if (i > 1) joined[used++] = ' ';
      memcpy(joined + used, argv[i], piece);
      used += piece;
      joined[used] = '\0';
    }
    char truncated_args[128];
    snprintf(truncated_args, sizeof truncated_args, "%.127s", joined);
    qa_q3_game_format_arg args[3] = {qa_q3_game_format_string(name[0] == 'v' ? "vtell" : "tell"),
        qa_q3_game_format_number(target), qa_q3_game_format_string(truncated_args)};
    char *text = qa_q3_game_format("%s %i %s", args, 3, 128);
    if (!text) return false;
    host->send_client_command(host->ctx, text);
    free(text);
    return true;
  }
  if (strcmp(name, "loaddeferred") == 0)
    return host->load_deferred_players(host->clients_ctx, reset_adapter, runtime);
  if (strcmp(name, "startorbit") == 0) {
    char developer[64];
    immediate(runtime, "developer", developer, sizeof developer);
    if (qa_game_atoi(developer) == 0) return true;
    qa_cvar_snapshot snapshot = host->read_vm_cvar(host->ctx, QA_Q3_PRES_CONSOLE_CVAR_CAMERA_ORBIT);
    double orbit = snapshot.numeric_value;
    qa_cvar_read_free(&snapshot);
    if (orbit != 0) {
      set_cvar(runtime, "cg_cameraOrbit", "0");
      set_cvar(runtime, "cg_thirdPerson", "0");
    } else {
      set_cvar(runtime, "cg_cameraOrbit", "5");
      set_cvar(runtime, "cg_thirdPerson", "1");
      set_cvar(runtime, "cg_thirdPersonAngle", "0");
      set_cvar(runtime, "cg_thirdPersonRange", "100");
    }
    return true;
  }
  if (strcmp(name, "loadhud") == 0) {
    require_hud(runtime);
    host->hud_reset_strings(host->hud_ctx);
    host->hud_reset_menus(host->hud_ctx);
    char path[1024];
    immediate(runtime, "cg_hudFiles", path, sizeof path);
    if (!host->hud_load_menus(host->hud_ctx, path[0] ? path : "ui/hud.txt")) return false;
    runtime_open(runtime);
    host->hud_clear_scoreboard(host->hud_ctx);
    return true;
  }
  if (strcmp(name, "scoresdown") == 0 || strcmp(name, "scoresup") == 0) {
    require_hud(runtime);
    if (host->hud_menu_scoreboard(host->hud_ctx) && state->score_board_showing) {
      const double feeders[3] = {11, 5, 6};
      for (int i = 0; i < 3; i++)
        if (!host->hud_scroll_feeder(host->hud_ctx, feeders[i], strcmp(name, "scoresdown") == 0)) return false;
    }
    return true;
  }
  if (strcmp(name, "nextteammember") == 0) {
    require_orders(runtime);
    host->select_next_player(host->orders_ctx);
    return true;
  }
  if (strcmp(name, "prevteammember") == 0) {
    require_orders(runtime);
    host->select_previous_player(host->orders_ctx);
    return true;
  }
  if (strcmp(name, "nextorder") == 0) { next_order(runtime); return true; }
  if (strcmp(name, "confirmorder") == 0 || strcmp(name, "denyorder") == 0) {
    bool yes = strcmp(name, "confirmorder") == 0;
    qa_q3_game_format_arg args[2] = {qa_q3_game_format_number(cgs->accept_leader),
                                     qa_q3_game_format_string(yes ? "yes" : "no")};
    char *text = qa_q3_game_format("cmd vtell %d %s\n", args, 2, 1024);
    if (!text) return false;
    host->send_console_command(host->ctx, text);
    free(text);
    host->send_console_command(host->ctx, yes ? "+button5; wait; -button5" : "+button6; wait; -button6");
    if (state->time < cgs->accept_order_time) {
      if (yes) {
        qa_q3_game_format_arg task_args[1] = {qa_q3_game_format_number(cgs->accept_task)};
        char *task_text = qa_q3_game_format("teamtask %d\n", task_args, 1, 1024);
        if (!task_text) return false;
        host->send_client_command(host->ctx, task_text);
        free(task_text);
      }
      cgs->accept_order_time = 0;
    }
    return true;
  }
  if (strcmp(name, "taskoffense") == 0) {
    task_command(runtime, cgs->game_type == QA_Q3_BASE_GT_CTF || cgs->game_type == QA_Q3_BASE_GT_1FCTF
                              ? "ongetflag" : "onoffense", 1);
    return true;
  }
  if (strcmp(name, "taskdefense") == 0) { task_command(runtime, "ondefense", 2); return true; }
  if (strcmp(name, "taskpatrol") == 0) { task_command(runtime, "onpatrol", 3); return true; }
  if (strcmp(name, "taskcamp") == 0) { task_command(runtime, "oncamp", 7); return true; }
  if (strcmp(name, "taskfollow") == 0) { task_command(runtime, "onfollow", 4); return true; }
  if (strcmp(name, "taskretrieve") == 0) { task_command(runtime, "onreturnflag", 5); return true; }
  if (strcmp(name, "taskescort") == 0) { task_command(runtime, "onfollowcarrier", 6); return true; }
  if (strcmp(name, "taskownflag") == 0) { host->send_console_command(host->ctx, "cmd vsay_team ihaveflag\n"); return true; }
  if (strcmp(name, "tasksuicide") == 0) {
    double target = qa_q3_pres_client_console_runtime_crosshair_player(runtime);
    if (target != -1) {
      qa_q3_game_format_arg args[1] = {qa_q3_game_format_number(target)};
      char *text = qa_q3_game_format("tell %i suicide", args, 1, 128);
      if (!text) return false;
      host->send_client_command(host->ctx, text);
      free(text);
    }
    return true;
  }
  if (strcmp(name, "tauntkillinsult") == 0) { host->send_console_command(host->ctx, "cmd vsay kill_insult\n"); return true; }
  if (strcmp(name, "tauntpraise") == 0) { host->send_console_command(host->ctx, "cmd vsay praise\n"); return true; }
  if (strcmp(name, "taunttaunt") == 0) { host->send_console_command(host->ctx, "cmd vtaunt\n"); return true; }
  if (strcmp(name, "tauntdeathinsult") == 0) { host->send_console_command(host->ctx, "cmd vsay death_insult\n"); return true; }
  if (strcmp(name, "tauntgauntlet") == 0) { host->send_console_command(host->ctx, "cmd vsay kill_guantlet\n"); return true; }
  if (strcmp(name, "spwin") == 0 || strcmp(name, "splose") == 0) {
    bool win = strcmp(name, "spwin") == 0;
    set_cvar(runtime, "cg_cameraOrbit", "2");
    set_cvar(runtime, "cg_cameraOrbitDelay", "35");
    set_cvar(runtime, "cg_thirdPerson", "1");
    set_cvar(runtime, "cg_thirdPersonAngle", "0");
    set_cvar(runtime, "cg_thirdPersonRange", "100");
    host->add_buffered_sound(host->ctx, host->sound(host->ctx, win ? QA_Q3_PRES_CONSOLE_SOUND_WINNER
                                                                    : QA_Q3_PRES_CONSOLE_SOUND_LOSER));
    host->center_print(host->ctx, win ? "YOU WIN!" : "YOU LOSE...", 144, 0);
    return true;
  }
  qa_range_error("Registered cgame console command has no handler: %s", name);
}
typedef struct execute_job {
  qa_q3_pres_client_console_runtime *runtime; const char *const *argv; size_t argc; bool result;
} execute_job;
static void execute_body(void *ctx) {
  execute_job *job = ctx;
  qa_q3_pres_client_console_runtime *runtime = job->runtime;
  runtime_open(runtime);
  char first[1024];
  const char *raw = argument(job->argv, job->argc, 0, first, sizeof first);
  char name[1024];
  fold_into(raw, name, sizeof name);
  if (!qa_q3_pres_client_console_runtime_handles(runtime, name)) { job->result = false; return; }
  job->result = dispatch(runtime, name, job->argv, job->argc);
  runtime_open(runtime);
}
bool qa_q3_pres_client_console_runtime_execute(qa_q3_pres_client_console_runtime *runtime,
                                               const char *const *argv, size_t argc) {
  if (argc > 1024) qa_range_error("Console command exceeds MAX_STRING_TOKENS");
  size_t total = 0;
  for (size_t i = 0; i < argc; i++) total += strlen(argv[i]) + 1;
  if (total > 8192 + 1024) qa_range_error("Console command exceeds source token storage");
  execute_job job = {runtime, argv, argc, false};
  qa_caught_error caught;
  if (qa_try(execute_body, &job, &caught)) return job.result;
  if (runtime->closed) qa_rethrow(&caught);
  /* A failed command is reported and the console stays usable; dispose is for lifecycle teardown only. */
  size_t length = strlen(caught.message) + sizeof "Console command failed: \n";
  char *text = malloc(length);
  if (!text) return false;
  snprintf(text, length, "Console command failed: %s\n", caught.message);
  runtime->host.print(runtime->host.ctx, text);
  free(text);
  return true;
}
