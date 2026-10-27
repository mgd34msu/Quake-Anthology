/* Port of src/bots/navigation/train.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_TRAIN_H
#define QA_BOTS_NAVIGATION_TRAIN_H
#include <stdbool.h>
#include "qa/bots/navigation/types.h"
typedef struct qa_navigation_train_ride { qa_navigation_train_stop boarding, arrival; } qa_navigation_train_ride;
/* TS navigationTrainRide; renamed off the record type name. False = TS null. */
bool qa_navigation_find_train_ride(const qa_navigation_entity_state *state, const qa_navigation_edge *edge, const qa_navigation_profile *profile, qa_navigation_train_ride *out);
#endif
