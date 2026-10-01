#include "internal.h"
#include "qa/launch_save.h"
#include "qa/source_save.h"

#define F(type, value, member) do { if (!qa_source_save_##type(io,&(value)->member)) return false; } while (0)
#define E(value, member) do { uint32_t enum_value_=(value)->member; if (!qa_source_save_u32(io,&enum_value_)) return false; if (reading) (value)->member=enum_value_; } while (0)
#define T(value, member) do { if (!string(io,draft,&(value)->member)) return false; } while (0)
static bool fail(qa_error *error, const char *message)
{ qa_error_set(error,QA_ERROR_FORMAT,0,"%s",message); return false; }
static bool allocate(qa_source_save_io *io, void **out, size_t count, size_t size)
{
    if (count>SIZE_MAX/size) return false;
    *out=count?calloc(count,size):NULL;
    if (count && !*out) { qa_error_set(io->error,QA_ERROR_MEMORY,io->offset,"Allocating saved draft choices"); return false; }
    return true;
}
static bool dictionary(qa_source_save_io *io, qa_launch_draft *draft)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; size_t count=qa_strings_count(draft->strings);
    if (!qa_source_save_count(io,&count,reading?io->input.size/8:UINT32_MAX) || count>UINT32_MAX) return false;
    for (size_t i=0;i<count;++i) {
        qa_bytes bytes=reading?(qa_bytes){0}:qa_strings_text(draft->strings,(qa_string_id)(i+1)); size_t length=bytes.size;
        if (!qa_source_save_count(io,&length,reading?io->input.size-io->offset:SIZE_MAX)) return false;
        if (reading) {
            if (length>io->input.size-io->offset || (length && memchr(io->input.data+io->offset,0,length))) return false;
            bytes=(qa_bytes){io->input.data+io->offset,length}; io->offset+=length; qa_string_id id;
            if (!qa_strings_intern(draft->strings,bytes,&id,io->error) || id!=i+1) return false;
        } else if ((length && memchr(bytes.data,0,length)) || !qa_source_save_bytes(io,(void *)bytes.data,length)) return false;
    }
    return true;
}
static bool string(qa_source_save_io *io, qa_launch_draft *draft, const char **value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; uint32_t id=0;
    if (!reading && *value) {
        id=qa_strings_find(draft->strings,(qa_bytes){(const uint8_t *)*value,strlen(*value)});
        if (!id || qa_strings_cstr(draft->strings,id)!=*value) return false;
    }
    if (!qa_source_save_u32(io,&id) || id>qa_strings_count(draft->strings)) return false;
    if (reading) *value=id?qa_strings_cstr(draft->strings,id):NULL;
    return true;
}
static bool product(qa_source_save_io *io, qa_catalog *catalog, qa_product_id *id)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    const qa_product *current=!reading?qa_catalog_product(catalog,*id):NULL;
    bool known=current!=NULL; uint32_t raw=*id;
    if (!qa_source_save_bool(io,&known)) return false;
    if (!known) {
        if (!qa_source_save_u32(io,&raw) || (raw && qa_catalog_product(catalog,raw))) return false;
        if (reading) *id=raw;
        return true;
    }
    size_t length=reading?0:strlen(current->identity);
    if (!qa_source_save_count(io,&length,reading?io->input.size-io->offset:SIZE_MAX) || !length) return false;
    if (!reading) return qa_source_save_bytes(io,(void *)current->identity,length);
    if (length>io->input.size-io->offset) return false;
    const qa_product *found=NULL;
    for (size_t i=0;i<qa_catalog_count(catalog);++i) {
        const qa_product *item=qa_catalog_at(catalog,i);
        if (strlen(item->identity)==length && !memcmp(item->identity,io->input.data+io->offset,length)) {
            if (found) return false;
            found=item;
        }
    }
    io->offset+=length;
    if (!found) return false;
    *id=found->id; return true;
}
static bool actor(qa_source_save_io *io, const qa_actor_registry *registry, qa_actor_id *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ, present=value->registry!=0;
    qa_saved_actor_id saved={.slot=value->slot,.generation=value->generation};
    if (!reading && present && (!registry || !qa_actors_save_reference(registry,*value,&saved,io->error))) return false;
    if (!qa_source_save_bool(io,&present) || !qa_source_save_u32(io,&saved.slot) || !qa_source_save_u64(io,&saved.generation)) return false;
    if (reading) {
        if (present) {
            if (!registry || !qa_actors_reference_saved(registry,saved,true,value,io->error)) return false;
        } else *value=(qa_actor_id){.slot=saved.slot,.generation=saved.generation};
    }
    return true;
}
static bool scope(qa_source_save_io *io, const qa_actor_registry *registry, qa_launch_scope *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; E(value,kind); F(u32,value,seat);
    return actor(io,registry,&value->actor);
}
static bool clock(qa_source_save_io *io, qa_clock_config *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; E(value,kind);
    F(u64,value,initial_time_ns); F(u64,value,interval_ns); F(u64,value,minimum_frame_ns);
    F(u64,value,maximum_frame_ns); F(u64,value,initial_lead_ns); F(u32,value,maximum_steps); return true;
}
static bool rules(qa_source_save_io *io, qa_mode_rules *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; E(value,source); E(value,kind);
    for (size_t i=0;i<3;++i) if (!qa_source_save_u32(io,&value->teams[i])) return false;
    F(u32,value,forced_team); F(i32,value,frag_limit); F(i32,value,capture_limit); F(i32,value,warmup_seconds);
    F(i32,value,competition); F(i32,value,setup_seconds); F(i32,value,countdown_seconds); F(i32,value,match_seconds);
    F(i32,value,max_game_players); F(i32,value,election_percent); F(i32,value,teamplay); F(i32,value,rune_mask);
    F(i32,value,vote_limit); F(u32,value,flags); F(u32,value,referee_flags);
    F(f32,value,time_limit_minutes); F(f32,value,obelisk_health); F(f32,value,obelisk_regen);
    F(u64,value,obelisk_regen_ns); F(u64,value,obelisk_respawn_ns);
    F(bool,value,enabled); F(bool,value,friendly_fire); F(bool,value,force_join); F(bool,value,match_lock);
    F(bool,value,paused); F(bool,value,auto_lock); F(bool,value,relics); F(bool,value,single_player_active);
    F(bool,value,tournament_restart); F(bool,value,q2_rerelease); F(bool,value,start_map); F(bool,value,force_balance);
    F(bool,value,voting_disabled); F(bool,value,rogue_deathmatch); return true;
}
static bool world(qa_source_save_io *io, qa_launch_draft *draft, qa_launch_world *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!product(io,draft->catalog,&value->preset) || !product(io,draft->catalog,&value->geometry) ||
        !product(io,draft->catalog,&value->presentation) || !product(io,draft->catalog,&value->environment_product)) return false;
    T(value,map); T(value,start_command); T(value,spawn_point); T(value,environment_path);
    F(bool,value,explicit_spawn_point); F(bool,value,explicit_presentation); F(bool,value,doppler); F(bool,value,campaign);
    E(value,environment); F(i32,value,skill); return true;
}
static bool provider(qa_source_save_io *io, qa_launch_draft *draft, const qa_actor_registry *registry, qa_launch_provider *value)
{
    (void)registry; bool reading=io->direction==QA_SOURCE_SAVE_READ;
    T(value,instance); T(value,implementation); T(value,artifact); T(value,component);
    if (!value->instance || !*value->instance || !value->implementation || !value->artifact || !value->component ||
        !product(io,draft->catalog,&value->product)) return false;
    E(value,runtime);
    if (!clock(io,&value->clock)) return false;
    size_t length=value->options.size;
    if (!qa_source_save_count(io,&length,reading?io->input.size-io->offset:SIZE_MAX)) return false;
    if (reading) {
        if (!allocate(io,(void **)&value->options.data,length,1)) return false;
        value->options.size=length;
    }
    return (!length || value->options.data) && qa_source_save_bytes(io,(void *)value->options.data,length);
}
static bool binding(qa_source_save_io *io, qa_launch_draft *draft, const qa_actor_registry *registry, qa_launch_binding *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!scope(io,registry,&value->scope)) return false;
    E(value,role); T(value,selector); T(value,instance); T(value,definition);
    return launch_scope_valid(value->scope) && (unsigned)value->role<QA_ROLE_COUNT && value->instance && *value->instance && value->selector && value->definition;
}
static bool mod(qa_source_save_io *io, qa_launch_draft *draft, const qa_actor_registry *registry, qa_launch_mod_selection *value)
{
    (void)registry; T(value,instance); T(value,component); F(bool,value,enabled);
    return value->instance && *value->instance && value->component && qa_catalog_mod_key(value->component);
}
static bool mode(qa_source_save_io *io, qa_launch_draft *draft, const qa_actor_registry *registry, qa_launch_mode *value)
{
    (void)registry; T(value,instance); T(value,forced_team);
    for (size_t i=0;i<3;++i) if (!string(io,draft,&value->teams[i]) || !value->teams[i]) return false;
    F(bool,value,primary_score);
    return value->instance && *value->instance && value->forced_team && rules(io,&value->rules);
}
static bool equipment(qa_source_save_io *io, qa_launch_draft *draft, const qa_actor_registry *registry, qa_launch_equipment *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!scope(io,registry,&value->scope)) return false;
    T(value,instance); T(value,grapple_source); T(value,grenade_source);
    E(&value->selection,grapple); E(&value->selection,binding);
    F(bool,&value->selection,retain_on_weapon_change); F(bool,&value->selection,release_on_jump); F(bool,&value->selection,release_on_teleport);
    F(bool,&value->selection.grenades,enabled); F(bool,&value->selection.grenades,infinite_ammo);
    F(i32,&value->selection.grenades,initial_ammo); F(i32,&value->selection.grenades,capacity);
    return value->instance && *value->instance && value->grapple_source && value->grenade_source;
}
static bool seat(qa_source_save_io *io, qa_launch_draft *draft, const qa_actor_registry *registry, qa_launch_seat *value)
{
    F(u32,value,id); F(u32,value,input_device);
    if (!actor(io,registry,&value->actor)) return false;
    T(value,name); T(value,team); F(bool,value,local); F(bool,value,spectator); F(bool,value,bot); F(f32,value,bot_skill);
    T(value,bot_definition); F(i32,value,bot_delay_ms);
    T(value,character_model); T(value,character_skin);
    T(value,character_head_model); T(value,character_head_skin);
    return value->name && value->team;
}
static bool loadout(qa_source_save_io *io, qa_launch_draft *draft, const qa_actor_registry *registry, qa_launch_loadout *value)
{
    if (!scope(io,registry,&value->scope)) return false;
    T(value,item); F(i32,value,quantity); F(i32,value,capacity); F(bool,value,override_capacity); F(bool,value,drop_on_death);
    return value->item && *value->item;
}
static bool monster(qa_source_save_io *io, qa_launch_draft *draft, const qa_actor_registry *registry, qa_launch_monster *value)
{
    (void)registry; T(value,authored_classname); T(value,instance); T(value,classname); F(bool,value,map_defined);
    return value->authored_classname && value->instance && value->classname;
}
static bool behavior(qa_source_save_io *io, qa_launch_draft *draft, const qa_actor_registry *registry, qa_launch_weapon_behavior *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!scope(io,registry,&value->scope)) return false;
    T(value,weapon); T(value,instance); T(value,behavior); E(value,role); F(bool,value,enabled);
    return value->weapon && value->instance && value->behavior;
}
static bool choices(qa_source_save_io *io, qa_launch_draft *draft, const qa_actor_registry *registry)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!world(io,draft,&draft->choices.world)) return false;
#define ARRAY(field, count_field, capacity_field, type, record, minimum) do { \
    size_t count_=draft->choices.count_field; \
    if (!qa_source_save_count(io,&count_,reading?io->input.size/(minimum):SIZE_MAX)) return false; \
    if (reading) { if (!allocate(io,(void **)&draft->choices.field,count_,sizeof(type))) return false; \
        draft->choices.count_field=draft->capacity_field=count_; } \
    for (size_t index_=0;index_<count_;++index_) if (!record(io,draft,registry,&((type *)draft->choices.field)[index_])) return false; \
} while (0)
    ARRAY(providers,provider_count,provider_capacity,qa_launch_provider,provider,80);
    ARRAY(bindings,binding_count,binding_capacity,qa_launch_binding,binding,37);
    ARRAY(mods,mod_count,mod_capacity,qa_launch_mod_selection,mod,9);
    ARRAY(modes,mode_count,mode_capacity,qa_launch_mode,mode,140);
    ARRAY(equipment,equipment_count,equipment_capacity,qa_launch_equipment,equipment,54);
    ARRAY(seats,seat_count,seat_capacity,qa_launch_seat,seat,60);
    ARRAY(loadout,loadout_count,loadout_capacity,qa_launch_loadout,loadout,35);
    ARRAY(monsters,monster_count,monster_capacity,qa_launch_monster,monster,13);
    ARRAY(behaviors,behavior_count,behavior_capacity,qa_launch_weapon_behavior,behavior,38);
