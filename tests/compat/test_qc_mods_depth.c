/*
 * Depth checks for the QuakeC mod subsystems. test_pickups reproduces
 * tests/compat/qc/mod-pickups.test.ts against the shared registry,
 * inventory, authority and original-pickup admission. The remaining
 * cases pin TS semantics read from the source: qcConsoleCall argument
 * resolution, qcDamageInputs, QcModEnvironment defaults (`??` keeps a
 * zero gravity, String(number) formatting), and QcModClientBindings'
 * frame liveness, which ends when the entry object is replaced.
 * Every callback checks a sentinel ctx. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qc.h"
#include "qa/compat/qc/mod_clients.h"
#include "qa/compat/qc/mod_combat.h"
#include "qa/compat/qc/mod_commands.h"
#include "qa/compat/qc/mod_environment.h"
#include "qa/compat/qc/mod_pickups.h"
#include "qa/foundation/support.h"
#include "qa/world/gameplay/original_pickups.h"

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

/* ---- mod-pickups.test.ts ---- */
typedef struct {
  qa_session_actor_registry *actors; qa_shared_inventory_table *inventory;
  qa_owned_actor actor, pickup; qa_client_id client;
  bool accepted, retire;
  char names[16][16]; size_t call_count;
  double pickup_count, has_count, dropped, time; bool self_ok, other_ok; char item[32];
  int fallback; bool completed[8]; size_t completed_count;
} PickupWorld;
static PickupWorld pw;
static bool pw_for_actor(void *ctx, qa_actor_id actor, qa_client_id *out) {
  EXPECT_CTX(ctx); if (!qa_same_actor(actor, pw.actor.id)) return false; *out = pw.client; return true;
}
static bool pw_actor(void *ctx, qa_client_id client, qa_actor_id *out) {
  EXPECT_CTX(ctx); if (!qa_same_client(client, pw.client)) return false; *out = pw.actor.id; return true;
}
static const qa_mod_client_identity *pw_clients(void *ctx, size_t *n) {
  EXPECT_CTX(ctx); static qa_mod_client_identity one; one = (qa_mod_client_identity){pw.client, pw.actor.id}; *n = 1; return &one;
}
static const char *pw_value_string(const qa_compat_qc_mod_input_entry *inputs, size_t n, const char *name) {
  for (size_t i = 0; i < n; i++) if (strcmp(inputs[i].name, name) == 0) return inputs[i].value.string_value;
  return NULL;
}
static const qa_mod_runtime_value *pw_value(const qa_compat_qc_mod_input_entry *inputs, size_t n, const char *name) {
  for (size_t i = 0; i < n; i++) if (strcmp(inputs[i].name, name) == 0) return &inputs[i].value;
  return NULL;
}
static double pw_invoke(void *ctx, qa_mod_source_call call, const qa_compat_qc_mod_input_entry *inputs, size_t n) {
  EXPECT_CTX(ctx);
  snprintf(pw.names[pw.call_count++ % 16], 16, "%s", call.function);
  pw.pickup_count = pw_value(inputs, n, "pickup-count")->number_value;
  pw.has_count = pw_value(inputs, n, "pickup-has-count")->number_value;
  pw.dropped = pw_value(inputs, n, "pickup-dropped")->number_value;
  pw.time = pw_value(inputs, n, "time")->number_value;
  pw.self_ok = qa_same_actor(*pw_value(inputs, n, "self")->actor_value, pw.actor.id);
  pw.other_ok = qa_same_actor(*pw_value(inputs, n, "other")->actor_value, pw.pickup.id);
  snprintf(pw.item, sizeof pw.item, "%s", pw_value_string(inputs, n, "item"));
  if (pw.retire) qa_session_actor_registry_release(pw.actors, pw.pickup);
  if (strcmp(call.function, "test_gate") == 0) return pw.accepted ? 1 : 0;
  qa_shared_inventory_table_give(pw.inventory, pw.actor, "q1:ammo/shells", 5); return 0;
}
static qa_original_pickup_decision pw_watch(void *ctx, qa_actor_id actor, qa_original_pickup_execution observer, void *run_ctx, qa_original_pickup_decision (*run)(void *)) {
  EXPECT_CTX(ctx); (void)actor; (void)observer; return run(run_ctx);
}
static bool pw_original(void *ctx) { EXPECT_CTX(ctx); pw.fallback++; return true; }
static void pw_complete(void *ctx, bool taken) { EXPECT_CTX(ctx); pw.completed[pw.completed_count++ % 8] = taken; }
static size_t pw_calls_named(const char *name) { size_t n = 0; for (size_t i = 0; i < pw.call_count && i < 16; i++) if (strcmp(pw.names[i], name) == 0) n++; return n; }
static qa_source_time pw_time(void *ctx) { EXPECT_CTX(ctx); return (qa_source_time){QA_SOURCE_TIME_SECONDS, 99}; }
typedef struct { qa_compat_qc_mod_pickups *p; qa_actor_id actor; } AdmitCall;
static void admit_call(void *ctx) { AdmitCall *c = ctx; qa_compat_qc_mod_pickups_admit(c->p, c->actor); }

