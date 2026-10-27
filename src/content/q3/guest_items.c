/* Port of src/content/q3/guest-items.ts. GPL-2.0-or-later.
 * Source item rows transcribed from TS owner
 * src/content/q3/base/shared/items.ts itemList("missionpack")
 * (weapon/ammo rows; verified by bun JSON dump, 24 rows). Item catalog
 * reading mirrors src/compat/qvm/item-catalog.ts. */
#include "qa/content/q3/guest_items.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

static void *xmalloc(size_t n){ void*p=malloc(n?n:1); if(!p) qa_range_error("out of memory"); return p; }
static char *xstrdup(const char*s){ size_t n=strlen(s)+1; char*p=xmalloc(n); memcpy(p,s,n); return p; }
static bool str_eq(const char*a,const char*b){ return a&&b&&strcmp(a,b)==0; }

const qa_q3_guest_weapon qa_q3_standard_guest_weapons[] = {
  {1,"q3:weapon/gauntlet",NULL,"Gauntlet",NULL,NULL,NULL},
  {2,"q3:weapon/machinegun","q3:ammo/machinegun","Machinegun",NULL,NULL,NULL},
  {3,"q3:weapon/shotgun","q3:ammo/shotgun","Shotgun",NULL,NULL,NULL},
  {4,"q3:weapon/grenadelauncher","q3:ammo/grenadelauncher","Grenade Launcher",NULL,NULL,NULL},
  {5,"q3:weapon/rocketlauncher","q3:ammo/rocketlauncher","Rocket Launcher",NULL,NULL,NULL},
  {6,"q3:weapon/lightning","q3:ammo/lightning","Lightning Gun",NULL,NULL,NULL},
  {7,"q3:weapon/railgun","q3:ammo/railgun","Railgun",NULL,NULL,NULL},
  {8,"q3:weapon/plasmagun","q3:ammo/plasmagun","Plasma Gun",NULL,NULL,NULL},
  {9,"q3:weapon/bfg","q3:ammo/bfg","BFG10K",NULL,NULL,NULL},
  {10,"q3:weapon/grapple",NULL,"Grappling Hook",NULL,NULL,NULL},
  {11,"q3:weapon/nailgun","q3:ammo/nailgun","Nailgun",NULL,NULL,NULL},
  {12,"q3:weapon/proxlauncher","q3:ammo/proxlauncher","Prox Launcher",NULL,NULL,NULL},
  {13,"q3:weapon/chaingun","q3:ammo/chaingun","Chaingun",NULL,NULL,NULL},
};
const qa_q3_guest_weapon qa_q3_base_guest_weapons[] = {
  {1,"q3:weapon/gauntlet",NULL,"Gauntlet",NULL,NULL,NULL},
  {2,"q3:weapon/machinegun","q3:ammo/machinegun","Machinegun",NULL,NULL,NULL},
  {3,"q3:weapon/shotgun","q3:ammo/shotgun","Shotgun",NULL,NULL,NULL},
  {4,"q3:weapon/grenadelauncher","q3:ammo/grenadelauncher","Grenade Launcher",NULL,NULL,NULL},
  {5,"q3:weapon/rocketlauncher","q3:ammo/rocketlauncher","Rocket Launcher",NULL,NULL,NULL},
  {6,"q3:weapon/lightning","q3:ammo/lightning","Lightning Gun",NULL,NULL,NULL},
  {7,"q3:weapon/railgun","q3:ammo/railgun","Railgun",NULL,NULL,NULL},
  {8,"q3:weapon/plasmagun","q3:ammo/plasmagun","Plasma Gun",NULL,NULL,NULL},
  {9,"q3:weapon/bfg","q3:ammo/bfg","BFG10K",NULL,NULL,NULL},
  {10,"q3:weapon/grapple",NULL,"Grappling Hook",NULL,NULL,NULL},
};
typedef struct source_item { const char *class_name; double type, tag; } source_item;
enum { IT_WEAPON = 1, IT_AMMO = 2 };
static const source_item SOURCE_ITEMS[] = {
  {"weapon_gauntlet",1,1},{"weapon_shotgun",1,3},{"weapon_machinegun",1,2},
  {"weapon_grenadelauncher",1,4},{"weapon_rocketlauncher",1,5},{"weapon_lightning",1,6},
  {"weapon_railgun",1,7},{"weapon_plasmagun",1,8},{"weapon_bfg",1,9},{"weapon_grapplinghook",1,10},
  {"ammo_shells",2,3},{"ammo_bullets",2,2},{"ammo_grenades",2,4},{"ammo_cells",2,8},
  {"ammo_lightning",2,6},{"ammo_rockets",2,5},{"ammo_slugs",2,7},{"ammo_bfg",2,9},
  {"ammo_nails",2,11},{"ammo_mines",2,12},{"ammo_belt",2,13},
  {"weapon_nailgun",1,11},{"weapon_prox_launcher",1,12},{"weapon_chaingun",1,13},
};
#define SOURCE_COUNT (sizeof SOURCE_ITEMS / sizeof SOURCE_ITEMS[0])