#undef ARRAY
    const qa_launch_choices *v=&draft->choices;
#define UNIQUE(count, test) do { for (size_t i=0;i<(count);++i) for (size_t j=0;j<i;++j) if (test) return false; } while (0)
    UNIQUE(v->provider_count,!strcmp(v->providers[i].instance,v->providers[j].instance));
    UNIQUE(v->binding_count,v->bindings[i].role==v->bindings[j].role && launch_scope_equal(v->bindings[i].scope,v->bindings[j].scope) && !strcmp(v->bindings[i].selector,v->bindings[j].selector));
    UNIQUE(v->mod_count,!strcmp(v->mods[i].instance,v->mods[j].instance));
    UNIQUE(v->mode_count,!strcmp(v->modes[i].instance,v->modes[j].instance));
    UNIQUE(v->equipment_count,launch_scope_equal(v->equipment[i].scope,v->equipment[j].scope) && !strcmp(v->equipment[i].instance,v->equipment[j].instance));
    UNIQUE(v->seat_count,v->seats[i].id==v->seats[j].id);
    UNIQUE(v->loadout_count,launch_scope_equal(v->loadout[i].scope,v->loadout[j].scope) && !strcmp(v->loadout[i].item,v->loadout[j].item));
    UNIQUE(v->monster_count,!strcmp(v->monsters[i].authored_classname,v->monsters[j].authored_classname));
    UNIQUE(v->behavior_count,v->behaviors[i].role==v->behaviors[j].role && launch_scope_equal(v->behaviors[i].scope,v->behaviors[j].scope) && !strcmp(v->behaviors[i].weapon,v->behaviors[j].weapon));
