/* Port of src/compat/qc/profile.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *xd(const char *s) { size_t n = strlen(s) + 1; char *r = malloc(n); if (r) memcpy(r, s, n); return r; }
size_t qa_compat_qc_program_search_order(qa_compat_qc_host_kind kind, const char **out, size_t cap) {
  bool qw = strcmp(kind, "quakeworld") == 0; size_t n = 0;
  if (qw) { if (out && n < cap) out[n] = "qwprogs.dat"; n++; }
  if (out && n < cap) { out[n] = "progs.dat"; } n++;
  return n;
}
bool qa_compat_qc_load_mounted_program(qa_compat_qc_mounted_open content, qa_compat_qc_host_kind kind, qa_compat_qc_program *out_p, qa_resolved_resource_reference *out_r) {
  qa_quakec_api_identity api = strcmp(kind, "quakeworld") == 0
    ? (qa_quakec_api_identity){QA_QUAKEC_API_Q1_QUAKEWORLD, 6, 54730}
    : (qa_quakec_api_identity){QA_QUAKEC_API_Q1_NETQUAKE, 6, 5927};
  const char *paths[2]; size_t n = qa_compat_qc_program_search_order(kind, paths, 2);
  for (size_t i = 0; i < n; i++) {
    size_t len = 0; qa_resolved_resource_reference ref; memset(&ref, 0, sizeof ref);
    uint8_t *bytes = content.open(content.ctx, paths[i], &len, &ref, content.storage);
    if (bytes) { bool ok = qa_compat_qc_load_program(bytes, len, &api, paths[i], out_p); free(bytes); if (ok && out_r) *out_r = ref; return ok; }
  }
  if (n == 2) qa_compat_qc_program_error("progs.dat", "missing qwprogs.dat or progs.dat");
  qa_compat_qc_program_error("progs.dat", "missing progs.dat");
}
qa_compat_qc_entity_layout qa_compat_qc_classic_entity_layout(const qa_compat_qc_program *p) {
  int64_t off = p->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD ? 104 : 96;
  return (qa_compat_qc_entity_layout){off + p->entity_field_words * 4, off, p->entity_field_words};
}
static qa_guest_field_layout field_layout(const qa_compat_qc_definition *d, double base) {
  bool vec = strcmp(d->type, "vector") == 0;
  return (qa_guest_field_layout){d->name, base + (double)d->offset * 4, (vec || strcmp(d->type, "float") == 0) ? "float32" : "int32", vec ? 3 : 1};
}
void qa_compat_qc_host_description_destroy(qa_compat_qc_host_description *d) {
  for (size_t i = 0; i < d->profile.builtin_count; i++) { if (d->binding_names) free(d->binding_names[i]); if (d->binding_callbacks) free(d->binding_callbacks[i]); }
  free(d->binding_names); free(d->binding_callbacks); free(d->bindings);
  free(d->global_fields); free(d->entity_fields); free(d->globals_id); free(d->entity_id); memset(d, 0, sizeof *d);
}
bool qa_compat_qc_describe_host(const qa_compat_qc_program *p, qa_compat_qc_host_kind kind, qa_compat_qc_entity_layout entities, const qa_compat_qc_builtin_registry *reg, const char *const *ext, size_t ext_n, qa_compat_qc_host_description *out) {
  memset(out, 0, sizeof *out);
  if ((strcmp(kind, "quakeworld") == 0) != (p->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD)) qa_compat_qc_program_error(p->source, "host kind disagrees with program API");
  out->global_fields = calloc(p->global_count ? p->global_count : 1, sizeof *out->global_fields);
  out->entity_fields = calloc(p->field_count ? p->field_count : 1, sizeof *out->entity_fields);
  if (!out->global_fields || !out->entity_fields) goto oom;
  for (size_t i = 0; i < p->global_count; i++) out->global_fields[i] = field_layout(&p->globals[i], 0);
  for (size_t i = 0; i < p->field_count; i++) out->entity_fields[i] = field_layout(&p->fields[i], 0);
  { char buf[96]; snprintf(buf, sizeof buf, "quakec:%lld-globals", (long long)p->api.system_crc); out->globals_id = xd(buf);
    snprintf(buf, sizeof buf, "quakec:%lld-entity-variables", (long long)p->api.system_crc); out->entity_id = xd(buf); }
  if (!out->globals_id || !out->entity_id) goto oom;
  out->globals_layout = (qa_guest_layout){out->globals_id, (double)p->initial_globals_length, 4, 4, out->global_fields, p->global_count};
  out->entity_layout = (qa_guest_layout){out->entity_id, (double)entities.field_words * 4, 4, 4, out->entity_fields, p->field_count};
  size_t bn = reg->numbered_count + reg->named_count;
  out->bindings = calloc(bn ? bn : 1, sizeof *out->bindings);
  out->binding_names = calloc(bn ? bn : 1, sizeof *out->binding_names);
  out->binding_callbacks = calloc(bn ? bn : 1, sizeof *out->binding_callbacks);
  if (!out->bindings || !out->binding_names || !out->binding_callbacks) goto oom;
  size_t bi = 0;
  for (size_t i = 0; i < reg->numbered_count; i++, bi++) {
    int64_t num = reg->numbered[i].number; const char *name = NULL;
    for (size_t j = 0; j < p->function_count; j++) if (p->functions[j].first_statement == -num) { name = p->functions[j].name; break; }
    char fb[64]; if (!name) { snprintf(fb, sizeof fb, "builtin_%lld", (long long)num); name = fb; }
    out->binding_names[bi] = xd(name);
    char cb[96]; snprintf(cb, sizeof cb, "quakec:numbered-%lld", (long long)num); out->binding_callbacks[bi] = xd(cb);
    if (!out->binding_names[bi] || !out->binding_callbacks[bi]) { out->profile.builtin_count = bi + 1; goto oom; }
    out->bindings[bi] = (qa_quakec_builtin_binding){"numbered", (double)num, out->binding_names[bi], out->binding_callbacks[bi]};
  }
  for (size_t i = 0; i < reg->named_count; i++, bi++) {
    out->binding_names[bi] = xd(reg->named[i].name);
    size_t need = strlen(reg->named[i].name) + 32; out->binding_callbacks[bi] = malloc(need);
    if (out->binding_callbacks[bi]) snprintf(out->binding_callbacks[bi], need, "quakec:named-%s", reg->named[i].name);
    if (!out->binding_names[bi] || !out->binding_callbacks[bi]) { out->profile.builtin_count = bi + 1; goto oom; }
    out->bindings[bi] = (qa_quakec_builtin_binding){"named", 0, out->binding_names[bi], out->binding_callbacks[bi]};
  }
  size_t so_n = qa_compat_qc_program_search_order(kind, out->search_order, 2);
  out->profile = (qa_quakec_host_profile){p->api, {out->search_order, so_n}, &out->globals_layout, &out->entity_layout, out->bindings, bn, {ext, ext_n}};
  return true;
oom: qa_compat_qc_host_description_destroy(out); return false;
}
