/* C checks for src/app/bootstrap/presentation-state.ts (PresentationState):
 * the PresentationState parts of tests/audio/component-presentation.test.ts,
 * tests/audio/component-loops.test.ts and
 * tests/first-playable/simulation/q1-fog-save.test.ts, reproduced on the
 * state itself (SimulationEvents adds only network messages on top). Values
 * not stated by those tests come from running the TS module under bun on
 * the same inputs. Every callback checks its sentinel context.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/component_media.h"
#include "qa/app/bootstrap/presentation_state.h"
#include "qa/content/composition/q1/types.h"
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

static const char *content = "q1:classic:id1:installed", *mod_content = "q1:classic:hipnotic:installed";
static int sentinel, reference_sentinel;
static double now_seconds = 1;
static qa_identity_owner ids;

static qa_source_time now_fn(void *ctx) {
  CHECK(ctx == &sentinel);
  return (qa_source_time){QA_SOURCE_TIME_SECONDS, now_seconds};
}
static bool slot_fn(void *ctx, qa_actor_id actor, double *out) {
  CHECK(ctx == &sentinel);
  *out = (double)actor.slot;
  return true;
}
static qa_actor_id reference_fn(void *ctx, qa_saved_actor_id saved) {
  CHECK(ctx == &reference_sentinel);
  return qa_identity_owner_actor(&ids, (int64_t)saved.slot, (int64_t)saved.generation);
}
static qa_app_presentation_state *world(void) {
  qa_app_presentation_state *state = qa_app_presentation_state_create(now_fn, slot_fn, &sentinel, NULL);
  CHECK(state != NULL);
  return state;
}
static const qa_source_time no_time = {QA_SOURCE_TIME_SECONDS, 0};
static qa_actor_id nobody(void) { qa_actor_id a; memset(&a, 0, sizeof a); return a; }

/* capture -> encodeCheckpointValue -> decodeCheckpointValue -> restore. */
static qa_save_value *roundtrip_value(qa_app_presentation_state *from) {
  qa_save_value *captured = qa_app_presentation_state_capture(from);
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(captured, &length);
  qa_save_value_free(captured);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  free(bytes);
  return decoded;
}
static void restore_value(qa_app_presentation_state *into, const qa_save_value *value) {
  qa_save_reader reader;
  CHECK(qa_save_reader_init(&reader, value, NULL));
  qa_app_presentation_state_restore(into, &reader, reference_fn, &reference_sentinel);
  qa_save_reader_destroy(&reader);
}

/* expect(() => ...).toThrow(text) */
typedef struct thrower { void (*fn)(void *arg); void *arg; } thrower;
static void run_thrower(void *ctx) { thrower *t = ctx; t->fn(t->arg); }
static bool throws(void (*fn)(void *arg), void *arg, const char *text) {
  thrower t = {fn, arg};
  qa_caught_error caught;
  if (qa_try(run_thrower, &t, &caught)) return false;
  bool hit = strstr(caught.message, text) != NULL;
  if (!hit) fprintf(stderr, "  caught: %s\n", caught.message);
  return hit;
}

/* Source events. */
static qa_app_sim_source_presentation_event music_track(double track) {
  qa_app_sim_source_presentation_event e; memset(&e, 0, sizeof e);
  e.kind = "music"; e.music.kind = "cd-track"; e.music.track = track;
  return e;
}
static qa_q1_event style_events[16]; static size_t style_event_count;
static qa_app_sim_source_presentation_event q1_style(const char *pattern) {
  qa_q1_event *q = &style_events[style_event_count++ % 16];
  memset(q, 0, sizeof *q);
  q->kind = "lightstyle"; q->style = 0; q->pattern = pattern;
  qa_app_sim_source_presentation_event e; memset(&e, 0, sizeof e);
  e.kind = "q1"; e.q1 = q;
  return e;
}
static qa_app_sim_source_presentation_event q1_frags(double value) {
  qa_app_sim_source_presentation_event e; memset(&e, 0, sizeof e);
  e.kind = "q1-client"; e.q1_client.kind = "frags"; e.q1_client.slot = 0; e.q1_client.value = value;
  return e;
}
static qa_component_presentation_media_request music_request(const char *intro, const char *loop) {
  qa_component_presentation_media_request r; memset(&r, 0, sizeof r);
  r.kind = QA_MEDIA_REQUEST_MUSIC; r.intro = intro; r.loop = loop;
  return r;
}
static qa_component_presentation_media_request music_stop(void) {
  qa_component_presentation_media_request r; memset(&r, 0, sizeof r);
  r.kind = QA_MEDIA_REQUEST_MUSIC_STOP;
  return r;
}
static qa_component_presentation_media_request shader_cue(const char *original, const char *replacement, double offset) {
  qa_component_presentation_media_request r; memset(&r, 0, sizeof r);
  r.kind = QA_MEDIA_REQUEST_SHADER_REMAP; r.original = original; r.replacement = replacement; r.time_offset = offset;
  return r;
}
static void emit(qa_app_presentation_state *s, const char *in, qa_app_sim_source_presentation_event e) {
  qa_app_presentation_state_emit(s, in, &e, false, no_time, false, nobody());
}
static void bound_emit(qa_app_presentation_owner_binding *b, const char *in, qa_app_sim_source_presentation_event e) {
  qa_app_presentation_owner_binding_emit(b, in, &e, false, no_time, false, nobody());
}
static void publish(qa_app_presentation_state *s, qa_app_presentation_owner_binding *b, const char *in,
                    qa_component_presentation_media_request r, bool initializing) {
  qa_presentation_owner owner;
  if (b) owner = qa_app_presentation_owner_binding_owner(b);
  qa_app_presentation_state_publish_local_media(s, b ? &owner : NULL, in, &r, initializing, NULL, NULL, NULL);
}
static void acknowledge_all(qa_app_presentation_state *s, bool committed) {
  size_t count = 0;
  const qa_app_retained_presentation *const *pending = qa_app_presentation_state_pending_local_media(s, &count);
  for (size_t i = 0; i < count; i++) qa_app_presentation_state_acknowledge_local_media(s, pending[i], committed);
}
static size_t take_count(qa_app_presentation_state *s) {
  size_t count = 0;
  qa_app_presentation_state_take(s, &count);
  return count;
}
static size_t persistent_count(qa_app_presentation_state *s) {
  size_t count = 0;
  qa_app_presentation_state_persistent_presentation(s, &count);
  return count;
}
static void assert_consumed(void *arg) { qa_app_presentation_state_assert_local_media_consumed(arg); }
static bool consumed(qa_app_presentation_state *s) {
  return !throws(assert_consumed, s, "completed local");
}
static bool cue_is(const qa_app_retained_presentation *r, const char *original, const char *replacement, double offset) {
  return r->local && r->local_media.event.kind == QA_MEDIA_REQUEST_SHADER_REMAP && strcmp(r->local_media.event.original, original) == 0
      && strcmp(r->local_media.event.replacement, replacement) == 0 && r->local_media.event.time_offset == offset;
}

