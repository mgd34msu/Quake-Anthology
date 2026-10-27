/* Port of src/bots/behavior/q3/navigation-services.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_NAVIGATION_SERVICES_H
#define QA_BOTS_BEHAVIOR_Q3_NAVIGATION_SERVICES_H
#include "qa/bots/behavior/q3/game_host.h"
#include "qa/bots/behavior/q3/navigation.h"
typedef struct qa_selected_bot_navigation_bots_a {
  qa_source_bot_navigation_bots_a base;
} qa_selected_bot_navigation_bots_a;
qa_source_bot_navigation_bots_a
qa_q3_bot_navigation_bots_a(qa_source_bot_game_bots_a game,
                            qa_selected_bot_navigation_bots_a selected);
#endif
