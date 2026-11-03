/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/library.h"
#include "qa/network/common/js_number.h"
#include "../library/library_internal.h"
#include <math.h>

static void print_severity(qa_bot_library_bots_a *l, int severity, const char *text) {
  l->options.print(l->options.ctx, severity, text);
}
static double time_of(qa_bot_library_bots_a *l) { return l->log.time; }
static bool reload_characters(void *ctx) {
  qa_bot_library_bots_a *l = ctx;
  return qa_bot_lib_vars_get_value(&l->variables, "bot_reloadcharacters") != 0;
}
static void print_fn(void *ctx, int severity, const char *text) {
  print_severity(ctx, severity, text);
}
static void print_info(void *ctx, const char *text) { print_severity(ctx, 1, text); }
static void globals_report(void *ctx, const qa_ui_script_diagnostic *diagnostic) {
  print_severity(ctx, diagnostic->severity == QA_UI_SCRIPT_SEVERITY_WARNING ? 2 : 3,
                 diagnostic->message);
}
static void character_report(void *ctx, const qa_character_diagnostic *diagnostic) {
  print_severity(ctx, diagnostic->severity == QA_CHARACTER_WARNING ? 2 : 3, diagnostic->message);
}
static void weapon_report(void *ctx, const qa_weapon_diagnostic *diagnostic) {
  print_severity(ctx, diagnostic->severity == QA_WEAPON_WARNING ? 2 : 3, diagnostic->message);
}
static void goal_report(void *ctx, const qa_goal_diagnostic *diagnostic) {
  print_severity(ctx,
                 diagnostic->severity == QA_GOAL_WARNING ? 2
                 : diagnostic->severity == QA_GOAL_ERROR ? 3
                                                         : 1,
                 diagnostic->message);
}
static void chat_report(void *ctx, const qa_chat_diagnostic *diagnostic) {
  print_severity(ctx,
                 diagnostic->severity == QA_CHAT_SEVERITY_WARNING ? 2
                 : diagnostic->severity == QA_CHAT_SEVERITY_ERROR ||
                         diagnostic->severity == QA_CHAT_SEVERITY_FATAL
                     ? 3
                     : 1,
                 diagnostic->message);
}
static void log_write(void *ctx, const char *text) {
  qa_bot_log_write(&((qa_bot_library_bots_a *)ctx)->log, text);
}
static bool log_has_file(void *ctx) {
  return qa_bot_log_has_file(&((qa_bot_library_bots_a *)ctx)->log);
}
static void log_file_write(void *ctx, const char *text) {
  (void)qa_bot_log_file_write(&((qa_bot_library_bots_a *)ctx)->log, text);
}
/* TS private capacity(name). */
static double capacity(qa_bot_library_bots_a *l, const char *name) {
  double value = trunc((double)qa_bot_lib_vars_value(&l->variables, name, "32"));
  if (value < 1) {
    qa_bot_lib_vars_set(&l->variables, name, "32");
    return 32;
  }
  return value;
}
static double max_weapon_info(void *ctx) { return capacity(ctx, "max_weaponinfo"); }
static double max_projectile_info(void *ctx) { return capacity(ctx, "max_projectileinfo"); }
static double clock_fn(void *ctx) { return time_of(ctx); }
static double game_type(void *ctx) {
  return qa_bot_lib_vars_value(&((qa_bot_library_bots_a *)ctx)->variables, "g_gametype", "0");
}
static double item_info_get(void *ctx) {
  return qa_bot_lib_vars_value(&((qa_bot_library_bots_a *)ctx)->variables, "max_iteminfo", "256");
}
/* TS String(value) of the integral max_iteminfo. */
static void item_info_set(void *ctx, double value) {
  char text[64];
  snprintf(text, sizeof text, "%s", QA_JS_NUMBER(value));
  qa_bot_lib_vars_set(&((qa_bot_library_bots_a *)ctx)->variables, "max_iteminfo", text);
}
static double max_level_items(void *ctx) {
  return qa_bot_lib_vars_value(&((qa_bot_library_bots_a *)ctx)->variables, "max_levelitems", "256");
}
static double dropped_weight(void *ctx) {
  return qa_bot_lib_vars_value(&((qa_bot_library_bots_a *)ctx)->variables, "droppedweight", "1000");
}
static bool developer(void *ctx) {
  return qa_bot_lib_vars_get_value(&((qa_bot_library_bots_a *)ctx)->variables, "bot_developer") != 0;
}
/* TS `*clientCommand(client, command) { options.clientCommand(...); return undefined; }`:
 * the body runs on the first step, not at the call. */
