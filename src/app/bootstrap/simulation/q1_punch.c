/* Port of src/app/bootstrap/simulation/q1-punch.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q1_punch.h"
#include <stdlib.h>
#include <string.h>
/* TS dropQ1Punch lives in qa/content/q1/foundation/
 * entity_services.h as qa_q1_drop_punch. */
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
#define MAX_LOCAL 64
struct qa_app_sim_q1_player_punch {
  qa_app_sim_q1_punch_actors actors;
  bool (*source)(void *ctx, qa_actor_id actor, qa_app_sim_q1_punch_owner *out); void *source_ctx;
  qa_owned_actor keys[MAX_LOCAL]; qa_vec3 values[MAX_LOCAL]; size_t count;
};
static bool same_owned(qa_owned_actor a, qa_owned_actor b) {
  return qa_same_actor(a.id, b.id) && a.owner == b.owner;
}
static int find_local(qa_app_sim_q1_player_punch *p, qa_owned_actor actor) {
  for (size_t i = 0; i < p->count; i++) if (same_owned(p->keys[i], actor)) return (int)i;
  return -1;
}
static void delete_local(qa_app_sim_q1_player_punch *p, size_t i) {
  memmove(&p->keys[i], &p->keys[i + 1], (p->count - i - 1) * sizeof p->keys[0]);
  memmove(&p->values[i], &p->values[i + 1], (p->count - i - 1) * sizeof p->values[0]);
  p->count--;
}
typedef struct owner_ref { qa_owned_actor actor; bool has_source; qa_app_sim_q1_punch_owner source; } owner_ref;
static owner_ref resolve(qa_app_sim_q1_player_punch *p, qa_actor_id actor) {
  owner_ref out; memset(&out, 0, sizeof out);
  if (!p->actors.resolve_owned(p->actors.ctx, actor, &out.actor))
    qa_range_error("Q1 punch references a retired actor");
  out.has_source = p->source(p->source_ctx, out.actor.id, &out.source);
  int at = find_local(p, out.actor);
  if (out.has_source && at >= 0) { out.source.write(out.source.ctx, p->values[at]); delete_local(p, (size_t)at); }
  return out;
}
qa_app_sim_q1_player_punch *qa_app_sim_q1_player_punch_create(qa_app_sim_q1_punch_actors actors,
    bool (*source)(void *ctx, qa_actor_id actor, qa_app_sim_q1_punch_owner *out), void *source_ctx) {
  qa_app_sim_q1_player_punch *p = calloc(1, sizeof *p);
  if (!p) qa_range_error("Out of memory");
  p->actors = actors; p->source = source; p->source_ctx = source_ctx;
  return p;
}
void qa_app_sim_q1_player_punch_destroy(qa_app_sim_q1_player_punch *punch) { free(punch); }
void qa_app_sim_q1_player_punch_release(qa_app_sim_q1_player_punch *punch, qa_owned_actor actor) {
  int at = find_local(punch, actor); if (at >= 0) delete_local(punch, (size_t)at);
}
qa_vec3 qa_app_sim_q1_player_punch_read(qa_app_sim_q1_player_punch *punch, qa_actor_id actor) {
  owner_ref o = resolve(punch, actor);
  if (o.has_source) return o.source.read(o.source.ctx);
  int at = find_local(punch, o.actor);
  return at >= 0 ? punch->values[at] : (qa_vec3){0, 0, 0};
}
void qa_app_sim_q1_player_punch_write(qa_app_sim_q1_player_punch *punch, qa_actor_id actor, qa_vec3 value) {
  qa_vec3 angles = {(float)value.x, (float)value.y, (float)value.z};
  owner_ref o = resolve(punch, actor);
  if (o.has_source) { o.source.write(o.source.ctx, angles); return; }
  int at = find_local(punch, o.actor);
  if (at >= 0) punch->values[at] = angles;
  else {
    if (punch->count >= MAX_LOCAL) qa_range_error("Out of memory");
    punch->keys[punch->count] = o.actor; punch->values[punch->count++] = angles;
  }
}
qa_vec3 qa_app_sim_q1_player_punch_advance(qa_app_sim_q1_player_punch *punch, qa_actor_id actor, double elapsed, qa_numeric_operations numeric) {
  qa_vec3 current = qa_app_sim_q1_player_punch_read(punch, actor);
  if (current.x == 0 && current.y == 0 && current.z == 0) return current;
  qa_vec3 next = qa_q1_drop_punch(current, elapsed, &numeric);
  qa_app_sim_q1_player_punch_write(punch, actor, next);
  return next;
}
qa_app_sim_q1_punch_capture_entry *qa_app_sim_q1_player_punch_capture(qa_app_sim_q1_player_punch *punch, size_t *out_count) {
  size_t i = 0;
  while (i < punch->count) { qa_actor_id id = punch->keys[i].id; (void)resolve(punch, id); if (i < punch->count && qa_same_actor(punch->keys[i].id, id)) i++; }
  qa_app_sim_q1_punch_capture_entry *out = punch->count ? malloc(punch->count * sizeof *out) : NULL;
  if (punch->count && !out) qa_range_error("Out of memory");
  for (size_t k = 0; k < punch->count; k++) {
    out[k].actor = qa_saved_actor_id_of(punch->keys[k].id); out[k].angles = punch->values[k];
  }
  if (out_count) *out_count = punch->count;
  return out;
}
void qa_app_sim_q1_player_punch_restore(qa_app_sim_q1_player_punch *punch, const qa_save_reader *reader) {
  punch->count = 0;
  if (qa_save_reader_is_undefined(reader)) return;
  size_t n = qa_save_reader_list_count(reader);
  for (size_t i = 0; i < n; i++) {
    qa_save_reader entry, af, vf;
    if (!qa_save_reader_item(reader, i, &entry)) qa_save_reader_fail(reader, "out of memory");
    if (!qa_save_reader_field(&entry, "actor", &af)) qa_save_reader_fail(&entry, "out of memory");
    qa_saved_actor_id saved = qa_read_saved_actor(&af); qa_save_reader_destroy(&af);
    qa_owned_actor actor;
    qa_app_sim_q1_punch_owner ignored;
    if (!punch->actors.resolve_saved(punch->actors.ctx, saved, &actor)
        || find_local(punch, actor) >= 0 || punch->source(punch->source_ctx, actor.id, &ignored))
      qa_save_reader_fail(&entry, "Saved Q1 punch has no unique fallback owner");
    if (!qa_save_reader_field(&entry, "angles", &vf)) qa_save_reader_fail(&entry, "out of memory");
    qa_vec3 angles = qa_read_vector(&vf); qa_save_reader_destroy(&vf);
    if (punch->count >= MAX_LOCAL) qa_save_reader_fail(&entry, "out of memory");
    punch->keys[punch->count] = actor; punch->values[punch->count++] = angles;
    qa_save_reader_destroy(&entry);
  }
}