static void test_pickups(void) {
  memset(&pw, 0, sizeof pw);
  qa_identity_owner ids; CHECK(qa_identity_owner_create(&ids, "qc-pickup-inputs"));
  pw.actors = qa_session_actor_registry_create(&ids, 64);
  pw.client = qa_identity_owner_client(&ids, 0, 0);
  pw.actor = qa_session_actor_registry_allocate(pw.actors, "q2:game", "q2:player");
  pw.pickup = qa_session_actor_registry_allocate(pw.actors, "q3:map", "q3:ammo_shells");
  pw.inventory = qa_shared_inventory_table_create(pw.actors);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(pw.actors);
  qa_gameplay_hooks hooks; memset(&hooks, 0, sizeof hooks);
  qa_gameplay_authority *combat = qa_gameplay_authority_create(pw.actors, callbacks, hooks);
  qa_combat_state initial; memset(&initial, 0, sizeof initial);
  initial.health = 100; initial.mass = 200; initial.can_take_damage = true;
  initial.armor.regular.kind = QA_REGULAR_ARMOR_NONE; initial.armor.powered.kind = "none";
  qa_gameplay_authority_create_state(combat, pw.actor, initial, NULL, NULL);
  qa_shared_inventory_table_create_entries(pw.inventory, pw.actor, NULL, 0);
  qa_mod_client_services clients; memset(&clients, 0, sizeof clients);
  clients.ctx = &SENTINEL; clients.maximum = 1; clients.clients = pw_clients; clients.for_actor = pw_for_actor; clients.actor = pw_actor;
  qa_mod_host_services services; memset(&services, 0, sizeof services);
  services.actors = pw.actors; services.inventory = pw.inventory; services.combat = combat; services.seed = 1;
  services.ctx = &SENTINEL; services.time = pw_time; services.clients = &clients;

  qa_mod_source_call gate = {"test_gate", NULL, 0, NULL, 0}, grant = {"test_grant", NULL, 0, NULL, 0};
  qa_pickup_write writes[1] = {{QA_PICKUP_RESOURCE_INVENTORY, QA_PROTECTION_REGULAR, "q1:ammo/shells", "count"}};
  qa_item_id offered[1] = {"q3:ammo_shells"};
  qa_mod_pickup_rule rules[1]; memset(rules, 0, sizeof rules);
  rules[0].id = "test:grant"; rules[0].writes = (qa_pickup_writes){writes, 1}; rules[0].offered = offered; rules[0].offered_count = 1;
  rules[0].operation = (qa_original_pickup_operation){true, &gate, &grant, sizeof(qa_mod_source_call), "always"};
  qa_mod_callback_declaration declaration; memset(&declaration, 0, sizeof declaration);
  declaration.pickups = rules; declaration.pickup_count = 1;
  qa_compat_qc_mod_pickups_operations ops; memset(&ops, 0, sizeof ops);
  ops.ctx = &SENTINEL; ops.invoke = pw_invoke; ops.watch = pw_watch; /* binding and identity come from services */
  qa_compat_qc_mod_pickups adapter; qa_compat_qc_mod_pickups_init(&adapter, declaration, "mod:grant", &services, ops);

  qa_pickup_resource shells = {QA_PICKUP_RESOURCE_INVENTORY, QA_PROTECTION_REGULAR, "q1:ammo/shells"};
  qa_original_pickup_offer offer; memset(&offer, 0, sizeof offer);
  offer.recipient = pw.actor.id; offer.pickup = pw.pickup.id; offer.source = "q3:map"; offer.item = "q3:ammo_shells";
  offer.default_resource = &shells; offer.count = (qa_pickup_count){false, 0}; offer.dropped = false;
  offer.time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 3250};
  qa_shared_original_pickup_admission *admission = qa_shared_original_pickup_admission_create(pw.actors, combat, pw.inventory, NULL, NULL);
  qa_original_pickup_continuation continuation = {&SENTINEL, NULL, pw_original, pw_complete};

  AdmitCall admit = {&adapter, pw.actor.id}; qa_caught_error caught;
  CHECK(!qa_try(admit_call, &admit, &caught) && strstr(caught.message, "not admitted") != NULL);
  CHECK(adapter.entry_count == 0); /* TS catch released the half-admitted entry */
  qa_shared_inventory_table_configure(pw.inventory, pw.actor, (qa_inventory_entry){"q1:ammo/shells", 2, 100, false, {NULL, NULL}}, NULL, NULL);
  qa_compat_qc_mod_pickups_admit(&adapter, pw.actor.id);
  CHECK(qa_shared_original_pickup_admission_touch(admission, offer, continuation) == QA_ORIGINAL_PICKUP_OUTCOME_REFUSED);
  CHECK(pw.call_count == 1 && strcmp(pw.names[0], "test_gate") == 0);
  CHECK(pw.pickup_count == 0 && pw.has_count == 0);
  pw.accepted = true;
  qa_original_pickup_offer counted = offer; counted.count = (qa_pickup_count){true, 12}; counted.dropped = true;
  CHECK(qa_shared_original_pickup_admission_touch(admission, counted, continuation) == QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED);
  CHECK(pw.call_count == 3 && strcmp(pw.names[1], "test_gate") == 0 && strcmp(pw.names[2], "test_grant") == 0);
  CHECK(pw.self_ok && pw.other_ok && strcmp(pw.item, "q3:ammo_shells") == 0 && pw.time == 3.25);
  CHECK(pw.pickup_count == 12 && pw.has_count == 1 && pw.dropped == 1);
  CHECK(qa_shared_inventory_table_count(pw.inventory, pw.actor.id, "q1:ammo/shells") == 7);
  CHECK(pw.fallback == 0 && pw.completed_count == 2 && !pw.completed[0] && pw.completed[1]);
  qa_original_pickup_offer other_item = offer; other_item.item = "q2:ammo_shells";
  CHECK(qa_shared_original_pickup_admission_touch(admission, other_item, continuation) == QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED);
  CHECK(pw.fallback == 1);
  qa_original_pickup_resolution resolved = qa_shared_inventory_table_resolve_pickup(pw.inventory, pw.actor, offer);
  CHECK(resolved.match_count == 1 && resolved.matches[0].current(resolved.matches[0].ctx));
  qa_compat_qc_mod_pickups_release(&adapter, pw.actor.id);
  CHECK(qa_shared_inventory_table_resolve_pickup(pw.inventory, pw.actor, offer).match_count == 0);
  CHECK(qa_shared_inventory_table_count(pw.inventory, pw.actor.id, "q1:ammo/shells") == 7);
  qa_compat_qc_mod_pickups_admit(&adapter, pw.actor.id); pw.retire = true;
  CHECK(qa_shared_original_pickup_admission_touch(admission, offer, continuation) == QA_ORIGINAL_PICKUP_OUTCOME_STALE);
  CHECK(strcmp(pw.names[(pw.call_count - 1) % 16], "test_gate") == 0);
  CHECK(pw_calls_named("test_grant") == 1 && pw.completed_count == 3 && pw.completed[2]);
  qa_compat_qc_mod_pickups_close(&adapter);
  CHECK(qa_shared_inventory_table_resolve_pickup(pw.inventory, pw.actor, offer).match_count == 0);
  qa_compat_qc_mod_pickups_close(&adapter);
  qa_compat_qc_mod_pickups_destroy(&adapter);
  qa_shared_original_pickup_admission_destroy(admission);
}

