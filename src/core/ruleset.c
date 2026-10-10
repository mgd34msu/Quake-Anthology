#include "qa/ruleset.h"
#include <stddef.h>

const qa_ruleset_descriptor qa_ruleset_descriptors[QA_RULESET_Q3 + 1] = {
    [QA_RULESET_NETQUAKE] = {"q1-netquake", "q1", QA_GAME_Q1},
    [QA_RULESET_QUAKEWORLD] = {"q1-quakeworld", "qw", QA_GAME_Q1},
    [QA_RULESET_Q2_CLASSIC] = {"q2-classic", "q2", QA_GAME_Q2},
    [QA_RULESET_Q2_RERELEASE] = {"q2-rerelease", "q2-rerelease", QA_GAME_Q2},
    [QA_RULESET_Q3] = {"q3", "q3", QA_GAME_Q3}
};

const qa_ruleset_descriptor *qa_ruleset_read(qa_ruleset_id ruleset)
{
    return (unsigned)ruleset <= QA_RULESET_Q3 ? &qa_ruleset_descriptors[ruleset] : NULL;
}

const char *qa_ruleset_settings_name(qa_ruleset_id ruleset)
{
    const qa_ruleset_descriptor *descriptor = qa_ruleset_read(ruleset);
    return descriptor ? descriptor->settings_name : NULL;
}
