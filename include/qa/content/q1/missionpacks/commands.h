/* Port of src/content/q1/missionpacks/commands.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_COMMANDS_H
#define QA_CONTENT_Q1_MISSIONPACKS_COMMANDS_H
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/missionpacks/player.h"
#include "qa/content/q1/missionpacks/types.h"

/* TS MissionPackCommandOptions (a host interface: fn + ctx; NULL = absent).
 * category is "weapons" | "ammo". */
typedef struct qa_q1mp_mission_pack_command_options {
  void *ctx;
  bool (*cheat_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  bool (*cheats_allowed)(void *ctx);
  void (*developer_message)(void *ctx, const char *text);
} qa_q1mp_mission_pack_command_options;
bool qa_q1mp_mission_pack_command(qa_q1_services *game, qa_q1_base *base, qa_q1mp_mission_pack_players *players,
                                  qa_q1_player_state *player, qa_q1mp_pack pack, double impulse,
                                  const qa_q1mp_mission_pack_command_options *options);
void qa_q1mp_dump_mission_pack_coordinates(qa_q1_services *game, qa_q1_player_state *player);

#endif
