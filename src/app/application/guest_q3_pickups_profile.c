#include "guest_q3_pickups_profile.h"
#include "guest_q3_private.h"
#include "qa/json.h"

static bool u32(const qa_json_document *d, qa_json_id id, uint32_t *out, qa_error *e)
{
    uint64_t n;
    if (!qa_json_u64(d,id,&n,e)) return false;
    if (n>UINT32_MAX) return application_fail(e,QA_ERROR_FORMAT,"Pickup declaration exceeds its Source word");
    *out=(uint32_t)n; return true;
}
static bool integer(const qa_json_document *d,qa_json_id id,int32_t *out,qa_error *e)
{
    int64_t n;
    if(!qa_json_i64(d,id,&n,e))return false;
    if(n<0 || n>INT32_MAX)return application_fail(e,QA_ERROR_FORMAT,"Pickup type exceeds its Source enum");
    *out=(int32_t)n;return true;
}
static bool list(const qa_json_document *d,qa_json_id id,void **out,size_t *count,size_t width,qa_error *e)
{
    if(qa_json_type(d,id)!=QA_JSON_ARRAY)return application_fail(e,QA_ERROR_FORMAT,"Pickup declaration requires a counted list");
    size_t n=qa_json_size(d,id);
    if(n>SIZE_MAX/width)return application_fail(e,QA_ERROR_MEMORY,"Pickup declaration list overflows");
    void *p=n?calloc(n,width):NULL;
    if(n&&!p)return application_fail(e,QA_ERROR_MEMORY,"Retaining original pickup declaration");
    *out=p;*count=n;return true;
}
static bool same_json(const qa_json_document *d,qa_json_id a,qa_json_id b,qa_error *e)
{
    qa_json_kind kind=qa_json_type(d,a);
    if(kind!=qa_json_type(d,b)||kind==QA_JSON_INVALID)return false;
    if(kind==QA_JSON_OBJECT || kind==QA_JSON_ARRAY){
        size_t n=qa_json_size(d,a);
        if(n!=qa_json_size(d,b))return false;
        for(size_t i=0;i<n;++i){
            qa_json_id x=qa_json_at(d,a,i),y=qa_json_at(d,b,i);
            if(kind==QA_JSON_OBJECT){
                qa_buffer key={0};
                if(!qa_json_string(d,qa_json_key_at(d,a,i),&key,e))return false;
                y=qa_json_get(d,b,(const char *)key.data);qa_buffer_free(&key);
            }
            if(!same_json(d,x,y,e))return false;
        }
        return true;
    }
    if(kind==QA_JSON_STRING){qa_buffer x={0},y={0};bool ok=qa_json_string(d,a,&x,e)&&qa_json_string(d,b,&y,e);
        bool equal=ok&&x.size==y.size&&(!x.size||!memcmp(x.data,y.data,x.size));qa_buffer_free(&x);qa_buffer_free(&y);return equal;}
    if(kind==QA_JSON_NUMBER){double x,y;return qa_json_number(d,a,&x,e)&&qa_json_number(d,b,&y,e)&&x==y;}
    if(kind==QA_JSON_BOOL){bool x,y;return qa_json_bool(d,a,&x,e)&&qa_json_bool(d,b,&y,e)&&x==y;}
    return kind==QA_JSON_NULL;
}
static bool function(const qa_json_document *d,qa_json_id id,application_q3_pickup_function *f,qa_error *e)
{
    void *p=NULL;
    if(!u32(d,qa_json_get(d,id,"entry"),&f->entry,e)||
        !list(d,qa_json_get(d,id,"calls"),&p,&f->call_count,sizeof(*f->calls),e))return false;
    f->calls=p;
    for(size_t i=0;i<f->call_count;++i)if(!u32(d,qa_json_at(d,qa_json_get(d,id,"calls"),i),f->calls+i,e))return false;
    return true;
}
static bool evaluation(const qa_json_document *d,qa_json_id id,application_q3_pickup_grant *g,qa_error *e)
{
    void *p=NULL;qa_qvm_region_evaluation *r=&g->evaluation;
    if(!u32(d,qa_json_get(d,id,"entry"),&r->entry,e)||!u32(d,qa_json_get(d,id,"join"),&r->join,e)||
        !list(d,qa_json_get(d,id,"inputs"),&p,&r->input_count,sizeof(*g->inputs),e))return false;
    g->inputs=p;r->inputs=p;r->result_offset=-1;
    for(size_t i=0;i<r->input_count;++i)if(!u32(d,qa_json_at(d,qa_json_get(d,id,"inputs"),i),g->inputs+i,e))return false;
    if(qa_json_type(d,qa_json_get(d,id,"result"))!=QA_JSON_NULL){uint32_t n;
        if(!u32(d,qa_json_get(d,id,"result"),&n,e))return false;
        if(n>INT32_MAX)return application_fail(e,QA_ERROR_FORMAT,"Pickup result exceeds its Source local offset");
        r->result_offset=(int32_t)n;}
    return true;
}
static bool declared(q3g_role *role,qa_bytes bytes,application_q3_pickup_profile *p,qa_error *e)
{
    qa_json_document *d=NULL;if(!qa_json_parse(bytes,&d,e))return false;
    qa_json_id root=qa_json_root(d),at=qa_json_get(d,root,"pickups");
    if(at==QA_JSON_NONE||qa_json_type(d,at)==QA_JSON_NULL){qa_json_destroy(d);return true;}
    p->present=true;qa_json_id fields=qa_json_get(d,at,"fields"),gate=qa_json_get(d,at,"gate"),items=qa_json_get(d,at,"items");
    bool ok=same_json(d,items,qa_json_get(d,root,"items"),e);
    if(!ok&&(!e||e->code==QA_OK))application_fail(e,QA_ERROR_FORMAT,"Pickup table differs from the actual primary item catalog");
    const char *names[]={"inuse","client","health","item","count","flags"};
    uint32_t *words[]={&p->fields.inuse,&p->fields.client,&p->fields.health,&p->fields.item,&p->fields.count,&p->fields.flags};
    ok=ok&&u32(d,qa_json_get(d,at,"entityStride"),&p->entity_stride,e)&&u32(d,qa_json_get(d,at,"clientStride"),&p->client_stride,e);
    for(size_t i=0;ok&&i<6;++i)ok=u32(d,qa_json_get(d,fields,names[i]),words[i],e);
    ok=ok&&u32(d,qa_json_get(d,at,"droppedFlag"),&p->dropped_flag,e)&&u32(d,qa_json_get(d,at,"touch"),&p->touch,e)&&
        u32(d,qa_json_get(d,at,"free"),&p->free,e)&&function(d,gate,&p->gate,e)&&function(d,qa_json_get(d,at,"targets"),&p->targets,e)&&
        u32(d,qa_json_get(d,gate,"itemArgument"),&p->item_argument,e)&&u32(d,qa_json_get(d,gate,"playerArgument"),&p->player_argument,e)&&
        integer(d,qa_json_get(d,items,"weaponType"),&p->weapon_type,e)&&integer(d,qa_json_get(d,items,"ammoType"),&p->ammo_type,e);
    void *allocation=NULL;
    if(ok){ok=list(d,qa_json_get(d,at,"objectiveTypes"),&allocation,&p->objective_count,sizeof(*p->objective_types),e);p->objective_types=allocation;}
    for(size_t i=0;ok&&i<p->objective_count;++i)ok=integer(d,qa_json_at(d,qa_json_get(d,at,"objectiveTypes"),i),p->objective_types+i,e);
    allocation=NULL;
    if(ok){ok=list(d,qa_json_get(d,at,"grants"),&allocation,&p->grant_count,sizeof(*p->grants),e);p->grants=allocation;}
    for(size_t i=0;ok&&i<p->grant_count;++i){
        application_q3_pickup_grant *g=p->grants+i;qa_json_id row=qa_json_at(d,qa_json_get(d,at,"grants"),i),op=qa_json_get(d,row,"operation");
        ok=function(d,row,&g->function,e)&&integer(d,qa_json_get(d,row,"itemType"),&g->item_type,e);
        g->region=qa_json_string_equal(d,qa_json_get(d,op,"kind"),"region");
        if(ok&&!g->region)ok=qa_json_string_equal(d,qa_json_get(d,op,"kind"),"return")&&u32(d,qa_json_get(d,op,"acceptedReturn"),&g->accepted_return,e);
        if(ok&&g->region){
            ok=u32(d,qa_json_get(d,op,"entry"),&g->entry,e)&&u32(d,qa_json_get(d,op,"join"),&g->join,e)&&u32(d,qa_json_get(d,op,"quantity"),&g->quantity,e);
            qa_json_id weapon=qa_json_get(d,op,"weapon");g->weapon=weapon!=QA_JSON_NONE;
            if(ok&&g->weapon){qa_json_id storage=qa_json_get(d,weapon,"storage");g->inventory=storage!=QA_JSON_NONE;
                ok=(!g->inventory||qa_json_string_equal(d,storage,"inventory"))&&evaluation(d,qa_json_get(d,weapon,"quantity"),g,e);
                if(ok&&!g->inventory)ok=u32(d,qa_json_get(d,weapon,"bitsOffset"),&g->bits_offset,e)&&u32(d,qa_json_get(d,weapon,"ammoOffset"),&g->ammo_offset,e);}
        }
        g->eligibility=Q3_PICKUP_SOURCE_BRANCHES;allocation=NULL;
        qa_json_id branches=qa_json_get(d,qa_json_get(d,row,"eligibility"),"branches");
        if(ok){ok=list(d,branches,&allocation,&g->branch_count,sizeof(*g->branches),e);g->branches=allocation;}
        for(size_t j=0;ok&&j<g->branch_count;++j){qa_json_id b=qa_json_at(d,branches,j);
            ok=u32(d,qa_json_get(d,b,"instructionIndex"),&g->branches[j].instruction,e)&&qa_json_bool(d,qa_json_get(d,b,"taken"),&g->branches[j].taken,e);}
        if(!ok&&(!e||e->code==QA_OK))application_fail(e,QA_ERROR_FORMAT,"Pickup operation differs from its declared Source schema");
    }
    qa_json_destroy(d);(void)role;return ok;
}
static bool one_call(application_q3_pickup_function *f,uint32_t entry,uint32_t a,uint32_t b,qa_error *e)
{
    f->entry=entry;f->call_count=b?2:1;f->calls=malloc(f->call_count*sizeof(*f->calls));
    if(!f->calls)return application_fail(e,QA_ERROR_MEMORY,"Retaining exact pickup call sites");
    f->calls[0]=a;if(b)f->calls[1]=b;return true;
}
static bool known(q3g_role *role,application_q3_pickup_profile *p,qa_error *e)
{
    char digest[65];qa_sha256_hex(qa_qvm_image_digest(role->image),digest);
    bool stock=!strcmp(digest,"57c52bf22e4f528c064f8af1553a7103723bab0a02276bb11eed944bf829b219");
    bool three=!strcmp(digest,"9751bad99a2d138f96a9b0436d2ea2d965b86214175dc33e4cea95e059419337");
    if(!stock&&!three)return true;
    if(role->abi!=QA_QVM_Q3_MODERN)return application_fail(e,QA_ERROR_FORMAT,"Known pickup executable has a different ABI");
    p->present=true;p->entity_stride=stock?808:876;p->client_stride=stock?776:944;
    p->fields.inuse=520;p->fields.client=516;p->fields.health=732;p->fields.item=804;p->fields.count=760;p->fields.flags=536;
    p->dropped_flag=4096;p->weapon_type=1;p->ammo_type=2;p->item_argument=1;p->player_argument=2;
    p->touch=stock?103974:168574;p->free=stock?129805:211210;
    p->objective_count=1;p->objective_types=malloc(sizeof(*p->objective_types));
    p->grant_count=stock?2:3;p->grants=calloc(p->grant_count,sizeof(*p->grants));
    if(!p->objective_types||!p->grants)return application_fail(e,QA_ERROR_MEMORY,"Retaining exact pickup profile");
    p->objective_types[0]=8;
    if(!one_call(&p->gate,stock?65953:20482,stock?104007:168697,stock?0:168873,e)||
        !one_call(&p->targets,stock?129114:210519,stock?104340:169404,0,e))return false;
    application_q3_pickup_grant *armor=p->grants,*ammo=p->grants+1;
    armor->item_type=3;armor->accepted_return=stock?103658:167814;
    armor->eligibility=stock?Q3_PICKUP_ELIGIBLE:Q3_PICKUP_THREEWAVE_OWNER;
    ammo->item_type=2;ammo->accepted_return=103265;ammo->eligibility=stock?Q3_PICKUP_ELIGIBLE:Q3_PICKUP_THREEWAVE_LITHIUM;
    if(!one_call(&armor->function,stock?103594:167408,stock?104107:169094,0,e)||
        !one_call(&ammo->function,stock?103220:166848,stock?104091:169078,0,e))return false;
    if(three){ammo->region=true;ammo->entry=166875;ammo->join=166893;ammo->quantity=20;
        application_q3_pickup_grant *w=p->grants+2;w->item_type=1;w->region=true;w->weapon=true;
        w->entry=167099;w->join=167173;w->quantity=20;w->bits_offset=204;w->ammo_offset=376;
        w->evaluation=(qa_qvm_region_evaluation){.entry=166958,.join=167099,.result_offset=20};w->eligibility=Q3_PICKUP_THREEWAVE_LITHIUM;
        if(!one_call(&w->function,166897,169062,0,e))return false;}
    return true;
}
static bool qualify_function(const qa_qvm_instruction *code,size_t n,const application_q3_pickup_function *f,qa_error *e)
{
    if(f->entry>=n||code[f->entry].opcode!=QA_QVM_ENTER)return application_fail(e,QA_ERROR_FORMAT,"Pickup callback is not an actual OP_ENTER");
    for(size_t i=0;i<f->call_count;++i){uint32_t at=f->calls[i];
        if(!at||at>=n||code[at].opcode!=QA_QVM_CALL||code[at-1].opcode!=QA_QVM_CONST||code[at-1].operand!=(int32_t)f->entry)
            return application_fail(e,QA_ERROR_FORMAT,"Pickup call site differs from its actual Source target");}
    return true;
}
static bool qualify(application_q3_pickup_profile *p,qa_error *e)
{
    size_t n;const qa_qvm_instruction *code=qa_qvm_image_instructions(p->image,&n);
    if(p->entity_stride<qa_qvm_shared_entity_bytes(p->abi)||p->client_stride<qa_qvm_player_bytes(p->abi)||
        p->entity_stride%4||p->client_stride%4||p->entity_stride>qa_qvm_image_memory_size(p->image)||p->client_stride>qa_qvm_image_memory_size(p->image)||
        p->item_argument>=62||p->player_argument>=62||p->weapon_type==p->ammo_type)
        return application_fail(e,QA_ERROR_FORMAT,"Pickup records or arguments leave their original ABI");
    uint32_t fields[]={p->fields.inuse,p->fields.client,p->fields.health,p->fields.item,p->fields.count,p->fields.flags};
    for(size_t i=0;i<6;++i)if(fields[i]%4||fields[i]>p->entity_stride-4)return application_fail(e,QA_ERROR_FORMAT,"Pickup field leaves its actual entity record");
    if(p->touch>=n||p->free>=n||code[p->touch].opcode!=QA_QVM_ENTER||code[p->free].opcode!=QA_QVM_ENTER||
        !qualify_function(code,n,&p->gate,e)||!qualify_function(code,n,&p->targets,e))return application_fail(e,QA_ERROR_FORMAT,"Pickup lifecycle callback is not its actual Source function");
    uint32_t gate_end=p->gate.entry+1;while(gate_end<n&&code[gate_end].opcode!=QA_QVM_ENTER)++gate_end;
    for(size_t i=0;i<p->grant_count;++i){application_q3_pickup_grant *g=p->grants+i;
        for(size_t j=0;j<i;++j)if(p->grants[j].item_type==g->item_type||p->grants[j].function.entry==g->function.entry)
            return application_fail(e,QA_ERROR_FORMAT,"Pickup grant types or hook entries are ambiguous");
        if(g->function.entry==p->touch||g->function.entry==p->free||g->function.entry==p->gate.entry||g->function.entry==p->targets.entry)
            return application_fail(e,QA_ERROR_FORMAT,"Pickup grant aliases another lifecycle hook");
        if(!qualify_function(code,n,&g->function,e))return false;
        if(!g->region){uint32_t at=g->accepted_return,owner=at;
            if(at>=n||at+1>=n||code[at].opcode!=QA_QVM_CONST||!code[at].operand||code[at+1].opcode!=QA_QVM_LEAVE)return application_fail(e,QA_ERROR_FORMAT,"Pickup return is not a nonzero Source CONST/LEAVE");
            while(owner&&code[owner].opcode!=QA_QVM_ENTER)--owner;
            if(owner!=g->function.entry)return application_fail(e,QA_ERROR_FORMAT,"Pickup return belongs to another Source function");
            g->return_value=code[at].operand;
        }else{
            if(!qa_qvm_qualify_source_region(p->image,g->function.entry,g->entry,g->join,e))return false;
            int32_t frame=code[g->function.entry].operand;
            if(g->quantity<8||g->quantity%4||frame<4||g->quantity>(uint32_t)frame-4)return application_fail(e,QA_ERROR_FORMAT,"Pickup quantity leaves its actual Source local frame");
            if(g->weapon){if(!qa_qvm_qualify_region(p->image,g->function.entry,&g->evaluation,e))return false;
                if(!g->inventory&&(g->bits_offset%4||g->ammo_offset%4||g->bits_offset>p->client_stride-4||g->ammo_offset>p->client_stride-4))return application_fail(e,QA_ERROR_FORMAT,"Pickup weapon projection leaves its Source client");}
        }
        for(size_t j=0;j<g->branch_count;++j){uint32_t at=g->branches[j].instruction;
            if(at<=p->gate.entry||at>=gate_end||code[at].opcode<QA_QVM_EQ||code[at].opcode>QA_QVM_GEF)return application_fail(e,QA_ERROR_FORMAT,"Pickup eligibility branch leaves its original gate");
            for(size_t k=0;k<j;++k)if(g->branches[k].instruction==at)return application_fail(e,QA_ERROR_FORMAT,"Pickup eligibility repeats a Source branch");}
    }
    return true;
}
void application_q3_pickup_profile_free(application_q3_pickup_profile *p)
{
    if(!p)return;
    free(p->gate.calls);free(p->targets.calls);free(p->objective_types);
    for(size_t i=0;p->grants&&i<p->grant_count;++i){free(p->grants[i].function.calls);free(p->grants[i].inputs);free(p->grants[i].branches);}
    free(p->grants);*p=(application_q3_pickup_profile){0};
}
bool application_q3_pickup_profile_read(q3g_role *role,qa_bytes primary,application_q3_pickup_profile *out,qa_error *e)
{
    if(!role||role->kind!=QA_QVM_GAME||!role->image||!role->vm||!out)return application_fail(e,QA_ERROR_ARGUMENT,"Pickup profile requires the real retained GAME executable");
    application_q3_pickup_profile p={.image=role->image,.abi=role->abi};
    bool ok=primary.size?declared(role,primary,&p,e):known(role,&p,e);
    if(ok&&p.present)ok=qualify(&p,e);
    if(!ok){application_q3_pickup_profile_free(&p);return false;}*out=p;return true;
}