#undef UNIQUE
    return true;
}
bool qa_launch_draft_checkpoint(const qa_launch_draft *draft, const qa_actor_registry *registry, qa_buffer *out, qa_error *error)
{
    if (!draft || !draft->catalog || !out) return fail(error,"Draft capture requires its actual private owner");
    qa_source_save_io io; uint8_t magic[4]={'Q','L','D','R'}; uint32_t schema=3; qa_launch_draft saved=*draft;
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    bool ok=qa_source_save_bytes(&io,magic,4) && qa_source_save_u32(&io,&schema) && dictionary(&io,&saved) &&
        choices(&io,&saved,registry) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) fail(error,"Unqualified private draft state");
    qa_source_save_dispose(&io); return ok;
}
bool qa_launch_draft_restore(qa_catalog *catalog, const qa_actor_registry *registry, qa_bytes bytes, qa_launch_draft **out, qa_error *error)
{
    if (!catalog || !out || *out) return fail(error,"Draft restore requires a qualified catalog and empty output");
    qa_launch_draft *draft=NULL;
    if (!launch_empty(catalog,&draft,error)) return false;
    qa_source_save_io io; uint8_t magic[4]; uint32_t schema=0;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) { qa_launch_draft_destroy(draft); return false; }
    bool ok=qa_source_save_bytes(&io,magic,4) && !memcmp(magic,"QLDR",4) && qa_source_save_u32(&io,&schema) && schema==3 &&
        dictionary(&io,draft) && choices(&io,draft,registry) && qa_source_save_finish(&io,NULL);
    if (ok) *out=draft;
    else { qa_launch_draft_destroy(draft); if (error && error->code==QA_OK) fail(error,"Invalid or unqualified private draft continuation"); }
    qa_source_save_dispose(&io); return ok;
}
