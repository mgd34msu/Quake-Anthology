/*
 * C checks for app/bootstrap/simulation mod-callbacks, mod-client-checkpoint,
 * equipment-runtime (+ equipment-checkpoint reader), grapple-runtime,
 * grapple-checkpoint and the qvm-grapple-source checkpoint reader. No TS test
 * imports these modules directly, so the expected values were produced by
 * running the TS classes under bun on the same inputs (scripts kept outside
 * the repository; outputs quoted next to each check). Every callback checks
 * its sentinel ctx. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#include "qa/app/bootstrap/simulation/equipment_checkpoint.h"
#include "qa/app/bootstrap/simulation/equipment_runtime.h"
#include "qa/app/bootstrap/simulation/grapple_checkpoint.h"
#include "qa/app/bootstrap/simulation/grapple_runtime.h"
#include "qa/app/bootstrap/simulation/mod_callbacks.h"
#include "qa/app/bootstrap/simulation/mod_client_checkpoint.h"
#include "qa/app/bootstrap/simulation/qvm_grapple_source.h"
#include "qa/content/q2/equipment/ctf_grapple.h"
#include "qa/content/q2/equipment/hand_grenades.h"
#include "qa/content/q2/foundation/entity_services.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/foundation/support.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/session/resources.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)
static int MOD_CTX, CLIENT_CTX, HOST_CTX, WEAPON_CTX, LINK_CTX, HOOK_CTX, SLOT_CTX, PRESENT_CTX, PROJECT_CTX;
#define EXPECT_CTX(ctx, sentinel) CHECK((ctx) == &(sentinel))
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* Runs fn under qa_try; returns true when it raised and the message holds `expected`. */
static bool raises(void (*fn)(void *), void *ctx, const char *expected) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) return false;
  if (strstr(caught.message, expected) == NULL) {
    fprintf(stderr, "  raised: %s\n", caught.message);
    return false;
  }
  return true;
}

