#include "guest_q3_mod_items_private.h"

static bool prefix(application_q3_mod_items *o,qa_source_save_io *io)
{
    uint8_t magic[4]={'Q','G','I','T'},expected[4];memcpy(expected,magic,4);
    uint32_t version=1,abi=(uint32_t)o->profile->source->abi;
    uint8_t digest[32],original[32];
    memcpy(digest,qa_qvm_image_digest(o->profile->source->image),32);memcpy(original,digest,32);
    size_t bytes=o->profile->source->declaration.size;
    if(!qa_source_save_bytes(io,magic,4)||memcmp(magic,expected,4)||
        !qa_source_save_u32(io,&version)||version!=1||!qa_source_save_u32(io,&abi)||abi!=(uint32_t)o->profile->source->abi||
        !qa_source_save_bytes(io,digest,32)||memcmp(digest,original,32)||
        !qa_source_save_count(io,&bytes,o->profile->source->declaration.size)||bytes!=o->profile->source->declaration.size)
        return q3mod_fail(io->error,QA_ERROR_FORMAT,"Saved items differ from their genuine source profile");
    if(io->direction==QA_SOURCE_SAVE_WRITE)return qa_source_save_bytes(io,o->profile->source->declaration.data,bytes);
    if(io->offset>io->input.size||bytes>io->input.size-io->offset||
        memcmp(io->input.data+io->offset,o->profile->source->declaration.data,bytes))return false;
    io->offset+=bytes;return true;
}
static bool record_used(application_q3_mod_items_profile *p,size_t record)
{
    for(size_t i=0;i<p->storage_count;++i){const item_storage *s=p->storage+i;
        if(s->field.record==record||(!s->bits&&s->capacity.kind==ITEM_CAPACITY_FIELD&&s->capacity.field.record==record))return true;
    }
    return false;
}
static size_t record_count(application_q3_mod_items_profile *p)
{size_t count=0;for(size_t i=0;i<p->source->record_count;++i)if(record_used(p,i))++count;return count;}
static bool request_valid(application_q3_mod_items *o,const item_actor *a)
{
    if(!a->request.id)return !a->request.item;
    if(!o->profile->stage||a->request.id>o->next_request||
        !qa_actor_id_equal(a->request.actor,a->actor)||(unsigned)a->status>Q3_ITEM_REQUEST_REFUSED)return false;
    if(!a->request.item)return true;
    for(size_t i=0;i<o->profile->stage->value_count;++i)
        if(o->profile->stage->values[i].item==a->request.item)return true;
    return false;
}
static bool fields(application_q3_mod_items *o,item_actor *a,qa_source_save_io *io)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    qa_qvm_binding watch=reading?0:a->watch;
    uint32_t status=(uint32_t)a->status;
    if(!qa_source_save_actor(io,&a->actor)||!qa_source_save_u64(io,&a->lease.serial)||!a->lease.serial||
        !qa_source_save_u64(io,&watch)||watch<=1||!qa_source_save_bool(io,&a->weapon_bound)||
        !qa_source_save_u64(io,&a->request.id)||!qa_source_save_string(io,&a->request.item)||
        !qa_source_save_u32(io,&status)||status>Q3_ITEM_REQUEST_REFUSED||
        !qa_source_save_count(io,&a->address_count,o->profile->source->record_count)||
        a->address_count!=record_count(o->profile))return false;
    if(reading){a->saved_watch=watch;a->lease.actor=a->actor;a->status=(application_q3_item_request_status)status;
        if(a->request.id)a->request.actor=a->actor;
        a->addresses=calloc(a->address_count,sizeof(*a->addresses));
        if(a->address_count&&!a->addresses)return q3mod_fail(io->error,QA_ERROR_MEMORY,"Retaining restored item source addresses");
    }
    for(size_t i=0;i<a->address_count;++i){item_record_address *r=a->addresses+i;
        if(!qa_source_save_count(io,&r->record,o->profile->source->record_count)||
            r->record>=o->profile->source->record_count||!record_used(o->profile,r->record)||
            !qa_source_save_u32(io,&r->address))return false;
        for(size_t j=0;j<i;++j)if(a->addresses[j].record==r->record)return false;
        const mod_record *record=o->profile->source->records+r->record;
        uint64_t end=(uint64_t)record->address+(uint64_t)record->stride*record->capacity;
        if(r->address<record->address||r->address>=end||(r->address-record->address)%record->stride)return false;
    }
    return qa_actors_get(qa_session_actors(o->mod->session),a->actor)&&
        (!a->weapon_bound||o->profile->stage)&&request_valid(o,a);
}
bool application_q3_mod_items_checkpoint(application_q3_mod_items *o,qa_buffer *out,qa_error *e)
{
    if(!o||!out||out->data||out->size||!application_q3_mod_items_idle(o)||!q3mod_storage_current(o->mod,e))return false;
    qa_source_save_io io;if(!qa_source_save_writer(&io,o->mod->session,e))return false;
    size_t count=0;for(item_actor *a=o->actors;a;a=a->next)++count;
    bool ok=prefix(o,&io)&&qa_source_save_u64(&io,&o->next_request)&&qa_source_save_count(&io,&count,UINT32_MAX);
    for(item_actor *a=o->actors;ok&&a;a=a->next){qa_qvm_saved_write_watch watch;
        ok=q3items_current(a,e)&&(!o->profile->stage||a->weapon_bound)&&
            qa_qvm_write_watch_read(o->mod->vm,a->watch,&watch,e)&&fields(o,a,&io);
    }
    if(ok)ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return ok;
}
bool application_q3_mod_items_restore(application_q3_mod_items *o,qa_bytes bytes,qa_error *e)
{
    if(!o||!o->mod->restoring||o->actors||o->next_request||!application_q3_mod_items_idle(o)||!q3mod_storage_current(o->mod,e))return false;
    qa_source_save_io io;if(!qa_source_save_reader(&io,o->mod->session,bytes,e))return false;
    size_t count=0;bool ok=prefix(o,&io)&&qa_source_save_u64(&io,&o->next_request)&&qa_source_save_count(&io,&count,UINT32_MAX);
    item_actor **tail=&o->actors;
    for(size_t i=0;ok&&i<count;++i){item_actor *a=calloc(1,sizeof(*a));
        if(!a){ok=q3mod_fail(e,QA_ERROR_MEMORY,"Owning restored item continuation");break;}
        a->owner=o;*tail=a;tail=&a->next;
        ok=fields(o,a,&io);
        for(item_actor *prior=o->actors;ok&&prior!=a;prior=prior->next)
            if(qa_actor_id_equal(prior->actor,a->actor)||prior->lease.serial==a->lease.serial||
                prior->saved_watch==a->saved_watch||(a->request.id&&prior->request.id==a->request.id))ok=false;
        qa_inventory_items binding;
        if(ok)ok=q3items_binding(a,&binding,e)&&q3items_watch_create(a,e);
    }
    if(ok)ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);return ok;
}
size_t application_q3_mod_items_watch_count(const application_q3_mod_items *o)
{size_t count=0;for(item_actor *a=o?o->actors:NULL;a;a=a->next)if(a->watch)++count;return count;}
bool application_q3_mod_items_watch(const application_q3_mod_items *o,size_t i,
    qa_qvm_saved_write_watch *out,qa_qvm_binding *saved,qa_error *e)
{
    if(!o||!out||!saved||!application_q3_mod_items_idle(o))return false;
    for(item_actor *a=o->actors;a;a=a->next)if(a->watch){if(i--==0){*saved=a->saved_watch?a->saved_watch:a->watch;return qa_qvm_write_watch_read(o->mod->vm,a->watch,out,e);}}
    return q3mod_fail(e,QA_ERROR_ARGUMENT,"Item watch inventory leaves actual source rows");
}
bool application_q3_mod_items_watches_adopt(application_q3_mod_items *o,qa_error *e)
{
    if(!o||!o->mod->restoring||!application_q3_mod_items_idle(o))return false;
    for(item_actor *a=o->actors;a;a=a->next){qa_qvm_saved_write_watch watch;
        if(!a->saved_watch||!qa_qvm_write_watch_read(o->mod->vm,a->saved_watch,&watch,e)||watch.context!=a)return false;
    }
    for(item_actor *a=o->actors;a;a=a->next){a->watch=a->saved_watch;a->saved_watch=0;}
    return true;
}
bool application_q3_mod_items_inventory_group(application_q3_mod_items *o,qa_actor_id actor,
    uint64_t serial,const qa_inventory_source_group *saved,qa_inventory_items *out,qa_error *e)
{
    item_actor *a=q3items_actor(o,actor);
    if(!o||!o->mod->restoring||!a||a->lease.serial!=serial||!saved||!out||
        saved->definitions_only||saved->owner!=o->mod->owner||saved->count!=o->profile->definition_count||
        !saved->items||!q3items_binding(a,out,e))return false;
    for(size_t i=0;i<saved->count;++i){const qa_item_admission *x=saved->items+i,*y=out->items+i;
        if(x->replace_primary!=y->replace_primary||x->definition.item!=y->definition.item||
            x->definition.ammo!=y->definition.ammo||x->definition.owner!=y->definition.owner||
            x->definition.weapon!=y->definition.weapon||x->definition.actions!=y->definition.actions||
            !x->definition.label||strcmp(x->definition.label,y->definition.label))return false;
    }
    return true;
}
