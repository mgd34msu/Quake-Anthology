#include "guest_q3_mod_private.h"

static bool prefix(application_q3_mod *o, qa_source_save_io *io)
{
    uint8_t magic[4]={'Q','G','M','D'}; uint32_t version=1,abi=(uint32_t)o->profile->abi;
    uint8_t expected[4]; memcpy(expected,magic,4);
    uint8_t digest[32]; memcpy(digest,qa_qvm_image_digest(o->profile->image),sizeof(digest));
    uint8_t original_digest[32]; memcpy(original_digest,digest,sizeof(digest));
    size_t bytes=o->profile->declaration.size;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,expected,4) ||
        !qa_source_save_u32(io,&version) || version!=1 || !qa_source_save_u32(io,&abi) || abi!=(uint32_t)o->profile->abi ||
        !qa_source_save_bytes(io,digest,sizeof(digest)) || memcmp(digest,original_digest,sizeof(digest)) ||
        !qa_source_save_count(io,&bytes,o->profile->declaration.size) || bytes!=o->profile->declaration.size)
        return q3mod_fail(io->error,QA_ERROR_FORMAT,"Generic source continuation header differs from its actual declaration");
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,o->profile->declaration.data,bytes);
    if (io->offset>io->input.size || bytes>io->input.size-io->offset || memcmp(io->input.data+io->offset,o->profile->declaration.data,bytes))
        return q3mod_fail(io->error,QA_ERROR_FORMAT,"Generic saved child differs from its actual retained declaration");
    io->offset+=bytes; return true;
}
bool application_q3_mod_checkpoint(application_q3_mod *o, qa_buffer *out, qa_error *e)
{
    if (!o || !out || out->data || out->size || !application_q3_mod_idle(o) || !application_q3_mod_validate(o,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic source checkpoint requires its actual idle installed owner");
    qa_source_save_io io;
    if (!qa_source_save_writer(&io,o->session,e)) return false;
    size_t count=0; for (mod_actor_channel *c=o->channels;c;c=c->next) ++count;
    bool active=o->active, callbacks=o->callbacks_active;
    bool ok=prefix(o,&io) && qa_source_save_bool(&io,&active) && qa_source_save_bool(&io,&callbacks) && qa_source_save_count(&io,&count,UINT32_MAX);
    for (mod_actor_channel *c=o->channels;ok && c;c=c->next) {
        size_t definition=c->definition; qa_actor_id actor=c->actor; int32_t client=c->client;
        uint64_t serial=c->lease.serial; bool bound=c->bound;
        qa_string_id rule=o->profile->protection[definition].claim.rule;
        ok=qa_source_save_actor(&io,&actor) && qa_source_save_i32(&io,&client) &&
            qa_source_save_count(&io,&definition,o->profile->protection_count) && qa_source_save_string(&io,&rule) &&
            qa_source_save_u64(&io,&serial) && qa_source_save_bool(&io,&bound);
    }
    if (ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool application_q3_mod_restore(application_q3_mod *o, qa_bytes bytes, qa_error *e)
{
    if (!o || !o->restoring || o->channels || o->callbacks || !application_q3_mod_idle(o))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic child restore requires its fresh detached source owner");
    qa_source_save_io io; if (!qa_source_save_reader(&io,o->session,bytes,e)) return false;
    bool active=false, callbacks=false; size_t count=0;
    bool ok=prefix(o,&io) && qa_source_save_bool(&io,&active) && qa_source_save_bool(&io,&callbacks) && qa_source_save_count(&io,&count,UINT32_MAX);
    mod_actor_channel *head=NULL, **tail=&head;
    for (size_t i=0;ok && i<count;++i) {
        mod_actor_channel *c=calloc(1,sizeof(*c));
        if (!c) { ok=q3mod_fail(e,QA_ERROR_MEMORY,"Owning detached source protection rows"); break; }
        c->owner=o; qa_string_id rule=0;
        ok=qa_source_save_actor(&io,&c->actor) && qa_source_save_i32(&io,&c->client) &&
            qa_source_save_count(&io,&c->definition,o->profile->protection_count) && qa_source_save_string(&io,&rule) &&
            qa_source_save_u64(&io,&c->lease.serial) && qa_source_save_bool(&io,&c->bound);
        if (ok && (c->definition>=o->profile->protection_count || !c->lease.serial || c->client<0 ||
            (uint32_t)c->client>=o->profile->maximum || !c->actor.registry ||
            !qa_actors_get(qa_session_actors(o->session),c->actor) ||
            rule!=o->profile->protection[c->definition].claim.rule || (c->bound && !active)))
            ok=q3mod_fail(e,QA_ERROR_FORMAT,"Saved protection row has no exact source definition or actor");
        for (mod_actor_channel *prior=head;ok && prior;prior=prior->next) if
            ((prior->definition==c->definition && qa_actor_id_equal(prior->actor,c->actor)) || prior->lease.serial==c->lease.serial)
                ok=q3mod_fail(e,QA_ERROR_FORMAT,"Saved protection rows duplicate physical ownership");
        if (!ok) { free(c); break; }
        c->lease.actor=c->actor; c->lease.channel=o->profile->protection[c->definition].channel;
        *tail=c; tail=&c->next;
    }
    if (ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok) { while (head) { mod_actor_channel *next=head->next; free(head); head=next; } return false; }
    o->channels=head; o->active=active; o->restored_callbacks=callbacks; return true;
}
static bool channels_current(application_q3_mod *o, qa_error *e)
{
    if (!q3mod_current(o,e)) return false;
    for (mod_actor_channel *c=o->channels;c;c=c->next) {
        int32_t client; qa_actor_owner owner=0;
        if (!qa_actors_get(qa_session_actors(o->session),c->actor) ||
            !o->services.client_slot(o->services.context,c->actor,&client,e) || client!=c->client ||
            !qa_combat_protection_current(o->combat,c->lease) ||
            !qa_combat_protection_owner(o->combat,c->actor,c->lease.channel,&owner,NULL) || owner!=o->owner ||
            qa_combat_protection_bound(o->combat,c->lease)!=c->bound)
            return q3mod_fail(e,QA_ERROR_ARGUMENT,"Canonical protection graph differs from its saved source lease");
        if (c->bound) { qa_armor actual={0}; if (!q3mod_protection_read(c,&actual,e)) return false; }
    }
    return true;
}
bool application_q3_mod_validate(application_q3_mod *o, qa_error *e)
{
    if (!channels_current(o,e)) return false;
    for (size_t i=0;i<o->profile->callback_count;++i) if (!o->callbacks || !o->callbacks[i].registration) {
        if (o->callbacks_active) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source callback continuation has incomplete actual registrations");
    } else if (!o->callbacks_active) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Inactive source callback owner retains a physical operation hook");
    return true;
}
bool application_q3_mod_activate(application_q3_mod *o, qa_error *e)
{
    if (!o || o->closing || !application_q3_mod_idle(o)) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic activation requires its real idle source owner");
    bool restoring=o->restoring, active=o->active; o->restoring=false;
    if (!q3mod_current(o,e) || (restoring && !channels_current(o,e))) {
        o->restoring=restoring; return false;
    }
    o->active=restoring?active:true;
    for (mod_actor_channel *c=o->channels;c;c=c->next) if (!application_q3_mod_admit(o,c->actor,e)) return false;
    return true;
}
bool application_q3_mod_callbacks_register(application_q3_mod *o, qa_error *e)
{
    if (!q3mod_current(o,e) || !application_q3_mod_idle(o))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Callback registration requires its actual installed idle source owner");
    if (o->restored_owner && !o->restored_callbacks) return true;
    return q3mod_callbacks_activate(o,e);
}
