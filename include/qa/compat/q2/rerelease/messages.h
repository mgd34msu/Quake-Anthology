/* Port of src/compat/q2/rerelease/messages.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_MESSAGES_H
#define QA_COMPAT_Q2_RERELEASE_MESSAGES_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/compat/q2/rerelease/module.h"
#include "qa/contracts/math.h"
#include "qa/network/q2/message.h"

typedef struct qa_compat_q2_rerelease_unicast {
  double client_slot; bool reliable; double dupe_key;
  const uint8_t *bytes; size_t byte_length;
} qa_compat_q2_rerelease_unicast;
typedef struct qa_compat_q2_rerelease_multicast {
  bool has_origin; qa_vec3 origin;
  const char *destination; /* all|phs|pvs */
  bool reliable;
  const uint8_t *bytes; size_t byte_length;
} qa_compat_q2_rerelease_multicast;
typedef struct qa_compat_q2_rerelease_message_services {
  void *ctx;
  qa_q2_sizebuf *buffer;
  bool (*accepts_client)(void *ctx, double slot);
  void (*unicast)(void *ctx, qa_compat_q2_rerelease_unicast message);
  void (*multicast)(void *ctx, qa_compat_q2_rerelease_multicast message);
} qa_compat_q2_rerelease_message_services;

typedef struct qa_compat_q2_rerelease_message_imports qa_compat_q2_rerelease_message_imports;
/* NULL only on OOM. */
qa_compat_q2_rerelease_message_imports *qa_compat_q2_rerelease_message_imports_create(
    qa_compat_q2_memory *memory, qa_compat_q2_rerelease_message_services services,
    void *slot_ctx, double (*source_slot)(void *ctx, qa_guest_address address));
void qa_compat_q2_rerelease_message_imports_destroy(qa_compat_q2_rerelease_message_imports *m);
qa_compat_q2_memory *qa_compat_q2_rerelease_message_imports_get_memory(const qa_compat_q2_rerelease_message_imports *m);
/* False is the TS undefined (unhandled call). */
bool qa_compat_q2_rerelease_message_imports_invoke(qa_compat_q2_rerelease_message_imports *m, qa_compat_q2_rerelease_import_call call, qa_guest_call_result *out);
#endif
