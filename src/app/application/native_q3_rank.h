#ifndef QA_APPLICATION_NATIVE_Q3_RANK_H
#define QA_APPLICATION_NATIVE_Q3_RANK_H

#include "internal.h"

/* GAME CalculateRanks runs at its actual connection, death and team-change
 * producers. Counts, physical order, follow slots and PS ranks stay with GAME;
 * score reads use the real selected match owner. Restore never calls this. */
bool application_native_q3_rank(application_provider *, qa_error *);

#endif
