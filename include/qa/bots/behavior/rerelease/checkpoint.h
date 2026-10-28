/* Port of src/bots/behavior/rerelease/checkpoint.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_CHECKPOINT_H
#define QA_BOTS_BEHAVIOR_RERELEASE_CHECKPOINT_H
#include <stdbool.h>
#include "qa/bots/behavior/rerelease/profile.h"
#include "qa/persistence/value.h"
/* TS readRereleaseBehavior(unknown) under path "bot.rerelease".
 * Malformed input aborts via SaveReader. False only on OOM.
 * The read path's map_digest is a duplicated string retained for the
 * process lifetime (qa_nav_path borrows its digest everywhere else). */
bool qa_read_rerelease_behavior(qa_rerelease_behavior_checkpoint *out, const qa_save_value *value);
#endif