/* ------------------------------------------------------------------ mod callbacks */
typedef struct logged { char id[16]; qa_app_sim_mod_callback_inputs inputs; } logged;
static logged mod_log[16];
static size_t mod_log_count;
static qa_source_time mod_time(void *ctx) {
  EXPECT_CTX(ctx, MOD_CTX);
  qa_source_time t = {QA_SOURCE_TIME_MILLISECONDS, 2500};
  return t;
}
static bool mod_execute(void *ctx, const void *callback, const qa_app_sim_mod_callback_inputs *inputs, double *out) {
  EXPECT_CTX(ctx, MOD_CTX);
  const qa_mod_callback_binding *binding = callback;
  logged *entry = &mod_log[mod_log_count++];
  snprintf(entry->id, sizeof entry->id, "%s", binding->id);
  entry->inputs = *inputs;
  for (size_t i = 0; i < inputs->count; i++)
    if (inputs->values[i].actor_value != NULL) entry->inputs.values[i].actor_value = &entry->inputs.actors[i];
  static const struct { const char *id; bool has; double value; } results[] = {
      {"m:dmg", true, 12}, {"m:obs", true, 99}, {"m:give", false, 0}, {"m:con", true, 5}, {"m:think", false, 0},
      {"m:use", true, 0}, {"m:pain", true, 3}, {"m:die", true, 1}, {"m:kb", true, 8}};
  for (size_t i = 0; i < sizeof results / sizeof results[0]; i++)
    if (is(results[i].id, binding->id)) { *out = results[i].value; return results[i].has; }
  return false;
}
static const logged *log_for(const char *id) {
  for (size_t i = 0; i < mod_log_count; i++) if (is(mod_log[i].id, id)) return &mod_log[i];
  return NULL;
}
/* Checks one logged input: name at position, kind, and the scalar/actor value. */
static bool input_is(const logged *entry, size_t index, const char *name, const char *kind, double number,
                     int64_t slot, int64_t generation, bool null_actor) {
  if (entry == NULL || index >= entry->inputs.count) return false;
  const qa_mod_runtime_value *v = &entry->inputs.values[index];
  if (!is(entry->inputs.names[index], name) || !is(v->kind, kind)) return false;
  if (is(kind, "float")) return v->number_value == number;
  if (is(kind, "actor")) {
    if (null_actor) return v->actor_value == NULL;
    return v->actor_value != NULL && v->actor_value->slot == slot && v->actor_value->generation == generation;
  }
  return true;
}
typedef struct canonical_ctx { int calls; } canonical_ctx;
static void canonical_damage(void *ctx, const void *request, void *out) {
  (void)ctx; (void)request;
  qa_damage_outcome outcome;
  memset(&outcome, 0, sizeof outcome);
  outcome.kind = "committed";
  outcome.decision.applied_damage = 7;
  *(qa_damage_outcome *)out = outcome;
}
static qa_damage_request last_damage;
static void canonical_capture_damage(void *ctx, const void *request, void *out) {
  last_damage = *(const qa_damage_request *)request;
  canonical_damage(ctx, request, out);
}
static double last_give;
static void canonical_give(void *ctx, const void *request, void *out) {
  (void)ctx;
  last_give = ((const qa_inventory_give_request *)request)->count;
  *(double *)out = last_give;
}
static void canonical_true(void *ctx, const void *request, void *out) {
  (void)request;
  ((canonical_ctx *)ctx)->calls++;
  *(bool *)out = true;
}
static void test_mod_callbacks(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "mod-callbacks"));
  qa_actor_id a = qa_identity_owner_actor(&owner, 1, 1), b = qa_identity_owner_actor(&owner, 2, 3);
  qa_mod_operation damage, give, consume, think, touch, use, pain, die;
  CHECK(qa_mod_operation_init(&damage, "damage", sizeof(qa_damage_request), sizeof(qa_damage_outcome)));
  CHECK(qa_mod_operation_init(&give, "inventory.give", sizeof(qa_inventory_give_request), sizeof(double)));
  CHECK(qa_mod_operation_init(&consume, "inventory.consume", sizeof(qa_inventory_consume_request), sizeof(bool)));
  CHECK(qa_mod_operation_init(&think, "actor.think", sizeof(qa_actor_think_request), sizeof(bool)));
  CHECK(qa_mod_operation_init(&touch, "actor.touch", sizeof(qa_actor_touch_request), sizeof(bool)));
  CHECK(qa_mod_operation_init(&use, "actor.use", sizeof(qa_actor_use_request), sizeof(bool)));
  CHECK(qa_mod_operation_init(&pain, "actor.pain", sizeof(qa_actor_pain_request), sizeof(bool)));
  CHECK(qa_mod_operation_init(&die, "actor.die", sizeof(qa_actor_die_request), sizeof(bool)));
  qa_mod_registrations registrations;
  memset(&registrations, 0, sizeof registrations);
  registrations.instance = "mod:test";
  registrations.order = 0;
  registrations.resources = qa_resource_scope_create("mod-callbacks");
  registrations.operations.damage = &damage;
  registrations.operations.inventory_give = &give;
  registrations.operations.inventory_consume = &consume;
  registrations.operations.actor_think = &think;
  registrations.operations.actor_touch = &touch;
  registrations.operations.actor_use = &use;
  registrations.operations.actor_pain = &pain;
  registrations.operations.actor_die = &die;
  static const qa_mod_callback_binding bindings[] = {
      {"m:dmg", "damage", "transform", "amount"}, {"m:kb", "damage", "transform", "knockback"},
      {"m:obs", "damage", "observe", NULL}, {"m:give", "inventory.give", "transform", "amount"},
      {"m:con", "inventory.consume", "observe", NULL}, {"m:think", "actor.think", "replace", "boolean"},
      {"m:use", "actor.use", "replace", "boolean"}, {"m:pain", "actor.pain", "observe", NULL},
      {"m:die", "actor.die", "observe", NULL}, {"m:touchT", "actor.touch", "transform", NULL}};
  qa_app_sim_mod_callback_host host = {&MOD_CTX, mod_time, mod_execute};
  qa_app_sim_register_mod_callbacks(bindings, sizeof bindings / sizeof bindings[0], sizeof bindings[0], 0, &registrations, host);
  /* TS: touch "transform" registers nothing. */
  CHECK(damage.entry_count == 3 && give.entry_count == 1 && consume.entry_count == 1 && think.entry_count == 1);
  CHECK(touch.entry_count == 0 && use.entry_count == 1 && pain.entry_count == 1 && die.entry_count == 1);

  qa_damage_request request;
  memset(&request, 0, sizeof request);
  request.target = a; request.attack.attacker = &b; request.attack.inflictor = NULL;
  request.amount = 30; request.knockback = 10;
  request.direction = (qa_vec3){1, 0, 0}; request.point = (qa_vec3){2, 3, 4}; request.normal = (qa_vec3){0, 0, 1};
  request.delivery = "direct";
  canonical_ctx canon = {0};
  qa_mod_canonical damage_canonical = {&canon, canonical_capture_damage};
  qa_damage_outcome outcome;
  qa_mod_operation_dispatch(&damage, &request, damage_canonical, NULL, &outcome);
  /* bun (each transform alone): m:dmg -> {amount 12, knockback 10}; m:kb -> {amount 30, knockback 8}.
   * Composed in registration order they give amount 12, knockback 8. */
  CHECK(last_damage.amount == 12 && last_damage.knockback == 8);
  const logged *dmg = log_for("m:dmg");
  /* bun: [time float 2.5][self 1:1][attacker 2:3][inflictor null][amount 30][knockback 10][direction][point][normal] */
  CHECK(dmg != NULL && dmg->inputs.count == 9);
  CHECK(input_is(dmg, 0, "time", "float", 2.5, 0, 0, false));
  CHECK(input_is(dmg, 1, "self", "actor", 0, 1, 1, false));
  CHECK(input_is(dmg, 2, "attacker", "actor", 0, 2, 3, false));
  CHECK(input_is(dmg, 3, "inflictor", "actor", 0, 0, 0, true));
  CHECK(input_is(dmg, 4, "amount", "float", 30, 0, 0, false));
  CHECK(input_is(dmg, 5, "knockback", "float", 10, 0, 0, false));
  CHECK(dmg != NULL && is(dmg->inputs.names[7], "point") && dmg->inputs.values[7].vector_value.z == 4);
  const logged *obs = log_for("m:obs");
  /* bun: observe adds [result float 7] after the request inputs. */
  CHECK(obs != NULL && obs->inputs.count == 10 && input_is(obs, 9, "result", "float", 7, 0, 0, false));
  CHECK(obs != NULL && input_is(obs, 4, "amount", "float", 12, 0, 0, false));

  qa_owned_actor owned_a = qa_identity_owner_owned_actor(&owner, a, "test:a");
  qa_inventory_give_request gift = {owned_a, "q1:shells", 4};
  qa_mod_canonical give_canonical = {&canon, canonical_give};
  double given = 0;
  qa_mod_operation_dispatch(&give, &gift, give_canonical, NULL, &given);
  /* bun: m:give returns null -> amount unchanged (4). */
  CHECK(last_give == 4 && given == 4);
  const logged *gl = log_for("m:give");
  CHECK(gl != NULL && gl->inputs.count == 4 && is(gl->inputs.names[2], "item") && is(gl->inputs.values[2].kind, "string")
        && is(gl->inputs.values[2].string_value, "q1:shells") && input_is(gl, 3, "amount", "float", 4, 0, 0, false));
  qa_inventory_consume_request take = {owned_a, "q1:shells", 2};
  qa_mod_canonical consume_canonical = {&canon, canonical_true};
  bool consumed = false;
  qa_mod_operation_dispatch(&consume, &take, consume_canonical, NULL, &consumed);
  const logged *cl = log_for("m:con");
  CHECK(consumed && cl != NULL && cl->inputs.count == 5 && input_is(cl, 4, "result", "float", 1, 0, 0, false));

  qa_actor_think_request thinking;
  memset(&thinking, 0, sizeof thinking);
  thinking.self = owned_a;
  thinking.frame.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 4};
  thinking.frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 100};
  canon.calls = 0;
  bool thought = false;
  qa_mod_operation_dispatch(&think, &thinking, consume_canonical, NULL, &thought);
  /* bun: m:think null -> next(request) runs; inputs [time 4][self][elapsed 0.1]. */
  CHECK(canon.calls == 1 && thought);
  const logged *tl = log_for("m:think");
  CHECK(tl != NULL && tl->inputs.count == 3 && input_is(tl, 0, "time", "float", 4, 0, 0, false)
        && input_is(tl, 1, "self", "actor", 0, 1, 1, false) && input_is(tl, 2, "elapsed", "float", 0.1, 0, 0, false));
  qa_actor_use_request using;
  memset(&using, 0, sizeof using);
  using.self = owned_a; using.has_activator = true; using.activator = b;
  canon.calls = 0;
  bool used = true;
  qa_mod_operation_dispatch(&use, &using, consume_canonical, NULL, &used);
  /* bun: m:use returns 0 -> false without next; other null, activator 2:3. */
  CHECK(canon.calls == 0 && !used);
  const logged *ul = log_for("m:use");
  CHECK(ul != NULL && ul->inputs.count == 4 && input_is(ul, 2, "other", "actor", 0, 0, 0, true)
        && input_is(ul, 3, "activator", "actor", 0, 2, 3, false));
  qa_actor_pain_request hurt;
  memset(&hurt, 0, sizeof hurt);
  hurt.reaction.self = owned_a; hurt.reaction.attacker = &b; hurt.reaction.damage = 6; hurt.reaction.kick = 2;
  bool pained = false;
  qa_mod_canonical false_canonical = {&canon, canonical_true};
  qa_mod_operation_dispatch(&pain, &hurt, false_canonical, NULL, &pained);
  const logged *pl = log_for("m:pain");
  /* bun: [time][self][attacker 2:3][amount 6][knockback 2][result] (result 0 for false in bun; here canonical true -> 1). */
  CHECK(pl != NULL && pl->inputs.count == 6 && input_is(pl, 3, "amount", "float", 6, 0, 0, false)
        && input_is(pl, 4, "knockback", "float", 2, 0, 0, false) && input_is(pl, 5, "result", "float", 1, 0, 0, false));
  qa_actor_die_request death;
  memset(&death, 0, sizeof death);
  death.reaction.pain.self = owned_a; death.reaction.pain.damage = 9; death.reaction.pain.kick = 1;
  death.reaction.inflictor = &b; death.reaction.point = (qa_vec3){5, 6, 7};
  bool died = false;
  qa_mod_operation_dispatch(&die, &death, false_canonical, NULL, &died);
  const logged *dl = log_for("m:die");
  /* bun: [time][self][attacker null][inflictor 2:3][amount 9][knockback 1][point][result 1]. */
  CHECK(dl != NULL && dl->inputs.count == 8 && input_is(dl, 2, "attacker", "actor", 0, 0, 0, true)
        && input_is(dl, 3, "inflictor", "actor", 0, 2, 3, false) && dl->inputs.values[6].vector_value.y == 6
        && input_is(dl, 7, "result", "float", 1, 0, 0, false));
  qa_resource_scope_close(registrations.resources);
  CHECK(damage.entry_count == 0 && give.entry_count == 0);
  qa_resource_scope_destroy(registrations.resources);
  qa_mod_operation_destroy(&damage); qa_mod_operation_destroy(&give); qa_mod_operation_destroy(&consume);
  qa_mod_operation_destroy(&think); qa_mod_operation_destroy(&touch); qa_mod_operation_destroy(&use);
  qa_mod_operation_destroy(&pain); qa_mod_operation_destroy(&die);
  qa_identity_owner_destroy(&owner);
}