void qa_q3_guest_weapon_list_destroy(qa_q3_guest_weapon_list *list){
  if(!list) return;
  for(size_t i=0;i<list->count;i++){
    free(list->values[i].owned_item); free(list->values[i].owned_ammo); free(list->values[i].owned_label);
  }
  free(list->values); list->values=NULL; list->count=0;
}
static qa_q3_guest_weapon_list copy_weapons(const qa_q3_guest_weapon *src,size_t n){
  qa_q3_guest_weapon_list l={xmalloc((n?n:1)*sizeof *l.values),n};
  for(size_t i=0;i<n;i++){
    l.values[i]=src[i];
    l.values[i].owned_item=src[i].owned_item?xstrdup(src[i].owned_item):NULL;
    l.values[i].owned_ammo=src[i].owned_ammo?xstrdup(src[i].owned_ammo):NULL;
    l.values[i].owned_label=src[i].owned_label?xstrdup(src[i].owned_label):NULL;
    if(l.values[i].owned_item) l.values[i].item=l.values[i].owned_item;
    if(l.values[i].owned_ammo) l.values[i].ammo=l.values[i].owned_ammo;
    if(l.values[i].owned_label) l.values[i].label=l.values[i].owned_label;
  }
  return l;
}
/* --- item-catalog.ts mirrors (static) --- */
typedef struct catalog_items { qa_q3_guest_qvm_catalog_record *values; size_t count; } catalog_items;
static void free_catalog(catalog_items *c){
  for(size_t i=0;i<c->count;i++){ free(c->values[i].class_name); free(c->values[i].pickup_name); }
  free(c->values); c->values=NULL; c->count=0;
}
static int32_t word_le(const uint8_t *data,int64_t off){
  uint32_t u=(uint32_t)data[off]|((uint32_t)data[off+1]<<8)|((uint32_t)data[off+2]<<16)|((uint32_t)data[off+3]<<24);
  int32_t v; memcpy(&v,&u,sizeof v); return v;
}
static bool safe_int(double v){ return v==trunc(v)&&v>=-9007199254740991.0&&v<=9007199254740991.0; }
static catalog_items read_records(const uint8_t *data,size_t length,qa_q3_guest_qvm_item_layout layout,
    bool filter_types){
  int64_t table, count;
  if(!layout.address_is_global) table=(int64_t)layout.address;
  else {
    int64_t a=(int64_t)layout.address;
    if(!safe_int(layout.address)||a<0||a%4!=0||a+4>(int64_t)length) qa_range_error("QVM item table locator exceeds module data");
    table=word_le(data,a);
  }
  if(!layout.count_is_global) count=(int64_t)layout.count;
  else {
    int64_t a=(int64_t)layout.count;
    if(!safe_int(layout.count)||a<0||a%4!=0||a+4>(int64_t)length) qa_range_error("QVM item table locator exceeds module data");
    count=word_le(data,a);
  }
  double total=(double)table+(double)count*layout.stride;
  if(count<0||(layout.count_is_global&&count>(int64_t)layout.count_maximum)
    ||table<0||table%4!=0||(count>0&&table==0)||!safe_int(total)||total>(double)length)
    qa_range_error("QVM item table exceeds initialized or live module data");
  catalog_items out={xmalloc((count?(size_t)count:1)*sizeof *out.values),0};
  for(int64_t index=0;index<count;index++){
    int64_t address=table+index*(int64_t)layout.stride;
    int32_t class_ptr=word_le(data,address+(int64_t)layout.field_class_name);
    if(class_ptr==0) continue;
    int32_t type=word_le(data,address+(int64_t)layout.field_type);
    if(filter_types&&type!=(int32_t)layout.weapon_type&&type!=(int32_t)layout.ammo_type) continue;
    int32_t pickup_ptr=word_le(data,address+(int64_t)layout.field_pickup_name);
    int32_t tag=word_le(data,address+(int64_t)layout.field_tag);
    const int32_t ptrs[2]={class_ptr,pickup_ptr}; char *texts[2];
    for(int k=0;k<2;k++){
      int32_t ptr=ptrs[k];
      if(ptr<=0||ptr>=(int32_t)length) qa_range_error("QVM item string is outside initialized module data");
      const uint8_t *end=memchr(data+ptr,0,length-(size_t)ptr);
      if(!end) qa_range_error("QVM item string is unterminated");
      size_t n=(size_t)(end-(data+ptr)); texts[k]=xmalloc(n+1);
      for(size_t j=0;j<n;j++) texts[k][j]=(char)data[ptr+(int64_t)j];
      texts[k][n]='\0';
    }
    if(!texts[0][0]||!texts[1][0]) qa_range_error("QVM item has no source name");
    out.values[out.count++]=(qa_q3_guest_qvm_catalog_record){texts[0],texts[1],type,tag,(double)index,(double)address};
  }
  return out;
}
static catalog_items read_catalog(const uint8_t *data,size_t length,qa_q3_guest_qvm_item_layout layout,bool private_inventory){
  catalog_items items=read_records(data,length,layout,true);
  for(size_t i=0;i<items.count;i++)
    if(items.values[i].tag<1||(!private_inventory&&items.values[i].tag>15))
      qa_range_error("QVM item cannot be represented by its public weapon/ammo state");
  return items;
}
static const source_item *find_source(double type,const char *class_name){
  for(size_t i=0;i<SOURCE_COUNT;i++) if(SOURCE_ITEMS[i].type==type&&strcmp(SOURCE_ITEMS[i].class_name,class_name)==0) return &SOURCE_ITEMS[i];
  return NULL;
}
static char *guest_item(const char *digest,const char *class_name){
  size_t n=strlen("q3:guest/")+strlen(digest)+1+strlen(class_name)+1;
  char *s=xmalloc(n); snprintf(s,n,"q3:guest/%s/%s",digest,class_name); return s;
}
static qa_q3_guest_weapon_list catalog_weapons(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_guest_qvm_item_layout layout,const catalog_items *entries,
    bool has_selection,const qa_q3_guest_selection *selection,size_t selection_count){
  qa_q3_guest_weapon_list out={xmalloc((entries->count?entries->count:1)*sizeof *out.values),0};
  for(size_t i=0;i<entries->count;i++){
    const qa_q3_guest_qvm_catalog_record *item=&entries->values[i];
    if(item->type!=layout.weapon_type) continue;
    bool first=true;
    for(size_t j=0;j<entries->count;j++)
      if(entries->values[j].type==layout.weapon_type&&entries->values[j].tag==item->tag){ first=j==i; break; }
    if(!first) continue;
    const source_item *original=find_source(IT_WEAPON,item->class_name);
    const qa_q3_weapon_item *canonical=original?qa_q3_get_weapon_item(original->tag):NULL;
    const qa_q3_guest_qvm_catalog_record *ammo=NULL;
    for(size_t j=0;j<entries->count;j++) if(entries->values[j].type==layout.ammo_type&&entries->values[j].tag==item->tag){ ammo=&entries->values[j]; break; }
    const source_item *original_ammo=ammo?find_source(IT_AMMO,ammo->class_name):NULL;
    const qa_q3_weapon_item *canonical_ammo_weapon=original_ammo?qa_q3_get_weapon_item(original_ammo->tag):NULL;
    qa_item_id canonical_ammo=canonical_ammo_weapon?canonical_ammo_weapon->ammo:NULL;
    qa_q3_guest_weapon w={0}; w.weapon=item->tag;
    qa_item_id selected=NULL;
    if(has_selection) for(size_t j=0;j<selection_count;j++) if(selection[j].value==item->tag){ selected=selection[j].item; break; }
    if(selected) w.item=selected;
    else if(canonical) w.item=canonical->item;
    else { w.owned_item=guest_item(artifact->module.digest,item->class_name); w.item=w.owned_item; }
    if(!ammo) w.ammo=NULL;
    else if(canonical_ammo) w.ammo=canonical_ammo;
    else { w.owned_ammo=guest_item(artifact->module.digest,ammo->class_name); w.ammo=w.owned_ammo; }
    w.owned_label=xstrdup(item->pickup_name); w.label=w.owned_label;
    out.values[out.count++]=w;
  }
  return out;
}
static qa_q3_guest_qvm_item_layout parse_layout(const qa_save_reader *reader){
  qa_save_reader fields,stride_r; 
  if(!qa_save_reader_field(reader,"fields",&fields)||!qa_save_reader_field(reader,"stride",&stride_r)) qa_range_error("out of memory");
  double stride=qa_save_reader_integer_min(&stride_r,4); qa_save_reader_destroy(&stride_r);
  qa_q3_guest_qvm_item_layout layout={0}; layout.stride=stride;
  const char *names[4]={"className","pickupName","type","tag"};
  double offsets[4];
  for(int k=0;k<4;k++){
    qa_save_reader fr; if(!qa_save_reader_field(&fields,names[k],&fr)) qa_range_error("out of memory");
    double v=qa_save_reader_integer_min(&fr,0);
    if((int64_t)v%4!=0||v+4>stride) qa_save_reader_fail(&fr,"item field exceeds its record or is unaligned");
    offsets[k]=v; qa_save_reader_destroy(&fr);
  }
  qa_save_reader_destroy(&fields);
  layout.field_class_name=offsets[0]; layout.field_pickup_name=offsets[1];
  layout.field_type=offsets[2]; layout.field_tag=offsets[3];
  qa_save_reader pointer,count_r;
  if(!qa_save_reader_field(reader,"address",&pointer)||!qa_save_reader_field(reader,"count",&count_r)) qa_range_error("out of memory");
  if(pointer.value&&pointer.value->kind==QA_SAVE_NUMBER){ layout.address=qa_save_reader_integer_min(&pointer,4); }
  else { qa_save_reader g; if(!qa_save_reader_field(&pointer,"global",&g)) qa_range_error("out of memory");
    layout.address_is_global=true; layout.address=qa_save_reader_integer_min(&g,0); qa_save_reader_destroy(&g); }
  if(count_r.value&&count_r.value->kind==QA_SAVE_NUMBER){ layout.count=qa_save_reader_integer_min(&count_r,1); }
  else { qa_save_reader g,m; if(!qa_save_reader_field(&count_r,"global",&g)||!qa_save_reader_field(&count_r,"maximum",&m)) qa_range_error("out of memory");
    layout.count_is_global=true; layout.count=qa_save_reader_integer_min(&g,0); layout.count_maximum=qa_save_reader_integer_min(&m,1);
    qa_save_reader_destroy(&g); qa_save_reader_destroy(&m); }
  qa_save_reader_destroy(&pointer); qa_save_reader_destroy(&count_r);
  qa_save_reader source; if(!qa_save_reader_field(reader,"source",&source)) qa_range_error("out of memory");
  if(!qa_save_reader_is_undefined(&source)){ qa_save_reader_literal_string(&source,"live"); layout.source_live=true; }
  qa_save_reader_destroy(&source);
  qa_save_reader wt,at;
  if(!qa_save_reader_field(reader,"weaponType",&wt)||!qa_save_reader_field(reader,"ammoType",&at)) qa_range_error("out of memory");
  layout.weapon_type=qa_save_reader_integer_min(&wt,0); layout.ammo_type=qa_save_reader_integer_min(&at,0);
  qa_save_reader_destroy(&wt); qa_save_reader_destroy(&at);
  if((int64_t)layout.address%4!=0||(layout.count_is_global&&(int64_t)layout.count%4!=0)
    ||(int64_t)stride%4!=0||layout.weapon_type==layout.ammo_type)
    qa_save_reader_fail(reader,"invalid item table layout");
  if(!layout.source_live&&(layout.address_is_global||layout.count_is_global))
    qa_save_reader_fail(reader,"runtime item table locations require live source storage");
  return layout;
}
static const qa_q3_guest_qvm_item_layout THREEWAVE_LAYOUT = {
  false,5356,false,49,0,false,52,0,28,36,40,1,2,
};
qa_q3_guest_weapon_list qa_q3_guest_weapons(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_guest_mounts mounts, const qa_q3_guest_qvm_item_layout *declared_layout, bool private_inventory){
  bool has_layout=false; qa_q3_guest_qvm_item_layout layout={0};
  if(declared_layout){ layout=*declared_layout; has_layout=true; }
  else if(str_eq(artifact->module.digest,"sha256:9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337")){
    layout=THREEWAVE_LAYOUT; has_layout=true;
  }
  if(!declared_layout){
    uint8_t *bytes=NULL; size_t length=0;
    if(mounts.open(mounts.ctx,"qvm-items.json",&bytes,&length)){
      qa_save_value *value=qa_parse_json_value(bytes,length,false); free(bytes);
      if(!value) qa_range_error("out of memory");
      qa_save_reader reader; if(!qa_save_reader_init(&reader,value,"qvm-items.json")) qa_range_error("out of memory");
      qa_save_reader version; if(!qa_save_reader_field(&reader,"version",&version)) qa_range_error("out of memory");
      qa_save_reader_literal_number(&version,1); qa_save_reader_destroy(&version);
      qa_save_reader digest_r; if(!qa_save_reader_field(&reader,"artifactDigest",&digest_r)) qa_range_error("out of memory");
      const char *digest_text=qa_save_reader_string(&digest_r);
      if(!str_eq(digest_text,artifact->module.digest)) qa_save_reader_fail(&reader,"item declaration belongs to different qagame bytes");
      qa_save_reader_destroy(&digest_r);
      qa_save_reader items; if(!qa_save_reader_field(&reader,"items",&items)) qa_range_error("out of memory");
      layout=parse_layout(&items); has_layout=true;
      qa_save_reader_destroy(&items); qa_save_reader_destroy(&reader); qa_save_value_free(value);
    }
  }
  if(!has_layout)
    return (artifact->known&&str_eq(artifact->known->product,"missionpack"))
      ?copy_weapons(qa_q3_standard_guest_weapons,QA_Q3_STANDARD_GUEST_WEAPON_COUNT)
      :copy_weapons(qa_q3_base_guest_weapons,QA_Q3_BASE_GUEST_WEAPON_COUNT);
  if(layout.source_live){
    if(!declared_layout) qa_range_error("Live QVM catalogs require the complete declared primary interface");
    return copy_weapons(NULL,0);
  }
  catalog_items entries=read_catalog(artifact->image.initialized_data,artifact->image.initialized_data_length,layout,private_inventory);
  qa_q3_guest_weapon_list out=catalog_weapons(artifact,layout,&entries,false,NULL,0);
  free_catalog(&entries);
  return out;
}
/* --- Q3GuestCatalog --- */
struct qa_q3_guest_catalog {
  qa_q3_equipment_qvm_artifact artifact;
  qa_q3_guest_qvm_module module;
  bool has_layout; qa_q3_guest_qvm_item_layout layout;
  bool has_table; /* layout present (QvmSourceItemCatalog) */
  bool closed;
  catalog_items records; bool has_records;
  const qa_q3_guest_qvm_catalog_record *previous;
  qa_q3_guest_weapon_list weapons;
  bool has_selection; qa_q3_guest_selection *selection; size_t selection_count;
  bool has_private; qa_item_id *private_items; size_t private_count;
};
static bool private_has(const qa_q3_guest_catalog *c,qa_item_id item){
  for(size_t i=0;i<c->private_count;i++) if(str_eq(c->private_items[i],item)) return true;
  return false;
}
static void validate_weapons(const qa_q3_guest_catalog *c,const qa_q3_guest_weapon *weapons,size_t count){
  for(size_t i=0;i<count;i++){
    if(weapons[i].weapon<1||(!c->has_private&&weapons[i].weapon>15))
      qa_range_error("QVM item cannot be represented by its declared inventory storage");
    if(c->has_selection){
      bool found=false;
      for(size_t j=0;j<c->selection_count;j++) if(c->selection[j].value==weapons[i].weapon&&str_eq(c->selection[j].item,weapons[i].item)) found=true;
      if(!found) qa_range_error("QVM weapon has no declared original selection value");
    }
    if(c->has_private&&(!private_has(c,weapons[i].item)||(weapons[i].ammo&&!private_has(c,weapons[i].ammo))))
      qa_range_error("QVM item has no declared original inventory storage");
  }
}
qa_q3_guest_catalog *qa_q3_guest_catalog_create(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_guest_qvm_module module, const qa_q3_guest_qvm_item_layout *layout,
    const qa_q3_guest_weapon *initial, size_t initial_count,
    bool has_selection, const qa_q3_guest_selection *selection, size_t selection_count,
    const qa_q3_guest_qvm_inventory_profile *inventory){
  if(!str_eq(module.profile_module.digest,artifact->module.digest)
    ||!str_eq(module.profile_module.id,artifact->module.id)
    ||!str_eq(module.profile_module.artifact_path,artifact->module.artifact_path)
    ||!str_eq(module.profile_module.revision,artifact->module.revision))
    qa_range_error("QVM item catalog belongs to a different original module");
  qa_q3_guest_catalog *c=calloc(1,sizeof *c); if(!c) return NULL;
  c->artifact=*artifact; c->module=module;
  if(layout){ c->has_layout=true; c->layout=*layout; c->has_table=true; }
  c->weapons=copy_weapons(initial,initial_count);
  c->has_selection=has_selection;
  if(has_selection){
    c->selection=xmalloc((selection_count?selection_count:1)*sizeof *c->selection);
    if(selection_count) memcpy(c->selection,selection,selection_count*sizeof *c->selection);
    c->selection_count=selection_count;
  }
  if(inventory&&inventory->has_storage){
    c->has_private=true;
    size_t total=0;
    for(size_t i=0;i<inventory->storage_count;i++)
      total+=(inventory->storage[i].kind&&strcmp(inventory->storage[i].kind,"counter")==0)?1:inventory->storage[i].item_count;
    c->private_items=xmalloc((total?total:1)*sizeof *c->private_items);
    for(size_t i=0;i<inventory->storage_count;i++){
      const qa_qvm_item_storage *s=&inventory->storage[i];
      if(s->kind&&strcmp(s->kind,"counter")==0) c->private_items[c->private_count++]=s->item;
      else for(size_t j=0;j<s->item_count;j++) c->private_items[c->private_count++]=s->items[j].item;
    }
  }
  validate_weapons(c,c->weapons.values,c->weapons.count);
  return c;
}
void qa_q3_guest_catalog_destroy(qa_q3_guest_catalog *c){
  if(!c) return;
  free_catalog(&c->records); qa_q3_guest_weapon_list_destroy(&c->weapons);
  free(c->selection); free(c->private_items); free(c);
}
static const catalog_items *table_records(qa_q3_guest_catalog *c){
  /* QvmSourceItemCatalog.records(): assertLive has no mirror state. */
  if(c->closed) qa_range_error("QVM item catalog owner is retired");
  if(c->has_records) return &c->records;
  const uint8_t *data=c->layout.source_live?c->module.memory_bytes:c->artifact.image.initialized_data;
  size_t length=c->layout.source_live?c->module.memory_length:c->artifact.image.initialized_data_length;
  c->records=read_records(data,length,c->layout,false);
  c->has_records=true;
  return &c->records;
}
const qa_q3_guest_qvm_catalog_record *qa_q3_guest_catalog_records(qa_q3_guest_catalog *c, size_t *out_count){
  if(c->closed) qa_range_error("QVM item catalog owner is retired");
  if(!c->has_table) qa_range_error("QVM source has no declared item table");
  const catalog_items *r=table_records(c); *out_count=r->count; return r->values;
}
const qa_q3_guest_weapon *qa_q3_guest_catalog_weapons(qa_q3_guest_catalog *c, size_t *out_count){
  if(c->closed) qa_range_error("QVM item catalog owner is retired");
  if(!c->has_table||!c->has_layout||!c->layout.source_live){ *out_count=c->weapons.count; return c->weapons.values; }
  const catalog_items *entries=table_records(c);
  if(entries->values==c->previous){ *out_count=c->weapons.count; return c->weapons.values; }
  qa_q3_guest_weapon_list weapons=catalog_weapons(&c->artifact,c->layout,entries,c->has_selection,c->selection,c->selection_count);
  validate_weapons(c,weapons.values,weapons.count);
  qa_q3_guest_weapon_list_destroy(&c->weapons);
  c->previous=entries->values; c->weapons=weapons;
  *out_count=c->weapons.count; return c->weapons.values;
}
void qa_q3_guest_catalog_reset(qa_q3_guest_catalog *c){
  if(c->has_table){ free_catalog(&c->records); c->has_records=false; }
  c->previous=NULL;
}
void qa_q3_guest_catalog_close(qa_q3_guest_catalog *c){
  if(c->has_table){ free_catalog(&c->records); c->has_records=false; }
  c->previous=NULL; c->closed=true;
}
