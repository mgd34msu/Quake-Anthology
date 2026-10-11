#ifndef QA_RULESET_H
#define QA_RULESET_H

#include <stdbool.h>

typedef enum qa_game_family { QA_GAME_Q1, QA_GAME_Q2, QA_GAME_Q3 } qa_game_family;

typedef enum qa_ruleset_id {
    QA_RULESET_NETQUAKE = 0,
    QA_RULESET_QUAKEWORLD = 1,
    QA_RULESET_Q2_CLASSIC = 2,
    QA_RULESET_Q2_RERELEASE = 3,
    QA_RULESET_Q3 = 4
} qa_ruleset_id;

typedef struct qa_ruleset_descriptor {
    const char *settings_name;
    const char *console_name;
    qa_game_family family;
} qa_ruleset_descriptor;
extern const qa_ruleset_descriptor qa_ruleset_descriptors[QA_RULESET_Q3 + 1];
const qa_ruleset_descriptor *qa_ruleset_read(qa_ruleset_id);
const char *qa_ruleset_settings_name(qa_ruleset_id);
/* Q2 archive fields retain their 0/1 encoding at the boundary. */
unsigned qa_ruleset_q2_tag(qa_ruleset_id);
qa_ruleset_id qa_ruleset_q2_from_tag(unsigned);

qa_ruleset_id qa_ruleset_from_family_tag(unsigned, bool q2_rerelease, qa_ruleset_id q1_rules);

#endif
