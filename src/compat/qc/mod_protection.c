/* Port of src/compat/qc/mod-protection.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_protection.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
struct ProtLane { const char *channel; const qa_mod_qc_protection *definition; qa_protection_reservation reservation; };
struct ProtEntry { qa_actor_id actor; qa_client_id client; struct ProtLane *lanes; size_t lane_count; bool bound; };
typedef struct { bool has_regular; qa_regular_armor_state regular; bool has_powered; qa_powered_protection_state powered; } Projection;
struct ProtStage { qa_actor_id actor; qa_protection_observer observer; Projection before; };
typedef qa_compat_qc_mod_protection P;
static bool actor_eq(qa_actor_id a, qa_actor_id b){return qa_same_actor(a, b);}
static bool client_eq(qa_client_id a, qa_client_id b){return qa_same_client(a, b);}
/* q1qc view builder (program data only) */
static qa_q1qc_program qv; static qa_q1qc_definition *qv_g,*qv_f; static qa_q1qc_function *qv_fn; static qa_q1qc_statement *qv_st;
static const qa_q1qc_program *qview(qa_compat_qc_program *p){
  free(qv_g);free(qv_f);free(qv_fn);free(qv_st);
  qv_g=calloc(p->global_count?p->global_count:1,sizeof *qv_g); qv_f=calloc(p->field_count?p->field_count:1,sizeof *qv_f);
  qv_fn=calloc(p->function_count?p->function_count:1,sizeof *qv_fn); qv_st=calloc(p->statement_count?p->statement_count:1,sizeof *qv_st);
  for(size_t i=0;i<p->global_count;i++) qv_g[i]=(qa_q1qc_definition){p->globals[i].type,p->globals[i].native_type,p->globals[i].save,p->globals[i].offset,p->globals[i].name};
  for(size_t i=0;i<p->field_count;i++) qv_f[i]=(qa_q1qc_definition){p->fields[i].type,p->fields[i].native_type,p->fields[i].save,p->fields[i].offset,p->fields[i].name};
  for(size_t i=0;i<p->function_count;i++) qv_fn[i]=(qa_q1qc_function){p->functions[i].index,p->functions[i].first_statement,p->functions[i].parameter_start,p->functions[i].local_words,p->functions[i].name,p->functions[i].file,p->functions[i].parameter_sizes,p->functions[i].parameter_size_count,p->functions[i].named_builtin};
  for(size_t i=0;i<p->statement_count;i++) qv_st[i]=(qa_q1qc_statement){(qa_q1qc_opcode)p->statements[i].opcode,p->statements[i].a,p->statements[i].b,p->statements[i].c};
  qv=(qa_q1qc_program){p->source,p->api.kind==QA_QUAKEC_API_Q1_QUAKEWORLD?"q1-quakeworld":"q1-netquake",qv_st,p->statement_count,qv_g,p->global_count,qv_f,p->field_count,qv_fn,p->function_count,p->strings,p->string_length,p->initial_globals,p->initial_globals_length,p->entity_field_words,p->checksum,p->digest};
  return &qv;
}
void qa_compat_qc_protection_regions_free(qa_q1qc_armor_stage **regions, size_t count){
  for(size_t i=0;i<count;i++) { qa_q1qc_armor_stage_destroy(regions[i]); } free(regions);
}
qa_q1qc_armor_stage **qa_compat_qc_protection_regions(qa_compat_qc_program *program, qa_mod_callback_declaration declaration, size_t *out_count) {
  qa_q1qc_armor_stage **regions = NULL; size_t rn = 0, cap = 0;
  bool seen_regular=false, seen_powered=false;
  for (size_t di = 0; di < declaration.protection_count; di++) {
    const qa_mod_qc_protection *definition = &declaration.protection[di];
    bool reg = strcmp(definition->channel, "regular") == 0;
    if (!declaration.clients || (reg ? seen_regular : seen_powered)) qa_range_error("QC protection requires clients and one declaration per channel");
    if (reg) seen_regular = true; else seen_powered = true;
    const char *count_name = reg ? definition->storage_points : definition->storage_cells;
    const char *names[2] = {count_name, definition->has_selection ? definition->selection_field : NULL};
    for (int ni = 0; ni < 2; ni++) { const char *name = names[ni]; if (!name) break;
      const qa_compat_qc_definition *f = qa_compat_qc_program_field_named(program, name);
      bool priv = false; for(size_t j=0;j<declaration.actor_field_count;j++) if(strcmp(declaration.actor_fields[j].field,name)==0 && strcmp(declaration.actor_fields[j].binding,"private")==0) priv=true;
      if (!f || strcmp(f->type,"float")!=0 || !priv) qa_range_error("QC protection requires private float storage %s", name); }
    size_t val_count = reg ? definition->item_value_count : definition->kind_value_count;
    if (definition->has_selection) {
      bool bad = strcmp(definition->selection_field, count_name)==0 || val_count==0;
      for(size_t i=0;i<val_count && !bad;i++){ double v = reg?definition->item_values[i].value:definition->kind_values[i].value;
        if(!isfinite(v)||qa_float32(v)!=v) bad=true;
        for(size_t j=i+1;j<val_count;j++){ double w=reg?definition->item_values[j].value:definition->kind_values[j].value; if(w==v) bad=true; } }
      if (bad) qa_range_error("QC protection selection is not representable");
      if (definition->has_selection_mask) { double mask = definition->selection_mask; bool bad2 = mask != trunc(mask) || mask <= 0 || mask > 0x7fffff;
        for(size_t i=0;i<val_count;i++){ double v=reg?definition->item_values[i].value:definition->kind_values[i].value; if(v!=trunc(v) || (((int64_t)v)&(int64_t)mask)!=(int64_t)v) bad2=true; }
        if (bad2) qa_range_error("QC protection selection mask is invalid"); }
    }
    double flagv[5] = {definition->flags.no_armor, definition->flags.no_power_armor, definition->flags.no_regular_armor, definition->flags.energy, definition->flags.radius};
    for(int i=0;i<5;i++) if(flagv[i]!=trunc(flagv[i])||flagv[i]<0||flagv[i]>0x7fffff) qa_range_error("QC protection flags exceed source integer precision");
    if (definition->absorb_is_region) {
      qa_q1qc_armor_stage *region = qa_q1qc_armor_stage_create(qview(program), &definition->absorb_stage);
      if (!region || !region->region.has_standalone || strcmp(definition->absorb_call.function, region->source.function)!=0)
        qa_range_error("QC donor region lacks a qualified standalone frame");
      const qa_compat_qc_function *fn = qa_compat_qc_program_function_named(program, definition->absorb_call.function);
      /* parameter words -> declared arguments */
      int64_t word = fn->parameter_start;
      typedef struct { int64_t word; qa_mod_callback_value value; } ArgAt; ArgAt args[8]; size_t an=0;
      for(size_t i=0;i<fn->parameter_size_count;i++){ if(i<definition->absorb_call.argument_count) args[an++]=(ArgAt){word, definition->absorb_call.arguments[i]}; word += fn->parameter_sizes[i]; }
      const struct { int64_t word; const char *name; } req[3] = {{definition->absorb_stage.target, "self"}, {definition->absorb_stage.damage, "amount"}, {(int64_t)definition->absorb_stage.flags.word, "damage-flags"}};
      size_t req_n = definition->absorb_stage.flags.is_bits ? 3 : 2;
      for(size_t i=0;i<req_n;i++){ bool ok=false; for(size_t j=0;j<an;j++) if(args[j].word==req[i].word && strcmp(args[j].value.kind,"input")==0 && strcmp(args[j].value.name,req[i].name)==0) ok=true;
        if(!ok) qa_range_error("QC donor region must initialize %s from its current stage", req[i].name); }
      if(rn==cap){ cap=cap?cap*2:4; regions=realloc(regions,cap*sizeof *regions); if(!regions) { qa_range_error("protection allocation failed"); } }
      regions[rn++]=region;
    }
  }
  *out_count = rn; return regions;
}
static struct ProtEntry *find_entry(P *p, qa_actor_id a){ for(size_t i=0;i<p->entry_count;i++){ if(actor_eq(p->entries[i].actor,a)) return &p->entries[i]; } return NULL; }
static struct ProtEntry *require_entry(P *p, qa_actor_id actor) {
  struct ProtEntry *entry = find_entry(p, actor);
  qa_client_id client; bool has_client = false; qa_actor_id back; bool has_back = false;
  const qa_mod_client_services *clients = (const qa_mod_client_services *)p->services->clients;
  if (entry) { has_client = clients && clients->for_actor(clients->ctx, actor, &client);
    has_back = has_client && clients->actor(clients->ctx, entry->client, &back); }
  if (!entry || !qa_session_actor_registry_is_live(p->services->actors, actor) || !clients || !has_client || !client_eq(client, entry->client) || !has_back || !actor_eq(back, actor))
    qa_range_error("QC protection client is retired");
  return entry;
}
void qa_compat_qc_mod_protection_init(P *p, qa_mod_callback_declaration decl, qa_provider_id provider, qa_mod_host_services *services, qa_compat_qc_mod_protection_operations ops) {
  memset(p,0,sizeof *p); p->declaration=decl; p->provider=provider; p->services=services; p->operations=ops; p->self_ref=p;
  p->regions = qa_compat_qc_protection_regions(ops.machine->program, decl, &p->region_count);
}
void qa_compat_qc_mod_protection_destroy(P *p) {
  qa_compat_qc_mod_protection_close(p);
  qa_compat_qc_protection_regions_free(p->regions, p->region_count);
  free(p->stages); free(p->entries);
  for(size_t i=0;i<p->bind_count;i++) free(p->bind_storage[i]);
  free(p->bind_storage); memset(p,0,sizeof *p);
}
void qa_compat_qc_mod_protection_reserve(P *p, qa_actor_id actor) {
  if (find_entry(p, actor)) { require_entry(p, actor); return; }
  qa_owned_actor owner; bool has_owner = qa_session_actor_registry_resolve_owned(p->services->actors, actor, &owner);
  const qa_mod_client_services *clients = (const qa_mod_client_services *)p->services->clients;
  qa_client_id client; bool has_client = clients && clients->for_actor(clients->ctx, actor, &client);
  qa_actor_id back; bool live = has_client && clients->actor(clients->ctx, client, &back) && actor_eq(back, actor);
  if (!has_owner || !has_client || !live) qa_range_error("QC protection requires a live canonical client");
  struct ProtLane *lanes = calloc(p->declaration.protection_count ? p->declaration.protection_count : 1, sizeof *lanes);
  if (!lanes) qa_range_error("protection allocation failed");
  for (size_t i = 0; i < p->declaration.protection_count; i++) {
    const qa_mod_qc_protection *definition = &p->declaration.protection[i];
    qa_protection_claim claim; memset(&claim,0,sizeof claim); claim.owner=p->provider; claim.rule=definition->id;
    claim.admission.kind = definition->admission.kind ? definition->admission.kind : "claim"; claim.admission.owner = definition->admission.owner;
    bool reg = strcmp(definition->channel, "regular") == 0;
    lanes[i] = (struct ProtLane){definition->channel, definition,
      qa_gameplay_authority_reserve_protection(p->services->combat, owner, reg ? QA_PROTECTION_REGULAR : QA_PROTECTION_POWERED, claim)};
  }
  if (p->entry_count==p->entry_capacity){ size_t c=p->entry_capacity?p->entry_capacity*2:4; p->entries=realloc(p->entries,c*sizeof *p->entries); if(!p->entries) qa_range_error("protection allocation failed"); p->entry_capacity=c; }
  p->entries[p->entry_count++] = (struct ProtEntry){actor, client, lanes, p->declaration.protection_count, false};
}
static qa_compat_qc_words *prot_words(P *p, qa_actor_id actor) {
  require_entry(p, actor);
  return qa_compat_qc_entity_memory_from_reference(p->operations.machine->entities, p->operations.reference(p->operations.ctx, actor));
}
static double prot_count(P *p, qa_actor_id actor, const char *name, bool has_next, double next) {
  qa_compat_qc_words *words = prot_words(p, actor);
  int64_t offset = qa_compat_qc_machine_field_offset(p->operations.machine, name);
  if (has_next) qa_compat_qc_words_set_float(words, offset, next);
  return qa_compat_qc_words_float(words, offset);
}
static double prot_selected(P *p, qa_actor_id actor, const qa_mod_qc_protection *def) {
  double value = prot_count(p, actor, def->selection_field, false, 0);
  return def->has_selection_mask ? (double)((int64_t)trunc(value) & (int64_t)def->selection_mask) : value;
}
static qa_regular_armor_state regular_read(P *p, qa_actor_id actor, const qa_mod_qc_protection *def) {
  double points = prot_count(p, actor, def->storage_points, false, 0);
  qa_item_id item = def->storage_item;
  if (def->has_selection) { item = NULL; double sel = prot_selected(p, actor, def);
    for(size_t i=0;i<def->item_value_count;i++) if(def->item_values[i].value==sel) item = def->item_values[i].item; }
  if (!item) { if (!def->has_selection && !def->storage_item) qa_range_error("QC regular armor selection is undeclared"); if (def->has_selection) qa_range_error("QC regular armor selection is undeclared"); }
  qa_regular_armor_state r; memset(&r,0,sizeof r); r.kind = QA_REGULAR_ARMOR_SOURCE; r.points = points; r.item = item; return r;
}
static qa_powered_protection_state powered_read(P *p, qa_actor_id actor, const qa_mod_qc_protection *def) {
  const char *kind = def->storage_kind;
  if (def->has_selection) { kind = NULL; double sel = prot_selected(p, actor, def);
    for(size_t i=0;i<def->kind_value_count;i++) if(def->kind_values[i].value==sel) kind = def->kind_values[i].kind; }
  if (!kind) qa_range_error("QC powered armor selection is undeclared");
  return (qa_powered_protection_state){kind, strcmp(kind,"none")==0 ? 0 : prot_count(p, actor, def->storage_cells, false, 0)};
}
static void valid_count(double count) { if (count < 0 || !isfinite(count) || qa_float32(count) != count) qa_range_error("QC protection count is not representable"); }
static void validate_regular(P *p, qa_actor_id actor, const qa_mod_qc_protection *def, qa_regular_armor_state next) {
  require_entry(p, actor);
  if (next.kind != QA_REGULAR_ARMOR_SOURCE) qa_range_error("QC independent regular armor requires its source projection");
  valid_count(next.points);
  bool ok;
  if (!def->has_selection) ok = (next.item == def->storage_item) || (next.item && def->storage_item && strcmp(next.item, def->storage_item)==0);
  else { ok=false; for(size_t i=0;i<def->item_value_count;i++) if(next.item && def->item_values[i].item && strcmp(def->item_values[i].item,next.item)==0) ok=true; }
  if (!ok) qa_range_error("QC regular armor item is undeclared");
}
static void validate_powered(P *p, qa_actor_id actor, const qa_mod_qc_protection *def, qa_powered_protection_state next) {
  require_entry(p, actor); valid_count(strcmp(next.kind,"none")==0 ? 0 : next.cells);
  bool ok;
  if (!def->has_selection) ok = strcmp(next.kind, def->storage_kind)==0;
  else { ok=false; for(size_t i=0;i<def->kind_value_count;i++) if(strcmp(def->kind_values[i].kind,next.kind)==0) ok=true; }
  if (!ok) qa_range_error("QC powered armor kind is undeclared");
}
static void prot_select(P *p, qa_actor_id actor, const qa_mod_qc_protection *def, double next) {
  prot_count(p, actor, def->selection_field, true, def->has_selection_mask
    ? (double)(((int64_t)trunc(prot_count(p, actor, def->selection_field, false, 0)) & ~(int64_t)def->selection_mask) | (int64_t)next) : next);
}
static Projection projection_of(P *p, qa_actor_id actor) {
  struct ProtEntry *entry = require_entry(p, actor);
  Projection r; memset(&r,0,sizeof r);
  for(size_t i=0;i<entry->lane_count;i++){
    if (strcmp(entry->lanes[i].channel,"regular")==0){ r.has_regular=true; r.regular=regular_read(p,actor,entry->lanes[i].definition); }
    else { r.has_powered=true; r.powered=powered_read(p,actor,entry->lanes[i].definition); }
  }
  return r;
}
static void rebase(P *p, qa_actor_id actor) {
  Projection state = projection_of(p, actor);
  for(size_t i=0;i<p->stage_count;i++) if(actor_eq(p->stages[i].actor,actor)) p->stages[i].before = state;
}
struct ProtBind { P *p; qa_actor_id actor; const qa_mod_qc_protection *definition; };
typedef struct ProtBind BindCtx;
static BindCtx *bind_for(P *p, qa_actor_id actor, const qa_mod_qc_protection *def) {
  for(size_t i=0;i<p->bind_count;i++) if(actor_eq(p->bind_storage[i]->actor,actor)&&p->bind_storage[i]->definition==def) return p->bind_storage[i];
  if(p->bind_count==p->bind_capacity){ size_t c=p->bind_capacity?p->bind_capacity*2:8; p->bind_storage=realloc(p->bind_storage,c*sizeof *p->bind_storage); if(!p->bind_storage) qa_range_error("protection allocation failed"); p->bind_capacity=c; }
  BindCtx *cell=malloc(sizeof *cell); if(!cell) qa_range_error("protection allocation failed");
  *cell=(BindCtx){p,actor,def}; p->bind_storage[p->bind_count++]=cell; return cell;
}
static qa_regular_armor_state b_read_regular(void *ctx){ BindCtx *b=ctx; return regular_read(b->p,b->actor,b->definition); }
static qa_powered_protection_state b_read_powered(void *ctx){ BindCtx *b=ctx; return powered_read(b->p,b->actor,b->definition); }
static void b_validate_regular(void *ctx, qa_regular_armor_state next){ BindCtx *b=ctx; validate_regular(b->p,b->actor,b->definition,next); }
static void b_validate_powered(void *ctx, qa_powered_protection_state next){ BindCtx *b=ctx; validate_powered(b->p,b->actor,b->definition,next); }
static void b_write_regular(void *ctx, qa_regular_armor_state next){
  BindCtx *b=ctx; P *p=b->p; const qa_mod_qc_protection *def=b->definition;
  validate_regular(p,b->actor,def,next);
  if (def->has_selection && next.kind==QA_REGULAR_ARMOR_SOURCE){
    bool found=false; double v=0; for(size_t i=0;i<def->item_value_count;i++) if(next.item&&def->item_values[i].item&&strcmp(def->item_values[i].item,next.item)==0){v=def->item_values[i].value;found=true;}
    if(!found) qa_range_error("Missing validated QC armor selection");
    prot_select(p,b->actor,def,v);
  }
  prot_count(p,b->actor,def->storage_points,true,next.kind==QA_REGULAR_ARMOR_NONE?0:next.points); rebase(p,b->actor);
}
static void b_write_powered(void *ctx, qa_powered_protection_state next){
  BindCtx *b=ctx; P *p=b->p; const qa_mod_qc_protection *def=b->definition;
  validate_powered(p,b->actor,def,next);
  if (def->has_selection){
    bool found=false; double v=0; for(size_t i=0;i<def->kind_value_count;i++) if(strcmp(def->kind_values[i].kind,next.kind)==0){v=def->kind_values[i].value;found=true;}
    if(!found) qa_range_error("Missing validated QC power selection");
    prot_select(p,b->actor,def,v);
  }
  prot_count(p,b->actor,def->storage_cells,true,strcmp(next.kind,"none")==0?0:next.cells); rebase(p,b->actor);
}
static qa_armor_stage_result absorb_impl(P *p, qa_actor_id actor, const qa_mod_qc_protection *definition, qa_armor_stage_input input, qa_protection_observer observer) {
  require_entry(p, actor);
  qa_mod_qc_protection_flags flags = definition->flags;
  double scale = input.flags.has_regular_protection_scale ? input.flags.regular_protection_scale : 1;
  bool reg = strcmp(definition->channel,"regular")==0;
  if (reg && scale != 1) {
    bool has=false;
    for(size_t i=0;i<definition->absorb_call.argument_count;i++) if(strcmp(definition->absorb_call.arguments[i].kind,"input")==0&&strcmp(definition->absorb_call.arguments[i].name,"regular-protection-scale")==0) has=true;
    for(size_t i=0;i<definition->absorb_call.global_count;i++) if(strcmp(definition->absorb_call.globals[i].value.kind,"input")==0&&strcmp(definition->absorb_call.globals[i].value.name,"regular-protection-scale")==0) has=true;
    if(!has) qa_range_error("QC regular protection scale requires an explicit source input");
  }
  int64_t damage_flags = (input.flags.no_armor?(int64_t)flags.no_armor:0)|(input.flags.no_power_armor?(int64_t)flags.no_power_armor:0)
    |(input.flags.no_regular_armor?(int64_t)flags.no_regular_armor:0)|(input.flags.energy?(int64_t)flags.energy:0)
    |(strcmp(input.request.delivery,"radius")==0?(int64_t)flags.radius:0);
  qa_source_time now = p->services->time(p->services->ctx);
  double now_seconds = now.kind==QA_SOURCE_TIME_SECONDS?now.value:now.value/1000;
  static qa_actor_id self_id; self_id = actor;
  qa_compat_qc_mod_input_entry inputs[11]; memset(inputs,0,sizeof inputs);
  const struct { const char *name; } names[11] = {{"self"},{"attacker"},{"inflictor"},{"amount"},{"damage-flags"},{"knockback"},{"direction"},{"regular-protection-scale"},{"point"},{"normal"},{"time"}};
  for(int i=0;i<11;i++) inputs[i].name = names[i].name;
  inputs[0].value.kind="actor"; inputs[0].value.actor_value=&self_id;
  inputs[1].value.kind="actor"; inputs[1].value.actor_value=input.request.attack.attacker;
  inputs[2].value.kind="actor"; inputs[2].value.actor_value=input.request.attack.inflictor;
  inputs[3].value.kind="float"; inputs[3].value.number_value=input.amount;
  inputs[4].value.kind="float"; inputs[4].value.number_value=(double)damage_flags;
  inputs[5].value.kind="float"; inputs[5].value.number_value=input.request.knockback;
  inputs[6].value.kind="vector"; inputs[6].value.vector_value=input.direction;
  inputs[7].value.kind="float"; inputs[7].value.number_value=scale;
  inputs[8].value.kind="vector"; inputs[8].value.vector_value=input.point;
  inputs[9].value.kind="vector"; inputs[9].value.vector_value=input.normal;
  inputs[10].value.kind="float"; inputs[10].value.number_value=now_seconds;
  /* Inline watch: push stage, invoke, pop */
  bool pushed = find_entry(p, actor) != NULL;
  if (pushed) {
    if (p->stage_count==p->stage_capacity){ size_t c=p->stage_capacity?p->stage_capacity*2:4; p->stages=realloc(p->stages,c*sizeof *p->stages); if(!p->stages) qa_range_error("protection allocation failed"); p->stage_capacity=c; }
    p->stages[p->stage_count++] = (struct ProtStage){actor, observer, projection_of(p, actor)};
  }
  const qa_q1qc_armor_stage *region = NULL;
  if (definition->absorb_is_region) for(size_t i=0;i<p->region_count;i++) if(p->regions[i]->region.entry==definition->absorb_stage.entry) region=p->regions[i];
  double saved = p->operations.invoke(p->operations.ctx, definition->absorb_call, inputs, 11, region);
  if (pushed) p->stage_count--;
  if (qa_session_actor_registry_is_live(p->services->actors, actor) && find_entry(p, actor)) require_entry(p, actor);
  return (qa_armor_stage_result){saved};
}
static qa_armor_stage_result b_absorb(void *ctx, qa_armor_stage_input input, qa_protection_observer observer){ BindCtx *b=ctx; return absorb_impl(b->p,b->actor,b->definition,input,observer); }
void qa_compat_qc_mod_protection_activate(P *p, qa_actor_id actor) {
  struct ProtEntry *entry = require_entry(p, actor); if (entry->bound) return;
  for (size_t i = 0; i < entry->lane_count; i++) {
    struct ProtLane *lane = &entry->lanes[i];
    const qa_mod_qc_protection *definition = lane->definition;
    bool reg = strcmp(lane->channel, "regular") == 0;
    qa_protection_claim claim; memset(&claim,0,sizeof claim); claim.owner=p->provider; claim.rule=definition->id; claim.admission.kind = definition->admission.kind ? definition->admission.kind : "claim"; claim.admission.owner = definition->admission.owner;
    const qa_original_pickup_rule *pickup_rules[16]; size_t pickup_count = 0;
    if (p->operations.pickups) pickup_count = p->operations.pickups(p->operations.ctx, actor, reg ? QA_PROTECTION_REGULAR : QA_PROTECTION_POWERED, pickup_rules, 16);
    /* load pickup rules as pointer array of borrowed rules: authority copies ctx bindings by value */
    qa_original_pickup_rule flat[16]; for(size_t j=0;j<pickup_count;j++) flat[j]=*pickup_rules[j];
    BindCtx *b = bind_for(p, actor, definition);
    qa_protection_binding binding; memset(&binding,0,sizeof binding);
    binding.ctx=b; binding.claim=claim; binding.channel=reg?QA_PROTECTION_REGULAR:QA_PROTECTION_POWERED;
    binding.inventory_items=NULL; binding.inventory_item_count=0;
    binding.pickups=flat; binding.pickup_count=pickup_count;
    binding.read_regular=b_read_regular; binding.read_powered=b_read_powered;
    binding.validate_write_regular=b_validate_regular; binding.validate_write_powered=b_validate_powered;
    binding.write_regular=b_write_regular; binding.write_powered=b_write_powered;
    binding.absorb=b_absorb;
    lane->reservation.bind(lane->reservation.ctx, binding);
  }
  entry->bound = true;
}
static bool regular_equal(qa_regular_armor_state a, qa_regular_armor_state b){ return a.kind==b.kind && a.points==b.points && ((a.item==b.item)||(a.item&&b.item&&strcmp(a.item,b.item)==0)); }
static bool powered_equal(qa_powered_protection_state a, qa_powered_protection_state b){ return strcmp(a.kind,b.kind)==0 && a.cells==b.cells; }
void qa_compat_qc_mod_protection_observe(P *p, qa_compat_qc_entity_store_observation store) {
  if (p->stage_count == 0) return;
  qa_actor_id actor = p->operations.actor(p->operations.ctx, store.reference);
  bool any=false; for(size_t i=0;i<p->stage_count;i++) if(actor_eq(p->stages[i].actor,actor)) any=true;
  Projection after; memset(&after,0,sizeof after); if (any) after = projection_of(p, actor);
  struct ProtStage *current = NULL;
  for(size_t i=0;i<p->stage_count;i++) if(actor_eq(p->stages[i].actor,actor)) current=&p->stages[i];
  for(size_t i=0;i<p->stage_count;i++) if(actor_eq(p->stages[i].actor,actor)){
    Projection before = p->stages[i].before; p->stages[i].before = after;
    if (&p->stages[i] != current) continue;
    qa_protection_store changed; memset(&changed,0,sizeof changed);
    bool has_reg = before.has_regular && after.has_regular && !regular_equal(before.regular, after.regular);
    bool has_pow = before.has_powered && after.has_powered && !powered_equal(before.powered, after.powered);
    if (has_reg){ changed.has_regular=true; changed.regular_before=before.regular; changed.regular_after=after.regular; }
    if (has_pow){ changed.has_powered=true; changed.powered_before=before.powered; changed.powered_after=after.powered; }
    if (has_reg || has_pow) p->stages[i].observer.stored(p->stages[i].observer.ctx, changed);
  }
}
double qa_compat_qc_mod_protection_watch(P *p, qa_actor_id actor, qa_protection_observer observer, void *ctx, double (*run)(void *)) {
  if (!find_entry(p, actor)) return run(ctx);
  if (p->stage_count==p->stage_capacity){ size_t c=p->stage_capacity?p->stage_capacity*2:4; p->stages=realloc(p->stages,c*sizeof *p->stages); if(!p->stages) qa_range_error("protection allocation failed"); p->stage_capacity=c; }
  p->stages[p->stage_count++] = (struct ProtStage){actor, observer, projection_of(p, actor)};
  double result = run(ctx);
  p->stage_count--;
  return result;
}
void qa_compat_qc_mod_protection_release(P *p, qa_actor_id actor) {
  struct ProtEntry *entry = find_entry(p, actor); if (!entry) return;
  for(size_t i=0;i<entry->lane_count;i++) entry->lanes[i].reservation.close(entry->lanes[i].reservation.ctx);
  free(entry->lanes);
  size_t idx=(size_t)(entry-p->entries);
  memmove(&p->entries[idx],&p->entries[idx+1],(p->entry_count-idx-1)*sizeof *p->entries); p->entry_count--;
}
void qa_compat_qc_mod_protection_close(P *p) {
  while (p->entry_count) qa_compat_qc_mod_protection_release(p, p->entries[0].actor);
}
void qa_compat_qc_mod_protection_assert_idle(P *p) {
  if (p->stage_count) qa_range_error("QC protection requires an idle source stage");
}
