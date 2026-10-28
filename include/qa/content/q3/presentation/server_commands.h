/* Port of src/content/q3/presentation/server-commands.ts. GPL-2.0-or-later.
 * From id Software's code/cgame/cg_servercmds.c, cg_main.c
 * CG_StartMusic/CG_BuildSpectatorString and q_shared.c COM_ParseExt.
 * The TS awaited command queue is synchronous here: commands run in
 * submission order on the caller's stack, and aborts (the port's error
 * channel) leave the queue's dispose-on-reject path unreachable. */
#ifndef QA_CONTENT_Q3_PRESENTATION_SERVER_COMMANDS_H
#define QA_CONTENT_Q3_PRESENTATION_SERVER_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/presentation/client_info.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/core/cvars/index.h"
typedef enum qa_q3_pres_client_server_command_sound {
  QA_Q3_PRES_COUNT_PREPARE_SOUND, QA_Q3_PRES_COUNT_PREPARE_TEAM_SOUND, QA_Q3_PRES_COUNT_FIGHT_SOUND,
  QA_Q3_PRES_TALK_SOUND, QA_Q3_PRES_VOTE_NOW, QA_Q3_PRES_VOTE_PASSED, QA_Q3_PRES_VOTE_FAILED,
  QA_Q3_PRES_CLIENT_SERVER_COMMAND_SOUND_COUNT
} qa_q3_pres_client_server_command_sound;
const char *qa_q3_pres_client_server_command_sound_name(qa_q3_pres_client_server_command_sound sound);
/* TS ClientServerCommandHost. get_server_command false is TS null; on
 * true the token strings/array are caller-freed. read_vm_cvar fills a
 * deep-copy snapshot (qa_cvar_read_free by caller), false only on OOM.
 * register_* false only on OOM; load_deferred_players/reset false only
 * on OOM. config_string returns a borrowed string. */
typedef struct qa_q3_pres_client_server_command_host {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_game_static_state *static_state;
  void *clients_ctx;
  qa_q3_pres_client_info *(*client_info)(void *ctx, double index);
  bool (*new_client_info)(void *ctx, double client_num, const char *config);
  bool (*load_deferred_players)(void *ctx,
      void (*reset_player_entity)(void *reset_ctx, qa_q3_pres_client_entity *entity), void *reset_ctx);
  bool (*reset_clients)(void *ctx);
  void *resources_ctx;
  bool (*register_model)(void *ctx, const char *path, qa_q3_pres_scene_model *out);
  qa_q3_pres_sound_asset_reader assets;
  qa_q3_game_random *random;
  void *ctx;
  void (*reset_player_entity)(void *ctx, qa_q3_pres_client_entity *entity);
  bool (*get_server_command)(void *ctx, double sequence, char ***out_tokens, size_t *out_count);
  void (*refresh_game_state)(void *ctx);
  const char *(*config_string)(void *ctx, double index);
  bool (*read_vm_cvar)(void *ctx, const char *name, qa_cvar_snapshot *out);
  void (*set_cvar)(void *ctx, const char *name, const char *value);
  void (*print)(void *ctx, const char *text);
  void (*center_print)(void *ctx, const char *text, double y, double char_width);
  void (*send_console_command)(void *ctx, const char *text);
  qa_pcm_sound *(*sound)(void *ctx, qa_q3_pres_client_server_command_sound name);
  bool (*register_sound)(void *ctx, const char *path, bool compressed, qa_pcm_sound **out);
  void (*start_local_sound)(void *ctx, qa_pcm_sound *sound, double channel);
  void (*start_background_track)(void *ctx, const char *intro, const char *loop);
  void (*remap_shader)(void *ctx, const char *original, const char *replacement, const char *time_offset);
  void (*clear_local_entities)(void *ctx);
  void (*clear_marks)(void *ctx);
  void (*clear_particles)(void *ctx);
  void (*clear_looping_sounds)(void *ctx, bool kill_all);
  void (*set_score_selection)(void *ctx);
  void (*show_response_head)(void *ctx);
  double (*memory_remaining)(void *ctx);
} qa_q3_pres_client_server_command_host;
typedef struct qa_q3_pres_client_server_command_runtime qa_q3_pres_client_server_command_runtime;
/* Product/slot mismatches abort with the exact TS Error text. */
bool qa_q3_pres_client_server_command_runtime_create(qa_q3_pres_client_server_command_runtime **out,
    qa_q3_pres_client_server_command_host host);
void qa_q3_pres_client_server_command_runtime_destroy(qa_q3_pres_client_server_command_runtime *runtime);
/* False only on OOM (including the clients reset). */
bool qa_q3_pres_client_server_command_runtime_dispose(qa_q3_pres_client_server_command_runtime *runtime);
/* False only on OOM. Closed runtimes abort with the TS Error text. */
bool qa_q3_pres_client_server_command_runtime_execute_new_server_commands(
    qa_q3_pres_client_server_command_runtime *runtime, double latest_sequence);
bool qa_q3_pres_client_server_command_runtime_execute_command(
    qa_q3_pres_client_server_command_runtime *runtime, const char *const *command, size_t command_count);
/* False only on OOM duplicating configstring-owned text. */
bool qa_q3_pres_client_server_command_runtime_parse_server_info(
    qa_q3_pres_client_server_command_runtime *runtime);
bool qa_q3_pres_client_server_command_runtime_set_config_values(
    qa_q3_pres_client_server_command_runtime *runtime);
bool qa_q3_pres_client_server_command_runtime_shader_state_changed(
    qa_q3_pres_client_server_command_runtime *runtime);
bool qa_q3_pres_client_server_command_runtime_start_music(qa_q3_pres_client_server_command_runtime *runtime);
bool qa_q3_pres_client_server_command_runtime_build_spectator_string(
    qa_q3_pres_client_server_command_runtime *runtime);
bool qa_q3_pres_client_server_command_runtime_add_to_team_chat(
    qa_q3_pres_client_server_command_runtime *runtime, const char *input);
/* False is the TS false return or OOM (distinguished by host prints). */
bool qa_q3_pres_client_server_command_runtime_parse_voice_chats(
    qa_q3_pres_client_server_command_runtime *runtime, const char *filename, double list_index,
    double maximum_chats);
bool qa_q3_pres_client_server_command_runtime_load_voice_chats(
    qa_q3_pres_client_server_command_runtime *runtime);
bool qa_q3_pres_client_server_command_runtime_voice_chat_local(
    qa_q3_pres_client_server_command_runtime *runtime, double mode, bool voice_only, double client_number,
    double color, const char *command);
bool qa_q3_pres_client_server_command_runtime_play_buffered_voice_chats(
    qa_q3_pres_client_server_command_runtime *runtime);
#endif
