/* Port of src/compat/qc/compatibility.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/compatibility.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/mods/callbacks.h"
#include "qa/content/mods/match.h"
typedef qa_save_reader R;
static const R *F(const R *r, const char *n, R *o) { qa_save_reader_field(r, n, o); return o; }
static const char *read_dialect(const R *field) {
  static const char *const choices[3] = {"known-retail", "q1-rerelease-private", "quake-1-re-ts-private"};
  const char *v = qa_save_reader_choice_string(field, choices, 3);
  return strcmp(v, "quake-1-re-ts-private") == 0 ? "q1-rerelease-private" : v;
}
void qa_compat_qc_quakec_compatibility_destroy(qa_compat_qc_quakec_compatibility *c) {
  free(c->teams); free(c->combat); free(c->pickup_callers); memset(c, 0, sizeof *c);
}
bool qa_compat_qc_read_quakec_compatibility(const uint8_t *bytes, size_t length, const char *digest, qa_compat_qc_quakec_compatibility *out) {
  memset(out, 0, sizeof *out); out->message_dialect = "known-retail";
  if (!bytes) return true;
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (!value) return false;
  R reader; if (!qa_save_reader_init(&reader, value, "quakec-compatibility.json")) return false;
  { R v; qa_save_reader_literal_number(F(&reader, "version", &v), 1); qa_save_reader_destroy(&v); }
  { R v; qa_save_reader_literal_string(F(&reader, "artifactDigest", &v), digest); qa_save_reader_destroy(&v); }
  { R v; F(&reader, "teams", &v); if (!qa_save_reader_is_undefined(&v)) { out->has_teams = true; out->teams = qa_read_source_team_aliases(&v, &out->team_count); if (!out->teams && out->team_count) { qa_save_reader_destroy(&v); goto oom; } } qa_save_reader_destroy(&v); }
  { R v; F(&reader, "combat", &v); if (!qa_save_reader_is_undefined(&v)) { out->has_combat = true; out->combat = qa_read_qc_combat(&v); if (!out->combat) { qa_save_reader_destroy(&v); goto oom; } } qa_save_reader_destroy(&v); }
  { R v; F(&reader, "weaponStage", &v); if (!qa_save_reader_is_undefined(&v)) { out->has_weapon_stage = true; out->weapon_stage = qa_q1qc_read_primary_weapon_stage(&v); } qa_save_reader_destroy(&v); }
  { R v; F(&reader, "messageDialect", &v); if (!qa_save_reader_is_undefined(&v)) out->message_dialect = read_dialect(&v); qa_save_reader_destroy(&v); }
  { R v; F(&reader, "pickupCallers", &v);
    if (!qa_save_reader_is_undefined(&v)) {
      size_t n = qa_save_reader_list_count(&v);
      out->pickup_callers = calloc(n ? n : 1, sizeof *out->pickup_callers); out->pickup_caller_count = n;
      if (!out->pickup_callers) { qa_save_reader_destroy(&v); goto oom; }
      for (size_t i = 0; i < n; i++) { R item; qa_save_reader_item(&v, i, &item); out->pickup_callers[i] = qa_q1qc_read_pickup_caller(&item); qa_save_reader_destroy(&item); }
    }
    qa_save_reader_destroy(&v); }
  qa_save_reader_destroy(&reader);
  return true;
oom: qa_save_reader_destroy(&reader); qa_compat_qc_quakec_compatibility_destroy(out); return false;
}
