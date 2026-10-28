/* Port of src/app/bootstrap/simulation/qvm-weapon-behavior.ts (checkpoint reader, entities filter, source shell). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/qvm_weapon_behavior.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/formats/q1_map/entities.h"
#include "qa/formats/q1_map/types.h"
#include "qa/foundation/support.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
#include "qa/world/gameplay/weapon_behaviors.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_save_reader_fail(r, "out of memory"); return o;
}
qa_app_sim_qvm_weapon_behavior_checkpoint qa_app_sim_read_qvm_weapon_behavior_checkpoint(
    const qa_save_reader *reader, qa_weapon_behavior_definition expected) {
  qa_app_sim_qvm_weapon_behavior_checkpoint out; memset(&out, 0, sizeof out);
  qa_save_reader f;
  f = fld(reader, "module"); out.module = qa_read_guest(&f);
  if (out.module.kind != QA_GUEST_CHECKPOINT_QVM) qa_save_reader_fail(&f, "expected QVM checkpoint");
  qa_save_reader_destroy(&f);
  f = fld(reader, "profileDigest"); out.profile_digest = qa_save_reader_string(&f);
  bool ok = out.profile_digest && strlen(out.profile_digest) == 64;
  for (const char *c = out.profile_digest; ok && *c; c++) ok = (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f');
  if (!ok) qa_save_reader_fail(&f, "invalid profile digest");
  qa_save_reader_destroy(&f);
  f = fld(reader, "version"); out.version = qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  f = fld(reader, "definition"); out.definition = qa_read_weapon_behavior_definition(&f, expected); qa_save_reader_destroy(&f);
  f = fld(reader, "activated");
  out.activated_count = qa_save_reader_list_count(&f);
  out.activated = out.activated_count ? malloc(out.activated_count * sizeof *out.activated) : NULL;
  for (size_t i = 0; i < out.activated_count; i++) {
    qa_save_reader e; qa_save_reader_item(&f, i, &e);
    out.activated[i] = qa_read_saved_actor(&e); qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  f = fld(reader, "bindings");
  out.binding_count = qa_save_reader_list_count(&f);
  out.bindings = out.binding_count ? malloc(out.binding_count * sizeof *out.bindings) : NULL;
  static const char *const kinds[] = {"target", "client", "projectile"};
  for (size_t i = 0; i < out.binding_count; i++) {
    qa_save_reader e, bf; qa_save_reader_item(&f, i, &e);
    bf = fld(&e, "actor"); out.bindings[i].actor = qa_read_saved_actor(&bf); qa_save_reader_destroy(&bf);
    bf = fld(&e, "pointer"); out.bindings[i].pointer = qa_save_reader_integer_min(&bf, 1); qa_save_reader_destroy(&bf);
    bf = fld(&e, "kind"); out.bindings[i].kind = qa_save_reader_choice_string(&bf, kinds, 3); qa_save_reader_destroy(&bf);
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  f = fld(reader, "retired");
  out.retired_count = qa_save_reader_list_count(&f);
  out.retired = out.retired_count ? malloc(out.retired_count * sizeof *out.retired) : NULL;
  for (size_t i = 0; i < out.retired_count; i++) {
    qa_save_reader e, bf, t; qa_save_reader_item(&f, i, &e);
    bf = fld(&e, "actor"); out.retired[i].actor = qa_read_saved_actor(&bf); qa_save_reader_destroy(&bf);
    t = fld(&e, "trajectory");
    bf = fld(&t, "origin"); out.retired[i].origin = qa_read_vector(&bf); qa_save_reader_destroy(&bf);
    bf = fld(&t, "velocity"); out.retired[i].velocity = qa_read_vector(&bf); qa_save_reader_destroy(&bf);
    bf = fld(&t, "angles"); out.retired[i].angles = qa_read_vector(&bf); qa_save_reader_destroy(&bf);
    qa_save_reader_destroy(&t); qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  return out;
}
char *qa_app_sim_qvm_weapon_initialization_entities(const char *text) {
  static const char *const classes[] = {"worldspawn", "info_player_start", "info_player_deathmatch",
    "info_player_intermission", "team_CTF_redplayer", "team_CTF_blueplayer", "team_CTF_redspawn", "team_CTF_bluespawn"};
  size_t count = 0;
  qa_q1_entity *entities = qa_q1_parse_entities(text, &count);
  size_t worlds = 0;
  for (size_t i = 0; i < count; i++) {
    const char *c = qa_q1_entity_value(&entities[i], "classname");
    if (is(c, "worldspawn")) worlds++;
  }
  if (worlds != 1) { qa_q1_entities_destroy(entities, count); qa_range_error("QVM component requires one authored worldspawn"); }
  size_t cap = 1024, len = 0; char *out = malloc(cap);
  if (!out) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    const char *c = qa_q1_entity_value(&entities[i], "classname");
    bool keep = false;
    for (size_t k = 0; k < 8; k++) if (is(c, classes[k])) keep = true;
    if (!keep) continue;
    size_t need = 4;
    for (size_t p = 0; p < entities[i].property_count; p++) {
      if (strchr(entities[i].properties[p].key, '"') || strchr(entities[i].properties[p].value, '"'))
        qa_range_error("Unquotable QVM spawn field");
      need += strlen(entities[i].properties[p].key) + strlen(entities[i].properties[p].value) + 6;
    }
    if (len + need + 1 > cap) { while (len + need + 1 > cap) cap *= 2; out = realloc(out, cap); if (!out) qa_range_error("Out of memory"); }
    len += (size_t)sprintf(out + len, "{\n");
    for (size_t p = 0; p < entities[i].property_count; p++)
      len += (size_t)sprintf(out + len, "%s\"%s\" \"%s\"", p ? "\n" : "", entities[i].properties[p].key, entities[i].properties[p].value);
    len += (size_t)sprintf(out + len, "\n}\n");
  }
  out[len] = '\0';
  qa_q1_entities_destroy(entities, count);
  return out;
}
struct qa_app_sim_qvm_weapon_behavior_source {
  qa_weapon_behavior_definition definition;
  qa_app_sim_qvm_weapon_host host;
};
static qa_weapon_behavior_instance_result qvm_attach(void *ctx, qa_weapon_behavior_launch launch) {
  qa_app_sim_qvm_weapon_behavior_source *s = ctx;
  return s->host.attach(s->host.ctx, launch);
}
qa_app_sim_qvm_weapon_behavior_source *qa_app_sim_qvm_weapon_behavior_source_create(
    qa_weapon_behavior_definition definition, qa_app_sim_qvm_weapon_host host) {
  qa_app_sim_qvm_weapon_behavior_source *s = calloc(1, sizeof *s);
  if (!s) qa_range_error("Out of memory");
  s->definition = definition; s->host = host;
  return s;
}
void qa_app_sim_qvm_weapon_behavior_source_destroy(qa_app_sim_qvm_weapon_behavior_source *source) { free(source); }
qa_weapon_behavior_definition qa_app_sim_qvm_weapon_behavior_source_definition(const qa_app_sim_qvm_weapon_behavior_source *source) { return source->definition; }
qa_weapon_behavior_source qa_app_sim_qvm_weapon_behavior_source_as_source(qa_app_sim_qvm_weapon_behavior_source *source) {
  qa_weapon_behavior_source out; memset(&out, 0, sizeof out);
  out.ctx = source; out.definition = source->definition; out.attach = qvm_attach;
  return out;
}
qa_app_sim_qvm_weapon_behavior_checkpoint qa_app_sim_qvm_weapon_behavior_source_checkpoint(qa_app_sim_qvm_weapon_behavior_source *source) {
  return source->host.checkpoint(source->host.ctx);
}
void qa_app_sim_qvm_weapon_behavior_source_restore(qa_app_sim_qvm_weapon_behavior_source *source,
    qa_app_sim_qvm_weapon_behavior_checkpoint checkpoint) {
  source->host.restore(source->host.ctx, checkpoint);
}
void qa_app_sim_qvm_weapon_behavior_source_close(qa_app_sim_qvm_weapon_behavior_source *source) {
  if (source->host.close) source->host.close(source->host.ctx);
}
