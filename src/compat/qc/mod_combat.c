/* Port of src/compat/qc/mod-combat.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_combat.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q1/quakec/armor_points.h"
#include "qa/content/q1/quakec/armor_stage.h"
#include "qa/content/q1/quakec/damage_scale.h"
#include "qa/content/q1/quakec/id1_program.h"
#include "qa/foundation/support.h"
#include "qa/world/actors/callbacks.h"
typedef qa_compat_qc_mod_combat C;
/* Build a qa_q1qc_program view over the real program. */
static bool build_qprogram(C *c, qa_compat_qc_program *p) {
  c->qglobals = calloc(p->global_count ? p->global_count : 1, sizeof *c->qglobals);
  c->qfields = calloc(p->field_count ? p->field_count : 1, sizeof *c->qfields);
  c->qfunctions = calloc(p->function_count ? p->function_count : 1, sizeof *c->qfunctions);
  c->qstatements = calloc(p->statement_count ? p->statement_count : 1, sizeof *c->qstatements);
  if (!c->qglobals || !c->qfields || !c->qfunctions || !c->qstatements) return false;
  for (size_t i = 0; i < p->global_count; i++) c->qglobals[i] = (qa_q1qc_definition){p->globals[i].type, p->globals[i].native_type, p->globals[i].save, p->globals[i].offset, p->globals[i].name};
  for (size_t i = 0; i < p->field_count; i++) c->qfields[i] = (qa_q1qc_definition){p->fields[i].type, p->fields[i].native_type, p->fields[i].save, p->fields[i].offset, p->fields[i].name};
  for (size_t i = 0; i < p->function_count; i++) c->qfunctions[i] = (qa_q1qc_function){p->functions[i].index, p->functions[i].first_statement, p->functions[i].parameter_start, p->functions[i].local_words, p->functions[i].name, p->functions[i].file, p->functions[i].parameter_sizes, p->functions[i].parameter_size_count, p->functions[i].named_builtin};
  for (size_t i = 0; i < p->statement_count; i++) c->qstatements[i] = (qa_q1qc_statement){(qa_q1qc_opcode)p->statements[i].opcode, p->statements[i].a, p->statements[i].b, p->statements[i].c};
  c->qprogram = (qa_q1qc_program){p->source, p->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD ? "q1-quakeworld" : "q1-netquake",
    c->qstatements, p->statement_count, c->qglobals, p->global_count, c->qfields, p->field_count, c->qfunctions, p->function_count,
    p->strings, p->string_length, p->initial_globals, p->initial_globals_length, p->entity_field_words, p->checksum, p->digest};
  return true;
}
static void sync_entities(C *c) {
  qa_compat_qc_entity_memory *e = c->options.machine->entities;
  c->qentities = (qa_q1qc_entity_memory){e->layout.stride_bytes, e->layout.variables_offset_bytes, e->layout.field_words, e->capacity, e->count, e->bytes};
}
static qa_q1qc_machine *machine_fn(void *ctx) {
  C *c = ctx; qa_compat_qc_machine *m = c->options.machine;
  sync_entities(c);
  c->qmachine.ctx = c; c->qmachine.program = &c->qprogram;
  c->qmachine.globals = (qa_q1qc_words){m->globals.bytes, m->globals.byte_length};
  c->qmachine.entities = &c->qentities; c->qmachine.numeric = *m->numeric;
  return &c->qmachine;
}
static void qm_fail(void *ctx, const char *message) { C *c = ctx; qa_compat_qc_machine_fail(c->options.machine, "%s", message); }
static void qm_execute(void *ctx, int64_t fn, int64_t argc) { C *c = ctx; qa_compat_qc_machine_execute(c->options.machine, fn, argc); }
static double qm_execute_region(void *ctx, qa_q1qc_inline_region region, int64_t argc) {
  C *c = ctx;
  qa_compat_qc_inline_region r = {region.function_index, region.entry, region.exit, region.replaceable, region.has_standalone, region.standalone_saved, region.standalone_scope};
  return qa_compat_qc_machine_execute_region(c->options.machine, r, argc);
}
static const char *qm_strings_get(void *ctx, int64_t reference, char *buf, size_t size) {
  C *c = ctx; const char *s = qa_compat_qc_strings_get(&c->options.machine->strings, reference);
  if (buf && size) { snprintf(buf, size, "%s", s); return buf; }
  return s;
}
static int64_t qm_strings_set_engine(void *ctx, const char *name, const char *text, size_t capacity) {
  C *c = ctx; return qa_compat_qc_strings_set_engine(&c->options.machine->strings, name, text, (int64_t)capacity);
}
void qa_compat_qc_validate_mod_combat(qa_compat_qc_program *p, qa_mod_qc_combat declaration) {
  C tmp; memset(&tmp, 0, sizeof tmp);
  if (!build_qprogram(&tmp, p)) qa_range_error("combat validation allocation failed");
  if (declaration.empty_armor) qa_q1qc_empty_armor_for(&tmp.qprogram, declaration.empty_armor);
  { qa_q1qc_armor_stage *s = qa_q1qc_armor_stage_create(&tmp.qprogram, declaration.armor_stage); qa_q1qc_armor_stage_destroy(s); }
  { qa_q1qc_damage_scale *s = qa_q1qc_damage_scale_create(&tmp.qprogram, &declaration.damage, declaration.damage_scale); qa_q1qc_damage_scale_destroy(s); }
  static const char *const fields[6] = {"health","takedamage","flags","invincible_finished","armorvalue","armortype"};
  for (int i = 0; i < 6; i++) { const qa_compat_qc_definition *d = qa_compat_qc_program_field_named(p, fields[i]);
    if (!d || strcmp(d->type, "float") != 0) qa_range_error("QC combat requires float field %s", fields[i]); }
  qa_q1_id1_program_binding_get(&tmp.qprogram, &declaration.damage);
  free(tmp.qglobals); free(tmp.qfields); free(tmp.qfunctions); free(tmp.qstatements);
}
size_t qa_compat_qc_damage_inputs(qa_damage_request r, double seconds, qa_compat_qc_mod_input_entry *out, size_t cap) {
  /* Actor pointers borrow this retained copy (valid until next call). */
  static qa_damage_request saved; saved = r;
  qa_compat_qc_mod_input_entry entries[9];
  memset(entries, 0, sizeof entries);
  entries[0].name="self"; entries[0].value.kind="actor"; entries[0].value.actor_value=&saved.target;
  entries[1].name="attacker"; entries[1].value.kind="actor"; entries[1].value.actor_value=saved.attack.attacker;
  entries[2].name="inflictor"; entries[2].value.kind="actor"; entries[2].value.actor_value=saved.attack.inflictor;
  entries[3].name="amount"; entries[3].value.kind="float"; entries[3].value.number_value=r.amount;
  entries[4].name="knockback"; entries[4].value.kind="float"; entries[4].value.number_value=r.knockback;
  entries[5].name="point"; entries[5].value.kind="vector"; entries[5].value.vector_value=r.point;
  entries[6].name="direction"; entries[6].value.kind="vector"; entries[6].value.vector_value=r.direction;
  entries[7].name="normal"; entries[7].value.kind="vector"; entries[7].value.vector_value=r.normal;
  entries[8].name="time"; entries[8].value.kind="float"; entries[8].value.number_value=seconds;
  size_t n = cap < 9 ? cap : 9;
  for (size_t i = 0; i < n; i++) out[i] = entries[i];
  return 9;
}
static double seconds_of(C *c) { qa_source_time t = c->options.services->time(c->options.services->ctx); return t.kind == QA_SOURCE_TIME_SECONDS ? t.value : t.value / 1000; }
static int64_t fld(C *c, const char *n) { return qa_compat_qc_machine_field_offset(c->options.machine, n); }
static qa_damage_outcome apply_damage(C *c, qa_damage_request request);
static qa_damage_request request_of(void *ctx, qa_q1_id1_damage_call call);
static void reaction_of(void *ctx, qa_damage_request request, qa_source_damage_result result, qa_q1qc_function_execution execute);
static void completed_of(void *ctx, qa_damage_request request, qa_damage_outcome outcome) {
  C *c = ctx;
  if (c->incoming_count) {
    size_t last = c->incoming_count - 1;
    if (memcmp(&c->incoming[last], &request, sizeof request) == 0) { c->outcomes[last] = outcome; c->has_outcome[last] = true; }
  }
}
static qa_actor_id proj_actor(void *ctx, int64_t ref) { C *c = ctx; return c->options.actor(c->options.ctx, ref); }
static int64_t proj_reference(void *ctx, const qa_actor_id *a) { C *c = ctx; return c->options.reference(c->options.ctx, a); }
bool qa_compat_qc_mod_combat_init(C *c, qa_compat_qc_mod_combat_options o) {
  memset(c, 0, sizeof *c); c->options = o; c->self_ref = c;
  if (!build_qprogram(c, o.program)) return false;
  c->qmachine.fail = qm_fail; c->qmachine.execute = qm_execute; c->qmachine.execute_region = qm_execute_region;
  c->qmachine.strings_get = qm_strings_get; c->qmachine.strings_set_engine = qm_strings_set_engine;
  sync_entities(c);
  c->qsource = (qa_q1qc_source){&c->qprogram, &c->qentities, o.services->actors, o.slots, o.slots_options.provider, o.ctx, o.slot_free};
  qa_q1_id1_damage_projection projection = {c, NULL, proj_actor, proj_reference, reaction_of, completed_of};
  c->damage = qa_q1_id1_damage_binding_create(c->qsource, o.services->combat, machine_fn, c, request_of, c, &projection,
    o.declaration->armor_stage, o.declaration->damage_scale ? &o.declaration->damage : NULL, o.declaration->damage_scale);
  if (!c->damage) return false;
  return true;
}
void qa_compat_qc_mod_combat_destroy(C *c) {
  qa_q1_id1_damage_binding_destroy(c->damage);
  free(c->qglobals); free(c->qfields); free(c->qfunctions); free(c->qstatements);
  free(c->incoming); free(c->entered); free(c->outcomes); free(c->has_outcome);
  memset(c, 0, sizeof *c);
}
typedef struct { C *c; qa_compat_qc_words *words; qa_owned_actor actor; const qa_q1_id1_program_binding *binding; } BindCtx;
static BindCtx bind_storage[512]; static size_t bind_count;
static BindCtx *bind_for(C *c, qa_owned_actor actor) {
  for (size_t i = 0; i < bind_count; i++) if (bind_storage[i].c == c && qa_same_owned_actor(bind_storage[i].actor, actor)) return &bind_storage[i];
  if (bind_count >= 512) qa_range_error("combat binding storage exhausted");
  bind_storage[bind_count] = (BindCtx){c, NULL, actor, NULL}; return &bind_storage[bind_count++];
}
static void write_armor_inner(BindCtx *b, qa_armor_state armor) {
  C *c = b->c;
  if (strcmp(armor.powered.kind, "none") != 0 || (armor.regular.kind != QA_REGULAR_ARMOR_NONE && armor.regular.kind != QA_REGULAR_ARMOR_Q1))
    qa_range_error("Cannot store foreign armor in source QC fields");
  int64_t green = (int64_t)b->binding->armor_masks[0], yellow = (int64_t)b->binding->armor_masks[1], red = (int64_t)b->binding->armor_masks[2], mask = green | yellow | red;
  int64_t bit = armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0
    : armor.regular.item && strcmp(armor.regular.item, "q1:item_armorInv") == 0 ? red
    : armor.regular.item && strcmp(armor.regular.item, "q1:item_armor2") == 0 ? yellow : green;
  qa_compat_qc_words_set_float(b->words, fld(c, "armorvalue"), armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : armor.regular.points);
  qa_compat_qc_words_set_float(b->words, fld(c, "armortype"), armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : armor.regular.absorption);
  int64_t f = fld(c, b->binding->armor_field);
  qa_compat_qc_words_set_float(b->words, f, (double)(((int64_t)trunc(qa_compat_qc_words_float(b->words, f)) & ~mask) | bit));
}
static qa_damage_outcome b_source_damage(void *ctx, qa_damage_request r) { return apply_damage(((BindCtx *)ctx)->c, r); }
static qa_combat_state b_read(void *ctx) {
  BindCtx *b = ctx; C *c = b->c;
  qa_q1qc_words qw = {b->words->bytes, b->words->byte_length};
  return (qa_combat_state){qa_compat_qc_words_float(b->words, fld(c, "health")), qa_q1_id1_damage_read_armor(c->damage, &qw), 200,
    qa_compat_qc_words_float(b->words, fld(c, "takedamage")) != 0,
    qa_compat_qc_words_float(b->words, fld(c, "invincible_finished")) > seconds_of(c), false, false,
    c->options.player_team ? c->options.player_team(c->options.ctx, b->actor.id) : NULL};
}
static void b_validate(void *ctx, qa_armor_state armor) {
  (void)ctx;
  if (strcmp(armor.powered.kind, "none") != 0 || (armor.regular.kind != QA_REGULAR_ARMOR_NONE && armor.regular.kind != QA_REGULAR_ARMOR_Q1))
    qa_range_error("Cannot store foreign armor in source QC fields");
}
static void b_write_health(void *ctx, double h) { BindCtx *b = ctx; qa_compat_qc_words_set_float(b->words, fld(b->c, "health"), h); }
static void b_write_armor(void *ctx, qa_armor_state a) { write_armor_inner(ctx, a); }
static void b_write_traits(void *ctx, qa_combat_traits traits) {
  BindCtx *b = ctx; C *c = b->c;
  const char *player_team = c->options.player_team ? c->options.player_team(c->options.ctx, b->actor.id) : NULL;
  const char *mapped = traits.team && strcmp(traits.team, "red") == 0 ? "team:red" : traits.team && strcmp(traits.team, "blue") == 0 ? "team:blue" : traits.team;
  bool have_player = c->options.player_team != NULL;
  if (have_player && ((player_team == NULL) != (mapped == NULL) || (player_team && mapped && strcmp(player_team, mapped) != 0)))
    if (c->options.player_set_team) c->options.player_set_team(c->options.ctx, b->actor.id, mapped);
  if (traits.can_take_damage != (qa_compat_qc_words_float(b->words, fld(c, "takedamage")) != 0))
    qa_compat_qc_words_set_float(b->words, fld(c, "takedamage"), traits.can_take_damage ? 1 : 0);
  if (traits.invulnerable != (qa_compat_qc_words_float(b->words, fld(c, "invincible_finished")) > seconds_of(c)))
    qa_compat_qc_words_set_float(b->words, fld(c, "invincible_finished"), traits.invulnerable ? INFINITY : 0);
}
static qa_regular_armor_state b_empty(void *ctx, double points) {
  BindCtx *b = ctx;
  qa_q1qc_empty_armor armor = qa_q1qc_empty_armor_for(&b->c->qprogram, b->c->options.declaration->empty_armor);
  return qa_q1qc_empty_armor_apply(armor, points);
}
void qa_compat_qc_mod_combat_admit(C *c, qa_owned_actor actor) {
  qa_actor_source_result src = qa_session_actor_registry_source_of(c->options.services->actors, actor.id);
  if (!src.has_value || strcmp(src.provider, c->options.slots_options.provider) != 0) qa_range_error("QC combat admission requires its own source actor");
  BindCtx *b = bind_for(c, actor);
  b->words = qa_compat_qc_entity_memory_at(c->options.machine->entities, src.slot);
  b->binding = qa_q1_id1_program_binding_get(&c->qprogram, NULL);
  qa_source_armor_stage powered_stage, regular_stage;
  bool has_powered = qa_q1_id1_damage_protection_stage(c->damage, actor, QA_PROTECTION_POWERED, &powered_stage);
  bool has_regular = qa_q1_id1_damage_protection_stage(c->damage, actor, QA_PROTECTION_REGULAR, &regular_stage);
  qa_combat_state_binding state; memset(&state, 0, sizeof state);
  state.ctx = b; state.source_damage = b_source_damage;
  state.protection_regular = (qa_combat_protection_declaration){true, false, actor.owner, has_regular, has_regular ? regular_stage : (qa_source_armor_stage){0}};
  state.protection_powered = (qa_combat_protection_declaration){true, true, NULL, has_powered, has_powered ? powered_stage : (qa_source_armor_stage){0}};
  state.read = b_read; state.write_health = b_write_health; state.write_armor = b_write_armor;
  state.validate_armor = b_validate; state.write_traits = b_write_traits;
  if (c->options.declaration->empty_armor) state.empty_regular_armor = b_empty;
  if (!qa_gameplay_authority_read(c->options.services->combat, actor.id).has_value) qa_gameplay_authority_bind(c->options.services->combat, actor, state);
  else qa_gameplay_authority_rebind(c->options.services->combat, actor, state);
}
static qa_damage_outcome apply_damage(C *c, qa_damage_request request) {
  if (c->incoming_count == c->incoming_capacity) { size_t cap = c->incoming_capacity ? c->incoming_capacity*2 : 4;
    c->incoming = realloc(c->incoming, cap*sizeof *c->incoming); c->entered = realloc(c->entered, cap*sizeof *c->entered);
    c->outcomes = realloc(c->outcomes, cap*sizeof *c->outcomes); c->has_outcome = realloc(c->has_outcome, cap*sizeof *c->has_outcome);
    if (!c->incoming || !c->entered || !c->outcomes || !c->has_outcome) { qa_range_error("combat allocation failed"); } c->incoming_capacity = cap; }
  size_t i = c->incoming_count++;
  c->incoming[i] = request; c->entered[i] = false; c->has_outcome[i] = false;
  qa_compat_qc_mod_input_entry inputs[9]; qa_compat_qc_damage_inputs(request, seconds_of(c), inputs, 9);
  c->options.invoke(c->options.ctx, c->options.declaration->damage, inputs, 9);
  qa_damage_outcome outcome = c->outcomes[i]; bool has = c->has_outcome[i];
  c->incoming_count--;
  if (!has) qa_range_error("QC damage did not complete its shared authority boundary");
  return outcome;
}
static qa_damage_request request_of(void *ctx, qa_q1_id1_damage_call call) {
  C *c = ctx;
  if (c->incoming_count && !c->entered[c->incoming_count-1]) { c->entered[c->incoming_count-1] = true; return c->incoming[c->incoming_count-1]; }
  if (!c->options.services->damage_context) qa_range_error("Authored QC damage requires canonical attack provenance");
  double sequence = 0; qa_provider_id wp=NULL, cp=NULL, ip=NULL, mp=NULL;
  c->options.services->damage_context(c->options.services->ctx, c->options.module.id, &sequence, &wp, &cp, &ip, &mp);
  qa_compat_qc_machine *machine = c->options.machine;
  const qa_compat_qc_definition *death_type = qa_compat_qc_program_field_named(c->options.program, "deathtype");
  const char *death_text = "";
  char death_buf[256] = "";
  if (death_type) { qa_compat_qc_words *words = qa_compat_qc_entity_memory_from_reference(machine->entities, c->options.reference(c->options.ctx, &call.target));
    snprintf(death_buf, sizeof death_buf, "%s", qa_compat_qc_strings_get(&machine->strings, qa_compat_qc_words_int(words, death_type->offset))); death_text = death_buf; }
  qa_body_state_result body = qa_shared_body_table_read(c->options.services->bodies, call.target);
  qa_damage_request r; memset(&r, 0, sizeof r);
  r.target = call.target; r.amount = call.amount; r.knockback = 0; r.delivery = "direct";
  r.point = body.has_value ? body.value.origin : (qa_vec3){0,0,0};
  r.attack.sequence = sequence; r.attack.weapon_provider = wp; r.attack.combat_provider = cp; r.attack.inventory_provider = ip; r.attack.movement_provider = mp;
  r.attack.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, qa_compat_qc_words_float(&machine->globals, qa_compat_qc_machine_global_offset(machine, "time"))};
  static qa_actor_id atk, inf; atk = call.attacker; inf = call.inflictor;
  r.attack.attacker = &atk; r.attack.inflictor = &inf; r.attack.weapon = NULL;
  r.attack.cause.kind = QA_ATTACK_CAUSE_Q1; r.attack.cause.death_type = death_text;
  (void)death_buf;
  return r;
}
typedef struct { C *c; qa_q1qc_function_execution execute; bool entered; qa_actor_id owner; qa_pain_reaction reaction; } ReactionCtx;
typedef struct { ReactionCtx *r; qa_pain_reaction effective; } PainPrepare;
static void pain_prepare(void *ctx, qa_q1qc_machine *qm) {
  (void)qm; PainPrepare *pp = ctx; qa_compat_qc_machine *m = pp->r->c->options.machine;
  qa_compat_qc_words_set_int(&m->globals, 4, (double)pp->r->c->options.reference(pp->r->c->options.ctx, pp->effective.attacker));
  qa_compat_qc_words_set_float(&m->globals, 7, pp->effective.damage);
}
static void pain_execute(void *ctx, qa_pain_reaction effective) {
  ReactionCtx *r = ctx;
  if (!qa_q1qc_actor_equal(effective.self.id, r->owner)) qa_range_error("Source pain target changes require callback replacement");
  r->entered = true;
  PainPrepare pp = {r, effective};
  r->execute.run(r->execute.ctx, pain_prepare, &pp);
}
static void die_execute(void *ctx, qa_death_reaction effective) {
  ReactionCtx *r = ctx;
  bool attacker_eq = (effective.pain.attacker == NULL && r->reaction.attacker == NULL) || (effective.pain.attacker && r->reaction.attacker && qa_q1qc_actor_equal(*effective.pain.attacker, *r->reaction.attacker));
  if (!qa_q1qc_actor_equal(effective.pain.self.id, r->owner) || !attacker_eq || effective.pain.damage != r->reaction.damage)
    qa_range_error("Source death argument changes require callback replacement");
  r->entered = true; r->execute.run(r->execute.ctx, NULL, NULL);
}
static void reaction_of(void *ctx, qa_damage_request request, qa_source_damage_result result, qa_q1qc_function_execution execute) {
  C *c = ctx; qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(c->options.services->actors, request.target, &owner)) { int32_t zero[3] = {0,0,0}; execute.skip(execute.ctx, zero); return; }
  if (!c->options.services->callbacks) qa_range_error("QC damage reactions require shared actor callbacks");
  qa_pain_reaction reaction = {&request.attack, owner, request.attack.attacker, request.knockback, result.applied_damage};
  ReactionCtx rc = {c, execute, false, owner.id, reaction};
  if (strcmp(result.reaction, "pain") == 0) qa_actor_callback_table_source_pain(c->options.services->callbacks, reaction, pain_execute, &rc);
  else if (strcmp(result.reaction, "death") == 0) {
    qa_death_reaction death = {reaction, request.attack.inflictor, request.point};
    qa_actor_callback_table_source_die(c->options.services->callbacks, death, die_execute, &rc);
  } else qa_range_error("QC reaction boundary has no source reaction");
  if (!rc.entered) { int32_t zero[3] = {0,0,0}; execute.skip(execute.ctx, zero); }
}
static void actor_reaction(C *c, qa_pain_reaction reaction, const char *field, const qa_mod_callback_value *args, size_t argc) {
  qa_compat_qc_machine *machine = c->options.machine;
  int64_t reference = c->options.reference(c->options.ctx, &reaction.self.id);
  int32_t index = qa_compat_qc_words_int(qa_compat_qc_entity_memory_from_reference(machine->entities, reference), fld(c, field));
  if (index == 0) return;
  qa_mod_callback_value self_v = {"input", "self", 0, NULL, {0,0,0}}, time_v = {"input", "time", 0, NULL, {0,0,0}};
  qa_mod_named_value globals[2] = {{"self", self_v}, {"time", time_v}};
  qa_mod_source_call call = {qa_compat_qc_program_function_at(c->options.program, index)->name, args, argc, globals, 2};
  qa_compat_qc_mod_input_entry inputs[4];
  static qa_actor_id self_id, attacker_id;
  self_id = reaction.self.id; if (reaction.attacker) attacker_id = *reaction.attacker;
  inputs[0]=(qa_compat_qc_mod_input_entry){"self",{"actor",0,NULL,{0,0,0},&self_id}};
  inputs[1]=(qa_compat_qc_mod_input_entry){"attacker",{"actor",0,NULL,{0,0,0},reaction.attacker?&attacker_id:NULL}};
  inputs[2]=(qa_compat_qc_mod_input_entry){"amount",{"float",reaction.damage,NULL,{0,0,0},NULL}};
  inputs[3]=(qa_compat_qc_mod_input_entry){"time",{"float",seconds_of(c),NULL,{0,0,0},NULL}};
  c->options.invoke(c->options.ctx, call, inputs, 4);
}
void qa_compat_qc_mod_combat_pain(C *c, qa_pain_reaction reaction) {
  qa_mod_callback_value args[2] = {{"input","attacker",0,NULL,{0,0,0}}, {"input","amount",0,NULL,{0,0,0}}};
  actor_reaction(c, reaction, "th_pain", args, 2);
}
void qa_compat_qc_mod_combat_die(C *c, qa_death_reaction reaction) { actor_reaction(c, reaction.pain, "th_die", NULL, 0); }
