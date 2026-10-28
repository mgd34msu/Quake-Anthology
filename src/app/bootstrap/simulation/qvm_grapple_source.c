/* Port of src/app/bootstrap/simulation/qvm-grapple-source.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/qvm_grapple_source.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_save_reader_fail(r, "out of memory"); return o;
}
qa_app_sim_qvm_grapple_source_checkpoint qa_app_sim_read_qvm_grapple_source_checkpoint(const qa_save_reader *reader) {
  qa_app_sim_qvm_grapple_source_checkpoint out; memset(&out, 0, sizeof out);
  qa_save_reader grapple = fld(reader, "grapple"), f;
  f = fld(&grapple, "module"); out.grapple.module = qa_read_guest(&f);
  if (out.grapple.module.kind != QA_GUEST_CHECKPOINT_QVM) qa_save_reader_fail(&f, "Expected QVM grapple continuation");
  qa_save_reader_destroy(&f);
  f = fld(reader, "version"); out.version = qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  f = fld(&grapple, "version"); out.grapple.version = qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  f = fld(&grapple, "profile"); out.grapple.profile = qa_save_reader_string(&f); qa_save_reader_destroy(&f);
  f = fld(&grapple, "owners");
  out.grapple.owner_count = qa_save_reader_list_count(&f);
  out.grapple.owners = out.grapple.owner_count ? malloc(out.grapple.owner_count * sizeof *out.grapple.owners) : NULL;
  for (size_t i = 0; i < out.grapple.owner_count; i++) {
    qa_save_reader e; qa_save_reader_item(&f, i, &e);
    out.grapple.owners[i] = qa_read_saved_actor(&e); qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&grapple);
  f = fld(reader, "bindings");
  out.binding_count = qa_save_reader_list_count(&f);
  out.bindings = out.binding_count ? malloc(out.binding_count * sizeof *out.bindings) : NULL;
  for (size_t i = 0; i < out.binding_count; i++) {
    qa_save_reader e, bf; qa_save_reader_item(&f, i, &e);
    bf = fld(&e, "actor"); out.bindings[i].actor = qa_read_saved_actor(&bf); qa_save_reader_destroy(&bf);
    bf = fld(&e, "pointer"); out.bindings[i].pointer = qa_save_reader_integer_min(&bf, 1); qa_save_reader_destroy(&bf);
    bf = fld(&e, "client"); out.bindings[i].client = qa_save_reader_boolean(&bf); qa_save_reader_destroy(&bf);
    bf = fld(&e, "origin"); out.bindings[i].origin = qa_read_vector(&bf); qa_save_reader_destroy(&bf);
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  f = fld(reader, "tethers");
  out.tether_count = qa_save_reader_list_count(&f);
  out.tethers = out.tether_count ? malloc(out.tether_count * sizeof *out.tethers) : NULL;
  for (size_t i = 0; i < out.tether_count; i++) {
    qa_save_reader e, bf; qa_save_reader_item(&f, i, &e);
    bf = fld(&e, "owner"); out.tethers[i].owner = qa_read_saved_actor(&bf); qa_save_reader_destroy(&bf);
    bf = fld(&e, "actor"); out.tethers[i].actor = qa_read_saved_actor(&bf); qa_save_reader_destroy(&bf);
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  return out;
}
#define MAX_BINDINGS 128
struct qa_app_sim_qvm_grapple_source {
  qa_app_sim_qvm_grapple_host host;
  bool closed;
  struct { qa_actor_id actor; double pointer; bool client; qa_vec3 origin; } bindings[MAX_BINDINGS];
  size_t binding_count;
  struct { qa_actor_id owner, actor; } tethers[MAX_BINDINGS];
  size_t tether_count;
};
static qa_vec3 angle_forward(qa_vec3 angles) {
  /* qvmAngleVectors(angles).forward with degree angles. */
  double yaw = angles.y * 3.141592653589793 / 180, pitch = angles.x * 3.141592653589793 / 180;
  return (qa_vec3){cos(yaw) * cos(pitch), sin(yaw) * cos(pitch), -sin(pitch)};
}
qa_app_sim_qvm_grapple_source *qa_app_sim_qvm_grapple_source_create(qa_app_sim_qvm_grapple_host host) {
  qa_app_sim_qvm_grapple_source *s = calloc(1, sizeof *s);
  if (!s) qa_range_error("Out of memory");
  s->host = host;
  return s;
}
void qa_app_sim_qvm_grapple_source_destroy(qa_app_sim_qvm_grapple_source *source) { free(source); }
bool qa_app_sim_qvm_grapple_source_pulling(qa_app_sim_qvm_grapple_source *source, qa_actor_id actor) {
  return source->host.pulling && source->host.pulling(source->host.ctx, actor);
}
void qa_app_sim_qvm_grapple_source_pull(qa_app_sim_qvm_grapple_source *source, qa_actor_id actor) {
  qa_body_state_result body = qa_shared_body_table_read(source->host.bodies, actor);
  if (!body.has_value) return;
  if (source->host.synchronize) source->host.synchronize(source->host.ctx);
  source->host.core_pull(source->host.ctx, actor, angle_forward(body.value.angles));
}
qa_app_sim_qvm_grapple_source_checkpoint qa_app_sim_qvm_grapple_source_capture(qa_app_sim_qvm_grapple_source *source) {
  qa_app_sim_qvm_grapple_source_checkpoint out; memset(&out, 0, sizeof out);
  out.version = 1;
  out.grapple = source->host.core_capture(source->host.ctx);
  out.binding_count = source->binding_count;
  out.bindings = out.binding_count ? malloc(out.binding_count * sizeof *out.bindings) : NULL;
  for (size_t i = 0; i < out.binding_count; i++) {
    out.bindings[i].actor = qa_saved_actor_id_of(source->bindings[i].actor);
    out.bindings[i].pointer = source->bindings[i].pointer;
    out.bindings[i].client = source->bindings[i].client;
    out.bindings[i].origin = source->bindings[i].origin;
  }
  out.tether_count = source->tether_count;
  out.tethers = out.tether_count ? malloc(out.tether_count * sizeof *out.tethers) : NULL;
  for (size_t i = 0; i < out.tether_count; i++) {
    out.tethers[i].owner = qa_saved_actor_id_of(source->tethers[i].owner);
    out.tethers[i].actor = qa_saved_actor_id_of(source->tethers[i].actor);
  }
  return out;
}
static qa_actor_id restore_actor(qa_app_sim_qvm_grapple_source *source, qa_saved_actor_id saved) {
  qa_owned_actor resolved;
  if (!qa_session_actor_registry_resolve_saved(source->host.actors, saved, &resolved))
    qa_range_error("Saved grapple refers to a missing shared actor");
  return resolved.id;
}
void qa_app_sim_qvm_grapple_source_restore(qa_app_sim_qvm_grapple_source *source, qa_app_sim_qvm_grapple_source_checkpoint checkpoint) {
  if (checkpoint.version != 1) qa_range_error("Invalid grapple source checkpoint");
  source->binding_count = 0;
  for (size_t i = 0; i < checkpoint.binding_count; i++) {
    qa_actor_id actor = restore_actor(source, checkpoint.bindings[i].actor);
    for (size_t j = 0; j < source->binding_count; j++)
      if (qa_same_actor(source->bindings[j].actor, actor) || source->bindings[j].pointer == checkpoint.bindings[i].pointer)
        qa_range_error("Duplicate saved grapple binding");
    if (source->binding_count >= MAX_BINDINGS) qa_range_error("Out of memory");
    size_t at = source->binding_count++;
    source->bindings[at].actor = actor;
    source->bindings[at].pointer = checkpoint.bindings[i].pointer;
    source->bindings[at].client = checkpoint.bindings[i].client;
    source->bindings[at].origin = checkpoint.bindings[i].origin;
  }
  size_t restored_count = 0;
  struct { qa_actor_id owner, actor; } restored[MAX_BINDINGS];
  for (size_t i = 0; i < checkpoint.tether_count; i++) {
    qa_actor_id owner = restore_actor(source, checkpoint.tethers[i].owner);
    qa_owned_actor actor;
    bool resolved = qa_session_actor_registry_resolve_saved(source->host.actors, checkpoint.tethers[i].actor, &actor);
    bool dup = false;
    for (size_t j = 0; j < restored_count; j++) if (qa_same_actor(restored[j].owner, owner)) dup = true;
    if (!resolved || actor.owner != source->host.provider || dup) qa_range_error("Invalid saved shared tether");
    restored[restored_count].owner = owner; restored[restored_count].actor = actor.id; restored_count++;
  }
  (void)restored_count;
  source->tether_count = 0;
  source->host.core_restore(source->host.ctx, checkpoint.grapple);
}
void qa_app_sim_qvm_grapple_source_close(qa_app_sim_qvm_grapple_source *source) {
  if (source->closed) return;
  if (source->host.close_provider) source->host.close_provider(source->host.ctx);
  source->closed = true; source->binding_count = 0; source->tether_count = 0;
  if (source->host.close_files) source->host.close_files(source->host.ctx);
  if (source->host.retire_vm) source->host.retire_vm(source->host.ctx);
}
