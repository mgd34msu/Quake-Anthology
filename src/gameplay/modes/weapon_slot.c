#include "qa/equipment_weapon_slot.h"
#include <stdlib.h>

typedef struct slot_binding {
    qa_equipment_weapon_binding value;
    uint64_t serial;
    bool bound;
} slot_binding;
struct qa_weapon_slot {
    qa_actor_registry *registry;
    qa_actor_id actor;
    qa_actor_owner primary;
    qa_weapon_slot_state state;
    slot_binding *bindings;
    size_t count, capacity, calls;
    uint64_t serial, revision;
    bool restore_pending, cancelling;
};
typedef struct slot_witness { qa_actor_owner provider; uint64_t serial; } slot_witness;
static bool fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, message); return false;
}
static bool live(const qa_weapon_slot *s) {
    return s && qa_actors_get(s->registry, s->actor);
}
static slot_binding *binding(qa_weapon_slot *s, qa_actor_owner provider) {
    if (!live(s)) return NULL;
    for (size_t i=0;i<s->count;++i) {
        slot_binding *b=&s->bindings[i];
        if (b->bound && b->value.provider==provider) {
            qa_equipment_weapon_binding v=b->value; uint64_t serial=b->serial;
            if (!v.current(v.context,s->actor)) return NULL;
            b=&s->bindings[i];
            return live(s)&&b->bound&&b->serial==serial ? b : NULL;
        }
    }
    return NULL;
}
static slot_witness witness(const slot_binding *b) {
    return (slot_witness){b->value.provider,b->serial};
}
static bool same(qa_weapon_slot *s, slot_witness w) {
    slot_binding *b=binding(s,w.provider); return b&&b->serial==w.serial;
}
static bool binding_valid(const qa_equipment_weapon_binding *b) {
    return b&&b->provider&&b->current&&b->accepts&&b->select&&b->holster&&b->holstered&&b->resume&&
        (!b->source_input||(b->status&&b->cancel&&b->restore_request));
}
static bool state_valid(const qa_weapon_slot_state *s) {
    return s&&s->provider&&s->phase<=QA_WEAPON_SLOT_ACTIVATING&&
        (s->phase==QA_WEAPON_SLOT_ACTIVE ? !s->next_provider&&!s->next_item&&!s->request :
         s->next_provider&&s->next_item&&(s->phase==QA_WEAPON_SLOT_ACTIVATING ? s->request!=0 : s->request==0));
}
bool qa_weapon_slot_bind(qa_weapon_slot *s,const qa_equipment_weapon_binding *v,qa_error *e) {
    if(!live(s)||!binding_valid(v)||!v->current(v->context,s->actor)) return fail(e,"Weapon binding lacks its current source owner");
    for(size_t i=0;i<s->count;++i) if(s->bindings[i].bound&&s->bindings[i].value.provider==v->provider)
        return fail(e,"Weapon source owner is already bound");
    if(s->serial==UINT64_MAX) return fail(e,"Weapon binding identity exhausted");
    size_t i=0;while(i<s->count&&s->bindings[i].bound)++i;
    if(i==s->capacity) {
        size_t n=s->capacity?s->capacity*2:4;
        if(n<s->capacity||n>SIZE_MAX/sizeof(*s->bindings)) return fail(e,"Weapon binding extent exhausted");
        slot_binding *p=realloc(s->bindings,n*sizeof(*p));
        if(!p){qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining source weapon bindings");return false;}
        s->bindings=p;s->capacity=n;
    }
    s->bindings[i]=(slot_binding){.value=*v,.serial=++s->serial,.bound=true};
    if(i==s->count)++s->count;
    return true;
}
bool qa_weapon_slot_create(qa_actor_registry *r,qa_actor_id actor,const qa_equipment_weapon_binding *primary,
    const qa_weapon_slot_state *restored,qa_weapon_slot **out,qa_error *e) {
    if(!out||!r||!qa_actors_get(r,actor)||!binding_valid(primary)||(restored&&!state_valid(restored)))
        return fail(e,"Weapon slot requires its actual actor and primary handoff");
    qa_weapon_slot *s=calloc(1,sizeof(*s));
    if(!s){qa_error_set(e,QA_ERROR_MEMORY,0,"Creating source weapon slot");return false;}
    s->registry=r;s->actor=actor;s->primary=primary->provider;
    s->state=restored?*restored:(qa_weapon_slot_state){.provider=s->primary};s->restore_pending=restored!=NULL;
    if(!qa_weapon_slot_bind(s,primary,e)){free(s->bindings);free(s);return false;}
    *out=s;return true;
}
static bool cancel(qa_weapon_slot *s,qa_error *e) {
    if(s->state.phase!=QA_WEAPON_SLOT_ACTIVATING||s->cancelling) return true;
    slot_binding *b=binding(s,s->state.next_provider);
    if(!b) return true;
    qa_equipment_weapon_binding v=b->value;qa_weapon_slot_state state=s->state;
    s->cancelling=true;
    bool ok=v.cancel(v.context,s->actor,state.request,state.next_item,e);
    s->cancelling=false;return ok;
}
static bool resume(qa_weapon_slot *s,qa_actor_owner provider,qa_item_id item,slot_witness outgoing,qa_error *e) {
    slot_binding *b=binding(s,provider);
    if(!b)return fail(e,"Weapon resumption has no live source owner");
    qa_equipment_weapon_binding v=b->value;slot_witness w=witness(b);
    s->state=(qa_weapon_slot_state){.provider=provider};uint64_t revision=++s->revision;
    bool accepted=false;uint64_t request=0;
    if(!v.resume(v.context,s->actor,item,&accepted,&request,e))return false;
    if(!live(s)||s->revision!=revision||!same(s,w)) {
        return !v.source_input||!request||v.cancel(v.context,s->actor,request,item,e);
    }
    if(v.source_input) {
        if(!request)return fail(e,"Source input resumption returned no real request identity");
        qa_weapon_request_status status;
        if(!v.status(v.context,s->actor,request,item,&status,e))return false;
        if(!live(s)||s->revision!=revision||!same(s,w))return v.cancel(v.context,s->actor,request,item,e);
        if(status==QA_WEAPON_REQUEST_PENDING) {
            if(!item||!outgoing.provider||!same(s,outgoing)) {
                if(!v.cancel(v.context,s->actor,request,item,e))return false;
                return fail(e,"Deferred weapon activation lost its unchanged outgoing source");
            }
            s->state=(qa_weapon_slot_state){QA_WEAPON_SLOT_ACTIVATING,outgoing.provider,provider,item,request};
            ++s->revision;return true;
        }
        accepted=status==QA_WEAPON_REQUEST_ACCEPTED;
    }
    if(accepted)return true;
    if(outgoing.provider&&same(s,outgoing))return resume(s,outgoing.provider,0,(slot_witness){0},e);
    return fail(e,"Original source refused weapon resumption");
}
static bool fallback(qa_weapon_slot *s,qa_error *e) {return resume(s,s->primary,0,(slot_witness){0},e);}
bool qa_weapon_slot_validate_restore(qa_weapon_slot *s,qa_error *e) {
    if(!s||!s->restore_pending)return s!=NULL;
    slot_binding *out=binding(s,s->state.provider);
    if(!out)return fail(e,"Saved outgoing weapon source has not been rebound");
    slot_witness w=witness(out);qa_weapon_slot_state state=s->state;uint64_t revision=s->revision;
    if(out->value.read) {
        qa_weapon_presentation p;
        if(!out->value.read(out->value.context,s->actor,&p,e)||p.provider!=w.provider||!same(s,w))
            return fail(e,"Saved weapon presentation changed its bound owner");
        if(p.active) {bool accepted=false;out=binding(s,w.provider);
            if(!out||!out->value.accepts(out->value.context,s->actor,p.active,&accepted,e)||!accepted||!same(s,w))
                return fail(e,"Saved active weapon is not declared by its source");}
    }
    if(state.phase!=QA_WEAPON_SLOT_ACTIVE) {
        slot_binding *in=binding(s,state.next_provider);bool accepted=false;
        if(!in)return fail(e,"Saved incoming weapon source has not been rebound");
        slot_witness next=witness(in);qa_equipment_weapon_binding v=in->value;
        if(!v.accepts(v.context,s->actor,state.next_item,&accepted,e)||!accepted||!same(s,next))
            return fail(e,"Saved requested weapon is not admitted by its source");
        if(state.phase==QA_WEAPON_SLOT_ACTIVATING&&(!v.source_input||
            !v.restore_request(v.context,s->actor,state.request,state.next_item,e)||!same(s,next)))
            return fail(e,"Saved activation differs from its original input request");
    }
    if(!live(s)||revision!=s->revision)return fail(e,"Saved weapon slot changed during qualification");
    s->restore_pending=false;return true;
}
static bool activation_reconcile(qa_weapon_slot *s,qa_error *e) {
    if(s->cancelling)return true;
    slot_binding *out=binding(s,s->state.provider),*in=binding(s,s->state.next_provider);
    if(!out){if(!cancel(s,e))return false;return fallback(s,e);}
    if(!in){qa_actor_owner owner=s->state.provider;if(!cancel(s,e))return false;return resume(s,owner,0,(slot_witness){0},e);}
    slot_witness a=witness(out),b=witness(in);qa_equipment_weapon_binding v=in->value;
    qa_weapon_slot_state state=s->state;uint64_t revision=s->revision;qa_weapon_request_status status;
    if(!v.status(v.context,s->actor,state.request,state.next_item,&status,e))return false;
    if(!live(s)||revision!=s->revision||!same(s,a)||!same(s,b))return true;
    if(status==QA_WEAPON_REQUEST_PENDING)return true;
    if(status==QA_WEAPON_REQUEST_ACCEPTED){s->state=(qa_weapon_slot_state){.provider=state.next_provider};++s->revision;return true;}
    return resume(s,state.provider,0,(slot_witness){0},e);
}
bool qa_weapon_slot_reconcile(qa_weapon_slot *s,qa_error *e) {
    if(!qa_weapon_slot_validate_restore(s,e))return false;
    if(!live(s))return true;
    if(s->state.phase==QA_WEAPON_SLOT_ACTIVATING)return activation_reconcile(s,e);
    slot_binding *out=binding(s,s->state.provider);
    if(!out)return fallback(s,e);
    if(s->state.phase==QA_WEAPON_SLOT_ACTIVE)return true;
    slot_witness a=witness(out);slot_binding *in=binding(s,s->state.next_provider);
    qa_weapon_slot_state state=s->state;uint64_t revision=s->revision;bool accepted=false;
    if(!in)return resume(s,state.provider,0,(slot_witness){0},e);
    slot_witness b=witness(in);qa_equipment_weapon_binding v=in->value;
    if(!v.accepts(v.context,s->actor,state.next_item,&accepted,e))return false;
    if(!live(s)||revision!=s->revision)return true;
    if(!accepted||!same(s,b))return resume(s,state.provider,0,(slot_witness){0},e);
    out=binding(s,state.provider);if(!out||out->serial!=a.serial)return true;
    bool holstered=false;v=out->value;
    if(!v.holstered(v.context,s->actor,&holstered,e))return false;
    return !holstered||revision!=s->revision||!same(s,a)||!same(s,b)||resume(s,state.next_provider,state.next_item,a,e);
}
bool qa_weapon_slot_request(qa_weapon_slot *s,qa_actor_owner provider,qa_item_id item,bool *accepted,qa_error *e) {
    if(!accepted||!provider||!item||!qa_weapon_slot_validate_restore(s,e))return false;
    *accepted=false;if(!live(s))return true;
    slot_binding *in=binding(s,provider);if(!in)return true;
    slot_witness w=witness(in);qa_equipment_weapon_binding v=in->value;bool allowed=false;
    if(!v.accepts(v.context,s->actor,item,&allowed,e))return false;
    if(!allowed||!same(s,w))return true;
    if(s->state.phase==QA_WEAPON_SLOT_ACTIVATING&&!activation_reconcile(s,e))return false;
    qa_weapon_slot_state state=s->state;
    if(state.phase==QA_WEAPON_SLOT_ACTIVATING) {
        if(state.next_provider==provider&&state.next_item==item){*accepted=true;return true;}
        uint64_t revision=s->revision;if(!cancel(s,e))return false;
        if(!live(s)||revision!=s->revision||!same(s,w))return true;
        if(provider==state.provider){if(!resume(s,provider,item,(slot_witness){0},e))return false;}
        else {s->state=(qa_weapon_slot_state){QA_WEAPON_SLOT_SWITCHING,state.provider,provider,item,0};++s->revision;}
        *accepted=true;return true;
    }
    if(state.phase==QA_WEAPON_SLOT_SWITCHING){s->state.next_provider=provider;s->state.next_item=item;++s->revision;*accepted=true;return true;}
    if(state.provider==provider) {
        if(!v.select(v.context,s->actor,item,accepted,e))return false;
        *accepted=*accepted&&same(s,w);return true;
    }
    slot_binding *out=binding(s,state.provider);
    if(!out)return fallback(s,e);
    v=out->value;s->state=(qa_weapon_slot_state){QA_WEAPON_SLOT_SWITCHING,state.provider,provider,item,0};++s->revision;
    if(!v.holster(v.context,s->actor,e))return false;
    *accepted=true;return true;
}
bool qa_weapon_slot_unbind(qa_weapon_slot *s,qa_actor_owner provider,void *context,qa_error *e) {
    if(!s||provider==s->primary)return fail(e,"Primary weapon binding belongs to its slot lifetime");
    for(size_t i=0;i<s->count;++i)if(s->bindings[i].bound&&s->bindings[i].value.provider==provider&&s->bindings[i].value.context==context) {
        /* Cancel against the real outgoing capability before retiring its row. */
        qa_weapon_slot_state state=s->state;
        if(!s->restore_pending&&(state.provider==provider||state.next_provider==provider)&&!cancel(s,e))return false;
        s->bindings[i].bound=false;
        if(!live(s)||s->restore_pending)return true;
        if(state.provider==provider)return fallback(s,e);
        if(state.phase!=QA_WEAPON_SLOT_ACTIVE&&state.next_provider==provider)return resume(s,state.provider,0,(slot_witness){0},e);
        return true;
    }
    return true;
}
bool qa_weapon_slot_idle(const qa_weapon_slot *s){return s&&!s->calls&&!s->cancelling;}
bool qa_weapon_slot_destroy(qa_weapon_slot **in,qa_error *e) {
    if(!in||!*in)return true;qa_weapon_slot *s=*in;
    if(!qa_weapon_slot_idle(s))return fail(e,"Weapon slot retains a source callback");
    if(!cancel(s,e))return false;
    free(s->bindings);free(s);*in=NULL;return true;
}
bool qa_weapon_slot_selected(const qa_weapon_slot *s,qa_actor_owner provider) {
    return live(s)&&!s->restore_pending&&s->state.phase==QA_WEAPON_SLOT_ACTIVE&&s->state.provider==provider&&binding((qa_weapon_slot *)s,provider);
}
bool qa_weapon_slot_presented(const qa_weapon_slot *s,qa_actor_owner provider) {
    return live(s)&&!s->restore_pending&&s->state.provider==provider&&binding((qa_weapon_slot *)s,provider);
}
bool qa_weapon_slot_primary_selected(const qa_weapon_slot *s){return s&&qa_weapon_slot_selected(s,s->primary);}
bool qa_weapon_slot_snapshot(const qa_weapon_slot *s,qa_weapon_slot_state *out) {
    if(!s||!out||!qa_weapon_slot_idle(s))return false;*out=s->state;return true;
}
size_t qa_weapon_slot_binding_count(const qa_weapon_slot *s){return s?s->count:0;}
bool qa_weapon_slot_presentation(qa_weapon_slot *s,size_t i,qa_weapon_presentation *out,bool *found,qa_error *e) {
    if(!s||!out||!found||i>=s->count)return fail(e,"Weapon presentation index exceeds its source registry");
    *found=false;slot_binding *b=&s->bindings[i];if(!b->bound||!b->value.read)return true;
    qa_equipment_weapon_binding v=b->value;slot_witness w=witness(b);
    if(!same(s,w))return true;
    if(!v.read(v.context,s->actor,out,e))return false;
    if(out->provider!=v.provider)return fail(e,"Weapon presentation belongs to another source");
    *found=same(s,w);return true;
}
bool qa_weapon_slot_state_fields(qa_source_save_io *io,qa_weapon_slot_state *s) {
    uint32_t phase=(uint32_t)s->phase;
    if(!qa_source_save_u32(io,&phase)||!qa_source_save_string(io,&s->provider)||
        !qa_source_save_string(io,&s->next_provider)||!qa_source_save_string(io,&s->next_item)||!qa_source_save_u64(io,&s->request))return false;
    if(io->direction==QA_SOURCE_SAVE_READ)s->phase=(qa_weapon_slot_phase)phase;
    return state_valid(s)||fail(io->error,"Invalid saved source weapon transition");
}
