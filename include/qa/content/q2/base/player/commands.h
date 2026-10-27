/* Port of src/content/q2/base/player/commands.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_PLAYER_COMMANDS_H
#define QA_CONTENT_Q2_BASE_PLAYER_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/base/player/types.h"
#include "qa/core/commands/documentation.h"

typedef struct qa_q2_players qa_q2_players;

/* TS Q2CommandHandler run signature. */
typedef void (*qa_q2_command_handler)(qa_q2_players *players, qa_q2_player_context *context,
                                      const char *const *args, size_t arg_count, const char *command);

/** TS Q2ClientCommandDefinition. */
typedef struct qa_q2_client_command_definition {
  const char *name;
  qa_command_documentation documentation;
  bool intermission;
  qa_q2_command_handler run;
} qa_q2_client_command_definition;

/* TS q2ClientCommands. */
extern const qa_q2_client_command_definition qa_q2_client_commands[];
extern const size_t qa_q2_client_command_count;

bool qa_q2_chat_allowed(qa_q2_player_context *context);
bool qa_q2_cheats_allowed(qa_q2_player_context *context);
/* TS runQ2ClientCommand. */
bool qa_q2_run_client_command(qa_q2_players *players, qa_q2_player_context *context,
                              const char *source_command, const char *const *args, size_t arg_count);

#endif