/* ---- local music retains silence and saved chronology without entering replication ---- */
static void test_local_music_chronology(void) {
  qa_app_presentation_state *first = world();
  qa_app_presentation_owner_binding *one = qa_app_presentation_state_bind_owner(first, "mod:one", mod_content, false);
  qa_app_presentation_owner_binding *two = qa_app_presentation_state_bind_owner(first, "mod:two", mod_content, false);
  emit(first, content, music_track(2));
  publish(first, one, mod_content, music_request("music/one.wav", "music/loop.wav"), false);
  publish(first, two, mod_content, music_stop(), false);
  CHECK(persistent_count(first) == 1);
  CHECK(take_count(first) == 1);
  CHECK(!consumed(first));
  acknowledge_all(first, true);
  CHECK(consumed(first));
  qa_save_value *state = roundtrip_value(first);
  qa_app_presentation_state *restored = world();
  restore_value(restored, state);
  qa_app_presentation_owner_binding *r_one = qa_app_presentation_state_bind_owner(restored, "mod:one", mod_content, true);
  qa_app_presentation_owner_binding *r_two = qa_app_presentation_state_bind_owner(restored, "mod:two", mod_content, true);
  qa_app_presentation_state_finish_owner_restore(restored);
  CHECK(consumed(restored));
  size_t count = 0;
  qa_app_presentation_state_pending_local_media(restored, &count);
  CHECK(count == 0);
  qa_app_presentation_state_enable_local_media(restored);
  const qa_app_retained_presentation *const *queued_all = qa_app_presentation_state_pending_local_media(restored, &count);
  CHECK(count == 2);
  const qa_app_retained_presentation *queued[2] = {queued_all[0], queued_all[1]};
  CHECK(queued[0]->local && queued[0]->local_media.event.kind == QA_MEDIA_REQUEST_MUSIC);
  CHECK(queued[1]->local && queued[1]->local_media.event.kind == QA_MEDIA_REQUEST_MUSIC_STOP);
  publish(restored, r_two, mod_content, music_request("default.wav", ""), true);
  const qa_app_retained_presentation *const *again = qa_app_presentation_state_pending_local_media(restored, &count);
  CHECK(count == 2 && again[0] == queued[0] && again[1] == queued[1]);
  for (size_t i = 0; i < 2; i++) qa_app_presentation_state_acknowledge_local_media(restored, queued[i], true);
  take_count(restored);
  qa_app_presentation_owner_binding_close(r_two);
  const qa_app_sim_presentation_event *taken = qa_app_presentation_state_take(restored, &count);
  CHECK(count == 1 && strcmp(taken[0].kind, "presentation-owner") == 0);
  const qa_app_retained_presentation *const *pending = qa_app_presentation_state_pending_local_media(restored, &count);
  CHECK(count == 1 && pending[0]->local && pending[0]->local_media.event.kind == QA_MEDIA_REQUEST_MUSIC
        && strcmp(pending[0]->local_media.event.intro, "music/one.wav") == 0
        && strcmp(pending[0]->local_media.event.loop, "music/loop.wav") == 0);
  qa_save_value_free(state);
  qa_app_presentation_owner_binding_free(one); qa_app_presentation_owner_binding_free(two);
  qa_app_presentation_owner_binding_free(r_one); qa_app_presentation_owner_binding_free(r_two);
  qa_app_presentation_state_destroy(first); qa_app_presentation_state_destroy(restored);
}

