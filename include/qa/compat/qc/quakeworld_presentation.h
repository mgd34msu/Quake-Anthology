/* Port of src/compat/qc/quakeworld-presentation.ts. GPL-2.0-or-later.
 * Mirrored types (TS owner src/app/bootstrap/simulation/
 * quakec-local-messages.ts, unported): local message state and host
 * as fn+ctx below, unique names. */
#ifndef QA_COMPAT_QC_QUAKEWORLD_PRESENTATION_H
#define QA_COMPAT_QC_QUAKEWORLD_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/presentation_host.h"
#include "qa/contracts/protocol.h"
/* Mirror of TS QuakeCLocalMessages surface used here. */
typedef struct qa_compat_qc_local_messages {
  void *ctx;
  /* actor NULL unless TS third argument present (set-view). */
  void (*receive)(void *ctx, const qa_netquake_message *messages, size_t count, const qa_actor_id *target, const qa_actor_id *actor);
} qa_compat_qc_local_messages;
/* Mirror of TS QuakeCLocalMessageHost (fields used on QW paths). */
typedef struct qa_compat_qc_local_message_host {
  void *ctx;
  const qa_actor_id *recipients; size_t recipient_count;
  const char *map; double seconds;
  /* TS host.message(event, target) */
  void (*message)(void *ctx, qa_network_event event, const qa_actor_id *target);
  /* TS host.emit(event, recipient?) */
  void (*emit)(void *ctx, qa_q1_event event, const qa_actor_id *recipient);
  /* TS presentQuakeCLocalMessage(message, target, state, host) */
  void (*present_local)(void *ctx, qa_netquake_message message, const qa_actor_id *target, qa_compat_qc_local_messages state, const qa_actor_id *fixed_actor);
  bool (*muzzle)(void *ctx, qa_actor_id actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
  qa_actor_id (*actor_of)(void *ctx, int64_t slot);
} qa_compat_qc_local_message_host;
void qa_compat_qc_present_quakeworld_message(qa_compat_qc_routed_message entry, const qa_actor_id *target, qa_compat_qc_local_messages state, qa_compat_qc_local_message_host host);
#endif
