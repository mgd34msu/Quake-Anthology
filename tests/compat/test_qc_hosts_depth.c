/*
 * Depth checks for the QuakeC host bindings (actor-state.ts,
 * presentation-host.ts finale/broadcast flush). Expected values follow
 * the TypeScript semantics they port: Number() parsing of inline model
 * names, `gravity || 1`, the finale acknowledgement latch, and
 * routeDropping's report-and-discard of malformed broadcast bytes.
 * Every callback checks a sentinel ctx. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qc.h"
#include "qa/compat/qc/actor_state.h"
#include "qa/compat/qc/presentation_host.h"
#include "qa/compat/qc/profile.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/constants.h"
#include "qa/network/q1/message.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    checks++;                                                          \
    if (!(cond)) {                                                     \
      failures++;                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
    }                                                                  \
  } while (0)

static int SENTINEL;
static int bad_ctx = 0;
#define EXPECT_CTX(ctx) do { if ((ctx) != &SENTINEL) bad_ctx++; } while (0)

#define DIGEST "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
static qa_compat_qc_definition FIELDS[] = {
    {"float", 3, false, 0, (char *)"solid"},
    {"float", 3, false, 1, (char *)"flags"},
    {"string", 1, false, 2, (char *)"model"},
    {"entity", 4, false, 3, (char *)"owner"},
    {"float", 3, false, 4, (char *)"movetype"},
    {"vector", 3, false, 5, (char *)"avelocity"},
    {"float", 3, false, 8, (char *)"gravity"},
    {"float", 3, false, 9, (char *)"waterlevel"},
    {"float", 3, false, 10, (char *)"watertype"},
    {"float", 3, false, 11, (char *)"health"},
};
static qa_compat_qc_definition GLOBALS[] = {
    {"entity", 4, false, 28, (char *)"self"},
    {"float", 2, false, 29, (char *)"time"},
};
static qa_compat_qc_function FUNCTIONS[] = {{0, 0, 0, 0, (char *)"", (char *)"", NULL, 0, false}};
static qa_compat_qc_statement STATEMENTS[] = {{QA_QC_OP_DONE, 0, 0, 0}};
static uint8_t INITIAL_GLOBALS[128];
static uint8_t STRINGS[] = {0};
static qa_compat_qc_program program = {
    (char *)"hosts.qc", {QA_QUAKEC_API_Q1_NETQUAKE, 6, 5927},
    STATEMENTS, 1, GLOBALS, 2, FIELDS, 10, FUNCTIONS, 1,
    STRINGS, 1, INITIAL_GLOBALS, 128, 12, 0, (char *)DIGEST,
};

static bool server_active(void *ctx) { EXPECT_CTX(ctx); return true; }
static bool make_machine(qa_compat_qc_entity_memory *entities, qa_compat_qc_builtin_registry *registry, qa_compat_qc_machine *machine) {
  if (!qa_compat_qc_entity_memory_init(entities, qa_compat_qc_classic_entity_layout(&program), 4, 4)) return false;
  qa_compat_qc_machine_options options;
  memset(&options, 0, sizeof options);
  options.program = &program; options.entities = entities;
  options.numeric = qa_create_numeric_operations(&qa_q1_donor_profile);
  options.builtins = registry; options.ctx = &SENTINEL; options.server_active = server_active;
  return qa_compat_qc_machine_init(machine, options);
}

/* actor-state.ts: slot 2 is the source row of the only actor. */
static bool source_slot(void *ctx, qa_actor_id actor, int64_t *out) { EXPECT_CTX(ctx); (void)actor; *out = 2; return true; }
static bool reference_of(void *ctx, int64_t reference, qa_actor_id *out) { EXPECT_CTX(ctx); (void)reference; (void)out; return false; }
static bool is_client(void *ctx, qa_actor_id actor) { EXPECT_CTX(ctx); (void)actor; return false; }

static double model_of(qa_compat_qc_actor_state *state, qa_compat_qc_machine *m, qa_owned_actor actor, const char *name, bool *has) {
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(m->entities, 2);
  qa_compat_qc_words_set_int(w, 2, (double)qa_compat_qc_strings_allocate(&m->strings, name));
  qa_compat_qc_shared_solid solid;
  CHECK(qa_compat_qc_actor_state_collision(state, actor, &solid));
  *has = solid.has_model;
  return solid.model;
}
typedef struct { qa_compat_qc_actor_state *state; qa_owned_actor actor; qa_body_state body; } MotionCall;
static void motion_call(void *ctx) {
  MotionCall *c = ctx; qa_q2_motion motion;
  (void)qa_compat_qc_actor_state_motion(c->state, c->actor, c->body, &motion);
}