/* ---- retiring local music reveals a later gameplay cue only locally ---- */
typedef struct publish_args { qa_app_presentation_state *s; qa_presentation_owner owner; const char *content; } publish_args;
static void publish_stop(void *arg) {
  publish_args *p = arg;
  qa_component_presentation_media_request r = music_stop();
  qa_app_presentation_state_publish_local_media(p->s, &p->owner, p->content, &r, false, NULL, NULL, NULL);
}
static void test_retire_local_music(void) {
  qa_app_presentation_state *first = world();
  qa_app_presentation_owner_binding *one = qa_app_presentation_state_bind_owner(first, "mod:one", mod_content, false);
  publish(first, one, mod_content, music_stop(), false);
  size_t count = 0;
  const qa_app_retained_presentation *pending = qa_app_presentation_state_pending_local_media(first, &count)[0];
  CHECK(count == 1);
  emit(first, content, music_track(7));
  take_count(first);
  qa_app_presentation_owner_binding_close(one);
  CHECK(!qa_app_presentation_state_local_media_current(first, pending));
  const qa_app_sim_presentation_event *taken = qa_app_presentation_state_take(first, &count);
  CHECK(count == 1 && strcmp(taken[0].kind, "presentation-owner") == 0);
  const qa_app_retained_presentation *const *now = qa_app_presentation_state_pending_local_media(first, &count);
  CHECK(count == 1 && !now[0]->local && strcmp(now[0]->simulation.kind, "music") == 0
        && strcmp(now[0]->simulation.music.kind, "cd-track") == 0 && now[0]->simulation.music.track == 7);
  publish_args args = {first, qa_app_presentation_owner_binding_owner(one), mod_content};
  CHECK(throws(publish_stop, &args, "active presentation owner"));
  qa_app_presentation_owner_binding *next = qa_app_presentation_state_bind_owner(first, "mod:one", mod_content, false);
  args.owner = qa_app_presentation_owner_binding_owner(next); args.content = content;
  CHECK(throws(publish_stop, &args, "active presentation owner"));
  qa_app_presentation_owner_binding_free(one); qa_app_presentation_owner_binding_free(next);
  qa_app_presentation_state_destroy(first);
}

/* ---- awaited local media keeps failures pending and older delayed controls cannot replace a newer cue ---- */
typedef struct media_fixture { qa_app_presentation_state *s; bool fail; const char *played[8]; size_t played_count; } media_fixture;
static int media_sentinel;
static media_fixture media_storage;
static media_fixture *fx(void *ctx) { CHECK(ctx == &media_sentinel); return &media_storage; }
static const qa_app_retained_presentation *const *h_pending(void *ctx, size_t *n) { return qa_app_presentation_state_pending_local_media(fx(ctx)->s, n); }
static double h_applied_sequence(void *ctx) { return qa_app_presentation_state_applied_media_sequence(fx(ctx)->s); }
static bool h_current(void *ctx, const qa_app_retained_presentation *r) { return qa_app_presentation_state_local_media_current(fx(ctx)->s, r); }
static void h_ack(void *ctx, const qa_app_retained_presentation *r, bool committed) { qa_app_presentation_state_acknowledge_local_media(fx(ctx)->s, r, committed); }
static void h_applied(void *ctx, double sequence) { qa_app_presentation_state_applied_media(fx(ctx)->s, sequence); }
static void h_applied_shader(void *ctx, const char *original, double sequence) { qa_app_presentation_state_applied_shader(fx(ctx)->s, original, sequence); }
static const qa_app_retained_presentation *h_resolve(void *ctx, const qa_app_retained_presentation *r) { return qa_app_presentation_state_resolve_shader_replay(fx(ctx)->s, r); }
static void h_enable(void *ctx) { qa_app_presentation_state_enable_local_media(fx(ctx)->s); }
static const char *h_play(void *ctx, const qa_app_local_presentation_media *source, qa_app_media_current_fn active, void *active_ctx) {
  media_fixture *f = fx(ctx);
  if (f->fail) return "Media unavailable";
  if (active(active_ctx)) f->played[f->played_count++] = source->event.kind == QA_MEDIA_REQUEST_MUSIC_STOP ? "music-stop" : "music";
  return NULL;
}
static const char *h_receive(void *ctx, const qa_app_sim_presentation_event *events, size_t count, qa_audio_audience audience, bool music) {
  (void)audience; (void)music;
  media_fixture *f = fx(ctx);
  for (size_t i = 0; i < count; i++) if (strcmp(events[i].kind, "music") == 0) f->played[f->played_count++] = events[i].music.kind;
  return NULL;
}
static void test_awaited_local_media(void) {
  media_fixture *f = &media_storage;
  memset(f, 0, sizeof *f);
  f->s = world();
  qa_app_component_media_host host; memset(&host, 0, sizeof host);
  host.ctx = &media_sentinel; host.pending_local_media = h_pending; host.applied_media_sequence = h_applied_sequence;
  host.local_media_current = h_current; host.acknowledge_local_media = h_ack; host.applied_media = h_applied;
  host.applied_shader = h_applied_shader; host.resolve_shader_replay = h_resolve; host.enable_local_media = h_enable;
  host.play_component_media = h_play; host.audio_receive = h_receive;
  qa_app_presentation_owner_binding *owner = qa_app_presentation_state_bind_owner(f->s, "mod:one", mod_content, false);
  emit(f->s, content, music_track(2));
  size_t older_count = 0;
  const qa_app_sim_presentation_event *taken = qa_app_presentation_state_take(f->s, &older_count);
  qa_app_sim_presentation_event older[4];
  for (size_t i = 0; i < older_count && i < 4; i++) CHECK(qa_app_sim_presentation_event_copy(&older[i], &taken[i]));
  publish(f->s, owner, mod_content, music_stop(), false);
  f->fail = true;
  const char *error = qa_app_prepare_presentation_audio(host, NULL, 0, NULL, 0);
  CHECK(error != NULL && strcmp(error, "Media unavailable") == 0);
  CHECK(!consumed(f->s));
  f->fail = false;
  CHECK(qa_app_prepare_presentation_audio(host, NULL, 0, NULL, 0) == NULL);
  CHECK(consumed(f->s));
  CHECK(qa_app_prepare_presentation_audio(host, older, older_count, NULL, 0) == NULL);
  CHECK(f->played_count == 1 && strcmp(f->played[0], "music-stop") == 0);
  emit(f->s, content, music_track(3));
  size_t count = 0;
  taken = qa_app_presentation_state_take(f->s, &count);
  CHECK(qa_app_prepare_presentation_audio(host, taken, count, NULL, 0) == NULL);
  CHECK(f->played_count == 2 && strcmp(f->played[1], "cd-track") == 0);
  for (size_t i = 0; i < older_count && i < 4; i++) qa_app_sim_presentation_event_release(&older[i]);
  qa_app_presentation_owner_binding_free(owner);
  qa_app_presentation_state_destroy(f->s);
}

