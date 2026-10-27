/* Port of src/content/q1/addons/lights.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_LIGHTS_H
#define QA_CONTENT_Q1_ADDONS_LIGHTS_H
#include "qa/content/q1/addons/context.h"
char qa_q1ad_light_style(double fraction, bool full_range);
void qa_q1ad_register_lights(qa_q1ad_context *context);
#endif