typedef struct client_command_step {
  qa_bot_library_bots_a *library;
  int64_t client;
  char *command;
} client_command_step;
static qa_call_step_result client_command_next(void *ctx, const char *thrown) {
  client_command_step *step = ctx;
  qa_call_step_result result;
  memset(&result, 0, sizeof result);
  result.done = true;
  result.thrown = thrown;
  if (thrown == NULL)
    step->library->options.client_command(step->library->options.ctx, step->client, step->command);
  return result;
}
static void client_command_release(void *ctx) {
  client_command_step *step = ctx;
  free(step->command);
  free(step);
}
static qa_call_steps client_command(void *ctx, int64_t client, const char *command) {
  client_command_step *step = lib_alloc(sizeof *step);
  step->library = ctx;
  step->client = client;
  step->command = lib_strdup(command);
  qa_call_steps steps;
  memset(&steps, 0, sizeof steps);
  steps.next = client_command_next;
  steps.ctx = step;
  steps.release = client_command_release;
  return steps;
}
static double max_messages(void *ctx) {
  return qa_bot_lib_vars_value(&((qa_bot_library_bots_a *)ctx)->variables, "max_messages", "1024");
}
/* TS getters of synonymFile/randomFile/matchFile/replyFile. */
static const char *chat_file_name(void *ctx, const char *option) {
  qa_bot_lib_vars *v = &((qa_bot_library_bots_a *)ctx)->variables;
  if (strcmp(option, "synonymFile") == 0)
    return qa_bot_lib_vars_string(v, "synfile", "syn.c");
  if (strcmp(option, "randomFile") == 0)
    return qa_bot_lib_vars_string(v, "rndfile", "rnd.c");
  if (strcmp(option, "matchFile") == 0)
    return qa_bot_lib_vars_string(v, "matchfile", "match.c");
  if (strcmp(option, "replyFile") == 0)
    return qa_bot_lib_vars_string(v, "rchatfile", "rchat.c");
  return NULL;
}
static bool no_chat(void *ctx) {
  return qa_bot_lib_vars_value(&((qa_bot_library_bots_a *)ctx)->variables, "nochat", "0") != 0;
}
static bool test_initial_chats(void *ctx) {
  return qa_bot_lib_vars_get_value(&((qa_bot_library_bots_a *)ctx)->variables, "bot_testichat") != 0;
}
static bool test_reply_chats(void *ctx) {
  return qa_bot_lib_vars_get_value(&((qa_bot_library_bots_a *)ctx)->variables, "bot_testrchat") != 0;
}
static double move_time(void *ctx) { return time_of(ctx); }
static qa_bot_lib_var *move_lib_var(void *ctx, const char *name, const char *value) {
  return qa_bot_lib_vars_get_or_create(&((qa_bot_library_bots_a *)ctx)->variables, name, value);
}
static qa_bot_log_open_result open_log(void *ctx, const char *filename) {
  qa_bot_library_bots_a *l = ctx;
  return l->options.open_log(l->options.ctx, filename);
}
static qa_bot_log_open_result resume_log(void *ctx, const char *filename, int64_t position) {
  qa_bot_library_bots_a *l = ctx;
  return l->options.resume_log(l->options.ctx, filename, position);
}
static const uint8_t *read_file(void *ctx, const char *path, size_t *out_len) {
  qa_bot_library_bots_a *l = ctx;
  return l->options.read_file(l->options.ctx, path, out_len);
}

