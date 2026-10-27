/* Port of src/compat/qc/message-routing.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QC_MESSAGE_ROUTING_H
#define QA_COMPAT_QC_MESSAGE_ROUTING_H
#include <stdbool.h>
#include "qa/compat/qc/presentation_host.h"
#include "qa/world/gameplay/q1_client_visibility.h"
typedef struct qa_compat_qc_message_origin { void *ctx; qa_vec3 (*fn)(void *ctx); } qa_compat_qc_message_origin;
typedef struct qa_compat_qc_message_visibility { void *ctx; qa_q1_client_visibility_scene (*fn)(void *ctx); } qa_compat_qc_message_visibility;
bool qa_compat_qc_receives_quakeworld_message(qa_actor_id actor, qa_compat_qc_message_destination destination, qa_compat_qc_message_origin origin, qa_compat_qc_message_visibility visibility);
#endif
