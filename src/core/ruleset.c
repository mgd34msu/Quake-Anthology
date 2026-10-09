#include "qa/ruleset.h"
#include <stddef.h>

const qa_ruleset_descriptor qa_ruleset_descriptors[QA_RULESET_Q3 + 1] = {
    [QA_RULESET_NETQUAKE] = {"q1-netquake", "q1"},
    [QA_RULESET_QUAKEWORLD] = {"q1-quakeworld", "qw"},
    [QA_RULESET_Q2_CLASSIC] = {"q2-classic", "q2"},
    [QA_RULESET_Q2_RERELEASE] = {"q2-rerelease", "q2-rerelease"},
    [QA_RULESET_Q3] = {"q3", "q3"}
};

const char *qa_ruleset_settings_name(qa_ruleset_id ruleset)
{
    return ruleset <= QA_RULESET_Q3 ? qa_ruleset_descriptors[ruleset].settings_name : NULL;
}