/* ---- shader ownership restores saved precedence and original material without entering server output ---- */
static void test_shader_ownership(void) {
  qa_app_presentation_state *first = world();
  qa_app_presentation_owner_binding *one = qa_app_presentation_state_bind_owner(first, "mod:one", mod_content, false);
  qa_app_presentation_owner_binding *two = qa_app_presentation_state_bind_owner(first, "mod:two", mod_content, false);
  publish(first, NULL, content, shader_cue("textures/wall.tga", "textures/base", 2), false);
  acknowledge_all(first, true);
  publish(first, one, mod_content, shader_cue("textures/wall.tga", "textures/one", 2), false);
  acknowledge_all(first, true);
  publish(first, two, mod_content, shader_cue("textures/wall.tga", "textures/wall.tga", 2), false);
  acknowledge_all(first, true);
  CHECK(take_count(first) == 0);
  CHECK(persistent_count(first) == 0);
  qa_save_value *state = roundtrip_value(first);
  qa_app_presentation_state *restored = world();
  restore_value(restored, state);
  qa_save_value_free(state);
  qa_app_presentation_owner_binding *r_one = qa_app_presentation_state_bind_owner(restored, "mod:one", mod_content, true);
  qa_app_presentation_owner_binding *r_two = qa_app_presentation_state_bind_owner(restored, "mod:two", mod_content, true);
  qa_app_presentation_state_finish_owner_restore(restored);
  qa_app_presentation_state_enable_local_media(restored);
  size_t count = 0;
  const qa_app_retained_presentation *const *saved_all = qa_app_presentation_state_pending_local_media(restored, &count);
  CHECK(count == 3);
  const qa_app_retained_presentation *saved[3] = {saved_all[0], saved_all[1], saved_all[2]};
  publish(restored, NULL, content, shader_cue("textures/wall.tga", "textures/default", 2), true);
  const qa_app_retained_presentation *const *again = qa_app_presentation_state_pending_local_media(restored, &count);
  CHECK(count == 3 && again[0] == saved[0] && again[1] == saved[1] && again[2] == saved[2]);
  for (size_t i = 0; i < 3; i++) CHECK(qa_app_presentation_state_local_media_current(restored, saved[i]));
  for (size_t i = 0; i < 3; i++) qa_app_presentation_state_acknowledge_local_media(restored, saved[i], true);
  qa_app_presentation_owner_binding_close(r_two);
  const qa_app_retained_presentation *const *pending = qa_app_presentation_state_pending_local_media(restored, &count);
  CHECK(count == 1 && cue_is(pending[0], "textures/wall.tga", "textures/one", 2));
  acknowledge_all(restored, true);
  qa_app_presentation_owner_binding_close(r_one);
  pending = qa_app_presentation_state_pending_local_media(restored, &count);
  CHECK(count == 1 && cue_is(pending[0], "textures/wall.tga", "textures/base", 2));
  qa_app_presentation_state *bare = world();
  qa_app_presentation_owner_binding *owner = qa_app_presentation_state_bind_owner(bare, "mod:bare", mod_content, false);
  publish(bare, owner, mod_content, shader_cue("textures/wall.tga", "textures/one", 2), false);
  acknowledge_all(bare, true);
  qa_app_presentation_owner_binding_close(owner);
  pending = qa_app_presentation_state_pending_local_media(bare, &count);
  CHECK(count == 1 && cue_is(pending[0], "textures/wall.tga", "textures/wall.tga", 0));
  qa_app_presentation_owner_binding_free(one); qa_app_presentation_owner_binding_free(two);
  qa_app_presentation_owner_binding_free(r_one); qa_app_presentation_owner_binding_free(r_two);
  qa_app_presentation_owner_binding_free(owner);
  qa_app_presentation_state_destroy(first); qa_app_presentation_state_destroy(restored); qa_app_presentation_state_destroy(bare);
}

