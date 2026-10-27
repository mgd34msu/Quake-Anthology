/* Port of src/compat/q2/rerelease/sounds.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_SOUNDS_H
#define QA_COMPAT_Q2_RERELEASE_SOUNDS_H
#include <stdbool.h>
#include "qa/compat/q2/rerelease/module.h"

typedef struct qa_compat_q2_rerelease_sound_event {
  bool has_entity_slot; double entity_slot;
  bool has_origin; qa_vec3 origin;
  double channel, sound_index, volume, attenuation, time_offset;
  bool audience_is_client; double client_slot, dupe_key;
} qa_compat_q2_rerelease_sound_event;
typedef struct qa_compat_q2_rerelease_sound_imports qa_compat_q2_rerelease_sound_imports;
/* NULL only on OOM. */
qa_compat_q2_rerelease_sound_imports *qa_compat_q2_rerelease_sound_imports_create(
    qa_compat_q2_memory *memory, void *emit_ctx, void (*emit)(void *ctx, qa_compat_q2_rerelease_sound_event event),
    void *slot_ctx, double (*source_slot)(void *ctx, qa_guest_address address));
void qa_compat_q2_rerelease_sound_imports_destroy(qa_compat_q2_rerelease_sound_imports *s);
qa_compat_q2_memory *qa_compat_q2_rerelease_sound_imports_get_memory(const qa_compat_q2_rerelease_sound_imports *s);
/* False is the TS undefined (unhandled call). */
bool qa_compat_q2_rerelease_sound_imports_invoke(qa_compat_q2_rerelease_sound_imports *s, qa_compat_q2_rerelease_import_call call, qa_guest_call_result *out);
#endif