void qa_bot_library_bots_a_init(qa_bot_library_bots_a *l,
                                const qa_source_bot_library_options_bots_a *options) {
  memset(l, 0, sizeof *l);
  l->options = *options;
  qa_bot_memory_init(&l->memory);
  qa_bot_lib_vars_init_memory(&l->variables, &l->memory);
  qa_ui_script_memory script_memory = lib_script_memory(&l->memory);
  l->globals = qa_ui_script_global_defines_create(globals_report, l, &script_memory);
  if (l->globals == NULL)
    qa_range_error("Out of memory creating bot script globals");

  qa_bot_script_sources_init(&l->sources);
  l->sources.globals = l->globals;
  l->sources.script_memory = script_memory;
  l->sources.read_file = read_file;
  l->sources.read_ctx = l;
  l->sources.print = print_fn;
  l->sources.print_ctx = l;
  l->sources.common_print = print_info;
  l->sources.common_print_ctx = l;
  l->reader = qa_bot_script_sources_reader(&l->sources);

  qa_bot_log_init(&l->log, &l->variables);
  l->log.print = print_fn;
  l->log.print_ctx = l;
  l->log.open_file = open_log;
  l->log.open_ctx = l;
  if (options->resume_log != NULL) {
    l->log.resume_file = resume_log;
    l->log.resume_ctx = l;
  }

  qa_weight_config_store_bots_a_init(&l->weights);
  qa_weight_config_store_options weights;
  memset(&weights, 0, sizeof weights);
  weights.memory = &l->memory;
  weights.reload_characters_fn = reload_characters;
  weights.reload_ctx = l;
  weights.print = print_fn;
  weights.print_ctx = l;
  qa_weight_config_store_bots_a_configure(&l->weights, &l->reader, &weights);

  qa_bot_character_library_init(&l->characters);
  qa_bot_character_options characters;
  memset(&characters, 0, sizeof characters);
  characters.memory = &l->memory;
  characters.log_write = log_write;
  characters.log_has_file = log_has_file;
  characters.log_ctx = l;
  characters.reload_characters = reload_characters;
  characters.reload_ctx = l;
  characters.report = character_report;
  characters.report_ctx = l;
  qa_bot_character_library_configure(&l->characters, &l->reader, &characters);

  qa_weapon_ai_bots_a_init(&l->weapons);
  qa_weapon_ai_options weapons;
  memset(&weapons, 0, sizeof weapons);
  weapons.memory = &l->memory;
  weapons.max_weapon_info.fn = max_weapon_info;
  weapons.max_weapon_info.ctx = l;
  weapons.max_projectile_info.fn = max_projectile_info;
  weapons.max_projectile_info.ctx = l;
  weapons.report = weapon_report;
  weapons.report_ctx = l;
  qa_weapon_ai_bots_a_configure(&l->weapons, &l->reader, &l->weights, &weapons);

  qa_bot_goal_library_init(&l->goals);
  qa_bot_goal_options goals;
  memset(&goals, 0, sizeof goals);
  goals.memory = &l->memory;
  goals.resolver = &l->reader;
  goals.weight_store = &l->weights;
  goals.log_write = log_write;
  goals.log_ctx = l;
  goals.random = options->random;
  goals.clock = clock_fn;
  goals.clock_ctx = l;
  goals.game_type = game_type;
  goals.game_type_ctx = l;
  goals.max_item_info_get = item_info_get;
  goals.max_item_info_set = item_info_set;
  goals.max_item_info_ctx = l;
  goals.max_level_items = max_level_items;
  goals.max_level_items_ctx = l;
  goals.dropped_weight = dropped_weight;
  goals.dropped_weight_ctx = l;
  goals.developer = developer;
  goals.developer_ctx = l;
  goals.report = goal_report;
  goals.report_ctx = l;
  qa_bot_goal_library_configure(&l->goals, &goals);

  qa_bot_action_buffer_init(&l->actions);
  qa_bot_action_host action_host = {client_command, l};
  qa_bot_action_buffer_bind(&l->actions, action_host, &l->memory);

  qa_bot_chat_library_init(&l->chat);
  qa_bot_chat_host chat_host;
  memset(&chat_host, 0, sizeof chat_host);
  chat_host.client_command = client_command;
  chat_host.time = clock_fn;
  chat_host.report = chat_report;
  chat_host.ctx = l;
  chat_host.random = options->random;
  qa_bot_chat_options chat;
  memset(&chat, 0, sizeof chat);
  chat.ctx = l;
  chat.log_write = log_write;
  chat.log_has_file = log_has_file;
  chat.log_file_write = log_file_write;
  chat.max_messages_fn = max_messages;
  chat.file_name_fn = chat_file_name;
  chat.no_chat = no_chat;
  chat.reload_characters = reload_characters;
  chat.test_initial_chats = test_initial_chats;
  chat.test_reply_chats = test_reply_chats;
  chat.developer = developer;
  qa_bot_chat_library_bind(&l->chat, qa_bot_chat_script_reader_from_sources(&l->sources), chat_host,
                           &chat, &l->memory);

  qa_bot_move_state_store_init(&l->move_states);
  qa_bot_move_state_host move_host = {l, move_time, print_fn, move_lib_var};
  qa_bot_move_state_store_bind(&l->move_states, move_host, &l->memory);
}
void qa_bot_library_bots_a_destroy(qa_bot_library_bots_a *l) {
  qa_bot_chat_library_destroy(&l->chat);
  qa_bot_goal_library_destroy(&l->goals);
  qa_weapon_ai_bots_a_destroy(&l->weapons);
  qa_bot_character_library_destroy(&l->characters);
  qa_weight_config_store_bots_a_destroy(&l->weights);
  qa_bot_action_buffer_destroy(&l->actions);
  qa_bot_move_state_store_destroy(&l->move_states);
  qa_bot_log_destroy(&l->log);
  qa_bot_script_sources_destroy(&l->sources);
  qa_ui_script_global_defines_destroy(l->globals);
  qa_bot_lib_vars_destroy(&l->variables);
  qa_bot_memory_destroy(&l->memory);
}
bool qa_bot_library_debug_build(const qa_bot_library_bots_a *l) { return l->options.debug; }
double qa_bot_library_time_bots_a(const qa_bot_library_bots_a *l) { return l->log.time; }
static void fail(const char *message) {
  fprintf(stderr, "%s\n", message);
  abort();
}
int64_t qa_bot_library_setup_bots_a(qa_bot_library_bots_a *l) {
  if (l->closed)
    fail("Bot behavior library is closed");
  (void)qa_bot_log_open(&l->log, "botlib.log");
  print_severity(l, 1, "------- BotLib Initialization -------\n");
  qa_bot_action_buffer_setup(&l->actions,
                             (int64_t)trunc(qa_bot_lib_vars_value(&l->variables, "maxclients", "128")));
  qa_weapon_load_result weapons =
      qa_weapon_ai_setup(&l->weapons, qa_bot_lib_vars_string(&l->variables, "weaponconfig", "weapons.c"));
  if (weapons != 0)
    return (int64_t)weapons;
  int64_t goals =
      qa_bot_goal_setup(&l->goals, qa_bot_lib_vars_string(&l->variables, "itemconfig", "items.c"));
  if (goals != 0)
    return goals;
  (void)qa_bot_lib_vars_get_or_create(&l->variables, "droppedweight", "1000");
  qa_bot_chat_library_setup(&l->chat);
  int64_t movement = qa_bot_move_state_store_setup(&l->move_states);
  if (movement != 0)
    return movement;
  l->initialized = true;
  return 0;
}
int64_t qa_bot_library_load_map_bots_a(qa_bot_library_bots_a *l, const qa_goal_world *world) {
  if (!l->initialized || l->closed)
    fail("Initialize bot behavior before loading its map goals");
  qa_bot_goal_init_level_items(&l->goals, world);
  return 0;
}
int64_t qa_bot_library_start_frame_bots_a(qa_bot_library_bots_a *l, double time) {
  if (!l->initialized || l->closed)
    fail("Initialize bot behavior before advancing its observations");
  if (!isfinite(time))
    qa_range_error("Bot observation time must be finite");
  l->log.time = (float)time;
  return 0;
}
static void genetic_warning(void *ctx, const char *text) { print_severity(ctx, 2, text); }
static int64_t genetic_next_int(void *ctx) {
  qa_bot_library_bots_a *l = ctx;
  return l->options.random.next_int(l->options.random.ctx);
}
qa_genetic_selection_result qa_bot_library_genetic_selection_bots_a(qa_bot_library_bots_a *l,
                                                                    const double *ranks,
                                                                    size_t count,
                                                                    qa_genetic_write_fn write,
                                                                    void *write_ctx) {
  qa_bot_random_bots_a random;
  memset(&random, 0, sizeof random);
  random.next_int = genetic_next_int;
  random.ctx = l;
  return qa_genetic_parents_and_child_selection(ranks, count, random, genetic_warning, l, write,
                                                write_ctx);
}
int64_t qa_bot_library_shutdown_bots_a(qa_bot_library_bots_a *l) {
  if (l->closed)
    return 0;
  qa_bot_chat_library_shutdown(&l->chat);
  qa_bot_move_state_store_shutdown(&l->move_states);
  qa_bot_goal_shutdown(&l->goals);
  qa_weapon_ai_shutdown(&l->weapons);
  qa_weight_config_store_shutdown(&l->weights);
  qa_bot_character_shutdown(&l->characters);
  qa_bot_action_buffer_shutdown(&l->actions);
  qa_bot_lib_vars_clear(&l->variables);
  qa_ui_script_global_defines_clear(l->globals);
  (void)qa_bot_log_shutdown(&l->log);
  qa_bot_script_sources_check_open(&l->sources);
  qa_bot_script_sources_dispose_resources(&l->sources);
  qa_bot_memory_dispose(&l->memory);
  l->closed = true;
  l->initialized = false;
  return 0;
}
/* TS memory.reference through the ScriptMemoryCapture view of a BotMemoryCapture. */
static int64_t script_capture_reference(void *ctx, qa_ui_script_allocation *allocation) {
  qa_bot_memory_allocation bot = {allocation->bytes, allocation->length, allocation->record};
  return qa_bot_memory_capture_reference(ctx, &bot);
}
qa_save_value *qa_bot_library_capture_save_state(qa_bot_library_bots_a *l,
                                                 qa_bot_memory_capture *memory) {
  if (l->closed)
    fail("Cannot checkpoint a closed bot library");
  qa_ui_script_memory_capture script = {memory, script_capture_reference};
  qa_save_value *out = lib_sv(qa_save_value_object());
  lib_set_num(out, "version", 1);
  lib_set(out, "initialized", lib_sv(qa_save_value_boolean(l->initialized)));
  lib_set(out, "debug", lib_sv(qa_save_value_boolean(l->options.debug)));
  lib_set_num(out, "time", l->log.time);
  lib_set(out, "variables", qa_bot_lib_vars_checkpoint(&l->variables, memory));
  lib_set(out, "sources", qa_bot_script_sources_capture_save_state(&l->sources, &script));
  lib_set(out, "characters", qa_bot_character_checkpoint(&l->characters, memory));
  lib_set(out, "actions", qa_bot_action_buffer_checkpoint(&l->actions, memory));
  lib_set(out, "chat", qa_bot_chat_library_checkpoint(&l->chat, memory));
  lib_set(out, "weights", qa_weight_config_store_checkpoint(&l->weights, memory));
  lib_set(out, "weapons", qa_weapon_ai_checkpoint(&l->weapons, memory));
  lib_set(out, "goals", qa_bot_goal_checkpoint(&l->goals, memory));
  lib_set(out, "movement", qa_bot_move_state_store_checkpoint(&l->move_states, memory, &l->variables));
  lib_set(out, "log", qa_bot_log_capture_save_state(&l->log));
  return out;
}
static qa_ui_script_allocation script_restore_allocation(void *ctx, int64_t reference) {
  qa_bot_memory_allocation bot = qa_bot_memory_restored_allocation(ctx, reference);
  qa_ui_script_allocation out = {bot.bytes, bot.length, bot.record};
  return out;
}
void qa_bot_library_restore_save_state(qa_bot_library_bots_a *l, const qa_save_value *value,
                                       qa_bot_memory_restored *memory, const qa_goal_world *world,
                                       qa_goal_restore_actor_fn actor, void *actor_ctx,
                                       qa_bot_move_edge_fn edge, void *edge_ctx) {
  if (l->initialized || l->closed)
    fail("Bot library restoration requires a fresh owner");
  qa_save_reader reader, cell;
  if (!qa_save_reader_init(&reader, value, "bot.library"))
    qa_range_error("out of memory");
  lib_field(&reader, "version", &cell);
  qa_save_reader_literal_number(&cell, 1);
  qa_save_reader_destroy(&cell);
  if (lib_read_bool(&reader, "debug") != l->options.debug)
    fail("Saved bot library debug profile differs");
  bool initialized = lib_read_bool(&reader, "initialized");
  lib_field(&reader, "time", &cell);
  double time = qa_save_reader_finite(&cell);
  qa_save_reader_destroy(&cell);
  lib_field(&reader, "variables", &cell);
  qa_bot_lib_vars_restore(&l->variables, cell.value, memory);
  qa_save_reader_destroy(&cell);
  qa_ui_script_memory_restore script;
  memset(&script, 0, sizeof script);
  script.ctx = memory;
  script.allocation = script_restore_allocation;
  lib_field(&reader, "sources", &cell);
  qa_bot_script_sources_restore_save_state(&l->sources, cell.value, &script);
  qa_save_reader_destroy(&cell);
  lib_field(&reader, "weights", &cell);
  qa_weight_config_store_restore(&l->weights, cell.value, memory);
  qa_save_reader_destroy(&cell);
  lib_field(&reader, "characters", &cell);
  qa_bot_character_restore(&l->characters, cell.value, memory);
  qa_save_reader_destroy(&cell);
  lib_field(&reader, "actions", &cell);
  qa_bot_action_buffer_restore(&l->actions, cell.value, memory);
  qa_save_reader_destroy(&cell);
  lib_field(&reader, "chat", &cell);
  qa_bot_chat_library_restore(&l->chat, cell.value, memory);
  qa_save_reader_destroy(&cell);
  lib_field(&reader, "weapons", &cell);
  qa_weapon_ai_restore(&l->weapons, cell.value, memory);
  qa_save_reader_destroy(&cell);
  lib_field(&reader, "goals", &cell);
  qa_bot_goal_restore(&l->goals, cell.value, memory, world, actor, actor_ctx);
  qa_save_reader_destroy(&cell);
  lib_field(&reader, "movement", &cell);
  qa_bot_move_state_store_restore(&l->move_states, cell.value, memory, &l->variables, edge, edge_ctx);
  qa_save_reader_destroy(&cell);
  lib_field(&reader, "log", &cell);
  qa_bot_log_restore_save_state(&l->log, cell.value);
  qa_save_reader_destroy(&cell);
  l->log.time = time;
  l->initialized = initialized;
  qa_save_reader_destroy(&reader);
}