/* ---- failed and source-refused shader requests preserve earlier owner cues and unrelated material ordering ---- */
static void test_failed_shader_requests(void) {
  qa_app_presentation_state *first = world();
  qa_app_presentation_owner_binding *owner = qa_app_presentation_state_bind_owner(first, "mod:one", mod_content, false);
  publish(first, owner, mod_content, shader_cue("wall", "base", 0), false);
  acknowledge_all(first, true);
  publish(first, owner, mod_content, shader_cue("wall", "missing", 0), false);
  CHECK(!consumed(first));
  acknowledge_all(first, false);
  publish(first, owner, mod_content, shader_cue("other", "other-skin", 0), false);
  acknowledge_all(first, true);
  qa_save_value *state = roundtrip_value(first);
  qa_app_presentation_state *restored = world();
  restore_value(restored, state);
  qa_save_value_free(state);
  qa_app_presentation_owner_binding *r = qa_app_presentation_state_bind_owner(restored, "mod:one", mod_content, true);
  qa_app_presentation_state_finish_owner_restore(restored);
  qa_app_presentation_state_enable_local_media(restored);
  size_t count = 0;
  const qa_app_retained_presentation *const *pending = qa_app_presentation_state_pending_local_media(restored, &count);
  CHECK(count == 2 && cue_is(pending[0], "wall", "base", 0) && cue_is(pending[1], "other", "other-skin", 0));
  for (size_t i = 0; i < count; i++) CHECK(qa_app_presentation_state_local_media_current(restored, pending[i]));
  acknowledge_all(restored, true);
  qa_app_presentation_owner_binding *added = qa_app_presentation_state_bind_owner(restored, "mod:added-after-load", mod_content, false);
  publish(restored, added, mod_content, shader_cue("wall", "new-mod", 0), true);
  pending = qa_app_presentation_state_pending_local_media(restored, &count);
  CHECK(count == 1 && cue_is(pending[0], "wall", "new-mod", 0));
  qa_app_presentation_owner_binding_free(owner); qa_app_presentation_owner_binding_free(r); qa_app_presentation_owner_binding_free(added);
  qa_app_presentation_state_destroy(first); qa_app_presentation_state_destroy(restored);
}

/* ---- persistent component output retires its activation and restores prior source overrides across save ---- */
static void finish_restore(void *arg) { qa_app_presentation_state_finish_owner_restore(arg); }
static void bind_one_again(void *arg) { (void)qa_app_presentation_state_bind_owner(arg, "mod:one", mod_content, true); }
static void emit_retired(void *arg) { bound_emit(arg, mod_content, q1_style("b")); }
static void test_persistent_component_output(void) {
  qa_app_presentation_state *first = world();
  emit(first, content, q1_style("m"));
  qa_app_presentation_owner_binding *one = qa_app_presentation_state_bind_owner(first, "mod:one", mod_content, false);
  qa_app_presentation_owner_binding *two = qa_app_presentation_state_bind_owner(first, "mod:two", mod_content, false);
  bound_emit(one, mod_content, q1_style("a"));
  bound_emit(two, mod_content, q1_style("z"));
  qa_q1_event model; memset(&model, 0, sizeof model);
  model.kind = "static-model"; model.path = "progs/flame.mdl"; model.has_origin = true;
  model.origin = (qa_vec3){1, 2, 3}; model.frame = 0.75; model.skin = -1.5; model.color_map = 0;
  qa_app_sim_source_presentation_event model_event; memset(&model_event, 0, sizeof model_event);
  model_event.kind = "q1"; model_event.q1 = &model;
  bound_emit(one, mod_content, model_event);
  CHECK(strcmp(qa_app_presentation_state_light_style(first, 0), "z") == 0);
  size_t count = 0;
  const qa_app_sim_presentation_event *const *persistent = qa_app_presentation_state_persistent_presentation(first, &count);
  bool truncated = false;
  for (size_t i = 0; i < count; i++)
    if (strcmp(persistent[i]->kind, "q1") == 0 && strcmp(persistent[i]->q1->kind, "static-model") == 0)
      truncated = persistent[i]->q1->frame == 0 && persistent[i]->q1->skin == -1 && !signbit(persistent[i]->q1->frame);
  CHECK(truncated);
  qa_save_value *state = roundtrip_value(first);
  take_count(first);
  qa_app_presentation_state *restored = world();
  restore_value(restored, state);
  qa_save_value_free(state);
  CHECK(throws(finish_restore, restored, "was not restored"));
  qa_app_presentation_owner_binding *r_one = qa_app_presentation_state_bind_owner(restored, "mod:one", mod_content, true);
  qa_app_presentation_owner_binding *r_two = qa_app_presentation_state_bind_owner(restored, "mod:two", mod_content, true);
  qa_app_presentation_state_finish_owner_restore(restored);
  CHECK(throws(bind_one_again, restored, "already active"));
  take_count(restored);
  qa_app_presentation_owner_binding_close(r_two);
  CHECK(strcmp(qa_app_presentation_state_light_style(restored, 0), "a") == 0);
  const qa_app_sim_presentation_event *output = qa_app_presentation_state_take(restored, &count);
  double retired_generation = -1;
  for (size_t i = 0; i < count; i++)
    if (strcmp(output[i].kind, "presentation-owner") == 0) retired_generation = output[i].presentation_owner.owner.generation;
  CHECK(retired_generation == 2);
  CHECK(throws(emit_retired, r_two, "retired"));
  qa_app_presentation_owner_binding *new_two = qa_app_presentation_state_bind_owner(restored, "mod:two", mod_content, false);
  bound_emit(new_two, mod_content, q1_style("c"));
  output = qa_app_presentation_state_take(restored, &count);
  CHECK(count >= 1 && output[0].has_owner && output[0].owner.generation > retired_generation);
  qa_app_presentation_owner_binding_close(r_one);
  persistent = qa_app_presentation_state_persistent_presentation(restored, &count);
  bool model_left = false;
  for (size_t i = 0; i < count; i++)
    if (strcmp(persistent[i]->kind, "q1") == 0 && strcmp(persistent[i]->q1->kind, "static-model") == 0) model_left = true;
  CHECK(!model_left);
  CHECK(strcmp(qa_app_presentation_state_light_style(restored, 0), "c") == 0);
  qa_app_presentation_owner_binding_close(new_two);
  CHECK(strcmp(qa_app_presentation_state_light_style(restored, 0), "m") == 0);
  qa_app_presentation_owner_binding_close(one); qa_app_presentation_owner_binding_close(two);
  qa_app_presentation_owner_binding_free(one); qa_app_presentation_owner_binding_free(two);
  qa_app_presentation_owner_binding_free(r_one); qa_app_presentation_owner_binding_free(r_two);
  qa_app_presentation_owner_binding_free(new_two);
  qa_app_presentation_state_destroy(first); qa_app_presentation_state_destroy(restored);
}

