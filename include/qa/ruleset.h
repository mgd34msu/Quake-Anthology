#ifndef QA_RULESET_H
#define QA_RULESET_H

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
} qa_ruleset_descriptor;
extern const qa_ruleset_descriptor qa_ruleset_descriptors[QA_RULESET_Q3 + 1];
const char *qa_ruleset_settings_name(qa_ruleset_id);

#endif
