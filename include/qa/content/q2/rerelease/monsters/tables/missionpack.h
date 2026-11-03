/* Port of src/content/q2/rerelease/monsters/tables/missionpack.ts: per-frame
 * callbacks of the mission pack monsters as stored in the rerelease game DLL
 * (game_x64.dll). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_TABLES_MISSIONPACK_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_TABLES_MISSIONPACK_H
#include "qa/content/q2/foundation/monsters/types.h"

/* One frame's DLL callback (TS callbacks[index]). */
typedef struct qa_q2rm_frame_callback { size_t frame; const char *name; } qa_q2rm_frame_callback;
/* TS RereleaseMoveCallbacks: every frame without an entry has no callback;
 * has_end false is TS `end` absent, else end replaces the move's end (NULL for none). */
typedef struct qa_q2rm_move_callbacks {
  const char *move;
  const qa_q2rm_frame_callback *callbacks; size_t callback_count;
  bool has_end; const char *end;
} qa_q2rm_move_callbacks;
/* TS rereleaseMissionPackCallbacks[classname]; NULL count 0 is undefined. */
const qa_q2rm_move_callbacks *qa_q2rm_rerelease_mission_pack_callbacks(const char *classname, size_t *out_count);
/* TS rereleaseMissionPackReplacements[classname]: moves whose animation range differs, replaced whole by name. */
const qa_q2m_move *qa_q2rm_rerelease_mission_pack_replacements(const char *classname, size_t *out_count);

/* TS rereleaseMissionPackMoves result. moves is NULL when TS returns the
 * classic list unchanged; otherwise moves, frames and actions are owned and
 * released with qa_q2rm_mission_pack_moves_free. */
typedef struct qa_q2rm_mission_pack_moves {
  qa_q2m_move *moves; size_t count;
  qa_q2m_frame *frames;
  qa_q2m_action *actions;
} qa_q2rm_mission_pack_moves;
/* TS rereleaseMissionPackMoves(classname, moves): the classic move list with the
 * rerelease DLL's callbacks substituted for the monster's moves that differ. */
qa_q2rm_mission_pack_moves qa_q2rm_rerelease_mission_pack_moves(const char *classname, const qa_q2m_move *moves, size_t count);
void qa_q2rm_mission_pack_moves_free(qa_q2rm_mission_pack_moves *moves);
#endif