/* ------------------------------------------------------------------ mod client commands */
typedef struct client_host { qa_identity_owner *owner; qa_client_id clients[2]; int64_t slots[2]; size_t count; } client_host;
static client_host *CH;
static qa_actor_id ch_actor(void *ctx, qa_saved_actor_id saved) {
  EXPECT_CTX(ctx, CLIENT_CTX);
  return qa_identity_owner_actor(CH->owner, (int64_t)saved.slot, (int64_t)saved.generation);
}
static bool ch_client(void *ctx, qa_actor_id actor, qa_client_id *out) {
  EXPECT_CTX(ctx, CLIENT_CTX);
  for (size_t i = 0; i < CH->count; i++) if (CH->slots[i] == actor.slot) { *out = CH->clients[i]; return true; }
  return false;
}
typedef struct read_job { const qa_save_value *value; qa_app_sim_mod_client_command_host host; } read_job;
static void read_commands_job(void *ctx) {
  read_job *job = ctx;
  qa_save_reader reader;
  CHECK(qa_save_reader_init(&reader, job->value, "c"));
  qa_app_sim_mod_client_commands commands = qa_app_sim_read_mod_client_commands(&reader, job->host);
  qa_app_sim_mod_client_commands_free(&commands);
  qa_save_reader_destroy(&reader);
}
static qa_save_value *list_of(qa_save_value *first, qa_save_value *second) {
  qa_save_value *list = qa_save_value_array();
  CHECK(qa_save_value_push(list, first));
  if (second != NULL) CHECK(qa_save_value_push(list, second));
  return list;
}
static qa_save_value *clone(const qa_save_value *value) {
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  qa_save_value *copy = qa_decode_checkpoint_value(bytes, length);
  free(bytes);
  return copy;
}
static void test_mod_client_checkpoint(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "mc"));
  qa_actor_id a = qa_identity_owner_actor(&owner, 3, 2), b = qa_identity_owner_actor(&owner, 4, 1);
  qa_client_id client = qa_identity_owner_client(&owner, 1, 5), client_b = qa_identity_owner_client(&owner, 2, 0);
  qa_mod_client_command_receipt cmd_a, cmd_b;
  memset(&cmd_a, 0, sizeof cmd_a);
  memset(&cmd_b, 0, sizeof cmd_b);
  cmd_a.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 1.5};
  cmd_a.input.actor = a;
  cmd_a.input.source.kind = QA_COMMAND_SOURCE_LOCAL_SEAT;
  cmd_a.input.source.client = client;
  cmd_a.input.source.seat = qa_identity_owner_seat(&owner, 0);
  cmd_a.input.sequence = 7;
  cmd_a.input.command.kind = QA_USER_COMMAND_Q2;
  qa_q2_user_command q2 = {16, {{1, -2, 3}}, 200, -50, 0, 1, 0, 64};
  cmd_a.input.command.as.q2 = q2;
  qa_arsenal_intent arsenal = {"q2:weapons", "q2:weapon_shotgun", false, true, 3};
  cmd_a.input.arsenal = &arsenal;
  cmd_b.time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 900};
  cmd_b.input.actor = b;
  cmd_b.input.source.kind = QA_COMMAND_SOURCE_BOT;
  cmd_b.input.source.provider = "q3:bots";
  cmd_b.input.sequence = 2;
  cmd_b.input.command.kind = QA_USER_COMMAND_Q3;
  qa_q3_user_command q3 = {900, {{10, 20, 30}}, 2, 5, 127, -127, 0};
  cmd_b.input.command.as.q3 = q3;
  qa_save_value *saved_a = qa_app_sim_capture_mod_client_command(&cmd_a, client);
  qa_save_value *saved_b = qa_app_sim_capture_mod_client_command(&cmd_b, client_b);
  /* bun capture keys: actor,client,time,source,sequence,command,arsenal. */
  CHECK(saved_a->entry_count == 7 && is(saved_a->entries[0].key, "actor") && is(saved_a->entries[6].key, "arsenal"));
  const qa_save_value *source = qa_save_value_get(saved_a, "source");
  CHECK(source != NULL && is(qa_save_value_get(source, "kind")->text, "local-seat") && qa_save_value_get(source, "seat")->number == 0);
  const qa_save_value *command = qa_save_value_get(saved_a, "command");
  /* bun: {"kind":"q2-classic","milliseconds":16,"angleShorts":[1,-2,3],...,"lightLevel":64} */
  CHECK(command != NULL && command->entry_count == 9 && is(command->entries[2].key, "angleShorts")
        && command->entries[2].value->items[1]->number == -2 && is(command->entries[8].key, "lightLevel"));
  CHECK(qa_save_value_get(qa_save_value_get(saved_a, "arsenal"), "impulse")->number == 3);
  CHECK(qa_save_value_get(saved_b, "arsenal")->kind == QA_SAVE_NULL);
  CHECK(is(qa_save_value_get(qa_save_value_get(saved_b, "source"), "provider")->text, "q3:bots"));

  client_host ch = {&owner, {client, client_b}, {3, 4}, 2};
  CH = &ch;
  qa_app_sim_mod_client_command_host host = {&owner, &CLIENT_CTX, ch_actor, ch_client};
  qa_save_value *list = list_of(saved_a, saved_b);
  qa_save_reader reader;
  CHECK(qa_save_reader_init(&reader, list, "c"));
  qa_app_sim_mod_client_commands commands = qa_app_sim_read_mod_client_commands(&reader, host);
  qa_save_reader_destroy(&reader);
  CHECK(commands.count == 2);
  const qa_mod_client_command_receipt *ra = &commands.commands[0], *rb = &commands.commands[1];
  CHECK(ra->time.kind == QA_SOURCE_TIME_SECONDS && ra->time.value == 1.5 && qa_same_actor(ra->input.actor, a));
  CHECK(ra->input.source.kind == QA_COMMAND_SOURCE_LOCAL_SEAT && ra->input.source.seat.index == 0 && ra->input.source.client.slot == 1);
  CHECK(ra->input.sequence == 7 && is(ra->input.angle_space, "absolute") && ra->input.command.kind == QA_USER_COMMAND_Q2);
  CHECK(ra->input.command.as.q2.angle_shorts.values[2] == 3 && ra->input.command.as.q2.light_level == 64 && ra->input.command.as.q2.side_move == -50);
  CHECK(ra->input.arsenal != NULL && is(ra->input.arsenal->weapon, "q2:weapon_shotgun") && ra->input.arsenal->has_impulse && ra->input.arsenal->impulse == 3);
  CHECK(rb->time.kind == QA_SOURCE_TIME_MILLISECONDS && rb->time.value == 900 && rb->input.source.kind == QA_COMMAND_SOURCE_BOT);
  CHECK(is(rb->input.source.provider, "q3:bots") && rb->input.arsenal == NULL && rb->input.command.as.q3.right_move == -127);
  CHECK(rb->input.command.as.q3.angle_words.values[1] == 20 && rb->input.command.as.q3.weapon == 5);
  qa_app_sim_mod_client_commands_free(&commands);

  /* bun (canonical host actors): "c[1]: Saved command requires one live restored client" */
  qa_save_value *dup = list_of(clone(saved_a), clone(saved_a));
  read_job job = {dup, host};
  CHECK(raises(read_commands_job, &job, "c[1]: Saved command requires one live restored client"));
  /* bun: slot mismatch -> "c[0]: Saved command requires one live restored client" */
  client_host wrong = {&owner, {client_b}, {3}, 1};
  CH = &wrong;
  qa_save_value *single = list_of(clone(saved_a), NULL);
  job.value = single;
  CHECK(raises(read_commands_job, &job, "c[0]: Saved command requires one live restored client"));
  CH = &ch;
  /* bun: "c[0].command.angleWords: Expected three command angle words" */
  qa_save_value *bad = clone(saved_b);
  qa_save_value *words = (qa_save_value *)qa_save_value_get(qa_save_value_get(bad, "command"), "angleWords");
  qa_save_value_free(words->items[2]);
  words->item_count = 2;
  qa_save_value *bad_list = list_of(bad, NULL);
  job.value = bad_list;
  CHECK(raises(read_commands_job, &job, "c[0].command.angleWords: Expected three command angle words"));
  /* bun: undefined reader -> [] */
  qa_save_reader undefined_reader;
  CHECK(qa_save_reader_init(&undefined_reader, NULL, "c"));
  commands = qa_app_sim_read_mod_client_commands(&undefined_reader, host);
  CHECK(commands.count == 0);
  qa_app_sim_mod_client_commands_free(&commands);
  qa_save_reader_destroy(&undefined_reader);
  qa_save_value_free(list); qa_save_value_free(dup); qa_save_value_free(single); qa_save_value_free(bad_list);
  qa_identity_owner_destroy(&owner);
}

