/* Port of src/network/q2/codecs/codec.ts (types only; the vtable lives
 * in the header). GPL-2.0-or-later. */
#include "qa/network/q2/codecs/codec.h"

#include <stdlib.h>

void qa_q2_clc_batch_move_free(qa_q2_clc_batch_move *move) {
  if (move->frames != NULL) {
    for (size_t i = 0; i < move->frame_count; i++) free(move->frames[i].cmds);
    free(move->frames);
  }
  move->frames = NULL;
  move->frame_count = 0;
}
