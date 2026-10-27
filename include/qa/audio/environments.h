/*
 * Port of src/audio/environments.ts. parseEnvironments takes JSON
 * text; it is parsed through the persistence JSON value tree (the TS
 * JSON.parse). Parsed environments own their material strings; free
 * with qa_reverb_environments_free. Material `null` (absent or the
 * "*" wildcard string) is materials==NULL. Plain TS Errors print
 * "Error: ..." and abort, matching other ports. Functions returning
 * bool fail only on OOM. GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_ENVIRONMENTS_H
#define QA_AUDIO_ENVIRONMENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/audio/reverb_presets.h"
#include "qa/contracts/math.h"

typedef struct qa_reverb_material {
  char **materials; /* owned; NULL = TS null */
  size_t material_count;
  int64_t preset_index;
} qa_reverb_material;

typedef struct qa_reverb_environment {
  double dimension;
  qa_reverb_material *reverbs; /* owned */
  size_t reverb_count;
} qa_reverb_environment;

typedef struct qa_audio_trace {
  double fraction;
  qa_vec3 end;
  const char *material; /* NULL = TS null */
  bool sky;
} qa_audio_trace;

typedef qa_audio_trace (*qa_audio_trace_query_fn)(qa_vec3 start, qa_vec3 end,
                                                  qa_vec3 mins, qa_vec3 maxs,
                                                  void *ctx);
typedef void (*qa_environment_warn_fn)(const char *message, void *ctx);

bool qa_parse_environments(const char *text, qa_environment_warn_fn warn,
                           void *warn_ctx, qa_reverb_environment **out,
                           size_t *out_count);
void qa_reverb_environments_free(qa_reverb_environment *environments,
                                 size_t count);
/* Aborts with the TS RangeError for an unknown index. */
qa_efx_reverb_params qa_reverb_preset(int64_t index);

typedef struct qa_environment_reverb {
  const qa_reverb_environment *environments; /* borrowed */
  size_t environment_count;
  qa_audio_trace_query_fn trace;
  void *trace_ctx;
  int64_t environment_index;
  int64_t probe_index;
  double probe_time;
  qa_vec3 results[14];
  int64_t current_preset;
  qa_efx_reverb_params active;
  qa_efx_reverb_params from;
  qa_efx_reverb_params to;
  double lerp_start;
  double lerp_end;
  bool enabled;
  double lerp_seconds;
} qa_environment_reverb;

void qa_environment_reverb_init(qa_environment_reverb *reverb,
                                const qa_reverb_environment *environments,
                                size_t environment_count,
                                qa_audio_trace_query_fn trace, void *ctx);
/* NULL when disabled or no environments (TS params getter). */
const qa_efx_reverb_params *qa_environment_reverb_params(
    const qa_environment_reverb *reverb);
int64_t qa_environment_reverb_preset_index(
    const qa_environment_reverb *reverb);
void qa_environment_reverb_update(qa_environment_reverb *reverb,
                                  qa_vec3 origin, double milliseconds);

#endif