/* ------------------------------------------------------------------ Q2 world fixture */
typedef struct fixture {
  double now;
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_actor_callback_table *callbacks;
  qa_shared_body_table *bodies;
  qa_shared_inventory_table *inventory;
  qa_gameplay_authority *combat;
  qa_q2_foundation_host host;
  qa_q2_entity_services *services;
  qa_q2_game_services *game;
  qa_q2w_weapons weapons;
  qa_owned_actor world, player;
} fixture;
static fixture *F;
static double h_now(void *c) { EXPECT_CTX(c, HOST_CTX); return F->now; }
static double h_frame(void *c) { EXPECT_CTX(c, HOST_CTX); return 0.1; }
static double h_gravity(void *c) { EXPECT_CTX(c, HOST_CTX); return 800; }
static double h_random(void *c) { EXPECT_CTX(c, HOST_CTX); return 0.5; }
static void h_schedule(void *c, qa_owned_actor a, const double *d) { EXPECT_CTX(c, HOST_CTX); (void)a; (void)d; }
static void h_touch(void *c, qa_owned_actor a) { EXPECT_CTX(c, HOST_CTX); (void)a; }
static qa_trace_result h_trace(void *c, qa_q2_trace_request r) {
  EXPECT_CTX(c, HOST_CTX);
  qa_trace_result t;
  memset(&t, 0, sizeof t);
  t.kind = "q2"; t.fraction = 1; t.end = r.end;
  return t;
}
static double h_contents(void *c, qa_vec3 p) { EXPECT_CTX(c, HOST_CTX); (void)p; return 0; }
static bool h_true2(void *c, qa_vec3 a, qa_vec3 b) { EXPECT_CTX(c, HOST_CTX); (void)a; (void)b; return true; }
static const qa_actor_id *h_players(void *c, size_t *n) {
  EXPECT_CTX(c, HOST_CTX);
  static qa_actor_id ids[1];
  ids[0] = F->player.id; *n = 1;
  return ids;
}
static const qa_actor_id *h_nearby(void *c, qa_vec3 o, double r, size_t *n) { (void)o; (void)r; return h_players(c, n); }
static qa_actor_id h_world(void *c) { EXPECT_CTX(c, HOST_CTX); return F->world.id; }
static bool h_is_player(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_CTX); return qa_same_actor(a, F->player.id); }
static bool h_is_monster(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_CTX); (void)a; return false; }
static qa_bounds h_inline(void *c, double m) { EXPECT_CTX(c, HOST_CTX); (void)m; qa_bounds b; memset(&b, 0, sizeof b); return b; }
static void h_solid(void *c, qa_owned_actor a, const char *s, const double *m) { EXPECT_CTX(c, HOST_CTX); (void)a; (void)s; (void)m; }
static void h_motion(void *c, qa_q2_motion m) { EXPECT_CTX(c, HOST_CTX); (void)m; }
static void h_portal(void *c, double p, bool o) { EXPECT_CTX(c, HOST_CTX); (void)p; (void)o; }
static void h_emit(void *c, qa_q2_presentation_event e) { EXPECT_CTX(c, HOST_CTX); (void)e; }
static qa_q2_player_view_result h_view(void *c, qa_actor_id a) {
  EXPECT_CTX(c, HOST_CTX); (void)a;
  qa_q2_player_view_result r;
  memset(&r, 0, sizeof r);
  return r;
}
static void h_key(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_CTX); (void)a; }
static void h_prepare(void *c, const char *m, const qa_q2_landmark_carry *l, double f) { EXPECT_CTX(c, HOST_CTX); (void)m; (void)l; (void)f; }
static void h_transition(void *c, qa_transition_intent t) { EXPECT_CTX(c, HOST_CTX); (void)t; }
static void h_diagnostic(void *c, const char *m) { EXPECT_CTX(c, HOST_CTX); qa_raise("Error", 0, 0, "%s", m); }
static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state s) { EXPECT_CTX(c, LINK_CTX); return qa_translated_body_bounds(a, s); }
static void link_noop(void *c, qa_linked_body b) { EXPECT_CTX(c, LINK_CTX); (void)b; }
static void unlink_noop(void *c, qa_actor_id a) { EXPECT_CTX(c, LINK_CTX); (void)a; }
static void w_emit(void *c, qa_q2w_event e) { EXPECT_CTX(c, WEAPON_CTX); (void)e; }
static void w_noise(void *c, qa_actor_id a, qa_vec3 o, bool s) { EXPECT_CTX(c, WEAPON_CTX); (void)a; (void)o; (void)s; }
static void w_dodge(void *c, qa_actor_id m, qa_actor_id a, double e, qa_trace_result t) { EXPECT_CTX(c, WEAPON_CTX); (void)m; (void)a; (void)e; (void)t; }
static void w_ammo(void *c, qa_actor_id a, qa_item_id i) { EXPECT_CTX(c, WEAPON_CTX); (void)a; (void)i; }
static bool w_target(void *c, const qa_actor_id *a, qa_actor_id t) { EXPECT_CTX(c, WEAPON_CTX); (void)a; (void)t; return true; }

static void fixture_open(fixture *f) {
  memset(f, 0, sizeof *f);
  F = f;
  CHECK(qa_identity_owner_create(&f->identities, "q2-equipment"));
  f->actors = qa_session_actor_registry_create(&f->identities, 1024);
  f->callbacks = qa_actor_callback_table_create(f->actors);
  qa_body_link_hooks link;
  memset(&link, 0, sizeof link);
  link.ctx = &LINK_CTX; link.absolute_bounds = link_bounds; link.on_link = link_noop; link.on_unlink = unlink_noop;
  f->bodies = qa_shared_body_table_create(f->actors, link);
  f->inventory = qa_shared_inventory_table_create(f->actors);
  qa_gameplay_hooks gameplay;
  memset(&gameplay, 0, sizeof gameplay);
  f->combat = qa_gameplay_authority_create(f->actors, f->callbacks, gameplay);
  f->world = qa_session_actor_registry_allocate_at_source(f->actors, "q2:game", 0, "q2:worldspawn");
  qa_body_state world_body;
  memset(&world_body, 0, sizeof world_body);
  qa_shared_body_table_create_body(f->bodies, f->world, world_body);
  f->player = qa_session_actor_registry_allocate(f->actors, "q3:character", "q3:sarge");
  qa_body_state body;
  memset(&body, 0, sizeof body);
  body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  qa_shared_body_table_create_body(f->bodies, f->player, body);
  qa_combat_state combat;
  memset(&combat, 0, sizeof combat);
  combat.health = 100; combat.mass = 200; combat.can_take_damage = true; combat.armor.powered.kind = "none";
  qa_gameplay_authority_create_state(f->combat, f->player, combat, NULL, NULL);
  qa_inventory_entry railgun = {.item = "q3:weapon/railgun", .count = 1, .capacity = 1};
  qa_shared_inventory_table_create_entries(f->inventory, f->player, &railgun, 1);
  qa_q2_foundation_host *h = &f->host;
  h->ctx = &HOST_CTX;
  h->actors = f->actors; h->bodies = f->bodies; h->callbacks = f->callbacks; h->combat = f->combat; h->inventory = f->inventory;
  h->now = h_now; h->frame_seconds = h_frame; h->gravity = h_gravity; h->random = h_random; h->schedule = h_schedule;
  h->touch_triggers = h_touch; h->trace = h_trace; h->point_contents = h_contents; h->in_pvs = h_true2; h->in_phs = h_true2;
  h->areas_connected = h_true2; h->nearby = h_nearby; h->players = h_players; h->world_actor = h_world;
  h->is_player = h_is_player; h->is_monster = h_is_monster; h->inline_model_bounds = h_inline; h->set_solid = h_solid;
  h->set_motion = h_motion; h->set_area_portal = h_portal; h->emit = h_emit; h->player_view_state = h_view;
  h->key_consumed = h_key; h->prepare_level_change = h_prepare; h->transition = h_transition; h->diagnostic = h_diagnostic;
  qa_q2_game_options options;
  memset(&options, 0, sizeof options);
  options.edition = QA_Q2_EDITION_CLASSIC; options.map_name = "weapon-check"; options.skill = 1;
  options.mode = QA_Q2_MODE_SINGLEPLAYER; options.max_clients = 1; options.provider = "q2:game"; options.campaign = "q2:campaign";
  options.combat_provider = "q2:combat"; options.inventory_provider = "q2:inventory"; options.movement_provider = "q1:movement";
  f->services = qa_q2_entity_services_create(h, options, NULL, 0);
  CHECK(f->services != NULL);
  f->game = qa_q2_entity_services_game(f->services);
  qa_q2w_hooks wh;
  memset(&wh, 0, sizeof wh);
  wh.ctx = &WEAPON_CTX; wh.emit = w_emit; wh.noise = w_noise; wh.dodge = w_dodge; wh.ammo_changed = w_ammo; wh.can_target = w_target;
  CHECK(qa_q2w_weapons_init(&f->weapons, wh));
}
static void fixture_close(fixture *f) {
  qa_q2_entity_services_destroy(f->services);
  qa_q2w_weapons_destroy(&f->weapons);
  qa_shared_inventory_table_destroy(f->inventory);
  qa_gameplay_authority_destroy(f->combat);
  qa_actor_callback_table_destroy(f->callbacks);
  qa_shared_body_table_destroy(f->bodies);
  qa_session_actor_registry_close(f->actors);
  qa_session_actor_registry_destroy(f->actors);
  qa_identity_owner_destroy(&f->identities);
}

