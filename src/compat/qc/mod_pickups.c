/* Port of src/compat/qc/mod-pickups.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_pickups.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
typedef qa_compat_qc_mod_pickups P;
typedef struct qa_compat_qc_mod_pickups_entry Entry;
static bool actor_eq(qa_actor_id a, qa_actor_id b){return qa_same_actor(a, b);}
static bool client_eq(qa_client_id a, qa_client_id b){return qa_same_client(a, b);}
static Entry *find_entry(P *p, qa_actor_id a){ for(size_t i=0;i<p->entry_count;i++){ if(actor_eq(p->entries[i].actor,a)) return &p->entries[i]; } return NULL; }
struct qa_compat_qc_mod_pickups_take { P *p; qa_actor_id actor; const qa_mod_pickup_rule *definition; };
typedef struct qa_compat_qc_mod_pickups_take TakeCtx;
static TakeCtx *take_for(P *p, qa_actor_id actor, const qa_mod_pickup_rule *def) {
  for(size_t i=0;i<p->take_count;i++) if(actor_eq(p->take_storage[i]->actor,actor)&&p->take_storage[i]->definition==def) return p->take_storage[i];
  if(p->take_count==p->take_capacity){ size_t c=p->take_capacity?p->take_capacity*2:8; p->take_storage=realloc(p->take_storage,c*sizeof *p->take_storage); if(!p->take_storage) qa_range_error("pickup allocation failed"); p->take_capacity=c; }
  TakeCtx *cell=malloc(sizeof *cell); if(!cell) qa_range_error("pickup allocation failed");
  *cell=(TakeCtx){p,actor,def}; p->take_storage[p->take_count++]=cell; return cell;
}
static bool current_of(P *p, qa_actor_id actor, Entry *entry) {
  qa_client_id client; qa_actor_id back;
  return find_entry(p, actor) == entry && qa_session_actor_registry_is_live(p->services->actors, actor)
    && p->operations.client_of(p->operations.ctx, actor, &client) && client_eq(client, entry->client)
    && p->operations.actor_of_client(p->operations.ctx, entry->client, &back) && actor_eq(back, actor);
}
typedef struct { P *p; qa_actor_id actor; const qa_mod_pickup_rule *definition; Entry *entry; qa_original_pickup_offer offer; qa_original_pickup_execution observer; } RunCtx;
static bool accepts_fn(double v) { if (!isfinite(v)) qa_range_error("QC pickup returned a non-finite source result"); return v != 0; }
static qa_original_pickup_decision run_fn(void *ctx) {
  RunCtx *r = ctx; P *p = r->p;
  qa_original_pickup_offer *offer = &r->offer;
  double count = offer->count.is_override ? offer->count.amount : 0;
  double time = offer->time.kind == QA_SOURCE_TIME_SECONDS ? offer->time.value : offer->time.value / 1000;
  if (!isfinite(count) || qa_float32(count) != count || !isfinite((double)qa_float32(time))) qa_range_error("QC pickup input exceeds its source scalar ABI");
  static qa_actor_id self_id, other_id; self_id = r->actor; other_id = offer->pickup;
  qa_compat_qc_mod_input_entry inputs[7]; memset(inputs,0,sizeof inputs);
  inputs[0].name="self"; inputs[0].value.kind="actor"; inputs[0].value.actor_value=&self_id;
  inputs[1].name="other"; inputs[1].value.kind="actor"; inputs[1].value.actor_value=&other_id;
  inputs[2].name="item"; inputs[2].value.kind="string"; inputs[2].value.string_value=offer->item;
  inputs[3].name="time"; inputs[3].value.kind="float"; inputs[3].value.number_value=time;
  inputs[4].name="pickup-count"; inputs[4].value.kind="float"; inputs[4].value.number_value=count;
  inputs[5].name="pickup-has-count"; inputs[5].value.kind="float"; inputs[5].value.number_value=offer->count.is_override?1:0;
  inputs[6].name="pickup-dropped"; inputs[6].value.kind="float"; inputs[6].value.number_value=offer->dropped?1:0;
  const qa_original_pickup_operation *operation = &r->definition->operation;
  if (operation->is_gate_then_grant) {
    double gate = p->operations.invoke(p->operations.ctx, *(const qa_mod_source_call *)operation->gate, inputs, 7);
    if (!accepts_fn(gate) || !r->observer.current(r->observer.current_ctx) || !current_of(p, r->actor, r->entry) || !qa_session_actor_registry_is_live(p->services->actors, offer->pickup))
      return QA_ORIGINAL_PICKUP_REFUSED;
  }
  double result = p->operations.invoke(p->operations.ctx, *(const qa_mod_source_call *)operation->grant, inputs, 7);
  bool always = operation->is_gate_then_grant && operation->grant_accepts && strcmp(operation->grant_accepts, "always") == 0;
  return (always || accepts_fn(result)) ? QA_ORIGINAL_PICKUP_ACCEPTED : QA_ORIGINAL_PICKUP_REFUSED;
}
static qa_original_pickup_decision take_fn(void *ctx, qa_original_pickup_offer offer, qa_original_pickup_execution observer) {
  TakeCtx *t = ctx; P *p = t->p; qa_actor_id actor = t->actor;
  Entry *entry = find_entry(p, actor);
  bool offered_has = false; for(size_t i=0;i<t->definition->offered_count;i++) if(strcmp(t->definition->offered[i], offer.item)==0) offered_has=true;
  if (!entry || !observer.current(observer.current_ctx) || !current_of(p, actor, entry) || !actor_eq(offer.recipient, actor)
      || !qa_session_actor_registry_is_live(p->services->actors, offer.pickup) || !offered_has)
    qa_range_error("QC pickup invocation differs from its admitted source binding");
  RunCtx run = {p, actor, t->definition, entry, offer, observer};
  return p->operations.watch(p->operations.ctx, actor, observer, &run, run_fn);
}
static qa_original_pickup_rule *actor_rules(P *p, qa_actor_id actor, size_t *out_count) {
  Entry *entry = find_entry(p, actor);
  if (entry && entry->rules) { *out_count = entry->rule_count; return entry->rules; }
  /* rules are cached on a side entry even before admit: use entry if present else build transient in entry created by admit path.
     TS caches in a separate map; here rules live on the entry, built on first admit/protection call. */
  if (!entry) {
    /* build into a new placeholder entry */
    if (p->entry_count==p->entry_capacity){ size_t c=p->entry_capacity?p->entry_capacity*2:4; p->entries=realloc(p->entries,c*sizeof *p->entries); if(!p->entries) qa_range_error("pickup allocation failed"); p->entry_capacity=c; }
    p->entries[p->entry_count] = (Entry){0}; p->entries[p->entry_count].actor = actor; entry = &p->entries[p->entry_count++];
  }
  size_t n = p->declaration.pickup_count;
  entry->rules = calloc(n ? n : 1, sizeof *entry->rules); entry->rule_count = n;
  if (!entry->rules) qa_range_error("pickup allocation failed");
  for (size_t i = 0; i < n; i++) {
    const qa_mod_pickup_rule *def = &p->declaration.pickups[i];
    entry->rules[i] = (qa_original_pickup_rule){take_for(p, actor, def), def->id, def->offered, def->offered_count, def->writes, take_fn};
  }
  *out_count = n; return entry->rules;
}
void qa_compat_qc_mod_pickups_init(P *p, qa_mod_callback_declaration decl, qa_provider_id provider, qa_mod_host_services *services, qa_compat_qc_mod_pickups_operations ops) {
  memset(p,0,sizeof *p); p->declaration=decl; p->provider=provider; p->services=services; p->operations=ops; p->self_ref=p;
}
void qa_compat_qc_mod_pickups_destroy(P *p) {
  for (size_t i=0;i<p->entry_count;i++){ free(p->entries[i].rules); free(p->entries[i].pickup_rules); }
  free(p->entries);
  for (size_t i=0;i<p->take_count;i++) free(p->take_storage[i]);
  free(p->take_storage); memset(p,0,sizeof *p);
}
void qa_compat_qc_mod_pickups_release(P *p, qa_actor_id actor) {
  Entry *entry = find_entry(p, actor); if (!entry) return;
  for (size_t i = 0; i < entry->remove_count; i++) entry->remove[i](entry->remove_ctx[i]);
  free(entry->rules); free(entry->pickup_rules);
  size_t idx = (size_t)(entry - p->entries);
  memmove(&p->entries[idx], &p->entries[idx+1], (p->entry_count-idx-1)*sizeof *p->entries); p->entry_count--;
}
void qa_compat_qc_mod_pickups_admit(P *p, qa_actor_id actor) {
  Entry *existing = find_entry(p, actor);
  if (existing && existing->remove_count) return;
  qa_owned_actor owner; qa_client_id client; qa_actor_id back;
  if (!p->operations.resolve_owned(p->operations.ctx, actor, &owner) || !p->operations.client_of(p->operations.ctx, actor, &client)
      || !p->operations.actor_of_client(p->operations.ctx, client, &back) || !actor_eq(back, actor))
    qa_range_error("QC pickups require a live canonical client");
  size_t rule_count = 0; qa_original_pickup_rule *rules = actor_rules(p, actor, &rule_count);
  Entry *entry = find_entry(p, actor);
  entry->client = client;
  size_t in_n = 0;
  free(entry->pickup_rules); entry->pickup_rules = calloc(rule_count ? rule_count : 1, sizeof *entry->pickup_rules);
  if (!entry->pickup_rules) qa_range_error("pickup allocation failed");
  for (size_t i = 0; i < rule_count; i++) {
    bool inv = false; for (size_t j = 0; j < rules[i].writes.count; j++) if (rules[i].writes.values[j].kind == QA_PICKUP_RESOURCE_INVENTORY) inv = true;
    if (inv) entry->pickup_rules[in_n++] = rules[i];
  }
  if (in_n) {
    void (*remove)(void *) = NULL; void *remove_ctx = NULL;
    p->operations.bind_pickup(p->operations.ctx, owner, entry->pickup_rules, in_n, &remove, &remove_ctx);
    if (remove && entry->remove_count < 8) { entry->remove[entry->remove_count] = remove; entry->remove_ctx[entry->remove_count] = remove_ctx; entry->remove_count++; }
  }
}
size_t qa_compat_qc_mod_pickups_protection(P *p, qa_actor_id actor, qa_protection_channel channel, const qa_original_pickup_rule **out, size_t cap) {
  size_t rule_count = 0; qa_original_pickup_rule *rules = actor_rules(p, actor, &rule_count);
  size_t n = 0;
  for (size_t i = 0; i < rule_count; i++) {
    bool hit = false; for (size_t j = 0; j < rules[i].writes.count; j++) if (rules[i].writes.values[j].kind == QA_PICKUP_RESOURCE_PROTECTION && rules[i].writes.values[j].channel == channel) hit = true;
    if (hit) { if (n < cap) out[n] = &rules[i]; n++; }
  }
  return n;
}
void qa_compat_qc_mod_pickups_close(P *p) {
  while (p->entry_count) qa_compat_qc_mod_pickups_release(p, p->entries[0].actor);
}
