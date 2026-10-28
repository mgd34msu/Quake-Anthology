/* Port of src/app/bootstrap/simulation/quakec-weapon-behavior.ts (checkpoint reader + source shell). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/quakec_weapon_behavior.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/world/gameplay/weapon_behaviors.h"
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_save_reader_fail(r, "out of memory"); return o;
}
static double *read_doubles(const qa_save_reader *r, const char *n, double min, size_t *out_count) {
  qa_save_reader f = fld(r, n);
  size_t count = qa_save_reader_list_count(&f);
  double *out = count ? malloc(count * sizeof *out) : NULL;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item; qa_save_reader_item(&f, i, &item);
    out[i] = qa_save_reader_integer_min(&item, min); qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&f); *out_count = count; return out;
}
static qa_app_sim_source_random_state read_random(const qa_save_reader *r) {
  /* TS readRandom (persistence): kind-tagged random state. The glibc
   * form is the one this source restores; other kinds keep their tag. */
  qa_app_sim_source_random_state out; memset(&out, 0, sizeof out);
  qa_save_reader k = fld(r, "kind"); out.kind = qa_save_reader_string(&k); qa_save_reader_destroy(&k);
  if (out.kind && strcmp(out.kind, "glibc-random") == 0) {
    qa_save_reader w = fld(r, "words");
    size_t count = qa_save_reader_list_count(&w);
    for (size_t i = 0; i < count && i < 31; i++) {
      qa_save_reader item; qa_save_reader_item(&w, i, &item);
      out.words[i] = (int32_t)qa_save_reader_integer(&item); qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&w);
    qa_save_reader f;
    f = fld(r, "front"); out.front = (int)qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
    f = fld(r, "rear"); out.rear = (int)qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
    f = fld(r, "draws"); out.draws = (int64_t)qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
  }
  return out;
}
qa_app_sim_quakec_weapon_behavior_checkpoint qa_app_sim_read_quakec_weapon_behavior_checkpoint(
    const qa_save_reader *reader, qa_weapon_behavior_definition expected) {
  qa_app_sim_quakec_weapon_behavior_checkpoint out; memset(&out, 0, sizeof out);
  qa_save_reader machine = fld(reader, "machine"), f;
  f = fld(reader, "version"); out.version = qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  f = fld(reader, "definition"); out.definition = qa_read_weapon_behavior_definition(&f, expected); qa_save_reader_destroy(&f);
  static const char *const modes[] = {"singleplayer", "coop", "deathmatch"};
  f = fld(reader, "mode"); out.mode = qa_save_reader_choice_string(&f, modes, 3); qa_save_reader_destroy(&f);
  f = fld(reader, "sourceTime"); out.source_time = qa_save_reader_finite(&f); qa_save_reader_destroy(&f);
  f = fld(reader, "random"); out.random = read_random(&f); qa_save_reader_destroy(&f);
  f = fld(&machine, "globals"); out.machine.globals = qa_save_reader_bytes(&f, &out.machine.globals_length); qa_save_reader_destroy(&f);
  f = fld(&machine, "entities"); out.machine.entities = qa_save_reader_bytes(&f, &out.machine.entities_length); qa_save_reader_destroy(&f);
  f = fld(&machine, "entityCount"); out.machine.entity_count = qa_save_reader_integer_min(&f, 1); qa_save_reader_destroy(&f);
  f = fld(&machine, "strings"); out.machine.strings = qa_save_reader_bytes(&f, &out.machine.strings_length); qa_save_reader_destroy(&f);
  f = fld(&machine, "statement"); out.machine.statement = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
  f = fld(&machine, "functionIndex"); out.machine.function_index = qa_save_reader_literal_number(&f, 0); qa_save_reader_destroy(&f);
  f = fld(&machine, "argumentCount"); out.machine.argument_count = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
  out.machine.profiling = read_doubles(&machine, "profiling", 0, &out.machine.profiling_count);
  f = fld(&machine, "traceEnabled"); out.machine.trace_enabled = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&machine);
  out.free_slots = read_doubles(reader, "free", 1, &out.free_count);
  out.retired = read_doubles(reader, "retired", 1, &out.retired_count);
  qa_save_reader bindings = fld(reader, "bindings");
  out.binding_count = qa_save_reader_list_count(&bindings);
  out.bindings = out.binding_count ? malloc(out.binding_count * sizeof *out.bindings) : NULL;
  static const char *const kinds[] = {"target", "projectile"};
  for (size_t i = 0; i < out.binding_count; i++) {
    qa_save_reader e, bf; qa_save_reader_item(&bindings, i, &e);
    bf = fld(&e, "slot"); out.bindings[i].slot = qa_save_reader_integer_min(&bf, 1); qa_save_reader_destroy(&bf);
    bf = fld(&e, "actor"); out.bindings[i].actor = qa_read_saved_actor(&bf); qa_save_reader_destroy(&bf);
    bf = fld(&e, "kind"); out.bindings[i].kind = qa_save_reader_choice_string(&bf, kinds, 2); qa_save_reader_destroy(&bf);
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&bindings);
  return out;
}
struct qa_app_sim_quakec_weapon_behavior_source {
  qa_weapon_behavior_definition definition;
  qa_app_sim_qc_weapon_host host;
};
static qa_weapon_behavior_instance_result qc_attach(void *ctx, qa_weapon_behavior_launch launch) {
  qa_app_sim_quakec_weapon_behavior_source *s = ctx;
  return s->host.attach(s->host.ctx, launch);
}
qa_app_sim_quakec_weapon_behavior_source *qa_app_sim_quakec_weapon_behavior_source_create(
    qa_weapon_behavior_definition definition, qa_app_sim_qc_weapon_host host) {
  qa_app_sim_quakec_weapon_behavior_source *s = calloc(1, sizeof *s);
  if (!s) qa_range_error("Out of memory");
  s->definition = definition; s->host = host;
  return s;
}
void qa_app_sim_quakec_weapon_behavior_source_destroy(qa_app_sim_quakec_weapon_behavior_source *source) { free(source); }
qa_weapon_behavior_definition qa_app_sim_quakec_weapon_behavior_source_definition(const qa_app_sim_quakec_weapon_behavior_source *source) { return source->definition; }
qa_weapon_behavior_source qa_app_sim_quakec_weapon_behavior_source_as_source(qa_app_sim_quakec_weapon_behavior_source *source) {
  qa_weapon_behavior_source out; memset(&out, 0, sizeof out);
  out.ctx = source; out.definition = source->definition; out.attach = qc_attach;
  return out;
}
qa_app_sim_quakec_weapon_behavior_checkpoint qa_app_sim_quakec_weapon_behavior_source_checkpoint(qa_app_sim_quakec_weapon_behavior_source *source) {
  return source->host.checkpoint(source->host.ctx);
}
void qa_app_sim_quakec_weapon_behavior_source_restore(qa_app_sim_quakec_weapon_behavior_source *source,
    qa_app_sim_quakec_weapon_behavior_checkpoint checkpoint,
    qa_actor_id (*reference)(void *ctx, qa_saved_actor_id saved), void *reference_ctx,
    qa_app_sim_source_random *random) {
  (void)reference; (void)reference_ctx; (void)random;
  source->host.restore(source->host.ctx, checkpoint);
}
