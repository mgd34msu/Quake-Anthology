/* Port of src/compat/qc/mod-pickups.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_pickups.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
typedef qa_compat_qc_mod_pickups P;
typedef struct qa_compat_qc_mod_pickups_entry Entry;
typedef struct qa_compat_qc_mod_pickups_rules RuleSet;
static bool actor_eq(qa_actor_id a, qa_actor_id b){return qa_same_actor(a, b);}
static bool client_eq(qa_client_id a, qa_client_id b){return qa_same_client(a, b);}
static Entry *find_entry(P *p, qa_actor_id a){ for(size_t i=0;i<p->entry_count;i++){ if(actor_eq(p->entries[i].actor,a)) return &p->entries[i]; } return NULL; }
static bool resolve_owned(P *p, qa_actor_id actor, qa_owned_actor *out) {
  if (p->operations.resolve_owned) return p->operations.resolve_owned(p->operations.ctx, actor, out);
  return qa_session_actor_registry_resolve_owned(p->services->actors, actor, out);
}
static bool client_of(P *p, qa_actor_id actor, qa_client_id *out) {
  if (p->operations.client_of) return p->operations.client_of(p->operations.ctx, actor, out);
  const qa_mod_client_services *clients = p->services->clients;
  return clients && clients->for_actor(clients->ctx, actor, out);
}
static bool actor_of_client(P *p, qa_client_id client, qa_actor_id *out) {
  if (p->operations.actor_of_client) return p->operations.actor_of_client(p->operations.ctx, client, out);
  const qa_mod_client_services *clients = p->services->clients;
  return clients && clients->actor(clients->ctx, client, out);
}
struct qa_compat_qc_mod_pickups_take { P *p; qa_actor_id actor; const qa_mod_pickup_rule *definition; };
typedef struct qa_compat_qc_mod_pickups_take TakeCtx;
static TakeCtx *take_for(P *p, qa_actor_id actor, const qa_mod_pickup_rule *def) {
  for(size_t i=0;i<p->take_count;i++) if(actor_eq(p->take_storage[i]->actor,actor)&&p->take_storage[i]->definition==def) return p->take_storage[i];
  if(p->take_count==p->take_capacity){ size_t c=p->take_capacity?p->take_capacity*2:8; p->take_storage=realloc(p->take_storage,c*sizeof *p->take_storage); if(!p->take_storage) qa_range_error("pickup allocation failed"); p->take_capacity=c; }
  TakeCtx *cell=malloc(sizeof *cell); if(!cell) qa_range_error("pickup allocation failed");
  *cell=(TakeCtx){p,actor,def}; p->take_storage[p->take_count++]=cell; return cell;
}
/* TS entries.get(actor) === entry; generation stands in for object identity. */
static bool current_of(P *p, qa_actor_id actor, uint64_t generation, qa_client_id entry_client) {
  const Entry *now = find_entry(p, actor);
  qa_client_id client; qa_actor_id back;
  return now && now->generation == generation && qa_session_actor_registry_is_live(p->services->actors, actor)
    && client_of(p, actor, &client) && client_eq(client, entry_client)
    && actor_of_client(p, entry_client, &back) && actor_eq(back, actor);
}
typedef struct { P *p; qa_actor_id actor; const qa_mod_pickup_rule *definition; uint64_t generation; qa_client_id client; qa_actor_id pickup; qa_original_pickup_execution observer; const qa_compat_qc_mod_input_entry *inputs; } RunCtx;
static bool accepts_fn(double v) { if (!isfinite(v)) qa_range_error("QC pickup returned a non-finite source result"); return v != 0; }
static qa_original_pickup_decision run_fn(void *ctx) {
  RunCtx *r = ctx; P *p = r->p;
  const qa_original_pickup_operation *operation = &r->definition->operation;
  if (operation->is_gate_then_grant) {
    double gate = p->operations.invoke(p->operations.ctx, *(const qa_mod_source_call *)operation->gate, r->inputs, 7);
    if (!accepts_fn(gate) || !r->observer.current(r->observer.current_ctx) || !current_of(p, r->actor, r->generation, r->client) || !qa_session_actor_registry_is_live(p->services->actors, r->pickup))
      return QA_ORIGINAL_PICKUP_REFUSED;
  }
  double result = p->operations.invoke(p->operations.ctx, *(const qa_mod_source_call *)operation->grant, r->inputs, 7);
  bool always = operation->is_gate_then_grant && operation->grant_accepts && strcmp(operation->grant_accepts, "always") == 0;
  return (always || accepts_fn(result)) ? QA_ORIGINAL_PICKUP_ACCEPTED : QA_ORIGINAL_PICKUP_REFUSED;
}
static qa_original_pickup_decision take_fn(void *ctx, qa_original_pickup_offer offer, qa_original_pickup_execution observer) {
  TakeCtx *t = ctx; P *p = t->p; qa_actor_id actor = t->actor;
  const Entry *entry = find_entry(p, actor);
  bool offered_has = false; for(size_t i=0;i<t->definition->offered_count;i++) if(strcmp(t->definition->offered[i], offer.item)==0) offered_has=true;
  if (!entry || !observer.current(observer.current_ctx) || !current_of(p, actor, entry->generation, entry->client) || !actor_eq(offer.recipient, actor)
      || !qa_session_actor_registry_is_live(p->services->actors, offer.pickup) || !offered_has)
    qa_range_error("QC pickup invocation differs from its admitted source binding");
  uint64_t generation = entry->generation; qa_client_id client = entry->client;
  double count = offer.count.is_override ? offer.count.amount : 0;
  double time = offer.time.kind == QA_SOURCE_TIME_SECONDS ? offer.time.value : offer.time.value / 1000;
  if (!isfinite(count) || (double)qa_float32(count) != count || !isfinite((double)qa_float32(time))) qa_range_error("QC pickup input exceeds its source scalar ABI");
  qa_actor_id self_id = actor, other_id = offer.pickup;
  qa_compat_qc_mod_input_entry inputs[7]; memset(inputs,0,sizeof inputs);
  inputs[0].name="self"; inputs[0].value.kind="actor"; inputs[0].value.actor_value=&self_id;
  inputs[1].name="other"; inputs[1].value.kind="actor"; inputs[1].value.actor_value=&other_id;
  inputs[2].name="item"; inputs[2].value.kind="string"; inputs[2].value.string_value=offer.item;
  inputs[3].name="time"; inputs[3].value.kind="float"; inputs[3].value.number_value=time;
  inputs[4].name="pickup-count"; inputs[4].value.kind="float"; inputs[4].value.number_value=count;
  inputs[5].name="pickup-has-count"; inputs[5].value.kind="float"; inputs[5].value.number_value=offer.count.is_override?1:0;
  inputs[6].name="pickup-dropped"; inputs[6].value.kind="float"; inputs[6].value.number_value=offer.dropped?1:0;
  RunCtx run = {p, actor, t->definition, generation, client, offer.pickup, observer, inputs};
  return p->operations.watch(p->operations.ctx, actor, observer, &run, run_fn);
}
static RuleSet *actor_rules(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->rule_set_count; i++) if (!p->rule_sets[i].retired && actor_eq(p->rule_sets[i].actor, actor)) return &p->rule_sets[i];
  if (p->rule_set_count==p->rule_set_capacity){ size_t c=p->rule_set_capacity?p->rule_set_capacity*2:4; p->rule_sets=realloc(p->rule_sets,c*sizeof *p->rule_sets); if(!p->rule_sets) qa_range_error("pickup allocation failed"); p->rule_set_capacity=c; }
  size_t n = p->declaration.pickup_count;
  RuleSet *set = &p->rule_sets[p->rule_set_count++];
  *set = (RuleSet){actor, false, calloc(n ? n : 1, sizeof *set->rules), n, calloc(n ? n : 1, sizeof *set->inventory_rules)};
  if (!set->rules || !set->inventory_rules) qa_range_error("pickup allocation failed");
  for (size_t i = 0; i < n; i++) {
    const qa_mod_pickup_rule *def = &p->declaration.pickups[i];
    set->rules[i] = (qa_original_pickup_rule){take_for(p, actor, def), def->id, def->offered, def->offered_count, def->writes, take_fn};
  }
  return set;
}
void qa_compat_qc_mod_pickups_init(P *p, qa_mod_callback_declaration decl, qa_provider_id provider, qa_mod_host_services *services, qa_compat_qc_mod_pickups_operations ops) {
  memset(p,0,sizeof *p); p->declaration=decl; p->provider=provider; p->services=services; p->operations=ops; p->self_ref=p;
}
void qa_compat_qc_mod_pickups_destroy(P *p) {
  free(p->entries);
  for (size_t i=0;i<p->rule_set_count;i++){ free(p->rule_sets[i].rules); free(p->rule_sets[i].inventory_rules); }
  free(p->rule_sets);
  for (size_t i=0;i<p->take_count;i++) free(p->take_storage[i]);
  free(p->take_storage); memset(p,0,sizeof *p);
}
typedef struct { P *p; Entry entry; } RemoveTry;
static void remove_binding(void *ctx) {
  RemoveTry *t = ctx;
  if (t->entry.remove) t->entry.remove(t->entry.remove_ctx);
  if (t->entry.has_inventory_binding) qa_shared_inventory_table_unbind_pickup(t->p->services->inventory, t->entry.owner, t->entry.inventory_binding);
}
void qa_compat_qc_mod_pickups_release(P *p, qa_actor_id actor) {
  Entry *found = find_entry(p, actor); if (!found) return;
  Entry entry = *found;
  RemoveTry attempt = {p, entry}; qa_caught_error caught;
  bool removed = qa_try(remove_binding, &attempt, &caught);
  found = find_entry(p, actor);
  if (found) { size_t idx=(size_t)(found - p->entries); memmove(&p->entries[idx],&p->entries[idx+1],(p->entry_count-idx-1)*sizeof *p->entries); p->entry_count--; }
  for (size_t i=0;i<p->rule_set_count;i++) if (actor_eq(p->rule_sets[i].actor, actor)) p->rule_sets[i].retired = true;
  if (!removed) qa_range_error("QC pickup release failed");
}
typedef struct { P *p; qa_actor_id actor; } AdmitUnwind;
static void unwind_admit(void *arg) { AdmitUnwind *u = arg; qa_compat_qc_mod_pickups_release(u->p, u->actor); }
void qa_compat_qc_mod_pickups_admit(P *p, qa_actor_id actor) {
  if (find_entry(p, actor)) return;
  qa_owned_actor owner; qa_client_id client; qa_actor_id back;
  if (!resolve_owned(p, actor, &owner) || !client_of(p, actor, &client) || !actor_of_client(p, client, &back) || !actor_eq(back, actor))
    qa_range_error("QC pickups require a live canonical client");
  if (p->entry_count==p->entry_capacity){ size_t c=p->entry_capacity?p->entry_capacity*2:4; p->entries=realloc(p->entries,c*sizeof *p->entries); if(!p->entries) qa_range_error("pickup allocation failed"); p->entry_capacity=c; }
  uint64_t generation = ++p->next_generation;
  p->entries[p->entry_count++] = (Entry){actor, client, generation, owner, NULL, NULL, false, 0};
  RuleSet *set = actor_rules(p, actor);
  size_t in_n = 0;
  for (size_t i = 0; i < set->rule_count; i++) {
    bool inv = false; for (size_t j = 0; j < set->rules[i].writes.count; j++) if (set->rules[i].writes.values[j].kind == QA_PICKUP_RESOURCE_INVENTORY) inv = true;
    if (inv) set->inventory_rules[in_n++] = set->rules[i];
  }
  if (in_n == 0) return;
  void (*remove)(void *) = NULL; void *remove_ctx = NULL; bool has_binding = false; int64_t binding = 0;
  AdmitUnwind unwind = {p, actor}; qa_cleanup scope; qa_cleanup_push(&scope, unwind_admit, &unwind); /* TS catch: release, rethrow */
  if (p->operations.bind_pickup) p->operations.bind_pickup(p->operations.ctx, owner, set->inventory_rules, in_n, &remove, &remove_ctx);
  else { binding = qa_shared_inventory_table_bind_pickup(p->services->inventory, owner, (qa_inventory_pickup_binding){p->provider, set->inventory_rules, in_n}); has_binding = true; }
  qa_cleanup_pop(&scope);
  Entry *entry = find_entry(p, actor);
  if (entry) { entry->remove = remove; entry->remove_ctx = remove_ctx; entry->has_inventory_binding = has_binding; entry->inventory_binding = binding; }
}
size_t qa_compat_qc_mod_pickups_protection(P *p, qa_actor_id actor, qa_protection_channel channel, const qa_original_pickup_rule **out, size_t cap) {
  RuleSet *set = actor_rules(p, actor);
  size_t n = 0;
  for (size_t i = 0; i < set->rule_count; i++) {
    bool hit = false; for (size_t j = 0; j < set->rules[i].writes.count; j++) if (set->rules[i].writes.values[j].kind == QA_PICKUP_RESOURCE_PROTECTION && set->rules[i].writes.values[j].channel == channel) hit = true;
    if (hit) { if (n < cap) out[n] = &set->rules[i]; n++; }
  }
  return n;
}
typedef struct { P *p; qa_actor_id actor; } CloseTry;
static void close_release(void *ctx) { CloseTry *t = ctx; qa_compat_qc_mod_pickups_release(t->p, t->actor); }
void qa_compat_qc_mod_pickups_close(P *p) {
  for (size_t i=0;i<p->rule_set_count;i++) p->rule_sets[i].retired = true;
  size_t errors = 0;
  while (p->entry_count) { CloseTry attempt = {p, p->entries[0].actor}; qa_caught_error caught; if (!qa_try(close_release, &attempt, &caught)) errors++; }
  if (errors) qa_range_error("QC pickup close failed");
}