/* ------------------------------------------------------------------ hand grenade runtime */
static qa_q2w_projection project(void *c, qa_vec3 angles, qa_vec3 offset) {
  EXPECT_CTX(c, PROJECT_CTX); (void)angles;
  qa_body_state_result body = qa_shared_body_table_read(F->bodies, F->player.id);
  qa_q2w_projection out = {{body.value.origin.x + offset.x, body.value.origin.y + offset.y, body.value.origin.z + 22 + offset.z}, {1, 0, 0}};
  return out;
}
typedef struct hg { fixture f; qa_q2_hand_grenade_equipment *equipment; qa_app_sim_source_random *random; qa_app_sim_hand_grenade_runtime *runtime; } hg;
static double ammo(hg *t) { return qa_shared_inventory_table_count(t->f.inventory, t->f.player.id, QA_HAND_GRENADE_AMMO); }
static qa_hand_grenade_equipment_state state_of(hg *t) {
  qa_hand_grenade_equipment_state s;
  memset(&s, 0, sizeof s);
  CHECK(qa_q2_hand_grenade_equipment_state_of(t->equipment, t->f.player.id, &s));
  return s;
}
static bool preparing(hg *t, double frame, double next_at, bool release_queued) {
  qa_hand_grenade_equipment_state s = state_of(t);
  return s.action.kind == QA_Q2W_HA_PREPARING && s.action.frame == frame && s.action.next_at == next_at
         && s.action.release_queued == release_queued;
}
static hg *HG;
static void input_world_job(void *ctx) { (void)ctx; qa_app_sim_hand_grenade_runtime_input(HG->runtime, HG->f.world.id, true); }
static const qa_app_sim_hand_grenade_runtime_checkpoint *HG_CP;
static void restore_job(void *ctx) { (void)ctx; qa_app_sim_hand_grenade_runtime_restore(HG->runtime, HG_CP); }
static void test_hand_grenade_runtime(void) {
  hg t;
  memset(&t, 0, sizeof t);
  fixture_open(&t.f);
  HG = &t;
  t.equipment = qa_q2_hand_grenade_equipment_create(t.f.game, &t.f.weapons.base);
  t.random = qa_app_sim_source_random_create(7, "classic");
  qa_hand_grenade_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.is_enabled = true; selection.source.provider = "q2:equipment"; selection.edition = "classic";
  selection.initial_ammo = 3; selection.capacity = 10;
  t.runtime = qa_app_sim_hand_grenade_runtime_create(selection, t.equipment, t.f.services, t.random);
  qa_actor_id player = t.f.player.id;
  qa_app_sim_hand_grenade_runtime_admit(t.runtime, player, NULL);
  /* bun admit: action idle, ammo 3, cap 10 */
  CHECK(state_of(&t).action.kind == QA_Q2W_HA_IDLE && ammo(&t) == 3);
  /* bun: "Actor has no selected hand grenade equipment" */
  CHECK(raises(input_world_job, NULL, "Actor has no selected hand grenade equipment"));
  qa_hand_grenade_equipment_input base;
  memset(&base, 0, sizeof base);
  base.gravity = 800; base.players_collide = true; base.lifecycle = QA_Q2W_ALIVE;
  base.project.ctx = &PROJECT_CTX; base.project.fn = project;
  qa_app_sim_hand_grenade_runtime_input(t.runtime, player, true);
  qa_app_sim_hand_grenade_runtime_step(t.runtime, player, base, true);
  /* bun step0: preparing frame 1 nextAt 0.1, ammo 2 */
  CHECK(preparing(&t, 1, 0.1, false) && ammo(&t) == 2);
  for (int frame = 1; frame <= 4; frame++) {
    t.f.now = frame / 10.0;
    qa_app_sim_hand_grenade_runtime_step(t.runtime, player, base, true);
  }
  /* bun hold4: preparing frame 5 nextAt 0.5 */
  CHECK(preparing(&t, 5, 0.5, false) && ammo(&t) == 2);
  qa_app_sim_hand_grenade_travel travel;
  CHECK(qa_app_sim_hand_grenade_runtime_travel(t.runtime, player, &travel));
  /* bun travel: seconds 0.4, ammo count 2 capacity 10, action preparing frame 5 */
  CHECK(travel.seconds == 0.4 && travel.ammo.count == 2 && travel.ammo.capacity == 10 && travel.state.action.frame == 5);
  t.f.now = 0.5;
  qa_app_sim_hand_grenade_runtime_step(t.runtime, player, base, false);
  /* bun disabled: action disarmed, enabled false, ammo 3 */
  CHECK(state_of(&t).action.kind == QA_Q2W_HA_DISARMED && !state_of(&t).config.enabled && ammo(&t) == 3);
  t.f.now = 0.6;
  qa_app_sim_hand_grenade_runtime_step(t.runtime, player, base, true);
  CHECK(state_of(&t).action.kind == QA_Q2W_HA_DISARMED && state_of(&t).config.enabled && ammo(&t) == 3);
  qa_app_sim_hand_grenade_runtime_checkpoint checkpoint = qa_app_sim_hand_grenade_runtime_capture(t.runtime);
  /* bun capture: one control {slot 1, generation 0, held true, pressed false, released false}; one actor */
  CHECK(checkpoint.control_count == 1 && checkpoint.controls[0].actor.slot == 1 && checkpoint.controls[0].held
        && !checkpoint.controls[0].pressed && checkpoint.controller.actor_count == 1 && checkpoint.version == 1);
  CHECK(is(checkpoint.random.kind, "glibc-random"));
  t.f.now = 2.0;
  travel.seconds = 0.3;
  qa_app_sim_hand_grenade_runtime_admit(t.runtime, player, &travel);
  /* bun carry: preparing frame 5 nextAt 2.2, ammo 2 */
  CHECK(preparing(&t, 5, 2.2, false) && ammo(&t) == 2);
  qa_app_sim_hand_grenade_runtime_input(t.runtime, player, false);
  t.f.now = 2.1;
  qa_app_sim_hand_grenade_runtime_step(t.runtime, player, base, true);
  /* bun release: releaseQueued true */
  CHECK(preparing(&t, 5, 2.2, true));
  qa_app_sim_hand_grenade_runtime_respawn(t.runtime, player);
  /* bun respawn: idle, ammo 3 */
  CHECK(state_of(&t).action.kind == QA_Q2W_HA_IDLE && ammo(&t) == 3);
  qa_app_sim_hand_grenade_runtime_restore(t.runtime, &checkpoint);
  /* bun restored: disarmed, enabled, ammo 3 */
  CHECK(state_of(&t).action.kind == QA_Q2W_HA_DISARMED && state_of(&t).config.enabled && ammo(&t) == 3);
  size_t saved_controls = checkpoint.control_count;
  checkpoint.control_count = 0;
  HG_CP = &checkpoint;
  /* bun: "Equipment checkpoint is missing an action's input state" */
  CHECK(raises(restore_job, NULL, "Equipment checkpoint is missing an action's input state"));
  checkpoint.control_count = saved_controls;
  qa_app_sim_hand_grenade_runtime_checkpoint_destroy(&checkpoint);
  /* actor release drops its controls: step becomes a no-op. */
  qa_session_actor_registry_release(t.f.actors, t.f.player);
  CHECK(!qa_app_sim_hand_grenade_runtime_travel(t.runtime, player, &travel));
  qa_app_sim_hand_grenade_runtime_destroy(t.runtime);
  qa_app_sim_source_random_destroy(t.random);
  qa_q2_hand_grenade_equipment_destroy(t.equipment);
  fixture_close(&t.f);
}

