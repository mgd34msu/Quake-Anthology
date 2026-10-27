/* Port of src/content/q2/multiplayer/horde.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_HORDE_H
#define QA_CONTENT_Q2_MULTIPLAYER_HORDE_H
#include <stddef.h>
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/contracts/session.h"

/** TS capture() result. monsters malloc'd; free with free(3). */
typedef struct qa_q2_horde_checkpoint {
  double wave, spawned, intermission;
  qa_saved_actor_id *monsters;
  size_t monster_count;
} qa_q2_horde_checkpoint;

typedef struct qa_q2_horde qa_q2_horde;
/* NULL only on OOM. */
qa_q2_horde *qa_q2_horde_create(void);
void qa_q2_horde_destroy(qa_q2_horde *horde);
qa_q2_spawn_module qa_q2_horde_as_spawn(qa_q2_horde *horde);
/* Callback storage owned by the horde. */
qa_q2_callback_definitions qa_q2_horde_callbacks(qa_q2_horde *horde);
qa_q2_entity *qa_q2_horde_manager(qa_q2_horde *horde);
double qa_q2_horde_wave(qa_q2_horde *horde);
bool qa_q2_horde_spawn(qa_q2_horde *horde, qa_q2_entity *entity, qa_q2_game_services *game);
/* False only on OOM. */
bool qa_q2_horde_capture(qa_q2_horde *horde, qa_q2_horde_checkpoint *out);
void qa_q2_horde_restore(qa_q2_horde *horde, qa_q2_game_services *game, qa_q2_horde_checkpoint saved);
#endif
