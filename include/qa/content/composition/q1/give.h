/*
 * Port of src/content/composition/q1/give.ts: host give syntax plus
 * shared named/category grants, using the selected inventory and real
 * item touches on the one Q1 game. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_GIVE_H
#define QA_CONTENT_COMPOSITION_Q1_GIVE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/composition/q1/commands.h"

/* The Q1SourceComposition fields giveQ1 destructures. */
typedef struct qa_comp_q1_give_source {
  qa_q1_services *game;
  const qa_comp_q1_services *services;
} qa_comp_q1_give_source;

/** TS giveQ1(source, actor, args). Aborts only where TS throws (no admitted player). */
void qa_comp_give_q1(const qa_comp_q1_give_source *source, qa_actor_id actor,
                     const char *const *args, size_t arg_count);

#endif
