#ifndef QA_APP_WEAPON_BEHAVIOR_TOOL_H
#define QA_APP_WEAPON_BEHAVIOR_TOOL_H

#include "qa/frontend.h"
#include <stdio.h>

bool qa_weapon_behavior_inspect(const qa_frontend_options *, const char *product,
    const char *artifact, FILE *, qa_error *);

#endif
