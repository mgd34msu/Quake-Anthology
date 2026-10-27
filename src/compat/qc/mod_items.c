/* Port of src/compat/qc/mod-items.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_items.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/item_icon.h"
#include "qa/content/q1/quakec/weapon_stage.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
typedef qa_compat_qc_mod_items I;
static bool actor_eq(qa_actor_id a, qa_actor_id b){return qa_same_actor(a, b);}
static int64_t check_field(qa_compat_qc_program *p, qa_mod_callback_declaration decl, const char *name, const char *type, bool input) {
  const qa_compat_qc_definition *d = qa_compat_qc_program_field_named(p, name);
  bool declared = false;
  for (size_t i = 0; i < decl.actor_field_count; i++) if (strcmp(decl.actor_fields[i].field, name) == 0
      && (strcmp(decl.actor_fields[i].binding, "private") == 0 || (input && strcmp(decl.actor_fields[i].binding, "client-input") == 0))) declared = true;
  if (!d || strcmp(d->type, type) != 0 || !declared) qa_range_error("QC item storage %s requires declared original %s storage", name, type);
  return d->offset;
}
/* Minimal q1qc program view for weapon-stage validation (data only). */
static qa_q1qc_program qview_storage;
static qa_q1qc_definition *qview_defs, *qview_fields; static qa_q1qc_function *qview_fns; static qa_q1qc_statement *qview_stmts;
static const qa_q1qc_program *qview(qa_compat_qc_program *p) {
  free(qview_defs); free(qview_fields); free(qview_fns); free(qview_stmts);
  qview_defs = calloc(p->global_count?p->global_count:1,sizeof *qview_defs);
  qview_fields = calloc(p->field_count?p->field_count:1,sizeof *qview_fields);
  qview_fns = calloc(p->function_count?p->function_count:1,sizeof *qview_fns);
  qview_stmts = calloc(p->statement_count?p->statement_count:1,sizeof *qview_stmts);
  for (size_t i=0;i<p->global_count;i++) qview_defs[i]=(qa_q1qc_definition){p->globals[i].type,p->globals[i].native_type,p->globals[i].save,p->globals[i].offset,p->globals[i].name};
  for (size_t i=0;i<p->field_count;i++) qview_fields[i]=(qa_q1qc_definition){p->fields[i].type,p->fields[i].native_type,p->fields[i].save,p->fields[i].offset,p->fields[i].name};
  for (size_t i=0;i<p->function_count;i++) qview_fns[i]=(qa_q1qc_function){p->functions[i].index,p->functions[i].first_statement,p->functions[i].parameter_start,p->functions[i].local_words,p->functions[i].name,p->functions[i].file,p->functions[i].parameter_sizes,p->functions[i].parameter_size_count,p->functions[i].named_builtin};
  for (size_t i=0;i<p->statement_count;i++) qview_stmts[i]=(qa_q1qc_statement){(qa_q1qc_opcode)p->statements[i].opcode,p->statements[i].a,p->statements[i].b,p->statements[i].c};
  qview_storage = (qa_q1qc_program){p->source, p->api.kind==QA_QUAKEC_API_Q1_QUAKEWORLD?"q1-quakeworld":"q1-netquake", qview_stmts,p->statement_count,qview_defs,p->global_count,qview_fields,p->field_count,qview_fns,p->function_count,p->strings,p->string_length,p->initial_globals,p->initial_globals_length,p->entity_field_words,p->checksum,p->digest};
  return &qview_storage;
}
void qa_compat_qc_validate_items(qa_compat_qc_program *p, qa_mod_callback_declaration decl) {
  const qa_mod_qc_items *items = decl.items; if (!items) return;
  if (!decl.clients || items->definition_count == 0) qa_range_error("QC source items require canonical clients and definitions");
  for (size_t i = 0; i < items->definition_count; i++) {
    if (strlen(items->definitions[i].label ? items->definitions[i].label : "") == 0) qa_range_error("QC item definitions are empty or duplicated");
    for (size_t j = i + 1; j < items->definition_count; j++) if (strcmp(items->definitions[i].item, items->definitions[j].item) == 0) qa_range_error("QC item definitions are empty or duplicated");
  }
  bool *bound = calloc(items->definition_count, sizeof(bool)); size_t bound_n = 0;
  if (!bound) qa_range_error("item validation allocation failed");
  typedef struct { int64_t field; const char *kind; } Seen; Seen seen[256]; size_t seen_n = 0;
  for (size_t si = 0; si < items->storage_count; si++) {
    const qa_mod_qc_item_storage *storage = &items->storage[si];
    int64_t count_field = check_field(p, decl, storage->field, "float", false);
    for (size_t k = 0; k < seen_n; k++) if (seen[k].field == count_field) qa_range_error("QC item storage fields overlap");
    seen[seen_n++] = (Seen){count_field, "count"};
    if (strcmp(storage->kind, "counter") == 0) {
      bool found = false; for (size_t d = 0; d < items->definition_count; d++) if (strcmp(items->definitions[d].item, storage->item) == 0 && !bound[d]) { bound[d] = true; bound_n++; found = true; }
      if (!found) qa_range_error("QC item %s lacks distinct declared storage", storage->item);
      if (!storage->capacity.is_constant) {
        int64_t cap_field = check_field(p, decl, storage->capacity.field, "float", false);
        for (size_t k = 0; k < seen_n; k++) if (seen[k].field == cap_field && strcmp(seen[k].kind, "count") == 0) qa_range_error("QC item capacity overlaps source storage");
        bool dup=false; for(size_t k=0;k<seen_n;k++) if(seen[k].field==cap_field) dup=true; if(!dup) seen[seen_n++]=(Seen){cap_field,"capacity"};
      } else if (!isfinite(storage->capacity.value) || storage->capacity.value < 0 || qa_float32(storage->capacity.value) != storage->capacity.value) qa_range_error("QC item capacity exceeds its source ABI");
    } else {
      double mask = storage->private_mask;
      if (mask != trunc(mask) || mask < 0 || mask > 0xffffff || storage->item_count == 0) qa_range_error("QC packed inventory requires an exact binary32 mask");
      int64_t imask = (int64_t)mask;
      for (size_t ei = 0; ei < storage->item_count; ei++) {
        bool found = false; for (size_t d = 0; d < items->definition_count; d++) if (strcmp(items->definitions[d].item, storage->items[ei].item) == 0 && !bound[d]) { bound[d] = true; bound_n++; found = true; }
        if (!found) qa_range_error("QC item %s lacks distinct declared storage", storage->items[ei].item);
        double em = storage->items[ei].mask; int64_t emi = (int64_t)em;
        if (em != trunc(em) || emi < 1 || emi > 0x800000 || (emi & (emi - 1)) != 0 || (imask & emi) != 0) qa_range_error("QC packed inventory masks overlap or exceed source precision");
        imask |= emi;
      }
    }
  }
  if (bound_n != items->definition_count) qa_range_error("QC item definition has no source storage");
  free(bound);
  size_t weapon_n = 0; for (size_t i = 0; i < items->definition_count; i++) if (items->definitions[i].is_weapon) weapon_n++;
  if ((weapon_n == 0) == (items->weapons != NULL)) qa_range_error("QC weapon definitions require their original source consumer");
  if (items->weapons) {
    qa_q1qc_weapon_stage *stage = qa_q1qc_declared_weapon_stage(qview(p), items->weapons->stage); qa_q1qc_weapon_stage_destroy(stage);
    static const char *const think_names[2] = {"think","nextthink"};
    for (int i = 0; i < 2; i++) { bool has=false; for(size_t j=0;j<decl.actor_field_count;j++) if(strcmp(decl.actor_fields[j].field,think_names[i])==0&&strcmp(decl.actor_fields[j].binding,think_names[i])==0) has=true; if(!has) qa_range_error("QC weapons require continuing source think ownership"); }
    const struct { const char *field; const qa_mod_qc_weapon_value *values; size_t count; bool input; } maps[2] = {
      {items->weapons->selected_field, items->weapons->selected_values, items->weapons->selected_value_count, false},
      {items->weapons->select_field, items->weapons->select_values, items->weapons->select_value_count, true}};
    for (int mi = 0; mi < 2; mi++) {
      check_field(p, decl, maps[mi].field, "float", maps[mi].input);
      bool bad = maps[mi].count != weapon_n;
      for (size_t i = 0; i < maps[mi].count && !bad; i++) for (size_t j = i+1; j < maps[mi].count; j++)
        if (strcmp(maps[mi].values[i].item, maps[mi].values[j].item)==0 || maps[mi].values[i].value==maps[mi].values[j].value) bad = true;
      for (size_t i = 0; i < items->definition_count && !bad; i++) if (items->definitions[i].is_weapon) {
        bool has=false; for(size_t j=0;j<maps[mi].count;j++) if(strcmp(maps[mi].values[j].item, items->definitions[i].item)==0) has=true; if(!has) bad=true;
      }
      for (size_t i = 0; i < maps[mi].count; i++) if (!isfinite(maps[mi].values[i].value) || qa_float32(maps[mi].values[i].value) != maps[mi].values[i].value || maps[mi].values[i].value == 0) bad = true;
      if (bad) qa_range_error("QC weapon selectors differ from their definitions");
    }
    check_field(p, decl, items->weapons->model_field, "string", false);
    check_field(p, decl, items->weapons->model_frame, "float", false);
    for (size_t i = 0; i < items->definition_count; i++) if (items->definitions[i].is_weapon && items->definitions[i].weapon.ammo) {
      bool has=false; for(size_t j=0;j<items->definition_count;j++) if(strcmp(items->definitions[j].item, items->definitions[i].weapon.ammo)==0) has=true;
      for(size_t j=0;j<decl.actor_field_count;j++) if(strcmp(decl.actor_fields[j].binding,"inventory")==0 && decl.actor_fields[j].item && strcmp(decl.actor_fields[j].item, items->definitions[i].weapon.ammo)==0) has=true;
      if(!has) qa_range_error("QC weapon has no declared ammo source");
    }
  }
}
static int64_t off_of(I *items, const char *name) {
  qa_compat_qc_program *p = items->operations.machine(items->operations.ctx)->program;
  const qa_compat_qc_definition *d = qa_compat_qc_program_field_named(p, name);
  if (!d) qa_range_error("Missing admitted source field %s", name);
  return d->offset;
}
static qa_compat_qc_words *words_of(I *items, qa_actor_id actor) {
  qa_compat_qc_machine *m = items->operations.machine(items->operations.ctx);
  return qa_compat_qc_entity_memory_from_reference(m->entities, items->operations.reference(items->operations.ctx, actor));
}
static size_t read_entries(I *items, qa_actor_id actor, const qa_compat_qc_entity_store_observation *previous, const qa_mod_qc_item_storage *storages, size_t storage_count, qa_inventory_entry *out, size_t cap) {
  qa_compat_qc_words *words = words_of(items, actor); size_t n = 0;
  for (size_t si = 0; si < storage_count; si++) {
    const qa_mod_qc_item_storage *storage = &storages[si];
    /* scalar with previous snapshot */
    if (strcmp(storage->kind, "counter") == 0) {
      double count, capacity;
      int64_t coff = off_of(items, storage->field);
      if (previous) { int64_t index = (coff - previous->word) * 4; float v; if (index >= 0 && index + 4 <= (int64_t)previous->before_length) { memcpy(&v, previous->before + index, 4); count = v; } else count = qa_compat_qc_words_float(words, coff); }
      else count = qa_compat_qc_words_float(words, coff);
      if (storage->capacity.is_constant) capacity = storage->capacity.value;
      else { int64_t foff = off_of(items, storage->capacity.field);
        if (previous) { int64_t index = (foff - previous->word) * 4; float v; if (index >= 0 && index + 4 <= (int64_t)previous->before_length) { memcpy(&v, previous->before + index, 4); capacity = v; } else capacity = qa_compat_qc_words_float(words, foff); }
        else capacity = qa_compat_qc_words_float(words, foff); }
      if (n < cap) out[n] = (qa_inventory_entry){storage->item, count, capacity, true, {"source-counter", "binary32"}};
      n++;
    } else {
      int64_t foff = off_of(items, storage->field);
      double value;
      if (previous) { int64_t index = (foff - previous->word) * 4; float v; if (index >= 0 && index + 4 <= (int64_t)previous->before_length) { memcpy(&v, previous->before + index, 4); value = v; } else value = qa_compat_qc_words_float(words, foff); }
      else value = qa_compat_qc_words_float(words, foff);
      int64_t mask = (int64_t)storage->private_mask;
      for (size_t ei = 0; ei < storage->item_count; ei++) mask |= (int64_t)storage->items[ei].mask;
      if (value != trunc(value) || value < 0 || value > 0xffffff || (((int64_t)value) & ~mask) != 0) qa_range_error("Original QC inventory contains undeclared bits");
      for (size_t ei = 0; ei < storage->item_count; ei++) {
        if (n < cap) out[n] = (qa_inventory_entry){storage->items[ei].item, (((int64_t)value) & (int64_t)storage->items[ei].mask) == 0 ? 0 : 1, 1, false, {NULL,NULL}};
        n++;
      }
    }
  }
  return n;
}
static qa_compat_qc_mod_items_entry *find_entry(I *items, qa_actor_id actor) {
  for (size_t i=0;i<items->entry_count;i++) { if (actor_eq(items->entries[i].actor.id, actor)) return &items->entries[i]; } return NULL;
}
typedef struct { I *items; qa_actor_id actor; qa_inventory_entry buffer[64]; } StateCtx;
static StateCtx state_storage[256]; static size_t state_count;
static StateCtx *state_for(I *items, qa_actor_id actor) {
  for (size_t i=0;i<state_count;i++) if (state_storage[i].items==items && actor_eq(state_storage[i].actor,actor)) return &state_storage[i];
  if (state_count>=256) qa_range_error("item state storage exhausted");
  state_storage[state_count]=(StateCtx){items,actor,{{0}}}; return &state_storage[state_count++];
}
static const qa_inventory_entry *st_read(void *ctx, size_t *out_count) {
  StateCtx *s = ctx; *out_count = read_entries(s->items, s->actor, NULL, s->items->definition->storage, s->items->definition->storage_count, s->buffer, 64); return s->buffer;
}
static bool st_entry(void *ctx, qa_item_id item, qa_inventory_entry *out) {
  StateCtx *s = ctx; I *items = s->items;
  const qa_mod_qc_item_storage *storage = NULL;
  for (size_t i=0;i<items->definition->storage_count;i++) {
    const qa_mod_qc_item_storage *st=&items->definition->storage[i];
    if (strcmp(st->kind,"counter")==0 && strcmp(st->item,item)==0) storage=st;
    if (strcmp(st->kind,"bits")==0) for(size_t j=0;j<st->item_count;j++) if(strcmp(st->items[j].item,item)==0) storage=st;
  }
  if (!storage) return false;
  qa_compat_qc_words *words = words_of(items, s->actor);
  double count = qa_compat_qc_words_float(words, off_of(items, storage->field));
  if (strcmp(storage->kind,"counter")==0) { *out=(qa_inventory_entry){item,count,storage->capacity.is_constant?storage->capacity.value:qa_compat_qc_words_float(words,off_of(items,storage->capacity.field)),true,{"source-counter","binary32"}}; return true; }
  const qa_mod_qc_item_mask *bit=NULL; for(size_t j=0;j<storage->item_count;j++) if(strcmp(storage->items[j].item,item)==0) bit=&storage->items[j];
  if(!bit) qa_range_error("Admitted packed item is missing");
  *out=(qa_inventory_entry){item,(((int64_t)count)&(int64_t)bit->mask)==0?0:1,1,false,{NULL,NULL}}; return true;
}
static void st_write(void *ctx, qa_inventory_entry entry) {
  StateCtx *s = ctx; I *items = s->items;
  qa_compat_qc_mod_items_entry *current = find_entry(items, s->actor);
  if (!current || !current->lease.current(current->lease.ctx)) qa_range_error("QC source items are no longer current");
  qa_compat_qc_words *words = words_of(items, s->actor);
  for (size_t i=0;i<items->definition->storage_count;i++) {
    const qa_mod_qc_item_storage *storage=&items->definition->storage[i];
    if (strcmp(storage->kind,"counter")==0 && strcmp(storage->item,entry.item)==0) {
      if (!isfinite((double)qa_float32(entry.count)) || !isfinite((double)qa_float32(entry.capacity))) qa_range_error("QC inventory exceeds source scalar precision");
      if (storage->capacity.is_constant && entry.capacity != storage->capacity.value) qa_range_error("QC source capacity is immutable");
      qa_compat_qc_words_set_float(words, off_of(items, storage->field), entry.count);
      if (!storage->capacity.is_constant) qa_compat_qc_words_set_float(words, off_of(items, storage->capacity.field), entry.capacity);
      return;
    }
    if (strcmp(storage->kind,"bits")==0) {
      const qa_mod_qc_item_mask *bit=NULL; for(size_t j=0;j<storage->item_count;j++) if(strcmp(storage->items[j].item,entry.item)==0) bit=&storage->items[j];
      if(!bit) continue;
      if (entry.capacity!=1 || (entry.count!=0 && entry.count!=1)) qa_range_error("QC weapon ownership must be one source bit");
      int64_t off=off_of(items,storage->field); int64_t bits=(int64_t)qa_compat_qc_words_float(words,off);
      qa_compat_qc_words_set_float(words,off,(double)(entry.count==0?bits&~(int64_t)bit->mask:bits|(int64_t)bit->mask)); return;
    }
  }
  qa_range_error("QC item write has no source field");
}
static bool st_mutable(void *ctx, qa_item_id item) {
  StateCtx *s=ctx; for(size_t i=0;i<s->items->definition->storage_count;i++){ const qa_mod_qc_item_storage *st=&s->items->definition->storage[i]; if(strcmp(st->kind,"counter")==0&&strcmp(st->item,item)==0&&!st->capacity.is_constant) return true; } return false;
}
static void invoke_item(I *items, qa_actor_id actor, qa_mod_source_call call) {
  double t = items->operations.time_seconds(items->operations.ctx);
  static qa_actor_id self_id; self_id = actor;
  qa_compat_qc_mod_input_entry inputs[2];
  memset(inputs,0,sizeof inputs);
  inputs[0].name="self"; inputs[0].value.kind="actor"; inputs[0].value.actor_value=&self_id;
  inputs[1].name="time"; inputs[1].value.kind="float"; inputs[1].value.number_value=t;
  items->operations.invoke(items->operations.ctx, call, inputs, 2);
}
static void st_action(void *ctx, qa_item_id item, const char *action) {
  StateCtx *s=ctx; I *items=s->items;
  qa_compat_qc_mod_items_entry *current=find_entry(items,s->actor);
  const qa_mod_source_call *call=NULL;
  for(size_t i=0;i<items->definition->definition_count;i++) if(strcmp(items->definition->definitions[i].item,item)==0)
    call = strcmp(action,"use")==0 ? items->definition->definitions[i].action_use : items->definition->definitions[i].action_drop;
  qa_owned_actor resolved_o; bool resolved_ok = current && items->operations.resolve_owned && items->operations.resolve_owned(items->operations.ctx, s->actor, &resolved_o) && qa_same_owned_actor(resolved_o, current->actor);
  bool cur = current && current->lease.current(current->lease.ctx) && resolved_ok
    && items->operations.reference(items->operations.ctx, s->actor)==current->reference;
  if (!current || !cur || !call) qa_range_error("Source item action is no longer admitted");
  invoke_item(items, s->actor, *call);
}
static bool current_of(I *items, qa_compat_qc_mod_items_entry *entry) {
  qa_owned_actor resolved;
  return items->operations.resolve_owned && items->operations.resolve_owned(items->operations.ctx, entry->actor.id, &resolved) && qa_same_owned_actor(resolved, entry->actor)
    && find_entry(items, entry->actor.id) == entry && entry->lease.current(entry->lease.ctx)
    && items->operations.reference(items->operations.ctx, entry->actor.id) == entry->reference;
}
static qa_item_id active_item(I *items, qa_actor_id actor) {
  const qa_mod_qc_weapons *w = items->definition->weapons; if (!w) return NULL;
  double value = qa_compat_qc_words_float(words_of(items, actor), off_of(items, w->selected_field));
  for (size_t i=0;i<w->selected_value_count;i++) if (w->selected_values[i].value==value) return w->selected_values[i].item;
  if (value != 0) qa_range_error("Original QC selected an undeclared source weapon");
  return NULL;
}
typedef struct { I *items; qa_compat_qc_mod_items_entry *entry; qa_source_weapon_presentation_model model; } WeaponCtx;
static WeaponCtx weapon_storage[256]; static size_t weapon_count;
static WeaponCtx *weapon_for(I *items, qa_compat_qc_mod_items_entry *entry) {
  for(size_t i=0;i<weapon_count;i++) if(weapon_storage[i].items==items && weapon_storage[i].entry==entry) return &weapon_storage[i];
  if(weapon_count>=256) qa_range_error("weapon storage exhausted");
  weapon_storage[weapon_count]=(WeaponCtx){items,entry,{0}}; return &weapon_storage[weapon_count++];
}
static bool wpn_current(void *ctx) { WeaponCtx *w=ctx; return current_of(w->items, w->entry); }
static qa_source_weapon_presentation wpn_read(void *ctx) {
  WeaponCtx *w=ctx; I *items=w->items;
  if (!current_of(items, w->entry)) qa_range_error("QC source weapon presentation has expired");
  const qa_mod_qc_weapons *def = items->definition->weapons;
  if (!def) qa_range_error("QC source has no weapon presentation");
  qa_compat_qc_words *words = words_of(items, w->entry->actor.id);
  qa_compat_qc_machine *m = items->operations.machine(items->operations.ctx);
  const char *path0 = qa_compat_qc_strings_get(&m->strings, qa_compat_qc_words_int(words, off_of(items, def->model_field)));
  char path[512]; snprintf(path,sizeof path,"%s",path0);
  qa_resolved_resource_reference asset; bool has = path[0] && items->operations.media_resource(items->operations.ctx, path, &asset);
  if (path[0] && !has) qa_range_error("Original QC weapon model was not prepared: %s", path);
  qa_source_weapon_presentation r; memset(&r,0,sizeof r);
  r.source.provider = items->provider; r.source.content = items->operations.media_content;
  r.active = active_item(items, w->entry->actor.id);
  if (has) { w->model.is_resolved=true; w->model.resource=asset; w->model.frame=qa_compat_qc_words_float(words, off_of(items, def->model_frame)); r.model=&w->model; }
  r.items = items->definitions; r.item_count = items->definition->definition_count;
  return r;
}
static bool wpn_accepts(void *ctx, qa_item_id item) {
  WeaponCtx *w=ctx; const qa_mod_qc_weapons *def=w->items->definition->weapons; if(!def) return false;
  bool listed=false; for(size_t i=0;i<def->selected_value_count;i++) if(strcmp(def->selected_values[i].item,item)==0) listed=true;
  return current_of(w->items, w->entry) && listed && w->items->operations.inventory_count(w->items->operations.ctx, w->entry->actor.id, item) > 0;
}
static bool wpn_select(void *ctx, qa_item_id item) {
  WeaponCtx *w=ctx; if(!wpn_accepts(ctx,item)) return false;
  const qa_mod_qc_weapons *def=w->items->definition->weapons;
  const qa_mod_qc_weapon_value *v=NULL; for(size_t i=0;i<def->select_value_count;i++) if(strcmp(def->select_values[i].item,item)==0) v=&def->select_values[i];
  if(!v) return false;
  qa_compat_qc_words_set_float(words_of(w->items, w->entry->actor.id), off_of(w->items, def->select_field), v->value);
  invoke_item(w->items, w->entry->actor.id, def->select_call);
  qa_item_id active = active_item(w->items, w->entry->actor.id);
  return current_of(w->items, w->entry) && active && strcmp(active,item)==0;
}
static void wpn_holster(void *ctx) { WeaponCtx *w=ctx; if(!current_of(w->items,w->entry)) qa_range_error("QC source weapon retired while holstering"); }
static bool wpn_holstered(void *ctx) { WeaponCtx *w=ctx; (void)w; return current_of(w->items, w->entry); /* stage settled checked by provider stage binding */ }
static bool wpn_resume(void *ctx, qa_item_id item) {
  WeaponCtx *w=ctx; const qa_mod_qc_weapons *def=w->items->definition->weapons;
  if(!current_of(w->items,w->entry)) qa_range_error("QC source weapon retired while resuming");
  if (item) return wpn_select(ctx,item);
  for(size_t i=0;i<def->resume_count;i++){ if(!current_of(w->items,w->entry)) qa_range_error("QC source weapon retired while resuming"); invoke_item(w->items,w->entry->actor.id,def->resume[i]); }
  return current_of(w->items,w->entry);
}
bool qa_compat_qc_mod_items_init(I *items, const qa_mod_qc_items *definition, qa_provider_id provider, qa_compat_qc_mod_items_operations operations, qa_compat_qc_program *program) {
  memset(items,0,sizeof *items); items->definition=definition; items->provider=provider; items->operations=operations; items->self_ref=items;
  items->admission_count = definition->definition_count;
  items->admissions = calloc(items->admission_count?items->admission_count:1,sizeof *items->admissions);
  items->definitions = calloc(items->admission_count?items->admission_count:1,sizeof *items->definitions);
  if(!items->admissions||!items->definitions) return false;
  for(size_t i=0;i<items->admission_count;i++){
    const qa_mod_qc_item_definition *d=&definition->definitions[i];
    qa_source_item_definition out; memset(&out,0,sizeof out);
    out.item=d->item; out.label=d->label; out.source.provider=provider; out.source.content=operations.media_content;
    out.is_weapon=d->is_weapon; out.weapon=d->weapon;
    out.has_icon=d->has_icon;
    if (d->has_icon && d->icon) { static qa_weapon_hud_icon icon_storage[64]; static size_t icon_count; icon_storage[icon_count]=qa_resolve_item_icon(*d->icon, operations.media_content); out.icon=&icon_storage[icon_count++]; }
    if (d->has_actions) { qa_source_item_action_calls calls = {(const void *)d->action_use, (const void *)d->action_drop}; static qa_source_item_action action_storage[128][2]; static size_t action_count; out.action_count=qa_source_item_action_names(calls, action_storage[action_count], 2); out.actions=action_storage[action_count]; action_count++; }
    items->definitions[i]=out;
    items->admissions[i]=(qa_source_item_admission){out, d->admission};
  }
  /* watched words */
  size_t cap=0;
  for(size_t i=0;i<definition->storage_count;i++){
    int64_t words_list[2]; size_t wn=0;
    const qa_compat_qc_definition *f=qa_compat_qc_program_field_named(program, definition->storage[i].field);
    if(!f) qa_range_error("Missing admitted source field %s", definition->storage[i].field);
    words_list[wn++]=f->offset;
    if(strcmp(definition->storage[i].kind,"counter")==0 && !definition->storage[i].capacity.is_constant){
      const qa_compat_qc_definition *cf=qa_compat_qc_program_field_named(program, definition->storage[i].capacity.field);
      if(!cf) qa_range_error("Missing admitted source field %s", definition->storage[i].capacity.field);
      words_list[wn++]=cf->offset;
    }
    for(size_t j=0;j<wn;j++){ bool dup=false; for(size_t k=0;k<items->watched_word_count;k++) if(items->watched_words[k]==words_list[j]) dup=true;
      if(!dup){ if(items->watched_word_count==cap){ cap=cap?cap*2:8; items->watched_words=realloc(items->watched_words,cap*sizeof *items->watched_words); if(!items->watched_words) return false; } items->watched_words[items->watched_word_count++]=words_list[j]; } }
  }
  items->has_weapons = definition->weapons != NULL;
  return true;
}
void qa_compat_qc_mod_items_destroy(I *items) { free(items->admissions); free(items->definitions); free(items->entries); free(items->watched_words); memset(items,0,sizeof *items); }
void qa_compat_qc_mod_items_admit(I *items, qa_actor_id actor) {
  if (find_entry(items, actor)) return;
  qa_owned_actor owner;
  if (!items->operations.resolve_owned(items->operations.ctx, actor, &owner) || !items->operations.client_is_current(items->operations.ctx, actor))
    qa_range_error("QC items require a live canonical client");
  int64_t reference = items->operations.reference(items->operations.ctx, actor);
  StateCtx *state = state_for(items, actor);
  qa_source_item_lease lease = items->operations.bind_items(items->operations.ctx, owner, items->admissions, items->admission_count, state, st_read, st_entry, st_write, st_mutable, st_action);
  if (items->entry_count==items->entry_capacity){ size_t c=items->entry_capacity?items->entry_capacity*2:8; items->entries=realloc(items->entries,c*sizeof *items->entries); if(!items->entries) qa_range_error("item allocation failed"); items->entry_capacity=c; }
  items->entries[items->entry_count]=(qa_compat_qc_mod_items_entry){owner, reference, lease, NULL, NULL};
  qa_compat_qc_mod_items_entry *entry=&items->entries[items->entry_count++];
  if (items->definition->weapons) {
    WeaponCtx *w = weapon_for(items, entry);
    qa_source_weapon_binding binding; memset(&binding,0,sizeof binding);
    binding.ctx=w; binding.read=wpn_read;
    binding.handoff.provider=items->provider;
    binding.handoff.ctx=w; binding.handoff.accepts=wpn_accepts; binding.handoff.select=wpn_select;
    binding.handoff.holster=wpn_holster; binding.handoff.is_holstered=wpn_holstered; binding.handoff.is_source_input=false; binding.handoff.resume_immediate=wpn_resume;
    binding.current=wpn_current;
    if (!items->operations.weapon_bind(items->operations.ctx, owner, binding, &entry->remove_weapon, &entry->remove_weapon_ctx)) {
      lease.close(lease.ctx); items->entry_count--; qa_range_error("QC source weapon service is absent");
    }
  }
}
void qa_compat_qc_mod_items_observe(I *items, qa_compat_qc_entity_store_observation store, const qa_original_pickup_execution *pickup) {
  int64_t end = store.word + (int64_t)(store.after_length / 4);
  bool touched=false; for(size_t i=0;i<items->watched_word_count;i++) if(items->watched_words[i]>=store.word && items->watched_words[i]<end) touched=true;
  if(!touched) return;
  const qa_mod_qc_item_storage *affected[64]; size_t an=0;
  for(size_t i=0;i<items->definition->storage_count;i++){
    const qa_mod_qc_item_storage *st=&items->definition->storage[i];
    int64_t w1=off_of(items,st->field); bool hit = w1>=store.word && w1<end;
    if(!hit && strcmp(st->kind,"counter")==0 && !st->capacity.is_constant){ int64_t w2=off_of(items,st->capacity.field); hit=w2>=store.word&&w2<end; }
    if(hit && an<64) affected[an++]=st;
  }
  qa_mod_qc_item_storage affected_storage[64]; for(size_t i=0;i<an;i++) affected_storage[i]=*affected[i];
  qa_actor_id actor = items->operations.actor(items->operations.ctx, store.reference);
  qa_compat_qc_mod_items_entry *entry = find_entry(items, actor);
  if (!entry) return;
  if (!current_of(items, entry)) qa_range_error("QC source item store belongs to a retired actor");
  qa_inventory_entry before[64], after[64];
  size_t bn = read_entries(items, actor, &store, affected_storage, an, before, 64);
  size_t fn = read_entries(items, actor, NULL, affected_storage, an, after, 64);
  qa_source_item_store changes[64]; size_t cn=0;
  for(size_t i=0;i<fn;i++){
    qa_inventory_entry *prev=NULL; for(size_t j=0;j<bn;j++) if(strcmp(before[j].item,after[i].item)==0) prev=&before[j];
    if(!prev || (prev->count==after[i].count && prev->capacity==after[i].capacity)) continue;
    if (pickup) {
      bool ok=false;
      for(size_t wi=0;wi<pickup->writes.count;wi++){
        const qa_pickup_write *wr=&pickup->writes.values[wi];
        if (wr->kind==QA_PICKUP_RESOURCE_INVENTORY && strcmp(wr->item,after[i].item)==0
          && (prev->count==after[i].count || strcmp(wr->fields,"capacity")!=0)
          && (prev->capacity==after[i].capacity || strcmp(wr->fields,"count")!=0)) ok=true;
      }
      if(!ok) qa_range_error("Original pickup changed an undeclared source item");
    }
    changes[cn++]=(qa_source_item_store){*prev, after[i]};
  }
  if (cn) entry->lease.stored(entry->lease.ctx, changes, cn);
}
void qa_compat_qc_mod_items_release(I *items, qa_actor_id actor) {
  qa_compat_qc_mod_items_entry *entry=find_entry(items,actor); if(!entry) return;
  qa_source_item_lease lease=entry->lease; void (*remove)(void *)=entry->remove_weapon; void *rctx=entry->remove_weapon_ctx;
  size_t idx=(size_t)(entry-items->entries);
  memmove(&items->entries[idx],&items->entries[idx+1],(items->entry_count-idx-1)*sizeof *items->entries); items->entry_count--;
  if (remove) remove(rctx);
  lease.close(lease.ctx);
}
void qa_compat_qc_mod_items_close(I *items) {
  while (items->entry_count) qa_compat_qc_mod_items_release(items, items->entries[0].actor.id);
}