static void test_actor_state(void) {
  qa_compat_qc_entity_memory entities; qa_compat_qc_builtin_registry registry; qa_compat_qc_machine m;
  memset(&registry, 0, sizeof registry);
  CHECK(make_machine(&entities, &registry, &m));
  qa_compat_qc_actor_state state;
  qa_compat_qc_actor_state_init(&state, (qa_compat_qc_actor_state_options){&m, false, &SENTINEL, source_slot, reference_of, is_client});
  qa_owned_actor actor; memset(&actor, 0, sizeof actor);
  bool has = false;
  /* TS Number(name.slice(1)) */
  CHECK(model_of(&state, &m, actor, "*12", &has) == 12 && has);
  CHECK(isnan(model_of(&state, &m, actor, "*1a", &has)) && has);
  CHECK(model_of(&state, &m, actor, "*", &has) == 0 && has);
  CHECK(model_of(&state, &m, actor, "* 7 ", &has) == 7);
  CHECK(model_of(&state, &m, actor, "*0x10", &has) == 16);
  CHECK(model_of(&state, &m, actor, "*1e2", &has) == 100);
  CHECK(model_of(&state, &m, actor, "*-Infinity", &has) == -INFINITY);
  CHECK(isnan(model_of(&state, &m, actor, "*inf", &has)));
  CHECK(isnan(model_of(&state, &m, actor, "*-0x10", &has)));
  (void)model_of(&state, &m, actor, "progs/player.mdl", &has);
  CHECK(!has); /* not inline and not the world slot: TS null */

  /* motion: gravity `|| 1` treats 0 and NaN as absent */
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(m.entities, 2);
  qa_compat_qc_words_set_float(w, 4, 6); /* MOVETYPE_TOSS */
  qa_body_state body; memset(&body, 0, sizeof body);
  qa_q2_motion motion;
  qa_compat_qc_words_set_float(w, 8, NAN);
  CHECK(qa_compat_qc_actor_state_motion(&state, actor, body, &motion) && motion.gravity == 1 && strcmp(motion.kind, "toss") == 0);
  qa_compat_qc_words_set_float(w, 8, 0);
  CHECK(qa_compat_qc_actor_state_motion(&state, actor, body, &motion) && motion.gravity == 1);
  qa_compat_qc_words_set_float(w, 8, 0.5);
  CHECK(qa_compat_qc_actor_state_motion(&state, actor, body, &motion) && motion.gravity == 0.5);
  /* MOVETYPE_WALK has no shared kind: plain Error with the TS message */
  qa_compat_qc_words_set_float(w, 4, 2);
  MotionCall call = {&state, actor, body}; qa_caught_error caught;
  CHECK(!qa_try(motion_call, &call, &caught) && strcmp(caught.message, "Unsupported id1 movetype 2") == 0);
  /* MOVETYPE 11 only in rerelease */
  qa_compat_qc_words_set_float(w, 4, 11);
  CHECK(!qa_try(motion_call, &call, &caught));
  state.options.rerelease = true;
  CHECK(qa_compat_qc_actor_state_motion(&state, actor, body, &motion) && strcmp(motion.kind, "bounce") == 0);

  qa_compat_qc_machine_destroy(&m); qa_compat_qc_entity_memory_destroy(&entities); qa_compat_qc_registry_destroy(&registry);
}

