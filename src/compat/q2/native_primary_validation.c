/* Port of src/compat/q2/native-primary-validation.ts (world-specific
 * tail checks need world profiles; see unresolved). GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_validation.h"
#include <math.h>
#include <string.h>
static bool safei(double v){return v==trunc(v)&&v>=-9007199254740991.0&&v<=9007199254740991.0;}
static void bound(double off,double bytes,double limit){ if(!safei(off)||!safei(bytes)||off<0||bytes<1||off+bytes>limit) qa_range_error("Native primary field exceeds its declared source record"); }
static qa_compat_q2_pe_file PE; static double ENT, CLI;
static void image(double off,double bytes,bool exec){ bound(off,bytes,PE.image_size); bool ok=false;
  for(size_t i=0;i<PE.section_count;i++){const qa_compat_q2_pe_section *s=&PE.sections[i];
    if(off>=s->rva&&off+bytes<=s->rva+s->mapped_size&&(exec?(s->permissions&&strstr(s->permissions,"execute")):(s->permissions&&strcmp(s->permissions,"none")!=0))) ok=true;}
  if(!ok) qa_range_error("Native primary address has no matching image section"); }
static double width(const char *e){ if(!strcmp(e,"int8")||!strcmp(e,"uint8"))return 1; if(!strcmp(e,"int16")||!strcmp(e,"uint16"))return 2; if(!strcmp(e,"int64")||!strcmp(e,"uint64")||!strcmp(e,"float64"))return 8; return 4; }
static void field(qa_native_item_field f){ double b=width(f.field.encoding);
  if(!strcmp(f.record,"image")) image(f.field.offset,b,false);
  else if(!strcmp(f.record,"entity")) bound(f.field.offset,b,ENT);
  else if(!strcmp(f.record,"client")) bound(f.field.offset,b,CLI);
  else qa_range_error("Native primary field has no declared source record"); }
static void test(const qa_native_item_test *t){ if(!strcmp(t->kind,"scalar")) field(t->field);
  else { if(!strcmp(t->pointer_field.record,"image")) image(t->pointer_field.offset,(double)PE.pointer_bytes,false);
    else if(!strcmp(t->pointer_field.record,"entity")) bound(t->pointer_field.offset,(double)PE.pointer_bytes,ENT);
    else if(!strcmp(t->pointer_field.record,"client")) bound(t->pointer_field.offset,(double)PE.pointer_bytes,CLI);
    else qa_range_error("Native primary field has no declared source record");
    if(t->pointer_value) image(t->pointer_value->rva,(double)PE.pointer_bytes,false);} }
void qa_compat_q2_validate_native_primary(const qa_compat_q2_native_primary_profile *profile, qa_compat_q2_pe_file pe, double entity_bytes){
  PE=pe; ENT=entity_bytes; const qa_compat_q2_native_primary_weapon_profile *w=profile->weapons; CLI=w->client_byte_length;
  const qa_compat_q2_native_primary_player_profile *pl=profile->player;
  const qa_compat_q2_native_primary_command_profile *cm=profile->commands;
  const qa_compat_q2_native_primary_inventory_profile *in=profile->inventory;
  const qa_compat_q2_native_primary_drop_profile *dr=profile->drop;
  const qa_compat_q2_native_pickup_profile *pk=profile->pickups;
  double entries[256]; size_t en=0;
  double base[]={w->spawn_entry,w->attack_animation_entry,pl->spawn,cm->give_entry,cm->give_weapons,cm->give_ammo,cm->drop_entry,in->next_entry,in->next_scan,in->prev_entry,in->prev_scan,in->validate_entry,in->use_entry,in->use_call,in->named_entry,in->named_call,in->named_lookup_call,in->named_lookup_return,dr->named,dr->inv_entry,dr->inv_admitted,dr->find,dr->lookup_return,dr->allocate,dr->free_entry,pk->touch,pk->grant_return,pk->targets_return,pk->ammo_entry};
  for(size_t i=0;i<sizeof base/sizeof *base;i++) entries[en++]=base[i];
  if(!w->dispatcher.entry.kind||strcmp(w->dispatcher.entry.kind,"rva")!=0) qa_range_error("Primary source dispatcher requires a declared executable address");
  entries[en++]=w->dispatcher.entry.rva;
  if(!strcmp(pl->objectives_kind,"entry")) entries[en++]=pl->objectives_entry;
  if(!strcmp(w->damage_kind,"source-result")) entries[en++]=w->damage_entry; else image(w->damage_address,width(w->damage_encoding),false);
  if(!strcmp(w->delay_evaluate_kind,"source-animation")){ entries[en++]=w->delay_anim_entry; for(size_t i=0;i<w->delay_write_count;i++) field(w->delay_writes[i]); for(size_t i=0;i<w->delay_projection_count;i++) field(w->delay_projection[i].field); if(w->delay_baseline_ms<=0) qa_range_error("Original weapon animation interval must be positive"); }
  for(size_t i=0;i<pk->grant_count;i++){const qa_compat_q2_native_pickup_grant *g=&pk->grants[i]; entries[en++]=g->entry; if(g->recipient_entry==g->recipient_join) qa_range_error("Native source region is empty"); entries[en++]=g->recipient_entry; entries[en++]=g->recipient_join;
    for(size_t j=0;j<g->consumer_count;j++) entries[en++]=g->consumers[j].entry;
    if(g->has_supply&&!strcmp(g->supply_kind,"ammo")) entries[en++]=g->supply_entry;
    else if(g->has_supply){entries[en++]=g->supply_ammo_return;entries[en++]=g->supply_settle;entries[en++]=g->supply_auto_entry;entries[en++]=g->supply_auto_join; if(g->supply_auto_entry==g->supply_auto_join) qa_range_error("Native source region is empty");}}
  if(pk->has_ammo_stop) entries[en++]=pk->ammo_stop;
  /* regions emptiness */
  for(size_t i=0;i<w->decision_count;i++){ if(w->decisions[i].entry==w->decisions[i].join) qa_range_error("Native source region is empty"); entries[en++]=w->decisions[i].entry; entries[en++]=w->decisions[i].join; }
  for(size_t i=0;i<en;i++) image(entries[i],1,true);
  qa_native_item_field fs[]={w->entity_water_level,w->entity_view_height,w->entity_max_health,w->client_buttons,w->client_latched_buttons,w->delay_flag,w->anim_frame,w->anim_end,w->anim_priority,w->anim_duck,w->anim_run};
  for(size_t i=0;i<11;i++) field(fs[i]);
  for(size_t i=0;i<w->decision_count;i++) for(size_t j=0;j<w->decisions[i].field_count;j++){field(w->decisions[i].fields[j].field); double cm2=w->decisions[i].fields[j].clear_mask; if(cm2!=trunc(cm2)||cm2<1||cm2>4294967295.0) qa_range_error("Native input mask is outside uint32");}
  for(size_t i=0;i<w->spawn_accepted_count;i++) test(&w->spawn_accepted[i]);
  for(size_t i=0;i<w->active_count;i++) test(&w->active[i]);
  for(size_t i=0;i<w->committed_input_count;i++) for(size_t j=0;j<w->committed_input[i].count;j++) test(&w->committed_input[i].tests[j]);
  for(size_t i=0;i<w->continuation_count;i++) for(size_t j=0;j<w->continuations[i].count;j++) test(&w->continuations[i].tests[j]);
  if(strcmp(w->entity_max_health.record,"entity")!=0||strcmp(w->entity_max_health.field.encoding,"int32")!=0) qa_range_error("Native source max health requires its declared int32 entity field");
  bound(w->client_view_angles,12,CLI); bound(pl->command_angles,12,CLI);
  if(pl->has_match){bound(pl->match.score,4,CLI); if(fmod(pl->match.score,4)!=0) qa_range_error("Native score field requires int32 alignment");}
  if(pl->has_forward) bound(pl->forward,12,CLI);
  bound(pl->velocity,12,ENT);
  if(in->client!=(!strcmp(profile->edition,"classic")?84:120)) qa_range_error("Native client pointer differs from the selected public game API");
  bound(in->inventory,in->count*4,CLI); bound(in->cursor,4,CLI);
  for(size_t i=0;i<in->selection_write_count;i++) bound(in->selection_writes[i].offset,in->selection_writes[i].bytes,CLI);
  bound(cm->client_weapon,(double)PE.pointer_bytes,CLI); bound(dr->client_weapon,(double)PE.pointer_bytes,CLI); bound(dr->client_pending,(double)PE.pointer_bytes,CLI);
  if(cm->has_client_ammo_index) bound(cm->client_ammo_index,4,CLI);
  for(size_t i=0;i<pk->ammo_capacity_count;i++) bound(pk->ammo_capacities[i],pk->ammo_capacity_bytes,CLI);
  image(cm->give_argc,(double)PE.pointer_bytes,false); image(cm->give_argv,(double)PE.pointer_bytes,false);
  bound(cm->items_classname,(double)PE.pointer_bytes,cm->items_stride); bound(cm->items_icon,(double)PE.pointer_bytes,cm->items_stride); bound(pk->items_pickup,(double)PE.pointer_bytes,cm->items_stride);
  bound(cm->items_flags,4,cm->items_stride); bound(pk->supply_flags,4,cm->items_stride); bound(pk->ammo_tag,4,cm->items_stride);
  bound(cm->ammo_offset,!strcmp(cm->ammo_kind,"name")?(double)PE.pointer_bytes:4,cm->items_stride);
  if(!strcmp(cm->ammo_kind,"name")) bound(cm->ammo_label,(double)PE.pointer_bytes,cm->items_stride);
  image(cm->items_table,cm->items_count*cm->items_stride,false);
  image(w->time_address,width(w->time_encoding),false); image(pk->time_address,!strcmp(pk->time_storage,"float32-seconds")?4:8,false);
  bound(pk->entity_count,4,ENT); bound(pk->entity_spawnflags,4,ENT); bound(pk->entity_item,(double)PE.pointer_bytes,ENT); bound(pk->entity_inuse,pk->entity_inuse_bytes,ENT);
  if(pk->has_entity_generation) bound(pk->entity_generation,4,ENT);
  if(cm->items_weapon_flag!=pk->supply_weapon_flag||cm->items_classname!=pk->items_classname||cm->items_flags!=pk->supply_flags||in->cursor!=dr->client_cursor||cm->client_weapon!=dr->client_weapon) qa_range_error("Native primary interfaces disagree about original source fields");
}