/* equipment-checkpoint reader over a tree built like the TS checkpoint object. */
static qa_save_value *num(double v) { return qa_save_value_number(v); }
static void set(qa_save_value *o, const char *k, qa_save_value *v) { CHECK(qa_save_value_set(o, k, v)); }
static qa_save_value *saved_actor_value(double slot, double generation) {
  qa_save_value *o = qa_save_value_object();
  set(o, "slot", num(slot)); set(o, "generation", num(generation));
  return o;
}
static qa_save_value *glibc_random_value(void) {
  qa_app_sim_source_random *r = qa_app_sim_source_random_create(7, "classic");
  qa_app_sim_source_random_state s = qa_app_sim_source_random_checkpoint(r);
  qa_app_sim_source_random_destroy(r);
  qa_save_value *o = qa_save_value_object(), *words = qa_save_value_array();
  set(o, "kind", qa_save_value_string("glibc-random"));
  for (int i = 0; i < 31; i++) CHECK(qa_save_value_push(words, num(s.words[i])));
  set(o, "words", words);
  set(o, "front", num(s.front)); set(o, "rear", num(s.rear)); set(o, "draws", num((double)s.draws));
  return o;
}
static qa_save_value *q2_entities_value(qa_q2_entity_services *services) {
  qa_q2_foundation_checkpoint capture = qa_q2_entity_services_capture(services);
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_foundation_checkpoint(&capture, &length);
  qa_save_value *value = qa_decode_checkpoint_value(bytes, length);
  free(bytes);
  return value;
}
static void test_equipment_checkpoint_reader(void) {
  fixture f;
  fixture_open(&f);
  qa_save_value *root = qa_save_value_object(), *controller = qa_save_value_object(), *actors = qa_save_value_array();
  qa_save_value *entry = qa_save_value_object(), *state = qa_save_value_object(), *config = qa_save_value_object();
  qa_save_value *action = qa_save_value_object();
  set(root, "version", num(1));
  set(controller, "version", num(1));
  set(controller, "edition", qa_save_value_string("classic"));
  set(entry, "actor", saved_actor_value(1, 0));
  set(config, "enabled", qa_save_value_boolean(true)); set(config, "initialAmmo", num(3));
  set(config, "capacity", num(10)); set(config, "infiniteAmmo", qa_save_value_boolean(false));
  set(action, "kind", qa_save_value_string("cooking")); set(action, "expiresAt", num(4.5));
  set(state, "config", config); set(state, "action", action);
  set(entry, "state", state);
  CHECK(qa_save_value_push(actors, entry));
  set(controller, "actors", actors);
  set(root, "controller", controller);
  qa_save_value *controls = qa_save_value_array(), *control = qa_save_value_object();
  set(control, "actor", saved_actor_value(1, 0)); set(control, "held", qa_save_value_boolean(true));
  set(control, "pressed", qa_save_value_boolean(false)); set(control, "released", qa_save_value_boolean(true));
  CHECK(qa_save_value_push(controls, control));
  set(root, "controls", controls);
  qa_save_value *source = qa_save_value_object();
  set(source, "entities", q2_entities_value(f.services));
  set(source, "random", glibc_random_value());
  set(root, "source", source);
  qa_save_reader reader;
  CHECK(qa_save_reader_init(&reader, root, "equipment"));
  qa_app_sim_hand_grenade_runtime_checkpoint cp = qa_app_sim_read_hand_grenade_runtime_checkpoint(&reader);
  qa_save_reader_destroy(&reader);
  CHECK(cp.version == 1 && cp.controller.edition == QA_Q2_EDITION_CLASSIC && cp.controller.actor_count == 1);
  CHECK(cp.controller.actors[0].state.action.kind == QA_Q2W_HA_COOKING && cp.controller.actors[0].state.action.expires_at == 4.5);
  CHECK(cp.controller.actors[0].state.config.capacity == 10 && cp.control_count == 1 && cp.controls[0].released);
  CHECK(is(cp.random.kind, "glibc-random") && cp.random.front == 3);
  qa_app_sim_hand_grenade_runtime_checkpoint_destroy(&cp);
  qa_save_value_free(root);
  fixture_close(&f);
}