/* ---- mod-commands.ts qcConsoleCall ---- */
typedef struct { qa_mod_console_command command; } ConsoleCall;
static void console_bad_index(void *ctx) {
  ConsoleCall *c = ctx; qa_mod_source_call out; qa_mod_callback_value *args; qa_mod_named_value *globals;
  const char *argv[1] = {"x"};
  if (qa_compat_qc_console_call(c->command, argv, 1, "", &out, &args, &globals)) qa_compat_qc_console_call_destroy(args, globals);
}
static void test_commands(void) {
  qa_mod_console_value arguments[5]; memset(arguments, 0, sizeof arguments);
  arguments[0] = (qa_mod_console_value){.kind = "argument", .index = 1};                  /* float via native atof */
  arguments[1] = (qa_mod_console_value){.kind = "argument", .index = 1, .type = "string"};
  arguments[2] = (qa_mod_console_value){.kind = "argument", .index = 7, .type = "string"}; /* absent: "" */
  arguments[3] = (qa_mod_console_value){.kind = "argument-count"};
  arguments[4] = (qa_mod_console_value){.kind = "float", .number_value = 3.5};             /* passes through */
  qa_mod_console_named_value globals[1] = {{"mode", {.kind = "arguments-text"}}};
  qa_mod_console_command command = {"cmd", "Cmd_Fn", arguments, 5, globals, 1};
  const char *argv[2] = {"cmd", "12.5xyz"};
  qa_mod_source_call call; qa_mod_callback_value *args = NULL; qa_mod_named_value *out_globals = NULL;
  CHECK(qa_compat_qc_console_call(command, argv, 2, "12.5xyz rest", &call, &args, &out_globals));
  CHECK(strcmp(call.function, "Cmd_Fn") == 0 && call.argument_count == 5 && call.global_count == 1);
  CHECK(strcmp(call.arguments[0].kind, "float") == 0 && call.arguments[0].number_value == 12.5);
  CHECK(strcmp(call.arguments[1].kind, "string") == 0 && strcmp(call.arguments[1].string_value, "12.5xyz") == 0);
  CHECK(strcmp(call.arguments[2].string_value, "") == 0);
  CHECK(strcmp(call.arguments[3].kind, "float") == 0 && call.arguments[3].number_value == 2);
  CHECK(strcmp(call.arguments[4].kind, "float") == 0 && call.arguments[4].number_value == 3.5);
  CHECK(strcmp(call.globals[0].name, "mode") == 0 && strcmp(call.globals[0].value.string_value, "12.5xyz rest") == 0);
  qa_compat_qc_console_call_destroy(args, out_globals);
  qa_mod_console_value bad[2] = {{.kind = "argument", .index = -1}, {.kind = "argument", .index = 1.5}};
  for (int i = 0; i < 2; i++) {
    ConsoleCall c = {{"cmd", "Cmd_Fn", &bad[i], 1, NULL, 0}}; qa_caught_error caught;
    CHECK(!qa_try(console_bad_index, &c, &caught) && strcmp(caught.message, "Mod console argument index must be nonnegative") == 0);
  }
}

