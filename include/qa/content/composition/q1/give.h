/*
 * Port of src/content/composition/q1/give.ts: host give syntax plus
 * shared named/category grants. givePickup (TS owner:
 * src/content/q1/foundation/pickups.ts, unported) is mirrored as a
 * fn+ctx pair on the source view. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_GIVE_H
#define QA_CONTENT_COMPOSITION_Q1_GIVE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/composition/q1/commands.h"

typedef struct qa_comp_q1_give_source {
  qa_comp_q1_game *game;
  const qa_comp_q1_services *services;
  void *give_ctx;
  /* Mirrored givePickup(game, item, actor). */
  bool (*give_pickup)(void *ctx, qa_comp_q1_game *game, qa_comp_q1_actor *item, qa_actor_id actor);
} qa_comp_q1_give_source;

/** TS giveQ1(). Errors abort like the TS throws. */
void qa_comp_give_q1(const qa_comp_q1_give_source *source, qa_actor_id actor,
                     const char *const *args, size_t arg_count);

#endif