/* ------------------------------------------------------------------ grapple runtime (Q2 CTF) */
static qa_grapple_pose g_pose(void *c, qa_actor_id a, qa_q2_game_services *g) {
  EXPECT_CTX(c, HOOK_CTX); (void)a; (void)g;
  qa_grapple_pose pose = {{0, 0, 0}, "right", 22, 800, {0, 0, -1}};
  return pose;
}
static const char *g_anchor(void *c, qa_actor_id a, qa_q2_game_services *g) { EXPECT_CTX(c, HOOK_CTX); (void)a; (void)g; return "world"; }
static bool g_dead(void *c, qa_actor_id a, qa_q2_game_services *g) { EXPECT_CTX(c, HOOK_CTX); (void)a; (void)g; return false; }
static qa_vec3 g_prev(void *c, qa_actor_id a) { EXPECT_CTX(c, HOOK_CTX); (void)a; qa_vec3 z = {0, 0, 0}; return z; }
static void g_set_prev(void *c, qa_actor_id a, qa_vec3 v) { EXPECT_CTX(c, HOOK_CTX); (void)a; (void)v; }
static double g_volume(void *c, qa_actor_id a) { EXPECT_CTX(c, HOOK_CTX); (void)a; return 1; }
static void g_noise(void *c, qa_actor_id a, qa_q2_game_services *g, qa_vec3 o, const char *k) { EXPECT_CTX(c, HOOK_CTX); (void)a; (void)g; (void)o; (void)k; }
static void g_prediction(void *c, qa_actor_id a, bool s) { EXPECT_CTX(c, HOOK_CTX); (void)a; (void)s; }
static double g_gravity(void *c) { EXPECT_CTX(c, HOOK_CTX); return 800; }
static void g_emit(void *c, qa_grapple_cable_event e) { EXPECT_CTX(c, HOOK_CTX); (void)e; }
static bool s_selected(void *c, qa_actor_id a) { EXPECT_CTX(c, SLOT_CTX); (void)a; return true; }
static bool s_available(void *c, qa_actor_id a) { EXPECT_CTX(c, SLOT_CTX); (void)a; return true; }
static int slot_frames;
static void s_frame(void *c, qa_actor_id a, double frame) { EXPECT_CTX(c, SLOT_CTX); (void)a; (void)frame; slot_frames++; }
static void p_noop(void *c) { EXPECT_CTX(c, PRESENT_CTX); }
static double p_time(void *c, qa_q2w_state *s) { EXPECT_CTX(c, PRESENT_CTX); (void)s; return 0; }
static qa_grapple_weapon_presentation s_presentation(void *c, qa_actor_id a) {
  EXPECT_CTX(c, SLOT_CTX); (void)a;
  qa_grapple_weapon_presentation p = {&PRESENT_CTX, NULL, p_noop, p_noop, p_noop, p_time};
  return p;
}
typedef struct gr { fixture f; qa_q2_ctf_grapple_equipment *core; qa_app_sim_source_random *random; qa_app_sim_grapple_runtime *runtime; } gr;
static gr *GR;
static void gr_open(gr *t, const char *binding) {
  memset(t, 0, sizeof *t);
  fixture_open(&t->f);
  GR = t;
  qa_grapple_hooks hooks = {&HOOK_CTX, g_pose, g_anchor, g_dead, g_prev, g_set_prev, g_volume, g_noise, g_prediction, g_gravity, g_emit};
  t->core = qa_q2_ctf_grapple_create(hooks, NULL, NULL, NULL, NULL);
  t->random = qa_app_sim_source_random_create(5, "classic");
  qa_grapple_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.is_enabled = true; selection.source.provider = "q2:ctf"; selection.binding = binding;
  selection.mechanic = "q2-ctf"; selection.edition = "classic";
  qa_app_sim_grapple_source source;
  memset(&source, 0, sizeof source);
  source.kind = "q2-ctf"; source.q2_game = t->f.services; source.ctf_core = t->core;
  qa_app_sim_grapple_slot_host slot = {&SLOT_CTX, s_selected, s_available, s_frame, s_presentation};
  t->runtime = qa_app_sim_grapple_runtime_create(selection, source, t->random, is(binding, "slot") ? &slot : NULL);
}
static void gr_close(gr *t) {
  qa_app_sim_grapple_runtime_destroy(t->runtime);
  qa_app_sim_source_random_destroy(t->random);
  qa_q2_ctf_grapple_destroy(t->core);
  fixture_close(&t->f);
}
static bool hook_is(gr *t, bool present, int64_t slot, int64_t generation) {
  qa_actor_id hook;
  bool has = qa_app_sim_grapple_runtime_hook(t->runtime, t->f.player.id, &hook);
  if (!present) return !has;
  return has && hook.slot == slot && hook.generation == generation;
}
static size_t entity_total(gr *t) { return qa_q2_game_entity_count(t->f.game); }
static void handoff_job(void *ctx) { (void)ctx; (void)qa_app_sim_grapple_runtime_handoff(GR->runtime, GR->f.player.id); }
static const qa_app_sim_grapple_runtime_checkpoint *GR_CP;
static void gr_restore_job(void *ctx) { (void)ctx; qa_app_sim_grapple_runtime_restore(GR->runtime, GR_CP); }
static void slot_host_missing_job(void *ctx) {
  (void)ctx;
  qa_grapple_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.binding = "slot";
  qa_app_sim_grapple_source source;
  memset(&source, 0, sizeof source);
  source.kind = "q2-ctf";
  (void)qa_app_sim_grapple_runtime_create(selection, source, NULL, NULL);
}
static void test_grapple_offhand(void) {
  gr t;
  gr_open(&t, "offhand");
  qa_actor_id player = t.f.player.id;
  CHECK(raises(slot_host_missing_job, NULL, "Selected grapple slot needs its source weapon host"));
  /* bun: admit-world ok (the world has a live body) */
  qa_app_sim_grapple_runtime_admit(t.runtime, t.f.world.id);
  qa_app_sim_grapple_runtime_admit(t.runtime, player);
  qa_app_sim_grapple_weapon_view view;
  /* bun admit: hook null, pulling false, held false, no state, 0 entities, view null */
  CHECK(hook_is(&t, false, 0, 0) && !qa_app_sim_grapple_runtime_pulling(t.runtime, player) && entity_total(&t) == 0);
  CHECK(!qa_app_sim_grapple_runtime_weapon_view(t.runtime, player, &view));
  qa_weapon_reference weapon = qa_app_sim_grapple_runtime_weapon(t.runtime);
  CHECK(is(weapon.provider, "q2:ctf") && is(weapon.item, "q2:weapon_grapple"));
  CHECK(raises(handoff_job, NULL, "Offhand grapple has no weapon slot"));
  qa_app_sim_grapple_runtime_input(t.runtime, player, true);
  t.f.now = 0.1;
  qa_app_sim_grapple_runtime_step(t.runtime, player, true);
  /* bun fire: hook [2,0], held, state fly t=0, 1 entity */
  CHECK(hook_is(&t, true, 2, 0) && qa_app_sim_grapple_runtime_held(t.runtime, player) && entity_total(&t) == 1);
  qa_ctf_grapple_state *st = qa_q2_ctf_grapple_find_state(t.core, player);
  CHECK(st != NULL && is(st->grapple_state, "fly") && st->grapple_release_time == 0);
  t.f.now = 0.2;
  qa_app_sim_grapple_runtime_step(t.runtime, player, true);
  CHECK(hook_is(&t, true, 2, 0) && entity_total(&t) == 1);
  qa_app_sim_grapple_runtime_observe_teleport(t.runtime, player, 0);
  CHECK(hook_is(&t, true, 2, 0));
  qa_app_sim_grapple_runtime_observe_teleport(t.runtime, player, 4);
  /* bun tele4: hook null, held false, state t=0.2, 0 entities */
  st = qa_q2_ctf_grapple_find_state(t.core, player);
  CHECK(hook_is(&t, false, 0, 0) && !qa_app_sim_grapple_runtime_held(t.runtime, player) && entity_total(&t) == 0
        && st != NULL && st->grapple_release_time == 0.2);
  qa_app_sim_grapple_runtime_input(t.runtime, player, true);
  t.f.now = 0.3;
  qa_app_sim_grapple_runtime_step(t.runtime, player, true);
  /* bun refire: hook [2,1], 1 entity */
  CHECK(hook_is(&t, true, 2, 1) && entity_total(&t) == 1);
  qa_app_sim_grapple_runtime_checkpoint cp = qa_app_sim_grapple_runtime_capture(t.runtime);
  /* bun capture: anim 0; controls [world: teleportBit null, held false], [player: teleportBit 4, held true];
   * states [{actor 1:0, grapple 2:1, fly, 0.2, noKnockback null}] */
  CHECK(cp.version == 2 && cp.weapon_animation_count == 0 && cp.control_count == 2);
  CHECK(cp.controls[0].actor.slot == 0 && !cp.controls[0].control.has_teleport_bit && !cp.controls[0].control.held);
  CHECK(cp.controls[1].actor.slot == 1 && cp.controls[1].control.has_teleport_bit && cp.controls[1].control.teleport_bit == 4
        && cp.controls[1].control.held && !cp.controls[1].control.pressed);
  CHECK(is(cp.source.kind, "q2-ctf") && cp.source.ctf_state_count == 1 && cp.source.ctf_states[0].state.has_grapple
        && cp.source.ctf_states[0].state.grapple.slot == 2 && cp.source.ctf_states[0].state.grapple.generation == 1
        && is(cp.source.ctf_states[0].state.grapple_state, "fly") && cp.source.ctf_states[0].state.grapple_release_time == 0.2
        && !cp.source.ctf_states[0].state.has_no_knockback);
  qa_app_sim_grapple_runtime_release(t.runtime, player);
  /* bun released: hook null, held false, state t=0.3 */
  st = qa_q2_ctf_grapple_find_state(t.core, player);
  CHECK(hook_is(&t, false, 0, 0) && !qa_app_sim_grapple_runtime_held(t.runtime, player) && st->grapple_release_time == 0.3);
  const char *kind = cp.source.kind;
  cp.source.kind = "q2-lmctf";
  GR_CP = &cp;
  /* bun: "Saved grapple source differs from selected equipment" */
  CHECK(raises(gr_restore_job, NULL, "Saved grapple source differs from selected equipment"));
  cp.source.kind = kind;
  qa_app_sim_grapple_runtime_checkpoint_destroy(&cp);
  qa_app_sim_grapple_runtime_set_jump(t.runtime, player, true);
  qa_app_sim_grapple_runtime_set_prediction(t.runtime, player, true);
  CHECK(qa_app_sim_grapple_runtime_jump(t.runtime, player) && qa_app_sim_grapple_runtime_prediction(t.runtime, player));
  qa_app_sim_grapple_runtime_step(t.runtime, player, false);
  /* bun disabled: release clears held/prediction */
  CHECK(!qa_app_sim_grapple_runtime_held(t.runtime, player) && !qa_app_sim_grapple_runtime_prediction(t.runtime, player));
  CHECK(qa_app_sim_grapple_runtime_gravity_scale(t.runtime, player) == 1);
  gr_close(&t);
}
static void test_grapple_slot(void) {
  gr t;
  gr_open(&t, "slot");
  qa_actor_id player = t.f.player.id;
  qa_app_sim_grapple_runtime_admit(t.runtime, t.f.world.id);
  qa_app_sim_grapple_runtime_admit(t.runtime, player);
  qa_app_sim_grapple_weapon_view view;
  /* bun: view {path models/weapons/grapple/tris.md2, frame 0, kick zero} */
  CHECK(qa_app_sim_grapple_runtime_weapon_view(t.runtime, player, &view) && is(view.path, "models/weapons/grapple/tris.md2")
        && view.frame == 0 && view.kick_pitch == 0);
  qa_app_sim_grapple_runtime_input(t.runtime, player, true);
  t.f.now = 0.1;
  qa_app_sim_grapple_runtime_step(t.runtime, player, true);
  t.f.now = 0.2;
  qa_app_sim_grapple_runtime_step(t.runtime, player, true);
  /* bun hold: no hook, no state in slot mode during raise */
  CHECK(hook_is(&t, false, 0, 0) && qa_q2_ctf_grapple_find_state(t.core, player) == NULL && entity_total(&t) == 0);
  qa_app_sim_grapple_runtime_checkpoint cp = qa_app_sim_grapple_runtime_capture(t.runtime);
  /* bun capture: anim 2 (world + player) */
  CHECK(cp.weapon_animation_count == 2 && cp.source.ctf_state_count == 0);
  qa_app_sim_grapple_runtime_checkpoint_destroy(&cp);
  qa_app_sim_equipment_weapon_handoff handoff = qa_app_sim_grapple_runtime_handoff(t.runtime, player);
  CHECK(is(handoff.weapon.item, "q2:weapon_grapple"));
  t.f.now = 0.37;
  handoff.holster(handoff.ctx);
  /* bun: holstered true; resume sets nextFrameAt round((0.37 + 0.1) * 1000) / 1000 = 0.47 */
  CHECK(handoff.is_holstered(handoff.ctx));
  handoff.resume(handoff.ctx);
  qa_app_sim_grapple_runtime_checkpoint after = qa_app_sim_grapple_runtime_capture(t.runtime);
  CHECK(after.weapon_animation_count == 2 && after.weapon_animations[1].animation.next_frame_at == 0.47);
  qa_app_sim_grapple_runtime_checkpoint_destroy(&after);
  /* bun frames []: Q2 slots report no Q1 frames. */
  CHECK(slot_frames == 0);
  gr_close(&t);
}