/* ---- mod-combat.ts qcDamageInputs ---- */
static void test_damage_inputs(void) {
  qa_actor_id target = {{NULL, NULL}, 4, 1, NULL}, attacker = {{NULL, NULL}, 5, 2, NULL};
  qa_damage_request r; memset(&r, 0, sizeof r);
  r.target = target; r.amount = 25; r.knockback = 9; r.point = (qa_vec3){1, 2, 3}; r.direction = (qa_vec3){0, 0, 1}; r.normal = (qa_vec3){0, 1, 0};
  r.attack.attacker = &attacker; r.attack.inflictor = NULL;
  qa_compat_qc_mod_input_entry inputs[9];
  CHECK(qa_compat_qc_damage_inputs(r, 4.5, inputs, 9) == 9);
  static const char *const names[9] = {"self", "attacker", "inflictor", "amount", "knockback", "point", "direction", "normal", "time"};
  for (int i = 0; i < 9; i++) CHECK(strcmp(inputs[i].name, names[i]) == 0);
  CHECK(qa_same_actor(*inputs[0].value.actor_value, target) && qa_same_actor(*inputs[1].value.actor_value, attacker));
  CHECK(inputs[2].value.actor_value == NULL && strcmp(inputs[2].value.kind, "actor") == 0);
  CHECK(inputs[3].value.number_value == 25 && inputs[4].value.number_value == 9 && inputs[8].value.number_value == 4.5);
  CHECK(inputs[5].value.vector_value.z == 3 && inputs[6].value.vector_value.z == 1 && inputs[7].value.vector_value.y == 1);
}

