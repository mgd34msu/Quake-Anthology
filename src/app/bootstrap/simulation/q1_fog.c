/* Port of src/app/bootstrap/simulation/q1-fog.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q1_fog.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"
#define MAX_ACTORS 64
typedef struct retained {
  bool has_player; qa_actor_id player;
  qa_q1_fog_state state; double sky_factor;
  bool has_context; qa_app_sim_q1_fog_context context;
} retained;
struct qa_app_sim_q1_fog {
  qa_app_sim_q1_fog_options options;
  qa_q1_fog_transition initial;
  retained global;
  retained actors[MAX_ACTORS]; size_t actor_count;
  qa_app_sim_presentation_event out[MAX_ACTORS + 1]; size_t out_count;
};
static bool same_actor(qa_actor_id a, qa_actor_id b) { return qa_same_actor(a, b); }
static retained *find_actor(qa_app_sim_q1_fog *f, qa_actor_id actor) {
  for (size_t i = 0; i < f->actor_count; i++) if (same_actor(f->actors[i].player, actor)) return &f->actors[i];
  return NULL;
}
static void emit(qa_app_sim_q1_fog *f, retained *value) {
  if (!value->has_context) return;
  qa_app_sim_presentation_event *e = &f->out[f->out_count++];
  memset(e, 0, sizeof *e);
  e->kind = "q1-fog"; e->q1_fog.kind = "transition";
  e->content = value->context.content; e->sequence = value->context.sequence; e->seconds = value->context.seconds;
  e->has_source_entity = value->context.has_source_entity; e->source_entity = value->context.source_entity;
  e->q1_fog.has_player = value->has_player; e->q1_fog.player = value->player;
  e->q1_fog.transition = qa_q1_fog_state_capture(&value->state); e->q1_fog.sky_factor = value->sky_factor;
}
qa_app_sim_q1_fog *qa_app_sim_q1_fog_create(qa_app_sim_q1_fog_options options) {
  qa_app_sim_q1_fog *f = calloc(1, sizeof *f);
  if (!f) qa_range_error("Out of memory");
  f->options = options;
  f->initial = qa_q1_world_fog(options.entities);
  qa_q1_fog_state_init(&f->global.state);
  qa_q1_fog_state_install(&f->global.state, f->initial);
  f->global.sky_factor = 0.5;
  return f;
}
void qa_app_sim_q1_fog_destroy(qa_app_sim_q1_fog *fog) { free(fog); }
void qa_app_sim_q1_fog_reset(qa_app_sim_q1_fog *fog) {
  qa_q1_fog_state_install(&fog->global.state, fog->initial);
  fog->global.sky_factor = 0.5; fog->global.has_context = false; fog->actor_count = 0;
}
void qa_app_sim_q1_fog_retire(qa_app_sim_q1_fog *fog, qa_actor_id actor) {
  for (size_t i = 0; i < fog->actor_count; i++)
    if (same_actor(fog->actors[i].player, actor)) {
      memmove(&fog->actors[i], &fog->actors[i + 1], (fog->actor_count - i - 1) * sizeof(retained));
      fog->actor_count--; return;
    }
}
static bool accepts(qa_app_sim_q1_fog *f, qa_content_id content) {
  if (content && f->options.content && strcmp(content, f->options.content) == 0) return true;
  if (!f->options.has_accepted) return false;
  for (size_t i = 0; i < f->options.accepted_content_count; i++)
    if (content && strcmp(content, f->options.accepted_contents[i]) == 0) return true;
  return false;
}
static qa_content_id accepted_text(qa_app_sim_q1_fog *f, qa_content_id content) {
  if (f->options.content && strcmp(content, f->options.content) == 0) return f->options.content;
  for (size_t i = 0; f->options.has_accepted && i < f->options.accepted_content_count; i++)
    if (strcmp(content, f->options.accepted_contents[i]) == 0) return f->options.accepted_contents[i];
  return content;
}
static bool alive_of(qa_app_sim_q1_fog *f, qa_actor_id a) { return f->options.alive(f->options.alive_ctx, a); }
static void apply(qa_app_sim_q1_fog *f, retained *value, qa_app_sim_q1_fog_context ctx, qa_app_sim_q1_fog_event_in ev) {
  qa_q1_fog fog = {ev.density, ev.color};
  qa_q1_fog_state_update(&value->state, fog, ctx.seconds, ev.duration);
  value->sky_factor = ev.sky_factor < 0 ? 0 : ev.sky_factor > 1 ? 1 : ev.sky_factor;
  value->context = ctx; value->has_context = true;
  emit(f, value);
}
const qa_app_sim_presentation_event *qa_app_sim_q1_fog_update(qa_app_sim_q1_fog *fog,
    qa_app_sim_q1_fog_context context, qa_app_sim_q1_fog_event_in event, size_t *out_count) {
  fog->out_count = 0;
  if (accepts(fog, context.content)) {
    if (!event.has_player) {
      apply(fog, &fog->global, context, event);
      for (size_t i = 0; i < fog->actor_count;) {
        retained *v = &fog->actors[i];
        if (!v->has_player || !alive_of(fog, v->player)) {
          memmove(v, v + 1, (fog->actor_count - i - 1) * sizeof(retained)); fog->actor_count--; continue;
        }
        apply(fog, v, context, event); i++;
      }
    } else if (alive_of(fog, event.player)) {
      retained *v = find_actor(fog, event.player);
      if (v == NULL) {
        if (fog->actor_count >= MAX_ACTORS) qa_range_error("Out of memory");
        v = &fog->actors[fog->actor_count++];
        memset(v, 0, sizeof *v); v->has_player = true; v->player = event.player;
        qa_q1_fog_state_init(&v->state);
        qa_q1_fog_state_install(&v->state, qa_q1_fog_state_capture(&fog->global.state));
        v->sky_factor = fog->global.sky_factor;
      }
      apply(fog, v, context, event);
    }
  }
  if (out_count) *out_count = fog->out_count;
  return fog->out;
}
const qa_app_sim_presentation_event *qa_app_sim_q1_fog_presentation(qa_app_sim_q1_fog *fog, size_t *out_count) {
  fog->out_count = 0;
  emit(fog, &fog->global);
  for (size_t i = 0; i < fog->actor_count; i++) emit(fog, &fog->actors[i]);
  if (out_count) *out_count = fog->out_count;
  return fog->out;
}
static qa_save_value *fog_value(qa_q1_fog v) {
  qa_save_value *o = qa_save_value_object(), *c = qa_save_value_object();
  if (!o || !c) qa_range_error("Out of memory");
  qa_save_value_set(c, "x", qa_save_value_number(v.color.x));
  qa_save_value_set(c, "y", qa_save_value_number(v.color.y));
  qa_save_value_set(c, "z", qa_save_value_number(v.color.z));
  qa_save_value_set(o, "density", qa_save_value_number(v.density));
  qa_save_value_set(o, "color", c);
  return o;
}
static qa_save_value *capture_value(retained *value) {
  qa_q1_fog_transition t = qa_q1_fog_state_capture(&value->state);
  qa_save_value *o = qa_save_value_object(), *tr = qa_save_value_object();
  if (!o || !tr) qa_range_error("Out of memory");
  qa_save_value_set(tr, "previous", fog_value(t.previous));
  qa_save_value_set(tr, "target", fog_value(t.target));
  qa_save_value_set(tr, "start", qa_save_value_number(t.start));
  qa_save_value_set(tr, "duration", qa_save_value_number(t.duration));
  qa_save_value_set(o, "transition", tr);
  qa_save_value_set(o, "skyFactor", qa_save_value_number(value->sky_factor));
  if (!value->has_context) qa_save_value_set(o, "context", qa_save_value_null());
  else {
    qa_save_value *c = qa_save_value_object();
    if (!c) qa_range_error("Out of memory");
    qa_save_value_set(c, "content", qa_save_value_string(value->context.content));
    qa_save_value_set(c, "sequence", qa_save_value_number(value->context.sequence));
    qa_save_value_set(c, "seconds", qa_save_value_number(value->context.seconds));
    qa_save_value_set(c, "sourceEntity", value->context.has_source_entity
        ? qa_save_value_number(value->context.source_entity) : qa_save_value_null());
    qa_save_value_set(o, "context", c);
  }
  return o;
}
qa_save_value *qa_app_sim_q1_fog_capture(qa_app_sim_q1_fog *fog) {
  qa_save_value *o = qa_save_value_object(), *actors = qa_save_value_array();
  if (!o || !actors) qa_range_error("Out of memory");
  qa_save_value_set(o, "content", qa_save_value_string(fog->options.content));
  qa_save_value_set(o, "global", capture_value(&fog->global));
  for (size_t i = 0; i < fog->actor_count; i++) {
    retained *v = &fog->actors[i];
    if (!v->has_player || !alive_of(fog, v->player)) continue;
    qa_save_value *entry = capture_value(v);
    qa_saved_actor_id saved = qa_saved_actor_id_of(v->player);
    qa_save_value *actor = qa_save_value_object();
    if (!actor) qa_range_error("Out of memory");
    qa_save_value_set(actor, "slot", qa_save_value_number(saved.slot));
    qa_save_value_set(actor, "generation", qa_save_value_number(saved.generation));
    qa_save_value_set(entry, "player", actor);
    qa_save_value_push(actors, entry);
  }
  qa_save_value_set(o, "actors", actors);
  return o;
}
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_save_reader_fail(r, "out of memory"); return o;
}
static double bounded(const qa_save_reader *r, double lo, double hi) {
  double v = qa_save_reader_finite(r);
  if (v < lo || v > hi) qa_save_reader_fail(r, "fog value out of range");
  return v;
}
static double fbounded(const qa_save_reader *r, const char *n, double lo, double hi) {
  qa_save_reader f = fld(r, n); double v = bounded(&f, lo, hi); qa_save_reader_destroy(&f); return v;
}
static qa_q1_fog read_fog(const qa_save_reader *r) {
  qa_save_reader c = fld(r, "color");
  qa_q1_fog out; out.density = fbounded(r, "density", 0, INFINITY);
  out.color.x = fbounded(&c, "x", 0, 1); out.color.y = fbounded(&c, "y", 0, 1); out.color.z = fbounded(&c, "z", 0, 1);
  qa_save_reader_destroy(&c); return out;
}
static qa_q1_fog_transition read_transition(const qa_save_reader *r) {
  qa_save_reader p = fld(r, "previous"), t = fld(r, "target"), s = fld(r, "start");
  qa_q1_fog_transition out; out.previous = read_fog(&p); out.target = read_fog(&t);
  out.start = qa_save_reader_finite(&s); out.duration = fbounded(r, "duration", 0, INFINITY);
  qa_save_reader_destroy(&p); qa_save_reader_destroy(&t); qa_save_reader_destroy(&s); return out;
}
static retained read_retained(qa_app_sim_q1_fog *fog, const qa_save_reader *entry, bool has_player, qa_actor_id player) {
  retained out; memset(&out, 0, sizeof out); out.has_player = has_player; out.player = player;
  qa_save_reader tr = fld(entry, "transition");
  qa_q1_fog_state_init(&out.state); qa_q1_fog_state_install(&out.state, read_transition(&tr));
  qa_save_reader_destroy(&tr);
  qa_save_reader ctx = fld(entry, "context");
  if (!qa_save_reader_is_null(&ctx)) {
    qa_save_reader cf = fld(&ctx, "content");
    qa_content_id content = qa_read_content_id(&cf); qa_save_reader_destroy(&cf);
    if (!accepts(fog, content)) qa_save_reader_fail(&ctx, "fog source content is not selected");
    /* The context outlives the save tree: keep the equal option string. */
    out.context.content = accepted_text(fog, content);
    qa_save_reader f1 = fld(&ctx, "sequence"); out.context.sequence = qa_save_reader_integer_min(&f1, 0); qa_save_reader_destroy(&f1);
    qa_save_reader f2 = fld(&ctx, "seconds"); out.context.seconds = qa_save_reader_finite(&f2); qa_save_reader_destroy(&f2);
    qa_save_reader f3 = fld(&ctx, "sourceEntity");
    if (!qa_save_reader_is_null(&f3)) { out.has_context = true; out.context.has_source_entity = true; out.context.source_entity = qa_save_reader_integer_min(&f3, 0); }
    else out.has_context = true;
    qa_save_reader_destroy(&f3);
  }
  qa_save_reader_destroy(&ctx);
  if (has_player && !out.has_context) qa_save_reader_fail(entry, "actor fog requires its source context");
  out.sky_factor = fbounded(entry, "skyFactor", 0, 1);
  return out;
}
const qa_app_sim_presentation_event *qa_app_sim_q1_fog_restore(qa_app_sim_q1_fog *fog, const qa_save_reader *reader,
    qa_actor_id (*reference)(void *ctx, qa_saved_actor_id saved), void *reference_ctx, size_t *out_count) {
  qa_save_reader cf = fld(reader, "content");
  qa_content_id content = qa_read_content_id(&cf); qa_save_reader_destroy(&cf);
  if (content == NULL || fog->options.content == NULL || strcmp(content, fog->options.content) != 0)
    qa_save_reader_fail(reader, "fog state belongs to another map content");
  qa_save_reader gf = fld(reader, "global");
  qa_actor_id none; memset(&none, 0, sizeof none);
  retained global = read_retained(fog, &gf, false, none); qa_save_reader_destroy(&gf);
  retained actors[MAX_ACTORS]; size_t count = 0;
  qa_save_reader af = fld(reader, "actors");
  size_t n = qa_save_reader_list_count(&af);
  for (size_t i = 0; i < n; i++) {
    qa_save_reader entry, pf;
    if (!qa_save_reader_item(&af, i, &entry)) qa_save_reader_fail(reader, "out of memory");
    pf = fld(&entry, "player"); qa_saved_actor_id saved = qa_read_saved_actor(&pf); qa_save_reader_destroy(&pf);
    qa_actor_id player = reference(reference_ctx, saved);
    if (!alive_of(fog, player)) qa_save_reader_fail(&entry, "fog actor is not alive");
    for (size_t k = 0; k < count; k++) if (same_actor(actors[k].player, player)) qa_save_reader_fail(&entry, "duplicate fog actor");
    if (count >= MAX_ACTORS) qa_save_reader_fail(&entry, "out of memory");
    actors[count++] = read_retained(fog, &entry, true, player);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&af);
  fog->global.state = global.state; fog->global.sky_factor = global.sky_factor;
  fog->global.has_context = global.has_context; fog->global.context = global.context;
  memcpy(fog->actors, actors, count * sizeof(retained)); fog->actor_count = count;
  return qa_app_sim_q1_fog_presentation(fog, out_count);
}