/* ---- legacy persistence stays primary unless restoring a component makes provenance ambiguous ---- */
static void bind_ambiguous(void *arg) { (void)qa_app_presentation_state_bind_owner(arg, "mod:ambiguous", content, true); }
static void test_legacy_persistence(void) {
  qa_app_presentation_state *source = world();
  qa_q1_event style; memset(&style, 0, sizeof style);
  style.kind = "lightstyle"; style.style = 2; style.pattern = "abc";
  qa_app_sim_source_presentation_event e; memset(&e, 0, sizeof e);
  e.kind = "q1"; e.q1 = &style;
  emit(source, content, e);
  qa_save_value *saved = qa_app_presentation_state_capture(source);
  qa_save_value *legacy = qa_save_value_object();
  const char *keys[] = {"presentationSequence", "styles", "persistent"};
  for (size_t i = 0; i < 3; i++) {
    qa_save_value *copy = NULL;
    for (size_t j = 0; j < saved->entry_count; j++)
      if (strcmp(saved->entries[j].key, keys[i]) == 0) { copy = saved->entries[j].value; saved->entries[j].value = qa_save_value_null(); }
    qa_save_value_set(legacy, keys[i], copy);
  }
  qa_save_value_free(saved);
  qa_app_presentation_state *target = world();
  restore_value(target, legacy);
  qa_save_value_free(legacy);
  qa_app_presentation_owner_binding *unrelated = qa_app_presentation_state_bind_owner(target, "mod:unrelated", mod_content, true);
  qa_app_presentation_owner_binding_close(unrelated);
  CHECK(strcmp(qa_app_presentation_state_light_style(target, 2), "abc") == 0);
  CHECK(throws(bind_ambiguous, target, "without recorded ownership"));
  qa_app_presentation_owner_binding *fresh = qa_app_presentation_state_bind_owner(target, "mod:fresh", content, false);
  qa_app_presentation_owner_binding_close(fresh);
  CHECK(strcmp(qa_app_presentation_state_light_style(target, 2), "abc") == 0);
  qa_app_presentation_state_finish_owner_restore(target);
  qa_app_presentation_owner_binding_free(unrelated); qa_app_presentation_owner_binding_free(fresh);
  qa_app_presentation_state_destroy(source); qa_app_presentation_state_destroy(target);
}

/* ---- retiring a global override replays still-live recipient overrides in source order ---- */
static void test_recipient_override_replay(void) {
  qa_app_presentation_state *state = world();
  qa_actor_id recipient = qa_identity_owner_actor(&ids, 4, 0);
  emit(state, content, q1_frags(0));
  qa_app_presentation_owner_binding *first = qa_app_presentation_state_bind_owner(state, "mod:first", content, false);
  qa_app_presentation_owner_binding *second = qa_app_presentation_state_bind_owner(state, "mod:second", content, false);
  bound_emit(first, content, q1_frags(1));
  qa_app_sim_source_presentation_event two = q1_frags(2);
  qa_app_presentation_owner_binding_emit(second, content, &two, false, no_time, true, recipient);
  size_t count = 0;
  CHECK(take_count(state) == 3);
  qa_app_presentation_owner_binding_close(first);
  /* bun: [presentation-owner retired, q1-client frags 0 (world), q1-client frags 2 (recipient 4:0)] */
  const qa_app_sim_presentation_event *out = qa_app_presentation_state_take(state, &count);
  CHECK(count == 3);
  if (count == 3) {
    CHECK(strcmp(out[0].kind, "presentation-owner") == 0 && strcmp(out[0].presentation_owner.kind, "retired") == 0);
    CHECK(strcmp(out[1].kind, "q1-client") == 0 && out[1].q1_client.value == 0 && !out[1].has_recipient && out[1].sequence == 4);
    CHECK(strcmp(out[2].kind, "q1-client") == 0 && out[2].q1_client.value == 2 && out[2].has_recipient
          && qa_same_actor(out[2].recipient, recipient) && out[2].has_owner && out[2].sequence == 5);
  }
  qa_app_presentation_owner_binding_close(second);
  out = qa_app_presentation_state_take(state, &count);
  CHECK(count == 2);
  if (count == 2) CHECK(strcmp(out[1].kind, "q1-client") == 0 && out[1].q1_client.value == 0 && !out[1].has_recipient);
  qa_app_presentation_owner_binding_free(first); qa_app_presentation_owner_binding_free(second);
  qa_app_presentation_state_destroy(state);
}

