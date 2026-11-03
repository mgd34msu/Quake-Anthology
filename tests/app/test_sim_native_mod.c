/* C checks for src/app/bootstrap/simulation/native-mod-presentation.ts
 * (readNativeModPresentation and the NativeModPresentation class). No TS
 * test imports the module directly; the cases follow its code paths and
 * the provider checkpoint validation that now calls
 * readNativeModPresentation. Every callback checks a sentinel ctx.
 * GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/simulation/native_mod_presentation.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
#include "qa/world/session/mod_engine.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(expr) do { checks++; fflush(NULL); pid_t pid_ = fork(); \
    if (pid_ == 0) { prctl(PR_SET_DUMPABLE, 0); if (freopen("/dev/null", "w", stderr) == NULL) _exit(3); expr; _exit(0); } \
    int status_ = 0; waitpid(pid_, &status_, 0); \
    if (!(WIFSIGNALED(status_) || (WIFEXITED(status_) && WEXITSTATUS(status_) != 0))) { failures++; fprintf(stderr, "%s:%d: expected abort: %s\n", __FILE__, __LINE__, #expr); } } while (0)
static int sentinel_storage;
#define SENTINEL ((void *)&sentinel_storage)
#define CTX_OK(ctx) do { if ((ctx) != SENTINEL) { fprintf(stderr, "%s:%d: wrong callback ctx\n", __FILE__, __LINE__); abort(); } } while (0)

static qa_save_value *fog_entry(double slot, double generation, double density) {
  qa_save_value *row = qa_save_value_object(), *actor = qa_save_value_object(), *value = qa_save_value_object(), *fog = qa_save_value_object(), *height = qa_save_value_object();
  qa_save_value_set(actor, "slot", qa_save_value_number(slot)); qa_save_value_set(actor, "generation", qa_save_value_number(generation));
  qa_save_value *color = qa_save_value_object(); qa_save_value_set(color, "x", qa_save_value_number(0.5)); qa_save_value_set(color, "y", qa_save_value_number(0.25)); qa_save_value_set(color, "z", qa_save_value_number(1));
  qa_save_value_set(fog, "density", qa_save_value_number(density)); qa_save_value_set(fog, "color", color); qa_save_value_set(fog, "skyFactor", qa_save_value_number(0));
  const char *vectors[] = {"startColor", "endColor"};
  for (int i = 0; i < 2; i++) { qa_save_value *v = qa_save_value_object(); qa_save_value_set(v, "x", qa_save_value_number(0)); qa_save_value_set(v, "y", qa_save_value_number(0)); qa_save_value_set(v, "z", qa_save_value_number(0)); qa_save_value_set(height, vectors[i], v); }
  qa_save_value_set(height, "startDistance", qa_save_value_number(0)); qa_save_value_set(height, "endDistance", qa_save_value_number(0));
  qa_save_value_set(height, "falloff", qa_save_value_number(0)); qa_save_value_set(height, "density", qa_save_value_number(0));
  qa_save_value_set(value, "fog", fog); qa_save_value_set(value, "heightFog", height);
  qa_save_value_set(row, "actor", actor); qa_save_value_set(row, "value", value); return row;
}
static void read_tree(qa_save_value *tree, qa_app_sim_native_mod_presentation_checkpoint *out) {
  qa_save_reader reader; qa_save_reader_init(&reader, tree, "presentation"); qa_app_sim_read_native_mod_presentation(&reader, out); qa_save_reader_destroy(&reader);
}
static void test_read_presentation(void) {
  qa_save_value *tree = qa_save_value_object(), *fog = qa_save_value_array();
  qa_save_value_push(fog, fog_entry(3, 7, 0.125)); qa_save_value_set(tree, "fog", fog);
  qa_app_sim_native_mod_presentation_checkpoint read; read_tree(tree, &read);
  /* clients is optional (older checkpoints): undefined reads as an empty list. */
  CHECK(read.fog_count == 1 && read.fog[0].actor.slot == 3 && read.fog[0].actor.generation == 7 && read.fog[0].value.fog.density == 0.125 && read.client_count == 0);
  qa_app_sim_native_mod_client_entry client = {{2, 9}, "xv 0 yv 0", (double[]){1, 0, 5}, 3};
  qa_app_sim_native_mod_presentation_checkpoint built = read; built.clients = &client; built.client_count = 1;
  qa_save_value *encoded = qa_app_sim_native_mod_presentation_checkpoint_value(&built);
  qa_app_sim_native_mod_presentation_checkpoint again; read_tree(encoded, &again);
  CHECK(again.fog_count == 1 && again.fog[0].value.fog.color.y == 0.25 && again.client_count == 1);
  CHECK(again.client_count == 1 && strcmp(again.clients[0].layout, "xv 0 yv 0") == 0 && again.clients[0].inventory_count == 3 && again.clients[0].inventory[2] == 5);
  qa_app_sim_native_mod_presentation_checkpoint_free(&again); qa_save_value_free(encoded);
  qa_save_value_push(fog, fog_entry(3, 7, 0.5));
  qa_app_sim_native_mod_presentation_checkpoint scratch;
  CHECK_ABORTS(read_tree(tree, &scratch)); /* Duplicate native mod presentation recipient */
  qa_app_sim_native_mod_presentation_checkpoint_free(&read); qa_save_value_free(tree);
}