/* grapple-checkpoint reader: a q2-lmctf source over a real foundation capture. */
static void read_grapple_job(void *ctx) {
  qa_save_reader reader;
  CHECK(qa_save_reader_init(&reader, ctx, "grapple"));
  qa_app_sim_grapple_runtime_checkpoint cp = qa_app_sim_read_grapple_runtime_checkpoint(&reader);
  qa_app_sim_grapple_runtime_checkpoint_destroy(&cp);
  qa_save_reader_destroy(&reader);
}
static void test_grapple_checkpoint_reader(void) {
  fixture f;
  fixture_open(&f);
  qa_save_value *root = qa_save_value_object();
  set(root, "random", glibc_random_value());
  set(root, "version", num(2));
  set(root, "weaponAnimations", qa_save_value_array());
  qa_save_value *controls = qa_save_value_array(), *control = qa_save_value_object(), *zero = qa_save_value_object();
  set(control, "actor", saved_actor_value(1, 0)); set(control, "held", qa_save_value_boolean(true));
  set(control, "jump", qa_save_value_boolean(false)); set(control, "teleportBit", num(4));
  set(control, "pressed", qa_save_value_boolean(false)); set(control, "released", qa_save_value_boolean(false));
  set(zero, "x", num(1)); set(zero, "y", num(2)); set(zero, "z", num(3));
  set(control, "previousVelocity", zero); set(control, "predictionSuppressed", qa_save_value_boolean(true));
  CHECK(qa_save_value_push(controls, control));
  set(root, "controls", controls);
  qa_save_value *source = qa_save_value_object(), *states = qa_save_value_array(), *entry = qa_save_value_object();
  qa_save_value *state = qa_save_value_object();
  set(source, "kind", qa_save_value_string("q2-lmctf"));
  set(source, "entities", q2_entities_value(f.services));
  set(entry, "actor", saved_actor_value(1, 0));
  set(state, "hook", qa_save_value_null()); set(state, "hookState", num(1));
  set(state, "hookLength", num(32.5)); set(state, "hookHeld", qa_save_value_boolean(true));
  set(entry, "state", state);
  CHECK(qa_save_value_push(states, entry));
  set(source, "states", states);
  set(root, "source", source);
  qa_save_reader reader;
  CHECK(qa_save_reader_init(&reader, root, "grapple"));
  qa_app_sim_grapple_runtime_checkpoint cp = qa_app_sim_read_grapple_runtime_checkpoint(&reader);
  qa_save_reader_destroy(&reader);
  CHECK(cp.version == 2 && cp.control_count == 1 && cp.controls[0].control.teleport_bit == 4
        && cp.controls[0].control.previous_velocity.z == 3 && cp.controls[0].control.prediction_suppressed);
  CHECK(is(cp.source.kind, "q2-lmctf") && cp.source.lmctf_state_count == 1 && !cp.source.lmctf_states[0].state.has_hook
        && cp.source.lmctf_states[0].state.hook_state == 1 && cp.source.lmctf_states[0].state.hook_length == 32.5);
  qa_app_sim_grapple_runtime_checkpoint_destroy(&cp);
  /* TS: teleportBit is choice(0, 4) and the random stream must be glibc/MT. */
  /* Object keys keep their first value, so edit the stored nodes in place. */
  qa_save_value *bit = (qa_save_value *)qa_save_value_get(control, "teleportBit");
  bit->number = 2;
  CHECK(raises(read_grapple_job, root, "teleportBit"));
  bit->number = 0;
  qa_save_value *random = (qa_save_value *)qa_save_value_get(root, "random");
  qa_save_value *random_kind = (qa_save_value *)qa_save_value_get(random, "kind");
  free(random_kind->text);
  random_kind->text = malloc(7);
  memcpy(random_kind->text, "q3-lcg", 7);
  set(random, "seed", num(1));
  CHECK(raises(read_grapple_job, root, "Unsupported grapple random stream"));
  qa_save_value_free(root);
  fixture_close(&f);
}

int main(void) {
  prctl(PR_SET_DUMPABLE, 0);
  test_mod_callbacks();
  test_mod_client_checkpoint();
  test_hand_grenade_runtime();
  test_equipment_checkpoint_reader();
  test_grapple_offhand();
  test_grapple_slot();
  test_grapple_checkpoint_reader();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