/* ---- component loop ownership survives save remapping (component-loops.test.ts) ---- */
static qa_actor_id restored_actor;
static qa_actor_id fixed_reference(void *ctx, qa_saved_actor_id saved) {
  CHECK(ctx == &reference_sentinel); (void)saved;
  return restored_actor;
}
static void test_component_loops(void) {
  qa_app_presentation_state *events = world();
  qa_actor_id actor = qa_identity_owner_actor(&ids, 4, 2);
  const char *owners[3] = {"mod:first", "mod:second", NULL};
  qa_q2_presentation_event sound[3];
  for (size_t i = 0; i < 3; i++) {
    memset(&sound[i], 0, sizeof sound[i]);
    sound[i].kind = "sound"; sound[i].has_actor = true; sound[i].actor = actor; sound[i].path = "world/loop.wav";
    sound[i].channel = 0; sound[i].volume = 1; sound[i].attenuation = 1; sound[i].loop = "start";
    if (owners[i]) { sound[i].has_loop_owner = true; sound[i].loop_owner = owners[i]; }
    qa_app_sim_source_presentation_event e; memset(&e, 0, sizeof e);
    e.kind = "q2"; e.q2 = &sound[i];
    emit(events, "q2:classic:baseq2:installed", e);
  }
  CHECK(persistent_count(events) == 3);
  take_count(events);
  qa_identity_owner local;
  CHECK(qa_identity_owner_create(&local, "restored-loop"));
  restored_actor = qa_identity_owner_actor(&local, 9, 5);
  qa_save_value *state = roundtrip_value(events);
  qa_save_reader reader;
  CHECK(qa_save_reader_init(&reader, state, NULL));
  qa_app_presentation_state_restore(events, &reader, fixed_reference, &reference_sentinel);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(state);
  qa_q2_presentation_event stop = sound[0];
  stop.actor = restored_actor; stop.loop = "stop";
  qa_app_sim_source_presentation_event e; memset(&e, 0, sizeof e);
  e.kind = "q2"; e.q2 = &stop;
  emit(events, "q2:classic:baseq2:installed", e);
  size_t count = 0;
  const qa_app_sim_presentation_event *const *retained = qa_app_presentation_state_persistent_presentation(events, &count);
  CHECK(count == 2);
  bool second = false, none = false;
  for (size_t i = 0; i < count; i++) {
    if (retained[i]->q2->has_loop_owner && strcmp(retained[i]->q2->loop_owner, "mod:second") == 0) second = true;
    if (!retained[i]->q2->has_loop_owner) none = true;
  }
  CHECK(second && none);
  qa_app_presentation_state_destroy(events);
  qa_identity_owner_destroy(&local);
}

/* ---- light styles (bun: PresentationState.lightStyles) ---- */
static void test_light_styles(void) {
  qa_app_presentation_state *state = world();
  emit(state, content, q1_style("az"));
  qa_q2_presentation_event q2; memset(&q2, 0, sizeof q2);
  q2.kind = "lightstyle"; q2.style = 3; q2.pattern = "m";
  qa_app_sim_source_presentation_event e; memset(&e, 0, sizeof e);
  e.kind = "q2"; e.q2 = &q2;
  emit(state, content, e);
  qa_q1_event empty; memset(&empty, 0, sizeof empty);
  empty.kind = "lightstyle"; empty.style = 5; empty.pattern = "";
  e.kind = "q1"; e.q1 = &empty;
  emit(state, content, e);
  size_t count = 0;
  const qa_scene_light_style *styles = qa_app_presentation_state_light_styles(state, 0.15, &count);
  /* bun: [{q1,0,value:550},{q2,3,rgb 1,white 3},{q1,5,value 256}] */
  CHECK(count == 3);
  CHECK(!styles[0].is_q2 && styles[0].style == 0 && styles[0].value == 550);
  CHECK(styles[1].is_q2 && styles[1].style == 3 && styles[1].rgb.x == 1 && styles[1].white == 3);
  CHECK(!styles[2].is_q2 && styles[2].value == 256);
  styles = qa_app_presentation_state_light_styles(state, -0.1, &count);
  CHECK(isnan(styles[0].value) && styles[1].white == 3 && styles[2].value == 256);
  styles = qa_app_presentation_state_light_styles(state, NAN, &count);
  CHECK(styles[0].value == 0);
  qa_app_presentation_state_destroy(state);
}

/* ---- capacity: 32,768 undrained events ---- */
static void fill(void *arg) {
  for (int i = 0; i <= QA_APP_PRESENTATION_EVENT_CAPACITY; i++) emit(arg, content, music_track(1));
}
static void test_capacity(void) {
  qa_app_presentation_state *state = world();
  CHECK(throws(fill, state, "Presentation event capacity exhausted (32768 undrained events)"));
  size_t count = 0;
  qa_app_presentation_state_take(state, &count);
  CHECK(count == QA_APP_PRESENTATION_EVENT_CAPACITY);
  qa_app_presentation_state_destroy(state);
}

