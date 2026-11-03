/* Port of src/content/q2/missionpacks/monsters/hints.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_HINTS_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_HINTS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"

/* TS Q2HintPathInitialization. issues is a malloc'd string array;
 * free with qa_q2mpm_hint_init_free. */
typedef struct qa_q2mpm_hint_initialization {
  bool present;
  size_t chains;
  char **issues; size_t issue_count;
} qa_q2mpm_hint_initialization;
void qa_q2mpm_hint_init_free(qa_q2mpm_hint_initialization *report);

/* TS Q2RogueHintsCheckpoint. Arrays are malloc'd; free with
 * qa_q2mpm_hints_checkpoint_free. */
typedef struct qa_q2mpm_hint_node_checkpoint {
  qa_saved_actor_id actor;
  double chain;
  bool has_next; qa_saved_actor_id next;
} qa_q2mpm_hint_node_checkpoint;
typedef struct qa_q2mpm_hint_monster_checkpoint {
  qa_saved_actor_id actor;
  bool has_goal; qa_saved_actor_id goal;
  double last_time;
} qa_q2mpm_hint_monster_checkpoint;
typedef struct qa_q2mpm_hints_checkpoint {
  double version; /* 1 */
  bool present;
  qa_saved_actor_id *starts; size_t start_count;
  qa_q2mpm_hint_node_checkpoint *nodes; size_t node_count;
  qa_q2mpm_hint_monster_checkpoint *monsters; size_t monster_count;
} qa_q2mpm_hints_checkpoint;
void qa_q2mpm_hints_checkpoint_free(qa_q2mpm_hints_checkpoint *checkpoint);
/* TS decodeQ2RogueHintsCheckpoint. False only on OOM; malformed
 * input aborts through the save reader, as TS throws. */
bool qa_q2mpm_decode_hints_checkpoint(const qa_save_reader *reader, qa_q2mpm_hints_checkpoint *out);

typedef struct qa_q2mpm_hint_node {
  qa_actor_id actor;
  double chain;
  bool has_next; qa_actor_id next;
} qa_q2mpm_hint_node;
typedef struct qa_q2mpm_hint_monster {
  qa_actor_id actor;
  bool has_goal; qa_actor_id goal;
  double last_time;
} qa_q2mpm_hint_monster;

/* TS Q2RogueHints (a Q2SpawnModule). */
typedef struct qa_q2mpm_hints {
  qa_q2m_monsters *monsters;
  bool present;
  qa_actor_id *starts; size_t start_count, start_cap;
  qa_q2mpm_hint_node *nodes; size_t node_count, node_cap;
  qa_q2mpm_hint_monster *pursuers; size_t pursuer_count, pursuer_cap;
  qa_q2m_hint_hooks hooks; /* ctx is this hints object */
  qa_q2_callback_definitions spawn_callbacks; /* TS callbacks getter, read by Q2EntityServices */
} qa_q2mpm_hints;

void qa_q2mpm_hints_init(qa_q2mpm_hints *hints, qa_q2m_monsters *monsters);
void qa_q2mpm_hints_destroy(qa_q2mpm_hints *hints);
qa_q2_callback_definitions qa_q2mpm_hints_callbacks(qa_q2mpm_hints *hints);
qa_q2_spawn_module qa_q2mpm_hints_as_spawn(qa_q2mpm_hints *hints);
bool qa_q2mpm_hints_spawn(qa_q2mpm_hints *hints, qa_q2_entity *entity, qa_q2_game_services *game);
bool qa_q2mpm_hints_finalize(qa_q2mpm_hints *hints, qa_q2_game_services *game, qa_q2mpm_hint_initialization *out);
qa_q2_entity *qa_q2mpm_hints_find_start(qa_q2mpm_hints *hints, qa_q2_entity *entity, qa_q2_game_services *game);
qa_q2_entity *qa_q2mpm_hints_other_end(qa_q2mpm_hints *hints, qa_q2_entity *entity, qa_q2_game_services *game);
bool qa_q2mpm_hints_check(qa_q2mpm_hints *hints, qa_q2m_context *context);
bool qa_q2mpm_hints_check_lost(qa_q2mpm_hints *hints, qa_q2m_context *context);
void qa_q2mpm_hints_go(qa_q2mpm_hints *hints, qa_q2m_context *context, qa_q2_entity *point);
void qa_q2mpm_hints_stop(qa_q2mpm_hints *hints, qa_q2m_context *context);
bool qa_q2mpm_hints_run(qa_q2mpm_hints *hints, qa_q2m_context *context, double distance);
void qa_q2mpm_hints_touch(qa_q2mpm_hints *hints, qa_q2_entity *hint, qa_q2_game_services *game, qa_q2m_context *context);
bool qa_q2mpm_hints_capture(qa_q2mpm_hints *hints, qa_q2_game_services *game, qa_q2mpm_hints_checkpoint *out);
void qa_q2mpm_hints_restore(qa_q2mpm_hints *hints, qa_q2_game_services *game, const qa_q2mpm_hints_checkpoint *checkpoint);

#endif