/* ---- mod-environment.ts defaults ---- */
static void env_print(void *ctx, const char *text) { EXPECT_CTX(ctx); (void)text; }
static int64_t env_reference(void *ctx, const qa_actor_id *actor) { EXPECT_CTX(ctx); (void)actor; return 0; }
static char *cvar_text(qa_compat_qc_mod_environment *e, const char *name) { return qa_cvar_registry_variable_string(e->cvars, name); }
static bool cvar_is(qa_compat_qc_mod_environment *e, const char *name, const char *want) {
  char *text = cvar_text(e, name); bool ok = text && strcmp(text, want) == 0;
  if (!ok) fprintf(stderr, "cvar %s = %s, want %s\n", name, text ? text : "(null)", want);
  free(text); return ok;
}
static void test_environment(void) {
  qa_identity_owner ids; CHECK(qa_identity_owner_create(&ids, "qc-environment"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&ids, 16);
  qa_session_mod_engine_services engine; memset(&engine, 0, sizeof engine);
  engine.ctx = &SENTINEL; engine.print = env_print; engine.has_environment = true;
  engine.environment_skill = 1.5; engine.environment_mode = "coop"; engine.environment_max_clients = 16; engine.environment_gravity = 0;
  qa_mod_host_services services; memset(&services, 0, sizeof services);
  services.actors = actors; services.engine = &engine; services.ctx = &SENTINEL;
  qa_compat_qc_program program; memset(&program, 0, sizeof program);
  program.api.kind = QA_QUAKEC_API_Q1_QUAKEWORLD;
  qa_mod_named_string cvars[1] = {{"teamplay", "2"}};
  qa_mod_callback_declaration declaration; memset(&declaration, 0, sizeof declaration);
  declaration.cvars = cvars; declaration.cvar_count = 1;
  qa_compat_qc_mod_environment env;
  CHECK(qa_compat_qc_mod_environment_init(&env, &services, &program, declaration, &SENTINEL, env_reference, NULL));
  CHECK(cvar_is(&env, "sv_gravity", "0"));   /* environment?.gravity ?? 800 keeps zero */
  CHECK(cvar_is(&env, "skill", "1.5"));
  CHECK(cvar_is(&env, "maxclients", "16"));
  CHECK(cvar_is(&env, "coop", "1") && cvar_is(&env, "deathmatch", "0"));
  CHECK(cvar_is(&env, "sv_aim", "2") && cvar_is(&env, "sv_phs", "1"));
  CHECK(cvar_is(&env, "teamplay", "2"));      /* declared value overrides the default */
  CHECK(env.visibility == NULL);
  qa_compat_qc_mod_environment_destroy(&env);
}

/* ---- mod-clients.ts frame liveness ---- */
typedef struct { qa_actor_id actor; qa_client_id client; bool connected; int invokes; qa_compat_qc_mod_client_bindings *b; } ClientWorld;
static ClientWorld cw;
static bool cw_for_actor(void *ctx, qa_actor_id actor, qa_client_id *out) { EXPECT_CTX(ctx); if (!cw.connected || !qa_same_actor(actor, cw.actor)) return false; *out = cw.client; return true; }
static bool cw_actor(void *ctx, qa_client_id client, qa_actor_id *out) { EXPECT_CTX(ctx); if (!qa_same_client(client, cw.client)) return false; *out = cw.actor; return true; }
static const char *cw_userinfo(void *ctx, qa_client_id client) { EXPECT_CTX(ctx); (void)client; return "\\name\\ranger\\team\\red"; }
static char cw_set[128];
static void cw_set_userinfo(void *ctx, qa_client_id client, const char *value) { EXPECT_CTX(ctx); (void)client; snprintf(cw_set, sizeof cw_set, "%s", value); }
static void cw_project(void *ctx, qa_actor_id actor) { EXPECT_CTX(ctx); (void)actor; }
static const char *cw_release(void *ctx, qa_actor_id actor) { EXPECT_CTX(ctx); (void)actor; return "released"; }
static void cw_invoke(void *ctx, qa_mod_source_call call, qa_actor_id actor, const qa_frame_context *frame) {
  EXPECT_CTX(ctx); (void)call; (void)frame; cw.invokes++;
  /* userinfo() runs require(), which replaces the clientless restored entry */
  char *name = qa_compat_qc_mod_client_bindings_userinfo(cw.b, actor, "name");
  if (!name || strcmp(name, "ranger") != 0) failures++;
  free(name);
}
static void test_client_frames(void) {
  memset(&cw, 0, sizeof cw);
  qa_identity_owner ids; CHECK(qa_identity_owner_create(&ids, "qc-clients"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&ids, 16);
  cw.actor = qa_session_actor_registry_allocate(actors, "q1:game", "q1:player").id;
  cw.client = qa_identity_owner_client(&ids, 0, 0);
  qa_mod_client_services services; memset(&services, 0, sizeof services);
  services.ctx = &SENTINEL; services.maximum = 4; services.for_actor = cw_for_actor; services.actor = cw_actor;
  services.userinfo = cw_userinfo; services.set_userinfo = cw_set_userinfo;
  qa_mod_source_call frames[2] = {{"Frame_A", NULL, 0, NULL, 0}, {"Frame_B", NULL, 0, NULL, 0}};
  qa_mod_qc_clients declaration; memset(&declaration, 0, sizeof declaration);
  declaration.maximum = 4; declaration.frame = frames; declaration.frame_count = 2;
  qa_compat_qc_mod_client_operations ops; memset(&ops, 0, sizeof ops);
  ops.ctx = &SENTINEL; ops.services = &services; ops.declaration = &declaration;
  ops.project = cw_project; ops.release = cw_release; ops.invoke = cw_invoke;
  qa_compat_qc_mod_client_bindings b; qa_compat_qc_mod_client_bindings_init(&b, ops); cw.b = &b;
  qa_compat_qc_mod_client_slot saved[1] = {{cw.actor, 2, true}};
  qa_compat_qc_mod_client_bindings_restore(&b, saved, 1); /* restored before the client reconnects: client null */
  cw.connected = true;
  qa_frame_context frame; memset(&frame, 0, sizeof frame);
  CHECK(!qa_compat_qc_mod_client_bindings_frame(&b, 1, frame)); /* no entry in slot 1 */
  CHECK(qa_compat_qc_mod_client_bindings_frame(&b, 2, frame));
  CHECK(cw.invokes == 1); /* the replaced entry ends the frame before Frame_B */
  CHECK(qa_compat_qc_mod_client_bindings_frame(&b, 2, frame));
  CHECK(cw.invokes == 3); /* the current entry runs both calls */
  qa_compat_qc_mod_client_bindings_set_userinfo(&b, cw.actor, "team", "blue");
  CHECK(strcmp(cw_set, "\\name\\ranger\\team\\blue") == 0);
  qa_compat_qc_mod_client_bindings_set_userinfo(&b, cw.actor, "name", "");
  CHECK(strcmp(cw_set, "\\team\\red") == 0); /* empty value removes the key */
  qa_compat_qc_mod_client_slot checkpoint[2];
  CHECK(qa_compat_qc_mod_client_bindings_checkpoint(&b, checkpoint, 2) == 1 && checkpoint[0].slot == 2 && checkpoint[0].admitted);
  qa_compat_qc_mod_client_bindings_close(&b);
  CHECK(!qa_compat_qc_mod_client_bindings_frame(&b, 2, frame));
  qa_compat_qc_mod_client_bindings_destroy(&b);
}

int main(void) {
  test_pickups();
  test_commands();
  test_damage_inputs();
  test_environment();
  test_client_frames();
  CHECK(bad_ctx == 0);
  printf("test_qc_mods_depth: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