/* ---- q1 fog: component fog retires to the primary transition (q1-fog-save.test.ts) ---- */
static int alive_sentinel;
static bool alive_fn(void *ctx, qa_actor_id actor) { CHECK(ctx == &alive_sentinel); (void)actor; return true; }
static qa_app_sim_source_presentation_event fog_event(qa_comp_q1_event *composition, qa_q1ad_event *fog, const qa_actor_id *player,
                                                      double density, qa_vec3 color, double duration, double sky) {
  memset(fog, 0, sizeof *fog); memset(composition, 0, sizeof *composition);
  fog->kind = QA_Q1AD_EVENT_FOG; fog->has_player = player != NULL; if (player) fog->player = *player;
  fog->density = density; fog->color_vec = color; fog->duration = duration; fog->sky_factor = sky;
  composition->kind = QA_COMP_Q1_EVENT_ADDON; composition->addon_event = fog;
  qa_app_sim_source_presentation_event e; memset(&e, 0, sizeof e);
  e.kind = "q1-composition"; e.q1_composition = composition;
  return e;
}
static void test_component_fog(void) {
  const char *map = "q1:rerelease:mg1:retail", *component = "q1:rerelease:copper:installed";
  qa_app_sim_q1_fog_options options; memset(&options, 0, sizeof options);
  options.content = map; options.entities = "{\"classname\" \"worldspawn\" \"fog\" \"0.2 0 0 1\"}";
  options.alive_ctx = &alive_sentinel; options.alive = alive_fn;
  qa_app_presentation_state *source = qa_app_presentation_state_create(now_fn, slot_fn, &sentinel, &options);
  qa_actor_id player = qa_identity_owner_actor(&ids, 1, 0);
  qa_app_presentation_owner_binding *owner = qa_app_presentation_state_bind_owner(source, "mod:fog", component, false);
  qa_comp_q1_event c; qa_q1ad_event f;
  now_seconds = 10;
  emit(source, map, fog_event(&c, &f, NULL, 1, (qa_vec3){1, 0, 0}, 4, 0.25));
  size_t count = 0;
  const qa_app_sim_presentation_event *out = qa_app_presentation_state_take(source, &count);
  CHECK(count == 2 && strcmp(out[1].kind, "q1-fog") == 0 && !out[1].q1_fog.has_player && out[1].q1_fog.sky_factor == 0.25);
  bound_emit(owner, component, fog_event(&c, &f, &player, 0.9, (qa_vec3){0, 1, 0}, 0, 0.1));
  out = qa_app_presentation_state_take(source, &count);
  CHECK(count == 2 && strcmp(out[1].kind, "q1-fog") == 0 && out[1].q1_fog.has_player && out[1].has_owner);
  qa_save_value *checkpoint = roundtrip_value(source);
  qa_app_presentation_state *restored = qa_app_presentation_state_create(now_fn, slot_fn, &sentinel, &options);
  restore_value(restored, checkpoint);
  qa_save_value_free(checkpoint);
  qa_app_presentation_owner_binding *rebound = qa_app_presentation_state_bind_owner(restored, "mod:fog", component, true);
  qa_app_presentation_state_finish_owner_restore(restored);
  out = qa_app_presentation_state_take(restored, &count);
  /* bun: the restored output replays the global transition then the owned actor transition. */
  CHECK(count == 2);
  if (count == 2) CHECK(!out[0].has_owner && !out[0].q1_fog.has_player && out[1].has_owner && out[1].q1_fog.has_player);
  qa_app_presentation_owner_binding_close(rebound);
  out = qa_app_presentation_state_take(restored, &count);
  /* bun: [presentation-owner retired, q1-fog global transition] */
  CHECK(count == 2);
  if (count == 2) CHECK(strcmp(out[0].kind, "presentation-owner") == 0 && strcmp(out[1].kind, "q1-fog") == 0 && !out[1].has_owner);
  qa_app_presentation_owner_binding_close(owner);
  out = qa_app_presentation_state_take(source, &count);
  CHECK(count == 2);
  now_seconds = 1;
  qa_app_presentation_owner_binding_free(owner); qa_app_presentation_owner_binding_free(rebound);
  qa_app_presentation_state_destroy(source); qa_app_presentation_state_destroy(restored);
}

/* ---- emitOwned virtual dispatch and receivePresentation ---- */
static int override_sentinel, override_calls;
static qa_app_presentation_state *override_state;
static qa_app_sim_presentation_event override_fn(void *ctx, const qa_presentation_owner *owner, qa_content_id in,
    const qa_app_sim_source_presentation_event *source, bool has_time, qa_source_time time, bool has_recipient, qa_actor_id recipient) {
  CHECK(ctx == &override_sentinel);
  override_calls++;
  return qa_app_presentation_state_emit_owned_event(override_state, owner, in, source, has_time, time, has_recipient, recipient);
}
static void test_override_and_receive(void) {
  qa_app_presentation_state *state = override_state = world();
  qa_app_presentation_state_set_emit_owned_override(state, override_fn, &override_sentinel);
  qa_app_presentation_owner_binding *one = qa_app_presentation_state_bind_owner(state, "mod:one", content, false);
  bound_emit(one, content, music_track(4));
  emit(state, content, music_track(5));
  qa_app_presentation_state_refresh_owner(state, "mod:one");
  qa_app_presentation_owner_binding_close(one);
  CHECK(override_calls == 4);
  size_t count = 0;
  const qa_app_sim_presentation_event *out = qa_app_presentation_state_take(state, &count);
  CHECK(count == 5);
  qa_app_presentation_state *client = world();
  qa_presentation_owner token = {"mod:one", 7};
  qa_app_presentation_state_admit_replicated_owner(client, token, content);
  double before = qa_app_presentation_state_receive_presentation(client, &out[0]);
  CHECK(before == 0);
  size_t client_count = 0;
  CHECK(qa_app_presentation_state_persistent_presentation(client, &client_count)[0]->music.track == 4);
  qa_app_presentation_state_retire_replicated_owner(client, token);
  qa_app_presentation_owner_binding *next = qa_app_presentation_state_bind_owner(client, "mod:one", content, false);
  CHECK(qa_app_presentation_owner_binding_owner(next).generation == 8);
  qa_app_presentation_owner_binding_free(one); qa_app_presentation_owner_binding_free(next);
  qa_app_presentation_state_destroy(state); qa_app_presentation_state_destroy(client);
}

int main(void) {
  CHECK(qa_identity_owner_create(&ids, "presentation-owners"));
  test_local_music_chronology();
  test_retire_local_music();
  test_awaited_local_media();
  test_shader_ownership();
  test_failed_shader_requests();
  test_persistent_component_output();
  test_legacy_persistence();
  test_recipient_override_replay();
  test_component_loops();
  test_light_styles();
  test_capacity();
  test_component_fog();
  test_override_and_receive();
  qa_identity_owner_destroy(&ids);
  printf("test_presentation_state: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