static void test_finale(void) {
  qa_compat_qc_finale_ack ack; qa_compat_qc_finale_ack_init(&ack);
  qa_actor_id a = {{NULL, NULL}, 1, 1, NULL}, b = {{NULL, NULL}, 2, 1, NULL};
  qa_actor_id one[1] = {a}, two[2] = {a, b};
  bool down[2] = {true, true}, up[1] = {false};
  CHECK(!qa_compat_qc_finale_ack_poll(&ack, 0, one, down, 1));   /* held on entry is ignored */
  CHECK(!qa_compat_qc_finale_ack_poll(&ack, 0.5, one, down, 1));
  CHECK(!qa_compat_qc_finale_ack_poll(&ack, 0.6, one, up, 1));
  CHECK(qa_compat_qc_finale_ack_poll(&ack, 0.7, one, down, 1));  /* a new press latches */
  CHECK(qa_compat_qc_finale_ack_poll(&ack, 0.8, one, up, 1));    /* and stays latched */
  CHECK(!qa_compat_qc_finale_ack_poll(&ack, 5, one, down, 1));   /* a gap over one second re-arms */
  CHECK(qa_compat_qc_finale_ack_poll(&ack, 5.1, two, down, 2));  /* a newly present pressed actor counts */
  CHECK(!qa_compat_qc_finale_ack_poll(&ack, 4, one, down, 1));   /* time going back re-arms */
  qa_compat_qc_finale_ack_dismiss(&ack, 4.5);
  CHECK(qa_compat_qc_finale_ack_poll(&ack, 4.6, one, down, 1));
  qa_compat_qc_finale_ack_reset(&ack);
  CHECK(!qa_compat_qc_finale_ack_poll(&ack, 4.7, one, down, 1));
  qa_compat_qc_finale_ack_destroy(&ack);
}

static int emitted = 0, dropped = 0;
static char dropped_text[512];
static void on_emit(void *ctx, qa_q1_event effect, const qa_actor_id *recipient) { EXPECT_CTX(ctx); (void)effect; (void)recipient; emitted++; }
static void on_dropped(void *ctx, const char *text) { EXPECT_CTX(ctx); dropped++; snprintf(dropped_text, sizeof dropped_text, "%s", text); }
static qa_owned_actor world_actor(void *ctx, int64_t slot) { EXPECT_CTX(ctx); (void)slot; qa_owned_actor a; memset(&a, 0, sizeof a); return a; }
static bool world_slot_actor(void *ctx, int64_t slot, qa_actor_id *out) { EXPECT_CTX(ctx); (void)slot; (void)out; return false; }

static void test_broadcast_flush(void) {
  qa_compat_qc_entity_memory entities; qa_compat_qc_builtin_registry registry; qa_compat_qc_machine m;
  memset(&registry, 0, sizeof registry);
  CHECK(make_machine(&entities, &registry, &m));
  qa_compat_qc_presentation_world world = {&SENTINEL, &program, &entities, NULL, 0, world_actor, world_slot_actor};
  qa_compat_qc_broadcast_messages b;
  CHECK(qa_compat_qc_broadcast_messages_init(&b, &world, &SENTINEL, on_emit, NULL, NULL));
  qa_compat_qc_broadcast_messages_set_dropped(&b, &SENTINEL, on_dropped);
  /* A well-formed broadcast temporary entity reaches the effect sink. */
  qa_msg_write_byte(&b.buffer, QA_Q1_SVC_TEMP_ENTITY); qa_msg_write_byte(&b.buffer, QA_Q1_TE_EXPLOSION);
  qa_msg_write_coord(&b.buffer, 16); qa_msg_write_coord(&b.buffer, -8); qa_msg_write_coord(&b.buffer, 4);
  qa_compat_qc_broadcast_messages_flush(&b);
  CHECK(emitted == 1 && dropped == 0 && b.buffer.cursize == 0);
  /* A non-temporary broadcast is unsupported: reported once and discarded. */
  qa_msg_write_byte(&b.buffer, QA_Q1_SVC_TEMP_ENTITY + 200);
  qa_compat_qc_broadcast_messages_flush(&b);
  CHECK(dropped == 1 && strncmp(dropped_text, "Dropped QC message: ", 20) == 0);
  CHECK(dropped_text[strlen(dropped_text) - 1] == '\n');
  CHECK(b.buffer.cursize == 0 && b.owner_count == 0);
  qa_compat_qc_broadcast_messages_flush(&b);
  CHECK(dropped == 1 && emitted == 1);
  /* Without a dropped sink the drop is silent and still clears the queue. */
  qa_compat_qc_broadcast_messages_set_dropped(&b, NULL, NULL);
  qa_msg_write_byte(&b.buffer, QA_Q1_SVC_TEMP_ENTITY + 200);
  qa_compat_qc_broadcast_messages_flush(&b);
  CHECK(dropped == 1 && b.buffer.cursize == 0);
  qa_compat_qc_broadcast_messages_destroy(&b);
  qa_compat_qc_machine_destroy(&m); qa_compat_qc_entity_memory_destroy(&entities); qa_compat_qc_registry_destroy(&registry);
}

int main(void) {
  test_actor_state();
  test_finale();
  test_broadcast_flush();
  CHECK(bad_ctx == 0);
  printf("test_qc_hosts_depth: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
