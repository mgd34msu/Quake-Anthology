/* Port of src/content/q1/addons/commands.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_COMMANDS_H
#define QA_CONTENT_Q1_ADDONS_COMMANDS_H
#include "qa/content/q1/addons/context.h"
void qa_q1ad_omnicide(qa_q1ad_context *context, qa_actor_id actor);
/* TS isQ1AddonDeveloperImpulse(program, impulse). */
bool qa_q1ad_is_developer_impulse(qa_q1ad_program program, double impulse);
bool qa_q1ad_handle_impulse(qa_q1ad_context *context, qa_actor_id actor, double impulse);
void qa_q1ad_frame_player(qa_q1ad_context *context, qa_actor_id actor, qa_vec3 view_offset);
#endif
