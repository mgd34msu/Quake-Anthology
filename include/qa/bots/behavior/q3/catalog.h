/*
 * Port of src/bots/behavior/q3/catalog.ts (game/g_bot.c catalogs, VM cvar and
 * delayed begins). The game owns clients. game, files, parser and host are
 * borrowed and receive their own ctx. GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_CATALOG_H
#define QA_BOTS_BEHAVIOR_Q3_CATALOG_H
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/bots/behavior/q3/game_host.h"
#include "qa/content/q3/base/game/memory.h"
#include "qa/core/common_parse.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_GAME_MAX_INFOS 1024
/* TS parseGameInfos. infos holds QA_GAME_MAX_INFOS cells; *infos_length is the TS
 * array length, raised when a write lands past it. Returns the parsed count. */
int64_t qa_parse_game_infos(qa_common_parse_cursor *cursor, int64_t maximum,
                            qa_common_parse_state *parser, void (*print)(void *ctx, const char *text),
                            void *print_ctx, qa_q3_game_memory *memory,
                            qa_q3_game_memory_allocation *infos, size_t *infos_length, size_t start);
/* TS BotCatalogFiles. read: borrowed bytes or NULL (TS null). list: names
 * borrowed until the next call. */
typedef struct qa_bot_catalog_files_bots_a {
  void *ctx;
  const uint8_t *(*read)(void *ctx, const char *path, size_t *out_length);
  const char *const *(*list)(void *ctx, const char *directory, const char *extension,
                             size_t *out_count);
} qa_bot_catalog_files_bots_a;
/* TS GameBotCatalogHost. */
typedef struct qa_game_bot_catalog_host_bots_a {
  void *ctx;
  int64_t (*allocate_client)(void *ctx);
  bool (*setup_client)(void *ctx, int64_t client, const qa_bot_settings_bots_a *settings, bool restart);
  void (*shutdown_client)(void *ctx, int64_t client, bool restart);
} qa_game_bot_catalog_host_bots_a;
typedef struct qa_game_info_catalog_bots_a {
  qa_q3_game_memory_allocation infos[QA_GAME_MAX_INFOS];
  size_t infos_length;
  int64_t count;
} qa_game_info_catalog_bots_a;
typedef struct qa_bot_spawn_slot_bots_a {
  int64_t client_num;
  double spawn_time;
} qa_bot_spawn_slot_bots_a;
typedef struct qa_game_bot_catalog_bots_a {
  qa_source_bot_game_bots_a *game;
  qa_bot_catalog_files_bots_a files;
  qa_common_parse_state *parser;
  qa_game_bot_catalog_host_bots_a host;
  qa_game_info_catalog_bots_a bots, arenas;
  qa_bot_spawn_slot_bots_a queue[16];
  bool has_minimum_players;
  qa_bot_cvar_bots_a minimum_players;
  double check_minimum_time;
} qa_game_bot_catalog_bots_a;
void qa_game_bot_catalog_bots_a_init(qa_game_bot_catalog_bots_a *c, qa_source_bot_game_bots_a *game,
                                     qa_bot_catalog_files_bots_a files, qa_common_parse_state *parser,
                                     qa_game_bot_catalog_host_bots_a host);
qa_save_value *qa_game_bot_catalog_capture_save_state(qa_game_bot_catalog_bots_a *c);
void qa_game_bot_catalog_restore_save_state(qa_game_bot_catalog_bots_a *c, const qa_save_value *value);
int64_t qa_game_bot_catalog_num_bots(const qa_game_bot_catalog_bots_a *c);
int64_t qa_game_bot_catalog_num_arenas(const qa_game_bot_catalog_bots_a *c);
/* Info lookups return malloc'd strings (caller frees); NULL is TS null. */
char *qa_game_bot_catalog_bot_info_by_number(qa_game_bot_catalog_bots_a *c, int64_t number);
char *qa_game_bot_catalog_bot_info_by_name(qa_game_bot_catalog_bots_a *c, const char *name);
char *qa_game_bot_catalog_arena_info_by_map(qa_game_bot_catalog_bots_a *c, const char *map);
void qa_game_bot_catalog_initialize_bots(qa_game_bot_catalog_bots_a *c, bool restart);
bool qa_game_bot_catalog_connect(qa_game_bot_catalog_bots_a *c, int64_t client, bool restart);
void qa_game_bot_catalog_shutdown_client(qa_game_bot_catalog_bots_a *c, int64_t client, bool restart);
void qa_game_bot_catalog_console_command(qa_game_bot_catalog_bots_a *c, const char *const *argv,
                                         size_t argc);
void qa_game_bot_catalog_remove_queued_begin(qa_game_bot_catalog_bots_a *c, int64_t client);
void qa_game_bot_catalog_check_spawn(qa_game_bot_catalog_bots_a *c);
int64_t qa_game_bot_catalog_count_human_players(qa_game_bot_catalog_bots_a *c, int64_t team);
int64_t qa_game_bot_catalog_count_bot_players(qa_game_bot_catalog_bots_a *c, int64_t team);
bool qa_game_bot_catalog_add_random_bot(qa_game_bot_catalog_bots_a *c, int64_t team);
bool qa_game_bot_catalog_add_match_bot(qa_game_bot_catalog_bots_a *c);
bool qa_game_bot_catalog_remove_random_bot(qa_game_bot_catalog_bots_a *c, int64_t team);
void qa_game_bot_catalog_check_minimum_players(qa_game_bot_catalog_bots_a *c);
#endif
