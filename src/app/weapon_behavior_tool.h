#ifndef QA_APP_WEAPON_BEHAVIOR_TOOL_H
#define QA_APP_WEAPON_BEHAVIOR_TOOL_H

#include "qa/frontend.h"
#include <stdio.h>

typedef enum qa_weapon_behavior_tool_action {
    QA_WEAPON_INSPECT, QA_WEAPON_DECLARE_QC, QA_WEAPON_DECLARE_QVM, QA_WEAPON_DECLARE_NATIVE
} qa_weapon_behavior_tool_action;
typedef struct qa_weapon_behavior_tool_command {
    qa_weapon_behavior_tool_action action;
    const char *product, *artifact, *id, *title, *role, *fire, *activate, *profile;
} qa_weapon_behavior_tool_command;

bool qa_weapon_behavior_tool_arguments(int *argc, char **argv,
    qa_weapon_behavior_tool_command *, bool *handled, bool *help, qa_error *);
bool qa_weapon_behavior_tool_run(const qa_frontend_options *,
    const qa_weapon_behavior_tool_command *, FILE *, qa_error *);

#endif