/* --- presentation class over a sentinel source --- */
typedef struct world { qa_identity_owner identity; qa_session_actor_registry *actors; qa_shared_body_table *bodies; size_t sounds, starts, stops, events; double sound_index; const char *last_loop; } world;
static world *W;
static const qa_app_sim_configstring table[] = {{288 + 4, "world/amb.wav"}, {288 + 5, "world/hum.wav"}};
static const qa_app_sim_configstring *s_configstrings(void *ctx, size_t *n) { CTX_OK(ctx); *n = 2; return table; }
static const qa_app_sim_classic_guest_message *s_drain(void *ctx, size_t *n) { CTX_OK(ctx); *n = 0; return NULL; }
static qa_app_sim_native_mod_appearance s_appearance(void *ctx, double slot) { CTX_OK(ctx); (void)slot; qa_app_sim_native_mod_appearance a; memset(&a, 0, sizeof a); a.path = "models/a.md2"; a.attached_models[0] = "models/b.md2"; a.attached_models[1] = ""; a.attached_models[2] = ""; a.attached_model_count = 3; a.scale = 1; a.alpha = 1; a.visible = true; return a; }
static const char *s_signature(void *ctx, double slot) { CTX_OK(ctx); (void)slot; return "[[1,0,0,0],0]"; }
static qa_app_sim_native_mod_player_state s_player(void *ctx, double slot) { CTX_OK(ctx); (void)slot; qa_app_sim_native_mod_player_state p; memset(&p, 0, sizeof p); return p; }
static qa_app_sim_native_mod_clock s_clock(void *ctx) { CTX_OK(ctx); qa_app_sim_native_mod_clock c = {3, 300, true, 100}; return c; }
static qa_app_sim_native_mod_entity_state s_state(void *ctx, double slot) { CTX_OK(ctx); (void)slot; qa_app_sim_native_mod_entity_state s; memset(&s, 0, sizeof s); s.active = true; s.visible = true; s.sound = W->sound_index; s.event = 2; s.volume = 1; s.attenuation = 1; return s; }
static bool p_actor_at(void *ctx, double slot, qa_actor_id *out) { CTX_OK(ctx); (void)slot; (void)out; return false; }
static bool p_slot_of(void *ctx, qa_actor_id actor, double *out) { CTX_OK(ctx); (void)actor; (void)out; return false; }
static bool p_accepts(void *ctx, double slot) { CTX_OK(ctx); (void)slot; return false; }
static void e_emit(void *ctx, const qa_session_mod_emit *value) {
  CTX_OK(ctx); const qa_session_mod_emit *emit = value;
  CHECK(strcmp(emit->event.kind, "q2") == 0 && emit->event.q2 != NULL);
  const qa_q2_presentation_event *event = emit->event.q2;
  if (strcmp(event->kind, "sound") == 0) { W->sounds++; if (strcmp(event->loop, "start") == 0) W->starts++; else W->stops++; W->last_loop = event->path; }
  if (strcmp(event->kind, "entity-event") == 0) W->events++;
}
static qa_source_time t_time(void *ctx) { CTX_OK(ctx); qa_source_time t = {QA_SOURCE_TIME_SECONDS, 1}; return t; }
static qa_bounds bounds_fn(void *ctx, qa_owned_actor actor, qa_body_state state) { (void)ctx; return qa_translated_body_bounds(actor, state); }
static void link_fn(void *ctx, qa_linked_body body) { (void)ctx; (void)body; }
static void unlink_fn(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; }
static void test_presentation_class(void) {
  world w; memset(&w, 0, sizeof w); W = &w; w.sound_index = 4;
  CHECK(qa_identity_owner_create(&w.identity, "native-mod-presentation"));
  w.actors = qa_session_actor_registry_create(&w.identity, 64);
  qa_body_link_hooks hooks = {NULL, bounds_fn, link_fn, unlink_fn}; w.bodies = qa_shared_body_table_create(w.actors, hooks);
  qa_session_mod_engine_services engine; memset(&engine, 0, sizeof engine); engine.ctx = SENTINEL; engine.events.ctx = SENTINEL; engine.events.emit = e_emit;
  qa_mod_host_services services; memset(&services, 0, sizeof services); services.actors = w.actors; services.bodies = w.bodies; services.ctx = SENTINEL; services.time = t_time; services.engine = &engine;
  qa_app_sim_native_mod_projection projection; memset(&projection, 0, sizeof projection);
  projection.ctx = SENTINEL; projection.actor_at = p_actor_at; projection.slot_of = p_slot_of; projection.accepts_client = p_accepts;
  qa_app_sim_native_mod_presentation_source source = {"classic", SENTINEL, s_configstrings, s_drain, s_appearance, s_signature, s_player, s_clock, s_state};
  qa_app_sim_native_mod_presentation *p = qa_app_sim_native_mod_presentation_create(source, "q2:test", &projection, &services, NULL, "mod:test", NULL);
  qa_owned_actor actor = qa_session_actor_registry_allocate(w.actors, "mod:test", "mod:test-actor");
  qa_app_sim_native_mod_actor_slot row = {actor.id, 5};
  qa_app_sim_native_mod_presentation_publish(p, &row, 1, true);
  CHECK(w.starts == 1 && w.events == 1 && strcmp(w.last_loop, "world/amb.wav") == 0);
  qa_app_sim_native_mod_presentation_publish(p, &row, 1, true); /* same loop and event: nothing new */
  CHECK(w.starts == 1 && w.stops == 0 && w.events == 1);
  w.sound_index = 5; qa_app_sim_native_mod_presentation_publish(p, &row, 1, true);
  CHECK(w.stops == 1 && w.starts == 2 && strcmp(w.last_loop, "world/hum.wav") == 0);
  qa_app_sim_native_mod_presentation_begin_frame(p); qa_app_sim_native_mod_presentation_publish(p, &row, 1, true);
  CHECK(w.events == 2); /* beginFrame forgets the last event, so it replays */
  size_t count = 0; const qa_app_simulation_presentation *items = qa_app_sim_native_mod_presentation_appearance(p, actor.id, 5, &count);
  CHECK(count == 2 && strcmp(items[1].path, "models/b.md2") == 0);
  CHECK(strcmp(qa_app_sim_native_mod_presentation_signature(p, 5), "[[1,0,0,0],0]") == 0);
  qa_app_sim_native_mod_presentation_publish(p, NULL, 0, true); /* actor left the published set: loop stops */
  CHECK(w.stops == 2);
  double generation = qa_app_sim_native_mod_presentation_generation(p); qa_app_sim_native_mod_presentation_close(p);
  CHECK(qa_app_sim_native_mod_presentation_generation(p) == generation + 1);
  qa_app_sim_native_mod_presentation_destroy(p);
  qa_shared_body_table_destroy(w.bodies); qa_session_actor_registry_destroy(w.actors);
}
int main(void) {
  test_read_presentation();
  test_presentation_class();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
