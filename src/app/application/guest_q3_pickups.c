#include "guest_q3_pickups.h"
#include "guest_q3_catalog.h"
#include "guest_projection_private.h"
#include "supplies.h"

typedef struct pickup_frame {
    struct pickup_frame *previous;
    struct pickup_frame *cleanup_next;
    struct application_q3_pickups *owner;
    const qa_qvm_call *call;
    qa_pickup_offer offer;
    qa_pickup_selection selection;
    qa_pickup_execution *execution;
    qa_actor_record item,recipient;
    uint32_t item_slot,recipient_slot,item_pointer,recipient_pointer,player_pointer;
    application_q3_catalog_record record;
    const application_q3_pickup_grant *grant;
    qa_qvm_word_projection *projection;
    qa_qvm_binding observer;
    uint32_t inuse_words[2];
    qa_supply_offer supply;
    qa_pickup_grant ammo;
    int32_t result;
    bool invalid,granted,supply_active,cancelled;
} pickup_frame;
typedef struct pickup_evaluation {
    struct pickup_evaluation *previous;
    pickup_frame *frame;
    bool entered;
} pickup_evaluation;
typedef struct pickup_hook {
    struct application_q3_pickups *owner;
    const application_q3_pickup_grant *grant;
    qa_qvm_saved_function descriptor;
} pickup_hook;
struct application_q3_pickups {
    q3g_role *role;
    const application_q3_pickup_profile *profile;
    pickup_frame *frames;
    pickup_frame *cleanup_frames;
    pickup_evaluation *evaluations;
    pickup_hook *hooks;
    size_t count,capacity,calls;
    bool closed,own_free;
};
static qa_application *application(const application_q3_pickups *o)
{return o->role->engine->provider->application;}
static bool word(application_q3_pickups *o,uint32_t address,int32_t *out,qa_error *e)
{uint8_t bytes[4];if(!qa_qvm_read(o->role->vm,address,bytes,4,e))return false;*out=qa_load_i32le(bytes);return true;}
static bool table(application_q3_pickups *o,qa_q3_host_game_data *out,qa_error *e)
{
    return (qa_q3_host_game_data_read(o->role->host,out)&&out->entity_stride==o->profile->entity_stride&&out->client_stride==o->profile->client_stride)||
        application_fail(e,QA_ERROR_ARGUMENT,"Pickup records differ from the actual located Source table");
}
static bool slot(application_q3_pickups *o,const qa_q3_host_game_data *t,int32_t pointer,uint32_t *out,qa_error *e)
{
    if(pointer<0||(uint32_t)pointer<t->entities_address||((uint32_t)pointer-t->entities_address)%t->entity_stride||
        ((uint32_t)pointer-t->entities_address)/t->entity_stride>=t->entity_count)
        return application_fail(e,QA_ERROR_ARGUMENT,"Pickup pointer leaves the actual located Source entities");
    *out=(uint32_t)(((uint32_t)pointer-t->entities_address)/t->entity_stride);(void)o;return true;
}
static bool actor_current(application_q3_pickups *o,const qa_actor_record *captured,uint32_t source_slot)
{
    const qa_actor_record *actual=qa_actors_get(qa_session_actors(application(o)->session),captured->id);
    if(source_slot<64){const q3g_client *client=o->role->engine->clients+source_slot;
        if(qa_actor_id_equal(client->actor,captured->id)&&(client->disconnect_started||client->pending_retirement))return false;}
    uint32_t actual_slot;qa_error ignored={0};
    return actual&&actual->owner==captured->owner&&actual->definition==captured->definition&&
        actual->has_source==captured->has_source&&actual->source_slot==captured->source_slot&&
        qa_q3_host_actor_slot(o->role->host,captured->id,&actual_slot,&ignored)&&actual_slot==source_slot;
}
static bool live(pickup_frame *f)
{
    application_q3_pickups *o=f->owner;qa_error ignored={0};qa_q3_host_game_data t;int32_t used_item,used_recipient,item,index,player;
    return !f->invalid&&!o->closed&&!o->role->retired&&f->execution&&qa_pickup_current(f->execution)&&
        application_q3_catalog_current(o->role->catalog,o->role->image,o->role->vm,o->role->abi)&&table(o,&t,&ignored)&&
        t.entities_address+(uint64_t)f->item_slot*t.entity_stride==f->item_pointer&&
        t.entities_address+(uint64_t)f->recipient_slot*t.entity_stride==f->recipient_pointer&&
        actor_current(o,&f->item,f->item_slot)&&actor_current(o,&f->recipient,f->recipient_slot)&&
        word(o,f->item_pointer+o->profile->fields.inuse,&used_item,&ignored)&&used_item&&
        word(o,f->recipient_pointer+o->profile->fields.inuse,&used_recipient,&ignored)&&used_recipient&&
        word(o,f->item_pointer+o->profile->fields.item,&item,&ignored)&&(uint32_t)item==f->record.address&&
        word(o,f->item_pointer+160,&index,&ignored)&&(uint32_t)index==f->record.index&&
        word(o,f->recipient_pointer+o->profile->fields.client,&player,&ignored)&&(uint32_t)player==f->player_pointer;
}
static bool cancel(pickup_frame *f,qa_error *e)
{
    f->invalid=true;
    if(f->cancelled)return true;
    if(!qa_qvm_cancel(f->call,e))return false;
    f->cancelled=true;return true;
}
static bool from(application_q3_pickups *o,const qa_qvm_call *call,const application_q3_pickup_function *function,qa_error *e)
{
    if(call->argument_base<8)return false;
    int32_t pc;if(!word(o,call->argument_base-8,&pc,e))return false;
    size_t n;const qa_qvm_instruction *code=qa_qvm_image_instructions(o->role->image,&n);
    for(size_t i=0;i<function->call_count;++i){uint32_t at=function->calls[i];if(at<n&&(uint32_t)pc==code[at].byte_offset+1)return true;}
    return false;
}
static bool arguments(const qa_qvm_call *call,uint32_t a,uint32_t b,uint32_t first,uint32_t second,qa_error *e)
{int32_t x,y;return qa_qvm_call_argument(call,a,&x,e)&&qa_qvm_call_argument(call,b,&y,e)&&(uint32_t)x==first&&(uint32_t)y==second;}
static bool projection_end(pickup_frame *f,qa_error *e)
{return !f->projection||qa_qvm_source_words_end(&f->projection,true,e);}
static bool project(pickup_frame *f,qa_item_id item,int32_t count,qa_error *e)
{
    guest_inventory_projection_word rows[2];size_t n;
    if(!application_guest_inventory_project(f->owner->role,f->recipient.id,item,count,rows,&n,e))return false;
    qa_qvm_source_word words[2];for(size_t i=0;i<n;++i)words[i]=(qa_qvm_source_word){rows[i].address,rows[i].value};
    return !n||qa_qvm_source_words_begin_observed(f->owner->role->vm,f->owner->role->image,words,n,&f->projection,e);
}
static bool frame_cleanup(pickup_frame *f,qa_error *e)
{
    if(f->observer){
        if(!qa_qvm_unobserve_writes(f->owner->role->vm,f->observer,e))return false;
        f->observer=0;
    }
    return projection_end(f,e);
}
static void frame_retire(pickup_frame *f)
{
    f->call=NULL;f->execution=NULL;f->supply_active=false;
    if(f->projection||f->observer){
        pickup_frame **tail=&f->owner->cleanup_frames;while(*tail)tail=&(*tail)->cleanup_next;
        *tail=f;return;
    }
    free((void *)f->record.class_name);free(f);
}
static bool source(pickup_frame *f,const qa_qvm_call *call,int32_t *out,qa_error *e)
{
    if(!live(f)){*out=0;return cancel(f,e);}
    bool ok=qa_qvm_proceed(call,out,e);
    if(ok&&!live(f)){*out=0;ok=cancel(f,e);}return ok;
}
static bool changed(void *context,qa_qvm *vm,const qa_qvm_committed_write *change,qa_error *e)
{
    pickup_frame *f=context;uint32_t fields[]={f->item_pointer+f->owner->profile->fields.inuse,f->recipient_pointer+f->owner->profile->fields.inuse};
    for(size_t j=0;j<2;++j){uint8_t value[4];qa_store_u32le(value,f->inuse_words[j]);
        for(size_t i=0;i<change->count;++i){const qa_qvm_committed_range *r=change->ranges+i;
            for(size_t k=0;k<4;++k){uint64_t at=(uint64_t)fields[j]+k;
                if(at>=r->offset&&at-r->offset<r->after.size)value[k]=r->after.data[at-r->offset];}}
        f->inuse_words[j]=qa_load_u32le(value);if(!f->inuse_words[j])f->invalid=true;
    }
    (void)vm;(void)e;return true;
}
static bool run_source(void *context,const qa_pickup_offer *offer,qa_pickup_selection selection,qa_pickup_execution *execution,qa_error *e)
{
    pickup_frame *f=context;application_q3_pickups *o=f->owner;
    if(selection==QA_PICKUP_SELECT_BLOCKED||selection==QA_PICKUP_SELECT_STALE){f->result=0;return true;}
    if(selection==QA_PICKUP_SELECT_REPLACEMENT&&!f->grant)return application_fail(e,QA_ERROR_UNSUPPORTED,"Replacement pickup has no admitted Source grant");
    f->offer=*offer;f->selection=selection;f->execution=execution;
    if(!live(f)){f->result=0;return true;}
    f->previous=o->frames;o->frames=f;
    qa_qvm_write_range ranges[]={{f->item_pointer+o->profile->fields.inuse,4},{f->recipient_pointer+o->profile->fields.inuse,4}};
    int32_t inuse[2];
    bool ok=word(o,ranges[0].offset,inuse,e)&&word(o,ranges[1].offset,inuse+1,e);
    if(ok){f->inuse_words[0]=(uint32_t)inuse[0];f->inuse_words[1]=(uint32_t)inuse[1];
        ok=qa_qvm_observe_writes(o->role->vm,ranges,2,changed,NULL,f,&f->observer,e)&&qa_qvm_proceed(f->call,&f->result,e);}
    qa_error cleanup={0};
    if(!frame_cleanup(f,&cleanup)){if(ok&&e)*e=cleanup;ok=false;}
    o->frames=f->previous;f->execution=NULL;return ok;
}
static bool resolve(application_q3_pickups *o,const application_q3_catalog_record *record,qa_item_id *item,qa_item_id *ammo,qa_error *e)
{
    *item=0;*ammo=0;const application_q3_catalog_weapon *weapons;size_t count;
    if(!application_q3_catalog_weapons(o->role->catalog,&weapons,&count,e))return false;
    for(size_t i=0;i<count;++i)if(weapons[i].weapon==record->tag){
        *ammo=weapons[i].ammo;
        if(record->type==o->profile->weapon_type)*item=weapons[i].item;
        else if(record->type==o->profile->ammo_type)*item=weapons[i].ammo;
        break;}
    if(*item)return true;
    if(!record->class_name)return application_fail(e,QA_ERROR_FORMAT,"Original pickup record lacks its Source classname");
    size_t n=strlen(record->class_name);if(n>SIZE_MAX-4)return application_fail(e,QA_ERROR_MEMORY,"Original pickup identity overflows");
    char *name=malloc(n+4);if(!name)return application_fail(e,QA_ERROR_MEMORY,"Retaining original pickup identity");
    memcpy(name,"q3:",3);memcpy(name+3,record->class_name,n+1);
    bool ok=qa_strings_intern(qa_session_strings(application(o)->session),(qa_bytes){(uint8_t *)name,n+3},item,e);free(name);return ok;
}
static bool touch(void *context,const qa_qvm_call *call,int32_t *out,qa_error *e)
{
    application_q3_pickups *o=((pickup_hook *)context)->owner;
    if(o->closed)return qa_qvm_proceed(call,out,e);
    int32_t item,recipient,player,health;qa_q3_host_game_data t;
    pickup_frame f={.owner=o,.call=call};
    if(!qa_qvm_call_argument(call,0,&item,e)||!qa_qvm_call_argument(call,1,&recipient,e)||!table(o,&t,e)||
        !slot(o,&t,item,&f.item_slot,e)||!slot(o,&t,recipient,&f.recipient_slot,e))return false;
    f.item_pointer=(uint32_t)item;f.recipient_pointer=(uint32_t)recipient;
    if(!word(o,f.recipient_pointer+o->profile->fields.client,&player,e)||!word(o,f.recipient_pointer+o->profile->fields.health,&health,e))return false;
    if(!player||health<1)return qa_qvm_proceed(call,out,e);
    f.player_pointer=(uint32_t)player;
    size_t extent=qa_qvm_memory_size(o->role->vm);
    if(player<0||f.player_pointer>extent||o->profile->client_stride>extent-f.player_pointer)
        return application_fail(e,QA_ERROR_ARGUMENT,"Pickup client leaves the actual private Source RAM");
    int32_t pointer,index,count,flags;
    if(!word(o,f.item_pointer+o->profile->fields.item,&pointer,e)||!word(o,f.item_pointer+160,&index,e)||
        !word(o,f.item_pointer+o->profile->fields.count,&count,e)||!word(o,f.item_pointer+o->profile->fields.flags,&flags,e))return false;
    const application_q3_catalog_record *records;size_t used;
    if(!application_q3_catalog_records(o->role->catalog,&records,&used,e))return false;
    bool found=false;for(size_t i=0;i<used;++i)if(records[i].address==(uint32_t)pointer){f.record=records[i];found=true;break;}
    if(!found||f.record.index!=(uint32_t)index)return application_fail(e,QA_ERROR_FORMAT,"Touch_Item differs from its actual Source item table");
    qa_actor_id a,b;
    if(!qa_q3_host_actor(o->role->host,f.item_slot,false,&a,e)||!qa_q3_host_actor(o->role->host,f.recipient_slot,false,&b,e))return false;
    const qa_actor_record *ar=qa_actors_get(qa_session_actors(application(o)->session),a),*br=qa_actors_get(qa_session_actors(application(o)->session),b);
    if(!ar||!br)return application_fail(e,QA_ERROR_NOT_FOUND,"Touch_Item lacks its full admitted pickup and recipient");
    f.item=*ar;f.recipient=*br;qa_item_id item_id,ammo;
    if(!actor_current(o,&f.item,f.item_slot)||!actor_current(o,&f.recipient,f.recipient_slot))
        return application_fail(e,QA_ERROR_NOT_FOUND,"Touch_Item source ownership or input admission has expired");
    char *class_name=q3g_copy_text(f.record.class_name,e);
    if(!class_name)return false;
    f.record.class_name=class_name;f.record.pickup_name=NULL;
    if(!resolve(o,&f.record,&item_id,&ammo,e)){free(class_name);return false;}
    qa_clock_state clock;qa_actor_owner owner=o->role->engine->provider->owner;
    if(!qa_session_clock(application(o)->session,owner,&clock)||clock.frame.provider!=owner||clock.frame.kind!=QA_CLOCK_Q3){
        free(class_name);return application_fail(e,QA_ERROR_NOT_FOUND,"Touch_Item has no completed actual Source clock");}
    f.offer=(qa_pickup_offer){.recipient=b,.pickup=a,.source=owner,.item=item_id,.override_count=count!=0,.count=count,
        .dropped=((uint32_t)flags&o->profile->dropped_flag)!=0,.time_ns=clock.frame.time_ns};
    if(f.record.type==3)f.offer.default_resource=(qa_pickup_resource){.kind=QA_PICKUP_PROTECTION,.channel=QA_PROTECTION_REGULAR};
    else if(f.record.type==o->profile->weapon_type||f.record.type==o->profile->ammo_type)f.offer.default_resource=(qa_pickup_resource){.kind=QA_PICKUP_INVENTORY,.item=item_id};
    for(size_t i=0;i<o->profile->objective_count;++i)if(o->profile->objective_types[i]==f.record.type)f.offer.grant=QA_PICKUP_MAP_COUPLED;
    for(size_t i=0;i<o->profile->grant_count;++i)if(o->profile->grants[i].item_type==f.record.type)f.grant=o->profile->grants+i;
    pickup_frame *held=malloc(sizeof(*held));
    if(!held){free(class_name);return application_fail(e,QA_ERROR_MEMORY,"Retaining real pickup Source continuation");}
    *held=f;
    ++o->calls;bool ok=qa_pickups_run_source(application(o)->pickups,&held->offer,run_source,held,e);--o->calls;
    if(ok)*out=held->result;
    frame_retire(held);return ok;
}
static bool decide(void *context,const qa_qvm_call *call,bool original,bool *taken,qa_error *e)
{*taken=((application_q3_pickup_branch *)context)->taken;(void)call;(void)original;(void)e;return true;}
static bool gate(void *context,const qa_qvm_call *call,int32_t *out,qa_error *e)
{
    application_q3_pickups *o=((pickup_hook *)context)->owner;pickup_frame *f=o->frames;
    if(!f||!from(o,call,&o->profile->gate,e)||!arguments(call,o->profile->item_argument,o->profile->player_argument,f->item_pointer,f->player_pointer,e))
        return e&&e->code!=QA_OK?false:qa_qvm_proceed(call,out,e);
    if(!live(f)){*out=0;return cancel(f,e);}
    if(f->selection!=QA_PICKUP_SELECT_REPLACEMENT||!f->grant)return qa_qvm_proceed(call,out,e);
    const application_q3_pickup_grant *g=f->grant;bool ok=true;
    if(g->eligibility==Q3_PICKUP_SOURCE_BRANCHES){
        qa_qvm_branch_binding *bindings=g->branch_count?calloc(g->branch_count,sizeof(*bindings)):NULL;
        if(g->branch_count&&!bindings)return application_fail(e,QA_ERROR_MEMORY,"Retaining original pickup gate branches");
        for(size_t i=0;i<g->branch_count;++i)bindings[i]=(qa_qvm_branch_binding){g->branches[i].instruction,decide,g->branches+i};
        ok=qa_qvm_bind_branches(call,bindings,g->branch_count,e)&&qa_qvm_proceed(call,out,e);free(bindings);if(ok)*out=*out!=0;
    }else if(g->eligibility==Q3_PICKUP_ELIGIBLE)*out=1;
    else {int32_t mode,lithium=0,generic,other,client;
        ok=qa_qvm_call_argument(call,0,&mode,e);
        if(ok&&g->eligibility==Q3_PICKUP_THREEWAVE_LITHIUM)ok=qa_qvm_call_argument(call,3,&lithium,e);
        if(ok)ok=word(o,f->item_pointer+164,&generic,e)&&word(o,f->item_pointer+168,&other,e)&&word(o,f->player_pointer+140,&client,e);
        if(ok)*out=(mode!=10&&!lithium)||generic!=2||other!=client;
    }
    if(ok&&!live(f)){*out=0;ok=cancel(f,e);}return ok;
}
static bool take(pickup_frame *f,qa_error *e)
{
    if(f->granted)return application_fail(e,QA_ERROR_ARGUMENT,"Original pickup attempted a second replacement grant");
    f->granted=true;qa_pickup_outcome outcome;
    if(!qa_pickup_execute_grant(f->execution,&outcome,e))return false;
    return outcome==QA_PICKUP_ACCEPTED&&live(f)?true:cancel(f,e);
}
static bool owns(pickup_frame *f,bool *out,qa_error *e)
{
    qa_supply *supply;
    if(!application_supplies_for(application(f->owner)->supplies,f->owner->role->engine->provider,f->recipient.id,&supply,e))return false;
    if(supply)return qa_supply_owns(supply,f->recipient.id,f->offer.item,out,e);
    double count;if(!qa_inventory_count_read(application(f->owner)->inventory,f->recipient.id,f->offer.item,&count,e))return false;
    *out=count>0;return true;
}
static bool region(void *context,const qa_qvm_call *call,bool *skip,qa_error *e)
{
    pickup_frame *f=context;*skip=true;
    if(!projection_end(f,e))return false;
    if(!live(f))return cancel(f,e);
    int32_t amount;if(!qa_qvm_local_word(call,f->grant->quantity,&amount,e))return false;
    qa_item_id item,ammo;if(!resolve(f->owner,&f->record,&item,&ammo,e))return false;
    if(!f->grant->weapon&&!ammo)return application_fail(e,QA_ERROR_NOT_FOUND,"Original ammo grant has no admitted Source counter");
    f->ammo=(qa_pickup_grant){ammo,amount};
    f->supply=(qa_supply_offer){.kind=f->grant->weapon?QA_SUPPLY_WEAPON:QA_SUPPLY_AMMO,.item=f->grant->weapon?f->offer.item:ammo,
        .ammo=ammo?&f->ammo:NULL,.ammo_count=ammo?1:0};
    f->supply_active=true;bool ok=take(f,e);f->supply_active=false;return ok;
}
static bool grant(void *context,const qa_qvm_call *call,int32_t *out,qa_error *e)
{
    pickup_hook *hook=context;application_q3_pickups *o=hook->owner;const application_q3_pickup_grant *g=hook->grant;
    pickup_evaluation *evaluation=o->evaluations;
    if(evaluation&&!evaluation->entered){pickup_frame *f=evaluation->frame;
        if(f->grant!=g||!live(f)||!arguments(call,0,1,f->item_pointer,f->recipient_pointer,e))return application_fail(e,QA_ERROR_ARGUMENT,"Original pickup quantity lost its exact grant caller");
        evaluation->entered=true;return qa_qvm_evaluate_call_region(call,&g->evaluation,NULL,out,e);}
    pickup_frame *f=o->frames;
    if(!f||f->grant!=g||!from(o,call,&g->function,e)||!arguments(call,0,1,f->item_pointer,f->recipient_pointer,e))return e&&e->code!=QA_OK?false:qa_qvm_proceed(call,out,e);
    if(!live(f)){*out=0;return cancel(f,e);}
    if(f->selection!=QA_PICKUP_SELECT_REPLACEMENT)return qa_qvm_proceed(call,out,e);
    if(!g->region){bool ok=take(f,e);if(ok)*out=f->cancelled?0:g->return_value;return ok;}
    if(g->weapon){bool owned;if(!owns(f,&owned,e))return false;if(!live(f)){*out=0;return cancel(f,e);}
        if(g->inventory){if(!project(f,f->offer.item,owned,e))return false;}
        else {int32_t before;
            if(!word(o,f->player_pointer+g->bits_offset,&before,e))return false;
            uint32_t bits=(uint32_t)before,mask=UINT32_C(1)<<((uint32_t)f->record.tag&31);bits=owned?bits|mask:bits&~mask;int32_t value;memcpy(&value,&bits,4);
            qa_qvm_source_word projection={f->player_pointer+g->bits_offset,value};
            if(!qa_qvm_source_words_begin_observed(o->role->vm,o->role->image,&projection,1,&f->projection,e))return false;}}
    qa_qvm_region_binding binding={.entry=g->entry,.join=g->join,.enter=region,.context=f};
    bool ok=qa_qvm_bind_regions(call,&binding,1,e)&&qa_qvm_proceed(call,out,e);qa_error cleanup={0};
    if(!projection_end(f,&cleanup)){if(ok&&e)*e=cleanup;ok=false;}return ok;
}
static bool targets(void *context,const qa_qvm_call *call,int32_t *out,qa_error *e)
{
    application_q3_pickups *o=((pickup_hook *)context)->owner;pickup_frame *f=o->frames;
    if(!f||!from(o,call,&o->profile->targets,e))return e&&e->code!=QA_OK?false:qa_qvm_proceed(call,out,e);
    return source(f,call,out,e);
}
bool application_q3_pickups_after_free(application_q3_pickups *o,const qa_qvm_call *call,int32_t pointer,qa_error *e)
{
    if(!o)return true;
    if(!call||call->vm!=o->role->vm)return application_fail(e,QA_ERROR_ARGUMENT,"Pickup free notification belongs to another Source executor");
    qa_q3_host_game_data t;uint32_t source_slot;int32_t inuse;
    if(!table(o,&t,e)||!slot(o,&t,pointer,&source_slot,e)||!word(o,(uint32_t)pointer+o->profile->fields.inuse,&inuse,e))return false;
    if(inuse)return true;
    for(pickup_frame *f=o->frames;f;f=f->previous)if(f->item_pointer==(uint32_t)pointer||f->recipient_pointer==(uint32_t)pointer)return cancel(f,e);
    return true;
}
static bool freed(void *context,const qa_qvm_call *call,int32_t *out,qa_error *e)
{
    application_q3_pickups *o=((pickup_hook *)context)->owner;int32_t pointer;qa_q3_host_game_data t;uint32_t source_slot;qa_actor_id actor;
    if(!qa_qvm_call_argument(call,0,&pointer,e)||!table(o,&t,e)||!slot(o,&t,pointer,&source_slot,e)||
        !qa_q3_host_actor(o->role->host,source_slot,false,&actor,e)||!qa_qvm_proceed(call,out,e))return false;
    int32_t inuse;if(!word(o,(uint32_t)pointer+o->profile->fields.inuse,&inuse,e))return false;
    if(!inuse){const qa_actor_record *record=qa_actors_get(qa_session_actors(application(o)->session),actor);
        if(record&&record->owner==o->role->engine->provider->owner&&!qa_session_release(application(o)->session,actor,e))return false;}
    return application_q3_pickups_after_free(o,call,pointer,e);
}
static bool quantity(void *context,const qa_inventory_entry *entry,qa_supply_quantity *out,qa_error *e)
{
    pickup_frame *f=context;application_q3_pickups *o=f->owner;const application_q3_pickup_grant *g=f->grant;
    if(o->frames!=f||!f->supply_active||!g||!g->weapon||!live(f))return application_fail(e,QA_ERROR_ARGUMENT,"Original pickup quantity requires its actual held supply grant");
    double count=trunc(entry->count);if(!isfinite(entry->count)||count<INT32_MIN||count>INT32_MAX)return application_fail(e,QA_ERROR_ARGUMENT,"Original pickup quantity exceeds the Source int32 ABI");
    qa_item_id item,ammo;if(!resolve(o,&f->record,&item,&ammo,e))return false;
    if(g->inventory){if(ammo&&!project(f,ammo,(int32_t)count,e))return false;}
    else {uint64_t offset=(uint64_t)g->ammo_offset+(uint64_t)(uint32_t)f->record.tag*4;
        if(f->record.tag<0||offset>o->profile->client_stride-4)return application_fail(e,QA_ERROR_FORMAT,"Original pickup ammo projection leaves its Source client");
        qa_qvm_source_word word={f->player_pointer+(uint32_t)offset,(int32_t)count};
        if(!qa_qvm_source_words_begin_observed(o->role->vm,o->role->image,&word,1,&f->projection,e))return false;}
    pickup_evaluation evaluation={.previous=o->evaluations,.frame=f};o->evaluations=&evaluation;
    int32_t args[]={(int32_t)f->item_pointer,(int32_t)f->recipient_pointer},result;
    bool ok=qa_qvm_invoke(o->role->vm,g->function.entry,args,2,&result,e);o->evaluations=evaluation.previous;qa_error cleanup={0};
    if(!projection_end(f,&cleanup)){if(ok&&e)*e=cleanup;ok=false;}
    if(ok&&!evaluation.entered)ok=application_fail(e,QA_ERROR_FORMAT,"Original pickup quantity did not enter its admitted Source evaluator");
    if(ok&&!live(f))ok=application_fail(e,QA_ERROR_NOT_FOUND,"Original pickup quantity lost its held Source grant");
    if(ok)*out=(qa_supply_quantity){.amount=result};
    return ok;
}
bool application_q3_pickups_supply(application_q3_pickups *o,const qa_pickup_offer *offer,qa_supply_offer *out,qa_supply_options *options,qa_error *e)
{
    pickup_frame *f=o?o->frames:NULL;
    if(!f||!offer||!out||!options||!f->granted||!f->supply_active||offer->item!=f->offer.item||
        !qa_actor_id_equal(offer->pickup,f->item.id)||!qa_actor_id_equal(offer->recipient,f->recipient.id)||!live(f))
        return application_fail(e,QA_ERROR_ARGUMENT,"Selected supply requires the actual original pickup grant");
    *out=f->supply;*options=(qa_supply_options){.selection=QA_PICKUP_SWITCH_IF_BETTER,.auto_switch=true,
        .quantity=f->grant->weapon?quantity:NULL,.quantity_context=f};return true;
}
static bool bind(application_q3_pickups *o,uint32_t entry,qa_qvm_function_hook callback,const application_q3_pickup_grant *grant,qa_error *e)
{
    pickup_hook *h=o->hooks+o->count;h->owner=o;h->grant=grant;
    h->descriptor=(qa_qvm_saved_function){.instruction=entry,.host_invocations=true,.hook=callback,.context=h};
    if(!qa_qvm_bind_function(o->role->vm,entry,true,callback,h,&h->descriptor.binding,e))return false;
    ++o->count;return true;
}
bool application_q3_pickups_create(q3g_role *role,const application_q3_pickup_profile *profile,application_q3_pickups **out,qa_error *e)
{
    if(!role||!out||*out||!profile||!profile->present||role->kind!=QA_QVM_GAME||profile->image!=role->image||profile->abi!=role->abi||
        !role->vm||!role->host||!role->catalog||!application_q3_catalog_current(role->catalog,role->image,role->vm,role->abi))
        return application_fail(e,QA_ERROR_ARGUMENT,"Pickup owner requires its actual retained Source profile and catalog");
    if(profile->grant_count>SIZE_MAX-4)return application_fail(e,QA_ERROR_MEMORY,"Pickup hook inventory overflows");
    application_q3_pickups *o=calloc(1,sizeof(*o));if(!o)return application_fail(e,QA_ERROR_MEMORY,"Owning original pickup runtime");
    *out=o;o->role=role;o->profile=profile;o->own_free=role->combat==NULL;o->capacity=profile->grant_count+3+(o->own_free?1:0);
    if(o->capacity>SIZE_MAX/sizeof(*o->hooks))return application_fail(e,QA_ERROR_MEMORY,"Pickup hook storage overflows");
    o->hooks=calloc(o->capacity,sizeof(*o->hooks));if(!o->hooks)return application_fail(e,QA_ERROR_MEMORY,"Owning original pickup hooks");
    if(!bind(o,profile->touch,touch,NULL,e)||!bind(o,profile->gate.entry,gate,NULL,e))return false;
    for(size_t i=0;i<profile->grant_count;++i)if(!bind(o,profile->grants[i].function.entry,grant,profile->grants+i,e))return false;
    return bind(o,profile->targets.entry,targets,NULL,e)&&(!o->own_free||bind(o,profile->free,freed,NULL,e));
}
bool application_q3_pickups_idle(const application_q3_pickups *o)
{return o&&!o->calls&&!o->frames&&!o->evaluations&&!o->cleanup_frames;}
static bool frame_cleanup_ready(const pickup_frame *f)
{return !f->projection||qa_qvm_source_words_is_last(f->projection);}
bool application_q3_pickups_cleanup_ready(const application_q3_pickups *o)
{
    if(!o||o->calls||o->frames||o->evaluations||o->role->engine->calls||!qa_qvm_source_returned(o->role->vm))return false;
    for(const pickup_frame *f=o->cleanup_frames;f;f=f->cleanup_next)
        if(frame_cleanup_ready(f))return true;
    return false;
}
bool application_q3_pickups_cleanup(application_q3_pickups *o,qa_error *e)
{
    if(!o)return true;
    if(o->calls||o->frames||o->evaluations||o->role->engine->calls||!qa_qvm_source_returned(o->role->vm))
        return application_fail(e,QA_ERROR_ARGUMENT,"Pickup cleanup requires its real returned Source boundary");
    while(application_q3_pickups_cleanup_ready(o)){
        pickup_frame **cursor=&o->cleanup_frames;
        while(*cursor&&!frame_cleanup_ready(*cursor))cursor=&(*cursor)->cleanup_next;
        pickup_frame *f=*cursor;if(!frame_cleanup(f,e))return false;
        *cursor=f->cleanup_next;free((void *)f->record.class_name);free(f);
    }
    return true;
}
bool application_q3_pickups_destroy(application_q3_pickups **owner,qa_error *e)
{
    application_q3_pickups *o=owner?*owner:NULL;if(!o)return true;
    if(o->calls||o->frames||o->evaluations||o->role->engine->calls||!qa_qvm_source_returned(o->role->vm))
        return application_fail(e,QA_ERROR_ARGUMENT,"Original pickup runtime retains an entered Source frame");
    o->closed=true;
    if(!application_q3_pickups_cleanup(o,e))return false;
    if(!application_q3_pickups_idle(o))return application_fail(e,QA_ERROR_ARGUMENT,"Original pickup cleanup is blocked by an actual Source word owner");
    while(o->count){pickup_hook *h=o->hooks+o->count-1;if(!qa_qvm_unbind(o->role->vm,h->descriptor.binding,e))return false;--o->count;}
    free(o->hooks);free(o);*owner=NULL;return true;
}
size_t application_q3_pickups_descriptor_count(const application_q3_pickups *o){return o?o->count:0;}
bool application_q3_pickups_descriptors(const application_q3_pickups *o,qa_qvm_saved_function *out,size_t count,qa_error *e)
{
    if(!o)return !count||application_fail(e,QA_ERROR_ARGUMENT,"Absent pickup owner has no descriptors");
    if(count!=o->count||(count&&!out)||!application_q3_pickups_idle(o))return application_fail(e,QA_ERROR_ARGUMENT,"Pickup descriptors require the actual returned owner");
    for(size_t i=0;i<count;++i)out[i]=o->hooks[i].descriptor;
    return true;
}
void application_q3_pickups_adopt(application_q3_pickups *o,const qa_qvm_binding *bindings)
{if(o)for(size_t i=0;i<o->count;++i)o->hooks[i].descriptor.binding=bindings[i];}
bool application_q3_pickups_checkpoint(const application_q3_pickups *o,qa_buffer *out,qa_error *e)
{
    if(!application_q3_pickups_idle(o)||!out||o->count!=o->capacity)return application_fail(e,QA_ERROR_ARGUMENT,"Pickup checkpoint requires its complete returned Source hook inventory");
    uint8_t *bytes=malloc(9);if(!bytes)return application_fail(e,QA_ERROR_MEMORY,"Encoding original pickup continuation");
    memcpy(bytes,"QAG3PU1",7);bytes[7]=o->closed;bytes[8]=o->own_free;*out=(qa_buffer){bytes,9};return true;
}
bool application_q3_pickups_restore(application_q3_pickups *o,qa_bytes bytes,qa_error *e)
{
    if(!application_q3_pickups_idle(o)||bytes.size!=9||memcmp(bytes.data,"QAG3PU1",7)||bytes.data[7]>1||bytes.data[8]!=(uint8_t)o->own_free)
        return application_fail(e,QA_ERROR_FORMAT,"Saved pickup continuation differs from its actual constructed free-hook owner");
    o->closed=bytes.data[7]!=0;return true;
}
